// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_PROCESSOR_H
#define GG_CORE_PROCESSOR_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <sst/cpputils/rtsan_support.h>

#include "gg/core/Diffuser.h"
#include "gg/core/GrainQueue.h"
#include "gg/core/LivePeaks.h"
#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "gg/core/SourceExchange.h"
#include "gg/core/SpaceReverb.h"
#include "gg/core/UiSnapshot.h"
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

/// What both plugins and gg-render run: the instance, real-time safe (spec §5, §6.1–§6.4; M2
/// sub-project 1's spec §6.2–§6.4): four layers, A to D, each a grain Engine and its wet chain,
/// summed by the morph law, then Space and the mix; the parameters and the held notes.
///
/// **Layers** (M2 sub-project 1's spec §6.2, §6.3). Each layer owns an Engine (EngineConfig::layer
/// its index, a kLiveSeconds live ring in both products, the note gate on in the instrument), a
/// Diffuser, its ramps (Level, Diffusion, Feedback and its on/off ramp), its wet and ring-input
/// buffers, its limited history for Feedback, a SourceExchange for its sample, a LivePeaks strip
/// and a fixed buffer for its grain events. Per piece, layer L renders
/// ```
/// ring_L[t] = sanitize(in[t]) + fb_L(t)·limited_L[t − D]
/// w'_L      = δ_L > 0 ? w_L + δ_L·(diffuse_L(w_L) − w_L) : w_L     (w_L its engine's output)
/// limited_L = softLimit(w'_L · g_L)
/// wet_L     = limited_L · weight_L(t)
/// ```
/// and wet = wet_A ⊕ wet_B ⊕ wet_C ⊕ wet_D: the first rendering layer in order A to D assigns,
/// each later one adds, and with none rendering wet is +0. The weight is the morph law (D1):
/// with x and y the ramped Morph X and Morph Y clamped to [0.001, 0.999], b_A = (1 − x)(1 − y),
/// b_B = x(1 − y), b_C = (1 − x)y, b_D = xy, r_L the layer's on/off ramp (0 for a layer not
/// rendering) and s = Σ r_K·b_K, weight_L = s > 0 ? r_L·b_L / s : 0. One layer rendering with its
/// ramp at 1 weighs exactly 1.0f wherever the pad is, so layer A alone renders M1's chain bit for
/// bit.
///
/// A layer's Layer On switch ramps r_L over kRampSeconds. A layer renders while its ramp's
/// target or value is non-zero (layerRendering). Once the ramp is down at 0 the layer is off: its
/// engine is disabled (Engine::setEnabled: its grains end, none spawn), its diffuser and Feedback
/// history are cleared, and only its ring is written, so it reads recent audio when it comes back.
/// The piece is cut at the sample the ramp reaches 0, so the layer stops rendering there however
/// the host splits blocks. Switched on again, the engine is enabled at that boundary, which forces
/// an onset there, and the ramp rises from 0. Switched off and on again within the ramp, the
/// ramp turns from its current value and the engine is never disabled.
///
/// **Source kind** (spec §6.4). Each layer reads the kind builtKind(sourceKindOf(...)) gives:
/// Sample or Live, in either product. Position, Spray and Feedback follow the kind (toCloudParams,
/// toWetParams). A change applies at its boundary (Engine::setSourceMode): the grains sounding
/// finish on the source they started on.
///
/// **Grain events** (spec §6.7). Each layer's engine records its spawns into its own fixed buffer
/// of 2 × grainCapacity events while it renders a piece; after the piece's layers have rendered,
/// the buffers are drained in layer order A to D into the sink (setEventSink) and, while
/// publishing, the grain queue. So the sink sees the grains in piece, layer, spawn order: for one
/// layer that is spawn order. A buffer that is full drops the event from the sink and the queue
/// (never from the audio) and counts it (droppedGrainEvents); no schedule the engine admits fills
/// it within one piece.
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
/// - every layer's engine gets setCloudParams(toCloudParams(..., layer)), its source kind (when it
///   changed), setHeldKeys with the distinct held keys (when the group had notes) and its freeze
///   setting (below), which apply from b;
/// - Mix, Reverb Amount, Dry to Reverb, Morph X and Morph Y, and each layer's Level, Diffusion,
///   Feedback and on/off ramp from their current values to their new ones.
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
/// boundary (Engine::setHeldKeys). Every layer's engine gets the keys. The instrument's engines
/// have the note gate on (Engine::setNoteGate): no grain spawns while nothing is held. Construction
/// and reset() clear the held set.
///
/// **Freeze is live until toggled** (INTENT §14; ruling M1-ac), per layer: each layer's Freeze
/// engages its own engine's, and is armed on its own. The ring isn't saved (an M2
/// non-goal), so a Freeze that is on when a fresh ring starts would hold that ring's first 20 ms,
/// usually silence. So the parameter engages the engine's freeze only once armed, and it is armed
/// by being seen off after the construction (an activation) or the last reset(): until then an
/// effective Freeze that is on leaves the engine live, though base() and effective() still read
/// it on. Freeze is seen at the points where it reaches the engine: each call's first sample,
/// each applied boundary, and a setBase() of a layer's Freeze or of Macro 6 (Macro Freeze, which
/// adds to layer A's; params.flush or a state load).
/// Construction and reset() disarm it, and the reset itself doesn't arm: after a reset (a VST3
/// suspend, say), a Freeze turned on before audio flows stays live until it is toggled, while one
/// that is off at the next call's first sample arms. Modulation that takes the effective Freeze
/// off and back on counts as a toggle. A boundary's events apply as one group, so off and on
/// again at one boundary is no transition. Inside process() Freeze changes only at boundaries,
/// and a call's first sample sees what the last boundary (or setBase() or reset()) left, so the
/// transitions fall on the same absolute samples however the host splits blocks. Once engaged,
/// Freeze behaves as before: the 20 ms ramp, then the head stops.
///
/// **Notes freeze the FX** (spec §6.3). In the FX a layer's engine's freeze is on while its
/// effective Freeze is on and armed, as above, **or** any key is held: a held key freezes every
/// layer. A note-on needs no arming: it
/// freezes even right after a reset or an activation. Notes never change Freeze's base(),
/// effective() or arming. Releasing every note unfreezes the ring unless the parameter is
/// engaged; the freeze changes at the same boundaries as the held set.
///
/// **The sample source** (spec §6.4), per layer. The plugin's main thread offers prepared sources
/// through sources(layer) (sources() is layer A's), each with the parameter values of a session
/// restored while active (D8), if any. Each process() call starts with takeOffers(), and ends by
/// handing back what retired during it. takeOffers() does, for each layer in turn: hands its
/// engine's retired sources back to its sources() (Engine::takeRetiredSource →
/// SourceExchange::retire), polls for a new offer, applies its values at once (as setBase does),
/// and holds its source until Engine::adoptSource accepts it: at once, unless two sources are still
/// in play, and then at the start of a later call, within Size. So an offer's values apply from the
/// first call that takes it, and its source from that call too, or, when two sources are already in
/// play, up to Size later. An offer that keeps the source (SourceExchange::offerValues, ruling R19)
/// applies its values and changes no source. A newer offer replaces a source still held, which goes
/// straight back. So, as each call has taken its offers, the exchange and the engine hold at most
/// three sources: the one draining, the one playing and one waiting. Between calls a fourth can
/// exist for up to one block: an offer made while one waits for adoption, until the next call takes
/// it and hands the waiting one back. The swap applies at a call's start, not on the control grid:
/// where it falls depends on when the main thread offered it, so only a source given before the
/// first call (setSourceNow, or an offer before it) is part of a block-size-invariant render.
///
/// **The ramped values.** Mix, Reverb Amount, Dry to Reverb, Morph X, Morph Y and each layer's
/// Level, Diffusion, Feedback and on/off ramp linearly to each new target over kRampSeconds,
/// round(0.01 × sr) samples: R samples after its boundary a ramp is exactly at its target. A ramp
/// is a function of the absolute sample, so it is block-size invariant too. Level ramps its linear
/// gain. setBase() and reset() jump them.
///
/// **The wet chain** (spec §6.4; sub-project 4's spec §6.1). Per sample, with wet the layers' sum
/// (above: each layer's diffuser, which runs every sample the layer renders, Level gain and soft
/// limiter, times its weight), d the FX's dry input, and m, a and s the ramped Mix, Amount and Dry
/// to Reverb:
/// ```
/// FX:         rIn = m·wet + s·d    out = d·(1 − m) + wet·m   [+ softLimit(a·r)]
/// instrument: rIn = wet            out = wet                 [+ softLimit(a·r)]
/// ```
/// r is the SpaceReverb's return for rIn, 32 samples late. The bracket is added only on samples
/// of an active reverb chunk (below); otherwise out is computed exactly as before Space, so with
/// Space at its defaults the output is bit for bit what it was, the sign of zero included. The
/// FX's dry signal is its input with every non-finite sample counted as 0 and every finite one
/// used unchanged (ruling M1-q), so the output is finite whatever the input. At Mix 0 a normal
/// input sample comes out bit for bit while the reverb is idle. |out| ≤ 2 + |d|. With one output
/// channel, each engine's mono render is diffused as both sides, the reverb hears that channel's
/// rIn on both sides, and the channel gets r's left. The instrument's input feeds its layers'
/// rings alone: it has no dry path.
///
/// **Feedback** (sub-project 5's spec §5), per layer. Each engine writes its live ring from
/// ```
/// ring_L[t] = sanitize(in[t]) + fb_L(t)·limited_L[t − D]    (limited = 0 before the first sample)
/// ```
/// with sanitize(x) = x when x is finite and 0 otherwise (so sanitize(in) is d above), fb_L the
/// layer's ramped Feedback, limited_L its wet signal above (after Level and the limiter, before
/// the weight and the mix; per channel, so with one output channel the right side is the diffused
/// mono render's) and D = kFeedbackDelay, the ring's head margin. The ring stores the sum as it
/// stores any input (non-finite as 0, clamped to ±2^20). The last D samples of limited are kept by
/// absolute time, and both products render in pieces of at most D samples, so the output is
/// block-size invariant. A sample where fb is 0 writes d alone, and a piece where fb is 0
/// throughout hands the engine the input itself, as before Feedback, so Feedback 0 renders bit
/// for bit as it did, the sign of zero included. Frozen, the ring writes nothing, so Feedback
/// regenerates nothing (Freeze wins). A layer that reads a sample has no ring to feed: its
/// Feedback is 0 (toWetParams), in either product.
///
/// **The reverb's grid.** SpaceReverb's chunks are the control grid's cells: its time t is the
/// absolute sample (Engine::sampleClock), which construction and reset() start at 0 with the
/// reverb's own, so every chunk starts on a grid boundary, where the parameters change. Its
/// other values (Decay, Size, Damping, Pre-delay, Modulation) are the effective values, which it
/// latches at each chunk's first sample. A chunk is active when the Amount ramp isn't exactly 0
/// at its first sample: Amount 0 keeps the reverb idle (SpaceReverb's idle rule).
///
/// **Publishing** (sub-project 5's spec §6.4). process() fills three lock-free structures the
/// editor draws from, without allocating, locking or waiting; the first two only while
/// setPublishing(true):
/// - grainQueue(): a GrainVis per spawned grain, its layer included, in the sink's order (above);
///   when the editor falls behind the queue drops the newest and counts them
///   (GrainQueue::dropped).
/// - snapshot(): a UiSnapshot at the end of every call (0-frame calls included), layer A's view
///   (its ring's head, freeze, Position, Spray and Feedback) with the grains of every layer, and
///   publishedClock(), the sample clock it carries, stored after it with release order.
/// - livePeaks(layer), each layer's, in both products, always, publishing or not (ruling
///   PU-T9b), so the strip follows the ring
///   itself and an editor that opens late finds the ring's recent audio there, not stale peaks:
///   the peaks of the ring input, the exact buffers the engine was handed (the input, or with
///   Feedback the input plus the fed-back wet signal), indexed by ring time: the frames the
///   ring's write head advanced over, so a freeze, which stops the head, writes none, and a
///   grain's startPos reads its own frames' peaks. Taken before the freeze ramp's write gain and
///   the ring's clamp. reset() silences it with the ring.
/// process() reads the flag once, at its start, and holds it for the whole call. With it off
/// nothing is queued or snapshotted, and with it on or off the audio is the same, bit for bit.
class Processor
{
  public:
    /// The control grid, in samples (spec §6.1).
    static constexpr uint32_t kControlGrid = 32;
    /// The layers, A to D (M2 sub-project 1's spec §6.2).
    static constexpr uint8_t kLayers = gg::core::kLayers;
    /// Every layer's live ring length, in seconds, in both products (spec §6.3; M2 sub-project 1's
    /// spec §6.2).
    static constexpr double kLiveSeconds = 16.0;
    /// The ramp time of every ramped value (the class comment), in seconds; rounded to samples, at
    /// least 1 (spec §6.1).
    static constexpr double kRampSeconds = 0.01;
    /// D, Feedback's delay in samples (sub-project 5's spec §5): the ring's frame t gets
    /// fb(t) × the limited wet signal of sample t − D. Both products render in pieces of at most
    /// D samples, so each piece's feedback is already rendered when an engine writes the piece's
    /// input into its ring, before any grain reads it.
    static constexpr uint32_t kFeedbackDelay = 256;

    /// Allocates the four layers (each an engine with a kLiveSeconds live ring, in both products,
    /// reading the kind its Source gives: Live in the FX and Sample, with no source, in the
    /// instrument by default; its wet scratch buffers, its diffuser's lines, its peak strip and its
    /// grain-event buffer) and the reverb (about 14.5 MB): about 45 MB at 48 kHz. Every parameter
    /// starts at its default with no modulation, so layer A is on and B to D are off. Throws
    /// std::invalid_argument for the configurations Engine rejects (sampleRate not finite and > 0,
    /// maxBlockSize 0).
    ///
    /// It reads every slot's metadata (paramMeta), whose first call allocates, so process()
    /// never makes that first call. Freeze starts disengaged, as the class comment describes.
    explicit Processor(const ProcessorConfig &config);

    /// Sets a base value at once, outside the control grid: for params.flush outside process()
    /// and for a state load. The value is clamped to the slot's range; NaN is ignored. A pending
    /// base change to the same slot is dropped, so this, the latest value, wins. The ramped
    /// values (Mix, Reverb Amount, Dry to Reverb, Morph X and Morph Y, and each layer's Level,
    /// Diffusion, Feedback and Layer On) jump to the new value without a ramp; a macro makes each
    /// ramped value whose effective value it moved jump (Macro 5 Diffusion, Macro 7 Feedback, the
    /// FX's Macro 8 Mix, the instrument's Macro 8 Level), and so does a layer's Source for its
    /// Feedback (0 on a Sample layer). The engines get the new parameters, and a new source kind,
    /// from the next process() call's first sample. Space's latched values (Decay, Size, Damping,
    /// Pre-delay and Modulation), and whether Reverb Amount keeps the reverb active, take effect
    /// at the next reverb chunk's first sample, the next grid boundary. Real-time safe; call it
    /// between process() calls.
    void setBase(ParamSlot slot, float value) noexcept;

    /// Sets every layer's engine seed (Engine::setSeed): the grains scheduled from the next
    /// process() call's first sample on are keyed by it; those already sounding keep their draws.
    /// For a state loaded while active (sub-project 5's spec §7). Real-time safe; call it between
    /// process() calls.
    void setSeed(uint64_t seed) noexcept;
    /// The engine seed in effect: ProcessorConfig::seed, or the last setSeed().
    uint64_t seed() const noexcept { return config_.seed; }

    /// The slot's base value in effect: the last setBase() or applied event. `slot` must be a
    /// parameter: ParamSlot::Count asserts (and in release builds reads the last parameter).
    float base(ParamSlot slot) const noexcept;
    /// The slot's effective value in effect: clamp(base + modulation + macro offset), as the
    /// class comment describes. `slot` as for base().
    float effective(ParamSlot slot) const noexcept;

    /// Renders numFrames frames into out, applying `events` and `notes` on the control grid as
    /// the class comment describes.
    ///
    /// - Input: in[c] is channel c's numFrames samples. A mono input (numInChannels 1) feeds
    ///   both channels; in == nullptr, numInChannels 0 or a null channel pointer is silence. It
    ///   feeds every layer's ring in both products, and the dry side of the mix in the FX alone.
    /// - Output: channels 0 and 1 carry the signal (one channel: the engines' unpanned mono and
    ///   the input's channel 0); further channels are zeroed. out == nullptr, numOutChannels 0
    ///   or a null channel pointer writes nothing for that channel, but the engines and Space
    ///   still run (with no output channel at all, on the engines' stereo render), so the next
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
    /// tests. Layer `layer`'s sources(layer) holds `s` (SourceExchange::hold), and its engine
    /// reads it from the next onset: it replaces the source as Engine::setPreparedSource does, so
    /// grains still sounding go on in `s` too; nullptr means no source. Once a later offer
    /// replaces it and its grains end, it goes back to sources(layer) and is freed there, as an
    /// offered source is. It supersedes every source offered to that layer so far, taken or not
    /// (an untaken offer's values still apply): each one, and each the engine hands back, goes
    /// back to sources(layer) and is freed there (SourceExchange::collect), on the calling thread.
    /// `layer` is below kLayers.
    void setSourceNow(uint8_t layer, std::shared_ptr<const PreparedSource> s);
    /// Layer A's: setSourceNow(0, s).
    void setSourceNow(std::shared_ptr<const PreparedSource> s) { setSourceNow(0, std::move(s)); }

    /// Real-time safe; the start of every process() call, and for the plugin's params.flush: for
    /// each layer in turn, hands its engine's retired sources back to its sources(), takes a new
    /// offer from it, applies the offer's values (as setBase does), and has the engine adopt the
    /// source waiting, if it can. Returns whether an offer's values were applied. Call it on the
    /// audio thread between process() calls, or on another while process() isn't running.
    bool takeOffers() noexcept SST_CPPUTILS_NONBLOCKING;

    /// The hand-off through which the plugin's main thread offers layer `layer`'s sources to
    /// process() (the class comment); `layer` below kLayers. Its main-thread calls (offer,
    /// collect, held) may run while process() does. sources() is layer A's.
    SourceExchange &sources(uint8_t layer) noexcept { return layerAt(layer).sources; }
    const SourceExchange &sources(uint8_t layer) const noexcept { return layerAt(layer).sources; }
    SourceExchange &sources() noexcept { return sources(0); }
    const SourceExchange &sources() const noexcept { return sources(0); }

    /// Restarts every layer's engine (Engine::reset: the clock, the schedule and the ring time at
    /// 0), and clears the diffusers and the reverb, whose time restarts at 0 with the engines':
    /// clearing the reverb's ~14.5 MB at once is a cost hosts expect of a reset (ruling R-T5b). A
    /// pending change applies at once, and the ramped values jump to their targets. Feedback's
    /// histories are cleared: a fresh ring has no wet signal behind it to take back; the live
    /// peaks are silenced with the rings (LivePeaks::reset). Every note is released, the pending
    /// ones included: heldKeys() is empty and the engines get no keys. Freeze disengages and waits
    /// for an off→on transition (the class comment). Real-time safe; call it between process()
    /// calls.
    void reset() noexcept;

    /// The grain-event sink (gg-render's event log, tests): called on the audio thread once per
    /// spawned grain with its layer, in piece, layer, spawn order (the class comment), whether or
    /// not publishing is on. nullptr removes it. Call it while process() isn't running.
    void setEventSink(GrainEventSink sink, void *ctx) noexcept;

    /// Turns publishing on or off (the class comment): the editor's, on the main thread, as it
    /// opens and closes. It takes effect from the next process() call's start. Turning it off
    /// leaves the grain queue as it is: what it holds then is stale, so the consumer clears it
    /// (GrainQueue::clear) when it starts reading a queue (the canvas, on the first frame that
    /// finds a queue it hasn't read), rather than draw that backlog.
    void setPublishing(bool on) noexcept { publishing_.store(on, std::memory_order_relaxed); }
    /// The spawned grains, for the editor (the queue's consumer, which clears it on opening:
    /// setPublishing).
    GrainQueue &grainQueue() noexcept { return *grainQueue_; }
    /// The latest UiSnapshot, for the editor (the buffer's consumer).
    SnapshotBuffer &snapshot() noexcept { return snapshot_; }
    /// The peak strip of layer `layer`'s live ring (`layer` below kLayers), kLiveSeconds long
    /// (ceil(kLiveSeconds × sampleRate) frames), written on every call whether or not publishing
    /// is on, and whether or not the layer is on (the class comment), in both products. Any
    /// thread may read it. livePeaks() is layer A's.
    const LivePeaks &livePeaks(uint8_t layer) const noexcept { return layerAt(layer).peaks; }
    const LivePeaks &livePeaks() const noexcept { return livePeaks(0); }
    /// The sample clock at the end of the last process() call that published (0 until one has),
    /// stored with release order after that call's snapshot.
    const std::atomic<int64_t> &publishedClock() const noexcept { return publishedClock_; }

    /// Layer `layer`'s engine (`layer` below kLayers); engine() is layer A's.
    const Engine &engine(uint8_t layer) const noexcept { return layerAt(layer).engine; }
    const Engine &engine() const noexcept { return engine(0); }

    /// Layer `layer`'s ramped Feedback at the absolute sample t, as its ring input uses it (for
    /// tests and the UI snapshot): exact only for t at or after the last boundary applied.
    /// feedbackAt(t) is layer A's.
    float feedbackAt(uint8_t layer, int64_t t) const noexcept
    {
        return layerAt(layer).feedback.at(t, rampSamples_);
    }
    float feedbackAt(int64_t t) const noexcept { return feedbackAt(0, t); }

    /// Whether layer `layer` renders (the class comment): its on/off ramp's target or its value at
    /// the current sample clock is non-zero.
    bool layerRendering(uint8_t layer) const noexcept;

    /// Layer `layer`'s morph weight at the absolute sample t (the class comment), for tests: exact
    /// only for t at or after the last boundary applied.
    float morphWeightAt(uint8_t layer, int64_t t) const noexcept;

    /// The grain events a full layer buffer kept from the sink and the grain queue (the class
    /// comment) since construction or the last reset(); the audio is never affected.
    uint64_t droppedGrainEvents() const noexcept { return droppedEvents_; }

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

    /// One layer (M2 sub-project 1's spec §6.2): its engine and its whole wet chain.
    struct Layer
    {
        explicit Layer(const EngineConfig &config);

        // Before engine, so destroyed after it: it frees the sources the engine may still point
        // to.
        SourceExchange sources;
        // A source polled from sources that the engine hasn't adopted yet (nullptr: no source),
        // if hasPendingSource.
        const PreparedSource *pendingSource{nullptr};
        bool hasPendingSource{false};
        Engine engine;
        Diffuser diffuser;
        Ramp level; // the linear gain
        Ramp diffusion;
        Ramp feedback; // 0 unless the layer reads its ring
        Ramp on;       // r_L: 1 on, 0 off
        // The engine's output, then wet_L in place: pieceMax frames each.
        std::array<std::vector<float>, 2> wet;
        // The ring input while Feedback acts: pieceMax frames each.
        std::array<std::vector<float>, 2> ringIn;
        // The limited wet signal of the last kFeedbackDelay samples: sample t at t % D.
        std::array<std::array<float, kFeedbackDelay>, 2> wetHistory{};
        LivePeaks peaks;
        // This piece's spawns, drained after the piece: reserved to 2 × grainCapacity.
        std::vector<GrainSpec> events;
        // Freeze has been seen off since the construction or the last reset(), so on engages it;
        // engineFrozen is the freeze setting last given to the engine.
        bool freezeArmed{false};
        bool engineFrozen{false};
        // The enable gate last given to the engine (Engine::setEnabled), and the kind.
        bool enabledGiven{true};
        SourceMode kind{SourceMode::Sample};
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

    Layer &layerAt(uint8_t layer) noexcept
    {
        return *layers_[std::min<size_t>(layer, kLayers - 1)];
    }
    const Layer &layerAt(uint8_t layer) const noexcept
    {
        return *layers_[std::min<size_t>(layer, kLayers - 1)];
    }

    /// The engines' grain-event sink, ctx the Processor: appends the grain to its layer's buffer
    /// (drainEvents empties it), or counts it when the buffer is full.
    static void onGrain(void *ctx, uint32_t layer,
                        const GrainSpec &g) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Hands each buffered grain event, layer by layer A to D, to the sink set by setEventSink
    /// and, while this call publishes, the grain queue; then empties the buffers.
    void drainEvents() noexcept SST_CPPUTILS_NONBLOCKING;
    /// takeOffers() for one layer.
    bool takeLayerOffers(Layer &l) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Hands every source the layer's engine has retired back to its sources
    /// (SourceExchange::retire).
    static void handBackRetired(Layer &l) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Renders the absolute samples [from, to) of the current call.
    void render(const Buffers &b, int64_t from, int64_t to) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Renders layer L's piece [from, from + n) into its wet buffer (the class comment), or, when
    /// it isn't rendering, only writes its ring.
    void renderLayerPiece(uint8_t L, int64_t from, uint32_t n, const float *const dry[2],
                          bool hasInput, uint32_t rendering) noexcept SST_CPPUTILS_NONBLOCKING;
    /// The morph weights of the four layers at sample t, with r_K the on/off ramps' values where
    /// rendering[K] and 0 elsewhere.
    void weightsAt(int64_t t, const std::array<bool, kLayers> &rendering,
                   std::array<float, kLayers> &weights) const noexcept;
    /// Moves the pending changes into the base and modulation values, and the pending notes'
    /// held set into effect.
    void commitPending() noexcept;
    /// Recomputes the effective values, gives each engine its cloud parameters and source kind,
    /// and keeps Space's.
    void updateEffective() noexcept;
    /// Jumps every ramped value to its target.
    void jumpRamps() noexcept;
    /// Ramps every ramped value from its value at t to its target.
    void retargetRamps(int64_t t) noexcept;
    /// Gives each engine its freeze setting: its layer's effective Freeze, once armed, or in the
    /// FX any held key. An effective Freeze that is off arms it.
    void syncFreeze() noexcept;
    /// Applies one note event, whose fields are in range, to held_.
    void applyNote(const NoteEvent &n) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Makes held_ the held set in effect: rebuilds keys_ and gives every engine its keys.
    void updateKeys() noexcept SST_CPPUTILS_NONBLOCKING;
    float clampToRange(size_t slot, float value) const noexcept;

    ProcessorConfig config_;
    int64_t rampSamples_;
    // The longest piece: min(maxBlockSize, kFeedbackDelay), in both products.
    uint32_t pieceMax_;
    std::array<std::unique_ptr<Layer>, kLayers> layers_;
    // The current piece's: which layers render, and their weights per sample.
    std::array<bool, kLayers> rendering_{};
    std::array<std::array<float, kFeedbackDelay>, kLayers> weights_{};

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

    // The held set: one channel bit per key. Pending notes apply to it as they arrive, in
    // order; hasPendingNotes_ says some have since the last applied boundary. keys_ is the set
    // in effect, as of that boundary: the distinct keys, ascending.
    std::array<uint16_t, 128> held_{};
    bool hasPendingNotes_{false};
    std::array<uint8_t, 128> keys_{};
    size_t keyCount_{0};

    Ramp mix_;
    Ramp amount_;
    Ramp drySend_; // always 0 in the instrument
    Ramp morphX_;
    Ramp morphY_;

    // Publishing (the class comment). The external sink is setEventSink's; publishingThisCall_
    // is publishing_ as the current process() call read it at its start.
    GrainEventSink externalSink_{nullptr};
    void *externalSinkCtx_{nullptr};
    std::atomic<bool> publishing_{false};
    bool publishingThisCall_{false};
    uint64_t droppedEvents_{0};
    std::unique_ptr<GrainQueue> grainQueue_; // heap: 128 KB
    SnapshotBuffer snapshot_;
    std::atomic<int64_t> publishedClock_{0};
    static_assert(std::atomic<bool>::is_always_lock_free);
    static_assert(std::atomic<int64_t>::is_always_lock_free);

    // Space (sub-project 4's spec §6.1): the effective values in SpaceReverb's terms, and the
    // reverb (heap: about 14.5 MB, 16-byte aligned).
    SpaceParams space_{};
    std::unique_ptr<SpaceReverb> reverb_;
};
} // namespace gg::core

#endif // GG_CORE_PROCESSOR_H
