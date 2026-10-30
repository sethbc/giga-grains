// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The Processor's sources and rings (M2 sub-project 2's spec §6.1, §6.2, §6.4, §6.5, §6.6): the
// Resample tap, the ring parameters on the control grid, the AddSplice and ClearSplices commands,
// the ring exchange (a resize, a restore, a declined offer), the main thread's ring read, and the
// real-time and determinism contracts with all of them in play.

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Render.h"
#include "RenderSpec.h"
#include "core/LayersFixture.h"
#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Processor.h"
#include "gg/core/SourceExchange.h"
#include "gg/source/RingStorage.h"
#include "support/AllocTripwire.h"

using gg::RingStorage;
using gg::core::CommandEvent;
using gg::core::CommandKind;
using gg::core::layerOnSlot;
using gg::core::LayerParam;
using gg::core::layerSlot;
using gg::core::Mode;
using gg::core::NoteEvent;
using gg::core::NoteEventType;
using gg::core::ParamEvent;
using gg::core::ParamSlot;
using gg::core::Processor;
using gg::core::RingOffer;
using gg::test::AllocTripwire;
using namespace gg::test::layers;

namespace
{
constexpr int64_t kRing = 16 * 48000; // the default Ring Length's frames at 48 kHz

ParamSlot slotOf(LayerParam p, uint8_t layer) { return layerSlot(p, layer); }

uint32_t bitsOf(float f) { return std::bit_cast<uint32_t>(f); }

// A command at an absolute sample.
struct TimedCommand
{
    int64_t time;
    CommandKind kind;
    uint8_t layer;
    float value;
};

// Renders [from, to) of `input` (null: none) through p in calls of `blocks` in turn, each given
// the events, notes and commands inside it, into out[from, to), numOut channels (1: out.left
// alone). As LayersFixture's render, with commands.
void renderCmd(Processor &p, const Stereo *input, int64_t from, int64_t to,
               const std::vector<uint32_t> &blocks, const std::vector<TimedEvent> &events,
               const std::vector<TimedCommand> &commands, Stereo &out, uint32_t numOut = 2,
               Log *log = nullptr, const std::vector<TimedNote> &notes = {})
{
    std::vector<ParamEvent> blockEvents;
    std::vector<NoteEvent> blockNotes;
    std::vector<CommandEvent> blockCommands;
    std::size_t nextEvent = 0;
    std::size_t nextNote = 0;
    std::size_t nextCommand = 0;
    while (nextEvent < events.size() && events[nextEvent].time < from)
    {
        ++nextEvent;
    }
    while (nextNote < notes.size() && notes[nextNote].time < from)
    {
        ++nextNote;
    }
    while (nextCommand < commands.size() && commands[nextCommand].time < from)
    {
        ++nextCommand;
    }
    int64_t done = from;
    for (std::size_t next = 0; done < to; ++next)
    {
        const uint32_t want = blocks[next % blocks.size()];
        const auto n = static_cast<uint32_t>(std::min<int64_t>(want, to - done));
        const int64_t limit = done + std::max<int64_t>(n, 1);
        blockEvents.clear();
        for (; nextEvent < events.size() && events[nextEvent].time < limit; ++nextEvent)
        {
            const TimedEvent &e = events[nextEvent];
            blockEvents.push_back(
                {static_cast<uint32_t>(e.time - done), e.slot, e.isModulation, e.value});
        }
        blockNotes.clear();
        for (; nextNote < notes.size() && notes[nextNote].time < limit; ++nextNote)
        {
            const TimedNote &e = notes[nextNote];
            blockNotes.push_back(
                {static_cast<uint32_t>(e.time - done), e.type, e.channel, e.key, e.velocity});
        }
        blockCommands.clear();
        for (; nextCommand < commands.size() && commands[nextCommand].time < limit; ++nextCommand)
        {
            const TimedCommand &c = commands[nextCommand];
            blockCommands.push_back(
                {static_cast<uint32_t>(c.time - done), c.kind, c.layer, c.value});
        }
        const auto j = static_cast<std::size_t>(done);
        const float *in[2] = {input != nullptr ? input->left.data() + j : nullptr,
                              input != nullptr ? input->right.data() + j : nullptr};
        float *o[2] = {out.left.data() + j, out.right.data() + j};
        p.process(input != nullptr ? in : nullptr, input != nullptr ? 2 : 0, o, numOut, n,
                  blockEvents, blockNotes, {}, blockCommands);
        done += n;
        if (log != nullptr)
        {
            ++log->call;
        }
    }
}

// Layer L's ring frame f (both channels), from the storage in effect.
std::pair<float, float> ringFrame(const Processor &p, uint8_t L, int64_t f)
{
    const gg::LiveSource::RingView v = p.engine(L).live().view();
    const int64_t s = f % v.capacity;
    return {v.slots[2 * s], v.slots[2 * s + 1]};
}

// How many frames t in [from, to) of layer L's ring differ, bit for bit, from what `expected`
// gives for t (the left and right values).
template <typename Expected>
int64_t ringMismatches(const Processor &p, uint8_t L, int64_t from, int64_t to, Expected &&expected)
{
    int64_t mismatches = 0;
    for (int64_t t = from; t < to; ++t)
    {
        const auto [l, r] = ringFrame(p, L, t);
        const std::pair<float, float> e = expected(t);
        if (bitsOf(l) != bitsOf(e.first) || bitsOf(r) != bitsOf(e.second))
        {
            ++mismatches;
        }
    }
    return mismatches;
}

// The main thread's ring read (spec §6.4), done in one go between process() calls: copies the
// stable part of layer L's window into `next` at next's slots, the frames older than the head
// read, at most next's window long, and returns that head R0 (the offer's copiedTo).
int64_t copyStable(const Processor &p, uint8_t L, RingStorage &next)
{
    const gg::core::RingRead r = p.ringRead(L);
    const int64_t head = r.head->load(std::memory_order_acquire);
    const RingStorage &from = *r.storage;
    const int64_t oldest = std::max({int64_t{0}, head - from.frames(), head - next.frames()});
    for (int64_t f = oldest; f < head; ++f)
    {
        const int64_t s = f % from.capacity();
        const int64_t d = f % next.capacity();
        next.slots()[2 * d] = from.slots()[2 * s];
        next.slots()[2 * d + 1] = from.slots()[2 * s + 1];
    }
    return head;
}

// Layer L's frames [from, to) of the storage in effect, interleaved.
std::vector<float> windowOf(const Processor &p, uint8_t L, int64_t from, int64_t to)
{
    std::vector<float> w;
    for (int64_t f = from; f < to; ++f)
    {
        const auto [l, r] = ringFrame(p, L, f);
        w.push_back(l);
        w.push_back(r);
    }
    return w;
}

// The golden case's spec (tests/golden/cases), parsed and resolved against the spec's directory.
gg::render::RenderSpec loadCase(const std::string &name)
{
    const auto path = std::filesystem::path{GG_GOLDEN_DIR} / "cases" / (name + ".json");
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    const std::string json{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::string error;
    auto spec = gg::render::parseRenderSpec(json, error);
    INFO(name << ": " << error);
    REQUIRE(spec.has_value());
    gg::render::resolvePaths(*spec, path.parent_path().string());
    return *spec;
}

bool sameAudio(const gg::metrics::AudioData &a, const gg::metrics::AudioData &b)
{
    if (a.channels.size() != b.channels.size())
    {
        return false;
    }
    for (std::size_t c = 0; c < a.channels.size(); ++c)
    {
        if (!sameBits(a.channels[c], b.channels[c]))
        {
            return false;
        }
    }
    return true;
}
} // namespace

TEST_CASE("the tap is 256 samples late", "[core][source]")
{
    // Spec §6.2: a Resample layer's ring input at t is the instance output at t − 256 (the mix,
    // the dry signal and the reverb's return, as the host hears it), and silence before the first
    // sample. Over impulses, so the output has the dry clicks, the grains and the reverb's tail.
    constexpr int64_t kFrames = 48000;
    Stereo input = silence(kFrames);
    input.left[1000] = 1.0f;
    input.right[1000] = 0.5f;
    input.left[20000] = -0.75f;
    input.right[33333] = 0.9f;
    Processor p({kRate, 512, 7, Mode::Fx});
    p.setBase(ParamSlot::Mix, 0.5f);
    p.setBase(ParamSlot::ReverbAmount, 0.25f);
    p.setBase(slotOf(LayerParam::Source, 1), 2.0f); // B Resample
    p.setBase(layerOnSlot(1), 1.0f);
    Stereo out = silence(kFrames);
    renderCmd(p, &input, 0, kFrames, {512}, {}, {}, out);
    REQUIRE(p.engine(1).liveWriteHead() == kFrames);
    REQUIRE(peak(out.left) > 0.1f);
    REQUIRE(peakIn(out.left, 1300, 20000) > 0.0f); // the grains and the reverb after the click
    const auto delayed = [&out](int64_t t) {
        const auto i = static_cast<std::size_t>(t - 256);
        return t < 256 ? std::pair{0.0f, 0.0f} : std::pair{out.left[i], out.right[i]};
    };
    REQUIRE(ringMismatches(p, 1, 0, kFrames, delayed) == 0);
}

TEST_CASE("the tap is silent before a Resample layer exists", "[core][source]")
{
    // Spec §6.2: the tap's history is written only while some layer is Resample, so the first
    // 256 samples after layer B turns Resample at the boundary 24032 read silence, though the
    // output before it was not silent; before it, B's ring took the input.
    constexpr int64_t kFrames = 48000;
    constexpr int64_t kSwitch = 24032; // the grid boundary of the event at 24005
    const Stereo input = noise(kFrames, 0.5f, 61);
    Processor p({kRate, 512, 7, Mode::Fx});
    p.setBase(ParamSlot::Mix, 0.5f);
    p.setBase(layerOnSlot(1), 1.0f); // B Live, the FX's default
    Stereo out = silence(kFrames);
    renderCmd(p, &input, 0, kFrames, {512}, {{24005, slotOf(LayerParam::Source, 1), false, 2.0}},
              {}, out);
    REQUIRE(peakIn(out.left, kSwitch - 256, kSwitch) > 0.05f);
    const auto inputAt = [&input](int64_t t) {
        const auto i = static_cast<std::size_t>(t);
        return std::pair{input.left[i], input.right[i]};
    };
    CHECK(ringMismatches(p, 1, 0, kSwitch, inputAt) == 0);
    CHECK(ringMismatches(p, 1, kSwitch, kSwitch + 256,
                         [](int64_t) { return std::pair{0.0f, 0.0f}; }) == 0);
    CHECK(ringMismatches(p, 1, kSwitch + 256, kFrames, [&out](int64_t t) {
              const auto i = static_cast<std::size_t>(t - 256);
              return std::pair{out.left[i], out.right[i]};
          }) == 0);
}

TEST_CASE("one output channel feeds both tap channels", "[core][source]")
{
    // Spec §6.2: with one output channel, the mono output the host hears feeds both sides of a
    // Resample layer's ring.
    constexpr int64_t kFrames = 24000;
    const Stereo input = noise(kFrames, 0.5f, 67);
    Processor p({kRate, 256, 7, Mode::Fx});
    p.setBase(ParamSlot::Mix, 0.5f);
    p.setBase(slotOf(LayerParam::Source, 2), 2.0f); // C Resample, off: its ring alone
    Stereo out = silence(kFrames);
    renderCmd(p, &input, 0, kFrames, {256}, {}, {}, out, 1);
    REQUIRE(peak(out.left) > 0.05f);
    CHECK(ringMismatches(p, 2, 0, kFrames, [&out](int64_t t) {
              const float v = t < 256 ? 0.0f : out.left[static_cast<std::size_t>(t - 256)];
              return std::pair{v, v};
          }) == 0);
}

TEST_CASE("a Resample layer takes Feedback", "[core][source]")
{
    // Spec §6.2: Feedback follows the kind; Resample is a Live engine, so its Feedback writes its
    // ring (ring = tap + fb·limited), while a Sample layer's Feedback is 0.
    constexpr int64_t kFrames = 24000;
    const Stereo input = noise(kFrames, 0.5f, 71);
    const auto run = [&input](float feedback, Processor &p, Stereo &out) {
        p.setBase(ParamSlot::Mix, 1.0f);
        p.setBase(slotOf(LayerParam::Source, 1), 2.0f); // B Resample
        p.setBase(layerOnSlot(1), 1.0f);
        p.setBase(slotOf(LayerParam::Feedback, 1), feedback);
        p.setBase(slotOf(LayerParam::Source, 2), 0.0f); // C Sample
        p.setBase(layerOnSlot(2), 1.0f);
        p.setBase(slotOf(LayerParam::Feedback, 2), feedback);
        p.setSourceNow(2, sine(330.0));
        renderCmd(p, &input, 0, kFrames, {512}, {}, {}, out);
    };
    const auto delayedOut = [](const Stereo &out) {
        return [&out](int64_t t) {
            const auto i = static_cast<std::size_t>(t - 256);
            return t < 256 ? std::pair{0.0f, 0.0f} : std::pair{out.left[i], out.right[i]};
        };
    };

    Processor fed({kRate, 512, 7, Mode::Fx});
    Stereo fedOut = silence(kFrames);
    run(0.5f, fed, fedOut);
    CHECK(fed.feedbackAt(1, kFrames) == 0.5f);
    CHECK(fed.feedbackAt(2, kFrames) == 0.0f);
    CHECK(ringMismatches(fed, 1, 0, kFrames, delayedOut(fedOut)) > kFrames / 4);

    Processor plain({kRate, 512, 7, Mode::Fx});
    Stereo plainOut = silence(kFrames);
    run(0.0f, plain, plainOut);
    CHECK(ringMismatches(plain, 1, 0, kFrames, delayedOut(plainOut)) == 0);
}

TEST_CASE("ring parameters apply on the grid", "[core][source]")
{
    // Spec §6.1, §7: Loop, Loop Length, Overdub and Splice apply at their event's grid boundary,
    // like any parameter: Loop at offset 40 takes effect at 64, not before, and a boundary
    // carried past a call's end applies in the next call.
    const Stereo input = noise(24000, 0.5f, 73);

    SECTION("Loop at offset 40 applies at 64")
    {
        Processor p({kRate, 512, 7, Mode::Fx});
        p.setBase(slotOf(LayerParam::LoopLength, 0), -1.0f); // 0.5 s
        Stereo out = silence(24000);
        const std::vector<TimedEvent> loop{{40, slotOf(LayerParam::Loop, 0), false, 1.0}};
        renderCmd(p, &input, 0, 50, {50}, loop, {}, out);
        CHECK(p.engine(0).live().windowFrames() == kRing);
        renderCmd(p, &input, 50, 64, {14}, {}, {}, out); // 64 is the call's end: carried over
        CHECK(p.engine(0).live().windowFrames() == kRing);
        renderCmd(p, &input, 64, 65, {1}, {}, {}, out);
        CHECK(p.engine(0).live().windowFrames() == 24000);
        CHECK(p.base(slotOf(LayerParam::Loop, 0)) == 1.0f);
    }

    SECTION("the boundary decides, at any block size")
    {
        // A loop with overdub changes what the ring holds, so the boundary is audible once the
        // ring holds a loop's length: Loop events at 4833, 4840 and 4864 render the same bytes
        // (boundary 4864), one at 4865 (boundary 4896) doesn't.
        const auto renderAt = [&input](int64_t at, const std::vector<uint32_t> &blocks) {
            Processor p({kRate, 4096, 7, Mode::Fx});
            p.setBase(ParamSlot::Mix, 1.0f);
            p.setBase(slotOf(LayerParam::LoopLength, 0), -4.3f); // about 51 ms
            p.setBase(slotOf(LayerParam::Overdub, 0), 0.8f);
            p.setBase(slotOf(LayerParam::Density, 0), 7.0f);
            p.setBase(slotOf(LayerParam::Size, 0), -6.0f);
            Stereo out = silence(24000);
            renderCmd(p, &input, 0, 24000, blocks,
                      {{at, slotOf(LayerParam::Loop, 0), false, 1.0},
                       {6001, slotOf(LayerParam::Splice, 0), false, 1.0},
                       {9017, slotOf(LayerParam::LoopLength, 0), false, -2.0},
                       {12003, slotOf(LayerParam::Overdub, 0), false, 0.3}},
                      {{2005, CommandKind::AddSplice, 0, 0.02f}}, out);
            return out;
        };
        const Stereo ref = renderAt(4840, {4096});
        REQUIRE(peak(ref.left) > 0.05f);
        for (const int64_t at : {int64_t{4833}, int64_t{4864}})
        {
            INFO("at " << at);
            const Stereo r = renderAt(at, {4096});
            CHECK(sameBits(r.left, ref.left));
            CHECK(sameBits(r.right, ref.right));
        }
        CHECK_FALSE(sameBits(renderAt(4865, {4096}).left, ref.left));
        for (const uint32_t block : {1u, 32u, 511u})
        {
            INFO("blocks of " << block);
            const Stereo r = renderAt(4840, {block});
            CHECK(sameBits(r.left, ref.left));
            CHECK(sameBits(r.right, ref.right));
        }
    }
}

TEST_CASE("the ring parameters at their defaults render the same bytes", "[core][source]")
{
    // Spec §6.1: a piece whose Overdub ramp is 0 throughout writes the input alone by M1's path,
    // so fx-feedback's spec with every ring parameter set to its default, explicitly and by
    // automation, renders the bytes its golden does (its WAV's SHA-256 is pinned across builds by
    // scripts/update-goldens.sh); and so does a layer that overdubs and loops a ring nobody hears
    // (layer B, off).
    const gg::render::RenderSpec spec = loadCase("fx-feedback");
    const gg::metrics::AudioData ref = gg::render::render(spec, nullptr);
    REQUIRE(!ref.channels.empty());

    gg::render::RenderSpec explicitZero = spec;
    for (const char *name : {"A Overdub", "A Loop", "A Splice", "B Overdub"})
    {
        explicitZero.fx.params.emplace_back(name, 0.0);
    }
    auto &automation = explicitZero.fx.automation;
    automation.push_back({1.0, "A Overdub", 0.0});
    automation.push_back({1.5, "A Loop", 0.0});
    automation.push_back({2.0, "A Splice", 0.0});
    automation.push_back({2.5, "A Loop Length", 2.0});
    std::stable_sort(automation.begin(), automation.end(),
                     [](const auto &a, const auto &b) { return a.timeSeconds < b.timeSeconds; });
    CHECK(sameAudio(gg::render::render(explicitZero, nullptr), ref));

    gg::render::RenderSpec unheard = spec;
    unheard.fx.params.emplace_back("B Overdub", 0.7);
    unheard.fx.params.emplace_back("B Loop", 1.0);
    unheard.fx.params.emplace_back("B Loop Length", -2.0);
    CHECK(sameAudio(gg::render::render(unheard, nullptr), ref));
}

TEST_CASE("a splice command marks the ring", "[core][source]")
{
    // Spec §6.4: AddSplice marks R(b) − value × sr at its boundary b, clamped into the window,
    // ignoring a mark on an existing one; ClearSplices empties the marks; a command on a Sample
    // layer, on no layer or with a NaN value is ignored. The marks reach ringMeta after the next
    // piece's write, and Splice selects among them.
    constexpr int64_t kFrames = 49000;
    const Stereo input = noise(kFrames, 0.5f, 79);
    Processor p({kRate, 512, 13, Mode::Fx});
    p.setBase(slotOf(LayerParam::Source, 2), 0.0f); // C Sample
    Stereo out = silence(kFrames);
    renderCmd(p, &input, 0, 48000, {512}, {}, {}, out);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    renderCmd(p, &input, 48000, 48100, {100}, {},
              {{48010, CommandKind::AddSplice, 0, 0.25f},
               {48012, CommandKind::AddSplice, 0, 0.25f},  // on the first: ignored
               {48020, CommandKind::AddSplice, 0, 100.0f}, // clamped to the oldest frame, 0
               {48030, CommandKind::AddSplice, 2, 0.1f},   // a Sample layer: ignored
               {48030, CommandKind::AddSplice, 7, 0.1f},   // no such layer: ignored
               {48031, CommandKind::AddSplice, 1, nan}},   // ignored
              out);
    // The boundary is 48032: 48032 − 0.25 × 48000.
    const std::vector<int64_t> expected{0, 36032};
    const auto marks = p.engine(0).live().splices();
    CHECK(std::vector<int64_t>(marks.begin(), marks.end()) == expected);
    CHECK(p.engine(1).live().spliceCount() == 0);
    CHECK(p.engine(2).live().spliceCount() == 0);
    gg::core::RingMeta meta = p.ringMeta(0);
    CHECK(meta.head == 48100);
    CHECK(meta.spliceCount == 2);
    CHECK(meta.splices[0] == 0);
    CHECK(meta.splices[1] == 36032);
    CHECK(meta.windowFrames == kRing);
    CHECK_FALSE(meta.frozen);

    // Splice 2, the last, runs from its mark to the head.
    renderCmd(p, &input, 48100, 48200, {100}, {{48100, slotOf(LayerParam::Splice, 0), false, 2.0}},
              {}, out);
    CHECK(p.engine(0).live().windowFrames() == 48200 - 36032);
    CHECK(p.ringMeta(0).windowFrames == 48200 - 36032);

    renderCmd(p, &input, 48200, 48300, {100}, {}, {{48210, CommandKind::ClearSplices, 0, 0.0f}},
              out);
    CHECK(p.engine(0).live().spliceCount() == 0);
    meta = p.ringMeta(0);
    CHECK(meta.spliceCount == 0);
    CHECK(meta.windowFrames == kRing); // no mark: the whole window
}

TEST_CASE("a group keeps its first 64 commands", "[core][source]")
{
    // CommandEvent: a group applies at most kMaxGroupCommands commands; later ones are ignored.
    // 64 AddSplice commands make one mark (the rest land on it); a ClearSplices after them is the
    // 65th, and ignored, while one in 64th place clears.
    static_assert(Processor::kMaxGroupCommands == 64);
    const Stereo input = noise(2048, 0.5f, 139);
    for (const uint32_t adds : {63u, 64u})
    {
        INFO(adds << " AddSplice commands first");
        Processor p({kRate, 512, 41, Mode::Fx});
        Stereo out = silence(2048);
        renderCmd(p, &input, 0, 1024, {512}, {}, {}, out);
        std::vector<TimedCommand> commands(adds, {1030, CommandKind::AddSplice, 0, 0.01f});
        commands.push_back({1030, CommandKind::ClearSplices, 0, 0.0f});
        renderCmd(p, &input, 1024, 2048, {1024}, {}, commands, out);
        CHECK(p.engine(0).live().spliceCount() == (adds == 64 ? 1u : 0u));
    }
}

TEST_CASE("a ring restore arms Freeze", "[core][source]")
{
    // Spec §6.6, ruling R1-3 (INTENT §23 #40 amended): a layer whose ring is restored arms at the
    // restore, so its Freeze, on, holds the restored ring from the first sample, bit for bit; a
    // layer without a restore keeps "live until toggled" (FR-8).
    constexpr int64_t kSaved = 48000;
    const Stereo input = noise(2048, 0.5f, 83);
    const Stereo saved = noise(kSaved, 0.4f, 89);
    Processor p({kRate, 512, 17, Mode::Fx});
    // A state load with Freeze on in layers A and B: neither is armed, so both stay live until
    // toggled (setBase of one layer's Freeze would arm the others, seen off).
    gg::core::ParamValues loaded{};
    for (std::size_t s = 0; s < loaded.size(); ++s)
    {
        loaded[s] = p.base(static_cast<ParamSlot>(s));
    }
    loaded[static_cast<std::size_t>(slotOf(LayerParam::Freeze, 0))] = 1.0f;
    loaded[static_cast<std::size_t>(slotOf(LayerParam::Freeze, 1))] = 1.0f;
    p.setBases(loaded);
    auto storage = std::make_unique<RingStorage>(kRing, 512);
    for (int64_t f = 0; f < kSaved; ++f)
    {
        storage->slots()[2 * f] = saved.left[static_cast<std::size_t>(f)];
        storage->slots()[2 * f + 1] = saved.right[static_cast<std::size_t>(f)];
    }
    RingStorage *raw = storage.get();
    RingOffer how;
    how.restore = true;
    how.frames = kSaved;
    how.frozen = true;
    how.spliceCount = 1;
    how.splices[0] = 24000;
    const uint32_t epoch = p.ringRead(0).storageEpoch->load(std::memory_order_acquire);
    p.sources(0).offerRing(std::move(storage), how);
    CHECK(p.sources(0).ringAdoptionPending());

    // An unfrozen restore on layer C goes on from its head.
    auto live = std::make_unique<RingStorage>(kRing, 512);
    RingOffer liveHow;
    liveHow.restore = true;
    liveHow.frames = kSaved;
    p.sources(2).offerRing(std::move(live), liveHow);

    Stereo out = silence(2048);
    renderCmd(p, &input, 0, 2048, {512}, {}, {}, out);
    CHECK_FALSE(p.sources(0).ringAdoptionPending());
    CHECK(p.engine(0).liveWriteHead() == kSaved); // held from the first sample
    CHECK(p.ringRead(0).storage == raw);
    CHECK(p.ringRead(0).head->load(std::memory_order_acquire) == kSaved);
    CHECK(p.ringRead(0).storageEpoch->load(std::memory_order_acquire) == epoch + 2);
    CHECK(ringMismatches(p, 0, 0, kSaved, [&saved](int64_t t) {
              const auto i = static_cast<std::size_t>(t);
              return std::pair{saved.left[i], saved.right[i]};
          }) == 0);
    const gg::core::RingMeta meta = p.ringMeta(0);
    CHECK(meta.frozen);
    CHECK(meta.head == kSaved);
    CHECK(meta.spliceCount == 1);
    CHECK(meta.splices[0] == 24000);
    CHECK(p.sources(0).ringsHeld() == 1);

    CHECK(p.engine(1).liveWriteHead() == 2048); // not restored: live until toggled
    CHECK_FALSE(p.ringMeta(1).frozen);
    CHECK(p.engine(2).liveWriteHead() == kSaved + 2048);
    CHECK_FALSE(p.ringMeta(2).frozen);
}

TEST_CASE("a resize keeps the window", "[core][source]")
{
    // Spec §6.5: the main thread copies the stable part of the window, offers it, and the audio
    // thread copies the frames written since (at most kAdoptCopyFrames) at a call's start, so the
    // window is the same frames in the new storage; an offer whose gap is past the bound is
    // declined, counted and remade from the current head.
    constexpr int64_t kFrames = 4 * 48000;
    const Stereo input = noise(kFrames, 0.5f, 97);
    Processor p({kRate, 512, 19, Mode::Fx});
    p.setBase(ParamSlot::Mix, 1.0f);
    Stereo out = silence(kFrames);
    renderCmd(p, &input, 0, 96000, {512}, {}, {}, out);

    // Longer: 20 s. The main thread copies up to R0, then the audio thread renders a block before
    // it takes the offer.
    auto longer = std::make_unique<RingStorage>(20 * 48000, 512);
    RingStorage *rawLonger = longer.get();
    RingOffer how;
    how.copiedTo = copyStable(p, 0, *longer);
    REQUIRE(how.copiedTo == 96000);
    renderCmd(p, &input, 96000, 96512, {512}, {}, {}, out);
    const std::vector<float> before = windowOf(p, 0, 0, 96512);
    p.sources(0).offerRing(std::move(longer), how);
    renderCmd(p, &input, 96512, 97024, {512}, {}, {}, out);
    CHECK(p.ringRead(0).storage == rawLonger);
    CHECK(p.engine(0).liveFrames() == 20 * 48000);
    CHECK(windowOf(p, 0, 0, 96512) == before);
    CHECK_FALSE(p.sources(0).ringAdoptionPending());
    CHECK(p.ringAdoptionsDeclined(0) == 0);
    CHECK(p.ringMeta(0).windowFrames == 20 * 48000);

    // Shorter, 2 s, with a gap past kAdoptCopyFrames: declined, the storage handed back.
    auto late = std::make_unique<RingStorage>(2 * 48000, 512);
    how.copiedTo = copyStable(p, 0, *late);
    renderCmd(p, &input, 97024, 97024 + 9216, {512}, {}, {}, out);
    p.sources(0).offerRing(std::move(late), how);
    renderCmd(p, &input, 106240, 106752, {512}, {}, {}, out);
    CHECK(p.ringAdoptionsDeclined(0) == 1);
    CHECK(p.sources(0).ringAdoptionPending());
    CHECK(p.ringRead(0).storage == rawLonger);
    p.sources(0).collect();
    CHECK(p.sources(0).ringsHeld() == 1);

    // Remade from the current head: adopted, the newest 2 s kept.
    auto remade = std::make_unique<RingStorage>(2 * 48000, 512);
    RingStorage *rawRemade = remade.get();
    how.copiedTo = copyStable(p, 0, *remade);
    const std::vector<float> newest = windowOf(p, 0, 106752 - 96000, 106752);
    p.sources(0).offerRing(std::move(remade), how);
    renderCmd(p, &input, 106752, 107264, {512}, {}, {}, out);
    CHECK(p.ringRead(0).storage == rawRemade);
    CHECK(p.engine(0).liveFrames() == 2 * 48000);
    CHECK(windowOf(p, 0, 106752 - 96000 + 512, 106752) ==
          std::vector<float>(newest.begin() + 1024, newest.end()));
    CHECK_FALSE(p.sources(0).ringAdoptionPending());
    p.sources(0).collect();
    CHECK(p.sources(0).ringsHeld() == 1); // the 20 s storage, retired, is freed
    REQUIRE(allFinite(out.left));
}

TEST_CASE("Position reaches the whole of a longer ring", "[core][source]")
{
    // Spec §6.3: Position's seconds reach to the ring in effect, not to the 16 s the engine was
    // built with: over a restored 20 s ring holding 19 s, Position 18 s reads 18 s behind the head.
    const double v = std::log2(18.0 * 255.0 / 16.0 + 1.0) / 8.0; // (16/255)(2^(8v) − 1) = 18
    constexpr int64_t kSaved = 19 * 48000;
    Processor p({kRate, 512, 23, Mode::Fx});
    p.setBase(slotOf(LayerParam::Position, 0), static_cast<float>(v));
    p.setBase(slotOf(LayerParam::Spray, 0), 0.0f);
    p.setBase(slotOf(LayerParam::Density, 0), 5.0f);
    p.setBase(slotOf(LayerParam::Size, 0), -3.0f);
    RingOffer how;
    how.restore = true;
    how.frames = kSaved;
    p.sources(0).offerRing(std::make_unique<RingStorage>(20 * 48000, 512), how);
    Log log;
    p.setEventSink(&Log::record, &log);
    const Stereo input = noise(24000, 0.5f, 101);
    Stereo out = silence(24000);
    renderCmd(p, &input, 0, 24000, {512}, {}, {}, out);
    const auto grains = log.layer(0);
    REQUIRE(grains.size() > 5);
    for (const gg::GrainSpec &g : grains)
    {
        const double head = static_cast<double>(kSaved + g.onsetSample + 1);
        CHECK(head - g.startPos == Catch::Approx(18.0 * 48000).margin(2.0));
    }
}

TEST_CASE("setRingNow adopts a ring while inactive", "[core][source]")
{
    // The inactive case (gg-render's pre-render resize, the plugin's activation): the ring is
    // adopted on the calling thread; a bad offer is refused and its storage freed.
    Processor p({kRate, 512, 29, Mode::Fx});
    REQUIRE(p.setRingNow(1, std::make_unique<RingStorage>(5 * 48000, 512), RingOffer{}));
    CHECK(p.engine(1).liveFrames() == 5 * 48000);
    CHECK(p.sources(1).ringsHeld() == 1);
    RingOffer bad;
    bad.copiedTo = 10; // past the head
    CHECK_FALSE(p.setRingNow(1, std::make_unique<RingStorage>(48000, 512), bad));
    CHECK(p.engine(1).liveFrames() == 5 * 48000);
    CHECK(p.sources(1).ringsHeld() == 1);
    RingOffer restore;
    restore.restore = true;
    restore.frames = 1000;
    REQUIRE(p.setRingNow(1, std::make_unique<RingStorage>(48000, 512), restore));
    CHECK(p.engine(1).liveWriteHead() == 1000);
    CHECK(p.sources(1).ringsHeld() == 1); // the 5 s one, replaced, is freed

    // A declined offer stays pending until setRingNow supersedes it.
    Stereo out = silence(10000);
    const Stereo input = noise(10000, 0.5f, 137);
    renderCmd(p, &input, 0, 9600, {512}, {}, {}, out);
    p.sources(1).offerRing(std::make_unique<RingStorage>(48000, 512), RingOffer{}); // gap 9600
    renderCmd(p, &input, 9600, 10000, {400}, {}, {}, out);
    CHECK(p.ringAdoptionsDeclined(1) == 1);
    CHECK(p.sources(1).ringAdoptionPending());
    RingOffer now;
    now.copiedTo = p.engine(1).liveWriteHead();
    REQUIRE(p.setRingNow(1, std::make_unique<RingStorage>(2 * 48000, 512), now));
    CHECK_FALSE(p.sources(1).ringAdoptionPending());
    CHECK(p.sources(1).ringsHeld() == 1);

    // Ring Length's seconds, for the plugin's watcher: the base value's.
    CHECK(p.ringSecondsRequested(1) == 16.0);
    p.setBase(slotOf(LayerParam::RingLength, 1), static_cast<float>(std::log2(20.0)));
    CHECK(p.ringSecondsRequested(1) == Catch::Approx(20.0).epsilon(1e-6));
    CHECK(p.ringSecondsRequested(0) == 16.0);
}

TEST_CASE("ringRead's wrap check", "[core][source]")
{
    // Spec §6.4: the main thread reads R0, copies [R0 − W, R0), reads R1 and accepts the copy
    // only if no frame written meanwhile can share a slot with one it read: R1 − R0 + W ≤ N
    // (frames up to a block past the published head may be in flight, and C − N is a block),
    // with the storage epoch unchanged. Here the "audio thread" renders in the middle of the
    // copy, on the same thread, over a 1 s ring.
    constexpr int64_t kN = 48000;
    const Stereo input = noise(4 * kN, 0.5f, 103);
    Processor p({kRate, 512, 31, Mode::Fx});
    REQUIRE(p.setRingNow(0, std::make_unique<RingStorage>(kN, 512), RingOffer{}));
    Stereo out = silence(4 * kN);
    renderCmd(p, &input, 0, 2 * kN, {512}, {}, {}, out);
    int64_t done = 2 * kN;
    const auto renderFor = [&](int64_t frames) {
        renderCmd(p, &input, done, done + frames, {512}, {}, {}, out);
        done += frames;
    };

    // Copies the newest `w` frames (RingRead's steps, with W given) in two halves with `between`
    // frames rendered in the middle; returns whether the check accepts it, and whether the copy
    // is the window as of R0.
    const auto copy = [&](int64_t w, int64_t between) {
        const std::atomic<uint32_t> *epoch = p.ringRead(0).storageEpoch;
        const uint32_t e0 = epoch->load(std::memory_order_acquire);
        const gg::core::RingRead r = p.ringRead(0);
        const RingStorage &s = *r.storage;
        w = std::min(w, s.frames());
        const int64_t r0 = r.head->load(std::memory_order_acquire);
        const int64_t oldest = std::max<int64_t>(0, r0 - w);
        const std::vector<float> truth = windowOf(p, 0, oldest, r0);
        std::vector<float> copied;
        const auto copyRange = [&](int64_t from, int64_t to) {
            for (int64_t f = from; f < to; ++f)
            {
                const int64_t slot = f % s.capacity();
                copied.push_back(s.slots()[2 * slot]);
                copied.push_back(s.slots()[2 * slot + 1]);
            }
        };
        copyRange(oldest, r0 - w / 2);
        renderFor(between);
        copyRange(r0 - w / 2, r0);
        // The fence before the closing reads: no frame load completes after R1 is sampled.
        std::atomic_thread_fence(std::memory_order_acquire);
        const int64_t r1 = r.head->load(std::memory_order_relaxed);
        const uint32_t e1 = epoch->load(std::memory_order_relaxed);
        const bool accepted = e0 == e1 && e0 % 2 == 0 && r1 - oldest <= s.frames();
        return std::pair{accepted, copied == truth};
    };

    SECTION("a short copy passes while the head moves")
    {
        CHECK(copy(1000, 512) == std::pair{true, true});
    }
    SECTION("a whole-window copy the head wrapped past fails, and was torn")
    {
        CHECK(copy(kN, 36000) == std::pair{false, false});
    }
    SECTION("a whole-window copy of a frozen ring passes")
    {
        renderCmd(p, &input, done, done + 1024, {512},
                  {{done, slotOf(LayerParam::Freeze, 0), false, 0.0}}, {}, out); // arms
        done += 1024;
        renderCmd(p, &input, done, done + 2048, {512},
                  {{done, slotOf(LayerParam::Freeze, 0), false, 1.0}}, {}, out);
        done += 2048; // past the 20 ms ramp: the head stands
        CHECK(copy(kN, 36000) == std::pair{true, true});
    }
    SECTION("an adoption in the middle changes the epoch")
    {
        auto next = std::make_unique<RingStorage>(kN, 512);
        RingOffer how;
        how.copiedTo = copyStable(p, 0, *next);
        p.sources(0).offerRing(std::move(next), how);
        CHECK_FALSE(copy(100, 512).first);
    }
}

TEST_CASE("a Resample layer, a loop with overdub and splices are block-size invariant",
          "[core][source][determinism]")
{
    // Spec §7: the tap, Loop, Loop Length, Overdub, Splice and the commands apply on the control
    // grid or by absolute time, so an FX and an instrument render with all of them in play are
    // the same bytes, the same grains and the same rings at any block split.
    const Stereo input = noise(kInvarianceFrames, 0.5f, 107);
    const std::vector<TimedEvent> events{
        {9601, slotOf(LayerParam::Loop, 0), false, 1.0},
        {14402, slotOf(LayerParam::Overdub, 0), false, 0.7},
        {48000, slotOf(LayerParam::Splice, 0), false, 1.0},
        {50005, slotOf(LayerParam::Overdub, 2), false, 0.5},
        {62403, slotOf(LayerParam::Splice, 0), false, 2.0},
        {76801, slotOf(LayerParam::Splice, 0), false, 0.0},
        {80007, slotOf(LayerParam::LoopLength, 0), true, 1.5},
        {86410, slotOf(LayerParam::Loop, 0), false, 0.0},
    };
    const std::vector<TimedCommand> commands{
        {24003, CommandKind::AddSplice, 0, 0.2f},
        {43211, CommandKind::AddSplice, 0, 0.1f},
        {70001, CommandKind::AddSplice, 2, 0.05f},
        {90000, CommandKind::ClearSplices, 0, 0.0f},
    };
    struct Result
    {
        Stereo audio;
        Log log;
        std::array<int64_t, 4> heads{};
        std::array<uint32_t, 4> splices{};
    };
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        INFO((mode == Mode::Fx ? "FX" : "instrument"));
        const auto renderSplit = [&](const std::vector<uint32_t> &blocks) {
            auto r = std::make_unique<Result>();
            Processor p({kRate, 4096, 47, mode});
            p.setEventSink(&Log::record, &r->log);
            p.setBase(ParamSlot::Mix, 0.7f);
            p.setBase(slotOf(LayerParam::Source, 0), 1.0f); // A Live in both products
            p.setBase(slotOf(LayerParam::LoopLength, 0), -2.0f);
            p.setBase(slotOf(LayerParam::Density, 0), 6.0f);
            p.setBase(slotOf(LayerParam::Source, 1), 2.0f); // B Resample, on, fed back
            p.setBase(layerOnSlot(1), 1.0f);
            p.setBase(slotOf(LayerParam::Feedback, 1), 0.3f);
            p.setBase(slotOf(LayerParam::Source, 2), 2.0f); // C Resample, off: its ring alone
            p.setBase(ParamSlot::ReverbAmount, 0.2f);
            const std::vector<TimedNote> notes =
                mode == Mode::Instrument ? std::vector<TimedNote>{{0, NoteEventType::On, 0, 60},
                                                                  {60000, NoteEventType::On, 0, 64}}
                                         : std::vector<TimedNote>{};
            r->audio = silence(kInvarianceFrames);
            renderCmd(p, &input, 0, kInvarianceFrames, blocks, events, commands, r->audio, 2,
                      &r->log, notes);
            for (uint8_t L = 0; L < 4; ++L)
            {
                r->heads[L] = p.engine(L).liveWriteHead();
                r->splices[L] = p.engine(L).live().spliceCount();
            }
            return r;
        };
        const auto ref = renderSplit({4096});
        REQUIRE(allFinite(ref->audio.left));
        REQUIRE(peak(ref->audio.left) > 0.05f);
        REQUIRE(ref->log.layer(0).size() > 20);
        REQUIRE(ref->log.layer(1).size() > 20);
        REQUIRE_FALSE(ref->log.overflowed);
        CHECK(ref->splices[2] == 1);
        for (const auto &blocks : {std::vector<uint32_t>{1}, std::vector<uint32_t>{32},
                                   std::vector<uint32_t>{511}, randomSplit(109)})
        {
            INFO("blocks " << blocks.size() << " first " << blocks.front());
            const auto r = renderSplit(blocks);
            REQUIRE(sameBits(r->audio.left, ref->audio.left));
            REQUIRE(sameBits(r->audio.right, ref->audio.right));
            REQUIRE(r->heads == ref->heads);
            REQUIRE(r->splices == ref->splices);
            for (uint32_t L = 0; L < 4; ++L)
            {
                INFO("layer " << L);
                REQUIRE(r->log.layer(L) == ref->log.layer(L));
            }
        }
    }
}

TEST_CASE("the tap, overdub, commands and ring adoptions do not allocate", "[core][source][rt]")
{
    // The Resample piece, the overdub piece, the commands, a resize adoption whose audio-thread
    // copy is 4,096 frames, a declined offer and a restore adoption, all armed.
    Processor p({kRate, 256, 51, Mode::Fx});
    p.setPublishing(true);
    p.setBase(ParamSlot::Mix, 0.8f);
    p.setBase(slotOf(LayerParam::Loop, 0), 1.0f);
    p.setBase(slotOf(LayerParam::LoopLength, 0), -2.0f);
    p.setBase(slotOf(LayerParam::Overdub, 0), 0.5f);
    p.setBase(slotOf(LayerParam::Feedback, 0), 0.3f);
    p.setBase(slotOf(LayerParam::Source, 1), 2.0f);
    p.setBase(layerOnSlot(1), 1.0f);
    p.setBase(slotOf(LayerParam::Source, 2), 2.0f);
    constexpr uint32_t kFrames = 1000;
    const Stereo input = noise(kFrames, 0.5f, 113);
    const float *in[2] = {input.left.data(), input.right.data()};
    std::vector<float> o0(kFrames), o1(kFrames);
    float *out[2] = {o0.data(), o1.data()};
    const std::vector<ParamEvent> events{
        {100, slotOf(LayerParam::Overdub, 1), false, 0.4},
        {600, slotOf(LayerParam::Splice, 0), false, 1.0},
        {900, slotOf(LayerParam::Loop, 0), false, 0.0},
    };
    const std::vector<CommandEvent> commands{
        {10, CommandKind::AddSplice, 0, 0.005f},
        {500, CommandKind::AddSplice, 0, 0.001f},
        {700, CommandKind::ClearSplices, 0, 0.0f},
        {800, CommandKind::AddSplice, 1, 0.0f},
    };
    for (int i = 0; i < 9; ++i)
    {
        p.process(in, 2, out, 2, kFrames, {}); // a head past the 4,096-frame gap
    }

    uint64_t allocations = 0;
    const auto armed = [&](const auto &body) {
        AllocTripwire::resetCount();
        {
            AllocTripwire::Scope scope;
            body();
        }
        allocations += AllocTripwire::count();
    };
    const auto calls = [&] {
        p.process(in, 2, out, 2, kFrames, events, {}, {}, commands);
        p.process(in, 2, out, 2, 0, {}, {}, {}, commands);
        p.process(in, 2, out, 1, 1, {}, {}, {}, commands);
        p.process(in, 1, out, 2, kFrames, events, {}, {}, commands);
    };

    auto resize = std::make_unique<RingStorage>(20 * 48000, 256);
    RingOffer how;
    how.copiedTo = copyStable(p, 0, *resize) - 4096; // the audio thread copies 4,096 frames
    p.sources(0).offerRing(std::move(resize), how);
    armed(calls);
    CHECK(p.engine(0).liveFrames() == 20 * 48000);

    auto declined = std::make_unique<RingStorage>(48000, 256);
    how.copiedTo = 0; // a gap past kAdoptCopyFrames
    p.sources(1).offerRing(std::move(declined), how);
    armed(calls);
    CHECK(p.ringAdoptionsDeclined(1) == 1);

    RingOffer restore;
    restore.restore = true;
    restore.frames = 30000;
    restore.spliceCount = 2;
    restore.splices[0] = 1000;
    restore.splices[1] = 20000;
    p.sources(2).offerRing(std::make_unique<RingStorage>(kRing, 256), restore);
    armed([&] {
        for (int i = 0; i < 5; ++i)
        {
            calls();
        }
    });
    CHECK(p.engine(2).liveWriteHead() > 30000);
    CHECK(allFinite(o0));
    if (AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("the worst Resample loop stays bounded", "[core][source][slow]")
{
    // Spec §6.2's stability argument, rendered: a Resample layer at Pitch 0 reading 256 samples
    // behind, Overdub 1 over a 50 ms loop (no decay at all), Feedback 0.95, Level +12 dB, dense
    // grains and the reverb at 100%, over 2 s of loud noise then silence, for 60 s: finite, and
    // |out| < 2 + |d| at every sample.
    constexpr int64_t kFrames = 60 * 48000;
    Stereo input = noise(kFrames, 0.9f, 127);
    std::fill(input.left.begin() + 2 * 48000, input.left.end(), 0.0f);
    std::fill(input.right.begin() + 2 * 48000, input.right.end(), 0.0f);
    Processor p({kRate, 512, 131, Mode::Fx});
    p.setBase(slotOf(LayerParam::Source, 0), 2.0f);
    p.setBase(slotOf(LayerParam::Overdub, 0), 1.0f);
    p.setBase(slotOf(LayerParam::Loop, 0), 1.0f);
    p.setBase(slotOf(LayerParam::LoopLength, 0), static_cast<float>(std::log2(0.05)));
    p.setBase(slotOf(LayerParam::Feedback, 0), gg::core::kFeedbackMax);
    p.setBase(slotOf(LayerParam::Position, 0), 0.0f);
    p.setBase(slotOf(LayerParam::Pitch, 0), 0.0f);
    p.setBase(slotOf(LayerParam::Density, 0), 7.0f);
    p.setBase(slotOf(LayerParam::Size, 0), -4.0f);
    p.setBase(ParamSlot::Level, 12.0f);
    p.setBase(ParamSlot::Mix, 0.5f);
    p.setBase(ParamSlot::ReverbAmount, 1.0f);
    Stereo out = silence(kFrames);
    renderCmd(p, &input, 0, kFrames, {512}, {}, {}, out);
    REQUIRE(allFinite(out.left));
    REQUIRE(allFinite(out.right));
    int64_t over = 0;
    for (std::size_t i = 0; i < out.left.size(); ++i)
    {
        over += std::abs(out.left[i]) >= 2.0f + std::abs(input.left[i]) ? 1 : 0;
        over += std::abs(out.right[i]) >= 2.0f + std::abs(input.right[i]) ? 1 : 0;
    }
    REQUIRE(over == 0);
    REQUIRE(peakIn(out.left, 50 * 48000, kFrames) > 0.01f); // still sounding: the loop holds
}
