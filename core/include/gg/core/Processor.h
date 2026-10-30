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
#include "gg/core/ModMatrix.h"
#include "gg/core/ModSources.h"
#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "gg/core/SourceExchange.h"
#include "gg/core/SpaceReverb.h"
#include "gg/core/UiSnapshot.h"
#include "gg/engine/Engine.h"
#include "gg/engine/TaskRunner.h"
#include "gg/grain/Budget.h"

namespace gg::core
{
struct ProcessorConfig
{
    double sampleRate;     ///< Finite and > 0.
    uint32_t maxBlockSize; ///< > 0. process() renders longer calls in pieces of this size.
    uint64_t seed;         ///< The engine seed (saved in the state).
    Mode mode;             ///< Which product (gg/core/ParamIds.h).
    /// Each layer's EngineConfig::grainCapacity (M2 engine spec §6.6): at most kMaxGrainCapacity.
    /// The plugins keep the default, 4,096 grains a layer.
    uint32_t grainCapacity{4096};
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

/// A note event inside one process() call (spec §6.1). The grains ignore velocity (D6); an On's
/// velocity is the mod matrix's Velocity source (M2 sub-project 1's spec §6.8).
struct NoteEvent
{
    uint32_t offset; ///< Frames from the start of the call. A call's notes are sorted by it.
    NoteEventType type;
    int16_t channel; ///< 0..15; −1 = any (Off and AllOff only). Other values make it ignored.
    int16_t key;     ///< 0..127; −1 = any (Off only; AllOff ignores it). Other values make the
                     ///< event ignored.
    /// On only: 0..1. Clamped to it; NaN counts as 0.
    float velocity{1.0f};
};

/// Which MIDI control a ControlEvent carries (M2 sub-project 1's spec §6.8): CC 1, channel
/// pressure or pitch bend.
enum class ControlKind : uint8_t
{
    ModWheel,
    Aftertouch,
    PitchBend,
};

/// A MIDI control inside one process() call: the mod matrix's Mod Wheel, Aftertouch and Pitch
/// Bend sources (spec §6.8). It joins the control grid as a note event does.
struct ControlEvent
{
    uint32_t offset;  ///< Frames from the start of the call. A call's controls are sorted by it.
    ControlKind kind; ///< Out of range: the event is ignored.
    int16_t channel;  ///< 0..15 or −1 (any); other values make it ignored. Not otherwise read yet.
    float value;      ///< 0..1, clamped to it (pitch bend's centre is 0.5); NaN or ±∞ ignored.
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
/// rendering) and s = Σ r_K·b_K,
/// ```
/// weight_L = s > 0 ? (r_L·b_L / s) × min(1, Σ_K r_K) : 0
/// ```
/// The factor min(1, Σ_K r_K) is 1 whenever at least one whole layer's worth of ramps is on, where
/// the law is D1's sum-normalized vector law; below that (the last rendering layer switched off,
/// or the first switched on) it makes the amplitude sum follow the ramps, so the last layer fades
/// out over its ramp instead of being cut when the ramp reaches 0 (D1 as amended in Task 10's fix
/// round). One layer rendering with its ramp at 1 weighs exactly 1.0f wherever the pad is
/// ((b/b) × min(1, 1)), so layer A alone renders M1's chain bit for bit.
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
/// **Threads** (spec §6.7). Per piece, after the boundary's changes and with every input to the
/// layers fixed (the piece's bounds and input, the channels the engines render, which layers
/// render, the morph weights for every sample of the piece, computed serially into a per-piece
/// buffer, and the ramps, effective values and engines' parameters, which change only between
/// pieces), process() first writes the off layers' rings inline, then hands the rendering layers,
/// in order A to D, to the TaskRunner (setTaskRunner) as one task each: task i renders layer
/// taskLayers_[i]'s piece (execTask), the same layer whatever thread runs it. A piece with one
/// rendering layer renders it inline, with no request; with none there is nothing to run. A task
/// touches only its own layer: its engine, diffuser, ramps' reads, wet, ring-input and history
/// buffers, peak strip, event buffer and dropped-event count; everything else it reads (the piece
/// state, the weights, rendering_, rampSamples_) is read-only until run() returns, and nothing in
/// it calls the host, allocates, locks or draws from a stateful RNG. Each task sets
/// ScopedDenormalsOff on its own thread for its duration, since a pool thread's floating-point mode
/// isn't the audio thread's (and Engine::process's own guard covers only the engine). After the
/// join, on the calling thread, the grain events are drained in layer order A to D and the layers
/// are summed in that order, as the serial loop did, so the output is the same bits with any
/// runner, any pool size and a host that declines (it then loops, SerialRunner's way). The
/// runner's run() must return only once every task has returned, with their writes visible to
/// the caller (a host pool's request_exec, StubPool's barrier).
///
/// **Source kind** (spec §6.4). Each layer reads the kind builtKind(sourceKindOf(...)) gives:
/// Sample or Live, in either product. Position, Spray and Feedback follow the kind (toCloudParams,
/// toWetParams). A change applies at its boundary (Engine::setSourceMode): the grains sounding
/// finish on the source they started on.
///
/// **Grain events** (spec §6.7; M2 engine spec §6.7). Each layer's engine records its spawns and
/// its steals, in the order it reports them (a steal right before the spawn whose admission
/// stole), into its own fixed buffer of 4 × grainCapacity events while it renders a piece; after
/// the piece's layers have rendered, the buffers are drained in layer order A to D: each spawn
/// into the sink (setEventSink) and, while publishing, the grain queue, each steal into the steal
/// sink (setStealSink) alone. So the sinks see the events in piece, layer, report order: for one
/// layer that is the engine's order. A buffer that is full drops the event from the sinks and the
/// queue (never from the audio) and counts it (droppedGrainEvents); no schedule the engine admits
/// fills it within one piece (at most 2 × grainCapacity spawns, each with at most one steal).
///
/// **The quotas** (M2 engine spec §6.4). Budget's effective value, as weight units (budgetUnits),
/// is kept by updateEffective; each piece, once the rendering layers are fixed and before any
/// task runs, each rendering layer's engine gets the quota floor(units / n), n the layers
/// rendering (Engine::setQuota, only when it differs from the one last given), on the calling
/// thread. A layer that isn't rendering keeps its quota and spawns nothing. The rendering set and
/// Budget change only at piece boundaries, so the quotas do too, from state known there: the
/// ladder's decisions are block-size invariant and the same with any runner. loweredGrains(),
/// stolenGrains() and each engine's droppedGrains() count them.
///
/// **Parameters.** Each slot has a base value and a modulation offset; the effective value,
/// clamp(base + modulation + route offset) to the slot's range, is what reaches the engine
/// (toCloudParams) and the wet chain (toWetParams). One clamp takes the whole sum. Values are
/// floats: event values are converted on receipt.
///
/// **The mod matrix** (M2 sub-project 1's spec §6.8). The route offset is the slot's sum of the
/// routes' contributions (applyRoutes over routes(), factoryRoutes(mode) at construction, which
/// are M1's macro recipes bit for bit, so every offset is exactly +0 while the macros rest). Each
/// time the effective values are computed (updateEffective):
/// 1. the eight macros' own effective values, clamp(base + modulation): no route targets a macro;
/// 2. every source's value, per layer (sourceValue): the macros (the same in every layer), each
///    layer's own modulators (two LFOs, two envelopes, two random sources and the step
///    sequencer) as of the last stepped boundary, the MIDI sources as of the last applied
///    boundary (Mod Wheel, Aftertouch and Pitch Bend from the control events, Velocity and Key
///    from the last note-on), the Env Follower and the transport's Beat, Bar and Playing;
/// 3. the route offsets, then every slot's clamp(base + modulation + offset);
/// 4. each layer's engine gets setCloudParams only when its CloudParams changed, bit for bit
///    (the scheduler takes the same values twice as once).
///
/// **The sources' clock.** Every source is stepped exactly once per grid boundary b, in order,
/// as rendering reaches b (after b's group of events is committed, before b's effective values
/// are computed), however the host splits blocks, so they are block-size invariant. At b each
/// layer's modulators step with their controls from the effective values in effect (lfoParams,
/// envParams, randomParams, stepParams; no route targets them), the envelopes' gate being the
/// held set's non-emptiness as of b, the random sources keyed by (seed, layer, 0, ModRandom1 or
/// ModRandom2, step); the Env Follower takes the peak |x| of the instance's input over the cell
/// [b − 32, b) (non-finite samples as 0; the first cell's is 0), scanned before any output is
/// written over it; the transport's values for the cell are taken, then it advances one cell
/// while the host plays. A source's value after stepping at b is its value for the cell
/// [b, b + 32). Construction and reset() reset them all, the transport to none given (the host
/// gives it again with its next call), and set the next boundary to step to 0, so a reset
/// Processor renders as a fresh one.
///
/// **Time-varying routes.** While some route has a source that moves with time alone
/// (isTimeVarying: the per-layer modulators, the Env Follower, the transport's Beat and Bar), a
/// non-zero depth and a valid target (timeVarying(), recomputed by setRoutes), process() applies
/// every grid boundary of the call as a group, an empty one where no event pends: the effective
/// values, the engines' parameters and the ramps' targets follow the sources cell by cell, and
/// pieces end at every cell. Otherwise only the boundaries with pending events are applied, as
/// before; the sources still step at every boundary, so they keep time, and their values reach
/// the effective values at the next applied boundary. No golden has a time-varying route, so the
/// pieces, and the bits, are M1's.
///
/// **The control grid** (spec §6.1; INTENT §14). An event, a parameter or a note event, at
/// absolute sample a = clock + offset takes effect at the grid boundary b = ceil(a / 32) × 32,
/// aligned to the stream's absolute sample count, not to block starts, so the output doesn't
/// depend on how the host splits blocks. A call's parameter, note and control events are merged by
/// offset into one sequence, so every kind joins the same boundary groups. Until b an event is
/// pending: per slot, base and modulation apart, the latest parameter event wins, and the note
/// events queue in order. process() renders up to each boundary that has a pending change, then:
/// - the group's note events apply in order to the held set (below), and its control events and
///   note-ons to the MIDI sources;
/// - the sources step (above), and the effective values become clamp(base + modulation +
///   route offset);
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
/// each applied boundary, and a setBase() of a layer's Freeze or of Macro 6 (Macro Freeze, whose
/// factory route adds to every layer's; params.flush), and once a state load (setBases) has set
/// every value: a load arms from its loaded values alone, every layer alike, so a layer not yet
/// armed whose loaded Freeze is on stays live until toggled (INTENT §23 #40).
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
/// SourceExchange::retire), polls for a new offer, applies its values at once (setBases),
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
    /// grain-event buffer) and the reverb (about 14.5 MB): about 51 MB at 48 kHz (rings 25, pools
    /// 5, reverb 14.5, event buffers 6.3: 4 layers × 4 × 4,096 events × 96 bytes). Every parameter
    /// starts at its default with no modulation, so layer A is on and B to D are off. Throws
    /// std::invalid_argument for the configurations Engine rejects (sampleRate not finite and > 0,
    /// maxBlockSize 0, grainCapacity above kMaxGrainCapacity).
    ///
    /// It reads every slot's metadata (paramMeta), whose first call allocates, so process()
    /// never makes that first call. Freeze starts disengaged, as the class comment describes.
    explicit Processor(const ProcessorConfig &config);

    /// Sets a base value at once, outside the control grid: for params.flush outside process()
    /// and for a state load. The value is clamped to the slot's range; NaN is ignored. A pending
    /// base change to the same slot is dropped, so this, the latest value, wins. The ramped
    /// values (Mix, Reverb Amount, Dry to Reverb, Morph X and Morph Y, and each layer's Level,
    /// Diffusion, Feedback and Layer On) jump to the new value without a ramp; a macro makes each
    /// ramped value whose effective value it moved through its routes jump (with the factory
    /// routes Macro 5 Diffusion, Macro 7 Feedback, the FX's Macro 8 Mix, the instrument's Macro 8
    /// Level, on every layer), and so does a layer's Source for its Feedback (0 on a Sample
    /// layer). The engines get the new parameters, and a new source kind,
    /// from the next process() call's first sample. Space's latched values (Decay, Size, Damping,
    /// Pre-delay and Modulation), and whether Reverb Amount keeps the reverb active, take effect
    /// at the next reverb chunk's first sample, the next grid boundary. Real-time safe; call it
    /// between process() calls.
    void setBase(ParamSlot slot, float value) noexcept;
    /// Sets every slot's base value at once: for a state load (ruling FR-3). The result is that
    /// of setBase() on every slot in slot order, bases, effective values, ramps and engines
    /// alike, but the effective values are recomputed once, not once per slot. Freeze arms from
    /// the loaded values alone, every layer alike: a layer whose loaded Freeze is off arms, and
    /// one whose Freeze is on stays live until toggled if not yet armed (a fresh ring, or one
    /// since the last reset()), while one already armed freezes at once (INTENT §23 #40; ruling
    /// FR-8). Real-time safe; call it between process() calls.
    void setBases(const ParamValues &values) noexcept;

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
    /// The slot's effective value in effect: clamp(base + modulation + route offset), as the
    /// class comment describes. `slot` as for base().
    float effective(ParamSlot slot) const noexcept;

    /// Replaces the mod matrix's routes with a copy of `routes` (the class comment), and
    /// recomputes timeVarying(). They apply at once, as a loaded state does: the effective values
    /// are recomputed, and each ramped value whose target they moved jumps (as setBase() of a
    /// macro makes it). Real-time safe; call it between process() calls.
    void setRoutes(const ModRoutes &routes) noexcept;
    /// The routes in effect: factoryRoutes(mode) from construction, or the last setRoutes().
    const ModRoutes &routes() const noexcept { return routes_; }
    /// Whether a route moves with time alone (the class comment's Time-varying routes).
    bool timeVarying() const noexcept { return timeVarying_; }

    /// The host's transport, given before a process() call (the transport sources: the class
    /// comment), held until the next one; the transport sources read 0 before any. A change of
    /// the playing flag applies at the call's first boundary, as an event at offset 0 would.
    /// Real-time safe; call it between process() calls.
    void setTransport(const Transport &transport) noexcept;

    /// Source `s`'s value on layer `layer` (below kLayers; a larger one reads the last layer), in
    /// [0, 1] (the class comment): a modulator's, the follower's and the transport's as of the
    /// last stepped boundary, the MIDI sources' as of the last applied one, a macro's its effective
    /// value. A global source has the same value on every layer; None and Count are 0. For tests
    /// and the editor's Mod view.
    float sourceValue(ModSource s, uint8_t layer) const noexcept;

    /// Renders numFrames frames into out, applying `events`, `notes` and `controls` on the
    /// control grid as the class comment describes.
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
    ///   is ignored, as is a note or control event with a field out of range (NoteEvent,
    ///   ControlEvent).
    /// - Bounded work: each event is read once, and a call applies at most
    ///   numFrames / kControlGrid + 2 boundaries, each a fixed amount of work.
    ///
    /// Accepts any numFrames, including 0 and more than maxBlockSize. The whole call runs with
    /// denormals flushed (ScopedDenormalsOff) and restores the caller's floating-point mode.
    void process(const float *const *in, uint32_t numInChannels, float *const *out,
                 uint32_t numOutChannels, uint32_t numFrames, std::span<const ParamEvent> events,
                 std::span<const NoteEvent> notes = {},
                 std::span<const ControlEvent> controls = {}) noexcept SST_CPPUTILS_NONBLOCKING;

    /// The runner the layer tasks go to (the class comment's Threads), from the next process()
    /// call; nullptr selects the built-in SerialRunner, the default. Used only inside process(), so
    /// set it while no process() call runs (the plugin sets it at activation).
    void setTaskRunner(TaskRunner *runner) noexcept;

    /// Renders task i of the current piece: layer taskLayers_[i]'s piece, from the piece state the
    /// caller fixed before its request (the class comment's Threads). For the runner, on any
    /// thread, only while process() waits on that request: the plugin's thread-pool exec calls it.
    /// An index past the piece's task count does nothing.
    void execTask(uint32_t i) noexcept SST_CPPUTILS_NONBLOCKING;

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
    /// offer from it, applies the offer's values (setBases), and has the engine adopt the
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
    /// for an off→on transition (the class comment). The sources restart: the modulators and the
    /// follower reset, the MIDI sources return to their initial values (MidiSources), and the
    /// transport is forgotten, as before any setTransport(): the next one applies as a fresh
    /// Processor's first does. Real-time safe; call it between process() calls.
    void reset() noexcept;

    /// The grain-event sink (gg-render's event log, tests): called on the audio thread once per
    /// spawned grain with its layer, in piece, layer, spawn order (the class comment), whether or
    /// not publishing is on. nullptr removes it. Call it while process() isn't running.
    void setEventSink(GrainEventSink sink, void *ctx) noexcept;
    /// The steal sink (gg-render's event log, tests): called on the audio thread once per grain
    /// the overload ladder steals, with its layer, the stolen grain's index and the onset of the
    /// grain it made room for, right before that grain's setEventSink call (the class comment),
    /// whether or not publishing is on. nullptr removes it. Call it while process() isn't running.
    /// A steal that takes the last slot of its layer's event buffer leaves that buffer no room for
    /// its spawn, so the sinks see the steal without it; no schedule the engine admits gets there,
    /// and the dropped spawn is counted in droppedGrainEvents().
    void setStealSink(GrainStealSink sink, void *ctx) noexcept;

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
    uint64_t droppedGrainEvents() const noexcept;

    /// The four engines' Engine::loweredGrains() and Engine::stolenGrains(), summed (M2 engine spec
    /// §6.5): since construction or the last reset().
    uint64_t loweredGrains() const noexcept;
    uint64_t stolenGrains() const noexcept;
    /// Layer `layer`'s Engine::activeGrains() (`layer` below kLayers), for tests.
    uint32_t layerActiveGrains(uint8_t layer) const noexcept
    {
        return layerAt(layer).engine.activeGrains();
    }

    /// Whether the reverb's return is being added: the reverb chunk holding the last rendered
    /// sample is active (the class comment). False after construction and reset().
    bool reverbActive() const noexcept { return reverb_->chunkActive(); }

  private:
    /// The per-layer sources, Lfo1 to Step (isPerLayerSource).
    static constexpr size_t kModulatorSources =
        static_cast<size_t>(ModSource::Step) - static_cast<size_t>(ModSource::Lfo1) + 1;

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

    /// One entry of a layer's event buffer (the class comment's Grain events): a spawn's grain, or
    /// a steal's stolen grain index and the onset of the grain it made room for.
    struct LayerEvent
    {
        enum class Kind : uint8_t
        {
            Spawn,
            Steal,
        } kind;
        GrainSpec grain;
        uint64_t index;
        int64_t onset;
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
        // This piece's spawns and steals, in the engine's order, drained after the piece: reserved
        // to 4 × grainCapacity. The events a full buffer kept from the sinks since the
        // construction or the last reset(), per layer so that each task counts its own
        // (droppedGrainEvents sums them).
        std::vector<LayerEvent> events;
        uint64_t droppedEvents{0};
        // The quota last given to the engine (Engine::setQuota): the engine's own default until
        // the first piece that renders the layer.
        uint64_t quotaGiven{kUnlimitedQuota};
        // Freeze has been seen off since the construction or the last reset(), so on engages it;
        // engineFrozen is the freeze setting last given to the engine.
        bool freezeArmed{false};
        bool engineFrozen{false};
        // The enable gate last given to the engine (Engine::setEnabled), and the kind.
        bool enabledGiven{true};
        SourceMode kind{SourceMode::Sample};
        // The cloud parameters last given to the engine, if cloudGiven (updateEffective).
        CloudParams cloud{};
        bool cloudGiven{false};
        // The layer's modulators (spec §6.8), stepped on the audio thread between pieces, never
        // in a task, and their values for the cell of the last stepped boundary, in ModSource
        // order from Lfo1 to Step.
        LayerModulators modulators;
        std::array<float, kModulatorSources> modulatorValues{};
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
    /// The engines' steal sink, ctx the Processor: appends the steal to its layer's buffer, in
    /// order with the spawns, or counts it when the buffer is full.
    static void onSteal(void *ctx, uint32_t layer, uint64_t index,
                        int64_t onset) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Hands each buffered event, layer by layer A to D, in order: a spawn to the sink set by
    /// setEventSink and, while this call publishes, the grain queue; a steal to the sink set by
    /// setStealSink. Then empties the buffers.
    void drainEvents() noexcept SST_CPPUTILS_NONBLOCKING;
    /// takeOffers() for one layer.
    bool takeLayerOffers(Layer &l) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Hands every source the layer's engine has retired back to its sources
    /// (SourceExchange::retire).
    static void handBackRetired(Layer &l) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Renders the absolute samples [from, to) of the current call.
    void render(const Buffers &b, int64_t from, int64_t to) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Renders layer L's piece [from, from + n) into its wet buffer (the class comment), or, when
    /// it isn't rendering, only writes its ring. A rendering layer's runs as its task (execTask),
    /// on any thread, so it writes only layer L's state and reads the rest read-only (Threads).
    void renderLayerPiece(uint8_t L, int64_t from, uint32_t n, const float *const dry[2],
                          bool hasInput, uint32_t rendering) noexcept SST_CPPUTILS_NONBLOCKING;
    /// The TaskFn handed to the runner, ctx the Processor: execTask(i).
    static void layerTask(void *ctx, uint32_t i) noexcept SST_CPPUTILS_NONBLOCKING;
    /// The morph weights of the four layers at sample t, with r_K the on/off ramps' values where
    /// rendering[K] and 0 elsewhere.
    void weightsAt(int64_t t, const std::array<bool, kLayers> &rendering,
                   std::array<float, kLayers> &weights) const noexcept;
    /// Moves the pending changes into the base and modulation values, the pending notes' held set
    /// and the pending MIDI sources into effect.
    void commitPending() noexcept;
    /// Steps every source once per grid boundary c with lastStepped_ < c ≤ upTo, in order (the
    /// class comment's sources' clock), scanning the input up to each c first for the follower;
    /// then scans the input up to scanTo (≥ upTo, within the call). Every boundary it steps and
    /// every sample it scans is in the call b describes, and not yet rendered.
    void stepSources(const Buffers &b, int64_t upTo,
                     int64_t scanTo) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Adds the input's peak |x| over [inputScanned_, to) to cellPeak_ (non-finite samples as 0).
    void scanInput(const Buffers &b, int64_t to) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Steps every source at the boundary c (stepSources).
    void stepSourcesAt(int64_t c) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Makes each ramped value whose target the effective values moved jump to it (setBase() of a
    /// macro, setRoutes()).
    void jumpMovedRamps() noexcept;
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
    /// Applies one note event, whose fields are in range, to held_, and an On's velocity and key to
    /// the pending MIDI sources.
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
    // Budget's effective value as weight units (budgetUnits), the rendering layers' shared
    // budget: kept by updateEffective, split into the quotas per piece (the class comment).
    uint64_t budgetUnits_{0};
    std::array<std::array<float, kFeedbackDelay>, kLayers> weights_{};
    // The current piece's state for its tasks (the class comment's Threads), fixed before the
    // request and read-only while it runs: the piece's bounds, its input, the channels the
    // engines render, and the rendering layers, in order A to D, one task each.
    int64_t pieceFrom_{0};
    uint32_t pieceN_{0};
    std::array<const float *, 2> pieceDry_{};
    bool pieceHasInput_{false};
    uint32_t pieceRendering_{2};
    std::array<uint8_t, kLayers> taskLayers_{};
    uint32_t taskCount_{0};
    // setTaskRunner's runner, or serialRunner_.
    SerialRunner serialRunner_;
    TaskRunner *runner_{&serialRunner_};

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

    // The mod matrix (the class comment): its routes, whether one moves with time alone, and the
    // sources' values per layer, filled by updateEffective. The global sources: the MIDI ones in
    // effect and pending (equal when nothing pends), the follower and the transport, with the
    // follower's and the transport's values for the cell of the last stepped boundary
    // (lastStepped_, −32 before boundary 0), and the input's peak over the cell being scanned,
    // up to the absolute sample inputScanned_. setTransport's change of the playing flag waits
    // for the next call's first boundary (transportChanged_).
    ModRoutes routes_{};
    bool timeVarying_{false};
    std::array<ModSourceValues, kLayers> sources_{};
    MidiSources midi_{};
    MidiSources pendingMidi_{};
    EnvFollower follower_;
    float followerValue_{0.0f};
    TransportSource transport_{};
    float transportBeat_{0.0f};
    float transportBar_{0.0f};
    float transportPlaying_{0.0f};
    bool transportChanged_{false};
    int64_t lastStepped_{-static_cast<int64_t>(kControlGrid)};
    int64_t inputScanned_{0};
    float cellPeak_{0.0f};
    double cellSeconds_;

    Ramp mix_;
    Ramp amount_;
    Ramp drySend_; // always 0 in the instrument
    Ramp morphX_;
    Ramp morphY_;

    // Publishing (the class comment). The external sink is setEventSink's; publishingThisCall_
    // is publishing_ as the current process() call read it at its start.
    GrainEventSink externalSink_{nullptr};
    void *externalSinkCtx_{nullptr};
    GrainStealSink externalStealSink_{nullptr};
    void *externalStealSinkCtx_{nullptr};
    std::atomic<bool> publishing_{false};
    bool publishingThisCall_{false};
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
