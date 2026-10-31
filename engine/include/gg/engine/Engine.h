// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_ENGINE_ENGINE_H
#define GG_ENGINE_ENGINE_H

#include <cstdint>
#include <limits>
#include <memory>
#include <span>

#include <sst/cpputils/rtsan_support.h>

#include "gg/core/Window.h"
#include "gg/grain/CloudScheduler.h"
#include "gg/grain/GrainPool.h"
#include "gg/source/LiveSource.h"
#include "gg/source/PreparedSource.h"
#include "gg/source/SampleBuffer.h"
#include "gg/source/SourceMode.h"
#include "gg/spectral/SpectralSource.h"

namespace gg
{
struct EngineConfig
{
    double sampleRate{48000.0};  ///< Output sample rate; finite and > 0.
    uint32_t maxBlockSize{4096}; ///< process() works in blocks of at most this many frames; > 0.
    /// The most grains that may sound at once; below 2^31, since the pool stores twice as many.
    uint32_t grainCapacity{4096};
    uint64_t seed{0};
    /// > 0 allocates a stereo live ring, which SourceMode::Live reads (spec §6.3); 0 means no
    /// ring. Finite and >= 0. Grains read a window of N = ceil(liveSeconds × sampleRate) frames,
    /// stored in N + maxBlockSize frames (plus 3 guard frames) so that a block's later frames
    /// never overwrite the window (ruling M1-o; see the class comment and LiveSource).
    double liveSeconds{0.0};
    /// The layer this engine plays (spec §6.2): the `layer` field of every random draw's key.
    /// Below 4. Layer A is 0, which keys every draw as M1's one-layer engine did.
    uint32_t layer{0};
    /// true allocates the Spectral Smear (the extremes spec §6.2): a SpectralSource and its
    /// SpectralSource::kRingSeconds ring, which SourceMode::Spectral reads. false (the default)
    /// means none: setSourceMode(Spectral) is ignored, and nothing else changes.
    bool spectral{false};
};

/// Called on the audio thread, from inside process(), once for every grain that spawns, in
/// spawn order, with the engine's EngineConfig::layer (M2 sub-project 1's spec §6.7). It must be
/// real-time safe.
using GrainEventSink = void (*)(void *ctx, uint32_t layer, const GrainSpec &);

/// The grain engine, one layer's (spec §6.2): one Cloud grain stream (layer EngineConfig::layer,
/// stream 0) whose new grains read one source kind, a sample (SourceMode::Sample, the default),
/// the live ring (SourceMode::Live) or, with EngineConfig::spectral, the Spectral Smear's ring
/// (SourceMode::Spectral). Each grain keeps reading the source it started on: when a new sample
/// source is adopted, the grains already sounding finish on the old one (adoptSource), and when
/// the source mode changes they finish on their sample or on their ring (setSourceMode). Its pool
/// row says which (GrainSpec::sourceSlot: a sample slot, kLiveSlot or kSpectralSlot).
/// The enable gate (setEnabled) holds the stream off while the ring and the clock run on.
///
/// With the same configuration, source, input and parameter changes (at the same sample
/// positions), the output and the grain events are bit-identical however the host splits blocks
/// (INTENT §8.6, §14):
/// - each grain's read position and window phase come from its elapsed time since its onset,
///   never from state accumulated across blocks;
/// - grains are summed in spawn order;
/// - a grain is admitted when fewer than grainCapacity grains are still sounding at its onset,
///   and otherwise dropped. That depends only on the schedule, not on block boundaries;
/// - live mode writes each block's input into the ring before any grain reads it. At output
///   sample t a grain reads only the window [R(t) − N, R(t) − 1]: frames written at or before
///   t, and at least M = 256 frames before it unless a freeze has stopped the head and the fade
///   is taking the grain out. The ring stores N + maxBlockSize frames (ruling M1-o), so the
///   frames the block writes after t never overwrite t's window, however far the ring has
///   wrapped. What a sample reads therefore depends neither on where the block ends nor on
///   maxBlockSize.
///
/// **Live mode** (spec §6.3). Ring time: R(t) is the number of frames written through output
/// sample t (LiveSource). Each grain's GrainSpec::startPos is a ring-time position, and its rate
/// is the pitch ratio (the ring runs at the output rate). At its onset T = onsetSample +
/// onsetFrac, with the head at ρ = R(T) (R advances linearly between samples while it moves),
/// the grain asks to start at s* = ρ − positionDraw × sampleRate, clamped to [lo, hi]:
///
///     hi = ρ − 3 − M − max(0, dur × rate − span)
///     lo = ρ − N + 1 + M + max(0, span × (1 − rate))
///
/// where dur is durationSamples and span = min(dur, how far the head will still advance), as
/// LiveSource::headRunAfter says from the freeze state at the onset. Not frozen, span = dur:
/// hi = ρ − 3 − M − max(0, dur × (rate − 1)) and lo = ρ − N + 1 + M + max(0, dur × (1 − rate)).
/// Frozen with the head stopped, span = 0: hi = ρ − 3 − M − dur × rate and lo = ρ − N + 1 + M.
/// During the 20 ms freeze ramp, span is the frames left in it. Either way every tap the grain
/// reads, over its whole life, stays at least M frames behind the head and M frames inside the
/// oldest frame, unless the freeze setting changes while it sounds. If lo > hi, the grain is
/// dropped and counted in droppedGrains().
///
/// What admission can't foresee, a freeze that stops the head under a grain or an unfreeze
/// that moves the oldest frame onto it, the head-proximity fade (LiveSource::fade) covers: each
/// output sample multiplies the grain by the fade at its read position, and where the fade is
/// 0 nothing is read, so neither stale data nor NaN can reach the output.
///
/// **Spectral mode** (the extremes spec §6.2). Each block, after the live ring's write and
/// before any grain spawns, the SpectralSource writes the block into its own ring:
/// - while the mode in effect is Spectral, the smear of the material at the Position in effect
///   (CloudParams, in the material's units). The material is the current sample slot when it
///   holds a source, and the live ring otherwise, read at each hop's own head;
/// - in any other mode, the drain of the frames already computed, then silence.
///
/// A Spectral grain is a ring grain over the smear's ring, on kSpectralSlot: it starts
/// positionDraw seconds behind that ring's head under the admission clamp above, with N the
/// smear's ring frames, and the live kernels render it. That ring never freezes: setFreeze
/// reaches the live ring alone.
class Engine
{
  public:
    /// GrainSpec::sourceSlot of a grain reading the live ring; 0 and 1 are the sample slots.
    static constexpr uint8_t kLiveSlot = 2;
    /// GrainSpec::sourceSlot of a grain reading the Spectral Smear's ring (the extremes spec §6.2).
    static constexpr uint8_t kSpectralSlot = 3;

    /// Allocates everything process() needs, the live ring and the Spectral Smear included. Throws
    /// std::invalid_argument if sampleRate is not finite and positive, maxBlockSize is 0,
    /// liveSeconds is not finite and >= 0, grainCapacity is 2^31 or more, or layer is 4 or more
    /// (all checked before anything is allocated), or if the ring's storage, N + maxBlockSize
    /// frames, would exceed 2^40, and std::bad_alloc if the Spectral Smear can't be allocated.
    explicit Engine(const EngineConfig &config);

    /// Not real-time safe: call only while process() is not running. PreparedSource::make, then
    /// an adoption that replaces the source every sample grain reads: the sample grains still
    /// sounding go on at their positions in the new source, and the grains from the next onset
    /// on read it too.
    /// nullptr means no source: the schedule keeps running but no grains spawn in Sample mode,
    /// and the grains still sounding, which would read nothing, end at once. That ends the
    /// grains reading the ring too: a source removed entirely is the one hard cut that remains,
    /// by design. Otherwise grains reading the ring are left alone.
    ///
    /// The engine owns this source, keeping it alive until the next setSource or its
    /// destruction; takeRetiredSource never hands it back. Sources adopted with adoptSource that
    /// it replaces (the current one and the one draining, at most two) are handed back by
    /// takeRetiredSource, oldest first.
    ///
    /// A mono source feeds both output channels; a stereo (or wider) source feeds its first two
    /// channels to them, read from PreparedSource's interleaved copy, which doubles the source's
    /// memory. The buffer's samples must not change afterwards (see PreparedSource); to pick up
    /// a change, call setSource again. If the copy can't be allocated the engine reads the
    /// planar channels alone: the same output bits, only slower.
    void setSource(std::shared_ptr<const SampleBuffer> source);

    /// setSource's adoption, for a source already prepared: not real-time safe, the same
    /// contract, and the engine owns `source` in the same way. Never throws.
    void setPreparedSource(std::shared_ptr<const PreparedSource> source) noexcept;

    /// setPreparedSource's replacement (every sample grain still sounding goes on in `source` too),
    /// for a source the caller owns, as adoptSource's are: not real-time safe, call it only while
    /// process() is not running. The sources it replaces are handed back as setPreparedSource hands
    /// them back (a setSource source is let go), and `source` is then an adopted source like any
    /// other: a later adoption replaces it, and takeRetiredSource hands it back once no grain reads
    /// it. nullptr means no source. `source` must not be in play already or waiting to be handed
    /// back; take every retired source first, so the hand-backs fit. Never throws.
    void replaceSource(const PreparedSource *source) noexcept;

    /// Real-time safe, but not synchronized with process(): call it on the audio thread between
    /// process() calls, or while process() is not running (spec §6.4). Grains from the next
    /// onset on read `source` (nullptr: no source, so none spawn in Sample mode); the grains
    /// still sounding finish on the source they started on. Adopting the current source again
    /// changes nothing and returns true.
    ///
    /// Two sources may be in play: the current one and the previous one, draining until its
    /// last grain ends and it has been handed back. Returns false, changing nothing, while two
    /// are in play. A further source then waits at most Size (and a block), provided the caller
    /// takes each retired source promptly: a source still waiting to be taken keeps the next one
    /// from retiring. Also false while more than one source awaits takeRetiredSource, which only
    /// setSource replacing adopted sources can cause.
    ///
    /// The engine never owns `source`: the caller keeps it alive until takeRetiredSource
    /// returns it (or setSource hands it back, or the engine is destroyed), and must not adopt
    /// one that is still waiting to be handed back.
    bool adoptSource(const PreparedSource *source) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Real-time safe, with the same threading rule as adoptSource. An adopted source that no
    /// grain reads any more, each handed back once, oldest first; nullptr when there is none.
    /// The previous source retires at the first block end at or past its last grain's end (or at
    /// reset()), one at a time: the next waits until this one has been taken.
    const PreparedSource *takeRetiredSource() noexcept SST_CPPUTILS_NONBLOCKING;

    /// Real-time safe, but not synchronized with process(): call it on the audio thread
    /// between process() calls, or while process() is not running. The new parameters apply
    /// from the next process() call, at its first sample g: to every grain that spawns from then
    /// on. A density or distribution change also moves the pending onset (ruling M1-h; see
    /// CloudScheduler), so for a block-size-invariant render, make changes at fixed samples.
    /// Position and spray are read in the source mode in effect (ruling M1-k; CloudParams).
    void setCloudParams(const CloudParams &params) noexcept;

    /// The held keys (spec §6.1): distinct MIDI keys 0..127, ascending, at most 128. Copied; no
    /// allocation. Each grain that spawns from the next process() call's first frame on picks
    /// one of them for its pitch, and the first key held after none forces an onset at that
    /// frame (CloudScheduler). For a block-size-invariant render, change them at fixed samples.
    /// Real-time safe, with the same threading rule as setCloudParams.
    void setHeldKeys(std::span<const uint8_t> keys) noexcept;

    /// true (the instrument): no grain spawns while no key is held. Takes effect from the next
    /// process() call's first frame. Real-time safe, with the same threading rule as
    /// setCloudParams.
    void setNoteGate(bool gate) noexcept;

    /// Which source new grains read (spec §6.4). Live needs a ring (liveSeconds > 0) and Spectral
    /// the Spectral Smear (EngineConfig::spectral); without it the call is ignored. Takes effect
    /// from the next process() call's first frame: the scheduler draws position and spray for the
    /// new kind from that frame, and the grains still sounding finish on the source they started
    /// on (their sample slot or their ring), so a switch doesn't click. Switching to Spectral
    /// starts the smear at the next hop boundary; switching away stops its new frames, and its
    /// ring drains what is pending and then takes silence (SpectralSource::setActive), so the
    /// Spectral grains still sounding fade out with it. Real-time safe, with the same threading
    /// rule as setCloudParams.
    void setSourceMode(SourceMode mode) noexcept;

    /// The enable gate (spec §6.2). Takes effect from the next process() call's first frame. Off:
    /// the grains still sounding end there, and no grain spawns while off; the ring is still
    /// written, and the clock, the retirement of sources and freeze go on, so a layer switched on
    /// later reads recent audio. On again: the pending onset is forced to that frame
    /// (CloudScheduler::forceNextOnset), as a first held key forces it. The default is on.
    /// Turning off also lowers bus2Used(). Real-time safe, with the same threading rule as
    /// setCloudParams.
    void setEnabled(bool enabled) noexcept { requestedEnabled_ = enabled; }
    /// The enable gate in effect: set by the last process() call (see setEnabled).
    bool enabled() const noexcept { return enabled_; }

    /// Freezes the live ring (see LiveSource): the write gain ramps to 0 over 20 ms and then the
    /// head stops, so grains keep reading what the ring holds; off, the head resumes and the gain
    /// ramps back. Takes effect from the next process() call's first frame. It affects only the
    /// ring, so it does nothing without one. Real-time safe, with the same threading rule as
    /// setCloudParams.
    void setFreeze(bool freeze) noexcept;

    /// The sink is called from process(); pass nullptr to remove it. Real-time safe, but not
    /// synchronized with process(): call it on the audio thread between process() calls, or
    /// while process() is not running.
    void setEventSink(GrainEventSink sink, void *ctx) noexcept;

    /// Silences every grain, lowers bus2Used() and restarts the clock, the schedule and ring time
    /// at 0, the Spectral Smear's included. Keeps the current source (the previous one retires, see
    /// takeRetiredSource), the source mode, the enable gate, the Cloud parameters, the held keys,
    /// the note gate, the freeze setting and the sink: the next process() continues as a new engine
    /// with those settings would, a freeze ramping down from its first frame. The ring's old frames
    /// can't be read again. Real-time safe, but not synchronized with process(): call it on the
    /// audio thread between process() calls, or while process() is not running.
    void reset() noexcept;

    /// Renders numFrames frames, overwriting out[0 .. numChannels). Before rendering, if there is
    /// a live ring, writes the input into it: in[c] is channel c's numFrames samples. A mono
    /// input (numInChannels 1) fills both ring channels; in == nullptr, numInChannels 0 or a null
    /// channel pointer is silence. Non-finite input is stored as 0. The ring records in either
    /// source mode, so a switch to Live finds its history.
    ///
    /// Channels 0 and 1 carry the grains, each grain panned by panGains(); any further channels
    /// are zeroed. Accepts any numFrames, including 0 (which renders nothing) and more than
    /// maxBlockSize (which is split internally).
    ///
    /// The schedule and the clock advance whatever the outputs are, so the next call
    /// continues the same render:
    /// - with one channel, it carries source channel 0, unpanned;
    /// - out == nullptr renders nothing;
    /// - a null channel pointer is skipped, and when out[0] is null nothing is rendered, so
    ///   channel 1 (if not null) is only zeroed.
    ///
    /// It flushes denormals while it runs and restores the caller's floating-point mode.
    void process(const float *const *in, uint32_t numInChannels, float *const *out,
                 uint32_t numChannels, uint32_t numFrames) noexcept SST_CPPUTILS_NONBLOCKING;

    /// The two-bus form (extremes spec §6.3): as above, but the grains whose bus is 1
    /// (GrainSpec::bus, from CloudParams::busSplit) are added into bus2 instead of out, the
    /// bus-0 grains into out, each bus summing its grains in spawn order. bus2 is null, the
    /// five-argument form exactly (every grain into out), or two channel pointers of numFrames
    /// frames each, which the call overwrites as it does out: both are zeroed, and bus2 takes
    /// out's layout (with one output channel, bus2[0] carries source channel 0 unpanned and
    /// bus2[1] is only zeroed; when out renders nothing, bus2 is only zeroed). A null bus2
    /// channel pointer is skipped as out's are. At a split of 0 no grain is on bus 1, so out is
    /// the five-argument form's render bit for bit and bus2 is all +0.
    void process(const float *const *in, uint32_t numInChannels, float *const *out,
                 uint32_t numChannels, float *const *bus2,
                 uint32_t numFrames) noexcept SST_CPPUTILS_NONBLOCKING;

    /// M0's form: process(nullptr, 0, out, numChannels, numFrames), with no input.
    void process(float *const *out, uint32_t numChannels,
                 uint32_t numFrames) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Sets the seed that keys the schedule's random draws (EngineConfig::seed) from the next
    /// process() call on: the grains whose onsets it schedules use it, and the grains already
    /// sounding keep their draws. The grain index keeps counting, so what follows isn't a fresh
    /// engine's render at `seed`; it is the same at any block split, like any change made
    /// between process() calls. Real-time safe; call it between process() calls.
    void setSeed(uint64_t seed) noexcept { config_.seed = seed; }
    /// The seed in effect: EngineConfig::seed, or the last setSeed().
    uint64_t seed() const noexcept { return config_.seed; }

    double sampleRate() const noexcept { return config_.sampleRate; }
    /// Frames rendered since construction or the last reset().
    int64_t sampleClock() const noexcept { return clock_; }
    /// The grains still sounding after the last process() call: the pool retires the finished
    /// ones at the end of every block.
    uint32_t activeGrains() const noexcept { return pool_.size(); }
    /// Grains the schedule asked for that found the pool full or, in Live mode, whose read span
    /// doesn't fit the ring, since construction or reset().
    uint64_t droppedGrains() const noexcept { return dropped_; }
    /// True from the first admission of a bus-1 grain (whichever process() form runs) until
    /// reset() or the enable gate turning off, where the pool is cleared; false at construction.
    /// It stays raised while no grain is on bus 1 any more, so a layer that sums bus2 after its
    /// filter keeps the filter's tail (extremes spec §6.3).
    bool bus2Used() const noexcept { return bus2Used_; }
    /// The source mode in effect: set by the last process() call (see setSourceMode).
    SourceMode sourceMode() const noexcept { return mode_; }
    /// N, the live ring's length in frames; 0 without a ring.
    int64_t liveFrames() const noexcept { return live_.frames(); }
    /// The write head R: frames written into the live ring since construction or reset(). It
    /// stops while frozen.
    int64_t liveWriteHead() const noexcept { return live_.head(); }
    /// The live ring itself, read-only: read it with liveWriteHead() as the head.
    const LiveSource &live() const noexcept { return live_; }
    /// Whether the current sample slot holds a source: the Spectral Smear's material is then the
    /// sample, else the live ring (the extremes spec §6.2).
    bool hasSampleSource() const noexcept { return slots_[current_].source != nullptr; }
    /// The cloud parameters in effect, as the scheduler clamped them (CloudScheduler::params).
    const CloudParams &cloudParams() const noexcept { return cloud_.params(); }
    /// The Spectral Smear, read-only; null without EngineConfig::spectral.
    const SpectralSource *spectral() const noexcept { return spectral_.get(); }

  private:
    void processBlock(const float *const *in, uint32_t numInChannels, float *const *out,
                      uint32_t numChannels, float *const *bus2, uint32_t offset,
                      uint32_t numFrames) noexcept SST_CPPUTILS_NONBLOCKING;
    /// The grain a draw makes from the current sample source.
    GrainSpec sampleGrain(const GrainDraw &d) const noexcept;
    /// The grain a draw makes from `ring` (the live ring, or the smear's), admission-clamped, on
    /// source slot `slot`; false when it can't fit.
    bool liveGrain(const GrainDraw &d, GrainSpec &g, const LiveSource &ring,
                   uint8_t slot) const noexcept;
    /// Where a block's grains go: bus[b] are the two channel pointers bus b's grains are added
    /// into, at the block's first frame. Without bus2, bus[1] is bus[0], so every grain goes to
    /// out.
    struct BlockOutputs
    {
        float *bus[2][2];
    };
    void admit(const GrainSpec &g, const BlockOutputs &outputs, int64_t blockStart,
               int64_t &renderedTo) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Adds every pool grain into its row's bus over [from, to), in spawn order, through
    /// gg/grain/GrainKernels.h with each grain's latched tier and source, whatever the mode in
    /// effect: the SIMD live kernel for a grain of kLiveSlot over the live ring, and for one of
    /// kSpectralSlot over the smear's ring, otherwise the SIMD sample kernel reading the grain's
    /// sample slot.
    void renderPool(const BlockOutputs &outputs, int64_t blockStart, int64_t from,
                    int64_t to) noexcept SST_CPPUTILS_NONBLOCKING;

    EngineConfig config_;
    HannTable hann_;
    GrainPool pool_;
    CloudScheduler cloud_;
    LiveSource live_;
    // The Spectral Smear (EngineConfig::spectral), or null.
    std::unique_ptr<SpectralSource> spectral_;

    // One source grains may read, with what process() reads of it cached, so it never touches
    // a reference count. No source (or one without channels) reads nothing: frames 0.
    struct SourceSlot
    {
        const PreparedSource *source{nullptr};
        const float *data[2]{nullptr, nullptr};
        int64_t frames{0};
        bool mono{true};
        const float *interleaved{nullptr}; // a stereo source's pairs; null if mono or none
        double rate{0.0};                  // the source's sample rate; unused without one
        // The latest grainEndSample of the grains admitted to this slot (never a ring grain);
        // INT64_MIN when none has been since the last clear.
        int64_t lastEnd{std::numeric_limits<int64_t>::min()};
    };
    /// The slot reading `source`, with no grains yet.
    static SourceSlot slotFor(const PreparedSource *source) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Hands the previous slot's source back, and frees the slot, once its last sample grain has
    /// ended and nothing else is waiting to be handed back. setSource's own source is just let go.
    void retirePrevious() noexcept SST_CPPUTILS_NONBLOCKING;
    /// The replacement setPreparedSource and replaceSource share: hands back the adopted sources
    /// in play and makes `source` current, read by every sample grain still sounding (the ring's
    /// grains keep the ring). owned_ is left to the caller.
    void replaceCurrent(const PreparedSource *source) noexcept;
    /// pool_.clear(), and no slot has grains any more.
    void clearGrains() noexcept SST_CPPUTILS_NONBLOCKING;
    SourceSlot &currentSlot() noexcept { return slots_[current_]; }
    SourceSlot &previousSlot() noexcept { return slots_[1 - current_]; }

    // Spec §6.4: grains read slots_[their sourceSlot]. New grains read slots_[current_]; the
    // other slot holds the previous source until its last grain ends, or nothing.
    SourceSlot slots_[2];
    uint8_t current_{0};
    // The source setSource made, which the engine owns. process() never touches it: a slot that
    // reads it is only let go, and it is freed by the next setSource or the destructor.
    std::shared_ptr<const PreparedSource> owned_;
    // Adopted sources no grain reads any more, oldest first, for takeRetiredSource. retirePrevious
    // adds one only when this is empty, and setSource at most two more, after which adoptSource
    // refuses until all but one are taken, so three slots suffice.
    static constexpr uint32_t kRetiredCapacity = 3;
    const PreparedSource *retired_[kRetiredCapacity]{};
    uint32_t retiredCount_{0};

    // In effect, and as last requested; process() applies the requests at its first frame.
    // mode_ is the kind new grains read; it never chooses a sounding grain's kernel.
    SourceMode mode_{SourceMode::Sample};
    SourceMode requestedMode_{SourceMode::Sample};
    bool enabled_{true};
    bool requestedEnabled_{true};
    bool freeze_{false};
    // Raised at the first bus-1 admission; lowered by reset() and the gate turning off.
    bool bus2Used_{false};

    GrainEventSink sink_{nullptr};
    void *sinkCtx_{nullptr};

    int64_t clock_{0};
    uint64_t dropped_{0};
};
} // namespace gg

#endif // GG_ENGINE_ENGINE_H
