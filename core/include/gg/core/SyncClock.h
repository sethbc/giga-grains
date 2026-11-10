// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_SYNCCLOCK_H
#define GG_CORE_SYNCCLOCK_H

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "gg/core/FpContract.h"
#include "gg/core/Transport.h"

namespace gg::core
{
/// Which grid line `SyncClock::nextLineSample` looks for: every beat, or every bar.
enum class SyncLine : uint8_t
{
    Beat,
    Bar,
};

/// The sync clock (M3 sub-project 7's spec R1, R2, RB1 to RB5, §4.2): a closed-form beat line,
/// `beatAt(t) = beatA + (t - a) × k` with k = T / (60 × sampleRate) beats per sample, evaluated at
/// any sample and never accumulated, so every synced value it gives is the same at any host block
/// size.
///
/// Each host transport is an observation (`observe`, latest wins), resolved at the first grid
/// boundary b at or after its sample s (`resolveAt`). The line keeps its anchor while a playing
/// host agrees with it within one 32-sample cell of beats at the same tempo, bit for bit, and is
/// relocked or rebased only at a discontinuity, each incrementing `epoch`:
/// - a jump (a loop, a locate, drift past a cell): relock, `a = b`, `beatA = h + (b - s) × kHost`;
/// - a tempo change at an agreeing position: rebase, `beatA = beatAt(b)` with the old k, `a = b`,
///   then the new tempo and k, so the beat is continuous and the slope bends;
/// - stopped to playing (or an internal clock meeting a playing host): always a relock;
/// - playing to stopped, or the timeline lost: a rebase at b, and the line runs on (R2);
/// - while stopped or without a timeline, only the tempo (a rebase) and the meter change;
/// - a meter change: an epoch, with no position change.
/// The first observation with a timeline after a reset takes the host's position whatever its play
/// state, so a stopped host seeds the line it runs on from. Without a timeline (`hasBeats` false,
/// or a non-finite position) the clock stays internal, at the host's tempo.
///
/// Header-only and real-time safe: every member is noexcept, nothing allocates, and the arithmetic
/// is double under GG_FP_CONTRACT_OFF with no libm call but std::floor and std::fabs. Its state is
/// never saved.
class SyncClock
{
  public:
    explicit SyncClock(double sampleRate) noexcept : sampleRate_(sampleRate) { reset(); }

    /// RB4: beat 0 at sample 0, 120 BPM, internal, not locked, epoch 0, 4/4 with no offset, nothing
    /// pending, and the next observation with a timeline seeds the position.
    void reset() noexcept
    {
        a_ = 0;
        beatA_ = 0.0;
        tempo_ = kDefaultTempo;
        k_ = beatsPerSampleAt(kDefaultTempo);
        locked_ = false;
        seeded_ = false;
        epoch_ = 0;
        barBeats_ = 4.0;
        barOffset_ = 0.0;
        pending_ = false;
        host_ = Transport{};
        hostSample_ = 0;
    }

    /// Takes the host's transport as it stood at sample s (a call's first sample, absolute). It
    /// replaces any observation still pending.
    void observe(const Transport &host, int64_t s) noexcept
    {
        host_ = host;
        hostSample_ = s;
        pending_ = true;
    }

    /// Whether an observation waits for its boundary.
    bool pending() const noexcept { return pending_; }

    /// Whether resolving the pending observation now would increment the epoch.
    bool pendingChangesLine() const noexcept { return pending_ && plan().changes; }

    /// Resolves the pending observation at boundary b, if there is one and its sample is at or
    /// before b.
    void resolveAt(int64_t b) noexcept
    {
        GG_FP_CONTRACT_OFF
        if (!pending_ || hostSample_ > b)
            return;
        const Plan p = plan();
        pending_ = false;
        if (!p.changes)
            return;
        switch (p.kind)
        {
        case Plan::Kind::Relock:
            setTempo(p.tempo);
            a_ = b;
            beatA_ = host_.beatPosition + static_cast<double>(b - hostSample_) * k_;
            break;
        case Plan::Kind::Rebase:
            beatA_ = beatAt(b);
            a_ = b;
            setTempo(p.tempo);
            break;
        case Plan::Kind::Keep:
            break;
        }
        locked_ = p.locked;
        seeded_ = seeded_ || p.seeds;
        barBeats_ = p.barBeats;
        barOffset_ = p.barOffset;
        ++epoch_;
    }

    /// The beat at sample t, any t (backward from the anchor too).
    double beatAt(int64_t t) const noexcept
    {
        GG_FP_CONTRACT_OFF
        return beatA_ + static_cast<double>(t - a_) * k_;
    }

    /// The sample of the first beat or bar line at or after `from`: the line's exact sample on the
    /// current line, rounded half away from zero (llround's rule).
    int64_t nextLineSample(SyncLine line, int64_t from) const noexcept
    {
        GG_FP_CONTRACT_OFF
        const double period = line == SyncLine::Bar ? barBeats_ : 1.0;
        const double offset = line == SyncLine::Bar ? barOffset_ : 0.0;
        const double n0 = std::floor((beatAt(from) - offset) / period);
        // The line at or before `from` and the next two cover any rounding of n0; one before them
        // covers a line just behind `from` that rounds onto it.
        for (int i = -1; i <= 2; ++i)
        {
            const double lineBeat = offset + (n0 + static_cast<double>(i)) * period;
            const int64_t t = a_ + roundHalfAway((lineBeat - beatA_) / k_);
            if (t >= from)
                return t;
        }
        return from;
    }

    double tempo() const noexcept { return tempo_; }
    /// k = tempo / (60 × sampleRate).
    double beatsPerSample() const noexcept { return k_; }
    /// The bar's length in beats (quarter notes): numerator × 4 / denominator.
    double barBeats() const noexcept { return barBeats_; }
    /// The beat of the first bar line at or after beat 0, mod barBeats: bar lines are the beats
    /// barOffset + m × barBeats.
    double barOffset() const noexcept { return barOffset_; }
    /// Anchored to a playing host (Ring Quantize's condition, R8).
    bool locked() const noexcept { return locked_; }
    /// Incremented by every relock, rebase and meter change.
    uint32_t epoch() const noexcept { return epoch_; }
    int64_t anchorSample() const noexcept { return a_; }
    double anchorBeat() const noexcept { return beatA_; }

  private:
    static constexpr double kDefaultTempo = 120.0;
    static constexpr double kMinTempo = 1.0;
    static constexpr double kMaxTempo = 1000.0;
    /// RB3: a playing host within one 32-sample cell of beats is steady.
    static constexpr double kSteadySamples = 32.0;

    /// What resolving the pending observation would do.
    struct Plan
    {
        enum class Kind : uint8_t
        {
            Keep,
            Rebase,
            Relock,
        };
        Kind kind{Kind::Keep};
        double tempo{kDefaultTempo};
        bool locked{false};
        bool seeds{false};
        double barBeats{4.0};
        double barOffset{0.0};
        bool changes{false};
    };

    double beatsPerSampleAt(double tempo) const noexcept
    {
        GG_FP_CONTRACT_OFF
        return tempo / (60.0 * sampleRate_);
    }

    /// k is recomputed only when the tempo changes.
    void setTempo(double tempo) noexcept
    {
        if (tempo != tempo_)
        {
            tempo_ = tempo;
            k_ = beatsPerSampleAt(tempo);
        }
    }

    /// Non-finite or not positive keeps the previous tempo; otherwise clamped to [1, 1000] BPM.
    static double sanitizeTempo(double host, double previous) noexcept
    {
        if (!std::isfinite(host) || host <= 0.0)
            return previous;
        return std::clamp(host, kMinTempo, kMaxTempo);
    }

    /// llround's rule (half away from zero) with std::floor alone: x - floor(x) is exact.
    static int64_t roundHalfAway(double x) noexcept
    {
        GG_FP_CONTRACT_OFF
        const double f = std::floor(x);
        const double d = x - f;
        const bool up = d > 0.5 || (d == 0.5 && x > 0.0);
        return static_cast<int64_t>(up ? f + 1.0 : f);
    }

    Plan plan() const noexcept
    {
        GG_FP_CONTRACT_OFF
        Plan p;
        p.tempo = sanitizeTempo(host_.tempoBpm, tempo_);
        const bool tempoChanges = p.tempo != tempo_;

        // RB5: num × 4 / denom, 4/4 for a meter that isn't one; the offset is bar_start mod a bar.
        int num = host_.beatsPerBar;
        int denom = host_.beatUnit;
        const bool denomValid =
            denom == 1 || denom == 2 || denom == 4 || denom == 8 || denom == 16 || denom == 32;
        if (num < 1 || !denomValid)
        {
            num = 4;
            denom = 4;
        }
        p.barBeats = static_cast<double>(num) * 4.0 / static_cast<double>(denom);
        const double barStart = std::isfinite(host_.barStart) ? host_.barStart : 0.0;
        p.barOffset = barStart - p.barBeats * std::floor(barStart / p.barBeats);
        const bool meterChanges = p.barBeats != barBeats_ || p.barOffset != barOffset_;

        const bool timeline = host_.hasBeats && std::isfinite(host_.beatPosition);
        if (!timeline || !host_.playing)
        {
            if (timeline && !seeded_)
            {
                p.kind = Plan::Kind::Relock; // a stopped host's position seeds the line
                p.seeds = true;
            }
            else
                p.kind = (locked_ || tempoChanges) ? Plan::Kind::Rebase : Plan::Kind::Keep;
            p.locked = false;
        }
        else
        {
            if (!locked_)
                p.kind = Plan::Kind::Relock;
            else if (std::fabs(host_.beatPosition - beatAt(hostSample_)) > kSteadySamples * k_)
                p.kind = Plan::Kind::Relock;
            else
                p.kind = tempoChanges ? Plan::Kind::Rebase : Plan::Kind::Keep;
            p.seeds = true;
            p.locked = true;
        }
        p.changes = p.kind != Plan::Kind::Keep || meterChanges;
        return p;
    }

    double sampleRate_;
    int64_t a_{0};
    double beatA_{0.0};
    double tempo_{kDefaultTempo};
    double k_{0.0};
    bool locked_{false};
    bool seeded_{false};
    uint32_t epoch_{0};
    double barBeats_{4.0};
    double barOffset_{0.0};
    bool pending_{false};
    Transport host_{};
    int64_t hostSample_{0};
};
} // namespace gg::core

#endif // GG_CORE_SYNCCLOCK_H
