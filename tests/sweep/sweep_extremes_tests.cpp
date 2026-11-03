// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The extremes (M2 engine spec §6.11): every parameter at its minimum, at its maximum, and at 32
// seeded random corners (each parameter at one end of its range), each held for 1 s with a note
// played in it, then every parameter back at its default for a last second.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "core/LayersFixture.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"
#include "sweep/SweepDriver.h"

using gg::core::LayerParam;
using gg::core::Mode;
using gg::core::NoteEventType;
using gg::core::ParamSlot;
using gg::test::Material;
using gg::test::SweepDriver;

namespace
{
constexpr int kCorners = 32;

/// Every parameter at its minimum (0), its maximum (1), or corner c's end for it (2 + c).
void setAll(SweepDriver &d, int state, uint64_t seed)
{
    for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        const auto slot = static_cast<ParamSlot>(s);
        const auto &m = d.meta(slot);
        const bool top =
            state == 1 ||
            (state >= 2 && gg::test::sweepdraw::byId(seed, static_cast<uint64_t>(state - 2),
                                                     gg::core::paramId(slot)) < 0.5);
        d.set(slot, top ? m.maxVal : m.minVal);
    }
    d.mark(state == 0 ? std::string("every parameter at its minimum")
           : state == 1
               ? std::string("every parameter at its maximum")
               : "corner " + std::to_string(state - 1) + " of " + std::to_string(kCorners));
}

/// Each exemption's own share of a driver's render (SweepDriver::exemptShare).
struct Shares
{
    double fadeIn, reverbSize, limiterDrive, delayGlideHold;
    double frames; ///< The render's length in samples.
};

Shares sharesOf(const SweepDriver &d)
{
    using gg::test::Exemption;
    return {d.exemptShare(Exemption::FadeIn), d.exemptShare(Exemption::ReverbSize),
            d.exemptShare(Exemption::LimiterDrive), d.exemptShare(Exemption::DelayGlideHold),
            static_cast<double>(d.clock())};
}

std::string describe(const Shares &s)
{
    std::ostringstream o;
    o << "exempt shares: fade-in " << s.fadeIn << ", Reverb Size " << s.reverbSize
      << ", kLimiterDrive " << s.limiterDrive << ", kDelayGlideHold " << s.delayGlideHold;
    return o.str();
}

/// `minScored`: the share of the render the click check scores, pinned a little under what the
/// case measures (0.424 in the FX, 0.437 in the instrument, with the draws keyed by CLAP ID):
/// Reverb Size moves are exempt (row 217), and so are kLimiterDrive's and kDelayGlideHold's
/// stretches (SweepDriver.h; the delay's glide, at 0.5 samples a sample, runs for up to 4 s).
/// The FX's was 0.413 before M2 sub-project 2's ring parameters were drawn too, and is 0.413 still
/// with them held at their defaults: at their ends (Overdub 1 among them) the rings are louder, so
/// Peak is driven under -6 dB longer and kLimiterDrive, unchanged, covers 12.8% of the render
/// against 11.6%; every other exemption covers what it did. Each exemption's own share of the
/// FX render (SweepDriver::exemptShare; they overlap): the fade-in 0.04%, Reverb Size moves
/// 43.0%, kLimiterDrive 12.8% and kDelayGlideHold 24.3% (the instrument's: 0.04%, 37.3%, 13.0% and
/// 21.5%; re-measured after row 302's fix). The reverb's onset, which was exempt for
/// 18.5% of it (20.7% of the instrument's) until row 302's fix, is scored now: 42.41% of the FX's
/// render is scored (40.17% before) and 43.67% of the instrument's (30.04% before).
Shares extremes(Mode mode, uint64_t seed, double minScored)
{
    SweepDriver d(mode, seed);
    const Material m = mode == Mode::Fx ? Material::SineLive : Material::NoteOverSine;
    d.play(2.0, m);
    for (int state = 0; state < 2 + kCorners; ++state)
    {
        setAll(d, state, seed);
        // A note in each second: in the FX it freezes the rings; in the instrument it joins the
        // held one.
        const auto key = static_cast<int16_t>(36 + state % 48);
        d.play(0.1, m);
        d.note(NoteEventType::On, key, 0.25f + 0.02f * static_cast<float>(state % 32));
        d.play(0.5, m);
        d.note(NoteEventType::Off, key);
        d.play(0.4, m);
    }
    // No runaway under the last corner's settings (row 309), before the defaults come back: the
    // check below sees the restored last second, which proves recovery alone.
    CHECK(d.check(gg::test::chainBound(d, 0.0)).runawayDb <= gg::test::Checks::kRunawayDb);
    d.setDefaults();
    d.play(1.0, m);
    gg::test::requireClean(d, gg::test::chainBound(d, minScored));
    return sharesOf(d);
}
} // namespace

TEST_CASE("extremes: minima, maxima and 32 corners, fx", "[sweep]")
{
    const Shares shares = extremes(Mode::Fx, 2101, 0.415);
    // The integration review's Minor 3: sub-project 2's Overdub can make the rings arbitrarily
    // loud, so kLimiterDrive's coverage grows with the draws rather than with the limiter's law
    // alone. A coarse cap on it, well above the 12.8% it covers.
    INFO(describe(shares));
    CHECK(shares.limiterDrive < 0.15);
}

TEST_CASE("extremes: minima, maxima and 32 corners, instrument", "[sweep]")
{
    extremes(Mode::Instrument, 2102, 0.43);
}

namespace
{
constexpr int kChainCorners = 16;

/// Resample and Spectral corner `c` (the integration review's Minor 2): every parameter at the
/// end corner c draws for it, as setAll's corners, then every layer on, its Source Resample (2)
/// or Spectral (3) by turns, the delay engaged at Amount 100% and the limiter Soft or Peak by
/// turns, with the pad at its centre and, in the FX, Mix 100%, so every layer is heard through the
/// chain. The ring parameters (Ring Length, Loop, Loop Length, Overdub, Splice) keep their drawn
/// ends, so Resample's loop runs back through the delay and the limiter (D4: the tap is after
/// the limiter) and Spectral reads rings of every length, looping and overdubbing or not.
void setChainCorner(SweepDriver &d, int c, uint64_t seed)
{
    gg::core::ParamValues v{};
    for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        const auto slot = static_cast<ParamSlot>(s);
        const auto &m = d.meta(slot);
        const bool top = gg::test::sweepdraw::byId(seed, static_cast<uint64_t>(c),
                                                   gg::core::paramId(slot)) < 0.5;
        v[s] = top ? m.maxVal : m.minVal;
    }
    const auto put = [&v](ParamSlot slot, float value) {
        v[static_cast<std::size_t>(slot)] = value;
    };
    for (uint8_t L = 0; L < gg::core::kLayers; ++L)
    {
        put(gg::core::layerOnSlot(L), 1.0f);
        put(gg::core::layerSlot(LayerParam::Source, L), (L + c) % 2 == 0 ? 2.0f : 3.0f);
    }
    put(ParamSlot::DelayAmount, 1.0f);
    put(ParamSlot::LimiterMode, c % 2 == 0 ? 1.0f : 2.0f); // Soft, Peak
    // At its top ceiling, 0 dB, the limiter still limits the rings' overdubbed material, without
    // D5b's drive under a low one (which the main extremes cover, and which here would leave half
    // the render to kLimiterDrive). Every other parameter, the reverb's included, takes its
    // drawn end, as the main extremes' corners do.
    put(ParamSlot::LimiterCeiling, d.meta(ParamSlot::LimiterCeiling).maxVal);
    put(ParamSlot::MorphX, 0.5f);
    put(ParamSlot::MorphY, 0.5f);
    if (d.mode() == Mode::Fx)
    {
        put(ParamSlot::Mix, 1.0f);
    }
    for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        d.set(static_cast<ParamSlot>(s), v[s]);
    }
    d.mark("Resample and Spectral corner " + std::to_string(c + 1) + " of " +
           std::to_string(kChainCorners));
}

/// kChainCorners Resample and Spectral corners, each held for 1 s with a note in it as the
/// extremes play theirs, then every parameter at its default for a last second. In the FX,
/// layers A to C have no sample, so a Spectral layer there smears its live ring; layer D keeps
/// the sine, so it smears that. In the instrument every layer keeps the sine.
Shares chainCorners(Mode mode, uint64_t seed, double minScored)
{
    SweepDriver d(mode, seed);
    if (mode == Mode::Fx)
    {
        for (uint8_t L = 0; L < 3; ++L)
        {
            d.processor().setSourceNow(L, nullptr);
        }
    }
    const Material m = mode == Mode::Fx ? Material::SineLive : Material::NoteOverSine;
    // The FX's layer A smears its ring when Spectral, the instrument's its sample.
    REQUIRE(d.processor().layerMaterial(0) ==
            (mode == Mode::Fx ? gg::SourceMode::Live : gg::SourceMode::Sample));
    d.play(2.0, m);
    for (int c = 0; c < kChainCorners; ++c)
    {
        setChainCorner(d, c, seed);
        const auto key = static_cast<int16_t>(40 + c % 40);
        d.play(0.1, m);
        d.note(NoteEventType::On, key, 0.3f + 0.03f * static_cast<float>(c % 16));
        d.play(0.5, m);
        d.note(NoteEventType::Off, key);
        d.play(0.4, m);
    }
    // No runaway under the last corner's settings (row 309), before the defaults come back.
    CHECK(d.check(gg::test::chainBound(d, 0.0)).runawayDb <= gg::test::Checks::kRunawayDb);
    d.setDefaults();
    d.play(1.0, m);
    gg::test::requireClean(d, gg::test::chainBound(d, minScored));
    return sharesOf(d);
}
} // namespace

namespace
{
/// The eight Soft-to-Peak mode fades of the chain corners (480 samples and 480 after each), as a
/// share of `frames`.
constexpr double kPeakFadeShare(double frames) { return 8 * 960.0 / frames; }
/// Slack on a share: a rounding error's worth, not a sample.
constexpr double kShareEpsilon = 1e-6;
} // namespace

// The floors, a little under what each case measures (the integration review's Minor 2,
// 2026-11-01): the FX scores 0.4253 of its render (it scored 0.3323 while the reverb's onset was
// exempt, row 302), the instrument 0.3698 (0.2795). Each exemption's own share in the FX: the
// fade-in 0.08%, Reverb Size moves 45.3%, kLimiterDrive 0.84% and kDelayGlideHold 31.5%; in the
// instrument 0.08%, 44.8%, 0.84% and 24.9%. kLimiterDrive is pinned to the stretches it must
// cover (K-2): the render starts at Off and `setChainCorner` alternates Soft (even corners) and
// Peak (odd ones), so there are eight Soft-to-Peak mode fades, each 480 samples and the 480 after
// (kPeakFadeShare below). The ceiling is held at 0 dB, so more means the limiter is driven where
// the case says it isn't.
TEST_CASE("extremes: Resample and Spectral under the delay and the limiter, fx", "[sweep]")
{
    const Shares shares = chainCorners(Mode::Fx, 2104, 0.42);
    INFO(describe(shares));
    CHECK(shares.limiterDrive <= kPeakFadeShare(shares.frames) + kShareEpsilon);
}

TEST_CASE("extremes: Resample and Spectral under the delay and the limiter, instrument", "[sweep]")
{
    const Shares shares = chainCorners(Mode::Instrument, 2105, 0.365);
    INFO(describe(shares));
    CHECK(shares.limiterDrive <= kPeakFadeShare(shares.frames) + kShareEpsilon);
}

namespace
{
/// Corner 13's layer C, as the instrument extremes drew it under the sources branch's slot layout
/// (the corner13 report): layer C alone, the pad at X 0, Y 0, Level and Level C at +12 dB, and C's
/// grain parameters at their corner ends (Size 5 ms, Density 0.25 Hz, Texture 1, the gated
/// anchor, Pitch -24, Spray, Stereo, Freeze, Distribution, both spreads at their tops, Quality
/// Eco). Its Source, Oscillator, plays as Sample on main; Sample is set here so the case doesn't
/// move when Oscillator is built. The rest stay at their defaults: a lone 5 ms grain every few
/// seconds over the held note, with a 16-sample release into silence.
void corner13LayerC(SweepDriver &d)
{
    auto &p = d.processor();
    constexpr uint8_t kC = 2;
    for (uint8_t L = 0; L < gg::core::kLayers; ++L)
    {
        p.setBase(gg::core::layerOnSlot(L), L == kC ? 1.0f : 0.0f);
    }
    p.setBase(ParamSlot::MorphX, 0.0f);
    p.setBase(ParamSlot::MorphY, 0.0f);
    p.setBase(ParamSlot::Level, 12.0f);
    p.setBase(gg::core::levelSlot(kC), 12.0f);
    const auto end = [&](LayerParam q, bool top) {
        const ParamSlot slot = gg::core::layerSlot(q, kC);
        p.setBase(slot, top ? d.meta(slot).maxVal : d.meta(slot).minVal);
    };
    end(LayerParam::Source, false);
    end(LayerParam::Size, false);
    end(LayerParam::Density, false);
    end(LayerParam::Texture, true);
    end(LayerParam::Pitch, false);
    end(LayerParam::Spray, true);
    end(LayerParam::Stereo, true);
    end(LayerParam::Freeze, true);
    end(LayerParam::Distribution, true);
    end(LayerParam::SizeSpread, true);
    end(LayerParam::PitchSpread, true);
    end(LayerParam::Quality, false);
}

constexpr double kGrainSeconds = 5.5;

/// corner13LayerC over the held note for kGrainSeconds, with Reverb Pre-delay at `predelay`
/// seconds and, when `moveAt` >= 0, a Reverb Size move at sample `moveAt` (a multiple of 32, so
/// it applies there): its exemption then ends at moveAt + the pre-delay + kReverbSizeExempt.
/// Reverb Amount stays 0, so neither changes the output.
std::unique_ptr<SweepDriver> renderCorner13(int64_t moveAt, double predelay)
{
    auto d = std::make_unique<SweepDriver>(Mode::Instrument, 2103);
    corner13LayerC(*d);
    d->processor().setBase(ParamSlot::ReverbPredelay, static_cast<float>(predelay));
    if (moveAt >= 0)
    {
        d->play(static_cast<double>(moveAt) / SweepDriver::kRate, Material::NoteOverSine);
        d->set(ParamSlot::ReverbSize, 0.9);
    }
    d->play(kGrainSeconds - static_cast<double>(d->clock()) / SweepDriver::kRate,
            Material::NoteOverSine);
    return d;
}
} // namespace

TEST_CASE("extremes: corner 13's lone grain, its release cut by an exemption's end", "[sweep]")
{
    // The instrument extremes' corner 13 (under the sources branch's slot layout) failed at 36.6
    // against 15: a sample-end exemption ended 8 samples before the end of a lone 5 ms grain's
    // 16-sample release, and the score of the stretch after it saw those 8 samples against the
    // silence after them alone. Over the whole render the same samples score 2.8. The click
    // check scores each sample outside the exemptions as the goldens score it, over the whole
    // render, so cutting the render can only lower the score.
    const auto plain = renderCorner13(-1, 0.0);
    const auto &out = plain->output();
    const auto frames = static_cast<int64_t>(out[0].size());
    const auto mono = [&](int64_t i) {
        const auto j = static_cast<std::size_t>(i);
        return 0.5 * (static_cast<double>(out[0][j]) + static_cast<double>(out[1][j]));
    };
    // The last grain: its last sounding sample, and its first.
    int64_t last = frames - 1;
    while (last > 0 && mono(last) == 0.0)
    {
        --last;
    }
    int64_t first = last;
    while (first > 0 && mono(first - 1) != 0.0)
    {
        --first;
    }
    INFO("the last grain sounds from sample " << first << " to " << last);
    REQUIRE(last - first < 1000);      // one short grain
    REQUIRE(frames - last > 2000);     // silence after it
    REQUIRE(first > 2 * 28800 + 2000); // room for the exemption before it
    REQUIRE(mono(first - 1) == 0.0);
    // The exemption's end, 8 samples before the grain's last: inside its 16-sample release.
    const int64_t cut = last - 7;
    const int64_t moveAt = (cut - SweepDriver::kReverbSizeExempt - 1000) / 32 * 32;
    const int64_t predelayFrames = cut - SweepDriver::kReverbSizeExempt - moveAt;
    const auto moved =
        renderCorner13(moveAt, static_cast<double>(predelayFrames) / SweepDriver::kRate);
    REQUIRE(moved->output() == out); // the move changes the exemptions, not the audio

    const gg::test::Checks c = gg::test::chainBound(*plain, 0.0);
    const auto whole = plain->check(c);
    const auto cutUp = moved->check(c);
    INFO("whole: " << whole.clickScore << ", " << whole.where);
    INFO("with the exemption: " << cutUp.clickScore << ", scored " << cutUp.scoredFraction << ", "
                                << cutUp.where);
    REQUIRE(whole.clickScore <= c.clickLimit);
    REQUIRE(cutUp.scoredFraction < whole.scoredFraction); // the exemption is there
    CHECK(cutUp.clickScore <= whole.clickScore);
    CHECK(cutUp.clickScore <= c.clickLimit);
}
