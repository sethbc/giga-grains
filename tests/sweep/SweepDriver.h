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
//
// The click check looks away from (exempt()):
// - the input's fade-in, the first 10 ms and the score's ±5 ms beyond;
// - the pre-delay and kReverbSizeExempt after every Reverb Size move: carry-over row 217,
//   accepted (S8);
// - and, pending a decision, the click sources this harness found (the task 11 report),
//   each where the driver sees it happen:
//   1. kSampleEnd: a grain reading a sample is cut where it runs past the sample's end (its taps
//      read 0 there), whatever its window's value;
//   2. kReverbOnset: when the reverb goes from idle to active (Reverb Amount rising from exactly
//      0) under a sounding input, it starts from an empty room and hears its input cut in at the
//      first active chunk, and that step comes back through the pre-delay and the network;
//   The morph law's normalization (r_L·b_L / Σ r_K·b_K) also packs a crossfade into a few samples
//   of a layer's ramp when the pad is off its centre and the rendering layers' base weights are
//   nearly 0 (a layer switch with the pad in a corner; a pad move near an edge). Nothing here
//   exempts it: no committed case meets it, and the Layer On sweeps are scored.
//   Reverb Size moves are exempt only when the Size in effect (its base or its modulation)
//   changes.
//
// Each case pins a floor under the share of the render the click check scores
// (Checks::minScored), so an exemption that widens past what the case needs fails there.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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
#include "gg/metrics/AudioData.h"
#include "gg/metrics/Metrics.h"
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

struct Checks
{
    /// The largest |sample| allowed: the chain's bound, 2 + the input's peak (INTENT §14).
    double peakBound;
    /// The goldens' click limit (metricsVersion 2).
    double clickLimit = 15;
    /// Skips the click check altogether.
    bool clicksExempt = false;
    /// The least share of the render the click check must score (Verdict::scoredFraction): each
    /// case pins one a little under what it measures, so an exemption that widens past what the
    /// case needs fails here instead of blinding the check.
    double minScored = 0.0;
};

struct Verdict
{
    bool finite;
    float peak;
    /// computeMetrics' click score: the largest over the stretches the check doesn't exempt.
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

class SweepDriver
{
  public:
    static constexpr double kRate = 48000.0;
    static constexpr uint32_t kMaxBlock = 4096;
    /// The input's fade-in, 10 ms (gg-render's kInputFadeSeconds).
    static constexpr int64_t kFade = 480;
    static constexpr double kInputHz = 220.0;
    static constexpr float kInputAmp = 0.5f;
    /// LiveSource's mirrored guard frames after its ring (its kGuardFrames, private there): the
    /// live-ring check covers them.
    static constexpr int64_t kLiveGuardFrames = 15;
    static constexpr int16_t kNoteKey = 60;
    /// How long after a Reverb Size move, beyond the pre-delay then in effect, the click check
    /// looks away (row 217): Reverb 2's lines jump to their new lengths at the next chunk, and the
    /// jump echoes through them, scoring above the limit for up to 0.52 s at infinite Decay over
    /// this sine, and up to 0.36 s past a 0.3 s pre-delay in a burst.
    static constexpr int64_t kReverbSizeExempt = 28800;
    /// How long after the reverb goes active, beyond its pre-delay, the check looks away
    /// (kReverbOnset): the onset's echoes through the network, as long as a Size move's.
    static constexpr int64_t kReverbOnsetExempt = 28800;

    /// `fixedBlock` > 0 renders every call at that size instead of the drawn ones.
    SweepDriver(gg::core::Mode mode, uint64_t seed, uint32_t fixedBlock = 0)
        : mode_(mode), seed_(seed), fixedBlock_(fixedBlock),
          processor_(gg::core::ProcessorConfig{kRate, kMaxBlock, seed, mode})
    {
        const auto sample = gg::test::layers::sine(kInputHz);
        sampleFrames_ = sample->level(0).frames; // the fixture's length, not a copy of it
        for (uint8_t L = 0; L < gg::core::kLayers; ++L)
        {
            processor_.setSourceNow(L, sample);
        }
        processor_.setPublishing(true);
        processor_.setEventSink(&SweepDriver::onGrain, this);
        out_[0].reserve(static_cast<std::size_t>(kRate * 120));
        out_[1].reserve(static_cast<std::size_t>(kRate * 120));
        for (auto &c : in_)
        {
            c.resize(kMaxBlock);
        }
        events_.reserve(1 << 14);
        callEvents_.reserve(1 << 14);
        sampleEnds_.reserve(kSampleEndCapacity);
        exempt_.push_back({0, kFade + 256}); // the input's fade-in
        sizeBase_ = meta(gg::core::ParamSlot::ReverbSize).defaultVal;
        label("the start");
    }

    gg::core::Processor &processor() noexcept { return processor_; }
    gg::core::Mode mode() const noexcept { return mode_; }
    /// The absolute sample the next call starts at.
    int64_t clock() const noexcept { return clock_; }
    /// The input's peak so far: kInputAmp once a SineLive stretch has played.
    float inputPeak() const noexcept { return inputPeak_; }
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
    /// A base-value event at its default, and a modulation event of 0, for every parameter at the
    /// next call's first sample.
    void setDefaults()
    {
        for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
        {
            const auto slot = static_cast<gg::core::ParamSlot>(s);
            queue(clock_, slot, false, meta(slot).defaultVal);
            queue(clock_, slot, true, 0.0);
        }
        label("every parameter at its default, with no modulation");
    }

    /// `steps` base-value events from `lo` to `hi` (step k at lo + (hi − lo)·k/(steps − 1)), at
    /// the starts of `steps` equal stretches of `seconds`, rendered with the material last
    /// played.
    void sweep(gg::core::ParamSlot slot, double lo, double hi, int steps, double seconds)
    {
        holdNote();
        const int64_t total = framesOf(seconds);
        const int64_t start = clock_;
        for (int k = 0; k < steps; ++k)
        {
            const double v = steps > 1 ? lo + (hi - lo) * k / (steps - 1) : lo;
            queue(start + total * k / steps, slot, false, v);
            label(meta(slot).name + " step " + std::to_string(k + 1) + "/" + std::to_string(steps) +
                  " = " + num(v));
            renderTo(start + total * (k + 1) / steps);
        }
    }
    /// sweep() over the parameter's CLAP range and a quarter of it beyond each end.
    void sweepWide(gg::core::ParamSlot slot, int steps, double seconds)
    {
        const auto &m = meta(slot);
        const double q = (static_cast<double>(m.maxVal) - m.minVal) / 4.0;
        sweep(slot, m.minVal - q, m.maxVal + q, steps, seconds);
    }

    /// A note event at the next call's first sample.
    void note(gg::core::NoteEventType type, int16_t key, float velocity = 1.0f, int16_t channel = 0)
    {
        notes_.push_back({clock_, type, channel, key, velocity});
        label(labels_.back().second + ", then " +
              (type == gg::core::NoteEventType::On    ? "note on "
               : type == gg::core::NoteEventType::Off ? "note off "
                                                      : "all notes off ") +
              std::to_string(key));
    }

    /// Labels the render from the clock on (the stretch a failure names), until the next label.
    void mark(std::string what) { label(std::move(what)); }

    /// One process() call of the next drawn size n holding `events` seeded random parameter
    /// events (random slots; values anywhere in range or, one in four, modulation offsets of up
    /// to a quarter of it; offsets in [0, n]), 32 note events (on, off and all-off, any channel
    /// and key, -1 included), 32 MIDI controls and, before the call, a random transport.
    void burst(int events, uint64_t seed)
    {
        uint64_t k = 0;
        const auto u = [&] { return gg::test::layers::uniform(seed, k++); };
        const uint32_t n = nextBlock();
        const auto offset = [&] { return static_cast<uint32_t>(u() * (n + 1.0)) % (n + 1); };
        processor_.setTransport({.hasBeats = u() < 0.8,
                                 .beatPosition = u() * 64.0,
                                 .tempoBpm = 20.0 + u() * 280.0,
                                 .playing = u() < 0.7,
                                 .beatsPerBar = 1 + static_cast<int>(u() * 7.0)});
        std::vector<gg::core::ParamEvent> params;
        params.reserve(static_cast<std::size_t>(events));
        for (int i = 0; i < events; ++i)
        {
            const auto slot = static_cast<gg::core::ParamSlot>(
                static_cast<std::size_t>(u() * gg::core::kParamCount) % gg::core::kParamCount);
            const auto &m = meta(slot);
            const double span = static_cast<double>(m.maxVal) - m.minVal;
            const bool isMod = u() < 0.25;
            const double v = isMod ? (2.0 * u() - 1.0) * span / 4.0 : m.minVal + u() * span;
            params.push_back({offset(), slot, isMod, v});
        }
        std::vector<gg::core::NoteEvent> notes;
        for (int i = 0; i < 32; ++i)
        {
            const double t = u();
            const auto type = t < 0.6   ? gg::core::NoteEventType::On
                              : t < 0.9 ? gg::core::NoteEventType::Off
                                        : gg::core::NoteEventType::AllOff;
            notes.push_back(
                {offset(), type, anyOr(u(), 16), anyOr(u(), 128), static_cast<float>(u())});
        }
        std::vector<gg::core::ControlEvent> controls;
        for (int i = 0; i < 32; ++i)
        {
            controls.push_back({offset(), static_cast<gg::core::ControlKind>(u() * 3.0),
                                anyOr(u(), 16), static_cast<float>(u())});
        }
        const auto byOffset = [](const auto &a, const auto &b) { return a.offset < b.offset; };
        std::stable_sort(params.begin(), params.end(), byOffset);
        for (const auto &e : params)
        {
            noteExempt(clock_ + e.offset, e.slot, e.isModulation, e.value);
        }
        std::stable_sort(notes.begin(), notes.end(), byOffset);
        std::stable_sort(controls.begin(), controls.end(), byOffset);
        label("burst " + std::to_string(seed) + ", " + std::to_string(events) + " events in " +
              std::to_string(n) + " frames");
        call(n, params, notes, controls);
    }

    /// The whole render's verdict against `c` (the comment at the top of the file).
    Verdict check(const Checks &c) const
    {
        Verdict v{true, 0.0f, 0.0, 0.0, 0.0, {}};
        std::ostringstream where;
        const auto frames = static_cast<int64_t>(out_[0].size());

        // Finite: the output, then what the Processor exposes.
        int64_t firstBad = -1;
        int64_t peakAt = 0;
        for (int64_t i = 0; i < frames; ++i)
        {
            for (const auto &ch : out_)
            {
                const float x = ch[static_cast<std::size_t>(i)];
                if (!std::isfinite(x))
                {
                    firstBad = firstBad < 0 ? i : firstBad;
                }
                else if (std::abs(x) > v.peak)
                {
                    v.peak = std::abs(x);
                    peakAt = i;
                }
            }
        }
        if (firstBad >= 0)
        {
            v.finite = false;
            where << "non-finite output at sample " << firstBad << " (" << labelAt(firstBad)
                  << "); ";
        }
        if (firstBadExposed_ >= 0)
        {
            v.finite = false;
            where << "non-finite " << badExposedWhat_ << " after the call ending at sample "
                  << firstBadExposed_ << " (" << labelAt(firstBadExposed_ - 1) << "); ";
        }
        for (uint8_t L = 0; L < gg::core::kLayers; ++L)
        {
            // Every slot of the ring's storage, the mirrored ones included (LiveSource::view).
            const auto ring = processor_.engine(L).live().view();
            const std::size_t floats =
                2 * static_cast<std::size_t>(ring.capacity + kLiveGuardFrames);
            for (std::size_t i = 0; ring.slots != nullptr && i < floats; ++i)
            {
                if (!std::isfinite(ring.slots[i]))
                {
                    v.finite = false;
                    where << "non-finite live ring of layer " << int{L} << " at slot " << i / 2
                          << "; ";
                    break;
                }
            }
            const auto &peaks = processor_.livePeaks(L);
            for (int64_t f = 0; f < peaks.frames(); f += gg::core::LivePeaks::kBucket)
            {
                if (!std::isfinite(peaks.at(f)))
                {
                    v.finite = false;
                    where << "non-finite peak strip of layer " << int{L} << " at frame " << f
                          << "; ";
                    break;
                }
            }
        }
        if (!(v.peak <= c.peakBound))
        {
            where << "peak " << v.peak << " above " << c.peakBound << " at sample " << peakAt
                  << " (" << labelAt(peakAt) << "); ";
        }

        // Runaway, on the mono mix: the last second against every full second before it, from
        // the second second on.
        const int64_t second = framesOf(1.0);
        if (frames >= 3 * second)
        {
            const int64_t last = frames - second;
            const double lastDb = rmsDb(last, second);
            double loudest = -200.0;
            int64_t loudestAt = second;
            for (int64_t s = second; s + second <= last; s += second)
            {
                const double db = rmsDb(s, second);
                if (db > loudest)
                {
                    loudest = db;
                    loudestAt = s;
                }
            }
            v.runawayDb = lastDb - loudest;
            if (!(v.runawayDb <= 6.0))
            {
                where << "runaway: the last second, from sample " << last << " (" << labelAt(last)
                      << "), at " << lastDb << " dB RMS, " << v.runawayDb
                      << " dB above the loudest earlier one, from sample " << loudestAt << " ("
                      << labelAt(loudestAt) << "); ";
            }
        }

        // The click score, over every stretch the check doesn't exempt.
        int64_t worstFrom = 0;
        int64_t worstTo = 0;
        int64_t scored = 0;
        for (const auto &[a, b] : unexempt(frames))
        {
            scored += b - a;
            const double s = clickScore(a, b);
            if (s > v.clickScore)
            {
                v.clickScore = s;
                worstFrom = a;
                worstTo = b;
            }
        }
        if (sampleEndsOverflowed_)
        {
            // The kSampleEnd stretches are incomplete, so the score can't be trusted.
            v.clickScore = std::numeric_limits<double>::infinity();
            where << "more than " << kSampleEndCapacity << " sample-end stretches; ";
        }
        else if (!c.clicksExempt && !(v.clickScore <= c.clickLimit))
        {
            const int64_t at = locateClick(worstFrom, worstTo);
            where << "click score " << v.clickScore << " above " << c.clickLimit << " at sample "
                  << at << " (" << labelAt(at) << "); ";
        }
        v.scoredFraction =
            frames > 0 ? static_cast<double>(scored) / static_cast<double>(frames) : 0.0;
        if (!c.clicksExempt && !(v.scoredFraction >= c.minScored))
        {
            where << "the click check scored " << v.scoredFraction << " of the render, below "
                  << c.minScored << "; ";
        }
        v.where = where.str();
        return v;
    }

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
    };
    static constexpr std::size_t kSampleEndCapacity = 1 << 18;
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
    std::string labelAt(int64_t sample) const
    {
        const auto *found = &labels_.front();
        for (const auto &l : labels_)
        {
            if (l.first <= sample)
            {
                found = &l;
            }
        }
        return found->second + ", from sample " + std::to_string(found->first);
    }

    void queue(int64_t time, gg::core::ParamSlot slot, bool isMod, double value)
    {
        events_.push_back({time, {0, slot, isMod, value}});
        noteExempt(time, slot, isMod, value);
    }
    /// The exemption an event at `time` brings: a Reverb Size event that changes the Size in
    /// force (events must arrive in the order they apply).
    void noteExempt(int64_t time, gg::core::ParamSlot slot, bool isMod, double value)
    {
        if (slot != gg::core::ParamSlot::ReverbSize)
        {
            return;
        }
        double &held = isMod ? sizeMod_ : sizeBase_;
        if (value != held)
        {
            held = value;
            sizeMoves_.push_back((time + 31) / 32 *
                                 32); // exempt once applied, with its pre-delay (call())
        }
    }

    /// The Processor's grain sink (on the audio thread, inside process(): no allocation past
    /// the reserve): a grain reading a sample that runs past the sample's last frame, less the
    /// interpolators' reach, is cut there (kSampleEnd).
    static void onGrain(void *ctx, uint32_t, const gg::GrainSpec &g)
    {
        auto *d = static_cast<SweepDriver *>(ctx);
        const double kLast = static_cast<double>(d->sampleFrames_ - 1) - 16.0;
        if (g.sourceSlot == gg::Engine::kLiveSlot || !(g.rate > 0.0) ||
            g.startPos + g.rate * g.durationSamples <= kLast)
        {
            return;
        }
        const auto end =
            g.onsetSample + static_cast<int64_t>(std::max(0.0, kLast - g.startPos) / g.rate);
        const Stretch s{end - 256, end + 512 + static_cast<int64_t>(32.0 / g.rate)};
        auto &v = d->sampleEnds_;
        if (!v.empty() && s.from <= v.back().to && s.to >= v.back().from)
        {
            v.back().to = std::max(v.back().to, s.to);
            v.back().from = std::min(v.back().from, s.from);
        }
        else if (v.size() < v.capacity())
        {
            v.push_back(s);
        }
        else
        {
            d->sampleEndsOverflowed_ = true;
        }
    }

    uint32_t nextBlock()
    {
        if (fixedBlock_ > 0)
        {
            return fixedBlock_;
        }
        const uint64_t h = gg::rng::hash({seed_, 0, 0, gg::rng::Field::SignalNoise, draws_++});
        auto n = static_cast<uint32_t>(gg::rng::uniform01(h) * (kMaxBlock + 1.0)) % (kMaxBlock + 1);
        if (n > 1 && sinceTiny_ == 31)
        {
            n = static_cast<uint32_t>(h & 1u); // the floor: one of every 32 draws is 0 or 1
        }
        sinceTiny_ = n <= 1 ? 0 : sinceTiny_ + 1;
        return n;
    }

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
    void renderTo(int64_t to)
    {
        std::stable_sort(events_.begin(), events_.end(),
                         [](const Queued &a, const Queued &b) { return a.time < b.time; });
        std::vector<gg::core::NoteEvent> callNotes;
        while (clock_ < to)
        {
            const auto n = static_cast<uint32_t>(std::min<int64_t>(nextBlock(), to - clock_));
            const int64_t limit = clock_ + std::max<int64_t>(n, 1);
            callEvents_.clear();
            std::size_t used = 0;
            for (; used < events_.size() && events_[used].time < limit; ++used)
            {
                gg::core::ParamEvent e = events_[used].event;
                e.offset = static_cast<uint32_t>(events_[used].time - clock_);
                callEvents_.push_back(e);
            }
            events_.erase(events_.begin(), events_.begin() + static_cast<std::ptrdiff_t>(used));
            callNotes.clear();
            std::size_t usedNotes = 0;
            for (; usedNotes < notes_.size() && notes_[usedNotes].time < limit; ++usedNotes)
            {
                const auto &t = notes_[usedNotes];
                callNotes.push_back(
                    {static_cast<uint32_t>(t.time - clock_), t.type, t.channel, t.key, t.velocity});
            }
            notes_.erase(notes_.begin(), notes_.begin() + static_cast<std::ptrdiff_t>(usedNotes));
            call(n, callEvents_, callNotes, {});
        }
    }

    /// One process() call of n frames at the clock.
    void call(uint32_t n, std::span<const gg::core::ParamEvent> events,
              std::span<const gg::core::NoteEvent> notes,
              std::span<const gg::core::ControlEvent> controls)
    {
        const auto at = static_cast<std::size_t>(clock_);
        out_[0].resize(at + n);
        out_[1].resize(at + n);
        const bool live = material_ == Material::SineLive;
        for (uint32_t i = 0; i < n; ++i)
        {
            const auto t = static_cast<double>(clock_ + i);
            double x = kInputAmp * std::sin(2.0 * std::numbers::pi * kInputHz * t / kRate);
            if (clock_ + i < kFade)
            {
                const double s = std::sin(0.5 * std::numbers::pi * t / kFade);
                x *= s * s;
            }
            in_[0][i] = static_cast<float>(x);
            in_[1][i] = static_cast<float>(x);
        }
        if (live && n > 0)
        {
            inputPeak_ = kInputAmp;
        }
        const float *in[2] = {in_[0].data(), in_[1].data()};
        float *out[2] = {out_[0].data() + at, out_[1].data() + at};
        processor_.process(live ? in : nullptr, live ? 2 : 0, out, 2, n, events, notes, controls);
        const int64_t callStart = clock_;
        clock_ += n;

        // The Reverb Size moves applied in this call (row 217), and kReverbOnset: the reverb went
        // active in it. Each looks away for the pre-delay in effect and the echoes after it.
        const int64_t predelay =
            framesOf(std::max(0.0f, processor_.effective(gg::core::ParamSlot::ReverbPredelay)));
        std::erase_if(sizeMoves_, [&](int64_t b) {
            if (b >= clock_)
            {
                return false; // not applied yet
            }
            exempt_.push_back({b - 64, b + predelay + kReverbSizeExempt});
            return true;
        });
        const bool active = processor_.reverbActive();
        if (active && !reverbWasActive_)
        {
            exempt_.push_back({callStart - 64, clock_ + predelay + kReverbOnsetExempt});
        }
        reverbWasActive_ = active;
        inspect();
    }

    /// Takes the snapshot and drains the grain queue, noting the first non-finite value.
    void inspect()
    {
        gg::core::UiSnapshot s;
        if (processor_.snapshot().take(s) &&
            !(std::isfinite(s.basePosition) && std::isfinite(s.baseSpray) &&
              std::isfinite(s.effectivePosition) && std::isfinite(s.effectiveSpray) &&
              std::isfinite(s.feedback)))
        {
            exposedBad("snapshot");
        }
        gg::core::GrainVis g;
        while (processor_.grainQueue().pop(g))
        {
            if (!(std::isfinite(g.startPos) && std::isfinite(g.rate) &&
                  std::isfinite(g.durationSamples) && std::isfinite(g.gain) &&
                  std::isfinite(g.pan)))
            {
                exposedBad("grain in the grain queue");
            }
        }
    }
    void exposedBad(const char *what)
    {
        if (firstBadExposed_ < 0)
        {
            firstBadExposed_ = clock_;
            badExposedWhat_ = what;
        }
    }

    /// The mono mix's RMS over [from, from + n), in dB, floored at −200.
    double rmsDb(int64_t from, int64_t n) const
    {
        double sum = 0.0;
        for (int64_t i = from; i < from + n; ++i)
        {
            const auto j = static_cast<std::size_t>(i);
            const double m = 0.5 * (finiteOr0(out_[0][j]) + finiteOr0(out_[1][j]));
            sum += m * m;
        }
        return std::max(-200.0, 10.0 * std::log10(sum / static_cast<double>(n)));
    }

    /// [0, frames) less every exempt stretch, and less the stretches left between two that are
    /// shorter than kMinStretch.
    std::vector<std::pair<int64_t, int64_t>> unexempt(int64_t frames) const
    {
        std::vector<std::pair<int64_t, int64_t>> ex;
        for (const auto &s : exempt_)
        {
            ex.emplace_back(s.from, s.to);
        }
        for (const auto &s : sampleEnds_)
        {
            ex.emplace_back(s.from, s.to);
        }
        std::sort(ex.begin(), ex.end());
        std::vector<std::pair<int64_t, int64_t>> out;
        int64_t at = 0;
        for (const auto &[a, b] : ex)
        {
            if (a > at && at < frames)
            {
                out.emplace_back(at, std::min(a, frames));
            }
            at = std::max(at, b);
        }
        if (at < frames)
        {
            out.emplace_back(at, frames);
        }
        // A stretch much shorter than the score's ±5 ms window would score its few samples
        // against too little context (none, for the shortest): leave it out with its neighbours.
        std::erase_if(out, [](const auto &s) { return s.second - s.first < kMinStretch; });
        return out;
    }

    double clickScore(int64_t from, int64_t to) const
    {
        gg::metrics::AudioData audio{kRate, {}};
        for (const auto &ch : out_)
        {
            audio.channels.emplace_back(ch.begin() + from, ch.begin() + to);
        }
        return gg::metrics::computeMetrics(audio).clickScore;
    }

    /// Where in [from, to) the click scores highest, for the report: the n maximizing
    /// d[n] / max(L[n], 1e-6) over d[n] > 1e-3, as computeMetrics defines the score (Metrics.h),
    /// recomputed here because the score alone doesn't say where.
    int64_t locateClick(int64_t from, int64_t to) const
    {
        const auto n = static_cast<std::size_t>(to - from);
        std::vector<double> d(n, 0.0);
        std::vector<double> prefix(n + 1, 0.0);
        const auto mono = [&](std::size_t k) {
            const auto j = static_cast<std::size_t>(from) + k;
            return 0.5 * (finiteOr0(out_[0][j]) + finiteOr0(out_[1][j]));
        };
        for (std::size_t k = 2; k < n; ++k)
        {
            d[k] = std::abs(mono(k) - 2.0 * mono(k - 1) + mono(k - 2));
            prefix[k + 1] = prefix[k] + d[k] * d[k];
        }
        constexpr std::size_t kWindow = 240; // ±5 ms at 48 kHz
        constexpr std::size_t kGuard = 2;
        double best = -1.0;
        std::size_t at = 0;
        for (std::size_t k = 2; k < n; ++k)
        {
            if (!(d[k] > 1e-3))
            {
                continue;
            }
            const std::size_t lo = k - std::min(k - 2, kWindow);
            const std::size_t hi = k + std::min(n - 1 - k, kWindow);
            const std::size_t glo = k - std::min(k - lo, kGuard);
            const std::size_t ghi = k + std::min(hi - k, kGuard);
            const std::size_t count = (hi - lo + 1) - (ghi - glo + 1);
            const double sum = (prefix[hi + 1] - prefix[lo]) - (prefix[ghi + 1] - prefix[glo]);
            const double level =
                count > 0 ? std::sqrt(std::max(sum, 0.0) / static_cast<double>(count)) : 0.0;
            const double score = d[k] / std::max(level, 1e-6);
            if (score > best)
            {
                best = score;
                at = k;
            }
        }
        return from + static_cast<int64_t>(at);
    }

    gg::core::Mode mode_;
    uint64_t seed_;
    uint32_t fixedBlock_;
    gg::core::Processor processor_;
    Material material_{Material::SineLive};
    int64_t clock_{0};
    uint64_t draws_{0};
    uint32_t sinceTiny_{0};
    float inputPeak_{0.0f};
    bool reverbWasActive_{false};
    int64_t sampleFrames_{0};
    // The Reverb Size in force as queued (base, modulation): a move is exempt only if it changes
    // it.
    double sizeBase_{0.0};
    double sizeMod_{0.0};
    std::array<std::vector<float>, 2> out_;
    std::array<std::vector<float>, 2> in_;
    std::vector<Queued> events_;
    std::vector<gg::core::ParamEvent> callEvents_;
    std::vector<gg::test::layers::TimedNote> notes_;
    std::vector<std::pair<int64_t, std::string>> labels_;
    std::vector<Stretch> exempt_;
    std::vector<int64_t> sizeMoves_;  // Reverb Size moves' boundaries, until applied
    std::vector<Stretch> sampleEnds_; // the grain sink's, reserved: kSampleEndCapacity
    bool sampleEndsOverflowed_{false};
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
    CHECK(v.runawayDb <= 6.0);
    if (!c.clicksExempt)
    {
        CHECK(v.clickScore <= c.clickLimit);
        CHECK(v.scoredFraction >= c.minScored);
    }
}

/// The chain's bound for the driver's render, 2 + the input's peak, and the click check's pinned
/// floor on the share it scores.
inline Checks chainBound(const SweepDriver &d, double minScored)
{
    Checks c{2.0 + d.inputPeak()};
    c.minScored = minScored;
    return c;
}
} // namespace gg::test

#endif // GG_TESTS_SWEEP_SWEEPDRIVER_H
