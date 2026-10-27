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
#include "gg/source/SampleBuffer.h"

namespace gg
{
struct EngineConfig
{
    double sampleRate{48000.0};  ///< Output sample rate; finite and > 0.
    uint32_t maxBlockSize{4096}; ///< process() works in blocks of at most this many frames; > 0.
    uint32_t grainCapacity{4096};
    uint64_t seed{0};
};

/// Called on the audio thread, from inside process(), once for every grain that spawns, in
/// spawn order. It must be real-time safe.
using GrainEventSink = void (*)(void *ctx, const GrainSpec &);

/// The grain engine: one Cloud grain stream (layer 0, stream 0) reading one source.
///
/// With the same configuration, source and parameter changes (at the same sample positions),
/// the output and the grain events are bit-identical however the host splits blocks
/// (INTENT §8.6, §14):
/// - each grain's read position and window phase come from its elapsed time since its onset,
///   never from state accumulated across blocks;
/// - grains are summed in spawn order;
/// - a grain is admitted when fewer than grainCapacity grains are still sounding at its onset,
///   and otherwise dropped. That depends only on the schedule, not on block boundaries.
class Engine
{
  public:
    /// Allocates everything process() needs. Throws std::invalid_argument if sampleRate is
    /// not finite and positive or maxBlockSize is 0.
    explicit Engine(const EngineConfig &config);

    /// Not real-time safe: call only while process() is not running. The engine keeps the
    /// buffer alive until the next setSource or its destruction. A mono source feeds both
    /// output channels; a stereo (or wider) source feeds its first two channels to them.
    /// nullptr means no source: the schedule keeps running but no grains spawn.
    void setSource(std::shared_ptr<const SampleBuffer> source);

    /// Real-time safe, but not synchronized with process(): call it on the audio thread
    /// between process() calls, or while process() is not running. The new parameters apply
    /// from the next process() call, at its first sample g: to every grain that spawns from then
    /// on. A density or distribution change also moves the pending onset (ruling M1-h; see
    /// CloudScheduler), so for a block-size-invariant render, make changes at fixed samples.
    void setCloudParams(const CloudParams &params) noexcept;

    /// The sink is called from process(); pass nullptr to remove it. Real-time safe, but not
    /// synchronized with process(): call it on the audio thread between process() calls, or
    /// while process() is not running.
    void setEventSink(GrainEventSink sink, void *ctx) noexcept;

    /// Silences every grain and restarts the clock and the schedule at 0. Keeps the source,
    /// the Cloud parameters and the sink. Real-time safe, but not synchronized with
    /// process(): call it on the audio thread between process() calls, or while process() is
    /// not running.
    void reset() noexcept;

    /// Renders numFrames frames, overwriting out[0 .. numChannels). Channels 0 and 1 carry
    /// the grains, each grain panned by panGains(); any further channels are zeroed. Accepts
    /// any numFrames, including 0 (which does nothing) and more than maxBlockSize (which is
    /// split internally).
    ///
    /// The schedule and the clock advance whatever the outputs are, so the next call
    /// continues the same render:
    /// - with one channel, it carries source channel 0, unpanned;
    /// - out == nullptr renders nothing;
    /// - a null channel pointer is skipped, and when out[0] is null nothing is rendered, so
    ///   channel 1 (if not null) is only zeroed.
    void process(float *const *out, uint32_t numChannels,
                 uint32_t numFrames) noexcept SST_CPPUTILS_NONBLOCKING;

    double sampleRate() const noexcept { return config_.sampleRate; }
    /// Frames rendered since construction or the last reset().
    int64_t sampleClock() const noexcept { return clock_; }
    uint32_t activeGrains() const noexcept { return pool_.size(); }
    /// Grains the schedule asked for that found the pool full, since construction or reset().
    uint64_t droppedGrains() const noexcept { return dropped_; }

  private:
    void processBlock(float *const *out, uint32_t numChannels, uint32_t offset,
                      uint32_t numFrames) noexcept SST_CPPUTILS_NONBLOCKING;
    /// The grain a draw makes from the sample source.
    GrainSpec sampleGrain(const GrainDraw &d) const noexcept;
    void admit(const GrainSpec &g, float *const dst[2], int64_t blockStart,
               int64_t &renderedTo) noexcept SST_CPPUTILS_NONBLOCKING;
    void renderPool(float *const dst[2], int64_t blockStart, int64_t from,
                    int64_t to) noexcept SST_CPPUTILS_NONBLOCKING;
    template <bool TwoOutputs, bool MonoSource>
    void renderGrain(const GrainSpec &g, float *const dst[2], int64_t blockStart, int64_t from,
                     int64_t to) const noexcept SST_CPPUTILS_NONBLOCKING;

    EngineConfig config_;
    HannTable hann_;
    GrainPool pool_;
    CloudScheduler cloud_;

    // sourceOwner_ keeps the source alive; process() reads only the raw pointer and the
    // cached channel data, so it never touches the reference count.
    std::shared_ptr<const SampleBuffer> sourceOwner_;
    const SampleBuffer *source_{nullptr};
    const float *sourceData_[2]{nullptr, nullptr};
    int64_t sourceFrames_{0};
    double sourceRate_{48000.0};
    bool monoSource_{true};

    GrainEventSink sink_{nullptr};
    void *sinkCtx_{nullptr};

    int64_t clock_{0};
    uint64_t dropped_{0};
};
} // namespace gg

#endif // GG_ENGINE_ENGINE_H
