// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_ENGINE_ENGINE_H
#define GG_ENGINE_ENGINE_H

#include <cstdint>
#include <memory>

#include <sst/cpputils/rtsan_support.h>

#include "gg/core/Window.h"
#include "gg/grain/CloudScheduler.h"
#include "gg/grain/GrainPool.h"
#include "gg/source/LiveSource.h"
#include "gg/source/SampleBuffer.h"
#include "gg/source/SourceMode.h"

namespace gg
{
struct EngineConfig
{
    double sampleRate{48000.0};  ///< Output sample rate; finite and > 0.
    uint32_t maxBlockSize{4096}; ///< process() works in blocks of at most this many frames; > 0.
    uint32_t grainCapacity{4096};
    uint64_t seed{0};
    /// > 0 allocates a stereo live ring of ceil(liveSeconds × sampleRate) frames, which
    /// SourceMode::Live reads (spec §6.3); 0 means no ring. Finite and >= 0.
    double liveSeconds{0.0};
};

/// Called on the audio thread, from inside process(), once for every grain that spawns, in
/// spawn order. It must be real-time safe.
using GrainEventSink = void (*)(void *ctx, const GrainSpec &);

/// The grain engine: one Cloud grain stream (layer 0, stream 0) reading one source, either a
/// sample (SourceMode::Sample, the default) or the live ring (SourceMode::Live).
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
class Engine
{
  public:
    /// Allocates everything process() needs, the live ring included. Throws
    /// std::invalid_argument if sampleRate is not finite and positive, maxBlockSize is 0, or
    /// liveSeconds is not finite and >= 0 (or asks for a ring over 2^40 frames).
    explicit Engine(const EngineConfig &config);

    /// Not real-time safe: call only while process() is not running. The engine keeps the
    /// buffer alive until the next setSource or its destruction. A mono source feeds both
    /// output channels; a stereo (or wider) source feeds its first two channels to them.
    /// nullptr means no source: the schedule keeps running but no grains spawn in Sample mode.
    void setSource(std::shared_ptr<const SampleBuffer> source);

    /// Real-time safe, but not synchronized with process(): call it on the audio thread
    /// between process() calls, or while process() is not running. The new parameters apply
    /// from the next process() call, at its first sample g: to every grain that spawns from then
    /// on. A density or distribution change also moves the pending onset (ruling M1-h; see
    /// CloudScheduler), so for a block-size-invariant render, make changes at fixed samples.
    /// Position and spray are read in the source mode in effect (ruling M1-k; CloudParams).
    void setCloudParams(const CloudParams &params) noexcept;

    /// Which source grains read. Live needs a ring (liveSeconds > 0); without one the call is
    /// ignored. Takes effect from the next process() call's first frame: the grains still
    /// sounding from the old source are silenced, and the scheduler draws position and spray
    /// for the new one. Real-time safe, with the same threading rule as setCloudParams.
    void setSourceMode(SourceMode mode) noexcept;

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

    /// Silences every grain and restarts the clock, the schedule and ring time at 0. Keeps the
    /// source, the source mode, the Cloud parameters, the freeze setting and the sink: the next
    /// process() continues as a new engine with those settings would, a freeze ramping down from
    /// its first frame. The ring's old frames can't be read again. Real-time safe, but not
    /// synchronized with process(): call it on the audio thread between process() calls, or
    /// while process() is not running.
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

    /// M0's form: process(nullptr, 0, out, numChannels, numFrames), with no input.
    void process(float *const *out, uint32_t numChannels,
                 uint32_t numFrames) noexcept SST_CPPUTILS_NONBLOCKING;

    double sampleRate() const noexcept { return config_.sampleRate; }
    /// Frames rendered since construction or the last reset().
    int64_t sampleClock() const noexcept { return clock_; }
    uint32_t activeGrains() const noexcept { return pool_.size(); }
    /// Grains the schedule asked for that found the pool full or, in Live mode, whose read span
    /// doesn't fit the ring, since construction or reset().
    uint64_t droppedGrains() const noexcept { return dropped_; }
    /// The source mode in effect: set by the last process() call (see setSourceMode).
    SourceMode sourceMode() const noexcept { return mode_; }
    /// N, the live ring's length in frames; 0 without a ring.
    int64_t liveFrames() const noexcept { return live_.frames(); }
    /// The write head R: frames written into the live ring since construction or reset(). It
    /// stops while frozen.
    int64_t liveWriteHead() const noexcept { return live_.head(); }

  private:
    void processBlock(const float *const *in, uint32_t numInChannels, float *const *out,
                      uint32_t numChannels, uint32_t offset,
                      uint32_t numFrames) noexcept SST_CPPUTILS_NONBLOCKING;
    /// The grain a draw makes from the sample source.
    GrainSpec sampleGrain(const GrainDraw &d) const noexcept;
    /// The grain a draw makes from the live ring, admission-clamped; false when it can't fit.
    bool liveGrain(const GrainDraw &d, GrainSpec &g) const noexcept;
    void admit(const GrainSpec &g, float *const dst[2], int64_t blockStart,
               int64_t &renderedTo) noexcept SST_CPPUTILS_NONBLOCKING;
    void renderPool(float *const dst[2], int64_t blockStart, int64_t from,
                    int64_t to) noexcept SST_CPPUTILS_NONBLOCKING;
    template <bool TwoOutputs, bool MonoSource>
    void renderGrain(const GrainSpec &g, float *const dst[2], int64_t blockStart, int64_t from,
                     int64_t to) const noexcept SST_CPPUTILS_NONBLOCKING;
    template <bool TwoOutputs>
    void renderLiveGrain(const GrainSpec &g, float *const dst[2], int64_t blockStart, int64_t from,
                         int64_t to) const noexcept SST_CPPUTILS_NONBLOCKING;

    EngineConfig config_;
    HannTable hann_;
    GrainPool pool_;
    CloudScheduler cloud_;
    LiveSource live_;

    // sourceOwner_ keeps the source alive; process() reads only the raw pointer and the
    // cached channel data, so it never touches the reference count.
    std::shared_ptr<const SampleBuffer> sourceOwner_;
    const SampleBuffer *source_{nullptr};
    const float *sourceData_[2]{nullptr, nullptr};
    int64_t sourceFrames_{0};
    double sourceRate_{48000.0};
    bool monoSource_{true};

    // In effect, and as last requested; process() applies the requests at its first frame.
    SourceMode mode_{SourceMode::Sample};
    SourceMode requestedMode_{SourceMode::Sample};
    bool freeze_{false};

    GrainEventSink sink_{nullptr};
    void *sinkCtx_{nullptr};

    int64_t clock_{0};
    uint64_t dropped_{0};
};
} // namespace gg

#endif // GG_ENGINE_ENGINE_H
