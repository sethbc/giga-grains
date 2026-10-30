// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_TESTS_SUPPORT_STUBPOOL_H
#define GG_TESTS_SUPPORT_STUBPOOL_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <sst/cpputils/rtsan_support.h>

#include "gg/engine/TaskRunner.h"

namespace gg::test
{
/// A stand-in for a host's thread pool (spec §6.7), for the tests only, off the audio path: the
/// thread-pool invariance test renders through it and through SerialRunner and compares bytes.
///
/// The constructor starts `workers` std::threads, which sleep until a run() and are joined by
/// the destructor. run() publishes the task list, wakes them, and then the workers and the
/// calling thread take indices from one atomic counter until none is left, so every index runs
/// exactly once on some thread, in no fixed order. run() returns once every index has returned
/// and every worker has finished with the list (a barrier), so the next run() starts clean;
/// each run() is usable any number of times and allocates nothing. One thread calls run() at a
/// time, as one audio thread does.
///
/// With declineEveryOther, every second run() (the 2nd, the 4th, …) runs the whole list on the
/// calling thread instead, in index order: a host that declines the request, after which CLAP
/// says the plugin loops itself.
///
/// It sets no floating-point mode on its threads: each task sets its own (ScopedDenormalsOff),
/// and this stub must not hide a task that forgets. It isn't real-time safe (run() locks and
/// waits), so nothing of it carries SST_CPPUTILS_NONBLOCKING. Under RealtimeSanitizer, run()'s
/// own locking and waiting are exempt (SST_CPPUTILS_RTSAN_DISABLE), as a host's request_exec is
/// the host's: called from a nonblocking Processor::process(), the stub stands for the host, and
/// only the tasks it runs, on the calling thread and on its own (each a nonblocking function),
/// are checked.
class StubPool final : public gg::TaskRunner
{
  public:
    explicit StubPool(unsigned workers, bool declineEveryOther = false)
        : workers_(workers), declineEveryOther_(declineEveryOther)
    {
        threads_.reserve(workers);
        for (unsigned w = 0; w < workers; ++w)
            threads_.emplace_back([this] { workerLoop(); });
    }

    ~StubPool() override
    {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        for (auto &t : threads_)
            t.join();
    }

    StubPool(const StubPool &) = delete;
    StubPool &operator=(const StubPool &) = delete;

    void run(uint32_t count, gg::TaskFn fn, void *ctx) noexcept override
    {
        ++calls_;
        if (count == 0)
            return;
        if (workers_ == 0 || (declineEveryOther_ && calls_ % 2 == 0))
        {
            for (uint32_t i = 0; i < count; ++i)
                fn(ctx, i);
            return;
        }

        {
            SST_CPPUTILS_RTSAN_DISABLE; // the host's side of the request (the class comment)
            {
                std::lock_guard lock(mutex_);
                count_ = count;
                fn_ = fn;
                ctx_ = ctx;
                next_.store(0, std::memory_order_relaxed);
                finished_ = 0;
                ++generation_;
            }
            wake_.notify_all();
        }

        drain(count, fn, ctx);

        SST_CPPUTILS_RTSAN_DISABLE;
        std::unique_lock lock(mutex_);
        done_.wait(lock, [this] { return finished_ == workers_; });
    }

  private:
    void drain(uint32_t count, gg::TaskFn fn, void *ctx) noexcept
    {
        for (uint32_t i = next_.fetch_add(1, std::memory_order_relaxed); i < count;
             i = next_.fetch_add(1, std::memory_order_relaxed))
            fn(ctx, i);
    }

    void workerLoop()
    {
        uint64_t seen = 0;
        std::unique_lock lock(mutex_);
        for (;;)
        {
            wake_.wait(lock, [&] { return stop_ || generation_ != seen; });
            if (stop_)
                return;
            seen = generation_;
            const uint32_t count = count_;
            const gg::TaskFn fn = fn_;
            void *const ctx = ctx_;
            lock.unlock();

            drain(count, fn, ctx);

            lock.lock();
            if (++finished_ == workers_)
                done_.notify_one();
        }
    }

    const unsigned workers_;
    const bool declineEveryOther_;
    uint64_t calls_{0}; ///< run() calls so far; only the calling thread touches it.

    std::mutex mutex_;
    std::condition_variable wake_; ///< Workers wait here for a new generation or stop_.
    std::condition_variable done_; ///< run() waits here for every worker to finish the list.

    // Guarded by mutex_.
    uint64_t generation_{0};
    uint32_t count_{0};
    gg::TaskFn fn_{nullptr};
    void *ctx_{nullptr};
    unsigned finished_{0}; ///< Workers done with the current list.
    bool stop_{false};

    std::atomic<uint32_t> next_{0}; ///< The next index to hand out.

    std::vector<std::thread> threads_;
};
} // namespace gg::test

#endif // GG_TESTS_SUPPORT_STUBPOOL_H
