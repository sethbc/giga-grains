// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_PROCESSOR_H
#define GG_CORE_PROCESSOR_H

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <sst/cpputils/rtsan_support.h>

#include "gg/core/Diffuser.h"
#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "gg/core/SourceExchange.h"
#include "gg/core/SpaceReverb.h"
#include "gg/engine/Engine.h"

namespace gg::core
{
struct ProcessorConfig
{
    double sampleRate;     ///< Finite and > 0.
    uint32_t maxBlockSize; ///< > 0. process() renders longer calls in pieces of this size.
    uint64_t seed;         ///< The engine seed (saved in the state).
    Mode mode;             ///< Which product (gg/core/ParamIds.h).
};

/// A parameter change inside one process() call: a value (CLAP's param value event) or a
/// modulation offset (CLAP's param mod event).
struct ParamEvent
{
    uint32_t offset;   ///< Frames from the start of the call. A call's events are sorted by it.
    ParamSlot slot;    ///< Events for ParamSlot::Count are ignored.
    bool isModulation; ///< An offset added to the base value, rather than the base value itself.
    double value;      ///< In the parameter's units (paramMeta). Converted to float on receipt.
};

enum class NoteEventType : uint8_t
{
    On,     ///< Holds (channel, key).
    Off,    ///< Releases the held pairs that channel and key match; −1 matches any.
    AllOff, ///< Releases every key held on its channel, as MIDI CC 123 does (ruling R14); −1
            ///< releases everything. The key is ignored.
};

/// A note event inside one process() call (spec §6.1). Velocity is ignored (D6), so it has none.
struct NoteEvent
{
    uint32_t offset; ///< Frames from the start of the call. A call's notes are sorted by it.
    NoteEventType type;
    int16_t channel; ///< 0..15; −1 = any (Off and AllOff only). Other values make it ignored.
    int16_t key;     ///< 0..127; −1 = any (Off only; AllOff ignores it). Other values make the
                     ///< event ignored.
};

/// What both plugins and gg-render run: the grain Engine, its parameters, the held notes and the
/// wet chain, real-time safe (spec §5, §6.1–§6.4).
///
/// **Parameters.** Each slot has a base value and a modulation offset; the effective value,
/// clamp(base + modulation + macro offset) to the slot's range, is what reaches the engine
/// (toCloudParams) and the wet chain (toWetParams). The macro offset is the slot's entry of
/// macroOffsets (sub-project 5's spec §3), from the eight macros' own effective values,
/// clamp(base + modulation); a macro's own offset is 0, and every offset is exactly 0 while the
/// macros rest. One clamp takes the whole sum. Values are floats: event values are converted on
/// receipt.
///
/// **The control grid** (spec §6.1; INTENT §14). An event, a parameter or a note event, at
/// absolute sample a = clock + offset takes effect at the grid boundary b = ceil(a / 32) × 32,
/// aligned to the stream's absolute sample count, not to block starts, so the output doesn't
/// depend on how the host splits blocks. A call's parameter and note events are merged by offset
/// into one sequence, so both kinds join the same boundary groups. Until b an event is pending:
/// per slot, base and modulation apart, the latest parameter event wins, and the note events
/// queue in order. process() renders up to each boundary that has a pending change, then:
/// - the effective values become clamp(base + modulation + macro offset);
/// - the group's note events apply in order to the held set (below);
/// - the engine gets setCloudParams(toCloudParams(...)), setHeldKeys with the distinct held keys
///   (when the group had notes) and its freeze setting (below), which apply from b;
/// - Mix, Level, Diffusion, Reverb Amount and Dry to Reverb ramp from their current values to
///   their new ones.
///
/// A boundary before a call's end applies in that call. One at or past the end carries over to
/// the next call, which renders up to it first: there it merges with that call's events for
/// the same boundary (at offset 0, when the boundary is the call's first sample), so each
/// boundary applies once, with all its events, however the host splits blocks. Every event's
/// boundary is below end + 32, so at most one boundary is pending after a call. base(),
/// effective() and heldKeys() don't show a pending change until its boundary.
///
/// **Notes** (spec §6.1–§6.3). The held set is the held (channel, key) pairs: an On holds its
/// pair, an Off releases the pairs it matches (−1 matches any channel or key), an AllOff releases
/// every key on its channel, or everything for channel −1 (ruling R14). Only the held set after a
/// group counts, so a note that starts and ends within one group does nothing. A key held on two
/// channels counts once, and stays held until both are off. The engine picks each grain's key from
/// the distinct held keys; when the set goes from empty to non-empty the cloud starts at that
/// boundary (Engine::setHeldKeys). The instrument's engine has the note gate on
/// (Engine::setNoteGate): no grain spawns while nothing is held. Construction and reset() clear the
/// held set.
///
/// **Freeze is live until toggled** (INTENT §14; ruling M1-ac). The ring isn't saved (an M2
/// non-goal), so a Freeze that is on when a fresh ring starts would hold that ring's first 20 ms,
/// usually silence. So the parameter engages the engine's freeze only once armed, and it is armed
/// by being seen off after the construction (an activation) or the last reset(): until then an
/// effective Freeze that is on leaves the engine live, though base() and effective() still read
/// it on. Freeze is seen at the points where it reaches the engine: each call's first sample,
/// each applied boundary, and a setBase() of Freeze itself or of Macro 6 (Macro Freeze, which adds
/// to it; params.flush or a state load).
/// Construction and reset() disarm it, and the reset itself doesn't arm: after a reset (a VST3
/// suspend, say), a Freeze turned on before audio flows stays live until it is toggled, while one
/// that is off at the next call's first sample arms. Modulation that takes the effective Freeze
/// off and back on counts as a toggle. A boundary's events apply as one group, so off and on
/// again at one boundary is no transition. Inside process() Freeze changes only at boundaries,
/// and a call's first sample sees what the last boundary (or setBase() or reset()) left, so the
/// transitions fall on the same absolute samples however the host splits blocks. Once engaged,
/// Freeze behaves as before: the 20 ms ramp, then the head stops.
///
/// **Notes freeze the FX** (spec §6.3). In the FX the engine's freeze is on while the effective
/// Freeze is on and armed, as above, **or** any key is held. A note-on needs no arming: it
/// freezes even right after a reset or an activation. Notes never change Freeze's base(),
/// effective() or arming. Releasing every note unfreezes the ring unless the parameter is
/// engaged; the freeze changes at the same boundaries as the held set.
///
/// **The sample source** (spec §6.4). The plugin's main thread offers prepared sources through
/// sources(), each with the parameter values of a session restored while active (D8), if any.
/// Each process() call starts with takeOffers(), and ends by handing back what retired during
/// it. takeOffers() hands the engine's retired sources back to sources()
/// (Engine::takeRetiredSource → SourceExchange::retire), polls for a new offer, applies
/// its values at once (as setBase does), and holds its source until Engine::adoptSource accepts
/// it: at once, unless two sources are still in play, and then at the start of a later call,
/// within Size. So an offer's values apply from the first call that takes it, and its source from
/// that call too, or, when two sources are already in play, up to Size later. An offer that
/// keeps the source (SourceExchange::offerValues, ruling R19) applies its values and changes no
/// source. A newer offer replaces a source still held, which goes straight back. So, as each call
/// has taken its offers, the exchange and the engine hold at most three sources: the one
/// draining, the one playing and one waiting. Between calls a fourth can exist for up to one
/// block: an offer made while one waits for adoption, until the next call takes it and hands the
/// waiting one back. The swap
/// applies at a call's start, not on the control grid: where it falls depends on when the main
/// thread offered it, so only a source given before the first call (setSourceNow, or an offer
/// before it) is part of a block-size-invariant render.
///
/// **The ramped values.** Mix, Level, Diffusion, Reverb Amount and Dry to Reverb ramp linearly
/// to each new target over kRampSeconds, round(0.01 × sr) samples: R samples after its boundary
/// a ramp is exactly at its target. A ramp is a function of the absolute sample, so it is
/// block-size invariant too. Level ramps its linear gain. setBase() and reset() jump them.
///
/// **The wet chain** (spec §6.4; sub-project 4's spec §6.1). Per sample, with w the engine's
/// output, d the FX's dry input, and m, g, δ, a and s the ramped Mix, Level gain, Diffusion,
/// Amount and Dry to Reverb:
/// ```
/// w'  = δ > 0 ? w + δ·(diffuse(w) − w) : w      (the Diffuser, which runs every sample)
/// wet = softLimit(w' · g)
/// FX:         rIn = m·wet + s·d    out = d·(1 − m) + wet·m   [+ softLimit(a·r)]
/// instrument: rIn = wet            out = wet                 [+ softLimit(a·r)]
/// ```
/// r is the SpaceReverb's return for rIn, 32 samples late. The bracket is added only on samples
/// of an active reverb chunk (below); otherwise out is computed exactly as before Space, so with
/// Space at its defaults the output is bit for bit what it was, the sign of zero included. The
/// FX's dry signal is its input with every non-finite sample counted as 0 and every finite one
/// used unchanged (ruling M1-q), so the output is finite whatever the input. At Mix 0 a normal
/// input sample comes out bit for bit while the reverb is idle. |out| ≤ 2 + |d|. With one output
/// channel, the engine's mono render is diffused as both sides, the reverb hears that channel's
/// rIn on both sides, and the channel gets r's left.
///
/// **The reverb's grid.** SpaceReverb's chunks are the control grid's cells: its time t is the
/// absolute sample (Engine::sampleClock), which construction and reset() start at 0 with the
/// reverb's own, so every chunk starts on a grid boundary, where the parameters change. Its
/// other values (Decay, Size, Damping, Pre-delay, Modulation) are the effective values, which it
/// latches at each chunk's first sample. A chunk is active when the Amount ramp isn't exactly 0
/// at its first sample: Amount 0 keeps the reverb idle (SpaceReverb's idle rule).
class Processor
{
  public:
    /// The control grid, in samples (spec §6.1).
    static constexpr uint32_t kControlGrid = 32;
    /// The FX's live ring length, in seconds (spec §6.3).
    static constexpr double kLiveSeconds = 16.0;
    /// The ramp time of Mix, Level, Diffusion, Reverb Amount and Dry to Reverb, in seconds;
    /// rounded to samples, at least 1 (spec §6.1).
    static constexpr double kRampSeconds = 0.01;

    /// Allocates the engine (the FX with a kLiveSeconds live ring read in SourceMode::Live, the
    /// instrument in SourceMode::Sample with no source and no ring), the wet scratch buffers, the
    /// diffuser's lines and the reverb (about 14.5 MB). Every parameter starts at its default with
    /// no modulation. Throws std::invalid_argument for the configurations Engine rejects
    /// (sampleRate not finite and > 0, maxBlockSize 0).
    ///
    /// It reads every slot's metadata (paramMeta), whose first call allocates, so process()
    /// never makes that first call. Freeze starts disengaged, as the class comment describes.
    explicit Processor(const ProcessorConfig &config);

    /// Sets a base value at once, outside the control grid: for params.flush outside process()
    /// and for a state load. The value is clamped to the slot's range; NaN is ignored. A pending
    /// base change to the same slot is dropped, so this, the latest value, wins. The ramped
    /// values (Mix, Level, Diffusion, Reverb Amount and Dry to Reverb) jump to the new value
    /// without a ramp; a macro makes each ramped value whose effective value it moved jump (Macro
    /// 5 Diffusion, the FX's Macro 8 Mix, the instrument's Macro 8 Level). The engine gets the new
    /// parameters from the next process() call's first sample. Space's latched values (Decay, Size,
    /// Damping, Pre-delay and Modulation), and whether Reverb Amount keeps the reverb active, take
    /// effect at the next reverb chunk's first sample, the next grid boundary. Real-time safe; call
    /// it between process() calls.
    void setBase(ParamSlot slot, float value) noexcept;

    /// The slot's base value in effect: the last setBase() or applied event. `slot` must be a
    /// parameter: ParamSlot::Count asserts (and in release builds reads the last parameter).
    float base(ParamSlot slot) const noexcept;
    /// The slot's effective value in effect: clamp(base + modulation + macro offset), as the
    /// class comment describes. `slot` as for base().
    float effective(ParamSlot slot) const noexcept;

    /// Renders numFrames frames into out, applying `events` and `notes` on the control grid as
    /// the class comment describes.
    ///
    /// - FX input: in[c] is channel c's numFrames samples. A mono input (numInChannels 1) feeds
    ///   both channels; in == nullptr, numInChannels 0 or a null channel pointer is silence. The
    ///   instrument ignores its input.
    /// - Output: channels 0 and 1 carry the signal (one channel: the engine's unpanned mono and
    ///   the input's channel 0); further channels are zeroed. out == nullptr, numOutChannels 0
    ///   or a null channel pointer writes nothing for that channel, but the engine and Space
    ///   still run (with no output channel at all, on the engine's stereo render), so the next
    ///   call continues the same render. Each output sample is written after its
    ///   input samples are read, so out may be in.
    /// - An event whose offset is past numFrames counts as at numFrames. An out-of-order event
    ///   (an offset below the one before it in its span) joins the pending boundary, or the last
    ///   one already applied, rather than going back in time. A parameter event with a NaN value
    ///   is ignored, as is a note event with a field out of range (NoteEvent).
    /// - Bounded work: each event is read once, and a call applies at most
    ///   numFrames / kControlGrid + 2 boundaries, each a fixed amount of work.
    ///
    /// Accepts any numFrames, including 0 and more than maxBlockSize. The whole call runs with
    /// denormals flushed (ScopedDenormalsOff) and restores the caller's floating-point mode.
    void process(const float *const *in, uint32_t numInChannels, float *const *out,
                 uint32_t numOutChannels, uint32_t numFrames, std::span<const ParamEvent> events,
                 std::span<const NoteEvent> notes = {}) noexcept SST_CPPUTILS_NONBLOCKING;

    /// The distinct keys held now, ascending (for tests and the editor): the held set as of the
    /// last applied boundary, so a pending note doesn't show until its boundary.
    std::span<const uint8_t> heldKeys() const noexcept { return {keys_.data(), keyCount_}; }

    /// Not real-time safe: while process() isn't running, for an inactive plugin, gg-render and
    /// tests. sources() holds `s` (SourceExchange::hold), and the engine reads it from the next
    /// onset: it replaces the source as Engine::setPreparedSource does, so grains still sounding
    /// go on in `s` too; nullptr means no source. Once a later offer replaces it and its grains
    /// end, it goes back to sources() and is freed there, as an offered source is. It supersedes
    /// every source offered so far, taken or not (an untaken offer's values still apply): each
    /// one, and each the engine hands back, goes back to sources() and is freed there
    /// (SourceExchange::collect), on the calling thread.
    void setSourceNow(std::shared_ptr<const PreparedSource> s);

    /// Real-time safe; the start of every process() call, and for the plugin's params.flush: hands
    /// the engine's retired sources back to sources(), takes a new offer from it, applies the
    /// offer's values (as setBase does), and has the engine adopt the source waiting, if it can.
    /// Returns whether an offer's values were applied. Call it on the audio thread between
    /// process() calls, or on another while process() isn't running.
    bool takeOffers() noexcept SST_CPPUTILS_NONBLOCKING;

    /// The hand-off through which the plugin's main thread offers sources to process() (the
    /// class comment). Its main-thread calls (offer, collect, held) may run while process() does.
    SourceExchange &sources() noexcept { return sources_; }
    const SourceExchange &sources() const noexcept { return sources_; }

    /// Restarts the engine (Engine::reset: the clock, the schedule and the ring time at 0), and
    /// clears the diffuser and the reverb, whose time restarts at 0 with the engine's: clearing
    /// the reverb's ~14.5 MB at once is a cost hosts expect of a reset (ruling R-T5b). A pending
    /// change applies at once, and the ramped values jump to their targets. Every note is
    /// released, the pending ones included: heldKeys() is empty and the engine gets no keys.
    /// Freeze disengages and waits for an off→on transition (the class comment). Real-time safe;
    /// call it between process() calls.
    void reset() noexcept;

    /// The engine's grain-event sink (Engine::setEventSink).
    void setEventSink(GrainEventSink sink, void *ctx) noexcept;

    const Engine &engine() const noexcept { return engine_; }

    /// Whether the reverb's return is being added: the reverb chunk holding the last rendered
    /// sample is active (the class comment). False after construction and reset().
    bool reverbActive() const noexcept { return reverb_->chunkActive(); }

  private:
    /// A linear ramp from `from` at sample `start` to `to` at start + length, held there.
    struct Ramp
    {
        float from{0.0f};
        float to{0.0f};
        int64_t start{0};

        float at(int64_t t, int64_t length) const noexcept;
        /// Ramps from the value at t to target, from t; unchanged if target is already `to`.
        void retarget(float target, int64_t t, int64_t length) noexcept;
        void jump(float target) noexcept;
    };

    /// One process() call's buffers.
    struct Buffers
    {
        const float *const *in;
        uint32_t numInChannels;
        float *const *out;
        uint32_t numOutChannels;
        int64_t callStart; ///< The absolute sample of the call's first frame.
    };

    /// Hands every source the engine has retired back to sources_ (SourceExchange::retire).
    void handBackRetired() noexcept SST_CPPUTILS_NONBLOCKING;
    /// Renders the absolute samples [from, to) of the current call.
    void render(const Buffers &b, int64_t from, int64_t to) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Moves the pending changes into the base and modulation values, and the pending notes'
    /// held set into effect.
    void commitPending() noexcept;
    /// Recomputes the effective values, gives the engine its cloud parameters and keeps Space's.
    void updateEffective() noexcept;
    /// Jumps every ramped value (Mix, Level, Diffusion, Amount, Dry to Reverb) to its target.
    void jumpRamps() noexcept;
    /// Gives the engine its freeze setting: the effective Freeze, once armed, or in the FX any
    /// held key. An effective Freeze that is off arms it.
    void syncFreeze() noexcept;
    /// Applies one note event, whose fields are in range, to held_.
    void applyNote(const NoteEvent &n) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Makes held_ the held set in effect: rebuilds keys_ and gives the engine its keys.
    void updateKeys() noexcept SST_CPPUTILS_NONBLOCKING;
    float clampToRange(size_t slot, float value) const noexcept;

    ProcessorConfig config_;
    // Before engine_, so destroyed after it: it frees the sources the engine may still point to.
    SourceExchange sources_;
    // A source polled from sources_ that the engine hasn't adopted yet (nullptr: no source), if
    // hasPendingSource_.
    const PreparedSource *pendingSource_{nullptr};
    bool hasPendingSource_{false};
    Engine engine_;
    int64_t rampSamples_;
    std::array<std::vector<float>, 2> wet_; // maxBlockSize frames each

    std::array<float, kParamCount> minVal_{};
    std::array<float, kParamCount> maxVal_{};
    ParamValues base_{};
    ParamValues mod_{};
    ParamValues effective_{};

    // The changes waiting for the boundary pendingAt_, if hasPending_.
    ParamValues pendingBase_{};
    ParamValues pendingMod_{};
    std::array<bool, kParamCount> hasPendingBase_{};
    std::array<bool, kParamCount> hasPendingMod_{};
    int64_t pendingAt_{0};
    bool hasPending_{false};

    // Freeze has been seen off since the construction or the last reset(), so on engages it.
    bool freezeArmed_{false};

    // The held set: one channel bit per key. Pending notes apply to it as they arrive, in
    // order; hasPendingNotes_ says some have since the last applied boundary. keys_ is the set
    // in effect, as of that boundary: the distinct keys, ascending.
    std::array<uint16_t, 128> held_{};
    bool hasPendingNotes_{false};
    std::array<uint8_t, 128> keys_{};
    size_t keyCount_{0};

    Ramp mix_;
    Ramp level_; // the linear gain
    Ramp diffusion_;
    Ramp amount_;
    Ramp drySend_; // always 0 in the instrument

    // Space (sub-project 4's spec §6.1): the effective values in SpaceReverb's terms, the
    // diffuser on the engine's output, and the reverb (heap: about 14.5 MB, 16-byte aligned).
    SpaceParams space_{};
    Diffuser diffuser_;
    std::unique_ptr<SpaceReverb> reverb_;
};
} // namespace gg::core

#endif // GG_CORE_PROCESSOR_H
