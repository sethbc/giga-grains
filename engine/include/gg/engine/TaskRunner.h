// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_ENGINE_TASKRUNNER_H
#define GG_ENGINE_TASKRUNNER_H

#include <cstdint>

namespace gg
{
/// One task of a run: called once with each index below the run's count. It must be real-time
/// safe, and it may be called on any thread, so it touches only what its index owns and sets
/// its own floating-point mode (ScopedDenormalsOff) for its duration (spec §6.7).
using TaskFn = void (*)(void *ctx, uint32_t index) noexcept;

/// Runs a list of independent tasks, on the calling thread or spread over a thread pool
/// (spec §6.7). Framework-free: the plugin's runner wraps the host's CLAP thread pool, and
/// SerialRunner loops when there is none or the host declines.
class TaskRunner
{
  public:
    virtual ~TaskRunner() = default;

    /// Runs fn(ctx, i) for every i < count and returns when all have returned. count may be 0.
    virtual void run(uint32_t count, TaskFn fn, void *ctx) noexcept = 0;
};

/// Runs every task on the calling thread, in index order. Real-time safe: it allocates, locks
/// and waits on nothing of its own.
class SerialRunner final : public TaskRunner
{
  public:
    void run(uint32_t count, TaskFn fn, void *ctx) noexcept override
    {
        for (uint32_t i = 0; i < count; ++i)
            fn(ctx, i);
    }
};
} // namespace gg

#endif // GG_ENGINE_TASKRUNNER_H
