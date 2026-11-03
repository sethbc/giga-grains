// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_TESTS_SWEEP_SWEEPDRIVER_H
#define GG_TESTS_SWEEP_SWEEPDRIVER_H

// The sweep harness's driver (M2 engine spec §6.11; INTENT §19's fuzz and property tests): a
// Processor in either product at 48 kHz, fed as a host feeds it, in calls whose sizes a seeded
// gg::rng draws from {0, 1, 2 … 4096} (at least one of every 32 draws 0 or 1), with parameter
// events, notes, MIDI controls and transport changes at absolute samples. It keeps the whole
// render, labels every stretch of it (the parameter, the step, the value), and checks it: finite
// (the output, and the rings, peak strips, snapshots and grain queue the Processor exposes), the
// chain's peak bound, no runaway and the goldens' click score, outside the stretches it exempts.
// Each sample outside them is scored as the goldens score the whole render, against its ±5 ms
// of the render whatever is exempt there: scoring a stretch as a render of its own would judge
// its first and last samples against half a window, so a grain's release cut by an
// exemption's end read as a click (the instrument extremes' corner 13 under the sources
// branch's slot layout: 36.6 for samples the whole render scores 2.8).
//
// The click check looks away from (exempt()):
// - the input's fade-in, the first 10 ms and the score's ±5 ms beyond;
// - the pre-delay and kReverbSizeExempt after every Reverb Size move: carry-over row 217,
//   accepted (S8). A move is exempt only when the Size in effect (its base or its modulation)
//   changes;
// - kLimiterDrive (the director's D5b, option 1, 2026-10-31; carry-over row 378): the limiter's
//   laws stand (zero lookahead, no attack), so a ceiling far under the material is a law
//   consequence, not a defect. The check looks away over every call in which Peak's gain fell
//   below -6 dB, or Soft's input passed twice the ceiling, and 10 ms after it; and every sample
//   of a limiter mode fade toward Peak (Processor::peakFadeSpan), and 10 ms after, whatever the
//   calls (the director's K-2, 2026-11-02; INTENT §23 #79): entering Peak mid-sound clicks.
// - kDelayGlideHold (D-glide, option 1, 2026-10-31; row 379): the delay's Time glide is
//   tape-like by design, and inside a Feedback 100% loop it re-reads each pass a fifth higher.
//   The check looks away over the samples where Delay Amount is above 0, the delay's Feedback is
//   at least 0.95 and its read time is still gliding.
//
// Not exempt, and so scored like any other stretch:
// - the reverb's onset: the Processor fades the reverb's input in over 10 ms at an idle-to-active
//   chunk (carry-over row 302, closed), so Reverb Amount rising from exactly 0 under a sounding
//   input doesn't click;
// - the morph law's first normalization (r_L·b_L / Σ r_K·b_K), which packed a crossfade into a
//   few samples of a layer's ramp off the pad's center (carry-over row 301, closed): the law now
//   moves at the ramps' pace.
//
// Each case pins a floor under the share of the render the click check scores
// (Checks::minScored), so an exemption that widens past what the case needs fails there.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/LayersFixture.h"
#include "gg/core/ModSources.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"
#include "gg/core/Rng.h"
#include "gg/engine/Engine.h"
#include "sst/basic-blocks/params/ParamMetadata.h"

namespace gg::test
{
/// What plays while the driver renders.
enum class Material
{
    /// A stereo 220 Hz sine at amplitude 0.5 into the input (the FX's dry side and every layer's
    /// ring), faded in over its first 10 ms with a raised cosine, as gg-render fades an FX input
    /// (ruling M1-v). The input runs to the render's end, so its only edge inside the output is
    /// that first one.
    SineLive,
    /// No input; key 60 held (sent again by play() and sweep() whenever nothing is held) over the
    /// 1 s 220 Hz sine sample every layer reads when its Source is Sample, in either product.
    NoteOverSine,
};

/// The harness's seeded draws are keyed by a parameter's CLAP ID, never by its slot index: the IDs
/// are append-only, so a parameter added later gets draws of its own and moves none of the
/// existing ones (sub-project 2 appends 20).
namespace sweepdraw
{
/// A draw in [0, 1) for `stream` (a corner, say) and the parameter `id`.
inline double byId(uint64_t seed, uint64_t stream, uint32_t id)
{
    return gg::test::layers::uniform(seed, (stream << 32) | id);
}

/// Burst event `event`'s parameter, chosen from `ids` by highest random weight: each ID's key is
/// a 64-bit hash of (seed, event, ID), and the ID with the largest key wins (the lower ID on a
/// tie). A key depends on the ID alone, so appending IDs to the list changes the pick of an event
/// only when an appended ID's key beats the old winner's: every other event keeps its parameter.
/// Returns an index into `ids`.
inline std::size_t pickById(uint64_t seed, uint64_t event, std::span<const uint32_t> ids)
{
    std::size_t best = 0;
    uint64_t bestKey = 0;
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        const uint64_t key =
            gg::rng::hash({seed, 1, 0, gg::rng::Field::SignalNoise, (event << 32) | ids[i]});
        if (i == 0 || key > bestKey || (key == bestKey && ids[i] < ids[best]))
        {
            best = i;
            bestKey = key;
        }
    }
    return best;
}

/// Burst event `event`'s field `field` (0 to 7), a draw in [0, 1) that depends on neither the
/// event's parameter nor the list.
inline double eventField(uint64_t seed, uint64_t event, uint64_t field)
{
    return gg::test::layers::uniform(seed, (uint64_t{1} << 48) + event * 8 + field);
}

/// Every slot's CLAP ID, in slot order.
inline std::vector<uint32_t> slotIds()
{
    std::vector<uint32_t> ids;
    for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        ids.push_back(gg::core::paramId(static_cast<gg::core::ParamSlot>(s)));
    }
    return ids;
}
} // namespace sweepdraw

struct Checks
{
    /// The largest |sample| allowed: the chain's bound, 2 + the input's peak, or 3 + it with the
    /// delay engaged (INTENT §14).
    double peakBound;
    /// The goldens' click limit (metricsVersion 2).
    double clickLimit = 15;
    /// Skips the click check altogether.
    bool clicksExempt = false;
    /// The least share of the render the click check must score (Verdict::scoredFraction): each
    /// case pins one a little under what it measures, so an exemption that widens past what the
    /// case needs fails here instead of blinding the check.
    double minScored = 0.0;
    /// The most the last second's RMS may stand above the loudest earlier one (Verdict::runawayDb).
    static constexpr double kRunawayDb = 6.0;
};

struct Verdict
{
    bool finite;
    float peak;
    /// computeMetrics' click score of the whole render, but only its samples in the stretches the
    /// check doesn't exempt (each against the whole render's ±5 ms around it): their largest.
    double clickScore;
    /// The share of the render's samples in the stretches the click score covers, 0 to 1: what
    /// the exemptions (and the stretches too short to score) leave.
    double scoredFraction;
    /// The last second's RMS minus the loudest earlier second's (from the second second on,
    /// once the input is steady), in dB; 0 when the render is shorter than 3 s.
    double runawayDb;
    /// Every failed check, each with where it fell: the stretch's label (the parameter, the step
    /// and its value) and the sample. Empty when they all pass.
    std::string where;
};

/// What brought an exempt stretch (SweepDriver::exemptShare): the header comment's list.
enum class Exemption
{
    FadeIn,         ///< the input's fade-in
    ReverbSize,     ///< row 217's Reverb Size moves
    LimiterDrive,   ///< kLimiterDrive (D5b)
    DelayGlideHold, ///< kDelayGlideHold (D-glide)
};

class SweepDriver
{
  public:
    static constexpr double kRate = 48000.0;
    static constexpr uint32_t kMaxBlock = 4096;
    /// The input's fade-in, 10 ms (gg-render's kInputFadeSeconds).
    static constexpr int64_t kFade = 480;
    static constexpr double kInputHz = 220.0;
    static constexpr float kInputAmp = 0.5f;
    static constexpr int16_t kNoteKey = 60;
    /// How long after a Reverb Size move, beyond the pre-delay then in effect, the click check
    /// looks away (row 217): Reverb 2's lines jump to their new lengths at the next chunk, and the
    /// jump echoes through them, scoring above the limit for up to 0.52 s at infinite Decay over
    /// this sine, and up to 0.36 s past a 0.3 s pre-delay in a burst.
    static constexpr int64_t kReverbSizeExempt = 28800;
    /// kLimiterDrive: Peak's gain below this (-6 dB) over a call, or Soft's input past twice the
    /// ceiling (Processor::limiterDrive reads that one), exempts the call, and so does every
    /// sample of a mode fade into Peak (Processor::peakFadeSpan; K-2, INTENT §23 #79)...
    static constexpr float kPeakDriveGain = 0.5011872336f; // 10^(-6/20)
    /// ...and this many samples after it (10 ms).
    static constexpr int64_t kLimiterDriveTail = 480;

    /// `fixedBlock` > 0 renders every call at that size instead of the drawn ones.
    SweepDriver(gg::core::Mode mode, uint64_t seed, uint32_t fixedBlock = 0);

    gg::core::Processor &processor() noexcept { return processor_; }
    gg::core::Mode mode() const noexcept { return mode_; }
    /// The absolute sample the next call starts at.
    int64_t clock() const noexcept { return clock_; }
    /// The input's peak so far: kInputAmp once a SineLive stretch has played.
    float inputPeak() const noexcept { return inputPeak_; }
    /// Whether the global delay was engaged at any point of the render so far (INTENT §14's
    /// D-bound). Engaged means Delay Amount above 0 in force, as the effective value at the end
    /// of a call or as a value an event carried into one, and it stays set: the check bounds the
    /// whole render's peak, and the delay's return is in the render from the first engaged
    /// sample, so a whole-render flag is exact for that check where a per-window one would need
    /// the ramp's and the loop's edges. Amount 0 throughout never sets it.
    bool delayEngaged() const noexcept { return delayEngaged_; }
    /// Whether the click check looks away from `sample`: it lies in a stretch an exemption
    /// brought (the narrowness tests' way to see an exemption's edges).
    bool exemptAt(int64_t sample) const
    {
        return std::any_of(exempt_.begin(), exempt_.end(),
                           [&](const Stretch &s) { return s.from <= sample && sample < s.to; });
    }
    /// The share of the render so far, 0 to 1, that exemption `why` covers on its own: the union
    /// of its stretches inside the render over the render's length. Shares overlap, so they can
    /// sum past what the check leaves out (1 − Verdict::scoredFraction, less the short stretches
    /// it drops between exempt ones). Instrumentation for a case's pinned floor.
    double exemptShare(Exemption why) const;
    /// The render so far, channel by channel (a planted fault's way in).
    std::array<std::vector<float>, 2> &output() noexcept { return out_; }

    static const sst::basic_blocks::params::ParamMetaData &meta(gg::core::ParamSlot slot,
                                                                gg::core::Mode mode)
    {
        return gg::core::paramMeta(slot, mode);
    }
    const sst::basic_blocks::params::ParamMetaData &meta(gg::core::ParamSlot slot) const
    {
        return meta(slot, mode_);
    }

    /// Takes the snapshot and drains the grain queue as every call does (a planted fault's way
    /// in: the Processor's own publishing supersedes what a test pushes before a call).
    void inspectExposed() { inspect(); }

    /// Renders `seconds` of `m`, applying what is queued.
    void play(double seconds, Material m)
    {
        material_ = m;
        holdNote();
        renderTo(clock_ + framesOf(seconds));
    }

    /// A base-value event at the next call's first sample.
    void set(gg::core::ParamSlot slot, double value)
    {
        queue(clock_, slot, false, value);
        label("set " + meta(slot).name + " = " + num(value));
    }
    /// A modulation-offset event at the next call's first sample.
    void setMod(gg::core::ParamSlot slot, double value)
    {
        queue(clock_, slot, true, value);
        label("set modulation of " + meta(slot).name + " = " + num(value));
    }
    /// computeMetrics' click score of the whole render, over every sample (the check's scoring
    /// with nothing exempt).
    double wholeRenderClickScore() const
    {
        return clickScore(clickTerms(), 0, static_cast<int64_t>(out_[0].size())).first;
    }
    /// A base-value event at its default, and a modulation event of 0, for every parameter at the
    /// next call's first sample.
    void setDefaults();

    /// `steps` base-value events from `lo` to `hi` (step k at lo + (hi − lo)·k/(steps − 1)), at
    /// the starts of `steps` equal stretches of `seconds`, rendered with the material last
    /// played.
    void sweep(gg::core::ParamSlot slot, double lo, double hi, int steps, double seconds);
    /// sweep() over the parameter's CLAP range and a quarter of it beyond each end.
    void sweepWide(gg::core::ParamSlot slot, int steps, double seconds)
    {
        const auto &m = meta(slot);
        const double q = (static_cast<double>(m.maxVal) - m.minVal) / 4.0;
        sweep(slot, m.minVal - q, m.maxVal + q, steps, seconds);
    }

    /// A note event at the next call's first sample.
    void note(gg::core::NoteEventType type, int16_t key, float velocity = 1.0f,
              int16_t channel = 0);

    /// Labels the render from the clock on (the stretch a failure names), until the next label.
    void mark(std::string what) { label(std::move(what)); }

    /// One process() call of the next drawn size n holding `events` seeded random parameter
    /// events (random slots; values anywhere in range or, one in four, modulation offsets of up
    /// to a quarter of it; offsets in [0, n]), 32 note events (on, off and all-off, any channel
    /// and key, -1 included), 32 MIDI controls and, before the call, a random transport.
    void burst(int events, uint64_t seed);

    /// The whole render's verdict against `c` (the comment at the top of the file).
    Verdict check(const Checks &c) const;

  private:
    struct Queued
    {
        int64_t time;
        gg::core::ParamEvent event; // its offset is set per call
    };
    /// A stretch [from, to) the click check exempts.
    struct Stretch
    {
        int64_t from;
        int64_t to;
        Exemption why;
    };
    /// The shortest stretch the click check scores: four times the score's ±5 ms.
    static constexpr int64_t kMinStretch = 4 * 240;

    static int64_t framesOf(double seconds)
    {
        return static_cast<int64_t>(std::llround(seconds * kRate));
    }
    static double finiteOr0(float x) { return std::isfinite(x) ? static_cast<double>(x) : 0.0; }
    static std::string num(double v)
    {
        std::ostringstream s;
        s << v;
        return s.str();
    }
    /// −1 (any) or 0 .. count − 1, from u in [0, 1).
    static int16_t anyOr(double u, int count)
    {
        return static_cast<int16_t>(static_cast<int>(u * (count + 1)) - 1);
    }

    void label(std::string what) { labels_.emplace_back(clock_, std::move(what)); }
    std::string labelAt(int64_t sample) const;

    void queue(int64_t time, gg::core::ParamSlot slot, bool isMod, double value)
    {
        events_.push_back({time, {0, slot, isMod, value}});
        noteExempt(time, slot, isMod, value);
    }
    /// The exemption an event at `time` brings: a Reverb Size event that changes the Size in
    /// force (events must arrive in the order they apply).
    void noteExempt(int64_t time, gg::core::ParamSlot slot, bool isMod, double value);

    uint32_t nextBlock();

    /// With NoteOverSine, holds kNoteKey from the clock if nothing is held.
    void holdNote()
    {
        if (material_ == Material::NoteOverSine && processor_.heldKeys().empty())
        {
            notes_.push_back({clock_, gg::core::NoteEventType::On, 0, kNoteKey, 1.0f});
        }
    }

    /// Renders from the clock to `to` in drawn calls, each with the queued events and notes that
    /// fall in it (a 0-frame call takes those at its sample).
    void renderTo(int64_t to);

    /// One process() call of n frames at the clock.
    void call(uint32_t n, std::span<const gg::core::ParamEvent> events,
              std::span<const gg::core::NoteEvent> notes,
              std::span<const gg::core::ControlEvent> controls);

    /// Takes the snapshot and drains the grain queue, noting the first non-finite value.
    void inspect();
    void exposedBad(const char *what)
    {
        if (firstBadExposed_ < 0)
        {
            firstBadExposed_ = clock_;
            badExposedWhat_ = what;
        }
    }

    /// The mono mix's RMS over [from, from + n), in dB, floored at −200.
    double rmsDb(int64_t from, int64_t n) const;

    /// [0, frames) less every exempt stretch, and less the stretches left between two that are
    /// shorter than kMinStretch.
    std::vector<std::pair<int64_t, int64_t>> unexempt(int64_t frames) const;

    /// The goldens' click score's terms over the whole render (Metrics.h; computeMetrics scores
    /// the mono mix so): d[n] = |m[n] − 2m[n − 1] + m[n − 2]| from n = 2, and prefix[n] the sum of
    /// d[j]² for j < n.
    struct ClickTerms
    {
        std::vector<double> d;
        std::vector<double> prefix;
    };
    ClickTerms clickTerms() const;

    /// The click score over the samples in [from, to), and the sample where it peaks: the most of
    /// d[n] / max(L[n], 1e-6) over d[n] > 1e-3, where L[n] is the RMS of d over n ± 5 ms of the
    /// whole render less n ± 2 (computeMetrics' definition, Metrics.h). Over [0, frames) it is
    /// computeMetrics' score of the whole render.
    std::pair<double, int64_t> clickScore(const ClickTerms &t, int64_t from, int64_t to) const;

    gg::core::Mode mode_;
    uint64_t seed_;
    uint32_t fixedBlock_;
    gg::core::Processor processor_;
    Material material_{Material::SineLive};
    int64_t clock_{0};
    uint64_t draws_{0};
    uint32_t sinceTiny_{0};
    float inputPeak_{0.0f};
    bool delayEngaged_{false};
    // The Reverb Size in force as queued (base, modulation): a move is exempt only if it changes
    // it.
    double sizeBase_{0.0};
    double sizeMod_{0.0};
    // The Processor's Reverb Size base at the end of the last call (row 309): a different value
    // before the next one was set on the Processor directly, not queued.
    float seenSizeBase_{0.0f};
    std::array<std::vector<float>, 2> out_;
    std::array<std::vector<float>, 2> in_;
    std::vector<Queued> events_;
    std::vector<gg::core::ParamEvent> callEvents_;
    std::vector<gg::test::layers::TimedNote> notes_;
    std::vector<std::pair<int64_t, std::string>> labels_;
    std::vector<Stretch> exempt_;
    std::vector<int64_t> sizeMoves_; // Reverb Size moves' boundaries, until applied
    int64_t firstBadExposed_{-1};
    const char *badExposedWhat_{""};
};

/// Checks the driver's whole render against `c` and requires every check to pass, naming where
/// each failed one fell.
inline void requireClean(const SweepDriver &d, const Checks &c)
{
    const Verdict v = d.check(c);
    INFO("peak " << v.peak << ", click score " << v.clickScore << ", scored " << v.scoredFraction
                 << ", runaway " << v.runawayDb << " dB, over " << d.clock() << " samples");
    INFO(v.where);
    CHECK(v.finite);
    CHECK(v.peak <= c.peakBound);
    CHECK(v.runawayDb <= Checks::kRunawayDb);
    if (!c.clicksExempt)
    {
        CHECK(v.clickScore <= c.clickLimit);
        CHECK(v.scoredFraction >= c.minScored);
    }
}

/// The chain's bound for the driver's render, and the click check's pinned floor on the share it
/// scores. The bound is 2 + the input's peak, or 3 + it once the delay was engaged (INTENT §14,
/// the director's D-bound, 2026-10-31): the delay's return, capped near 1, adds to the wet signal.
inline Checks chainBound(const SweepDriver &d, double minScored)
{
    Checks c{(d.delayEngaged() ? 3.0 : 2.0) + d.inputPeak()};
    c.minScored = minScored;
    return c;
}
} // namespace gg::test

#endif // GG_TESTS_SWEEP_SWEEPDRIVER_H
