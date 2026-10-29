// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/Processor.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>

#include "gg/core/DenormalGuard.h"
#include "gg/core/ParamMap.h"
#include "sst/basic-blocks/params/ParamMetadata.h"

namespace gg::core
{
// SpaceReverb's chunks must be the control grid's cells, so that its latched parameters and its
// idle rule change on grid boundaries alone (the class comment).
static_assert(SpaceReverb::kChunk == Processor::kControlGrid);
// Feedback's delay is the ring's head margin (sub-project 5's spec §5): the admission clamp keeps
// every grain at least that far behind the write head, so a piece of at most D samples never
// reads a frame whose feedback it hasn't rendered yet.
static_assert(Processor::kFeedbackDelay == gg::LiveSource::kHeadMargin);

namespace
{
EngineConfig engineConfig(const ProcessorConfig &c)
{
    EngineConfig e;
    e.sampleRate = c.sampleRate;
    e.maxBlockSize = c.maxBlockSize;
    e.seed = c.seed;
    e.liveSeconds = c.mode == Mode::Fx ? Processor::kLiveSeconds : 0.0;
    return e;
}

/// R = round(kRampSeconds × sampleRate), at least 1. The min keeps llround's argument in range
/// for any finite rate.
int64_t rampLength(double sampleRate)
{
    const double samples = std::min(Processor::kRampSeconds * sampleRate, 0x1.0p40);
    return std::max<int64_t>(1, std::llround(samples));
}

/// The first grid boundary at or after the absolute sample a >= 0.
int64_t gridCeil(int64_t a) noexcept
{
    constexpr auto grid = static_cast<int64_t>(Processor::kControlGrid);
    return (a + grid - 1) / grid * grid;
}

float finiteOrZero(float x) noexcept { return std::isfinite(x) ? x : 0.0f; }

/// A missing ring-input channel, for the live peaks: the FX's pieces are at most
/// kFeedbackDelay samples.
constexpr std::array<float, Processor::kFeedbackDelay> kSilence{};

/// Whether a note event's fields are in range (NoteEvent): On needs a channel 0..15 and a key
/// 0..127, Off also takes −1 for either, AllOff takes a channel 0..15 or −1 and ignores the key.
bool validNote(const NoteEvent &n) noexcept
{
    const auto inRange = [](int16_t v, int16_t lo, int16_t hi) { return v >= lo && v <= hi; };
    switch (n.type)
    {
    case NoteEventType::On:
        return inRange(n.channel, 0, 15) && inRange(n.key, 0, 127);
    case NoteEventType::Off:
        return inRange(n.channel, -1, 15) && inRange(n.key, -1, 127);
    case NoteEventType::AllOff:
        return inRange(n.channel, -1, 15);
    }
    return false;
}
} // namespace

float Processor::Ramp::at(int64_t t, int64_t length) const noexcept
{
    const int64_t i = t - start;
    if (i >= length)
    {
        return to;
    }
    const auto f = static_cast<double>(from);
    const double delta = static_cast<double>(to) - f;
    return static_cast<float>(f + delta * (static_cast<double>(i) / static_cast<double>(length)));
}

void Processor::Ramp::retarget(float target, int64_t t, int64_t length) noexcept
{
    if (target == to)
    {
        return;
    }
    from = at(t, length);
    to = target;
    start = t;
}

void Processor::Ramp::jump(float target) noexcept
{
    from = target;
    to = target;
    start = 0;
}

Processor::Processor(const ProcessorConfig &config)
    : config_(config), engine_(engineConfig(config)), rampSamples_(rampLength(config.sampleRate)),
      reverb_(std::make_unique<SpaceReverb>(config.sampleRate))
{
    diffuser_.prepare(config_.sampleRate);
    for (auto &channel : wet_)
    {
        channel.assign(config_.maxBlockSize, 0.0f);
    }
    if (config_.mode == Mode::Fx)
    {
        for (auto &channel : ringIn_)
        {
            channel.assign(config_.maxBlockSize, 0.0f);
        }
    }
    // The first paramMeta() call builds its tables, which allocates; make every call here, off
    // the audio thread, so that process() never makes it, and keep the ranges.
    for (size_t s = 0; s < kParamCount; ++s)
    {
        const auto &meta = paramMeta(static_cast<ParamSlot>(s), config_.mode);
        minVal_[s] = meta.minVal;
        maxVal_[s] = meta.maxVal;
        base_[s] = clampToRange(s, meta.defaultVal);
    }
    if (config_.mode == Mode::Fx)
    {
        engine_.setSourceMode(SourceMode::Live);
    }
    else
    {
        engine_.setNoteGate(true); // silent until a note (spec §6.2)
    }
    // Publishing (sub-project 5's spec §6.4): the engine's sink is ours for good, and forwards to
    // setEventSink's. The peak strip spans the ring, N = ceil(kLiveSeconds × sampleRate) frames.
    grainQueue_ = std::make_unique<GrainQueue>();
    if (config_.mode == Mode::Fx)
    {
        livePeaks_.prepare(engine_.liveFrames());
    }
    engine_.setEventSink(&Processor::onGrain, this);
    updateEffective();
    jumpRamps();
}

void Processor::setSeed(uint64_t seed) noexcept
{
    config_.seed = seed;
    engine_.setSeed(seed);
}

void Processor::jumpRamps() noexcept
{
    const WetParams w = toWetParams(effective_, config_.mode);
    mix_.jump(w.mix);
    level_.jump(w.levelGain);
    diffusion_.jump(space_.diffusion);
    amount_.jump(space_.amount);
    drySend_.jump(space_.drySend);
    feedback_.jump(w.feedback);
}

float Processor::clampToRange(size_t slot, float value) const noexcept
{
    return std::clamp(value, minVal_[slot], maxVal_[slot]);
}

void Processor::setBase(ParamSlot slot, float value) noexcept
{
    const auto s = static_cast<size_t>(slot);
    if (s >= kParamCount || std::isnan(value))
    {
        return;
    }
    base_[s] = clampToRange(s, value);
    hasPendingBase_[s] = false;
    updateEffective();
    if (slot == ParamSlot::Freeze || slot == ParamSlot::Macro6)
    {
        syncFreeze(); // Freeze set off is a transition's first half, even between calls
    }
    const WetParams w = toWetParams(effective_, config_.mode);
    if (isMacro(slot))
    {
        // A macro moves its targets' effective values (sub-project 5's spec §3.1): each ramped
        // value whose target it changed jumps, as a setBase of the target itself makes it; the
        // others keep their ramps.
        if (w.mix != mix_.to)
        {
            mix_.jump(w.mix);
        }
        if (w.levelGain != level_.to)
        {
            level_.jump(w.levelGain);
        }
        if (space_.diffusion != diffusion_.to)
        {
            diffusion_.jump(space_.diffusion);
        }
        if (space_.amount != amount_.to)
        {
            amount_.jump(space_.amount);
        }
        if (space_.drySend != drySend_.to)
        {
            drySend_.jump(space_.drySend);
        }
        if (w.feedback != feedback_.to)
        {
            feedback_.jump(w.feedback);
        }
        return;
    }
    switch (slot)
    {
    case ParamSlot::Mix:
        mix_.jump(w.mix);
        break;
    case ParamSlot::Level:
        level_.jump(w.levelGain);
        break;
    case ParamSlot::Diffusion:
        diffusion_.jump(space_.diffusion);
        break;
    case ParamSlot::ReverbAmount:
        amount_.jump(space_.amount);
        break;
    case ParamSlot::DryToReverb:
        drySend_.jump(space_.drySend);
        break;
    case ParamSlot::Feedback:
        feedback_.jump(w.feedback);
        break;
    default:
        break;
    }
}

// ParamSlot::Count asserts; in release builds it reads the last slot rather than out of bounds,
// as paramMeta does.
float Processor::base(ParamSlot slot) const noexcept
{
    const auto s = static_cast<size_t>(slot);
    assert(s < kParamCount);
    return base_[std::min(s, kParamCount - 1)];
}

float Processor::effective(ParamSlot slot) const noexcept
{
    const auto s = static_cast<size_t>(slot);
    assert(s < kParamCount);
    return effective_[std::min(s, kParamCount - 1)];
}

void Processor::commitPending() noexcept
{
    for (size_t s = 0; s < kParamCount; ++s)
    {
        if (hasPendingBase_[s])
        {
            base_[s] = pendingBase_[s];
        }
        if (hasPendingMod_[s])
        {
            mod_[s] = pendingMod_[s];
        }
    }
    hasPendingBase_.fill(false);
    hasPendingMod_.fill(false);
    if (hasPendingNotes_)
    {
        hasPendingNotes_ = false;
        updateKeys();
    }
    hasPending_ = false;
}

void Processor::applyNote(const NoteEvent &n) noexcept
{
    switch (n.type)
    {
    case NoteEventType::On:
        held_[static_cast<size_t>(n.key)] |= static_cast<uint16_t>(1u << n.channel);
        break;
    case NoteEventType::Off:
    {
        // The channel bits to keep: none for any channel, all but its own otherwise.
        const auto keep = static_cast<uint16_t>(n.channel < 0 ? 0u : ~(1u << n.channel));
        if (n.key < 0)
        {
            for (uint16_t &channels : held_)
            {
                channels &= keep;
            }
        }
        else
        {
            held_[static_cast<size_t>(n.key)] &= keep;
        }
        break;
    }
    case NoteEventType::AllOff:
    {
        // Ruling R14: MIDI's CC 123 releases its own channel's notes; −1 releases every channel.
        const auto keep = static_cast<uint16_t>(n.channel < 0 ? 0u : ~(1u << n.channel));
        for (uint16_t &channels : held_)
        {
            channels &= keep;
        }
        break;
    }
    }
}

void Processor::updateKeys() noexcept
{
    keyCount_ = 0;
    for (size_t k = 0; k < held_.size(); ++k)
    {
        if (held_[k] != 0)
        {
            keys_[keyCount_++] = static_cast<uint8_t>(k);
        }
    }
    engine_.setHeldKeys(heldKeys());
}

void Processor::updateEffective() noexcept
{
    // Sub-project 5's spec §3.1: the macros' own effective values first, clamp(base + mod) as
    // any slot's, then every slot's clamp(base + mod + offset), one clamp of the whole sum. A
    // macro's own offset is 0, so its effective value is the one its curves read. At rest every
    // offset is +0.0f and the sum is base + mod, bit for bit, but for a −0 sum (a −0 base and a
    // −0 modulation, which only a host can send), which becomes +0.
    // base is finite and in range, mod is never NaN, and every offset is finite (the macros are
    // clamped to 0..1), so the sum is never NaN; an infinite mod clamps to an end of the range.
    ParamValues macros{};
    for (const ParamSlot slot : kMacroSlots)
    {
        const auto s = static_cast<size_t>(slot);
        macros[s] = clampToRange(s, base_[s] + mod_[s]);
    }
    const ParamValues offsets = macroOffsets(macros, config_.mode);
    for (size_t s = 0; s < kParamCount; ++s)
    {
        effective_[s] = clampToRange(s, base_[s] + mod_[s] + offsets[s]);
    }
    engine_.setCloudParams(toCloudParams(effective_, config_.mode));
    space_ = toSpaceParams(effective_, config_.mode);
}

void Processor::syncFreeze() noexcept
{
    const bool on = toWetParams(effective_, config_.mode).freeze;
    if (!on)
    {
        freezeArmed_ = true; // off seen: the next on engages
    }
    // Spec §6.3: in the FX a held key freezes too, with no arming, and never arms the parameter.
    const bool noteHeld = config_.mode == Mode::Fx && keyCount_ > 0;
    engineFrozen_ = (on && freezeArmed_) || noteHeld;
    engine_.setFreeze(engineFrozen_);
}

void Processor::reset() noexcept
{
    engine_.reset();
    commitPending();
    // Every note released (spec §6.1), and the engine told: Engine::reset keeps its keys.
    held_.fill(0);
    updateKeys();
    updateEffective();
    // A fresh ring: Freeze waits for an off→on transition, as after construction.
    freezeArmed_ = false;
    engineFrozen_ = false;
    engine_.setFreeze(false);
    jumpRamps();
    // The engine's clock is back at 0, and so is the reverb's time: its chunks stay on the grid.
    // A fresh ring has no wet signal behind it to feed back.
    for (auto &channel : wetHistory_)
    {
        channel.fill(0.0f);
    }
    diffuser_.reset();
    reverb_->reset();
    // The ring restarts at time 0, and its peak strip with it.
    livePeaks_.reset();
}

void Processor::setSourceNow(std::shared_ptr<const PreparedSource> s)
{
    // process() isn't running, so this thread may take the audio side's role in sources_. An
    // offer not yet taken and a source taken but not adopted are both superseded.
    takeOffers();
    if (hasPendingSource_)
    {
        sources_.retire(pendingSource_);
        hasPendingSource_ = false;
    }
    // A retired source waiting would leave no room for the ones the replacement hands back.
    handBackRetired();
    engine_.replaceSource(sources_.hold(std::move(s)));
    handBackRetired();
    sources_.collect();
}

void Processor::handBackRetired() noexcept
{
    while (const PreparedSource *retired = engine_.takeRetiredSource())
    {
        sources_.retire(retired);
    }
}

bool Processor::takeOffers() noexcept
{
    // A retired source first: one waiting to be taken would keep the engine from adopting.
    handBackRetired();
    SourceOffer offer;
    const bool offered = sources_.poll(offer);
    if (offered)
    {
        if (offer.hasValues)
        {
            for (size_t s = 0; s < kParamCount; ++s)
            {
                setBase(static_cast<ParamSlot>(s), offer.values[s]);
            }
        }
        // A keep-source offer (ruling R19) leaves the source alone: the engine's, and one taken
        // but not yet adopted.
        if (!offer.keepSource)
        {
            if (hasPendingSource_)
            {
                sources_.retire(pendingSource_); // superseded before the engine ever read it
            }
            pendingSource_ = offer.source;
            hasPendingSource_ = true;
        }
    }
    if (hasPendingSource_ && engine_.adoptSource(pendingSource_))
    {
        hasPendingSource_ = false;
    }
    return offered && offer.hasValues;
}

void Processor::setEventSink(GrainEventSink sink, void *ctx) noexcept
{
    externalSink_ = sink;
    externalSinkCtx_ = ctx;
}

void Processor::onGrain(void *ctx, const GrainSpec &g) noexcept
{
    auto *self = static_cast<Processor *>(ctx);
    if (self->externalSink_ != nullptr)
    {
        self->externalSink_(self->externalSinkCtx_, g);
    }
    if (self->publishingThisCall_)
    {
        // A full queue drops the grain and counts it: the editor resumes with the next ones.
        self->grainQueue_->push({g.onsetSample, g.startPos, static_cast<float>(g.rate),
                                 static_cast<float>(g.durationSamples), g.gain, g.pan});
    }
}

void Processor::process(const float *const *in, uint32_t numInChannels, float *const *out,
                        uint32_t numOutChannels, uint32_t numFrames,
                        std::span<const ParamEvent> events,
                        std::span<const NoteEvent> notes) noexcept
{
    // One guard for the whole call, the dry/wet sum included (spec §6.4).
    ScopedDenormalsOff denormalsOff;
    // Read once: the whole call publishes, or none of it does (sub-project 5's spec §6.4).
    publishingThisCall_ = publishing_.load(std::memory_order_relaxed);
    takeOffers();
    if (in == nullptr || config_.mode != Mode::Fx)
    {
        numInChannels = 0;
    }
    if (out == nullptr)
    {
        numOutChannels = 0;
    }
    const int64_t start = engine_.sampleClock();
    const int64_t end = start + static_cast<int64_t>(numFrames);
    const Buffers buffers{in, numInChannels, out, numOutChannels, start};

    // The Freeze in effect from the call's first sample: as at the last boundary, or as
    // setBase() or reset() left it.
    syncFreeze();

    // Renders up to the pending boundary and applies the pending changes there.
    int64_t rendered = start;
    const auto applyPending = [&] {
        assert(pendingAt_ < end);
        render(buffers, rendered, pendingAt_);
        rendered = pendingAt_;
        commitPending();
        updateEffective();
        syncFreeze();
        const WetParams w = toWetParams(effective_, config_.mode);
        mix_.retarget(w.mix, rendered, rampSamples_);
        level_.retarget(w.levelGain, rendered, rampSamples_);
        diffusion_.retarget(space_.diffusion, rendered, rampSamples_);
        amount_.retarget(space_.amount, rendered, rampSamples_);
        drySend_.retarget(space_.drySend, rendered, rampSamples_);
        feedback_.retarget(w.feedback, rendered, rampSamples_);
    };

    // Puts an event at `offset` into its boundary's group, applying the pending group first if
    // the boundary is a later one.
    const auto join = [&](uint32_t offset) {
        const int64_t a = start + std::min<int64_t>(offset, numFrames);
        // max: an out-of-order event can't land before what is already rendered. rendered is
        // either the call's start, which a is at or after, or a boundary.
        const int64_t b = std::max(gridCeil(a), rendered);
        // Sorted events have non-decreasing boundaries, so a later one ends the pending group.
        // Every boundary but the last is at most end − 32, before this call's end.
        if (hasPending_ && b > pendingAt_)
        {
            applyPending();
        }
        if (!hasPending_)
        {
            pendingAt_ = b;
            hasPending_ = true;
        }
    };

    // The two spans merged by offset into one sequence, a parameter event first at a tie. The
    // order between the kinds doesn't matter: a group's parameter changes and notes are
    // independent, and each kind keeps its own order.
    size_t nextEvent = 0;
    size_t nextNote = 0;
    while (nextEvent < events.size() || nextNote < notes.size())
    {
        if (nextNote == notes.size() ||
            (nextEvent < events.size() && events[nextEvent].offset <= notes[nextNote].offset))
        {
            const ParamEvent &e = events[nextEvent++];
            const auto s = static_cast<size_t>(e.slot);
            const auto value = static_cast<float>(e.value);
            if (s >= kParamCount || std::isnan(value))
            {
                continue;
            }
            join(e.offset);
            if (e.isModulation)
            {
                pendingMod_[s] = value;
                hasPendingMod_[s] = true;
            }
            else
            {
                pendingBase_[s] = clampToRange(s, value);
                hasPendingBase_[s] = true;
            }
        }
        else
        {
            const NoteEvent &n = notes[nextNote++];
            if (!validNote(n))
            {
                continue;
            }
            join(n.offset);
            applyNote(n);
            hasPendingNotes_ = true;
        }
    }
    // A boundary at end carries over, like one past it: the next call's events at offset 0
    // belong to the same group, which a single call spanning both would apply once.
    if (hasPending_ && pendingAt_ < end)
    {
        applyPending();
    }
    render(buffers, rendered, end);
    // A source that retired in this call goes back now, not at the next call's start: the host
    // may not call again for a while (it stopped processing), and the main thread can only free
    // what was handed back.
    handBackRetired();

    for (uint32_t c = 2; c < numOutChannels; ++c)
    {
        if (out[c] != nullptr)
        {
            std::fill_n(out[c], numFrames, 0.0f);
        }
    }

    if (publishingThisCall_)
    {
        const int64_t clock = engine_.sampleClock();
        snapshot_.push({clock, engine_.liveWriteHead(), engineFrozen_, engine_.activeGrains(),
                        base_[static_cast<size_t>(ParamSlot::Position)],
                        base_[static_cast<size_t>(ParamSlot::Spray)],
                        effective_[static_cast<size_t>(ParamSlot::Position)],
                        effective_[static_cast<size_t>(ParamSlot::Spray)], feedbackAt(clock)});
        // Release: an editor that reads this clock with acquire finds the snapshot it ends.
        publishedClock_.store(clock, std::memory_order_release);
    }
}

void Processor::render(const Buffers &b, int64_t from, int64_t to) noexcept
{
    const bool fx = config_.mode == Mode::Fx;
    // Channels 0 and 1 carry the signal; one output channel gets the engine's mono render. With
    // no output channel the engine renders stereo all the same, so the diffuser and the reverb
    // hear what they would have, and the next call continues the same render.
    const uint32_t channels = std::min(b.numOutChannels, 2u);
    const uint32_t rendering = channels == 0 ? 2u : channels;
    float *const wet[2] = {wet_[0].data(), wet_[1].data()};
    // The FX's pieces are at most kFeedbackDelay samples (sub-project 5's spec §5), so a piece's
    // ring input needs only wet samples rendered before it.
    const auto pieceMax = static_cast<int64_t>(fx ? std::min(config_.maxBlockSize, kFeedbackDelay)
                                                  : config_.maxBlockSize);
    while (from < to)
    {
        const auto n = static_cast<uint32_t>(std::min<int64_t>(to - from, pieceMax));
        const auto offset = static_cast<size_t>(from - b.callStart);

        // This piece's input: a mono input feeds both channels; a missing one is silence.
        const float *dry[2] = {nullptr, nullptr};
        if (b.numInChannels > 0)
        {
            dry[0] = b.in[0];
            dry[1] = b.numInChannels == 1 ? b.in[0] : b.in[1];
        }
        for (const float *&d : dry)
        {
            d = d != nullptr ? d + offset : nullptr;
        }
        // Feedback (spec §5): ring[t] = sanitize(in[t]) + fb(t) × limited[t − D], with limited
        // from wetHistory_. The ramp changes only at boundaries, between render() calls, so it
        // is 0 throughout a piece whose target and first value are 0: such a piece hands the
        // engine its input as it is (the sign of zero included), as before Feedback.
        const float *ringIn[2] = {dry[0], dry[1]};
        uint32_t ringChannels = b.numInChannels > 0 ? 2 : 0;
        if (fx && (feedback_.to != 0.0f || feedback_.at(from, rampSamples_) != 0.0f))
        {
            for (uint32_t i = 0; i < n; ++i)
            {
                const int64_t t = from + i;
                const float fb = feedback_.at(t, rampSamples_);
                const auto h = static_cast<size_t>(t % kFeedbackDelay);
                for (size_t c = 0; c < 2; ++c)
                {
                    const float d = dry[c] != nullptr ? finiteOrZero(dry[c][i]) : 0.0f;
                    // A sample where the ramp is 0 writes the input alone, as a piece without
                    // feedback does, so where a piece starts never changes the ring.
                    ringIn_[c][i] = fb != 0.0f ? d + fb * wetHistory_[c][h] : d;
                }
            }
            ringIn[0] = ringIn_[0].data();
            ringIn[1] = ringIn_[1].data();
            ringChannels = 2;
        }
        const int64_t headBefore = engine_.liveWriteHead();
        engine_.process(ringIn, ringChannels, wet, rendering, n);
        if (fx)
        {
            // The peaks of what the ring took (spec §6.4), publishing or not (ruling PU-T9b), so
            // the strip never holds an older pass's peaks when the editor opens: the buffers the
            // engine was handed, over the frames its head advanced, a prefix of the piece (all
            // of it unless a freeze stopped the head), at ring time.
            const auto written = static_cast<uint32_t>(engine_.liveWriteHead() - headBefore);
            const float *l = ringChannels > 0 && ringIn[0] != nullptr ? ringIn[0] : kSilence.data();
            const float *r = ringChannels > 0 && ringIn[1] != nullptr ? ringIn[1] : kSilence.data();
            livePeaks_.write(l, r, headBefore, written);
        }

        float *dst[2] = {nullptr, nullptr};
        for (uint32_t c = 0; c < channels; ++c)
        {
            dst[c] = b.out[c] != nullptr ? b.out[c] + offset : nullptr;
        }
        for (uint32_t i = 0; i < n; ++i)
        {
            const int64_t t = from + i;
            const float m = mix_.at(t, rampSamples_);
            const float g = level_.at(t, rampSamples_);
            // The ramps stay between in-range values; the clamp keeps the diffuser's crossfade
            // from extrapolating whatever happens.
            const float delta = std::clamp(diffusion_.at(t, rampSamples_), 0.0f, 1.0f);
            const float a = amount_.at(t, rampSamples_);
            const float s = drySend_.at(t, rampSamples_);
            const bool active = a != 0.0f; // read by the reverb at a chunk's first sample
            // Read both inputs before writing either output: out may be in, and a mono input
            // feeds both channels.
            float d[2] = {0.0f, 0.0f};
            for (uint32_t c = 0; c < rendering; ++c)
            {
                d[c] = dry[c] != nullptr ? finiteOrZero(dry[c][i]) : 0.0f;
            }
            // One channel: its mono render is diffused as both sides, and its rIn feeds both.
            float w[2] = {wet[0][i], rendering > 1 ? wet[1][i] : wet[0][i]};
            diffuser_.process(w[0], w[1], delta);
            float o[2];
            float rIn[2];
            for (uint32_t c = 0; c < 2; ++c)
            {
                const float limited = softLimit(w[c] * g);
                o[c] = fx ? d[c] * (1.0f - m) + limited * m : limited;
                rIn[c] = fx ? m * limited + s * d[c] : limited;
                if (fx)
                {
                    // Read for sample t + D's ring input, D samples from now.
                    wetHistory_[c][static_cast<size_t>(t % kFeedbackDelay)] = limited;
                }
            }
            if (rendering == 1)
            {
                rIn[1] = rIn[0];
            }
            // Reverb 2 sums (L + R)·0.5, which overflows to inf (and its loop to NaN, held until
            // the next clear) past about 8.5e37: a huge but finite dry input sent at 100% would.
            // The wet side is bounded by softLimit; ±1e6 is far past anything audible.
            for (float &v : rIn)
            {
                v = std::clamp(v, -1.0e6f, 1.0e6f);
            }
            float r[2];
            reverb_->processSample(t, rIn[0], rIn[1], space_, active, r[0], r[1]);
            if (reverb_->chunkActive())
            {
                // Only here: an idle chunk adds nothing, not even a +0 that would turn a −0 to +0.
                o[0] += softLimit(a * r[0]);
                o[1] += softLimit(a * r[1]);
            }
            for (uint32_t c = 0; c < channels; ++c)
            {
                if (dst[c] != nullptr)
                {
                    dst[c][i] = o[c];
                }
            }
        }
        from += n;
    }
}
} // namespace gg::core
