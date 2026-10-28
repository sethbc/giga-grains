// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "Render.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

#include <fmt/format.h>

#include "Signals.h"
#include "gg/core/Processor.h"
#include "gg/engine/Engine.h"
#include "gg/source/SampleBuffer.h"

namespace gg::render
{
namespace
{
/// Collects the grains spawned during one process() call, and the latest grainEndSample of all
/// grains so far. The engine calls the sink from inside process(), which is annotated
/// non-blocking, so the sink only copies into storage reserved up front and never allocates;
/// that keeps RTSan builds of the tests clean. The lines are formatted between process() calls.
struct GrainCollector
{
    explicit GrainCollector(std::size_t capacity) { pending.reserve(capacity); }

    static void sink(void *ctx, const GrainSpec &g)
    {
        auto &self = *static_cast<GrainCollector *>(ctx);
        self.latestEnd = std::max(self.latestEnd, grainEndSample(g));
        if (self.pending.size() < self.pending.capacity())
        {
            self.pending.push_back(g);
        }
        else
        {
            self.overflowed = true;
        }
    }

    std::vector<GrainSpec> pending;
    int64_t latestEnd{-1}; ///< -1 until a grain spawns
    bool overflowed{false};
};

std::shared_ptr<const SampleBuffer> toSampleBuffer(const gg::metrics::AudioData &audio)
{
    const auto channels = static_cast<uint32_t>(audio.channels.size());
    std::size_t frames = channels > 0 ? audio.channels[0].size() : 0;
    for (const auto &c : audio.channels)
    {
        frames = std::min(frames, c.size());
    }
    auto buffer =
        std::make_shared<SampleBuffer>(channels, static_cast<int64_t>(frames), audio.sampleRate);
    for (uint32_t c = 0; c < channels; ++c)
    {
        std::copy_n(audio.channels[c].begin(), frames, buffer->channel(c));
    }
    return buffer;
}

CloudParams toCloudParams(const StreamSpec &s)
{
    return {
        .densityHz = s.grainRateHz,
        .distribution = s.distribution,
        .grainDurationSec = s.grainDurationMs / 1000.0,
        .position = s.position,
        .spray = s.positionJitter,
        .pitchSemitones = s.pitchSemitones,
        .stereo = 0.0,
        .gain = static_cast<float>(s.gain),
        .quality = s.quality,
    };
}

/// The most grains one process() call of `frames` frames can spawn. The scheduler's own onsets
/// are at least CloudScheduler::kMinIntervalSamples (1/16) apart, less the rounding of t + Δ,
/// which is at most 2^-22 samples while t is below 2^32 (every render gg-render accepts is
/// shorter): fewer than frames / (1/16 − 2^-22) + 1 grains, at most 16 × frames + 5. A note-on
/// into an empty held set forces an onset at its grid boundary, however close to the one before
/// (spec §6.2), so the minimum interval doesn't hold there: such forced onsets add at most one
/// per boundary, frames / kControlGrid + 2 in a call. The code reserves
/// 16 × frames + frames / kControlGrid + 16: the extra slack over the derived + 7 is deliberate,
/// so the bound doesn't rest on that rounding analysis alone. The bound holds for a Processor
/// call too, which renders in pieces split at grid boundaries: the intervals are between
/// consecutive onsets, whichever piece they fall in.
std::size_t maxGrainsPerCall(uint32_t frames)
{
    const auto perFrame = static_cast<std::size_t>(1.0 / CloudScheduler::kMinIntervalSamples);
    return perFrame * frames + frames / gg::core::Processor::kControlGrid + 16;
}

/// Moves the grains collected during one process() call into eventLines, if it isn't null.
void takeEventLines(GrainCollector &grains, std::vector<std::string> *eventLines)
{
    if (eventLines != nullptr)
    {
        for (const GrainSpec &g : grains.pending)
        {
            eventLines->push_back(formatEventLine(g));
        }
    }
    grains.pending.clear();
}

gg::metrics::AudioData renderSample(const RenderSpec &spec, std::vector<std::string> *eventLines)
{
    const auto source = toSampleBuffer(makeSignal(spec.source, spec.sampleRate, spec.seed));

    EngineConfig config;
    config.sampleRate = spec.sampleRate;
    config.maxBlockSize = spec.blockSize;
    config.seed = spec.seed;
    Engine engine(config);
    engine.setSource(source);
    engine.setCloudParams(toCloudParams(spec.stream));

    GrainCollector grains(maxGrainsPerCall(spec.blockSize));
    engine.setEventSink(&GrainCollector::sink, &grains);
    if (eventLines != nullptr)
    {
        eventLines->clear();
    }

    gg::metrics::AudioData out{spec.sampleRate, {{}, {}}};
    // Renders frames [from, to) into `out`, which must already hold `to` frames.
    const auto renderFrames = [&](std::size_t from, std::size_t to) {
        for (std::size_t done = from; done < to;)
        {
            const auto n = static_cast<uint32_t>(std::min<std::size_t>(spec.blockSize, to - done));
            float *const block[2] = {out.channels[0].data() + done, out.channels[1].data() + done};
            engine.process(block, 2, n);
            done += n;
            takeEventLines(grains, eventLines);
        }
    };

    // durationSeconds of stream activity.
    const int64_t duration = framesFor(spec.durationSeconds, spec.sampleRate);
    for (auto &channel : out.channels)
    {
        channel.resize(static_cast<std::size_t>(duration));
    }
    renderFrames(0, static_cast<std::size_t>(duration));

    // The release tail: with the stream off, render until every grain has finished, plus one
    // silent frame. The end comes from the grains, not from polling activeGrains() once per
    // block, so the length doesn't depend on blockSize.
    CloudParams off = toCloudParams(spec.stream);
    off.densityHz = 0.0; // off (ruling M1-a)
    engine.setCloudParams(off);
    const int64_t end = std::max(duration, grains.latestEnd + 1);
    // Defensive bound. No grain is longer than the stream's grain duration (at least 16 frames,
    // as the engine clamps it), and one that started on the last frame of the duration ends
    // floor(length) + 2 frames later (grainEndSample), so the tail never needs more. Hitting the
    // bound means the engine broke that promise, an internal error: gg-render then exits with
    // status 3.
    const double longestGrain =
        std::max(16.0, spec.stream.grainDurationMs / 1000.0 * spec.sampleRate);
    const int64_t tailBound = static_cast<int64_t>(longestGrain) + 2 + spec.blockSize;
    if (end - duration > tailBound)
    {
        throw std::logic_error(
            fmt::format("the release tail would last {} frames, more than the {}-frame bound",
                        end - duration, tailBound));
    }
    for (auto &channel : out.channels)
    {
        channel.resize(static_cast<std::size_t>(end));
    }
    renderFrames(static_cast<std::size_t>(duration), static_cast<std::size_t>(end));

    if (engine.activeGrains() != 0)
    {
        throw std::logic_error("grains are still sounding after the release tail");
    }
    if (grains.overflowed)
    {
        throw std::logic_error("more grains spawned in one block than the scheduler allows");
    }
    return out;
}

gg::core::ParamSlot slotNamed(const std::string &name)
{
    const auto slot = paramSlotNamed(name);
    if (!slot)
    {
        throw std::invalid_argument("no parameter is named " + name);
    }
    return *slot;
}

/// The notes as note events at absolute frames, in frame order: an On at each note's start and
/// an Off at its end, unless that is at or past `frames`. On one frame the Offs go first, so a
/// key that ends where it starts again stays held; otherwise the notes' order is kept.
struct TimedNote
{
    std::size_t frame;
    gg::core::NoteEvent event; ///< Its offset is set per call.
};

std::vector<TimedNote> timedNotes(const std::vector<NoteSpec> &notes, double sampleRate,
                                  std::size_t frames)
{
    using gg::core::NoteEventType;
    std::vector<TimedNote> timed;
    timed.reserve(2 * notes.size());
    for (const NoteSpec &n : notes)
    {
        const auto channel = static_cast<int16_t>(n.channel);
        const auto key = static_cast<int16_t>(n.key);
        timed.push_back({static_cast<std::size_t>(framesFor(n.timeSeconds, sampleRate)),
                         {0, NoteEventType::On, channel, key}});
        const auto off =
            static_cast<std::size_t>(framesFor(n.timeSeconds + n.durationSeconds, sampleRate));
        if (off < frames)
        {
            timed.push_back({off, {0, NoteEventType::Off, channel, key}});
        }
    }
    std::stable_sort(timed.begin(), timed.end(), [](const TimedNote &a, const TimedNote &b) {
        const bool aOn = a.event.type == NoteEventType::On;
        const bool bOn = b.event.type == NoteEventType::On;
        return a.frame < b.frame || (a.frame == b.frame && !aOn && bOn);
    });
    return timed;
}

/// Instrument and FX mode: the plugin's Processor, fed as a host feeds it.
gg::metrics::AudioData renderProcessor(const RenderSpec &spec, std::vector<std::string> *eventLines)
{
    const bool fx = spec.mode == RenderMode::Fx;
    const auto &params = fx ? spec.fx.params : spec.instrument.params;
    const auto &automationPoints = fx ? spec.fx.automation : spec.instrument.automation;
    const auto frames = static_cast<std::size_t>(framesFor(spec.durationSeconds, spec.sampleRate));

    // The FX's input, silent after its own length (and cut at the render's end); the instrument
    // has none.
    gg::metrics::AudioData input;
    if (fx)
    {
        input = makeInput(spec.fx.input, spec.fx.channels, spec.sampleRate, spec.seed);
        for (auto &channel : input.channels)
        {
            channel.resize(frames, 0.0f);
        }
    }
    const auto inputChannels = static_cast<uint32_t>(input.channels.size());

    gg::core::Processor processor({spec.sampleRate, spec.blockSize, spec.seed,
                                   fx ? gg::core::Mode::Fx : gg::core::Mode::Instrument});
    if (!fx)
    {
        processor.setSourceNow(makeSample(spec.instrument.sample, spec.sampleRate, spec.seed));
    }
    for (const auto &[name, value] : params)
    {
        processor.setBase(slotNamed(name), static_cast<float>(value));
    }

    // The automation as events at absolute frames, in time order.
    struct TimedEvent
    {
        std::size_t frame;
        gg::core::ParamSlot slot;
        double value;
    };
    std::vector<TimedEvent> automation;
    automation.reserve(automationPoints.size());
    for (const AutomationPoint &p : automationPoints)
    {
        automation.push_back({static_cast<std::size_t>(framesFor(p.timeSeconds, spec.sampleRate)),
                              slotNamed(p.param), p.value});
    }
    const std::vector<TimedNote> notes =
        timedNotes(fx ? spec.fx.notes : spec.instrument.notes, spec.sampleRate, frames);

    GrainCollector grains(maxGrainsPerCall(spec.blockSize));
    processor.setEventSink(&GrainCollector::sink, &grains);
    if (eventLines != nullptr)
    {
        eventLines->clear();
    }

    gg::metrics::AudioData out{spec.sampleRate,
                               {std::vector<float>(frames), std::vector<float>(frames)}};
    std::vector<gg::core::ParamEvent> events;
    events.reserve(automation.size());
    std::vector<gg::core::NoteEvent> noteEvents;
    noteEvents.reserve(notes.size());
    std::size_t next = 0;     // the first automation event not yet handed to the processor
    std::size_t nextNote = 0; // the first note event not yet handed to it
    for (std::size_t done = 0; done < frames;)
    {
        const auto n = static_cast<uint32_t>(std::min<std::size_t>(spec.blockSize, frames - done));
        // This call's events, at their offsets from its first frame. The Processor merges the
        // two kinds by offset.
        events.clear();
        for (; next < automation.size() && automation[next].frame < done + n; ++next)
        {
            const TimedEvent &e = automation[next];
            events.push_back({static_cast<uint32_t>(e.frame - done), e.slot, false, e.value});
        }
        noteEvents.clear();
        for (; nextNote < notes.size() && notes[nextNote].frame < done + n; ++nextNote)
        {
            gg::core::NoteEvent e = notes[nextNote].event;
            e.offset = static_cast<uint32_t>(notes[nextNote].frame - done);
            noteEvents.push_back(e);
        }
        const float *const in[2] = {inputChannels > 0 ? input.channels[0].data() + done : nullptr,
                                    inputChannels > 1 ? input.channels[1].data() + done : nullptr};
        float *const block[2] = {out.channels[0].data() + done, out.channels[1].data() + done};
        processor.process(in, inputChannels, block, 2, n, events, noteEvents);
        done += n;
        takeEventLines(grains, eventLines);
    }

    if (grains.overflowed)
    {
        throw std::logic_error("more grains spawned in one block than the scheduler allows");
    }
    return out;
}
} // namespace

std::string formatEventLine(const GrainSpec &g)
{
    return fmt::format(
        R"({{"i":{},"onset":{},"frac":{},"pos":{},"rate":{},"dur":{},"gain":{},"pan":{}}})",
        g.index, g.onsetSample, g.onsetFrac, g.startPos, g.rate, g.durationSamples, g.gain, g.pan);
}

gg::metrics::AudioData render(const RenderSpec &spec, std::vector<std::string> *eventLines)
{
    return spec.mode == RenderMode::Sample ? renderSample(spec, eventLines)
                                           : renderProcessor(spec, eventLines);
}
} // namespace gg::render
