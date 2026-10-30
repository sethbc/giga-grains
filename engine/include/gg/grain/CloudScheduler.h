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
    /// behind the write head, [0, ring seconds].
    double position{0.5};
    /// Random spread of position. Sample mode: bipolar, as a fraction of the source (M0's
    /// positionJitter), [0, 1], or with sprayInSeconds, bipolar in seconds, [0, +inf). Live
    /// mode: seconds, further behind the head only, [0, ring seconds].
    double spray{0.0};
    double pitchSemitones{0.0}; ///< [-48, 48].
    double stereo{0.0};         ///< Random pan spread: 0 = every grain centred; [0, 1].
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
    /// applies (spec §6.3). u is keyed by (seed, layer, stream, PositionJitter, k) in all three.
    double positionDraw;
    /// Sample mode with CloudParams::sprayInSeconds: spray × bipolar(u), in seconds, which the
    /// source adds to positionDraw at its own rate (Engine::sampleGrain). Otherwise +0. With a
    /// Smear jitter j, Sample mode adds j × bipolar(u') seconds here, u' keyed by SmearJitter,
    /// and Live mode adds it to positionDraw instead (extremes spec §6.1).
    double sprayOffsetSec;
    /// The pitch ratio: exp2Det(clamp(pitchSemitones + (key − 60), −48, 48) / 12) with a key,
    /// exp2Det(pitchSemitones / 12) without. With a pitch spread s,
    /// exp2Det(clamp(pitchSemitones + (key − 60) + s × bipolar(u), −48, 48) / 12), the key term
    /// 0 without a key and u keyed by PitchSpread (M2 spec §6.6). A Smear detune c adds
    /// c × bipolar(u') after the spread's term, u' keyed by SmearDetune (extremes spec §6.1).
    double rate;
    /// Window length in output samples, at least 16: grainDurationSec × sampleRate, or with a
    /// size spread s, exp2Det(clamp(log2(grainDurationSec) + s × bipolar(u), log2 0.005,
    /// log2 2)) × sampleRate, u keyed by SizeSpread (M2 spec §6.6).
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
/// All members are real-time safe. Nothing in schedule() calls libm except the exact floor:
/// E(k) comes from logDet and the pitch ratios from exp2Det, so no libm value reaches a grain.
class CloudScheduler
{
  public:
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
    /// ring's length, which bounds both in Live mode; a non-finite or negative value means 0. The
    /// default is Sample. Neither the schedule nor any other draw changes.
    void setSourceMode(SourceMode mode, double ringSeconds) noexcept;

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
        // Covers the lambda below too: a lambda body takes the pragma of its enclosing scope.
        GG_FP_CONTRACT_OFF
        if (!(densityHz_ > 0.0) || (noteGate_ && keyCount_ == 0))
        {
            return; // off, or gated with no key held
        }
        const auto start = static_cast<double>(blockStart);
        const auto end = static_cast<double>(blockStart + static_cast<int64_t>(numFrames));
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
        while (nextOnset_ < end)
        {
            const double t = nextOnset_;
            const uint64_t k = nextK_;
            const double whole = std::floor(t);
            const auto key = [&](rng::Field field) {
                return rng::hash({seed, layer, stream, field, k});
            };
            const uint64_t jitter = key(rng::Field::PositionJitter);
            int16_t note = -1;
            double rate = pitchRatio_;
            if (keyCount_ > 0)
            {
                // floor(u × n) by truncation, as u × n >= 0; u × n < n, and the min() keeps the
                // index in range whatever the rounding.
                const double u = rng::uniform01(key(rng::Field::Note));
                const auto i = std::min(static_cast<uint32_t>(u * static_cast<double>(keyCount_)),
                                        keyCount_ - 1);
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
                    semis +=
                        params_.pitchSpreadSemitones * rng::bipolar(key(rng::Field::PitchSpread));
                }
                if (params_.smearDetuneSemitones != 0.0)
                {
                    semis +=
                        params_.smearDetuneSemitones * rng::bipolar(key(rng::Field::SmearDetune));
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
            if (params_.sizeSpreadOctaves != 0.0)
            {
                // M2 spec §6.6: the spread is in the log2 domain, within Size's range.
                const double log2Seconds =
                    std::clamp(durationLog2_ + params_.sizeSpreadOctaves *
                                                   rng::bipolar(key(rng::Field::SizeSpread)),
                               kMinSpreadLog2Seconds, kMaxSpreadLog2Seconds);
                durationSamples = std::clamp(exp2Det(log2Seconds) * sampleRate_,
                                             kMinDurationSamples, kMaxDurationSamples);
            }
            // Live mode: seconds behind the head, further back only. Sample mode: a fraction of
            // the source, with the spray folded in, or with sprayInSeconds the position alone and
            // the spray carried in seconds for the source to convert.
            const bool inSeconds = mode_ == SourceMode::Sample && params_.sprayInSeconds;
            double positionDraw = params_.position;
            if (mode_ == SourceMode::Live)
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
                // The extremes spec §6.1: seconds behind the head in Live mode (the admission
                // clamp then applies, as to the spray); in Sample mode carried in seconds, for
                // the source to convert at its own rate and clamp, as the spray in seconds is.
                const double smear =
                    params_.smearJitterSec * rng::bipolar(key(rng::Field::SmearJitter));
                if (mode_ == SourceMode::Live)
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
                params_.busSplit > 0.0 && rng::uniform01(key(rng::Field::Bus)) < params_.busSplit
                    ? 1
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
            };
            const double e = -logDet(1.0 - rng::uniform01(key(rng::Field::Onset)));
            lastOnset_ = t;
            lastE_ = e;
            hasLast_ = true;
            nextOnset_ = t + interval(e);
            ++nextK_;
            emit(g);
        }
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

    /// Δ for the unit-exponential draw e, with the current period and distribution.
    double interval(double e) const noexcept
    {
        GG_FP_CONTRACT_OFF
        const double d = params_.distribution;
        return std::max(period_ * ((1.0 - d) + d * e), kMinIntervalSamples);
    }

    double sampleRate_{48000.0};
    SourceMode mode_{SourceMode::Sample};
    double ringSeconds_{0.0};
    CloudParams requested_{}; // as last given to setParams, for setSourceMode to re-sanitize
    CloudParams params_{};    // sanitized, independent of the sample rate

    // Derived from params_ and sampleRate_ by updateDerived().
    double densityHz_{0.0}; // <= 0 when off
    double period_{0.0};    // P = sampleRate_ / densityHz_, when on
    double durationSamples_{0.0};
    // log2(grainDurationSec), for the size spread; log2 0.005 when the duration isn't positive.
    double durationLog2_{0.0};
    double pitchRatio_{1.0}; // exp2Det(pitchSemitones / 12)

    double nextOnset_{0.0}; // t(nextK_), in output samples since reset()
    uint64_t nextK_{0};

    // The last emitted grain, k − 1 = nextK_ − 1, for ruling M1-h.
    double lastOnset_{0.0}; // t(k − 1)
    double lastE_{0.0};     // E(k − 1)
    bool hasLast_{false};   // grain k − 1 was emitted since reset() or since the stream turned on
    bool rescalePending_{false}; // the period or distribution changed since the last schedule()

    // The held keys, ascending: the first keyCount_ entries (spec §6.1).
    std::array<uint8_t, 128> keys_{};
    uint32_t keyCount_{0};
    bool noteGate_{false}; // no grain while no key is held (the instrument)
    // The held set went from empty to non-empty, or forceNextOnset() was called, since the
    // last run.
    bool onsetForced_{false};
};
} // namespace gg

#endif // GG_GRAIN_CLOUDSCHEDULER_H
