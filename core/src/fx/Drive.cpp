// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "fx/Drive.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

// The one translation unit that includes sst-waveshapers (color spec §5.2), and only these four
// headers (never QuadWaveshaper_Impl.h, GetQuadWaveshaper or Fuzzes.h).
#include <sst/waveshapers/Effects.h>
#include <sst/waveshapers/Rectifiers.h>
#include <sst/waveshapers/Saturators.h>
#include <sst/waveshapers/Wavefolders.h>

#include "gg/core/MathDet.h"

namespace gg::core::fx
{
namespace ws = sst::waveshapers;

struct Drive::State
{
    ws::QuadWaveshaperState ws[2];
};

namespace
{
constexpr float kA = 0.25f; // -12 dBFS, the level C3 matches at

void fresh(ws::QuadWaveshaperState &s) noexcept
{
    const auto zero = SIMD_MM(setzero_ps)();
    for (auto &reg : s.R)
    {
        reg = zero;
    }
    s.init = SIMD_MM(cmpeq_ps)(zero, zero); // all bits set: a fresh lane
}

SIMD_M128 shapeOf(DriveShape shape, ws::QuadWaveshaperState *s, SIMD_M128 in,
                  SIMD_M128 drive) noexcept
{
    switch (shape)
    {
    case DriveShape::Soft:
        return ws::TANH(s, in, drive);
    case DriveShape::Medium:
        return ws::ZAMSAT(s, in, drive);
    case DriveShape::Hard:
        return ws::CLIP(s, in, drive);
    case DriveShape::Asymmetric:
        return ws::ASYM_SSE2(s, in, drive);
    case DriveShape::Ojd:
        return ws::OJD(s, in, drive);
    case DriveShape::Rectify:
        return ws::ADAA_FULL_WAVE(s, in, drive);
    case DriveShape::Fold:
        return ws::LINFOLD_SSE2(s, in, drive);
    case DriveShape::SineFold:
        return ws::SINUS_SSE2<true>(s, in, drive);
    }
    return ws::TANH(s, in, drive);
}

/// The shaper's input gain, 0 to +36 dB: 10^(36d/20).
float gainOf(float d) noexcept
{
    return static_cast<float>(exp2Det(static_cast<double>(d) * 36.0 / 6.020599913279624));
}

float clampD(float d) noexcept
{
    // NaN-safe: a NaN reads as 0.
    return d > 0.0f ? std::min(d, 1.0f) : 0.0f;
}
} // namespace

Drive::Drive() = default;
Drive::~Drive() = default;

void Drive::prepare(double sampleRate, uint32_t rampSamples, bool oversample)
{
    assert(std::isfinite(sampleRate) && sampleRate > 0.0);
    if (!state_)
    {
        state_ = std::make_unique<State>();
    }
    oversample_ = oversample;
    rampSamples_ = std::max<uint32_t>(rampSamples, 1);
    dcR_ = static_cast<float>(1.0 - 2.0 * std::numbers::pi * 5.0 / sampleRate);

    // C3: comp = a / max(p, a/4), p the largest |S(g·a·j/8)| over j = ±1..±8, each probe from a
    // fresh state. Evaluating it calls every shape: the prewarm (§5.2).
    for (int s = 0; s < kShapes; ++s)
    {
        for (int k = 0; k <= kCompSteps; ++k)
        {
            const float g = gainOf(static_cast<float>(k) / static_cast<float>(kCompSteps));
            float p = 0.0f;
            for (int j = -8; j <= 8; ++j)
            {
                if (j == 0)
                {
                    continue;
                }
                ws::QuadWaveshaperState probe;
                fresh(probe);
                const float in = kA * static_cast<float>(j) / 8.0f;
                alignas(16) float out[4];
                SIMD_MM(store_ps)(out, shapeOf(static_cast<DriveShape>(s), &probe,
                                               SIMD_MM(set1_ps)(in), SIMD_MM(set1_ps)(g)));
                p = std::max(p, std::fabs(out[0]));
            }
            comp_[static_cast<std::size_t>(s * (kCompSteps + 1) + k)] = kA / std::max(p, kA / 4.0f);
        }
    }
    reset();
}

void Drive::reset() noexcept
{
    up_.reset();
    down_.reset();
    if (state_)
    {
        fresh(state_->ws[0]);
        fresh(state_->ws[1]);
    }
    active_ = 0;
    shape_ = DriveShape::Soft;
    nextShape_ = DriveShape::Soft;
    fading_ = false;
    fadePos_ = 0;
    snapNext_ = true;
    dcXL_ = dcXR_ = dcYL_ = dcYR_ = 0.0f;
}

void Drive::jump(DriveShape shape) noexcept
{
    if (state_)
    {
        fresh(state_->ws[active_]);
    }
    shape_ = shape;
    nextShape_ = shape;
    fading_ = false;
    fadePos_ = 0;
    snapNext_ = false;
}

void Drive::setCell(DriveShape shape) noexcept
{
    if (snapNext_)
    {
        jump(shape);
        return;
    }
    if (fading_ && shape == shape_)
    {
        // Back to the outgoing shape: turn the fade from its current value (BusFilter's rule).
        active_ = static_cast<uint8_t>(1 - active_);
        shape_ = nextShape_;
        nextShape_ = shape;
        fadePos_ = std::min(rampSamples_ - fadePos_, rampSamples_ - 1);
    }
    else if (!fading_ && shape != shape_)
    {
        // The incoming shape starts from rest on the spare state.
        nextShape_ = shape;
        fading_ = true;
        fadePos_ = 0;
        fresh(state_->ws[1 - active_]);
    }
}

float Drive::comp(DriveShape shape, float d) const noexcept
{
    const float pos = clampD(d) * static_cast<float>(kCompSteps);
    const auto i = static_cast<int>(pos);
    const float *row = comp_.data() + static_cast<std::size_t>(shape) * (kCompSteps + 1);
    if (i >= kCompSteps)
    {
        return row[kCompSteps];
    }
    const float t = pos - static_cast<float>(i);
    return row[i] + t * (row[i + 1] - row[i]);
}

void Drive::shapePair(float inL, float inR, float g, float cOld, float cNew, float f, float &outL,
                      float &outR) noexcept
{
    const auto in = SIMD_MM(set_ps)(0.0f, 0.0f, inR, inL);
    const auto drive = SIMD_MM(set1_ps)(g);
    alignas(16) float a[4];
    SIMD_MM(store_ps)(a, shapeOf(shape_, &state_->ws[active_], in, drive));
    outL = a[0] * cOld;
    outR = a[1] * cOld;
    if (fading_)
    {
        alignas(16) float b[4];
        SIMD_MM(store_ps)(b, shapeOf(nextShape_, &state_->ws[1 - active_], in, drive));
        outL = (1.0f - f) * outL + f * (b[0] * cNew);
        outR = (1.0f - f) * outR + f * (b[1] * cNew);
    }
}

void Drive::process(float &l, float &r, float d) noexcept
{
    d = clampD(d);
    const float g = gainOf(d);
    const float cOld = comp(shape_, d);
    float cNew = 0.0f;
    float f = 0.0f;
    if (fading_)
    {
        cNew = comp(nextShape_, d);
        ++fadePos_;
        f = static_cast<float>(fadePos_) / static_cast<float>(rampSamples_);
    }

    float dl;
    float dr;
    if (oversample_)
    {
        float uL[2], uR[2], vL[2], vR[2];
        up_.up(l, r, uL, uR);
        // HalfBand2x's up keeps the library's 0.5 (process_block_U2); 2x (exact) makes the
        // up-and-down path unity, so the shaper sees the signal's own level.
        for (int k = 0; k < 2; ++k)
        {
            uL[k] *= 2.0f;
            uR[k] *= 2.0f;
        }
        shapePair(uL[0], uR[0], g, cOld, cNew, f, vL[0], vR[0]);
        shapePair(uL[1], uR[1], g, cOld, cNew, f, vL[1], vR[1]);
        down_.down(vL, vR, dl, dr);
    }
    else
    {
        shapePair(l, r, g, cOld, cNew, f, dl, dr);
    }

    if (fading_ && fadePos_ >= rampSamples_)
    {
        active_ = static_cast<uint8_t>(1 - active_);
        shape_ = nextShape_;
        fading_ = false;
        fadePos_ = 0;
    }

    // The 5 Hz DC blocker: y_n = x_n − x_{n−1} + R·y_{n−1}.
    const float yl = dl - dcXL_ + dcR_ * dcYL_;
    const float yr = dr - dcXR_ + dcR_ * dcYR_;
    dcXL_ = dl;
    dcXR_ = dr;
    dcYL_ = yl;
    dcYR_ = yr;

    const float e = std::min(1.0f, d / 0.05f);
    if (e != 0.0f)
    {
        l = l + e * (yl - l);
        r = r + e * (yr - r);
    }
}
} // namespace gg::core::fx
