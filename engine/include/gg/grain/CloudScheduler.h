// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_GRAIN_CLOUDSCHEDULER_H
#define GG_GRAIN_CLOUDSCHEDULER_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

#include "gg/core/FpContract.h"
#include "gg/core/MathDet.h"
#include "gg/core/Rng.h"
#include "gg/grain/Tier.h"
#include "gg/source/SourceMode.h"

namespace gg
{
/// A layer's Clock (M3 sub-project 3's spec §4.1): Free, M2's scheduler, or Pitch, one grain a
/// period of the note's pitch.
enum class Clock : uint8_t
{
    Free,
    Pitch,
};

/// The controls of one Cloud grain stream. CloudScheduler::setParams sanitizes them: a
/// non-finite value becomes the default, then every value is clamped to the range given here.
/// Position and spray depend on the scheduler's source mode (ruling M1-k).
struct CloudParams
{
    /// Grains per second. 0 or below turns the stream off (ruling M1-a); any positive value is
    /// clamped to [0.01, sampleRate].
    double densityHz{16.0};
    double distribution{0.0};     ///< 0 = periodic onsets, 1 = Poisson; [0, 1].
    double grainDurationSec{0.1}; ///< At least 16 samples (the minimum window length).
    /// Where grains read. Sample mode: a fraction of the source, [0, 1]. Live mode: seconds
    /// behind the write head, [0, ring seconds]. Spectral mode (the extremes spec §6.2): where the
    /// smear reads its material, in the material's units, a fraction of the sample or seconds
    /// behind the live ring's head, [0, max(1, ring seconds)]; its grains never read it.
    double position{0.5};
    /// Random spread of position. Sample mode: bipolar, as a fraction of the source (M0's
    /// positionJitter), [0, 1], or with sprayInSeconds, bipolar in seconds, [0, +inf). Live
    /// mode: seconds, further behind the head only, [0, ring seconds]. Spectral mode: seconds
    /// behind the spectral ring's head only, [0, SpectralSource::kRingSeconds].
    double spray{0.0};
    double pitchSemitones{0.0}; ///< [-48, 48].
    double stereo{0.0};         ///< Random pan spread: 0 = every grain centered; [0, 1].
    float gain{0.5f};           ///< [0, 4].
    /// The Texture envelope of each grain that starts from now on (spec §4; Envelope.h): 0 is
    /// the plain Hann; [0, 1].
    double texture{0.0};
    /// The interpolation tier of each grain that starts from now on (ruling 2). An enum, so
    /// setParams copies it as given.
    Tier quality{Tier::Normal};
    /// Sample mode only: spray is seconds, so its spread doesn't depend on the source's length
    /// (the instrument's Spray; sub-project 3's spec §6.8). Each grain then reads from
    /// clamp(position × (frames − 1) + spray × bipolar(u) × sourceRate, 0, frames − 1), with
    /// GrainDraw carrying the spray's offset in seconds for the source to convert. Live mode
    /// ignores it: its spray is already seconds. Copied as given.
    bool sprayInSeconds{false};
    /// Each grain's pitch spread in semitones (M2 spec §6.6): the grain plays
    /// spread × bipolar(u) semitones off its pitch, before the ±48 clamp; [0, 48].
    double pitchSpreadSemitones{0.0};
    /// Each grain's size spread in octaves (M2 spec §6.6): the grain's duration is
    /// grainDurationSec × 2^(spread × bipolar(u)), clamped to [5 ms, 2 s]; [0, 4].
    double sizeSpreadOctaves{0.0};
    /// Smear's position jitter in seconds (extremes spec §6.1): each grain's position gains
    /// jitter × bipolar(u) seconds, in Live mode behind the head, in Sample mode carried in
    /// GrainDraw::sprayOffsetSec for the source to convert; [0, 2].
    double smearJitterSec{0.0};
    /// Smear's micro-detune in semitones (extremes spec §6.1): each grain's pitch gains
    /// detune × bipolar(u) semitones, after the pitch spread's term and before the ±48 clamp;
    /// [0, 1].
    double smearDetuneSemitones{0.0};
    /// The share of grains on the layer's second bus (extremes spec §6.3): a grain takes bus 1
    /// when uniform01(u) < busSplit; [0, 1].
    double busSplit{0.0};
    /// The onset clock (M3 pitch-sync spec §4.2, SB2). At Free every other field means what it
    /// says above, and refreshHz is ignored. At Pitch, densityHz is the note's f0 (0 or below
    /// turns the stream off; a positive value is clamped to [1, sampleRate / 4]), grainDurationSec
    /// is Size's seconds, read in periods (S3), and refreshHz is the refresh rate (S4). An enum,
    /// so setParams copies it as given; the Cloud stream is always given Free.
    Clock clock{Clock::Free};
    /// Clock Pitch only: how often a voice draws a new grain shape, in Hz (S4). A non-finite value
    /// means 16; then it is clamped to [0.01, sampleRate].
    double refreshHz{0.0};
};

/// One grain as the scheduler draws it, before the source turns it into a GrainSpec. Every
/// field is latched at the grain's onset (INTENT §14).
struct GrainDraw
{
    uint64_t index;      ///< k, counted from the scheduler's reset().
    int64_t onsetSample; ///< floor(t(k)), in output samples.
    double onsetFrac;    ///< t(k) - onsetSample, in [0, 1).
    /// Sample mode: clamp(position + spray × bipolar(u), 0, 1), a fraction of the source, or
    /// with CloudParams::sprayInSeconds, the position alone. Live mode: position + spray ×
    /// uniform01(u), in seconds behind the write head; the live source's admission clamp then
    /// applies (spec §6.3). Spectral mode: spray × uniform01(u), Live's draw with Position 0, in
    /// seconds behind the spectral ring's head, under the same clamp (the extremes spec §6.2).
    /// u is keyed by (seed, layer, stream, PositionJitter, k) in every mode. With a Smear jitter
    /// j, Live and Spectral mode add j × bipolar(u') seconds here, before the clamp, u' keyed by
    /// SmearJitter (Sample mode adds it to sprayOffsetSec instead; extremes spec §6.1).
    double positionDraw;
    /// Sample mode with CloudParams::sprayInSeconds: spray × bipolar(u), in seconds, which the
    /// source adds to positionDraw at its own rate (Engine::sampleGrain). Otherwise +0. With a
    /// Smear jitter j, Sample mode adds j × bipolar(u') seconds here, u' keyed by SmearJitter,
    /// and Live and Spectral mode add it to positionDraw instead (extremes spec §6.1).
    double sprayOffsetSec;
    /// The pitch ratio: exp2Det(clamp(pitchSemitones + (key − 60), −48, 48) / 12) with a key,
    /// exp2Det(pitchSemitones / 12) without. With a pitch spread s,
    /// exp2Det(clamp(pitchSemitones + (key − 60) + s × bipolar(u), −48, 48) / 12), the key term
    /// 0 without a key and u keyed by PitchSpread (M2 spec §6.6). A Smear detune c adds
    /// c × bipolar(u') after the spread's term, u' keyed by SmearDetune (extremes spec §6.1).
    double rate;
    /// Window length in output samples, at least 16: grainDurationSec × sampleRate, or with a
    /// size spread s, exp2Det(clamp(log2(grainDurationSec) + s × bipolar(u), log2 0.005,
    /// log2 2)) × sampleRate, u keyed by SizeSpread (M2 spec §6.6). At Clock Pitch, that many
    /// seconds × kPeriodsPerSizeSecond periods, within [16, 2 × sampleRate] (the class comment).
    double durationSamples;
    double texture{0.0}; ///< CloudParams::texture at the onset.
    float gain;
    float pan;               ///< stereo × bipolar(u), in [-1, 1]; +0 at stereo 0 (see schedule()).
    Tier tier{Tier::Normal}; ///< CloudParams::quality at the onset.
    /// The held key the grain plays (spec §6.1), or −1 when none is held.
    int16_t key{-1};
    /// The layer bus the grain renders into, 0 or 1 (extremes spec §6.3): 1 when
    /// uniform01(u) < CloudParams::busSplit, u keyed by Bus; 0, with no draw, at a split of 0.
    uint8_t bus{0};
    /// Clock Pitch: true when the grain isn't a refresh, so every draw but its onset's is the last
    /// refresh's (S4), and the engine holds the ring's read anchor (§4.6). Always false at Free.
    bool held{false};

    friend bool operator==(const GrainDraw &, const GrainDraw &) = default;
};

/// The Cloud scheduler (INTENT §14; spec §6.2): one grain stream whose onsets run continuously
/// from periodic (distribution 0) to Poisson (distribution 1).
///
/// Onsets follow the recurrence t(0) = 0, t(k+1) = t(k) + Δ(k), in output samples since
/// reset(), with
///
///     Δ(k) = max(P × ((1 − d) + d × E(k)), kMinIntervalSamples),
///     P = sampleRate / densityHz,  E(k) = −logDet(1 − u(k)),
///
/// where d is the distribution and u(k) = uniform01(hash(seed, layer, stream, Onset, k)). E is a
/// unit exponential, so the mean interval is P at every d. At d = 0 every interval is exactly P.
/// When the sums k × P are exact too, as for a whole number of samples or a period like 5512.5,
/// the onsets are exactly M0's lattice; otherwise they differ from it in the last bits.
///
/// t(k+1) is computed when grain k is emitted, from the parameters in effect then. A change to
/// the density or the distribution also moves the onset already pending, so a change is heard
/// at once rather than after an interval drawn with the old values (ruling M1-h; it supersedes
/// spec §6.2 for the pending onset). The change takes effect at g, the blockStart of the next
/// schedule() call. If grain k−1 was emitted since reset() or since the stream was turned on,
/// and the pending onset t(k) is after g, then t(k) becomes max(t(k−1) + Δ_new(k−1), g):
/// Δ(k−1) recomputed with the new values from the same draw E(k−1), so no new draw is made.
/// Otherwise the pending onset stays. Changes land on grid-aligned absolute samples (the
/// Processor applies them there), so this is block-size invariant too.
///
/// **Notes** (spec §6.1–§6.3). setHeldKeys gives the held keys, n of them. While n > 0, grain k
/// plays keys[min(floor(u × n), n − 1)], u = uniform01(hash(seed, layer, stream, Note, k)), at
/// the rate exp2Det(clamp(pitchSemitones + (key − 60), −48, 48) / 12); key 60 plays at the
/// Pitch parameter alone. Every grain draws, so the draw doesn't depend on n, and with no key
/// held nothing is drawn: every other draw, and the rate, are what they were before notes.
/// When the held set goes from empty to non-empty, the next schedule() call that runs the
/// stream moves the pending onset to its blockStart g, so the first grain sounds at the note's
/// boundary rather than after an interval drawn before it. That forced onset replaces ruling
/// M1-h's rescale at g, and there is then no previous grain to rescale from until the next one
/// is emitted. Keys added to a non-empty set don't move the onset, and k keeps counting across
/// notes. With the note gate on (the instrument), no grain is emitted while no key is held: the
/// stream is held as if off.
///
/// **Spreads** (M2 spec §6.6). A pitch spread adds pitchSpreadSemitones × bipolar(u) to the
/// grain's semitones before the ±48 clamp, and a size spread adds sizeSpreadOctaves × bipolar(u)
/// to its log2 duration in seconds, clamped to [log2 0.005, log2 2]; each u has its own field,
/// PitchSpread and SizeSpread. A spread of 0 takes M1's path, with no draw and no log, so the
/// rate and the duration are M1's bit for bit (the two paths meet only approximately as the
/// spread goes to 0). The gain stays the layer's: its normalization uses the layer's Size, not
/// the grain's.
///
/// **Smear and the bus** (extremes spec §6.1, §6.3). Smear's detune adds
/// smearDetuneSemitones × bipolar(u) to the grain's semitones after the pitch spread's term,
/// before the ±48 clamp; its jitter adds smearJitterSec × bipolar(u) seconds to the position, in
/// Live mode to the seconds behind the head and in Sample mode to sprayOffsetSec. A grain takes
/// bus 1 when uniform01(u) < busSplit. Each u has its own field (SmearDetune, SmearJitter, Bus),
/// and each amplitude of 0 skips its draw and its term, so every draw is what it was without
/// them, bit for bit, on bus 0.
///
/// Every random field of a grain has its own rng::Field, keyed by (seed, layer, stream, field,
/// k), so the draws don't depend on block size, call order or each other.
///
/// **Clock Pitch** (M3 pitch-sync spec §4.2, S2 to S6, S9, SB2 to SB4; a voice's stream). With
/// CloudParams::clock Pitch, densityHz is the note's f0, clamped to [1, sampleRate / 4], and
/// P = sampleRate / f0. The onsets follow Δ(k) = P × (1 + 0.5 × d³ × bipolar(u(k))), u keyed by
/// Onset and the grain's own k, so the mean interval is P and every interval is within
/// [0.5 P, 1.5 P]; ruling M1-h's rescale uses this law with the same draw. A grain is
/// clamp(S × kPeriodsPerSizeSecond × P, 16, 2 × sampleRate) samples long, S Size's seconds (with
/// a size spread, moved in the log2 domain within [5 ms, 2 s] first). Grain k is a refresh when
/// no refresh is valid (since reset(), a Clock change or restartRefresh()) or when its onset is
/// at least sampleRate / refreshHz samples (the rate in effect at its emission) after the last
/// refresh's; every draw but the onset's is then keyed by the last refresh's index instead of k,
/// so the grains between two refreshes repeat one shape (GrainDraw::held). A Clock change forces
/// an onset at the next run's blockStart, drops any pending rescale and invalidates the refresh
/// (S9). At Free none of this runs: the only new step on Free's path is the test of the clock.
///
/// All members are real-time safe. Nothing in schedule() calls libm except the exact floor:
/// E(k) comes from logDet and the pitch ratios from exp2Det, so no libm value reaches a grain.
class CloudScheduler
{
  public:
    /// Periods of the note's pitch in one second of Size (Pitch clock; spec S1): 40/3.
    static constexpr double kPeriodsPerSizeSecond = 40.0 / 3.0;

    /// The floor on Δ(k) (ruling M1-g). At d = 1 an interval P × E(k) can be arbitrarily small
    /// (it is 0 when u(k) = 0), so without a floor nothing would bound the number of onsets in
    /// one sample, and schedule() would have no bounded loop. With it, one schedule() call
    /// emits at most about 16 grains per frame. At the maximum density, the sample rate (P = 1),
    /// it raises about 6.1% of Poisson intervals, each by less than 1/16 sample, moving the mean
    /// by about 2e-3 samples; at 20 kHz with a 48 kHz sample rate (P = 2.4), it raises about
    /// 2.6%, moving the mean by about 8e-4 samples. At d = 0 it never applies, since P is at
    /// least 1 sample.
    ///
    /// The floor bounds the loop only while t < 2^49 samples (about 93 years at 192 kHz).
    /// From 2^49 on, the spacing of doubles is 1/8 sample, so t + 1/16 is a tie that can round
    /// back to t, and a run of floored intervals would make no progress.
    static constexpr double kMinIntervalSamples = 1.0 / 16.0;

    /// The bound on CloudParams::pitchSemitones, and on a grain's pitch with a key held.
    static constexpr double kMaxPitchSemitones = 48.0;

    CloudScheduler() noexcept { prepare(48000.0); }

    /// Sets the output sample rate (a non-finite or non-positive rate means 48000) and resets.
    void prepare(double sampleRate) noexcept;

    /// Takes new controls, sanitized as CloudParams documents. They apply to every grain emitted
    /// after this call. A change to the effective period or the distribution also moves the
    /// pending onset at the next schedule() call, as the class comment describes (ruling M1-h).
    void setParams(const CloudParams &params) noexcept;

    /// Sets how position and spray are drawn (ruling M1-k; see CloudParams and GrainDraw), and
    /// re-sanitizes the controls last given to setParams for the new mode. ringSeconds is the live
    /// ring's length, which bounds both in Live mode; a non-finite or negative value means 0. In
    /// Spectral mode it bounds Position with 1 (the material's units, a sample's fraction or the
    /// live ring's seconds), and SpectralSource::kRingSeconds bounds Spray (the extremes spec
    /// §6.2). The default is Sample. Neither the schedule nor any other draw changes.
    void setSourceMode(SourceMode mode, double ringSeconds) noexcept;

    /// The controls in effect: the last setParams() sanitized for the source mode.
    const CloudParams &params() const noexcept { return params_; }

    /// The held keys (spec §6.1): distinct MIDI keys 0..127, ascending, at most 128. Copied;
    /// no allocation. They apply to every grain emitted after this call. When the set goes from
    /// empty to non-empty, the next schedule() call that runs the stream forces an onset at its
    /// blockStart, as the class comment describes; emptied again before then, it forces
    /// nothing. The default is none.
    void setHeldKeys(std::span<const uint8_t> keys) noexcept;

    /// true (the instrument): no grain is emitted while no key is held. The default is false
    /// (the FX).
    void setNoteGate(bool gate) noexcept { noteGate_ = gate; }

    /// Forces an onset at the next schedule() call that runs the stream, at its blockStart: the
    /// same path the first key held after none takes (setHeldKeys; the class comment's
    /// **Notes**). Engine::setEnabled calls it when a layer is switched on (spec §6.2).
    void forceNextOnset() noexcept { onsetForced_ = true; }

    /// Clock Pitch: the next grain is a refresh, with draws of its own (SB4). Engine calls it when
    /// it drops a voice's held anchor. The schedule doesn't move.
    void restartRefresh() noexcept { refreshValid_ = false; }

    /// Restarts the schedule: the next onset is t = 0 with k = 0. Keeps the sample rate, the
    /// controls, the held keys and the note gate.
    void reset() noexcept;

    /// Calls emit(const GrainDraw &) for every grain with onsetSample in
    /// [blockStart, blockStart + numFrames), in onset order. Blocks must be contiguous from the
    /// last reset(), starting at 0, but for one gap: Engine::process calls this only while its
    /// layer is enabled, and when its enable flag turns on (Engine::setEnabled only requests it)
    /// Engine::process forces an onset (forceNextOnset), so the first block after a disabled gap
    /// starts where the engine resumes.
    /// The forced onset moves the pending onset to that block's blockStart and forgets the last
    /// grain, so nothing is drawn for the gap and nothing is rescaled from before it. seed, layer
    /// and stream key the random draws.
    ///
    /// While the stream is off, or gated with no key held, nothing is emitted, and the pending
    /// onset keeps its time. If that time has passed when the stream is turned back on, the
    /// onset moves to blockStart, the first sample after the change. A forced onset (see the
    /// class comment) moves it to blockStart whatever its time.
    template <class Emit>
    void schedule(int64_t blockStart, uint32_t numFrames, uint64_t seed, uint32_t layer,
                  uint32_t stream, Emit &&emit) noexcept
    {
        if (!beginRun(blockStart))
        {
            return; // off, or gated with no key held
        }
        const auto end = static_cast<double>(blockStart + static_cast<int64_t>(numFrames));
        while (nextOnset_ < end)
        {
            emit(takeNext(seed, layer, stream));
        }
    }

    /// schedule() a grain at a time, for a caller that merges several streams by onset (the
    /// engine's voices, M3 Voice spec §4.3): the same draws, bit for bit, with no allocation.
    /// For the block starting at blockStart, call beginRun(blockStart) once; if it returns true,
    /// call takeNext() while pendingOnset() is before the block's end. That is exactly what
    /// schedule() does. false means the stream emits nothing in this block (off, or gated with no
    /// key held), and changes nothing, as schedule() then changes nothing; don't call takeNext().
    /// beginRun applies what schedule() applies at its blockStart: a forced onset (moved to
    /// blockStart), the resume after being off (ruling M1-j) and ruling M1-h's rescale.
    bool beginRun(int64_t blockStart) noexcept
    {
        GG_FP_CONTRACT_OFF
        if (!(densityHz_ > 0.0) || (noteGate_ && keyCount_ == 0))
        {
            return false; // off, or gated with no key held
        }
        const auto start = static_cast<double>(blockStart);
        if (onsetForced_)
        {
            // Spec §6.2–§6.3: the held set went from empty to non-empty, so the cloud starts at
            // g = start. No grain before it is rescaled from (ruling M1-h).
            onsetForced_ = false;
            nextOnset_ = start;
            hasLast_ = false;
            rescalePending_ = false;
        }
        nextOnset_ = std::max(nextOnset_, start); // resumed after being off (ruling M1-j)
        if (rescalePending_)
        {
            // Ruling M1-h, with g = start: rescale the pending onset from the last one, with
            // the last one's draw.
            rescalePending_ = false;
            if (nextOnset_ > start)
            {
                nextOnset_ = std::max(lastOnset_ + interval(lastE_), start);
            }
        }
        return true;
    }

    /// t(k), the onset of the grain takeNext() draws next, in output samples since reset().
    double pendingOnset() const noexcept { return nextOnset_; }

    /// Draws grain k at pendingOnset() and moves on to k + 1: one iteration of schedule()'s loop.
    /// seed, layer and stream key the draws, as schedule()'s do. Call it only after beginRun()
    /// returned true for the block that holds pendingOnset().
    GrainDraw takeNext(uint64_t seed, uint32_t layer, uint32_t stream) noexcept
    {
        // Covers the lambda below too: a lambda body takes the pragma of its enclosing scope.
        GG_FP_CONTRACT_OFF
        const double t = nextOnset_;
        const uint64_t k = nextK_;
        const double whole = std::floor(t);
        const bool pitch = params_.clock == Clock::Pitch;
        bool held = false;
        if (pitch)
        {
            // S4, SB4: a refresh when none is valid, or when the refresh period in effect has
            // passed since the last one's onset; every other grain holds its draws.
            if (!refreshValid_ || t - refreshOnset_ >= refreshPeriod_)
            {
                refreshK_ = k;
                refreshOnset_ = t;
                refreshValid_ = true;
            }
            else
            {
                held = true;
            }
        }
        // At Clock Pitch every draw but the onset's is keyed by the last refresh's index (S4).
        const auto key = [&](rng::Field field) {
            return rng::hash(
                {seed, layer, stream, field, pitch && field != rng::Field::Onset ? refreshK_ : k});
        };
        const uint64_t jitter = key(rng::Field::PositionJitter);
        int16_t note = -1;
        double rate = pitchRatio_;
        if (keyCount_ > 0)
        {
            // floor(u × n) by truncation, as u × n >= 0; u × n < n, and the min() keeps the
            // index in range whatever the rounding.
            const double u = rng::uniform01(key(rng::Field::Note));
            const auto i =
                std::min(static_cast<uint32_t>(u * static_cast<double>(keyCount_)), keyCount_ - 1);
            note = keys_[i];
        }
        if (params_.pitchSpreadSemitones != 0.0 || params_.smearDetuneSemitones != 0.0)
        {
            // M2 spec §6.6: the spread is added before the clamp; the extremes spec §6.1:
            // Smear's detune after it. A term whose amplitude is 0 is skipped, with no draw.
            const double keyTerm = note >= 0 ? static_cast<double>(note - 60) : 0.0;
            double semis = params_.pitchSemitones + keyTerm;
            if (params_.pitchSpreadSemitones != 0.0)
            {
                semis += params_.pitchSpreadSemitones * rng::bipolar(key(rng::Field::PitchSpread));
            }
            if (params_.smearDetuneSemitones != 0.0)
            {
                semis += params_.smearDetuneSemitones * rng::bipolar(key(rng::Field::SmearDetune));
            }
            rate = exp2Det(std::clamp(semis, -kMaxPitchSemitones, kMaxPitchSemitones) / 12.0);
        }
        else if (note >= 0)
        {
            rate = exp2Det(std::clamp(params_.pitchSemitones + static_cast<double>(note - 60),
                                      -kMaxPitchSemitones, kMaxPitchSemitones) /
                           12.0);
        }
        double durationSamples = durationSamples_;
        if (pitch)
        {
            // S3: N = Size × 40/3 periods of the f0 at the onset, within [16 samples, 2 s]; a
            // size spread moves Size's seconds first, within Size's range, as at Free.
            double seconds = params_.grainDurationSec;
            if (params_.sizeSpreadOctaves != 0.0)
            {
                seconds = exp2Det(
                    std::clamp(durationLog2_ + params_.sizeSpreadOctaves *
                                                   rng::bipolar(key(rng::Field::SizeSpread)),
                               kMinSpreadLog2Seconds, kMaxSpreadLog2Seconds));
            }
            durationSamples =
                std::clamp(seconds * kPeriodsPerSizeSecond * period_, kMinDurationSamples,
                           std::max(kMinDurationSamples, 2.0 * sampleRate_));
        }
        else if (params_.sizeSpreadOctaves != 0.0)
        {
            // M2 spec §6.6: the spread is in the log2 domain, within Size's range.
            const double log2Seconds =
                std::clamp(durationLog2_ + params_.sizeSpreadOctaves *
                                               rng::bipolar(key(rng::Field::SizeSpread)),
                           kMinSpreadLog2Seconds, kMaxSpreadLog2Seconds);
            durationSamples = std::clamp(exp2Det(log2Seconds) * sampleRate_, kMinDurationSamples,
                                         kMaxDurationSamples);
        }
        // Live mode: seconds behind the head, further back only. Spectral mode: the same with
        // Position replaced by 0, since Position addresses the smear's material, not its ring
        // (the extremes spec §6.2). Sample mode: a fraction of the source, with the spray
        // folded in, or with sprayInSeconds the position alone and the spray carried in
        // seconds for the source to convert.
        const bool inSeconds = mode_ == SourceMode::Sample && params_.sprayInSeconds;
        double positionDraw = params_.position;
        if (mode_ == SourceMode::Spectral)
        {
            positionDraw = params_.spray * rng::uniform01(jitter);
        }
        else if (mode_ == SourceMode::Live)
        {
            positionDraw += params_.spray * rng::uniform01(jitter);
        }
        else if (!inSeconds)
        {
            positionDraw =
                std::clamp(params_.position + params_.spray * rng::bipolar(jitter), 0.0, 1.0);
        }
        double sprayOffsetSec = inSeconds ? params_.spray * rng::bipolar(jitter) : 0.0;
        if (params_.smearJitterSec != 0.0)
        {
            // The extremes spec §6.1: seconds behind the head in Live and Spectral mode (the
            // admission clamp then applies, as to the spray); in Sample mode carried in
            // seconds, for the source to convert at its own rate and clamp, as the spray in
            // seconds is.
            const double smear =
                params_.smearJitterSec * rng::bipolar(key(rng::Field::SmearJitter));
            if (mode_ != SourceMode::Sample)
            {
                positionDraw += smear;
            }
            else
            {
                sprayOffsetSec += smear;
            }
        }
        // The extremes spec §6.3: no draw at a split of 0, so every grain is on bus 0.
        const uint8_t bus =
            params_.busSplit > 0.0 && rng::uniform01(key(rng::Field::Bus)) < params_.busSplit ? 1
                                                                                              : 0;
        const double p = params_.stereo * rng::bipolar(key(rng::Field::Pan));
        // A pan below the smallest normal float is 0, so the value doesn't depend on the
        // flush-to-zero mode. It is +0, never the -0 of stereo 0 times a negative draw.
        const float pan =
            std::abs(p) < std::numeric_limits<float>::min() ? 0.0f : static_cast<float>(p);
        const GrainDraw g{
            .index = k,
            .onsetSample = static_cast<int64_t>(whole),
            .onsetFrac = t - whole,
            .positionDraw = positionDraw,
            .sprayOffsetSec = sprayOffsetSec,
            .rate = rate,
            .durationSamples = durationSamples,
            .texture = params_.texture,
            .gain = params_.gain,
            .pan = pan,
            .tier = params_.quality,
            .key = note,
            .bus = bus,
            .held = held,
        };
        // Free: a unit exponential (the class comment); Pitch: bipolar(u), for S5's jitter. Either
        // is kept as lastE_, so ruling M1-h's rescale uses the clock's own law (SB3).
        const double e = pitch ? rng::bipolar(key(rng::Field::Onset))
                               : -logDet(1.0 - rng::uniform01(key(rng::Field::Onset)));
        lastOnset_ = t;
        lastE_ = e;
        hasLast_ = true;
        nextOnset_ = t + interval(e);
        ++nextK_;
        return g;
    }

  private:
    // The bounds on CloudParams::pitchSpreadSemitones and CloudParams::sizeSpreadOctaves.
    static constexpr double kMaxPitchSpreadSemitones = 48.0;
    static constexpr double kMaxSizeSpreadOctaves = 4.0;
    // The bounds on CloudParams::smearJitterSec and CloudParams::smearDetuneSemitones.
    static constexpr double kMaxSmearJitterSec = 2.0;
    static constexpr double kMaxSmearDetuneSemitones = 1.0;
    // The bounds on a spread grain's log2 duration in seconds: log2 0.005 and log2 2, Size's
    // range (M2 spec §6.6).
    static constexpr double kMinSpreadLog2Seconds = -7.643856189774724;
    static constexpr double kMaxSpreadLog2Seconds = 1.0;
    // The minimum window length in samples, and the maximum, which keeps grainEndSample and the
    // int64 sample arithmetic exact (about 3000 years at 48 kHz).
    static constexpr double kMinDurationSamples = 16.0;
    static constexpr double kMaxDurationSamples = 0x1.0p52;

    void updateDerived() noexcept;

    /// Δ for the draw e, with the current period and distribution: at Free e is a unit
    /// exponential; at Pitch it is bipolar, and Δ = P × (1 + 0.5 d³ e) is within [0.5 P, 1.5 P]
    /// with P at least 4 samples (at any rate of 4 Hz or more), so there is no floor (S5).
    double interval(double e) const noexcept
    {
        GG_FP_CONTRACT_OFF
        const double d = params_.distribution;
        if (params_.clock == Clock::Pitch)
        {
            const double d3 = d * d * d;
            return period_ * (1.0 + 0.5 * d3 * e);
        }
        return std::max(period_ * ((1.0 - d) + d * e), kMinIntervalSamples);
    }

    double sampleRate_{48000.0};
    SourceMode mode_{SourceMode::Sample};
    double ringSeconds_{0.0};
    CloudParams requested_{}; // as last given to setParams, for setSourceMode to re-sanitize
    CloudParams params_{};    // sanitized, independent of the sample rate

    // Derived from params_ and sampleRate_ by updateDerived().
    double densityHz_{0.0}; // <= 0 when off
    double period_{0.0};    // P = sampleRate_ / densityHz_ (at Pitch, the clamped f0), when on
    double durationSamples_{0.0};
    // log2(grainDurationSec), for the size spread; log2 0.005 when the duration isn't positive.
    double durationLog2_{0.0};
    double pitchRatio_{1.0}; // exp2Det(pitchSemitones / 12)

    double nextOnset_{0.0}; // t(nextK_), in output samples since reset()
    uint64_t nextK_{0};

    // The last emitted grain, k − 1 = nextK_ − 1, for ruling M1-h.
    double lastOnset_{0.0}; // t(k − 1)
    double lastE_{0.0};     // E(k − 1), or at Pitch its bipolar draw
    bool hasLast_{false};   // grain k − 1 was emitted since reset() or since the stream turned on
    bool rescalePending_{false}; // the period or distribution changed since the last schedule()

    // The held keys, ascending: the first keyCount_ entries (spec §6.1).
    std::array<uint8_t, 128> keys_{};
    uint32_t keyCount_{0};
    bool noteGate_{false}; // no grain while no key is held (the instrument)

    // Clock Pitch's refresh (SB4): the last refresh grain's index and onset, and whether one is
    // valid since reset(), a Clock change or restartRefresh().
    uint64_t refreshK_{0};
    double refreshOnset_{0.0};
    double refreshPeriod_{0.0}; // sampleRate_ / refreshHz, at Pitch
    bool refreshValid_{false};
    // The held set went from empty to non-empty, or forceNextOnset() was called, since the
    // last run.
    bool onsetForced_{false};
};
} // namespace gg

#endif // GG_GRAIN_CLOUDSCHEDULER_H
