// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The sweep harness's driver: the bodies (SweepDriver.h has the design and the exemptions).

#include "sweep/SweepDriver.h"

namespace gg::test
{
SweepDriver::SweepDriver(gg::core::Mode mode, uint64_t seed, uint32_t fixedBlock)
    : mode_(mode), seed_(seed), fixedBlock_(fixedBlock),
      processor_(gg::core::ProcessorConfig{kRate, kMaxBlock, seed, mode})
{
    const auto sample = gg::test::layers::sine(kInputHz);
    for (uint8_t L = 0; L < gg::core::kLayers; ++L)
    {
        processor_.setSourceNow(L, sample);
    }
    processor_.setPublishing(true);
    out_[0].reserve(static_cast<std::size_t>(kRate * 120));
    out_[1].reserve(static_cast<std::size_t>(kRate * 120));
    for (auto &c : in_)
    {
        c.resize(kMaxBlock);
    }
    events_.reserve(1 << 14);
    callEvents_.reserve(1 << 14);
    exempt_.push_back({0, kFade + 256, Exemption::FadeIn}); // the input's fade-in
    sizeBase_ = meta(gg::core::ParamSlot::ReverbSize).defaultVal;
    seenSizeBase_ = processor_.base(gg::core::ParamSlot::ReverbSize);
    label("the start");
}

double SweepDriver::exemptShare(Exemption why) const
{
    const auto frames = static_cast<int64_t>(out_[0].size());
    std::vector<std::pair<int64_t, int64_t>> ex;
    for (const auto &e : exempt_)
    {
        if (e.why == why)
        {
            ex.emplace_back(std::clamp<int64_t>(e.from, 0, frames),
                            std::clamp<int64_t>(e.to, 0, frames));
        }
    }
    std::sort(ex.begin(), ex.end());
    int64_t covered = 0;
    int64_t at = 0;
    for (const auto &[a, b] : ex)
    {
        const int64_t from = std::max(a, at);
        if (b > from)
        {
            covered += b - from;
        }
        at = std::max(at, b);
    }
    return frames > 0 ? static_cast<double>(covered) / static_cast<double>(frames) : 0.0;
}

void SweepDriver::setDefaults()
{
    for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        const auto slot = static_cast<gg::core::ParamSlot>(s);
        queue(clock_, slot, false, meta(slot).defaultVal);
        queue(clock_, slot, true, 0.0);
    }
    label("every parameter at its default, with no modulation");
}

void SweepDriver::sweep(gg::core::ParamSlot slot, double lo, double hi, int steps, double seconds)
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

void SweepDriver::note(gg::core::NoteEventType type, int16_t key, float velocity, int16_t channel)
{
    notes_.push_back({clock_, type, channel, key, velocity});
    label(labels_.back().second + ", then " +
          (type == gg::core::NoteEventType::On    ? "note on "
           : type == gg::core::NoteEventType::Off ? "note off "
                                                  : "all notes off ") +
          std::to_string(key));
}

void SweepDriver::burst(int events, uint64_t seed)
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
    // Each event's draws are keyed by the event index and, for the parameter, by CLAP ID
    // (sweepdraw), so appended parameters move no existing event's choice but their own.
    const std::vector<uint32_t> ids = sweepdraw::slotIds();
    std::vector<gg::core::ParamEvent> params;
    params.reserve(static_cast<std::size_t>(events));
    for (int i = 0; i < events; ++i)
    {
        const auto e = static_cast<uint64_t>(i);
        const auto f = [&](uint64_t field) { return sweepdraw::eventField(seed, e, field); };
        const auto slot = static_cast<gg::core::ParamSlot>(sweepdraw::pickById(seed, e, ids));
        const auto &m = meta(slot);
        const double span = static_cast<double>(m.maxVal) - m.minVal;
        const bool isMod = f(0) < 0.25;
        const double v = isMod ? (2.0 * f(1) - 1.0) * span / 4.0 : m.minVal + f(2) * span;
        const auto at = static_cast<uint32_t>(f(3) * (n + 1.0)) % (n + 1);
        params.push_back({at, slot, isMod, v});
    }
    std::vector<gg::core::NoteEvent> notes;
    for (int i = 0; i < 32; ++i)
    {
        const double t = u();
        const auto type = t < 0.6   ? gg::core::NoteEventType::On
                          : t < 0.9 ? gg::core::NoteEventType::Off
                                    : gg::core::NoteEventType::AllOff;
        notes.push_back({offset(), type, anyOr(u(), 16), anyOr(u(), 128), static_cast<float>(u())});
    }
    std::vector<gg::core::ControlEvent> controls;
    for (int i = 0; i < 32; ++i)
    {
        controls.push_back({offset(), static_cast<gg::core::ControlKind>(u() * 3.0), anyOr(u(), 16),
                            static_cast<float>(u())});
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

Verdict SweepDriver::check(const Checks &c) const
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
        where << "non-finite output at sample " << firstBad << " (" << labelAt(firstBad) << "); ";
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
        const std::size_t floats = 2 * static_cast<std::size_t>(ring.capacity + kLiveGuardFrames);
        for (std::size_t i = 0; ring.slots != nullptr && i < floats; ++i)
        {
            if (!std::isfinite(ring.slots[i]))
            {
                v.finite = false;
                where << "non-finite live ring of layer " << int{L} << " at slot " << i / 2 << "; ";
                break;
            }
        }
        const auto &peaks = processor_.livePeaks(L);
        for (int64_t f = 0; f < peaks.frames(); f += gg::core::LivePeaks::kBucket)
        {
            if (!std::isfinite(peaks.at(f)))
            {
                v.finite = false;
                where << "non-finite peak strip of layer " << int{L} << " at frame " << f << "; ";
                break;
            }
        }
    }
    if (!(v.peak <= c.peakBound))
    {
        where << "peak " << v.peak << " above " << c.peakBound << " at sample " << peakAt << " ("
              << labelAt(peakAt) << "); ";
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
        if (!(v.runawayDb <= Checks::kRunawayDb))
        {
            where << "runaway: the last second, from sample " << last << " (" << labelAt(last)
                  << "), at " << lastDb << " dB RMS, " << v.runawayDb
                  << " dB above the loudest earlier one, from sample " << loudestAt << " ("
                  << labelAt(loudestAt) << "); ";
        }
    }

    // The click score, over every stretch the check doesn't exempt.
    const ClickTerms terms = clickTerms();
    int64_t worstAt = 0;
    int64_t scored = 0;
    for (const auto &[a, b] : unexempt(frames))
    {
        scored += b - a;
        const auto [s, at] = clickScore(terms, a, b);
        if (s > v.clickScore)
        {
            v.clickScore = s;
            worstAt = at;
        }
    }
    if (!c.clicksExempt && !(v.clickScore <= c.clickLimit))
    {
        where << "click score " << v.clickScore << " above " << c.clickLimit << " at sample "
              << worstAt << " (" << labelAt(worstAt) << "); ";
    }
    v.scoredFraction = frames > 0 ? static_cast<double>(scored) / static_cast<double>(frames) : 0.0;
    if (!c.clicksExempt && !(v.scoredFraction >= c.minScored))
    {
        where << "the click check scored " << v.scoredFraction << " of the render, below "
              << c.minScored << "; ";
    }
    v.where = where.str();
    return v;
}

std::string SweepDriver::labelAt(int64_t sample) const
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

void SweepDriver::noteExempt(int64_t time, gg::core::ParamSlot slot, bool isMod, double value)
{
    if (slot != gg::core::ParamSlot::ReverbSize)
    {
        return;
    }
    double &held = isMod ? sizeMod_ : sizeBase_;
    if (static_cast<float>(value) != static_cast<float>(held))
    {
        held = value;
        sizeMoves_.push_back((time + 31) / 32 *
                             32); // exempt once applied, with its pre-delay (call())
    }
}

uint32_t SweepDriver::nextBlock()
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

void SweepDriver::renderTo(int64_t to)
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

void SweepDriver::call(uint32_t n, std::span<const gg::core::ParamEvent> events,
                       std::span<const gg::core::NoteEvent> notes,
                       std::span<const gg::core::ControlEvent> controls)
{
    // A Reverb Size base set straight on the Processor between calls (row 309) is a move at
    // this call's start, as a queued event is: the base now differs from the last call's end.
    // Compared as floats, the Processor's own width. Before the first sample nothing sounds, so
    // a set at clock 0 only updates the trackers.
    const float directSize = processor_.base(gg::core::ParamSlot::ReverbSize);
    if (directSize != seenSizeBase_)
    {
        sizeBase_ = directSize;
        seenSizeBase_ = directSize;
        if (clock_ > 0)
        {
            sizeMoves_.push_back((clock_ + 31) / 32 * 32);
        }
    }
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
    for (const auto &e : events)
    {
        if (e.slot == gg::core::ParamSlot::DelayAmount && e.value > 0.0)
        {
            delayEngaged_ = true;
        }
    }
    const float *in[2] = {in_[0].data(), in_[1].data()};
    float *out[2] = {out_[0].data() + at, out_[1].data() + at};
    processor_.process(live ? in : nullptr, live ? 2 : 0, out, 2, n, events, notes, controls);
    const int64_t callStart = clock_;
    clock_ += n;
    seenSizeBase_ = processor_.base(gg::core::ParamSlot::ReverbSize);
    if (processor_.effective(gg::core::ParamSlot::DelayAmount) > 0.0f)
    {
        delayEngaged_ = true;
    }

    // The Reverb Size moves applied in this call (row 217): each looks away for the pre-delay
    // in effect and the echoes after it.
    const int64_t predelay =
        framesOf(std::max(0.0f, processor_.effective(gg::core::ParamSlot::ReverbPredelay)));
    std::erase_if(sizeMoves_, [&](int64_t b) {
        if (b >= clock_)
        {
            return false; // not applied yet
        }
        exempt_.push_back({b - 64, b + predelay + kReverbSizeExempt, Exemption::ReverbSize});
        return true;
    });
    // kLimiterDrive: the call, and 10 ms after it.
    const auto drive = processor_.limiterDrive();
    if (drive.leastGain < kPeakDriveGain || drive.softDriven)
    {
        exempt_.push_back({callStart, clock_ + kLimiterDriveTail, Exemption::LimiterDrive});
    }
    // kDelayGlideHold: the samples of the call between the first and the last that met it.
    const auto glide = processor_.delayGlideSpan();
    if (glide.first >= 0)
    {
        exempt_.push_back(
            {callStart + glide.first, callStart + glide.last + 1, Exemption::DelayGlideHold});
    }
    inspect();
}

void SweepDriver::inspect()
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
              std::isfinite(g.durationSamples) && std::isfinite(g.gain) && std::isfinite(g.pan)))
        {
            exposedBad("grain in the grain queue");
        }
    }
}

double SweepDriver::rmsDb(int64_t from, int64_t n) const
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

std::vector<std::pair<int64_t, int64_t>> SweepDriver::unexempt(int64_t frames) const
{
    std::vector<std::pair<int64_t, int64_t>> ex;
    for (const auto &s : exempt_)
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
    // A stretch much shorter than the score's ±5 ms window lies mostly within that reach of
    // the exempt audio on either side, which its samples' windows hold: leave it out with its
    // neighbours (the floors on the scored share are pinned with this rule).
    std::erase_if(out, [](const auto &s) { return s.second - s.first < kMinStretch; });
    return out;
}

SweepDriver::ClickTerms SweepDriver::clickTerms() const
{
    const std::size_t n = out_[0].size();
    ClickTerms t{std::vector<double>(n, 0.0), std::vector<double>(n + 1, 0.0)};
    const auto mono = [&](std::size_t j) {
        return 0.5 * (finiteOr0(out_[0][j]) + finiteOr0(out_[1][j]));
    };
    for (std::size_t k = 2; k < n; ++k)
    {
        t.d[k] = std::abs(mono(k) - 2.0 * mono(k - 1) + mono(k - 2));
        t.prefix[k + 1] = t.prefix[k] + t.d[k] * t.d[k];
    }
    return t;
}

std::pair<double, int64_t> SweepDriver::clickScore(const ClickTerms &t, int64_t from,
                                                   int64_t to) const
{
    const std::size_t n = t.d.size();
    constexpr std::size_t kWindow = 240; // ±5 ms at 48 kHz
    constexpr std::size_t kGuard = 2;
    const std::size_t window = std::min(kWindow, n);
    double best = 0.0;
    int64_t at = from;
    for (auto k = static_cast<std::size_t>(std::max<int64_t>(from, 2));
         k < static_cast<std::size_t>(to); ++k)
    {
        if (!(t.d[k] > 1e-3))
        {
            continue;
        }
        const std::size_t lo = k - std::min(k - 2, window);
        const std::size_t hi = k + std::min(n - 1 - k, window);
        const std::size_t glo = k - std::min(k - lo, kGuard);
        const std::size_t ghi = k + std::min(hi - k, kGuard);
        const std::size_t count = (hi - lo + 1) - (ghi - glo + 1);
        const double sum = (t.prefix[hi + 1] - t.prefix[lo]) - (t.prefix[ghi + 1] - t.prefix[glo]);
        const double level =
            count > 0 ? std::sqrt(std::max(sum, 0.0) / static_cast<double>(count)) : 0.0;
        const double score = t.d[k] / std::max(level, 1e-6);
        if (score > best)
        {
            best = score;
            at = static_cast<int64_t>(k);
        }
    }
    return {best, at};
}
} // namespace gg::test
