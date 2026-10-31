// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "fx/BusFilter.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace gg::core::fx
{
void BusFilter::prepare(double sampleRate, uint32_t rampSamples)
{
    assert(std::isfinite(sampleRate) && sampleRate > 0.0);
    rampSamples_ = std::max<uint32_t>(rampSamples, 1);
    srInv_ = static_cast<float>(1.0 / sampleRate);
    cutoffMax_ = static_cast<float>(std::min(20000.0, 0.45 * sampleRate));
    reset();
}

void BusFilter::reset() noexcept
{
    for (Svf &f : filter_)
    {
        f.init();
        f.firstBlock = true;
    }
    active_ = 0;
    type_ = BusFilterType::Off;
    nextType_ = BusFilterType::Off;
    fading_ = false;
    fadePos_ = 0;
    cutoff_ = cutoffMax_;
    res_ = 0.0f;
}

void BusFilter::jump(BusFilterType type) noexcept
{
    Svf &f = filter_[active_];
    f.init();
    f.firstBlock = true;
    type_ = type;
    nextType_ = type;
    fading_ = false;
    fadePos_ = 0;
}

BusFilter::Svf::Mode BusFilter::modeOf(BusFilterType type) noexcept
{
    switch (type)
    {
    case BusFilterType::HighPass:
        return Svf::Mode::Highpass;
    case BusFilterType::BandPass:
        return Svf::Mode::Bandpass;
    case BusFilterType::Notch:
        return Svf::Mode::Notch;
    case BusFilterType::LowPass:
    case BusFilterType::Off:
        break;
    }
    return Svf::Mode::Lowpass;
}

void BusFilter::setCoeffs(Svf &f, BusFilterType type) noexcept
{
    if (type != BusFilterType::Off)
    {
        f.setCoeffForBlock<32>(modeOf(type), cutoff_, res_, srInv_);
    }
}

void BusFilter::setCell(BusFilterType type, float cutoffHz, float resonance) noexcept
{
    // NaN-safe: std::clamp passes a NaN through, so test it first.
    const float hz = std::isnan(cutoffHz) ? cutoffMax_ : cutoffHz;
    cutoff_ = std::clamp(hz, 20.0f, cutoffMax_);
    res_ = 0.95f * std::clamp(std::isnan(resonance) ? 0.0f : resonance, 0.0f, 1.0f);
    if (!fading_ && type != type_)
    {
        // The incoming type starts from silence on the spare filter, its coefficients snapped.
        nextType_ = type;
        fading_ = true;
        fadePos_ = 0;
        Svf &spare = filter_[1 - active_];
        spare.init();
        spare.firstBlock = true;
    }
    setCoeffs(filter_[active_], type_);
    if (fading_)
    {
        setCoeffs(filter_[1 - active_], nextType_);
    }
}

void BusFilter::process(float &l, float &r) noexcept
{
    if (!fading_)
    {
        if (type_ != BusFilterType::Off)
        {
            filter_[active_].processBlockStep(l, r);
        }
        return;
    }
    float fromL = l;
    float fromR = r;
    if (type_ != BusFilterType::Off)
    {
        filter_[active_].processBlockStep(fromL, fromR);
    }
    float toL = l;
    float toR = r;
    if (nextType_ != BusFilterType::Off)
    {
        filter_[1 - active_].processBlockStep(toL, toR);
    }
    ++fadePos_;
    const float f = static_cast<float>(fadePos_) / static_cast<float>(rampSamples_);
    l = fromL + (toL - fromL) * f;
    r = fromR + (toR - fromR) * f;
    if (fadePos_ >= rampSamples_)
    {
        active_ = static_cast<uint8_t>(1 - active_);
        type_ = nextType_;
        fading_ = false;
        fadePos_ = 0;
    }
}
} // namespace gg::core::fx
