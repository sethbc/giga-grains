// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The layer tasks on the TaskRunner (M2 sub-project 1's spec §6.7; programme design §7.4): the
// Processor renders each rendering layer's piece as one task, so the four-layer renders must be
// the same bytes on the serial runner and on any pool, one that declines included; one rendering
// layer runs inline without a request; each task flushes denormals on its own thread; and the
// tasks allocate nothing.

#include <array>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <sst/cpputils/rtsan_support.h>

#include "core/LayersFixture.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"
#include "gg/engine/TaskRunner.h"
#include "support/AllocTripwire.h"
#include "support/StubPool.h"

using gg::core::LayerParam;
using gg::core::layerSlot;
using gg::core::Mode;
using gg::core::NoteEventType;
using gg::core::ParamSlot;
using gg::core::Processor;
using gg::test::AllocTripwire;
using gg::test::StubPool;
using namespace gg::test::layers;

namespace
{
// Counts run() calls and the tasks they asked for, and runs them on the calling thread.
class CountingRunner final : public gg::TaskRunner
{
  public:
    void run(uint32_t count, gg::TaskFn fn, void *ctx) noexcept override
    {
        ++calls;
        tasks += count;
        serial_.run(count, fn, ctx);
    }

    uint64_t calls{0};
    uint64_t tasks{0};

  private:
    gg::SerialRunner serial_;
};

// Runs every task of each run() on a new thread of its own, which starts in the default
// floating-point mode, and waits for it: a pool whose threads never share the audio thread's mode
// (StubPool's calling thread takes tasks too).
class OtherThreadRunner final : public gg::TaskRunner
{
  public:
    void run(uint32_t count, gg::TaskFn fn, void *ctx) noexcept override
    {
        // Starting and joining a thread is this runner's side of the request, as StubPool's
        // locking is: exempt under RealtimeSanitizer, while the tasks themselves are checked.
        SST_CPPUTILS_RTSAN_DISABLE;
        std::thread t([=] {
            for (uint32_t i = 0; i < count; ++i)
            {
                fn(ctx, i);
            }
        });
        t.join();
    }
};

// The instrument's notes for the four-layer render: a key held from the start, a chord over it,
// a gap with nothing held, then two more.
std::vector<TimedNote> instrumentNotes()
{
    return {{0, NoteEventType::On, 0, 60},         {9001, NoteEventType::On, 0, 67},
            {30017, NoteEventType::On, 1, 64},     {41003, NoteEventType::Off, 0, 60},
            {47999, NoteEventType::AllOff, -1, 0}, {55021, NoteEventType::On, 0, 55},
            {70013, NoteEventType::On, 0, 72},     {88001, NoteEventType::Off, 0, -1}};
}

// The thread-pool invariance renders' setup: the product, the runner, and Feedback 0.4 on layers
// B and D from the start (the automation moves it on every layer later), so two layers feed their
// rings while they read them.
LayersSetup poolSetup(Mode mode, gg::TaskRunner *runner)
{
    LayersSetup s;
    s.mode = mode;
    s.runner = runner;
    if (mode == Mode::Instrument)
    {
        s.notes = instrumentNotes();
    }
    s.configure = [](Processor &p) {
        p.setBase(layerSlot(LayerParam::Feedback, 1), 0.4f);
        p.setBase(layerSlot(LayerParam::Feedback, 3), 0.4f);
        p.setBase(layerSlot(LayerParam::Source, 1), 1.0f); // Live
        p.setBase(layerSlot(LayerParam::Source, 3), 1.0f);
    };
    return s;
}

const char *nameOf(Mode mode) { return mode == Mode::Fx ? "FX" : "instrument"; }
} // namespace

TEST_CASE("renders are identical with no pool and pools of 1, 2 and 8 workers",
          "[core][threads][determinism]")
{
    // The four-layer render of "four layers are block-size invariant", every source kind in play
    // (the automation switches each layer between Sample, Live and Resample, a Live engine) and
    // Feedback on layers B and D, in both products: at every block size, the serial runner and
    // stub pools of 1, 2 and 8 workers render the same bytes, the same grains in the same order
    // and the same ring heads; and every block size renders what the first did.
    const Stereo input = noise(kInvarianceFrames, 0.5f, 49);
    const auto events = layerAutomation();

    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        INFO(nameOf(mode));
        LayersRender ref;
        renderLayers(input, events, 4096, {4096}, ref, poolSetup(mode, nullptr));
        REQUIRE(allFinite(ref.audio.left));
        REQUIRE(allFinite(ref.audio.right));
        REQUIRE(peak(ref.audio.left) > 0.05f);
        REQUIRE_FALSE(ref.log.overflowed);
        for (uint32_t L = 0; L < 4; ++L)
        {
            INFO("layer " << L);
            REQUIRE(ref.log.layer(L).size() > 10);
        }

        for (const uint32_t block : {1u, 32u, 64u, 511u, 4096u})
        {
            INFO("block size " << block);
            LayersRender serial;
            renderLayers(input, events, 4096, {block}, serial, poolSetup(mode, nullptr));
            REQUIRE_FALSE(serial.log.overflowed);
            REQUIRE(sameBits(ref.audio.left, serial.audio.left));
            REQUIRE(sameBits(ref.audio.right, serial.audio.right));
            for (uint32_t L = 0; L < 4; ++L)
            {
                INFO("layer " << L);
                REQUIRE(serial.log.layer(L) == ref.log.layer(L));
                REQUIRE(serial.heads[L] == ref.heads[L]);
            }

            for (const unsigned workers : {1u, 2u, 8u})
            {
                INFO(workers << " workers");
                StubPool pool(workers);
                LayersRender r;
                renderLayers(input, events, 4096, {block}, r, poolSetup(mode, &pool));
                REQUIRE_FALSE(r.log.overflowed);
                REQUIRE(sameBits(serial.audio.left, r.audio.left));
                REQUIRE(sameBits(serial.audio.right, r.audio.right));
                REQUIRE(r.heads == serial.heads);
                // The sink's order too: drained after the join in layer order.
                REQUIRE(r.log.events.size() == serial.log.events.size());
                bool sameOrder = true;
                for (std::size_t i = 0; i < r.log.events.size(); ++i)
                {
                    sameOrder = sameOrder && r.log.events[i].layer == serial.log.events[i].layer &&
                                r.log.events[i].grain == serial.log.events[i].grain;
                }
                REQUIRE(sameOrder);
            }
        }
    }
}

TEST_CASE("a declining pool renders the same bytes", "[core][threads][determinism]")
{
    // A host that declines every other request (the plugin then loops itself, CLAP's rule): the
    // pieces alternate between the pool and the calling thread, and the bytes don't change.
    const Stereo input = noise(kInvarianceFrames, 0.5f, 49);
    const auto events = layerAutomation();
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        INFO(nameOf(mode));
        for (const uint32_t block : {64u, 511u})
        {
            INFO("block size " << block);
            LayersRender serial;
            renderLayers(input, events, 4096, {block}, serial, poolSetup(mode, nullptr));
            StubPool pool(4, true);
            LayersRender r;
            renderLayers(input, events, 4096, {block}, r, poolSetup(mode, &pool));
            REQUIRE(sameBits(serial.audio.left, r.audio.left));
            REQUIRE(sameBits(serial.audio.right, r.audio.right));
            REQUIRE(r.heads == serial.heads);
            REQUIRE(r.log.events.size() == serial.log.events.size());
        }
    }
}

TEST_CASE("a Resample layer, a loop and overdub render the same bytes on any pool",
          "[core][threads][determinism]")
{
    // M2 sub-project 2's spec §6.2, §7: a Resample layer's task reads the instance's tap, written
    // after the join, and a looping, overdubbing layer writes only its own ring, so the serial
    // runner and pools of 1, 2 and 8 workers render the same bytes and the same ring heads.
    const Stereo input = noise(kInvarianceFrames, 0.5f, 49);
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        INFO(nameOf(mode));
        LayersSetup setup = poolSetup(mode, nullptr);
        setup.configure = [](Processor &p) {
            p.setBase(layerSlot(LayerParam::Source, 0), 1.0f); // Live
            p.setBase(layerSlot(LayerParam::Loop, 0), 1.0f);
            p.setBase(layerSlot(LayerParam::LoopLength, 0), -2.0f);
            p.setBase(layerSlot(LayerParam::Overdub, 0), 0.6f);
            p.setBase(layerSlot(LayerParam::Source, 1), 2.0f); // Resample, fed back
            p.setBase(layerSlot(LayerParam::Feedback, 1), 0.4f);
            p.setBase(layerSlot(LayerParam::Source, 3), 2.0f); // Resample
            p.setBase(layerSlot(LayerParam::Overdub, 3), 0.3f);
        };
        for (const uint32_t block : {64u, 511u})
        {
            INFO("block size " << block);
            LayersRender serial;
            renderLayers(input, {}, 4096, {block}, serial, setup);
            REQUIRE(allFinite(serial.audio.left));
            REQUIRE(peak(serial.audio.left) > 0.05f);
            for (const unsigned workers : {1u, 2u, 8u})
            {
                INFO(workers << " workers");
                StubPool pool(workers);
                LayersSetup pooled = setup;
                pooled.runner = &pool;
                LayersRender r;
                renderLayers(input, {}, 4096, {block}, r, pooled);
                REQUIRE(sameBits(serial.audio.left, r.audio.left));
                REQUIRE(sameBits(serial.audio.right, r.audio.right));
                REQUIRE(r.heads == serial.heads);
                REQUIRE(r.log.events.size() == serial.log.events.size());
            }
        }
    }
}

TEST_CASE("one rendering layer makes no request", "[core][threads]")
{
    // A piece with one rendering layer runs it inline; with two or more, one run() per piece,
    // asking for one task per rendering layer. Off layers' ring writes are never tasks.
    const Stereo input = noise(48000, 0.5f, 61);
    CountingRunner counter;
    Processor p({kRate, 4096, 63, Mode::Fx});
    p.setTaskRunner(&counter);
    p.setBase(ParamSlot::Mix, 1.0f);
    Stereo out = silence(48000);

    // A alone, as by default: 40 calls of 256, no request.
    render(p, &input, 0, 10240, {256}, {}, out);
    CHECK(counter.calls == 0);

    // B alone: still one rendering layer.
    p.setBase(gg::core::layerOnSlot(0), 0.0f);
    p.setBase(gg::core::layerOnSlot(1), 1.0f);
    render(p, &input, 10240, 20480, {256}, {}, out);
    CHECK(counter.calls == 0);

    // A and B: one request of two tasks per piece. Calls of 1000 frames are pieces of 256, 256,
    // 256 and 232: four requests per call.
    p.setBase(gg::core::layerOnSlot(0), 1.0f);
    render(p, &input, 20480, 30480, {1000}, {}, out);
    CHECK(counter.calls == 40);
    CHECK(counter.tasks == 80);

    // All four: four tasks per piece.
    p.setBase(gg::core::layerOnSlot(2), 1.0f);
    p.setBase(gg::core::layerOnSlot(3), 1.0f);
    render(p, &input, 30480, 40720, {256}, {}, out);
    CHECK(counter.calls == 80);
    CHECK(counter.tasks == 80 + 160);

    // None: no request.
    for (uint8_t L = 0; L < 4; ++L)
    {
        p.setBase(gg::core::layerOnSlot(L), 0.0f);
    }
    render(p, &input, 40720, 48000, {256}, {}, out);
    CHECK(counter.calls == 80);
}

TEST_CASE("tasks run with denormals flushed", "[core][threads][determinism]")
{
    // The audio thread renders with denormals flushed (process()); a pool thread starts in its
    // own mode, so each task flushes on its own thread for its duration. Tiny inputs make the
    // layers' chains (the diffuser, the limiter, Feedback) dip into the denormal range: a 1e-39
    // impulse, denormal itself, and bursts near the bottom of the normal range, whose products
    // fall below it and mix back into normal values. Every layer diffuses at 100% with Feedback,
    // so the diffuser's lines recirculate them. A task that kept its thread's mode renders other
    // bits: without the task's guard the 1e-37 and 1e-36 cases fail here (the 1e-39 and 2e-38
    // ones render the same bits either way, as measured).
    constexpr int64_t kFrames = 24000;
    for (const float amp : {1.0e-39f, 2.0e-38f, 1.0e-37f, 1.0e-36f})
    {
        INFO("amplitude " << amp);
        Stereo input = silence(kFrames);
        const Stereo burst = noise(kFrames, amp, 67);
        for (std::size_t i = 0; i < 2400; ++i)
        {
            input.left[i] = burst.left[i];
            input.right[i] = burst.right[i];
        }
        input.left[4800] = amp; // and an impulse

        const auto renderWith = [&](gg::TaskRunner *runner) {
            Processor p({kRate, 4096, 69, Mode::Fx});
            p.setTaskRunner(runner);
            p.setBase(ParamSlot::Mix, 1.0f);
            for (uint8_t L = 0; L < 4; ++L)
            {
                p.setBase(gg::core::layerOnSlot(L), 1.0f);
                p.setBase(layerSlot(LayerParam::Diffusion, L), 1.0f);
                p.setBase(layerSlot(LayerParam::Feedback, L), 0.9f);
                p.setBase(layerSlot(LayerParam::Density, L), 7.0f);
                p.setBase(gg::core::levelSlot(L), 6.0f);
            }
            Stereo out = silence(kFrames);
            render(p, &input, 0, kFrames, {512}, {}, out);
            return out;
        };
        const Stereo serial = renderWith(nullptr);
        OtherThreadRunner other;
        const Stereo elsewhere = renderWith(&other);
        StubPool pool(2);
        const Stereo pooled = renderWith(&pool);
        CHECK(sameBits(serial.left, elsewhere.left));
        CHECK(sameBits(serial.right, elsewhere.right));
        CHECK(sameBits(serial.left, pooled.left));
        CHECK(sameBits(serial.right, pooled.right));
    }
}

TEST_CASE("layer tasks do not allocate", "[core][threads][rt]")
{
    // Four layers rendering through StubPool(2), every layer's chain busy (Feedback, Diffusion,
    // a Sample layer and a Live one), publishing on, 8 calls armed. The tripwire is the calling
    // thread's, the audio thread's side of the request; in the rtsan preset the tasks carry
    // RealtimeSanitizer on the stub's threads too (Processor::execTask is nonblocking).
    StubPool pool(2);
    Processor p({kRate, 4096, 71, Mode::Fx});
    p.setTaskRunner(&pool);
    p.setPublishing(true);
    for (uint8_t L = 0; L < 4; ++L)
    {
        p.setBase(gg::core::layerOnSlot(L), 1.0f);
        p.setBase(layerSlot(LayerParam::Feedback, L), 0.4f);
        p.setBase(layerSlot(LayerParam::Diffusion, L), 0.7f);
        p.setBase(layerSlot(LayerParam::Density, L), 7.0f);
        p.setBase(layerSlot(LayerParam::Source, L), L % 2 == 0 ? 0.0f : 1.0f);
        p.setSourceNow(L, sine(220.0 * (L + 1)));
    }
    Log log;
    p.setEventSink(&Log::record, &log);

    constexpr uint32_t kFrames = 1000;
    const Stereo input = noise(kFrames, 0.5f, 73);
    const float *in[2] = {input.left.data(), input.right.data()};
    std::vector<float> o0(kFrames), o1(kFrames);
    float *out[2] = {o0.data(), o1.data()};
    p.process(in, 2, out, 2, kFrames, {}); // the first call, unarmed

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (int i = 0; i < 8; ++i)
        {
            p.process(in, 2, out, 2, kFrames, {});
            gg::core::GrainVis g;
            while (p.grainQueue().pop(g))
            {
            }
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    REQUIRE_FALSE(log.overflowed);
    for (uint32_t L = 0; L < 4; ++L)
    {
        INFO("layer " << L);
        CHECK_FALSE(log.layer(L).empty());
    }
    CHECK(allFinite(o0));
    REQUIRE_NO_ALLOCATIONS(allocations);
}

namespace
{
// The overloaded four-layer render's setup (M2 engine spec §6.7): the FX over noise for 1 s, A at
// HQ, B Normal, C Eco, D HQ, every layer at Density 400 Hz and Size 500 ms, Budget 256 (1,024
// units, 256 per layer while four render): every rung of the ladder runs, in every layer.
LayersSetup overloadSetup(gg::TaskRunner *runner)
{
    LayersSetup s;
    s.runner = runner;
    s.frames = 48000;
    s.configure = [](Processor &p) {
        constexpr std::array<float, 4> kQuality{2.0f, 1.0f, 0.0f, 2.0f};
        for (uint8_t L = 0; L < 4; ++L)
        {
            p.setBase(layerSlot(LayerParam::Quality, L), kQuality[L]);
            p.setBase(layerSlot(LayerParam::Density, L), 8.643856189774725f); // 400 Hz
            p.setBase(layerSlot(LayerParam::Size, L), -1.0f);                 // 500 ms
        }
        p.setBase(ParamSlot::Budget, 256.0f);
    };
    return s;
}

// Off the grid: layer D off at 0.4 s and on again at 0.6 s, so the quotas move with the rendering
// set, and Budget up to 512 at 0.75 s.
std::vector<TimedEvent> overloadEvents()
{
    return {{19205, gg::core::layerOnSlot(3), false, 0.0},
            {28803, gg::core::layerOnSlot(3), false, 1.0},
            {36007, ParamSlot::Budget, false, 512.0}};
}

// Each steal came right before the spawn it made room for: the same layer's, at its onset.
bool stealsPrecedeTheirSpawns(const Log &log)
{
    for (const Log::Steal &s : log.steals)
    {
        if (s.next >= log.events.size() || log.events[s.next].layer != s.layer ||
            log.events[s.next].grain.onsetSample != s.onset)
        {
            return false;
        }
    }
    return true;
}
} // namespace

TEST_CASE("an overloaded four-layer render is the same with any pool",
          "[core][threads][budget][determinism]")
{
    // M2 engine spec §6.4, §6.7: the quotas are fixed serially before the tasks run, from state
    // known at the boundary, so the bytes, the grains, the steals (in order with the spawns) and
    // the ladder's counters are the same through the serial runner and stub pools of 1, 2 and 8
    // workers, at every block size.
    const Stereo input = noise(48000, 0.5f, 53);
    const auto events = overloadEvents();
    LayersRender ref;
    renderLayers(input, events, 4096, {4096}, ref, overloadSetup(nullptr));
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    REQUIRE(peak(ref.audio.left) > 0.05f);
    REQUIRE_FALSE(ref.log.overflowed);
    // Not vacuous: every rung ran.
    REQUIRE(ref.lowered > 0);
    REQUIRE(ref.stolen > 0);
    REQUIRE(ref.stolen == ref.log.steals.size());
    REQUIRE(ref.dropped[0] + ref.dropped[1] + ref.dropped[2] + ref.dropped[3] > 0);
    REQUIRE(stealsPrecedeTheirSpawns(ref.log));
    for (uint32_t L = 0; L < 4; ++L)
    {
        INFO("layer " << L);
        REQUIRE(ref.log.layer(L).size() > 10);
        REQUIRE_FALSE(ref.log.layerSteals(L).empty());
    }

    for (const uint32_t block : {1u, 32u, 64u, 511u, 4096u})
    {
        INFO("block size " << block);
        LayersRender serial;
        renderLayers(input, events, 4096, {block}, serial, overloadSetup(nullptr));
        REQUIRE_FALSE(serial.log.overflowed);
        REQUIRE(sameBits(ref.audio.left, serial.audio.left));
        REQUIRE(sameBits(ref.audio.right, serial.audio.right));
        REQUIRE(serial.lowered == ref.lowered);
        REQUIRE(serial.stolen == ref.stolen);
        REQUIRE(serial.dropped == ref.dropped);
        REQUIRE(stealsPrecedeTheirSpawns(serial.log));
        for (uint32_t L = 0; L < 4; ++L)
        {
            INFO("layer " << L);
            REQUIRE(serial.log.layer(L) == ref.log.layer(L));
            REQUIRE(serial.log.layerSteals(L) == ref.log.layerSteals(L));
            REQUIRE(serial.heads[L] == ref.heads[L]);
        }

        for (const unsigned workers : {1u, 2u, 8u})
        {
            INFO(workers << " workers");
            StubPool pool(workers);
            LayersRender r;
            renderLayers(input, events, 4096, {block}, r, overloadSetup(&pool));
            REQUIRE_FALSE(r.log.overflowed);
            REQUIRE(sameBits(serial.audio.left, r.audio.left));
            REQUIRE(sameBits(serial.audio.right, r.audio.right));
            REQUIRE(r.heads == serial.heads);
            REQUIRE(r.lowered == serial.lowered);
            REQUIRE(r.stolen == serial.stolen);
            REQUIRE(r.dropped == serial.dropped);
            // The sinks' order too: drained after the join in layer order, each steal before
            // the spawn it made room for.
            REQUIRE(r.log.events.size() == serial.log.events.size());
            bool sameOrder = true;
            for (std::size_t i = 0; i < r.log.events.size(); ++i)
            {
                sameOrder = sameOrder && r.log.events[i].layer == serial.log.events[i].layer &&
                            r.log.events[i].grain == serial.log.events[i].grain;
            }
            REQUIRE(sameOrder);
            REQUIRE(r.log.steals.size() == serial.log.steals.size());
            bool sameSteals = true;
            for (std::size_t i = 0; i < r.log.steals.size(); ++i)
            {
                const Log::Steal &a = r.log.steals[i];
                const Log::Steal &b = serial.log.steals[i];
                sameSteals = sameSteals && a.layer == b.layer && a.index == b.index &&
                             a.onset == b.onset && a.next == b.next;
            }
            REQUIRE(sameSteals);
        }
    }
}
