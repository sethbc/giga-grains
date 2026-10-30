// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_TESTS_CORE_LAYERSFIXTURE_H
#define GG_TESTS_CORE_LAYERSFIXTURE_H

// What the four-layer tests share (layers_tests.cpp, threads_tests.cpp, mod_processor_tests.cpp,
// processor_tests.cpp): the signals, the grain log, the block-split renderer, the four-layer
// render of the block-size invariance test (M2 sub-project 1's spec §6.2–§6.4), which the
// thread-pool invariance test (§6.7) renders through each runner, and a route from every mod
// source (§6.8).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <numbers>
#include <vector>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Processor.h"
#include "gg/core/Rng.h"
#include "gg/engine/TaskRunner.h"
#include "gg/source/PreparedSource.h"
#include "gg/source/SampleBuffer.h"

namespace gg::test::layers
{
constexpr double kRate = 48000.0;

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;
};

inline Stereo silence(int64_t frames)
{
    return {std::vector<float>(static_cast<std::size_t>(frames), 0.0f),
            std::vector<float>(static_cast<std::size_t>(frames), 0.0f)};
}

inline double uniform(uint64_t seed, uint64_t index)
{
    return gg::rng::uniform01(gg::rng::hash({seed, 0, 0, gg::rng::Field::SignalNoise, index}));
}

// Stereo noise in ±amp from gg::rng, the two channels independent.
inline Stereo noise(int64_t frames, float amp, uint64_t seed)
{
    Stereo s = silence(frames);
    for (int64_t i = 0; i < frames; ++i)
    {
        using gg::rng::Field;
        const auto k = static_cast<uint64_t>(i);
        const auto j = static_cast<std::size_t>(i);
        s.left[j] = amp * static_cast<float>(
                              gg::rng::bipolar(gg::rng::hash({seed, 0, 0, Field::SignalNoise, k})));
        s.right[j] = amp * static_cast<float>(gg::rng::bipolar(
                               gg::rng::hash({seed, 0, 1, Field::SignalNoise, k})));
    }
    return s;
}

// A 1 s stereo sine at hz, 48 kHz, amplitude 0.5.
inline std::shared_ptr<const gg::PreparedSource> sine(double hz)
{
    constexpr int64_t kFrames = 48000;
    auto buf = std::make_shared<gg::SampleBuffer>(2, kFrames, kRate);
    for (uint32_t c = 0; c < 2; ++c)
    {
        for (int64_t i = 0; i < kFrames; ++i)
        {
            buf->channel(c)[i] = static_cast<float>(
                0.5 * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kRate));
        }
    }
    return gg::PreparedSource::make(std::move(buf));
}

inline bool sameBits(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

inline bool allFinite(const std::vector<float> &v)
{
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}

inline float peakIn(const std::vector<float> &v, int64_t from, int64_t to)
{
    float p = 0.0f;
    for (int64_t i = from; i < to; ++i)
    {
        p = std::max(p, std::abs(v[static_cast<std::size_t>(i)]));
    }
    return p;
}

inline float peak(const std::vector<float> &v)
{
    return peakIn(v, 0, static_cast<int64_t>(v.size()));
}

// Every spawned grain with its layer and the process() call it came from, into storage
// reserved up front: the sink runs inside process().
struct Log
{
    struct Event
    {
        uint32_t layer;
        uint64_t call;
        gg::GrainSpec grain;
    };
    static constexpr std::size_t kCapacity = 1 << 17;
    std::vector<Event> events;
    uint64_t call{0};
    bool overflowed{false};

    Log() { events.reserve(kCapacity); }

    static void record(void *ctx, uint32_t layer, const gg::GrainSpec &g)
    {
        auto *log = static_cast<Log *>(ctx);
        if (log->events.size() < kCapacity)
        {
            log->events.push_back({layer, log->call, g});
        }
        else
        {
            log->overflowed = true;
        }
    }

    // One layer's grains, in the order the sink saw them.
    std::vector<gg::GrainSpec> layer(uint32_t l) const
    {
        std::vector<gg::GrainSpec> out;
        for (const Event &e : events)
        {
            if (e.layer == l)
            {
                out.push_back(e.grain);
            }
        }
        return out;
    }
};

struct TimedEvent
{
    int64_t time; // absolute sample
    gg::core::ParamSlot slot;
    bool isModulation;
    double value;
};

struct TimedNote
{
    int64_t time; // absolute sample
    gg::core::NoteEventType type;
    int16_t channel;
    int16_t key;
    float velocity{1.0f};
};

// A MIDI control (the mod matrix's Mod Wheel, Aftertouch and Pitch Bend sources).
struct TimedControl
{
    int64_t time; // absolute sample
    gg::core::ControlKind kind;
    int16_t channel;
    float value;
};

// Renders the absolute samples [from, to) of `input` (null: no input) through p in process()
// calls of blockSizes in turn (cycling), each given the events, notes and controls that fall
// inside it (a 0-frame call at `done` takes those stamped `done`), into out[from, to), which must
// be that long. Each call bumps log->call, if a log is given. `after`, if given, runs after each
// call.
inline void render(gg::core::Processor &p, const Stereo *input, int64_t from, int64_t to,
                   const std::vector<uint32_t> &blockSizes, const std::vector<TimedEvent> &events,
                   Stereo &out, Log *log = nullptr, const std::function<void(int64_t)> &after = {},
                   const std::vector<TimedNote> &notes = {},
                   const std::vector<TimedControl> &controls = {})
{
    std::vector<gg::core::ParamEvent> blockEvents;
    blockEvents.reserve(events.size());
    std::vector<gg::core::NoteEvent> blockNotes;
    blockNotes.reserve(notes.size());
    std::vector<gg::core::ControlEvent> blockControls;
    blockControls.reserve(controls.size());
    std::size_t nextEvent = 0;
    while (nextEvent < events.size() && events[nextEvent].time < from)
    {
        ++nextEvent;
    }
    std::size_t nextNote = 0;
    while (nextNote < notes.size() && notes[nextNote].time < from)
    {
        ++nextNote;
    }
    std::size_t nextControl = 0;
    while (nextControl < controls.size() && controls[nextControl].time < from)
    {
        ++nextControl;
    }
    int64_t done = from;
    for (std::size_t next = 0; done < to; ++next)
    {
        const uint32_t want = blockSizes[next % blockSizes.size()];
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
        blockControls.clear();
        for (; nextControl < controls.size() && controls[nextControl].time < limit; ++nextControl)
        {
            const TimedControl &c = controls[nextControl];
            blockControls.push_back(
                {static_cast<uint32_t>(c.time - done), c.kind, c.channel, c.value});
        }
        const auto j = static_cast<std::size_t>(done);
        const float *in[2] = {input != nullptr ? input->left.data() + j : nullptr,
                              input != nullptr ? input->right.data() + j : nullptr};
        float *o[2] = {out.left.data() + j, out.right.data() + j};
        p.process(input != nullptr ? in : nullptr, input != nullptr ? 2 : 0, o, 2, n, blockEvents,
                  blockNotes, blockControls);
        done += n;
        if (log != nullptr)
        {
            ++log->call;
        }
        if (after)
        {
            after(done);
        }
    }
}

constexpr int64_t kInvarianceFrames = 2 * 48000;

// Every layer's built parameters automated (spec §6.2–§6.4), each with 12 events off the grid,
// every third a modulation offset of up to a quarter of the range: the M1 layer parameters,
// Source (Sample, Live and the unbuilt Resample), the spreads, each layer's Level and Layer On
// (on 70% of the time, so layers go off for longer than the ramp and come back), Morph X and
// Y swept, and each layer's Bus Split with both buses' filter type, cutoff and resonance. Times and
// values come from gg::rng.
inline std::vector<TimedEvent> layerAutomation()
{
    using gg::core::LayerParam;
    using gg::core::ParamSlot;
    struct Range
    {
        LayerParam param;
        float lo, hi;
    };
    const std::array<Range, 14> layerRanges{{{LayerParam::Position, 0.0f, 0.6f},
                                             {LayerParam::Size, -7.0f, -3.0f},
                                             {LayerParam::Density, 1.0f, 6.0f},
                                             {LayerParam::Distribution, 0.0f, 1.0f},
                                             {LayerParam::Pitch, -24.0f, 24.0f},
                                             {LayerParam::Spray, 0.0f, 1.0f},
                                             {LayerParam::Stereo, 0.0f, 1.0f},
                                             {LayerParam::Freeze, 0.0f, 1.0f},
                                             {LayerParam::Texture, 0.0f, 1.0f},
                                             {LayerParam::Feedback, 0.0f, 0.5f},
                                             {LayerParam::Diffusion, 0.0f, 1.0f},
                                             {LayerParam::Source, 0.0f, 2.0f},
                                             {LayerParam::PitchSpread, 0.0f, 12.0f},
                                             {LayerParam::SizeSpread, 0.0f, 1.0f}}};
    struct Target
    {
        ParamSlot slot;
        float lo, hi;
        bool stepped;
    };
    std::vector<Target> targets;
    for (uint8_t L = 0; L < 4; ++L)
    {
        for (const Range &r : layerRanges)
        {
            const bool stepped = r.param == LayerParam::Freeze || r.param == LayerParam::Source;
            targets.push_back({gg::core::layerSlot(r.param, L), r.lo, r.hi, stepped});
        }
        targets.push_back({gg::core::levelSlot(L), -18.0f, 6.0f, false});
        targets.push_back({gg::core::layerOnSlot(L), 0.0f, 1.0f, true});
    }
    targets.push_back({ParamSlot::MorphX, 0.0f, 1.0f, false});
    targets.push_back({ParamSlot::MorphY, 0.0f, 1.0f, false});
    targets.push_back({ParamSlot::Mix, 0.5f, 1.0f, false});
    // The grain buses (extremes spec §6.3), after the rest so that the older targets keep their
    // events: each layer's Bus Split, and each bus's filter type (stepped), cutoff and resonance.
    for (uint8_t L = 0; L < 4; ++L)
    {
        targets.push_back({gg::core::layerSlot(LayerParam::BusSplit, L), 0.0f, 1.0f, false});
        for (const LayerParam f : {LayerParam::Bus1Filter, LayerParam::Bus2Filter})
        {
            targets.push_back({gg::core::layerSlot(f, L), 0.0f, 4.0f, true});
        }
        for (const LayerParam c : {LayerParam::Bus1Cutoff, LayerParam::Bus2Cutoff})
        {
            targets.push_back({gg::core::layerSlot(c, L), 5.0f, 13.0f, false});
        }
        for (const LayerParam r : {LayerParam::Bus1Resonance, LayerParam::Bus2Resonance})
        {
            targets.push_back({gg::core::layerSlot(r, L), 0.0f, 1.0f, false});
        }
    }

    std::vector<TimedEvent> events;
    for (std::size_t t = 0; t < targets.size(); ++t)
    {
        const Target &r = targets[t];
        for (int k = 0; k < 12; ++k)
        {
            const uint64_t key = 700000 + t * 1000 + static_cast<uint64_t>(k) * 4;
            auto time = static_cast<int64_t>(uniform(91, key) * kInvarianceFrames);
            if (time % 32 == 0)
            {
                time += 1 + k % 31; // off the grid
            }
            time = std::min(time, kInvarianceFrames - 1);
            const double u = uniform(91, key + 1);
            const bool isModulation = !r.stepped && k % 3 == 2;
            double value = 0.0;
            if (isModulation)
            {
                value = (2.0 * u - 1.0) * (r.hi - r.lo) / 4.0;
            }
            else if (r.slot == gg::core::layerOnSlot(0) || r.slot == gg::core::layerOnSlot(1) ||
                     r.slot == gg::core::layerOnSlot(2) || r.slot == gg::core::layerOnSlot(3))
            {
                value = u < 0.7 ? 1.0 : 0.0;
            }
            else if (r.stepped)
            {
                value = std::floor(r.lo + (r.hi - r.lo + 1.0) * u);
            }
            else
            {
                value = r.lo + (r.hi - r.lo) * u;
            }
            events.push_back({time, r.slot, isModulation, value});
        }
    }
    std::stable_sort(events.begin(), events.end(),
                     [](const TimedEvent &a, const TimedEvent &b) { return a.time < b.time; });
    return events;
}

// Block sizes drawn from gg::rng covering kInvarianceFrames: a tenth 0-frame calls, three in
// ten multiples of 32, the rest anything in 1..4096.
inline std::vector<uint32_t> randomSplit(uint64_t seed)
{
    std::vector<uint32_t> sizes;
    int64_t total = 0;
    for (uint64_t i = 0; total < kInvarianceFrames; ++i)
    {
        const double kind = uniform(seed, 2 * i);
        const double u = uniform(seed, 2 * i + 1);
        const uint32_t n = kind < 0.1   ? 0
                           : kind < 0.4 ? 32 * (1 + static_cast<uint32_t>(u * 128))
                                        : 1 + static_cast<uint32_t>(u * 4096);
        sizes.push_back(n);
        total += n;
    }
    return sizes;
}

// A route of `source` (its own layer for a per-layer source) to `target` on `targetLayer`.
inline gg::core::ModRoute route(gg::core::ModSource source, gg::core::ParamSlot target,
                                uint8_t targetLayer, float depth, gg::core::ModCurve curve,
                                uint8_t sourceLayer = gg::core::kSameLayer)
{
    return gg::core::ModRoute{source, sourceLayer, target, targetLayer, depth, curve};
}

// Every source routed somewhere (one route per source; the per-layer ones from each target's own
// layer, on every layer), after the factory routes: the routes of the invariance and allocation
// tests.
inline gg::core::ModRoutes allSourceRoutes(gg::core::Mode mode)
{
    using gg::core::kAllLayers;
    using gg::core::ModCurve;
    using gg::core::ModSource;
    using gg::core::ParamSlot;
    gg::core::ModRoutes r = gg::core::factoryRoutes(mode);
    const std::array<gg::core::ModRoute, 16> extra{{
        route(ModSource::Lfo1, ParamSlot::Position, kAllLayers, 0.2f, ModCurve::Bipolar),
        route(ModSource::Lfo2, ParamSlot::Pitch, kAllLayers, 3.0f, ModCurve::Bipolar),
        route(ModSource::Env1, ParamSlot::Level, kAllLayers, 6.0f, ModCurve::Unipolar),
        route(ModSource::Env2, ParamSlot::Texture, kAllLayers, 0.5f, ModCurve::Unipolar),
        route(ModSource::Random1, ParamSlot::Spray, kAllLayers, 0.2f, ModCurve::Bipolar),
        route(ModSource::Random2, ParamSlot::Stereo, kAllLayers, 0.5f, ModCurve::Unipolar),
        route(ModSource::Step, ParamSlot::Density, kAllLayers, 1.0f, ModCurve::Bipolar),
        route(ModSource::ModWheel, ParamSlot::Size, kAllLayers, 1.0f, ModCurve::Bipolar),
        route(ModSource::Aftertouch, ParamSlot::Distribution, kAllLayers, 0.5f, ModCurve::Unipolar),
        route(ModSource::PitchBend, ParamSlot::Pitch, kAllLayers, 2.0f, ModCurve::Bipolar),
        route(ModSource::Velocity, ParamSlot::Level, kAllLayers, -6.0f, ModCurve::Unipolar),
        route(ModSource::Key, ParamSlot::PitchSpread, kAllLayers, 2.0f, ModCurve::Unipolar),
        route(ModSource::EnvFollower, ParamSlot::Diffusion, kAllLayers, 0.5f, ModCurve::Unipolar),
        route(ModSource::TransportBeat, ParamSlot::MorphX, kAllLayers, 0.3f, ModCurve::Bipolar),
        route(ModSource::TransportBar, ParamSlot::MorphY, kAllLayers, 0.3f, ModCurve::Bipolar),
        route(ModSource::TransportPlaying, ParamSlot::Mix, kAllLayers, -0.2f, ModCurve::Unipolar),
    }};
    std::copy(extra.begin(), extra.end(), r.begin() + 12);
    return r;
}

struct LayersRender
{
    Stereo audio;
    Log log;
    std::array<int64_t, 4> heads{};
    // Every mod source's value on every layer after the render (Processor::sourceValue).
    std::array<std::array<float, static_cast<std::size_t>(gg::core::ModSource::Count)>, 4>
        sources{};
};

// How renderLayers sets its Processor up beyond the invariance test's defaults: the product, the
// task runner (nullptr: the built-in serial one), the notes, the MIDI controls, and anything else
// (routes, a transport), before the first call.
struct LayersSetup
{
    gg::core::Mode mode{gg::core::Mode::Fx};
    gg::TaskRunner *runner{nullptr};
    std::vector<TimedNote> notes;
    std::function<void(gg::core::Processor &)> configure;
    std::vector<TimedControl> controls;
};

// The four-layer render of the block-size invariance test: every layer on, each with a sine
// sample for the times it reads one, `input` into the rings (and the FX's dry side), `events`
// applied, in calls of blockSizes.
inline void renderLayers(const Stereo &input, const std::vector<TimedEvent> &events,
                         uint32_t maxBlock, const std::vector<uint32_t> &blockSizes,
                         LayersRender &r, const LayersSetup &setup = {})
{
    gg::core::Processor p({kRate, maxBlock, 47, setup.mode});
    p.setTaskRunner(setup.runner);
    p.setEventSink(&Log::record, &r.log);
    const std::array<double, 4> hz{196.0, 293.66, 440.0, 659.25};
    for (uint8_t L = 0; L < 4; ++L)
    {
        p.setBase(gg::core::layerOnSlot(L), 1.0f);
        p.setSourceNow(L, sine(hz[L])); // for the layers that switch to Sample
    }
    if (setup.configure)
    {
        setup.configure(p);
    }
    r.audio = silence(kInvarianceFrames);
    render(p, &input, 0, kInvarianceFrames, blockSizes, events, r.audio, nullptr, {}, setup.notes,
           setup.controls);
    for (uint8_t L = 0; L < 4; ++L)
    {
        r.heads[L] = p.engine(L).liveWriteHead();
        for (std::size_t s = 0; s < r.sources[L].size(); ++s)
        {
            r.sources[L][s] = p.sourceValue(static_cast<gg::core::ModSource>(s), L);
        }
    }
}
} // namespace gg::test::layers

#endif // GG_TESTS_CORE_LAYERSFIXTURE_H
