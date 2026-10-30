// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/Processor.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>

#include "gg/core/DenormalGuard.h"
#include "gg/core/MathDet.h"
#include "gg/core/ParamMap.h"
#include "fx/BusFilter.h"
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
/// The configuration the engines would reject, rejected before anything is allocated.
const ProcessorConfig &validated(const ProcessorConfig &c)
{
    if (!std::isfinite(c.sampleRate) || c.sampleRate <= 0.0)
    {
        throw std::invalid_argument("Processor: sampleRate must be finite and > 0");
    }
    if (c.maxBlockSize == 0)
    {
        throw std::invalid_argument("Processor: maxBlockSize must be > 0");
    }
    return c;
}

/// Layer L's engine: a kLiveSeconds ring in both products (M2 sub-project 1's spec §6.2).
EngineConfig engineConfig(const ProcessorConfig &c, uint8_t layer)
{
    EngineConfig e;
    e.sampleRate = c.sampleRate;
    e.maxBlockSize = c.maxBlockSize;
    e.seed = c.seed;
    e.liveSeconds = Processor::kLiveSeconds;
    e.layer = layer;
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

/// A missing ring-input channel, for the live peaks: the pieces are at most
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

/// Whether a control event's fields are in range (ControlEvent): a known kind, a channel 0..15 or
/// −1, a finite value.
bool validControl(const ControlEvent &c) noexcept
{
    return c.kind <= ControlKind::PitchBend && c.channel >= -1 && c.channel <= 15 &&
           std::isfinite(c.value);
}

/// v clamped to [0, 1], NaN as 0.
float unitClamped(float v) noexcept { return v >= 0.0f ? (v <= 1.0f ? v : 1.0f) : 0.0f; }

/// Whether some route moves its target with time alone (the class comment's Time-varying
/// routes): a time-varying source, a non-zero depth and a target it reaches.
bool anyTimeVarying(const ModRoutes &routes) noexcept
{
    for (const ModRoute &r : routes)
    {
        const bool reaches =
            !isLayerTarget(r.target) || r.targetLayer == kAllLayers || r.targetLayer < kLayers;
        if (isTimeVarying(r.source) && r.depth != 0.0f && isValidTarget(r.target) && reaches)
        {
            return true;
        }
    }
    return false;
}

// sameCloud compares every field of CloudParams: a field added there must be added there too.
static_assert(sizeof(CloudParams) == 120, "CloudParams changed: update sameCloud");

/// Whether two CloudParams are the same, bit for bit (+0 and −0 differ, so the engine is never
/// spared a value that would change a bit).
bool sameCloud(const CloudParams &a, const CloudParams &b) noexcept
{
    const auto same = [](double x, double y) {
        return std::bit_cast<uint64_t>(x) == std::bit_cast<uint64_t>(y);
    };
    return same(a.densityHz, b.densityHz) && same(a.distribution, b.distribution) &&
           same(a.grainDurationSec, b.grainDurationSec) && same(a.position, b.position) &&
           same(a.spray, b.spray) && same(a.pitchSemitones, b.pitchSemitones) &&
           same(a.stereo, b.stereo) &&
           std::bit_cast<uint32_t>(a.gain) == std::bit_cast<uint32_t>(b.gain) &&
           same(a.texture, b.texture) && a.quality == b.quality &&
           a.sprayInSeconds == b.sprayInSeconds &&
           same(a.pitchSpreadSemitones, b.pitchSpreadSemitones) &&
           same(a.sizeSpreadOctaves, b.sizeSpreadOctaves) &&
           same(a.smearJitterSec, b.smearJitterSec) &&
           same(a.smearDetuneSemitones, b.smearDetuneSemitones) && same(a.busSplit, b.busSplit);
}

/// The delay damper's one-pole coefficient for Delay Damping (spec §6.4): the cutoff runs from
/// 20 kHz at 0 to 500 Hz at 1, exponentially, f_c = exp2Det(log2(20000) + damping × (log2(500) −
/// log2(20000))), and coeff = 1 − exp2Det(−f_c × 2π / ln 2 / sampleRate). No libm.
float dampCoefficient(float damping, double sampleRate) noexcept
{
    constexpr double kLog2Of20k = 14.287712379549449;
    constexpr double kLog2Of500 = 8.965784284662087;
    constexpr double kTwoPiOverLn2 = 9.064720283654388;
    const double cutoff =
        exp2Det(kLog2Of20k + static_cast<double>(damping) * (kLog2Of500 - kLog2Of20k));
    return static_cast<float>(1.0 - exp2Det(-cutoff * kTwoPiOverLn2 / sampleRate));
}

/// Peak's per-sample release coefficient for a release time constant in seconds (spec §6.5):
/// 1 − exp2Det(−(1/ln 2) / (release × sampleRate)). No libm.
float releaseCoefficient(float releaseSeconds, double sampleRate) noexcept
{
    constexpr double kInvLn2 = 1.4426950408889634;
    return static_cast<float>(
        1.0 - exp2Det(-kInvLn2 / (static_cast<double>(releaseSeconds) * sampleRate)));
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

Processor::Layer::Layer(const EngineConfig &config) : engine(config) {}

Processor::Layer::~Layer() = default;

Processor::Processor(const ProcessorConfig &config)
    : config_(validated(config)), rampSamples_(rampLength(config.sampleRate)),
      pieceMax_(std::min(config.maxBlockSize, kFeedbackDelay)), routes_(factoryRoutes(config.mode)),
      timeVarying_(anyTimeVarying(routes_)), follower_(config.sampleRate),
      cellSeconds_(static_cast<double>(kControlGrid) / config.sampleRate),
      reverb_(std::make_unique<SpaceReverb>(config.sampleRate))
{
    // The delay's line allocates, so it is sized here, off the audio thread.
    delay_.prepare(config_.sampleRate);
    limiter_.prepare(config_.sampleRate, static_cast<uint32_t>(rampSamples_));
    // The first paramMeta() call builds its tables, which allocates; make every call here, off
    // the audio thread, so that process() never makes it, and keep the ranges.
    for (size_t s = 0; s < kParamCount; ++s)
    {
        const auto &meta = paramMeta(static_cast<ParamSlot>(s), config_.mode);
        minVal_[s] = meta.minVal;
        maxVal_[s] = meta.maxVal;
        base_[s] = clampToRange(s, meta.defaultVal);
    }
    // Publishing (sub-project 5's spec §6.4): the engines' sink is ours for good, and forwards to
    // setEventSink's after each piece. Each peak strip spans its ring, N = ceil(kLiveSeconds ×
    // sampleRate) frames.
    grainQueue_ = std::make_unique<GrainQueue>();
    for (uint8_t L = 0; L < kLayers; ++L)
    {
        const EngineConfig engine = engineConfig(config_, L);
        layers_[L] = std::make_unique<Layer>(engine);
        Layer &l = *layers_[L];
        l.diffuser.prepare(config_.sampleRate);
        for (auto &filter : l.filter)
        {
            filter = std::make_unique<fx::BusFilter>();
            filter->prepare(config_.sampleRate, static_cast<uint32_t>(rampSamples_));
        }
        for (auto &channel : l.wet)
        {
            channel.assign(pieceMax_, 0.0f);
        }
        for (auto &channel : l.bus2)
        {
            channel.assign(pieceMax_, 0.0f);
        }
        for (auto &channel : l.ringIn)
        {
            channel.assign(pieceMax_, 0.0f);
        }
        l.peaks.prepare(l.engine.liveFrames());
        // The pool's storage bound on the spawns of one block (spec §6.7).
        l.events.reserve(2 * static_cast<size_t>(engine.grainCapacity));
        if (config_.mode == Mode::Instrument)
        {
            l.engine.setNoteGate(true); // silent until a note (spec §6.2)
        }
        l.engine.setEventSink(&Processor::onGrain, this);
    }
    // The kinds (the FX's layers read their rings) and the cloud parameters.
    updateEffective();
    // A layer off by default (B to D) starts with its engine disabled, so it spawns nothing
    // before it is switched on; layer A's is never told, as M1's engine wasn't.
    for (uint8_t L = 0; L < kLayers; ++L)
    {
        if (!layerOn(effective_, L))
        {
            layers_[L]->engine.setEnabled(false);
            layers_[L]->enabledGiven = false;
        }
    }
    jumpRamps();
}

void Processor::setSeed(uint64_t seed) noexcept
{
    config_.seed = seed;
    for (const auto &l : layers_)
    {
        l->engine.setSeed(seed);
    }
}

void Processor::jumpRamps() noexcept
{
    mix_.jump(toWetParams(effective_, config_.mode, 0).mix);
    amount_.jump(space_.amount);
    drySend_.jump(space_.drySend);
    delayAmount_.jump(delayParams_.amount);
    delayFeedback_.jump(delayParams_.feedback);
    delayDamp_.jump(delayDampCoeff_);
    limiterCeiling_.jump(limiterParams_.ceilingGain);
    limiterRelease_.jump(limiterReleaseCoeff_);
    // The limiter's mode snaps too: its own 10 ms fade would let a new Processor's, or a loaded
    // Peak state's, first 10 ms through unlimited.
    limiter_.jump(limiterParams_.mode);
    const MorphParams morph = toMorphParams(effective_);
    morphX_.jump(morph.x);
    morphY_.jump(morph.y);
    for (uint8_t L = 0; L < kLayers; ++L)
    {
        Layer &l = *layers_[L];
        const WetParams w = toWetParams(effective_, config_.mode, L);
        l.level.jump(w.levelGain);
        l.diffusion.jump(layerDiffusion(effective_, L));
        l.feedback.jump(w.feedback);
        l.on.jump(layerOn(effective_, L) ? 1.0f : 0.0f);
    }
}

void Processor::retargetRamps(int64_t t) noexcept
{
    mix_.retarget(toWetParams(effective_, config_.mode, 0).mix, t, rampSamples_);
    amount_.retarget(space_.amount, t, rampSamples_);
    drySend_.retarget(space_.drySend, t, rampSamples_);
    delayAmount_.retarget(delayParams_.amount, t, rampSamples_);
    delayFeedback_.retarget(delayParams_.feedback, t, rampSamples_);
    delayDamp_.retarget(delayDampCoeff_, t, rampSamples_);
    limiterCeiling_.retarget(limiterParams_.ceilingGain, t, rampSamples_);
    limiterRelease_.retarget(limiterReleaseCoeff_, t, rampSamples_);
    const MorphParams morph = toMorphParams(effective_);
    morphX_.retarget(morph.x, t, rampSamples_);
    morphY_.retarget(morph.y, t, rampSamples_);
    for (uint8_t L = 0; L < kLayers; ++L)
    {
        Layer &l = *layers_[L];
        const WetParams w = toWetParams(effective_, config_.mode, L);
        l.level.retarget(w.levelGain, t, rampSamples_);
        l.diffusion.retarget(layerDiffusion(effective_, L), t, rampSamples_);
        l.feedback.retarget(w.feedback, t, rampSamples_);
        l.on.retarget(layerOn(effective_, L) ? 1.0f : 0.0f, t, rampSamples_);
    }
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
    const std::optional<LayerRef> ref = layerOf(slot);
    if (slot == ParamSlot::Macro6 || (ref && ref->param == LayerParam::Freeze))
    {
        syncFreeze(); // Freeze set off is a transition's first half, even between calls
    }
    if (isMacro(slot))
    {
        // A macro moves its routes' targets' effective values (sub-project 5's spec §3.1; M2
        // sub-project 1's spec §6.8): each ramped value whose target it changed jumps, as a
        // setBase of the target itself makes it; the others keep their ramps.
        jumpMovedRamps();
        return;
    }
    const auto jumpIfMoved = [](Ramp &ramp, float target) {
        if (target != ramp.to)
        {
            ramp.jump(target);
        }
    };
    const MorphParams morph = toMorphParams(effective_);
    switch (slot)
    {
    case ParamSlot::Mix:
        mix_.jump(toWetParams(effective_, config_.mode, 0).mix);
        break;
    case ParamSlot::ReverbAmount:
        amount_.jump(space_.amount);
        break;
    case ParamSlot::DryToReverb:
        drySend_.jump(space_.drySend);
        break;
    case ParamSlot::DelayAmount:
        delayAmount_.jump(delayParams_.amount);
        break;
    case ParamSlot::DelayFeedback:
        delayFeedback_.jump(delayParams_.feedback);
        break;
    case ParamSlot::DelayDamping:
        delayDamp_.jump(delayDampCoeff_);
        break;
    case ParamSlot::LimiterMode:
        // As a ramped value's setBase jumps it: only process()'s events and the matrix fade.
        limiter_.jump(limiterParams_.mode);
        break;
    case ParamSlot::LimiterCeiling:
        limiterCeiling_.jump(limiterParams_.ceilingGain);
        break;
    case ParamSlot::LimiterRelease:
        limiterRelease_.jump(limiterReleaseCoeff_);
        break;
    case ParamSlot::MorphX:
        morphX_.jump(morph.x);
        break;
    case ParamSlot::MorphY:
        morphY_.jump(morph.y);
        break;
    default:
        break;
    }
    // A layer's Level (Level for layer A, Level B to D) or Layer On switch.
    if (const std::optional<uint8_t> L = perLayerGlobalLayer(slot))
    {
        Layer &l = *layers_[*L];
        if (slot == levelSlot(*L))
        {
            l.level.jump(toWetParams(effective_, config_.mode, *L).levelGain);
        }
        else
        {
            l.on.jump(layerOn(effective_, *L) ? 1.0f : 0.0f);
        }
    }
    if (ref)
    {
        Layer &l = *layers_[ref->layer];
        const WetParams w = toWetParams(effective_, config_.mode, ref->layer);
        switch (ref->param)
        {
        case LayerParam::Diffusion:
            l.diffusion.jump(layerDiffusion(effective_, ref->layer));
            break;
        case LayerParam::Feedback:
            l.feedback.jump(w.feedback);
            break;
        case LayerParam::Source:
            // Feedback follows the kind (spec §6.4): a kind that moved its target makes it jump,
            // as a macro's move does.
            jumpIfMoved(l.feedback, w.feedback);
            break;
        default:
            break;
        }
    }
}

void Processor::setBases(const ParamValues &values) noexcept
{
    // Ruling FR-3: setBase() on every slot in slot order, with the effective values recomputed
    // once. A NaN leaves its slot, and setBase()'s side effects at it, alone; no saved state holds
    // one, so that case takes the calls themselves, but for Freeze's arming (below).
    if (std::any_of(values.begin(), values.end(), [](float v) { return std::isnan(v); }))
    {
        std::array<bool, kLayers> armed{};
        for (uint8_t L = 0; L < kLayers; ++L)
        {
            armed[L] = layers_[L]->freezeArmed;
        }
        for (size_t s = 0; s < kParamCount; ++s)
        {
            setBase(static_cast<ParamSlot>(s), values[s]);
        }
        for (uint8_t L = 0; L < kLayers; ++L)
        {
            layers_[L]->freezeArmed = armed[L];
        }
        syncFreeze();
        return;
    }
    for (size_t s = 0; s < kParamCount; ++s)
    {
        base_[s] = clampToRange(s, values[s]);
    }
    hasPendingBase_.fill(false);
    // The engines take their parameters and kind as values, the last given winning, so one
    // update gives what the calls' last one gave. (A period the calls move and move back flags
    // the scheduler's rescale where one update doesn't, and that rescale recomputes the pending
    // onset it already has.)
    updateEffective();
    // Ruling FR-8 (INTENT §23 #40): Freeze arms from the loaded values alone, every layer alike.
    // A layer whose loaded Freeze is off arms; one whose Freeze is on stays live until toggled if
    // not yet armed (a fresh ring, or one since the last reset()), and freezes at once if armed.
    // The calls would arm a layer at an earlier slot's syncFreeze(), from its old value (layers B
    // to D at layer A's Freeze), and freeze an empty ring at a load.
    syncFreeze();
    // Every ramped value's own slot is among the calls, and setBase() of it jumps it to its
    // target; after that only a macro and its layer's Source (for Feedback) move that target, and
    // each jumps it if moved. So the calls leave every ramped value jumped to its final target.
    jumpRamps();
}

void Processor::jumpMovedRamps() noexcept
{
    // Generic on purpose: every ramped value is checked, whatever routes move it.
    const auto jumpIfMoved = [](Ramp &ramp, float target) {
        if (target != ramp.to)
        {
            ramp.jump(target);
        }
    };
    const MorphParams morph = toMorphParams(effective_);
    jumpIfMoved(mix_, toWetParams(effective_, config_.mode, 0).mix);
    jumpIfMoved(amount_, space_.amount);
    jumpIfMoved(drySend_, space_.drySend);
    jumpIfMoved(delayAmount_, delayParams_.amount);
    jumpIfMoved(delayFeedback_, delayParams_.feedback);
    jumpIfMoved(delayDamp_, delayDampCoeff_);
    jumpIfMoved(limiterCeiling_, limiterParams_.ceilingGain);
    jumpIfMoved(limiterRelease_, limiterReleaseCoeff_);
    jumpIfMoved(morphX_, morph.x);
    jumpIfMoved(morphY_, morph.y);
    for (uint8_t L = 0; L < kLayers; ++L)
    {
        Layer &l = *layers_[L];
        const WetParams w = toWetParams(effective_, config_.mode, L);
        jumpIfMoved(l.level, w.levelGain);
        jumpIfMoved(l.diffusion, layerDiffusion(effective_, L));
        jumpIfMoved(l.feedback, w.feedback);
        jumpIfMoved(l.on, layerOn(effective_, L) ? 1.0f : 0.0f);
    }
}

void Processor::setRoutes(const ModRoutes &routes) noexcept
{
    routes_ = routes;
    timeVarying_ = anyTimeVarying(routes_);
    updateEffective();
    jumpMovedRamps(); // as a loaded state's values do
}

void Processor::setTransport(const Transport &transport) noexcept
{
    if ((transport_.playing() != 0.0f) != transport.playing)
    {
        transportChanged_ = true; // Transport Playing changes at the next call's first boundary
    }
    transport_.set(transport);
}

float Processor::sourceValue(ModSource s, uint8_t layer) const noexcept
{
    const auto i = static_cast<size_t>(s);
    if (s >= ModSource::Macro1 && s <= ModSource::Macro8)
    {
        return effective_[static_cast<size_t>(
            kMacroSlots[i - static_cast<size_t>(ModSource::Macro1)])];
    }
    if (isPerLayerSource(s))
    {
        return layerAt(layer).modulatorValues[i - static_cast<size_t>(ModSource::Lfo1)];
    }
    switch (s)
    {
    case ModSource::ModWheel:
        return midi_.modWheel;
    case ModSource::Aftertouch:
        return midi_.aftertouch;
    case ModSource::PitchBend:
        return midi_.pitchBend;
    case ModSource::Velocity:
        return midi_.velocity;
    case ModSource::Key:
        return midi_.key;
    case ModSource::EnvFollower:
        return followerValue_;
    case ModSource::TransportBeat:
        return transportBeat_;
    case ModSource::TransportBar:
        return transportBar_;
    case ModSource::TransportPlaying:
        return transportPlaying_;
    default:
        return 0.0f; // None, Count and beyond
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
        // A modulator's control is in effect for the cell its boundary starts, as the sources
        // step there before the effective values are recomputed: no route targets it, so its
        // effective value is already known.
        if ((hasPendingBase_[s] || hasPendingMod_[s]) &&
            isModulatorControlSlot(static_cast<ParamSlot>(s)))
        {
            effective_[s] = clampToRange(s, base_[s] + mod_[s]);
        }
    }
    hasPendingBase_.fill(false);
    hasPendingMod_.fill(false);
    if (hasPendingNotes_)
    {
        hasPendingNotes_ = false;
        updateKeys();
    }
    midi_ = pendingMidi_;
    hasPending_ = false;
}

void Processor::scanInput(const Buffers &b, int64_t to) noexcept
{
    // A mono input feeds both channels: its one channel is the whole input.
    const uint32_t channels = std::min(b.numInChannels, 2u);
    float peak = cellPeak_;
    for (int64_t t = std::max(inputScanned_, b.callStart); t < to; ++t)
    {
        const auto i = static_cast<size_t>(t - b.callStart);
        for (uint32_t c = 0; c < channels; ++c)
        {
            if (b.in[c] == nullptr)
            {
                continue;
            }
            const float a = std::fabs(b.in[c][i]);
            // NaN fails both comparisons and infinity the second: each counts as 0.
            if (a > peak && a <= std::numeric_limits<float>::max())
            {
                peak = a;
            }
        }
    }
    cellPeak_ = peak;
    inputScanned_ = std::max(inputScanned_, to);
}

void Processor::stepSourcesAt(int64_t c) noexcept
{
    // The held set as of c: c's notes are committed before c steps.
    const bool gate = keyCount_ > 0;
    for (uint8_t L = 0; L < kLayers; ++L)
    {
        Layer &l = *layers_[L];
        LayerModulators &m = l.modulators;
        auto &v = l.modulatorValues; // Lfo1, Lfo2, Env1, Env2, Random1, Random2, Step
        v[0] = m.lfo[0].step(lfoParams(effective_, L, 0), cellSeconds_);
        v[1] = m.lfo[1].step(lfoParams(effective_, L, 1), cellSeconds_);
        v[2] = m.env[0].step(envParams(effective_, L, 0), gate, cellSeconds_);
        v[3] = m.env[1].step(envParams(effective_, L, 1), gate, cellSeconds_);
        v[4] = m.random[0].step(randomParams(effective_, L, 0), cellSeconds_, config_.seed, L,
                                rng::Field::ModRandom1);
        v[5] = m.random[1].step(randomParams(effective_, L, 1), cellSeconds_, config_.seed, L,
                                rng::Field::ModRandom2);
        v[6] = m.stepSeq.step(stepParams(effective_, L), cellSeconds_);
    }
    // The previous cell's input peak, [c − 32, c).
    const float peak = cellPeak_;
    const float *peakChannel = &peak;
    followerValue_ = follower_.step(&peakChannel, 1, 1);
    cellPeak_ = 0.0f;
    // The transport's values for the cell, then one cell on.
    transportBeat_ = transport_.beatPhase();
    transportBar_ = transport_.barPhase();
    transportPlaying_ = transport_.playing();
    transport_.step(cellSeconds_);
    lastStepped_ = c;
}

void Processor::stepSources(const Buffers &b, int64_t upTo, int64_t scanTo) noexcept
{
    constexpr auto grid = static_cast<int64_t>(kControlGrid);
    for (int64_t c = lastStepped_ + grid; c <= upTo; c += grid)
    {
        scanInput(b, c);
        stepSourcesAt(c);
    }
    scanInput(b, scanTo);
}

void Processor::applyNote(const NoteEvent &n) noexcept
{
    switch (n.type)
    {
    case NoteEventType::On:
        held_[static_cast<size_t>(n.key)] |= static_cast<uint16_t>(1u << n.channel);
        // Spec §6.8: the last note-on's velocity and key / 127, from its boundary.
        pendingMidi_.velocity = unitClamped(n.velocity);
        pendingMidi_.key = static_cast<float>(n.key) / 127.0f;
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
    for (const auto &l : layers_)
    {
        l->engine.setHeldKeys(heldKeys());
    }
}

void Processor::updateEffective() noexcept
{
    // Sub-project 5's spec §3.1, M2 sub-project 1's spec §6.8: the macros' own effective values
    // first, clamp(base + mod) as any slot's (no route targets a macro, so this is their final
    // value), then every source's value, the routes' offsets, and every slot's
    // clamp(base + mod + offset), one clamp of the whole sum. At rest the factory routes' offsets
    // are all +0.0f and the sum is base + mod, bit for bit, but for a −0 sum (a −0 base and a −0
    // modulation, which only a host can send), which becomes +0.
    // base is finite and in range and mod is never NaN; the routes' depths are finite, but two
    // huge ones can sum to ±∞ − ∞: a NaN sum takes no offset. An infinite one clamps to an end of
    // the range.
    for (const ParamSlot slot : kMacroSlots)
    {
        const auto s = static_cast<size_t>(slot);
        effective_[s] = clampToRange(s, base_[s] + mod_[s]);
    }
    for (uint8_t L = 0; L < kLayers; ++L)
    {
        for (size_t i = 0; i < sources_[L].v.size(); ++i)
        {
            sources_[L].v[i] = sourceValue(static_cast<ModSource>(i), L);
        }
    }
    ParamValues offsets{};
    applyRoutes(routes_, sources_, offsets);
    for (size_t s = 0; s < kParamCount; ++s)
    {
        const float sum = base_[s] + mod_[s] + offsets[s];
        effective_[s] = clampToRange(s, std::isnan(sum) ? base_[s] + mod_[s] : sum);
    }
    for (uint8_t L = 0; L < kLayers; ++L)
    {
        Layer &l = *layers_[L];
        // The scheduler takes the same values twice as once, so an unchanged layer is spared the
        // call: the per-cell path of a time-varying route stays cheap.
        const CloudParams cloud = toCloudParams(effective_, config_.mode, L);
        if (!l.cloudGiven || !sameCloud(cloud, l.cloud))
        {
            l.engine.setCloudParams(cloud);
            l.cloud = cloud;
            l.cloudGiven = true;
        }
        // Spec §6.4: a kind switch applies from the engine's next call, the grains sounding
        // finishing on their own source.
        const SourceMode kind = builtKind(sourceKindOf(effective_, L), config_.mode);
        if (kind != l.kind)
        {
            l.engine.setSourceMode(kind);
            l.kind = kind;
        }
    }
    space_ = toSpaceParams(effective_, config_.mode);
    // The delay and the limiter (spec §6.4–§6.6): the read's target moves at each boundary (the
    // delay glides to it), the ramps' targets are taken from these at retargetRamps.
    delayParams_ = delayParams(effective_);
    delayDampCoeff_ = dampCoefficient(delayParams_.damping, config_.sampleRate);
    delay_.setTimeSamples(static_cast<double>(delayParams_.timeSeconds) * config_.sampleRate);
    limiterParams_ = limiterParams(effective_);
    limiterReleaseCoeff_ = releaseCoefficient(limiterParams_.releaseSeconds, config_.sampleRate);
}

void Processor::syncFreeze() noexcept
{
    // Spec §6.3: in the FX a held key freezes every layer, with no arming, and never arms the
    // parameter.
    const bool noteHeld = config_.mode == Mode::Fx && keyCount_ > 0;
    for (uint8_t L = 0; L < kLayers; ++L)
    {
        Layer &l = *layers_[L];
        const bool on = toWetParams(effective_, config_.mode, L).freeze;
        if (!on)
        {
            l.freezeArmed = true; // off seen: the next on engages
        }
        l.engineFrozen = (on && l.freezeArmed) || noteHeld;
        l.engine.setFreeze(l.engineFrozen);
    }
}

void Processor::reset() noexcept
{
    for (const auto &l : layers_)
    {
        l->engine.reset();
    }
    commitPending();
    // The delay's line is cleared and its read forgets the glide before the effective values
    // below set its time, which then applies at once; the limiter is reset after them, by jump.
    delay_.reset();
    // Every note released (spec §6.1), and the engines told: Engine::reset keeps their keys.
    held_.fill(0);
    updateKeys();
    // The sources restart with the clock (the class comment), the transport as never given: the
    // host's next setTransport() is then a change, as a fresh Processor's first is, and applies
    // at the next call's first boundary.
    midi_ = {};
    pendingMidi_ = {};
    for (const auto &l : layers_)
    {
        l->modulators.reset();
        l->modulatorValues.fill(0.0f);
        l->cloudGiven = false; // the engine is told again, as before
    }
    follower_.reset();
    followerValue_ = 0.0f;
    transportBeat_ = 0.0f;
    transportBar_ = 0.0f;
    transportPlaying_ = 0.0f;
    transport_ = {};
    transportChanged_ = false;
    lastStepped_ = -static_cast<int64_t>(kControlGrid);
    inputScanned_ = 0;
    cellPeak_ = 0.0f;
    updateEffective();
    // Fresh rings: Freeze waits for an off→on transition, as after construction.
    for (const auto &l : layers_)
    {
        l->freezeArmed = false;
        l->engineFrozen = false;
        l->engine.setFreeze(false);
    }
    jumpRamps();
    // The engines' clocks are back at 0, and so is the reverb's time: its chunks stay on the
    // grid. A fresh ring has no wet signal behind it to feed back, and its peak strip restarts
    // at time 0 with it.
    for (const auto &l : layers_)
    {
        for (auto &channel : l->wetHistory)
        {
            channel.fill(0.0f);
        }
        l->diffuser.reset();
        l->filter[0]->reset();
        l->filter[1]->reset();
        l->busGiven = false;
        l->peaks.reset();
        l->events.clear();
        l->droppedEvents = 0;
    }
    reverb_->reset();
}

void Processor::setSourceNow(uint8_t layer, std::shared_ptr<const PreparedSource> s)
{
    // process() isn't running, so this thread may take the audio side's role in the layer's
    // sources. An offer not yet taken and a source taken but not adopted are both superseded.
    Layer &l = layerAt(layer);
    takeLayerOffers(l);
    if (l.hasPendingSource)
    {
        l.sources.retire(l.pendingSource);
        l.hasPendingSource = false;
    }
    // A retired source waiting would leave no room for the ones the replacement hands back.
    handBackRetired(l);
    l.engine.replaceSource(l.sources.hold(std::move(s)));
    handBackRetired(l);
    l.sources.collect();
}

void Processor::handBackRetired(Layer &l) noexcept
{
    while (const PreparedSource *retired = l.engine.takeRetiredSource())
    {
        l.sources.retire(retired);
    }
}

bool Processor::takeOffers() noexcept
{
    bool applied = false;
    for (const auto &l : layers_)
    {
        if (takeLayerOffers(*l))
        {
            applied = true;
        }
    }
    return applied;
}

bool Processor::takeLayerOffers(Layer &l) noexcept
{
    // A retired source first: one waiting to be taken would keep the engine from adopting.
    handBackRetired(l);
    SourceOffer offer;
    const bool offered = l.sources.poll(offer);
    if (offered)
    {
        if (offer.hasValues)
        {
            setBases(offer.values);
        }
        // A keep-source offer (ruling R19) leaves the source alone: the engine's, and one taken
        // but not yet adopted.
        if (!offer.keepSource)
        {
            if (l.hasPendingSource)
            {
                l.sources.retire(l.pendingSource); // superseded before the engine ever read it
            }
            l.pendingSource = offer.source;
            l.hasPendingSource = true;
        }
    }
    if (l.hasPendingSource && l.engine.adoptSource(l.pendingSource))
    {
        l.hasPendingSource = false;
    }
    return offered && offer.hasValues;
}

uint64_t Processor::droppedGrainEvents() const noexcept
{
    uint64_t dropped = 0;
    for (const auto &l : layers_)
    {
        dropped += l->droppedEvents;
    }
    return dropped;
}

void Processor::setTaskRunner(TaskRunner *runner) noexcept
{
    runner_ = runner != nullptr ? runner : &serialRunner_;
}

void Processor::layerTask(void *ctx, uint32_t i) noexcept
{
    static_cast<Processor *>(ctx)->execTask(i);
}

void Processor::execTask(uint32_t i) noexcept
{
    // Spec §6.7: a pool thread's floating-point mode isn't the audio thread's, and the engine's own
    // guard covers only Engine::process, not the diffuser, the limiter and Feedback after it.
    ScopedDenormalsOff denormalsOff;
    if (i < taskCount_)
    {
        renderLayerPiece(taskLayers_[i], pieceFrom_, pieceN_, pieceDry_.data(), pieceHasInput_,
                         pieceRendering_);
    }
}

void Processor::setEventSink(GrainEventSink sink, void *ctx) noexcept
{
    externalSink_ = sink;
    externalSinkCtx_ = ctx;
}

void Processor::onGrain(void *ctx, uint32_t layer, const GrainSpec &g) noexcept
{
    auto *self = static_cast<Processor *>(ctx);
    // Each engine's layer is its index (engineConfig), below kLayers.
    // Called on the thread of the layer's task (spec §6.7): it touches only that layer's buffer
    // and count.
    Layer &l = *self->layers_[std::min<uint32_t>(layer, kLayers - 1)];
    // Within the capacity reserved at construction a push_back never allocates.
    if (l.events.size() < l.events.capacity())
    {
        l.events.push_back(g);
    }
    else
    {
        ++l.droppedEvents;
    }
}

void Processor::drainEvents() noexcept
{
    for (uint8_t L = 0; L < kLayers; ++L)
    {
        std::vector<GrainSpec> &events = layers_[L]->events;
        for (const GrainSpec &g : events)
        {
            if (externalSink_ != nullptr)
            {
                externalSink_(externalSinkCtx_, L, g);
            }
            if (publishingThisCall_)
            {
                // A full queue drops the grain and counts it: the editor resumes with the next
                // ones.
                grainQueue_->push({g.onsetSample, g.startPos, static_cast<float>(g.rate),
                                   static_cast<float>(g.durationSamples), g.gain, g.pan, L});
            }
        }
        events.clear(); // keeps the capacity
    }
}

bool Processor::layerRendering(uint8_t layer) const noexcept
{
    const Layer &l = layerAt(layer);
    return l.on.to != 0.0f || l.on.at(l.engine.sampleClock(), rampSamples_) != 0.0f;
}

void Processor::weightsAt(int64_t t, const std::array<bool, kLayers> &rendering,
                          std::array<float, kLayers> &weights) const noexcept
{
    // Spec §6.3, D1 as amended in Task 10's fix round: the bilinear corner weights of the clamped
    // pad, each times its layer's on/off ramp, normalized by their sum, then times
    // min(1, Σ r_K), so the amplitude sum fades with the ramps when less than one whole layer is
    // on (the last layer switched off, the first switched on). Every term is a product of
    // non-negative values, so no weight is −0; one layer rendering at r = 1 gets
    // (b / b) × min(1, 1), exactly 1.
    static_assert(kLayers == 4);
    const float x = std::clamp(morphX_.at(t, rampSamples_), 0.001f, 0.999f);
    const float y = std::clamp(morphY_.at(t, rampSamples_), 0.001f, 0.999f);
    const float ix = 1.0f - x;
    const float iy = 1.0f - y;
    const std::array<float, kLayers> corner{ix * iy, x * iy, ix * y, x * y};
    std::array<float, kLayers> rb{};
    float sum = 0.0f;
    float ramps = 0.0f;
    for (size_t L = 0; L < kLayers; ++L)
    {
        const float r = rendering[L] ? layers_[L]->on.at(t, rampSamples_) : 0.0f;
        rb[L] = r * corner[L];
        sum += rb[L];
        ramps += r;
    }
    const float fade = std::min(1.0f, ramps);
    for (size_t L = 0; L < kLayers; ++L)
    {
        weights[L] = sum > 0.0f ? rb[L] / sum * fade : 0.0f;
    }
}

float Processor::morphWeightAt(uint8_t layer, int64_t t) const noexcept
{
    // r_K is each ramp's value at t: a layer that isn't rendering has its ramp at 0 there.
    std::array<bool, kLayers> rendering{};
    rendering.fill(true);
    std::array<float, kLayers> weights{};
    weightsAt(t, rendering, weights);
    return weights[std::min<size_t>(layer, kLayers - 1)];
}

void Processor::process(const float *const *in, uint32_t numInChannels, float *const *out,
                        uint32_t numOutChannels, uint32_t numFrames,
                        std::span<const ParamEvent> events, std::span<const NoteEvent> notes,
                        std::span<const ControlEvent> controls) noexcept
{
    // One guard for the whole call, the dry/wet sum included (spec §6.4).
    ScopedDenormalsOff denormalsOff;
    // Read once: the whole call publishes, or none of it does (sub-project 5's spec §6.4).
    publishingThisCall_ = publishing_.load(std::memory_order_relaxed);
    takeOffers();
    // The input feeds the rings in both products (M2 sub-project 1's spec §6.2), and the mix in
    // the FX alone (render).
    if (in == nullptr)
    {
        numInChannels = 0;
    }
    if (out == nullptr)
    {
        numOutChannels = 0;
    }
    const int64_t start = layers_[0]->engine.sampleClock();
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
        stepSources(buffers, rendered, rendered); // the sources' values for the cell from here
        updateEffective();
        syncFreeze();
        retargetRamps(rendered);
    };

    // While a route moves with time alone, every grid boundary below `limit` (and below end) is
    // applied, as an empty group where none pends (the class comment's Time-varying routes). The
    // next boundary to step is the next to apply: every one before it was applied, and a pending
    // group is never past it (its events' joins applied the cells before them). One before it
    // (events joining the boundary already applied, at `rendered`) applies there first.
    const auto applyCellsBelow = [&](int64_t limit) {
        if (!timeVarying_)
        {
            return;
        }
        for (;;)
        {
            const int64_t c = lastStepped_ + static_cast<int64_t>(kControlGrid);
            if (c >= limit || c >= end)
            {
                return;
            }
            assert(!hasPending_ || pendingAt_ <= c);
            if (hasPending_ && pendingAt_ < c)
            {
                applyPending();
                continue;
            }
            if (!hasPending_)
            {
                pendingAt_ = c;
                hasPending_ = true;
            }
            applyPending();
        }
    };

    // Puts an event at `offset` into its boundary's group, applying the pending group first if
    // the boundary is a later one.
    const auto join = [&](uint32_t offset) {
        const int64_t a = start + std::min<int64_t>(offset, numFrames);
        // max: an out-of-order event can't land before what is already rendered. rendered is
        // either the call's start, which a is at or after, or a boundary.
        const int64_t b = std::max(gridCeil(a), rendered);
        applyCellsBelow(b);
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

    // A change of the host's playing flag applies at the call's first boundary (setTransport).
    if (transportChanged_)
    {
        transportChanged_ = false;
        join(0);
    }

    // The three spans merged by offset into one sequence, at a tie a parameter event first, then a
    // note, then a control. The order between the kinds doesn't matter: a group's parameter
    // changes, notes and controls are independent, and each kind keeps its own order.
    size_t nextEvent = 0;
    size_t nextNote = 0;
    size_t nextControl = 0;
    constexpr uint64_t kNone = std::numeric_limits<uint64_t>::max();
    for (;;)
    {
        const uint64_t eventAt = nextEvent < events.size() ? events[nextEvent].offset : kNone;
        const uint64_t noteAt = nextNote < notes.size() ? notes[nextNote].offset : kNone;
        const uint64_t controlAt =
            nextControl < controls.size() ? controls[nextControl].offset : kNone;
        if (eventAt == kNone && noteAt == kNone && controlAt == kNone)
        {
            break;
        }
        if (eventAt <= noteAt && eventAt <= controlAt)
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
        else if (noteAt <= controlAt)
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
        else
        {
            const ControlEvent &c = controls[nextControl++];
            if (!validControl(c))
            {
                continue;
            }
            join(c.offset);
            const float v = unitClamped(c.value);
            switch (c.kind)
            {
            case ControlKind::ModWheel:
                pendingMidi_.modWheel = v;
                break;
            case ControlKind::Aftertouch:
                pendingMidi_.aftertouch = v;
                break;
            case ControlKind::PitchBend:
                pendingMidi_.pitchBend = v;
                break;
            }
        }
    }
    applyCellsBelow(end);
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
    for (const auto &l : layers_)
    {
        handBackRetired(*l);
    }

    for (uint32_t c = 2; c < numOutChannels; ++c)
    {
        if (out[c] != nullptr)
        {
            std::fill_n(out[c], numFrames, 0.0f);
        }
    }

    if (publishingThisCall_)
    {
        // Layer A's view, with every layer's grains (M2 sub-project 1's spec §6.2).
        const Layer &a = *layers_[0];
        const int64_t clock = a.engine.sampleClock();
        uint32_t active = 0;
        for (const auto &l : layers_)
        {
            active += l->engine.activeGrains();
        }
        snapshot_.push({clock, a.engine.liveWriteHead(), a.engineFrozen, active,
                        base_[static_cast<size_t>(ParamSlot::Position)],
                        base_[static_cast<size_t>(ParamSlot::Spray)],
                        effective_[static_cast<size_t>(ParamSlot::Position)],
                        effective_[static_cast<size_t>(ParamSlot::Spray)], feedbackAt(0, clock)});
        // Release: an editor that reads this clock with acquire finds the snapshot it ends.
        publishedClock_.store(clock, std::memory_order_release);
    }
}

void Processor::renderLayerPiece(uint8_t L, int64_t from, uint32_t n, const float *const dry[2],
                                 bool hasInput, uint32_t rendering) noexcept
{
    Layer &l = *layers_[L];
    const bool renders = rendering_[L];
    if (renders != l.enabledGiven)
    {
        // Spec §6.3: off, the engine spawns nothing and its grains end here; on again, the
        // pending onset is forced to this boundary, so the layer speaks at once.
        l.engine.setEnabled(renders);
        l.enabledGiven = renders;
        if (!renders)
        {
            // Nothing of the layer sounds while it is off, so nothing of it is kept for when it
            // comes back: the diffuser's lines and the limited history start from silence then,
            // as the pool does.
            l.diffuser.reset();
            l.filter[0]->reset();
            l.filter[1]->reset();
            l.busGiven = false;
            for (auto &channel : l.wetHistory)
            {
                channel.fill(0.0f);
            }
        }
    }

    // Feedback (sub-project 5's spec §5): ring[t] = sanitize(in[t]) + fb(t)·limited[t − D],
    // sanitize being finiteOrZero and limited from wetHistory; the ring clamps the sum as it
    // clamps any input (the class comment). The ramp changes only at boundaries, between render()
    // calls, so it is 0 throughout a piece whose target and first value are 0: such a piece hands
    // the engine its input as it is (the sign of zero included), as before Feedback.
    const float *ringIn[2] = {dry[0], dry[1]};
    uint32_t ringChannels = hasInput ? 2 : 0;
    if (l.feedback.to != 0.0f || l.feedback.at(from, rampSamples_) != 0.0f)
    {
        for (uint32_t i = 0; i < n; ++i)
        {
            const int64_t t = from + i;
            const float fb = l.feedback.at(t, rampSamples_);
            const auto h = static_cast<size_t>(t % kFeedbackDelay);
            for (size_t c = 0; c < 2; ++c)
            {
                const float d = dry[c] != nullptr ? finiteOrZero(dry[c][i]) : 0.0f;
                // A sample where the ramp is 0 writes the input alone, as a piece without
                // feedback does, so where a piece starts never changes the ring.
                l.ringIn[c][i] = fb != 0.0f ? d + fb * l.wetHistory[c][h] : d;
            }
        }
        ringIn[0] = l.ringIn[0].data();
        ringIn[1] = l.ringIn[1].data();
        ringChannels = 2;
    }
    const int64_t headBefore = l.engine.liveWriteHead();
    float *const wet[2] = {l.wet[0].data(), l.wet[1].data()};
    float *const bus2[2] = {l.bus2[0].data(), l.bus2[1].data()};
    if (renders)
    {
        l.engine.process(ringIn, ringChannels, wet, rendering, bus2, n);
    }
    else
    {
        // Off: the ring is written and the clock advances (Engine::setEnabled), nothing else.
        l.engine.process(ringIn, ringChannels, nullptr, 0, n);
    }
    // The peaks of what the ring took (spec §6.4), publishing or not (ruling PU-T9b), so the
    // strip never holds an older pass's peaks when the editor opens: the buffers the engine was
    // handed, over the frames its head advanced, a prefix of the piece (all of it unless a freeze
    // stopped the head), at ring time.
    const auto written = static_cast<uint32_t>(l.engine.liveWriteHead() - headBefore);
    const float *left = ringChannels > 0 && ringIn[0] != nullptr ? ringIn[0] : kSilence.data();
    const float *right = ringChannels > 0 && ringIn[1] != nullptr ? ringIn[1] : kSilence.data();
    l.peaks.write(left, right, headBefore, written);
    if (!renders)
    {
        return;
    }

    const std::array<float, kFeedbackDelay> &weight = weights_[L];
    for (uint32_t i = 0; i < n; ++i)
    {
        const int64_t t = from + i;
        const float g = l.level.at(t, rampSamples_);
        // The ramps stay between in-range values; the clamp keeps the diffuser's crossfade
        // from extrapolating whatever happens.
        const float delta = std::clamp(l.diffusion.at(t, rampSamples_), 0.0f, 1.0f);
        // One channel: its mono render is diffused as both sides.
        float w[2] = {wet[0][i], rendering > 1 ? wet[1][i] : wet[0][i]};
        // The two buses (sub-project 4's spec §6.3): each through its filter, set at every grid
        // boundary from the effective values in force, then summed once a grain has been on bus 2.
        if (!l.busGiven || t % kControlGrid == 0)
        {
            const BusParams bp = busParams(effective_, L);
            for (size_t b = 0; b < 2; ++b)
            {
                l.filter[b]->setCell(bp.type[b], bp.cutoffHz[b], bp.resonance[b]);
            }
            l.busGiven = true;
        }
        l.filter[0]->process(w[0], w[1]);
        if (l.engine.bus2Used())
        {
            float w2[2] = {bus2[0][i], rendering > 1 ? bus2[1][i] : bus2[0][i]};
            l.filter[1]->process(w2[0], w2[1]);
            w[0] += w2[0];
            w[1] += w2[1];
        }
        else
        {
            // Nothing on bus 2 yet: its filter hears silence, which leaves it silent.
            float silent[2] = {0.0f, 0.0f};
            l.filter[1]->process(silent[0], silent[1]);
        }
        l.diffuser.process(w[0], w[1], delta);
        for (size_t c = 0; c < 2; ++c)
        {
            const float limited = softLimit(w[c] * g);
            // Read for sample t + D's ring input, D samples from now.
            l.wetHistory[c][static_cast<size_t>(t % kFeedbackDelay)] = limited;
            wet[c][i] = limited * weight[i];
        }
    }
}

void Processor::render(const Buffers &b, int64_t from, int64_t to) noexcept
{
    const bool fx = config_.mode == Mode::Fx;
    // Channels 0 and 1 carry the signal; one output channel gets the engines' mono render. With
    // no output channel the engines render stereo all the same, so the diffusers and the reverb
    // hear what they would have, and the next call continues the same render.
    const uint32_t channels = std::min(b.numOutChannels, 2u);
    const uint32_t rendering = channels == 0 ? 2u : channels;
    // The sources step at every boundary the range covers (none of them has a group pending: those
    // end a range), and the follower's input is scanned, before any output is written over it.
    if (from < to)
    {
        stepSources(b, to - 1, to);
    }
    while (from < to)
    {
        // Pieces are at most kFeedbackDelay samples in both products (sub-project 5's spec §5;
        // M2 sub-project 1's spec §6.2), so a piece's ring input needs only wet samples rendered
        // before it.
        int64_t n = std::min<int64_t>(to - from, pieceMax_);
        // A layer ramping off stops rendering at the sample its ramp reaches 0 (the class
        // comment): the piece ends there, so that is where it stops however the host splits
        // blocks.
        for (const auto &l : layers_)
        {
            if (l->on.to == 0.0f && l->on.at(from, rampSamples_) != 0.0f)
            {
                const int64_t zero = l->on.start + rampSamples_;
                if (zero > from && zero < from + n)
                {
                    n = zero - from;
                }
            }
        }
        const auto count = static_cast<uint32_t>(n);
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

        // Which layers render, and their weights, fixed before any layer renders (spec §6.7).
        for (size_t L = 0; L < kLayers; ++L)
        {
            const Ramp &on = layers_[L]->on;
            rendering_[L] = on.to != 0.0f || on.at(from, rampSamples_) != 0.0f;
        }
        std::array<float, kLayers> weights{};
        for (uint32_t i = 0; i < count; ++i)
        {
            weightsAt(from + i, rendering_, weights);
            for (size_t L = 0; L < kLayers; ++L)
            {
                weights_[L][i] = weights[L];
            }
        }
        // The layers (spec §6.7): the off layers' ring writes inline, then one task per rendering
        // layer, in order A to D, from the piece state fixed here, read-only until run() returns.
        // One rendering layer runs inline, without a request.
        pieceFrom_ = from;
        pieceN_ = count;
        pieceDry_ = {dry[0], dry[1]};
        pieceHasInput_ = b.numInChannels > 0;
        pieceRendering_ = rendering;
        taskCount_ = 0;
        for (uint8_t L = 0; L < kLayers; ++L)
        {
            if (rendering_[L])
            {
                taskLayers_[taskCount_++] = L;
            }
            else
            {
                renderLayerPiece(L, from, count, dry, pieceHasInput_, rendering);
            }
        }
        if (taskCount_ == 1)
        {
            execTask(0);
        }
        else if (taskCount_ > 1)
        {
            runner_->run(taskCount_, &layerTask, this);
        }
        // The piece's grain events, in layer order A to D (spec §6.7), after the join.
        drainEvents();
        // No task outside a request: a host that calls thread_pool.exec now (a CLAP violation)
        // runs nothing (execTask checks i < taskCount_).
        taskCount_ = 0;

        float *dst[2] = {nullptr, nullptr};
        for (uint32_t c = 0; c < channels; ++c)
        {
            dst[c] = b.out[c] != nullptr ? b.out[c] + offset : nullptr;
        }
        for (uint32_t i = 0; i < count; ++i)
        {
            const int64_t t = from + i;
            const float m = mix_.at(t, rampSamples_);
            const float a = amount_.at(t, rampSamples_);
            const float s = drySend_.at(t, rampSamples_);
            const bool active = a != 0.0f; // read by the reverb at a chunk's first sample
            // Read both inputs before writing either output: out may be in, and a mono input
            // feeds both channels. The instrument's input feeds the rings alone.
            float d[2] = {0.0f, 0.0f};
            for (uint32_t c = 0; c < rendering; ++c)
            {
                d[c] = fx && dry[c] != nullptr ? finiteOrZero(dry[c][i]) : 0.0f;
            }
            // The layers' sum (spec §6.3): the first rendering layer assigns, each later one
            // adds, in order A to D; with none rendering it is +0.
            float wet[2] = {0.0f, 0.0f};
            bool first = true;
            for (size_t L = 0; L < kLayers; ++L)
            {
                if (!rendering_[L])
                {
                    continue;
                }
                const Layer &l = *layers_[L];
                for (size_t c = 0; c < 2; ++c)
                {
                    wet[c] = first ? l.wet[c][i] : wet[c] + l.wet[c][i];
                }
                first = false;
            }
            // The delay (spec §6.4), after the sum and before the reverb: one mono render is
            // delayed as a pair. At Amount 0 it leaves the frame untouched, bit for bit.
            if (rendering == 1)
            {
                wet[1] = wet[0];
            }
            delay_.process(wet[0], wet[1], delayAmount_.at(t, rampSamples_),
                           delayFeedback_.at(t, rampSamples_), delayDamp_.at(t, rampSamples_),
                           delayParams_.pingPong);
            float o[2];
            float rIn[2];
            for (uint32_t c = 0; c < 2; ++c)
            {
                o[c] = fx ? d[c] * (1.0f - m) + wet[c] * m : wet[c];
                rIn[c] = fx ? m * wet[c] + s * d[c] : wet[c];
            }
            if (rendering == 1)
            {
                rIn[1] = rIn[0];
            }
            // The limiter (spec §6.5) takes the wet side alone, after the reverb's return: with
            // its mode Off and settled, the M1 expressions below stand as they were (the
            // association matters to the goldens' bytes); otherwise the wet side is split from
            // the dry one, limited, and the dry added after it.
            const bool limiting = !limiter_.isOff() || limiterParams_.mode != LimiterMode::Off;
            float side[2]{};
            if (limiting)
            {
                for (uint32_t c = 0; c < 2; ++c)
                {
                    side[c] = fx ? wet[c] * m : wet[c];
                }
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
                float *wetSide = limiting ? side : o;
                wetSide[0] += softLimit(a * r[0]);
                wetSide[1] += softLimit(a * r[1]);
            }
            if (limiting)
            {
                limiter_.process(side[0], side[1], limiterParams_.mode,
                                 limiterCeiling_.at(t, rampSamples_),
                                 limiterRelease_.at(t, rampSamples_));
                for (uint32_t c = 0; c < 2; ++c)
                {
                    o[c] = fx ? d[c] * (1.0f - m) + side[c] : side[c];
                }
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
