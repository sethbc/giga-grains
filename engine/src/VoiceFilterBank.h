// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_ENGINE_SRC_VOICEFILTERBANK_H
#define GG_ENGINE_SRC_VOICEFILTERBANK_H

// Private to gg-engine (M3 sub-project 9's plan decision P3): it includes sst-filters, which
// gg-engine links PRIVATE, so no public engine header includes it. The tests reach it through
// engine/src.

#include <array>
#include <cstdint>
#include <span>

#include <sst/basic-blocks/simd/setup.h>
#include <sst/cpputils/rtsan_support.h>
#include <sst/filters/CytomicSVF.h>

#include "gg/grain/GrainPool.h"
#include "gg/grain/VoiceFilter.h"

namespace gg
{
/// A layer's voice filter (M3 sub-project 9's spec F2, F9, FB1, FB2, FB6, FB11, FB15, FB17):
/// 32 lanes, each a four-channel Cytomic state-variable filter pair (active and spare) with its
/// owner's cell, under one type controller that every lane follows.
///
/// The law is core/src/fx/BusFilter's, copied exactly, so channels 0 and 1 of a lane are
/// BusFilter's bit for bit over the same cells: the cutoff clamped to [20, min(20000, 0.45 ×
/// sample rate)] Hz (a NaN cutoff is the maximum, a NaN resonance 0), the filter's resonance 0.95
/// × clamp(resonance, 0, 1), the coefficients gliding across each 32-sample cell, and a type
/// change crossfading from the outgoing type to the incoming one, started from silence on the
/// spare, over the ramp. A change back during a fade turns it; a third type waits.
///
/// The owner (the engine) drives one cell at a time: applyRequest, the lanes' setCell, beginCell,
/// then for each block inside the cell (at most kLaneFrames frames) the busy lanes' filter in
/// any order and advance once. Every method but prepare is real-time safe: fixed arrays, no
/// allocation, bounded loops.
class VoiceFilterBank
{
  public:
    /// Sets the rate and the type fade's length, then reset(). Not real-time.
    void prepare(double sampleRate, uint32_t rampSamples);

    /// Every lane Free, its filters silent; Off in force and asked for; no fade.
    void reset() noexcept;

    // --- The type controller (FB2, FB15) ---

    /// The layer's type request, applied by the next applyRequest.
    void request(VoiceFilterType type) noexcept { requested_ = type; }
    VoiceFilterType requested() const noexcept { return requested_; }
    /// The type in force: the outgoing one during a fade.
    VoiceFilterType inForce() const noexcept { return type_; }
    /// Where the controller is headed: the incoming type during a fade, else the type in force.
    VoiceFilterType target() const noexcept { return fading_ ? nextType_ : type_; }
    bool fading() const noexcept { return fading_; }
    /// Applies the request, at a grid sample: with a busy lane exactly as BusFilter::setCell
    /// applies its type (a fade starts, turns back or waits); with none, a jump with no fade.
    void applyRequest() noexcept SST_CPPUTILS_NONBLOCKING;

    // --- Lanes (F2, FB6) ---

    /// The lane for a new voice: the lowest Free one, its active filter (and its spare, during a
    /// fade) started from silence with its coefficients snapped; else the Draining lane whose
    /// owner has the lowest ordinal, merged (its state kept, its owner and cell replaced). Its
    /// lastEnd becomes max(lastEnd, clock) (a new lane's is the clock). kNoLane if every lane is
    /// Owned.
    uint8_t assign(uint32_t ordinal, int64_t clock, float cutoffHz,
                   float resonance) noexcept SST_CPPUTILS_NONBLOCKING;
    /// The lane's cell from the next beginCell, clamped as BusFilter::setCell clamps.
    void setCell(uint8_t lane, float cutoffHz, float resonance) noexcept;
    /// Owned to Draining: the voice finished or was replaced. Its cell freezes.
    void drain(uint8_t lane) noexcept;
    /// lastEnd = max(lastEnd, end): a grain admitted to the lane ends at `end`.
    void raiseLastEnd(uint8_t lane, int64_t end) noexcept;
    /// Every busy lane's lastEnd becomes t (a clear of the grains at t).
    void endAllAt(int64_t t) noexcept;
    /// Every lane Free and silent; the controller jumps to the request.
    void freeAll() noexcept SST_CPPUTILS_NONBLOCKING;
    /// The free rule at t (F9, FB17; plan decision P4). A Draining lane is due when gridCeil(
    /// lastEnd) <= t, capped when gridCeil(lastEnd + capSamples()) <= t. Capped frees it; due
    /// frees it when not engaged, or when engaged with `grid`, no fade and stateMagnitude below
    /// kLaneFreeLevel. When the last busy lane frees during a fade, the controller jumps to the
    /// request.
    void freeFinished(int64_t t, bool grid, bool engaged) noexcept SST_CPPUTILS_NONBLOCKING;

    // --- Observers ---

    bool anyBusy() const noexcept { return busy_ != 0; }
    LaneState state(uint8_t lane) const noexcept;
    uint32_t owner(uint8_t lane) const noexcept;
    int64_t lastEnd(uint8_t lane) const noexcept;
    /// The largest |ic1eq| or |ic2eq| of the lane's active filter over its four channels (FB11).
    float stateMagnitude(uint8_t lane) const noexcept;
    /// llround(kLaneCapSeconds × sample rate).
    int64_t capSamples() const noexcept { return capSamples_; }
    /// min(20000, 0.45 × sample rate) Hz.
    float cutoffMax() const noexcept { return cutoffMax_; }

    // --- The render (FB5; plan decision P6) ---

    /// At a cell's start: every busy lane's active filter, and its spare during a fade, takes
    /// its cell's coefficients for a 32-sample glide (setCoeffForBlock<32>) for a type not Off.
    void beginCell() noexcept SST_CPPUTILS_NONBLOCKING;
    /// Writes the busy lanes in ascending owner ordinal to `order`; returns how many.
    uint32_t busyByOwner(std::span<uint8_t, kVoiceLanes> order) const noexcept;
    /// The lane's channel (0 to 3) scratch: kLaneFrames floats, filtered in place.
    float *scratch(uint8_t lane, uint32_t channel) noexcept;
    /// Zeroes the lane's four channels' first n frames.
    void zero(uint8_t lane, uint32_t n) noexcept;
    /// Filters the lane's first n (<= kLaneFrames) frames in place, four channels, from the
    /// controller's current fade position; a fade that ends inside them ends there. The
    /// controller doesn't move: advance does, once every lane is filtered.
    void filter(uint8_t lane, uint32_t n) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Moves the controller's fade on by n frames, swapping the roles when the fade ends.
    void advance(uint32_t n) noexcept;

  private:
    using Svf = sst::filters::CytomicSVF;

    struct Lane
    {
        std::array<Svf, 2> filter{};
        int64_t lastEnd{0};
        uint32_t owner{0};
        float cutoff{20000.0f};
        float res{0.0f};
        LaneState state{LaneState::Free};
    };

    static Svf::Mode modeOf(VoiceFilterType type) noexcept;
    static void silence(Svf &f) noexcept;
    void setCoeffs(Svf &f, const Lane &lane, VoiceFilterType type) noexcept;
    void freeLane(Lane &lane) noexcept;
    void jumpToRequest() noexcept;

    std::array<Lane, kVoiceLanes> lanes_{};
    alignas(16) float buf_[kVoiceLanes][4][kLaneFrames]{};
    uint32_t busy_{0};
    // lanes_[l].filter[active_] runs type_; during a fade filter[1 - active_] runs nextType_.
    uint8_t active_{0};
    VoiceFilterType type_{VoiceFilterType::Off};
    VoiceFilterType nextType_{VoiceFilterType::Off};
    VoiceFilterType requested_{VoiceFilterType::Off};
    bool fading_{false};
    uint32_t fadePos_{0};
    uint32_t rampSamples_{1};
    int64_t capSamples_{192000};
    float cutoffMax_{20000.0f};
    float srInv_{1.0f / 48000.0f};
};
} // namespace gg

#endif // GG_ENGINE_SRC_VOICEFILTERBANK_H
