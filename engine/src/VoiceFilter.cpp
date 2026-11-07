// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "VoiceFilterBank.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

namespace gg
{
namespace
{
/// The smallest multiple of kLaneFrames (the control grid) at or above x.
constexpr int64_t gridCeil(int64_t x) noexcept
{
    constexpr auto kGrid = static_cast<int64_t>(kLaneFrames);
    const int64_t q = x / kGrid;
    return (x % kGrid > 0 ? q + 1 : q) * kGrid;
}
static_assert(gridCeil(0) == 0 && gridCeil(1) == 32 && gridCeil(32) == 32 && gridCeil(-1) == 0);
} // namespace

void VoiceFilterBank::prepare(double sampleRate, uint32_t rampSamples)
{
    assert(std::isfinite(sampleRate) && sampleRate > 0.0);
    rampSamples_ = std::max<uint32_t>(rampSamples, 1);
    srInv_ = static_cast<float>(1.0 / sampleRate);
    cutoffMax_ = static_cast<float>(std::min(20000.0, 0.45 * sampleRate));
    capSamples_ = std::llround(kLaneCapSeconds * sampleRate);
    // Value-initialized, so a filter's coefficients are zeros, never indeterminate, before its
    // first beginCell.
    lanes_.fill(Lane{});
    std::memset(buf_, 0, sizeof(buf_));
    reset();
}

void VoiceFilterBank::silence(Svf &f) noexcept
{
    f.init();
    f.firstBlock = true;
}

void VoiceFilterBank::freeLane(Lane &lane) noexcept
{
    if (lane.state != LaneState::Free)
    {
        --busy_;
    }
    lane.state = LaneState::Free;
    for (Svf &f : lane.filter)
    {
        silence(f);
    }
    lane.owner = 0;
    lane.lastEnd = 0;
}

void VoiceFilterBank::reset() noexcept
{
    for (Lane &lane : lanes_)
    {
        freeLane(lane);
        lane.cutoff = cutoffMax_;
        lane.res = 0.0f;
    }
    busy_ = 0;
    active_ = 0;
    type_ = VoiceFilterType::Off;
    nextType_ = VoiceFilterType::Off;
    requested_ = VoiceFilterType::Off;
    fading_ = false;
    fadePos_ = 0;
}

void VoiceFilterBank::jumpToRequest() noexcept
{
    type_ = requested_;
    nextType_ = requested_;
    fading_ = false;
    fadePos_ = 0;
}

VoiceFilterBank::Svf::Mode VoiceFilterBank::modeOf(VoiceFilterType type) noexcept
{
    switch (type)
    {
    case VoiceFilterType::HighPass:
        return Svf::Mode::Highpass;
    case VoiceFilterType::BandPass:
        return Svf::Mode::Bandpass;
    case VoiceFilterType::Notch:
        return Svf::Mode::Notch;
    case VoiceFilterType::LowPass:
    case VoiceFilterType::Off:
        break;
    }
    return Svf::Mode::Lowpass;
}

void VoiceFilterBank::setCoeffs(Svf &f, const Lane &lane, VoiceFilterType type) noexcept
{
    if (type != VoiceFilterType::Off)
    {
        f.setCoeffForBlock<32>(modeOf(type), lane.cutoff, lane.res, srInv_);
    }
}

void VoiceFilterBank::applyRequest() noexcept
{
    if (busy_ == 0)
    {
        // Nothing to fade (FB15).
        jumpToRequest();
        return;
    }
    if (fading_ && requested_ == type_)
    {
        // BusFilter::setCell's change back to the outgoing type (A to B to A): the roles swap and
        // the fade turns from its current weight. A third type waits for the fade to end.
        active_ = static_cast<uint8_t>(1 - active_);
        type_ = nextType_;
        nextType_ = requested_;
        fadePos_ = std::min(rampSamples_ - fadePos_, rampSamples_ - 1);
    }
    else if (!fading_ && requested_ != type_)
    {
        // The incoming type starts from silence on every busy lane's spare filter.
        nextType_ = requested_;
        fading_ = true;
        fadePos_ = 0;
        for (Lane &lane : lanes_)
        {
            if (lane.state != LaneState::Free)
            {
                silence(lane.filter[1 - active_]);
            }
        }
    }
}

uint8_t VoiceFilterBank::assign(uint32_t ordinal, int64_t clock, float cutoffHz,
                                float resonance) noexcept
{
    uint8_t pick = kNoLane;
    for (uint8_t l = 0; l < kVoiceLanes; ++l)
    {
        if (lanes_[l].state == LaneState::Free)
        {
            pick = l;
            break;
        }
    }
    if (pick != kNoLane)
    {
        Lane &lane = lanes_[pick];
        silence(lane.filter[active_]);
        if (fading_)
        {
            silence(lane.filter[1 - active_]); // joins the fade, both sides from silence
        }
        lane.lastEnd = clock;
        ++busy_;
    }
    else
    {
        // The merge (F2): the oldest Draining lane, its state kept.
        for (uint8_t l = 0; l < kVoiceLanes; ++l)
        {
            if (lanes_[l].state == LaneState::Draining &&
                (pick == kNoLane || lanes_[l].owner < lanes_[pick].owner))
            {
                pick = l;
            }
        }
        if (pick == kNoLane)
        {
            return kNoLane;
        }
        lanes_[pick].lastEnd = std::max(lanes_[pick].lastEnd, clock);
    }
    Lane &lane = lanes_[pick];
    lane.state = LaneState::Owned;
    lane.owner = ordinal;
    setCell(pick, cutoffHz, resonance);
    return pick;
}

void VoiceFilterBank::setCell(uint8_t lane, float cutoffHz, float resonance) noexcept
{
    assert(lane < kVoiceLanes);
    Lane &l = lanes_[lane];
    // BusFilter::setCell's clamp: std::clamp passes a NaN through, so test it first.
    const float hz = std::isnan(cutoffHz) ? cutoffMax_ : cutoffHz;
    l.cutoff = std::clamp(hz, 20.0f, cutoffMax_);
    l.res = 0.95f * std::clamp(std::isnan(resonance) ? 0.0f : resonance, 0.0f, 1.0f);
}

void VoiceFilterBank::drain(uint8_t lane) noexcept
{
    assert(lane < kVoiceLanes);
    if (lanes_[lane].state == LaneState::Owned)
    {
        lanes_[lane].state = LaneState::Draining;
    }
}

void VoiceFilterBank::raiseLastEnd(uint8_t lane, int64_t end) noexcept
{
    assert(lane < kVoiceLanes);
    lanes_[lane].lastEnd = std::max(lanes_[lane].lastEnd, end);
}

void VoiceFilterBank::endAllAt(int64_t t) noexcept
{
    for (Lane &lane : lanes_)
    {
        if (lane.state != LaneState::Free)
        {
            lane.lastEnd = t;
        }
    }
}

void VoiceFilterBank::freeAll() noexcept
{
    for (Lane &lane : lanes_)
    {
        freeLane(lane);
    }
    busy_ = 0;
    jumpToRequest();
}

void VoiceFilterBank::freeFinished(int64_t t, bool grid, bool engaged) noexcept
{
    if (busy_ == 0)
    {
        return;
    }
    for (uint8_t l = 0; l < kVoiceLanes; ++l)
    {
        Lane &lane = lanes_[l];
        if (lane.state != LaneState::Draining)
        {
            continue;
        }
        const bool capped = gridCeil(lane.lastEnd + capSamples_) <= t;
        const bool due = gridCeil(lane.lastEnd) <= t;
        // The state is read only when due, engaged, on the grid and with no fade (FB11).
        const bool rungOut =
            due && (!engaged || (grid && !fading_ && stateMagnitude(l) < kLaneFreeLevel));
        if (capped || rungOut)
        {
            freeLane(lane);
        }
    }
    if (busy_ == 0 && fading_)
    {
        jumpToRequest(); // the last busy lane freed mid-fade (FB15)
    }
}

LaneState VoiceFilterBank::state(uint8_t lane) const noexcept
{
    assert(lane < kVoiceLanes);
    return lanes_[lane].state;
}

uint32_t VoiceFilterBank::owner(uint8_t lane) const noexcept
{
    assert(lane < kVoiceLanes);
    return lanes_[lane].owner;
}

int64_t VoiceFilterBank::lastEnd(uint8_t lane) const noexcept
{
    assert(lane < kVoiceLanes);
    return lanes_[lane].lastEnd;
}

float VoiceFilterBank::stateMagnitude(uint8_t lane) const noexcept
{
    assert(lane < kVoiceLanes);
    const Svf &f = lanes_[lane].filter[active_];
    alignas(16) float s1[4];
    alignas(16) float s2[4];
    SIMD_MM(store_ps)(s1, f.ic1eq);
    SIMD_MM(store_ps)(s2, f.ic2eq);
    float m = 0.0f;
    for (int c = 0; c < 4; ++c)
    {
        m = std::max({m, std::fabs(s1[c]), std::fabs(s2[c])});
    }
    return m;
}

void VoiceFilterBank::beginCell() noexcept
{
    if (busy_ == 0)
    {
        return;
    }
    for (Lane &lane : lanes_)
    {
        if (lane.state == LaneState::Free)
        {
            continue;
        }
        setCoeffs(lane.filter[active_], lane, type_);
        if (fading_)
        {
            setCoeffs(lane.filter[1 - active_], lane, nextType_);
        }
    }
}

uint32_t VoiceFilterBank::busyByOwner(std::span<uint8_t, kVoiceLanes> order) const noexcept
{
    // An insertion sort of at most 32: owners are unique among busy lanes (FB5).
    uint32_t n = 0;
    for (uint8_t l = 0; l < kVoiceLanes; ++l)
    {
        if (lanes_[l].state == LaneState::Free)
        {
            continue;
        }
        uint32_t j = n;
        while (j > 0 && lanes_[order[j - 1]].owner > lanes_[l].owner)
        {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = l;
        ++n;
    }
    return n;
}

float *VoiceFilterBank::scratch(uint8_t lane, uint32_t channel) noexcept
{
    assert(lane < kVoiceLanes && channel < 4);
    return buf_[lane][channel];
}

void VoiceFilterBank::zero(uint8_t lane, uint32_t n) noexcept
{
    assert(lane < kVoiceLanes && n <= kLaneFrames);
    for (float *ch : buf_[lane])
    {
        std::fill_n(ch, n, 0.0f);
    }
}

void VoiceFilterBank::filter(uint8_t lane, uint32_t n) noexcept
{
    assert(lane < kVoiceLanes && n <= kLaneFrames);
    Lane &l = lanes_[lane];
    float *a = buf_[lane][0];
    float *b = buf_[lane][1];
    float *c = buf_[lane][2];
    float *d = buf_[lane][3];
    // The controller's state, stepped locally as BusFilter::process steps its own; advance moves
    // the real one once every lane is filtered (plan decision P6).
    uint8_t act = active_;
    VoiceFilterType from = type_;
    const VoiceFilterType to = nextType_;
    bool fading = fading_;
    uint32_t pos = fadePos_;
    for (uint32_t i = 0; i < n; ++i)
    {
        if (!fading)
        {
            if (from == VoiceFilterType::Off)
            {
                return; // Off with no fade leaves the samples untouched
            }
            l.filter[act].processBlockStep(a[i], b[i], c[i], d[i]);
            continue;
        }
        float fa = a[i];
        float fb = b[i];
        float fc = c[i];
        float fd = d[i];
        if (from != VoiceFilterType::Off)
        {
            l.filter[act].processBlockStep(fa, fb, fc, fd);
        }
        float ta = a[i];
        float tb = b[i];
        float tc = c[i];
        float td = d[i];
        if (to != VoiceFilterType::Off)
        {
            l.filter[1 - act].processBlockStep(ta, tb, tc, td);
        }
        ++pos;
        const float f = static_cast<float>(pos) / static_cast<float>(rampSamples_);
        a[i] = fa + (ta - fa) * f;
        b[i] = fb + (tb - fb) * f;
        c[i] = fc + (tc - fc) * f;
        d[i] = fd + (td - fd) * f;
        if (pos >= rampSamples_)
        {
            act = static_cast<uint8_t>(1 - act);
            from = to;
            fading = false;
            pos = 0;
        }
    }
}

void VoiceFilterBank::advance(uint32_t n) noexcept
{
    if (!fading_ || n == 0)
    {
        return;
    }
    // A fade is in progress, so fadePos_ < rampSamples_.
    if (n >= rampSamples_ - fadePos_)
    {
        active_ = static_cast<uint8_t>(1 - active_);
        type_ = nextType_;
        fading_ = false;
        fadePos_ = 0;
    }
    else
    {
        fadePos_ += n;
    }
}
} // namespace gg
