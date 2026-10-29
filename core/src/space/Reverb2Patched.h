// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * sst-effects - an open source library of audio effects
 * built by Surge Synth Team.
 *
 * Copyright 2018-2023, various authors, as described in the GitHub
 * transaction log.
 *
 * sst-effects is released under the GNU General Public Licence v3
 * or later (GPL-3.0-or-later). The license is found in the "LICENSE"
 * file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 *
 * The majority of these effects at initiation were factored from
 * Surge XT, and so git history prior to April 2023 is found in the
 * surge repo, https://github.com/surge-synthesizer/surge
 *
 * All source in sst-effects available at
 * https://github.com/surge-synthesizer/sst-effects
 */
/*
 * Modified for Giga Grains (https://github.com/sethbc/giga-grains), 2026: a copy of
 * sst-effects' include/sst/effects/Reverb2.h at 987b39d, moved to the namespace
 * gg::core::space::patched, with an infinite-decay mode, a smoothed pre-delay and clear()
 * (Giga Grains' Space spec, §6.3-§6.4, S7 and S8). Every change is marked "GG PATCH". With
 * none of the additions used, it renders bit-identically to the original. It remains under
 * GPL-3.0-or-later, as above.
 */
// GG PATCH: our own include guard, so this copy and the original can share a translation unit.
#ifndef GG_CORE_SPACE_REVERB2PATCHED_H
#define GG_CORE_SPACE_REVERB2PATCHED_H

#include <cstring>
// GG PATCH: <algorithm>, <cassert>, <cmath>, <cstdint> and <limits> for the additions, and
// EffectCore.h by its path from sst-effects' include dir.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include "sst/effects/EffectCore.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "sst/basic-blocks/dsp/Lag.h"
#include "sst/basic-blocks/dsp/BlockInterpolators.h"
#include "sst/basic-blocks/dsp/QuadratureOscillators.h"
#include "sst/filters/BiquadFilter.h"
#include "sst/basic-blocks/mechanics/block-ops.h"
#include "sst/basic-blocks/mechanics/simd-ops.h"

// GG PATCH: our namespace. The aliases let the unchanged code below say core:: and
// basic_blocks:: as it did inside sst::effects (core:: would otherwise find gg::core).
namespace gg::core::space::patched
{
namespace core = sst::effects::core;
namespace basic_blocks = sst::basic_blocks;
namespace sdsp = sst::basic_blocks::dsp;
namespace mech = sst::basic_blocks::mechanics;

template <typename FXConfig> struct Reverb2 : core::EffectTemplateBase<FXConfig>
{
    enum rev2_params
    {
        rev2_predelay = 0,

        rev2_room_size,
        rev2_decay_time,
        rev2_diffusion,
        rev2_buildup,
        rev2_modulation,

        rev2_lf_damping,
        rev2_hf_damping,

        rev2_width,
        rev2_mix,

        rev2_num_params,
    };

    static constexpr int numParams{rev2_mix + 1};
    static constexpr const char *streamingName{"reverb2"};
    static constexpr const char *displayName{"Reverb 2"};

    static constexpr int NUM_BLOCKS = 4;
    static constexpr int NUM_INPUT_ALLPASSES = 4;
    static constexpr int NUM_ALLPASSES_PER_BLOCK = 2;
    static constexpr int MAX_ALLPASS_LEN = 16384 * 8;
    static constexpr int MAX_DELAY_LEN = 16384 * 8;
    static constexpr int DELAY_LEN_MASK = MAX_DELAY_LEN - 1;
    static constexpr int DELAY_SUBSAMPLE_BITS = 8;
    static constexpr int DELAY_SUBSAMPLE_RANGE = (1 << DELAY_SUBSAMPLE_BITS);
    static constexpr int PREDELAY_BUFFER_SIZE =
        48000 * 8 * 4; // max sample rate is 48000 * 8 probably
    static constexpr int PREDELAY_BUFFER_SIZE_LIMIT =
        48000 * 8 * 3; // allow for one second of diffusion

    // GG PATCH: a run of floats that clear() still has to zero.
    struct ClearSpan
    {
        float *data;
        size_t count;
    };

    class allpass
    {
      public:
        allpass();
        float process(float x, float coeff);
        void setLen(int len);
        // GG PATCH: starts a clear. Back to the constructed state, but for the data, which it
        // hands to the caller to zero: everything up to the longest length set since the last
        // clear. process() writes only there, and a fresh lap reads every index it visits
        // before writing it, so all of it can be read again.
        int prepareClear(ClearSpan *out) noexcept
        {
            out[0] = {_data, size_t(_highWater)};
            _k = 0;
            _len = 1;
            _highWater = 1;
            return 1;
        }

      private:
        int _len;
        int _k;
        int _highWater{1}; // GG PATCH: the longest _len since construction or clear(), at least 1
        float _data[MAX_ALLPASS_LEN];
    };

    class delay
    {
      public:
        delay();
        float process(float x, int tap1, float &tap_out1, int tap2, float &tap_out2,
                      int modulation);
        void setLen(int len);
        // GG PATCH: starts a clear, after `used` calls of process() since the last one; see
        // prepareLineClear.
        int prepareClear(ClearSpan *out, int64_t used, int reach) noexcept
        {
            _k = 0;
            _len = 1;
            return prepareLineClear(out, _data, MAX_DELAY_LEN, used, reach, _dirtyEnd);
        }

      private:
        int _len;
        int _k;
        int _dirtyEnd{0}; // GG PATCH: see prepareLineClear
        float _data[MAX_DELAY_LEN];
    };

    class predelay
    {
      public:
        predelay() { memset(_data, 0, PREDELAY_BUFFER_SIZE * sizeof(float)); }
        float process(float in, int tap)
        {
            k = (k + 1);
            if (k == PREDELAY_BUFFER_SIZE)
                k = 0;
            auto p = k - tap;
            while (p < 0)
                p += PREDELAY_BUFFER_SIZE;
            auto res = _data[p];
            _data[k] = in;
            return res;
        }
        // GG PATCH: the smoothed path. Writes `in`, then reads `delay` samples back, in
        // [0, PREDELAY_BUFFER_SIZE - 2], interpolating linearly; 0 returns `in` itself.
        float processSmoothed(float in, double delay)
        {
            k = (k + 1);
            if (k == PREDELAY_BUFFER_SIZE)
                k = 0;
            _data[k] = in;
            const int whole = int(delay);
            const float frac = float(delay - double(whole));
            auto p = k - whole;
            if (p < 0)
                p += PREDELAY_BUFFER_SIZE;
            const auto q = p == 0 ? PREDELAY_BUFFER_SIZE - 1 : p - 1;
            return _data[p] + (_data[q] - _data[p]) * frac;
        }
        // GG PATCH: starts a clear, after `used` calls of either process since the last one;
        // see prepareLineClear.
        int prepareClear(ClearSpan *out, int64_t used, int reach) noexcept
        {
            k = 0;
            return prepareLineClear(out, _data, PREDELAY_BUFFER_SIZE, used, reach, _dirtyEnd);
        }

      private:
        int k = 0;
        int _dirtyEnd{0}; // GG PATCH: see prepareLineClear
        float _data[PREDELAY_BUFFER_SIZE];
    };

    class onepole_filter
    {
      public:
        onepole_filter();
        float process_lowpass(float x, float c0);
        float process_highpass(float x, float c0);

      private:
        float a0;
    };

    Reverb2(typename FXConfig::GlobalStorage *s, typename FXConfig::EffectStorage *e,
            typename FXConfig::ValueStorage *p);
    // GG PATCH: neither copyable nor movable: _clearSpans points into this instance's arrays.
    Reverb2(const Reverb2 &) = delete;
    Reverb2 &operator=(const Reverb2 &) = delete;

    void initialize()
    {
        setvars(true);
        // GG PATCH
        _infiniteDcCoef = infiniteDcCoefficient(this->sampleRate());
        _infiniteGlideBlocks = std::max(
            1, int(std::lround(kInfiniteGlideSeconds * this->sampleRate() / FXConfig::blockSize)));
        _infiniteStep = std::min(_infiniteStep, _infiniteGlideBlocks);
    }
    void processBlock(float *__restrict L, float *__restrict R);

    // GG PATCH: Giga Grains' additions (Space spec §6.3-§6.4). Call them between blocks.

    /// How fast the smoothed pre-delay moves toward its target, in samples per sample: a glide
    /// bends the pitch at most an octave down (the read advancing at 0.5) or a fifth up (1.5)
    /// (ruling P1).
    static constexpr double kPredelayMaxRate{0.5};

    /// The LF dampers' cutoff at infinity, in Hz: a DC blocker (ruling R-T8a).
    static constexpr double kInfiniteDcHz{0.5};

    /// How long the loop takes to glide into or out of infinity, in seconds (ruling R-T8b).
    static constexpr double kInfiniteGlideSeconds{0.03};

    /// Infinite decay (S7). At infinity, in each block of the loop:
    /// - the decay multiplier is exactly 1.0f;
    /// - the HF damping coefficient's lower clamp is 0 rather than 0.01, so HF damping 0 is
    ///   lossless;
    /// - **the LF damper becomes a DC blocker** (ruling R-T8a): the same one-pole high-pass,
    ///   with its own state, at kInfiniteDcHz. A lossless loop integrates DC (the grains' small
    ///   offsets would climb toward loopClip's knee over a minute's hold). Over 30 s at
    ///   infinity it costs under 0.1 dB at 50 Hz and up, and 1-2 dB at 10-20 Hz;
    /// - then loopClip bounds the block's signal.
    ///
    /// **The switch glides** (ruling R-T8b): from the next block, the loop moves between its
    /// finite and infinite settings in equal steps over kInfiniteGlideSeconds (whole blocks, at
    /// least one). Each block, with g the share of infinity (0 to 1): the decay multiplier is
    /// decay + g·(1 − decay), the LF coefficient goes geometrically from the LF damping's to
    /// the DC blocker's, ldc·(dc/ldc)^g, the HF clamp's floor is 0.01·(1 − g), and loopClip
    /// runs while g > 0. Each filter keeps its state throughout, so neither entering nor
    /// leaving steps the loop. While no block has run since construction or the last clear(),
    /// g jumps to its target at once, as the pre-delay does (ruling R-T2a).
    ///
    /// At g = 0, the loop is Reverb 2's own.
    void setInfinite(bool on) noexcept
    {
        assert(!_clearing);
        _infinite = on;
    }

    /// The longest smoothed pre-delay: Space's Pre-delay range (spec §6.5).
    static constexpr double kMaxPredelaySeconds{0.5};

    /// The smoothed pre-delay (S8): the delay, in samples, toward which the pre-delay moves at
    /// kPredelayMaxRate, read with linear interpolation. Clamped to [0, kMaxPredelaySeconds ×
    /// the sample rate] (and to the buffer); NaN counts as 0. The first call switches Reverb 2's
    /// own pre-delay (rev2_predelay) off until clear(), and the glide starts from the delay that
    /// one was giving.
    ///
    /// **While no block has run since construction or the last clear(), the delay is set at
    /// once, without a glide**, so a reverb that starts empty starts at its pre-delay (ruling
    /// R-T2a). Call it before the first processBlock after a clear for that.
    void setPredelaySamples(float target) noexcept
    {
        assert(!_clearing);
        double t = target > 0.f ? double(target) : 0.0;
        t = std::min(
            {t, kMaxPredelaySeconds * this->sampleRate(), double(PREDELAY_BUFFER_SIZE - 2)});
        _pdTarget = t;
        if (_samplesSinceClear == 0)
            _pdDelay = t;
        else if (!_pdSmoothed)
            _pdDelay = double(_pdLastTap);
        _pdSmoothed = true;
    }

    /// Back to a freshly constructed and initialize()d instance: the next blocks render exactly
    /// what such an instance would. That includes turning infinite decay and the smoothed
    /// pre-delay off, so call setInfinite and setPredelaySamples again before the next block
    /// (SpaceReverb calls both at every chunk start, setPredelaySamples before the first block
    /// so the pre-delay starts where it should).
    ///
    /// It zeroes only what a later read can reach and was written: each allpass up to its
    /// longest length since the last clear, and of each delay line and the pre-delay, the
    /// oldest positions that a fresh run reads before writing them (clearReaches). That is
    /// exact while, until the next clear, the sample rate is the one in effect now and the
    /// values stay in their ranges: rev2_room_size at most 1, rev2_modulation at most 1, and
    /// any pre-delay, rev2_predelay's included, at most kMaxPredelaySeconds.
    ///
    /// The same as clearStep with no budget; it also finishes a clear clearStep has begun.
    void clear() noexcept { clearStep(std::numeric_limits<size_t>::max()); }

    /// clear(), spread over calls: zeroes at most `maxFloats` floats of what clear() zeroes,
    /// and returns true once all of it is done and the instance is back to fresh. The first
    /// call begins the clear; until one returns true, call neither processBlock nor the
    /// setters (the clear's end undoes them).
    bool clearStep(size_t maxFloats) noexcept
    {
        if (!_clearing)
            beginClear();
        while (_clearSpan < _clearSpanCount)
        {
            auto &span = _clearSpans[_clearSpan];
            if (span.count == 0)
            {
                ++_clearSpan;
                continue;
            }
            if (maxFloats == 0)
                return false;
            const size_t n = std::min(span.count, maxFloats);
            memset(span.data, 0, n * sizeof(float));
            span.data += n;
            span.count -= n;
            maxFloats -= n;
        }
        finishClear();
        return true;
    }

    /// The loop's soft clip at infinity: x within [-1, 1], else sign(x)·(2 − 1/|x|). Bounded
    /// below 2 in magnitude, slope 1 at the knee, no libm.
    static float loopClip(float x) noexcept
    {
        if (x > 1.f)
            return 2.f - 1.f / x;
        if (x < -1.f)
            return -2.f - 1.f / x;
        return x;
    }

    void suspendProcessing() { initialize(); }
    int getRingoutDecay() const { return ringout_time; }
    size_t silentSamplesLength() const { return 10; }
    void onSampleRateChanged() { initialize(); }

    basic_blocks::params::ParamMetaData paramAt(int idx) const
    {
        using pmd = basic_blocks::params::ParamMetaData;
        switch ((rev2_params)idx)
        {
        case rev2_predelay:
            return pmd()
                .asFloat()
                .withName("Pre-Delay")
                .withRange(-8.f, 1.f)
                .withDefault(-4.f)
                .withATwoToTheBFormatting(1.f, 1.f, "s")
                .withCustomMinDisplay("0s")
                .withMilisecondsBelowOneSecond()
                .temposyncable()
                .withTemposyncMultiplier(1.f);

        case rev2_decay_time:
            return pmd()
                .asFloat()
                .withName("Decay Time")
                .withRange(-4.f, 6.f)
                .withDefault(0.75f)
                .withATwoToTheBFormatting(1.f, 1.f, "s")
                .withCustomMinDisplay("0s")
                .withMilisecondsBelowOneSecond();

        case rev2_room_size:
            return pmd().asPercentBipolar().withName("Room Size").withDefault(0.f);

        case rev2_diffusion:
            return pmd().asPercent().withName("Diffusion").withDefault(1.f);

        case rev2_buildup:
            return pmd().asPercent().withName("Buildup").withDefault(1.f);

        case rev2_modulation:
            return pmd().asPercent().withName("Modulation").withDefault(0.5f);

        case rev2_hf_damping:
            return pmd().asPercent().withName("HF Damping").withDefault(0.2f);

        case rev2_lf_damping:
            return pmd().asPercent().withName("LF Damping").withDefault(0.2f);

        case rev2_mix:
            return pmd().asPercent().withName("Mix").withDefault(0.33f);

        case rev2_width:
            return this->getWidthParam();

        default:
            return pmd().asPercentBipolar().withName("ERROR " + std::to_string(idx));
        }
    }

  protected:
    void update_rtime();
    void setvars(bool);
    void calc_size(float);

    int ringout_time;
    allpass _input_allpass[NUM_INPUT_ALLPASSES];
    allpass _allpass[NUM_BLOCKS][NUM_ALLPASSES_PER_BLOCK];
    onepole_filter _hf_damper[NUM_BLOCKS];
    onepole_filter _lf_damper[NUM_BLOCKS];
    delay _delay[NUM_BLOCKS];
    predelay _predelay;
    int _tap_timeL[NUM_BLOCKS];
    int _tap_timeR[NUM_BLOCKS];
    float _tap_gainL[NUM_BLOCKS];
    float _tap_gainR[NUM_BLOCKS];
    float _state;
    sdsp::lipol<float, FXConfig::blockSize, true> _decay_multiply;
    sdsp::lipol<float, FXConfig::blockSize, true> _diffusion;
    sdsp::lipol<float, FXConfig::blockSize, true> _buildup;
    sdsp::lipol<float, FXConfig::blockSize, true> _hf_damp_coefficent;
    sdsp::lipol<float, FXConfig::blockSize, true> _lf_damp_coefficent;
    sdsp::lipol<float, FXConfig::blockSize, true> _modulation;

    using quadr_osc = sst::basic_blocks::dsp::SurgeQuadrOsc<float>;
    quadr_osc _lfo;
    float last_decay_time = -1.0;

    sdsp::lipol_sse<FXConfig::blockSize, false> widthS, widthM, mix;

    // GG PATCH: the additions' state and helpers.

    /// The farthest back, in samples, that a read of a delay line and of the pre-delay can
    /// reach at the current sample rate with values in range (see clear()), plus a margin.
    /// A delay line reads at most its longest length (178.8 ms at Size 2x) plus the
    /// modulation's depth (5 ms at 100%) plus one, behind its write position; the pre-delay
    /// reads at most kMaxPredelaySeconds plus one.
    void clearReaches(int &delayReach, int &predelayReach)
    {
        const auto sr = this->sampleRate();
        delayReach = msToSamples(178.8, 2.f, sr) + int(std::ceil(sr * 0.005)) + 4;
        predelayReach = int(std::ceil(kMaxPredelaySeconds * sr)) + 4;
    }

    /// process_highpass's c0 for a cutoff of kInfiniteDcHz at `sampleRate`: 1 - exp(-w), with
    /// w = 2 pi fc / sampleRate, from its series to w^3. w is below 4e-4 at 8 kHz and up, so
    /// the next term is far below a float's precision, and no libm means every platform gets
    /// the same bits.
    static float infiniteDcCoefficient(double sampleRate) noexcept
    {
        const double w = 2.0 * M_PI * kInfiniteDcHz / sampleRate;
        return float(w - w * w / 2.0 + w * w * w / 6.0);
    }

    /// Lists, in out, what a clear of a delay line or the pre-delay must zero, and keeps the
    /// line's bookkeeping. Such a line writes position 1, 2, ... after construction or a clear,
    /// sweeping its whole buffer whatever its length, and a read `d` back from write position
    /// n lands on n - d. After a clear (which restarts the positions at 0) a read can see what
    /// was there before only at position 0 (read at n = d, written again only after a whole
    /// sweep) and at the positions past size - reach (where n - d wraps). So it zeroes those,
    /// where they may hold anything: position 0 when the line has swept it since the last
    /// clear, and the window up to dirtyEnd, the highest position written since a clear left
    /// it, which spans several clears if the reach grows.
    static int prepareLineClear(ClearSpan *out, float *data, int size, int64_t used, int reach,
                                int &dirtyEnd) noexcept
    {
        int n = 0;
        const bool swept = used >= size;
        const int end = swept ? size - 1 : int(std::max<int64_t>(dirtyEnd, used));
        if (swept)
            out[n++] = {data, 1};
        const int windowStart = std::max(1, size - reach);
        if (end >= windowStart)
            out[n++] = {data + windowStart, size_t(end - windowStart + 1)};
        dirtyEnd = std::min(end, windowStart - 1);
        return n;
    }

    /// Starts a clear: lists what to zero and resets the lines' positions.
    void beginClear() noexcept
    {
        int delayReach, predelayReach;
        clearReaches(delayReach, predelayReach);
        int n = 0;
        for (auto &a : _input_allpass)
            n += a.prepareClear(_clearSpans + n);
        for (auto &block : _allpass)
            for (auto &a : block)
                n += a.prepareClear(_clearSpans + n);
        for (auto &d : _delay)
            n += d.prepareClear(_clearSpans + n, _samplesSinceClear, delayReach);
        n += _predelay.prepareClear(_clearSpans + n, _samplesSinceClear, predelayReach);
        _clearSpanCount = n;
        _clearSpan = 0;
        _clearing = true;
    }

    /// Ends a clear, the lines zeroed: every other piece of state back to fresh.
    void finishClear() noexcept
    {
        for (auto &d : _hf_damper)
            d = onepole_filter();
        for (auto &d : _lf_damper)
            d = onepole_filter();
        _state = 0.f;
        _decay_multiply.reset();
        _diffusion.reset();
        _buildup.reset();
        _hf_damp_coefficent.reset();
        _lf_damp_coefficent.reset();
        _modulation.reset();
        _lfo.r = 0.f; // SurgeQuadrOsc's constructed phase; set_rate sets the rest each block
        _lfo.i = -1.f;
        last_decay_time = -1.0;
        for (auto *l : {&widthS, &widthM, &mix})
        {
            l->set_target_instant(0.f);
            l->first_run = true;
        }
        _infinite = false;
        _infiniteStep = 0;
        _pdSmoothed = false;
        _pdDelay = 0.0;
        _pdTarget = 0.0;
        _pdLastTap = 0;
        _samplesSinceClear = 0;
        _clearing = false;
        _clearSpanCount = 0;
        _clearSpan = 0;
        initialize();
    }

    static constexpr int kMaxClearSpans{NUM_INPUT_ALLPASSES + NUM_BLOCKS * NUM_ALLPASSES_PER_BLOCK +
                                        2 * NUM_BLOCKS + 2};
    ClearSpan _clearSpans[kMaxClearSpans]{};
    int _clearSpanCount{0};
    int _clearSpan{0};
    bool _clearing{false};

    bool _infinite{false};
    int _infiniteStep{0};          // of _infiniteGlideBlocks: g = _infiniteStep / that
    int _infiniteGlideBlocks{1};   // the glide's length in blocks, at least 1
    float _infiniteDcCoef{0.f};    // process_highpass's c0 for kInfiniteDcHz
    bool _pdSmoothed{false};       // setPredelaySamples has been called since the last clear()
    double _pdDelay{0.0};          // the smoothed pre-delay now, in samples (64-bit: a position)
    double _pdTarget{0.0};         // and where it is going
    int _pdLastTap{0};             // Reverb 2's own pre-delay in the last block, in samples
    int64_t _samplesSinceClear{0}; // samples processed since construction or clear()

    inline int msToSamples(float ms, float scale, float samplerate)
    {
        float a = samplerate * ms * 0.001f;
        float b = a * scale;
        // these are clamped out above
        // assert( b < Reverb2Effect::MAX_ALLPASS_LEN);
        // assert( b < Reverb2Effect::MAX_DELAY_LEN);
        return (int)(b);
    }

    static constexpr double db60{0.001}; // powf(10.f, 0.05f * -60.f);
  public:
    static constexpr int16_t streamingVersion{1};
    static void remapParametersForStreamingVersion([[maybe_unused]] int16_t streamedFrom,
                                                   float *const /*param*/)
    {
        // base implementation - we have never updated streaming
        // input is parameters from stream version
        assert(streamedFrom == 1);
    }
};

template <typename FXConfig>
Reverb2<FXConfig>::Reverb2(typename FXConfig::GlobalStorage *s, typename FXConfig::EffectStorage *e,
                           typename FXConfig::ValueStorage *p)
    : core::EffectTemplateBase<FXConfig>(s, e, p)
{
    static_assert(core::ValidEffect<Reverb2>);
    _state = 0.f;
}

template <typename FXConfig> Reverb2<FXConfig>::allpass::allpass()
{
    _k = 0;
    _len = 1;
    memset(_data, 0, MAX_ALLPASS_LEN * sizeof(float));
}

template <typename FXConfig> void Reverb2<FXConfig>::allpass::setLen(int len)
{
    _len = std::clamp(len, 0, MAX_ALLPASS_LEN - 1);
    _highWater = std::max(_highWater, _len); // GG PATCH
}

template <typename FXConfig> float Reverb2<FXConfig>::allpass::process(float in, float coeff)
{
    _k++;
    if (_k >= _len)
        _k = 0;
    float delay_in = in - coeff * _data[_k];
    float result = _data[_k] + coeff * delay_in;
    _data[_k] = delay_in;
    return result;
}

template <typename FXConfig> Reverb2<FXConfig>::delay::delay()
{
    _k = 0;
    _len = 1;
    memset(_data, 0, MAX_DELAY_LEN * sizeof(float));
}

template <typename FXConfig> void Reverb2<FXConfig>::delay::setLen(int len)
{
    _len = std::clamp(len, 0, MAX_DELAY_LEN - 1);
}

template <typename FXConfig>
float Reverb2<FXConfig>::delay::process(float in, int tap1, float &tap_out1, int tap2,
                                        float &tap_out2, int modulation)
{
    _k = (_k + 1) & DELAY_LEN_MASK;

    tap_out1 = _data[(_k - tap1) & DELAY_LEN_MASK];
    tap_out2 = _data[(_k - tap2) & DELAY_LEN_MASK];

    int modulation_int = modulation >> DELAY_SUBSAMPLE_BITS;
    int modulation_frac1 = modulation & (DELAY_SUBSAMPLE_RANGE - 1);
    int modulation_frac2 = DELAY_SUBSAMPLE_RANGE - modulation_frac1;

    float d1 = _data[(_k - _len + modulation_int + 1) & DELAY_LEN_MASK];
    float d2 = _data[(_k - _len + modulation_int) & DELAY_LEN_MASK];
    const float multiplier = 1.f / (float)(DELAY_SUBSAMPLE_RANGE);

    float result = (d1 * (float)modulation_frac1 + d2 * (float)modulation_frac2) * multiplier;
    _data[_k] = in;

    return result;
}

template <typename FXConfig> Reverb2<FXConfig>::onepole_filter::onepole_filter() { a0 = 0.f; }

template <typename FXConfig>
float Reverb2<FXConfig>::onepole_filter::process_lowpass(float x, float c0)
{
    a0 = a0 * c0 + x * (1.f - c0);
    return a0;
}

template <typename FXConfig>
float Reverb2<FXConfig>::onepole_filter::process_highpass(float x, float c0)
{
    a0 = a0 * (1.f - c0) + x * c0;
    return x - a0;
}

template <typename FXConfig> void Reverb2<FXConfig>::update_rtime()
{
    auto ts = this->temposyncRatio(rev2_predelay);
    // * 2.f is to get the dB120 time
    auto pdlyt = std::max(0.1f, powf(2.f, this->floatValue(rev2_predelay)) * ts) * 2.f;
    auto dcyt = std::max(1.0f, powf(2.f, this->floatValue(rev2_decay_time))) * 2.f;
    float t = (this->sampleRate() * (dcyt + pdlyt)) / FXConfig::blockSize;

    ringout_time = (int)t;
}

template <typename FXConfig> void Reverb2<FXConfig>::calc_size(float scale)
{
    float m = scale;

    auto sr = this->sampleRate();
    _tap_timeL[0] = msToSamples(80.3, m, sr);
    _tap_timeL[1] = msToSamples(59.3, m, sr);
    _tap_timeL[2] = msToSamples(97.7, m, sr);
    _tap_timeL[3] = msToSamples(122.6, m, sr);
    _tap_timeR[0] = msToSamples(35.5, m, sr);
    _tap_timeR[1] = msToSamples(101.6, m, sr);
    _tap_timeR[2] = msToSamples(73.9, m, sr);
    _tap_timeR[3] = msToSamples(80.3, m, sr);

    _input_allpass[0].setLen(msToSamples(4.76, m, sr));
    _input_allpass[1].setLen(msToSamples(6.81, m, sr));
    _input_allpass[2].setLen(msToSamples(10.13, m, sr));
    _input_allpass[3].setLen(msToSamples(16.72, m, sr));

    _allpass[0][0].setLen(msToSamples(38.2, m, sr));
    _allpass[0][1].setLen(msToSamples(53.4, m, sr));
    _delay[0].setLen(msToSamples(178.8, m, sr));

    _allpass[1][0].setLen(msToSamples(44.0, m, sr));
    _allpass[1][1].setLen(msToSamples(41, m, sr));
    _delay[1].setLen(msToSamples(126.5, m, sr));

    _allpass[2][0].setLen(msToSamples(48.3, m, sr));
    _allpass[2][1].setLen(msToSamples(60.5, m, sr));
    _delay[2].setLen(msToSamples(106.1, m, sr));

    _allpass[3][0].setLen(msToSamples(38.9, m, sr));
    _allpass[3][1].setLen(msToSamples(42.2, m, sr));
    _delay[3].setLen(msToSamples(139.4, m, sr));
}

template <typename FXConfig> void Reverb2<FXConfig>::setvars(bool /*init*/)
{
    // TODO: balance the gains from the calculated decay coefficient?
    _tap_gainL[0] = 1.5f / 4.f;
    _tap_gainL[1] = 1.2f / 4.f;
    _tap_gainL[2] = 1.0f / 4.f;
    _tap_gainL[3] = 0.8f / 4.f;
    _tap_gainR[0] = 1.5f / 4.f;
    _tap_gainR[1] = 1.2f / 4.f;
    _tap_gainR[2] = 1.0f / 4.f;
    _tap_gainR[3] = 0.8f / 4.f;

    calc_size(1.f);
}

template <typename FXConfig> void Reverb2<FXConfig>::processBlock(float *dataL, float *dataR)
{
    assert(!_clearing); // GG PATCH
    float scale = powf(2.f, 1.f * this->floatValue(rev2_room_size));
    calc_size(scale);

    if (fabs(this->floatValue(rev2_decay_time) - last_decay_time) > 0.001f)
        update_rtime();

    last_decay_time = this->floatValue(rev2_decay_time);

    float wetL alignas(16)[FXConfig::blockSize], wetR alignas(16)[FXConfig::blockSize];

    float loop_time_s = 0.5508 * scale;
    float decay = powf(db60, loop_time_s / (4.f * (powf(2.f, this->floatValue(rev2_decay_time)))));

    // GG PATCH: the glide into and out of infinity (ruling R-T8b): g, its share, steps one
    // block's worth toward _infinite, or jumps there before the first block.
    const int stepTarget = _infinite ? _infiniteGlideBlocks : 0;
    if (_samplesSinceClear == 0)
        _infiniteStep = stepTarget;
    else if (_infiniteStep < stepTarget)
        ++_infiniteStep;
    else if (_infiniteStep > stepTarget)
        --_infiniteStep;
    const float g = _infiniteStep == _infiniteGlideBlocks
                        ? 1.f
                        : float(_infiniteStep) / float(_infiniteGlideBlocks);
    if (g == 1.f)
        decay = 1.f;
    else if (g > 0.f)
        decay = decay + g * (1.f - decay);

    _decay_multiply.newValue(decay);
    _diffusion.newValue(0.7f * this->floatValue(rev2_diffusion));
    _buildup.newValue(0.7f * this->floatValue(rev2_buildup));
    _hf_damp_coefficent.newValue(0.8 * this->floatValue(rev2_hf_damping));
    _lf_damp_coefficent.newValue(0.2 * this->floatValue(rev2_lf_damping));
    _modulation.newValue(this->floatValue(rev2_modulation) * this->sampleRate() * 0.001f * 5.f);

    this->setWidthTarget(widthS, widthM, rev2_width);

    mix.set_target_smoothed(this->floatValue(rev2_mix));

    _lfo.set_rate(2.0 * M_PI * powf(2, -2.f) / this->sampleRate());

    int pdt = std::clamp((int)(this->sampleRate() * pow(2.f, this->floatValue(rev2_predelay)) *
                               this->temposyncRatioInv(rev2_predelay)),
                         1, PREDELAY_BUFFER_SIZE_LIMIT - 1);
    _pdLastTap = pdt; // GG PATCH

    // GG PATCH: the LF coefficient toward and at infinity, once a block (_lf_damp_coefficent
    // doesn't move within one): geometric in g from the LF damping's to the DC blocker's.
    float ldcInfinite = _infiniteDcCoef;
    if (g > 0.f && g < 1.f)
    {
        const float l = std::clamp(_lf_damp_coefficent.v, 0.01f, 0.99f);
        ldcInfinite = l * powf(_infiniteDcCoef / l, g);
    }

    for (int k = 0; k < FXConfig::blockSize; k++)
    {
        float in = (dataL[k] + dataR[k]) * 0.5f;

        // GG PATCH: the smoothed pre-delay, once setPredelaySamples has been called.
        if (_pdSmoothed)
        {
            _pdDelay += std::clamp(_pdTarget - _pdDelay, -kPredelayMaxRate, kPredelayMaxRate);
            in = _predelay.processSmoothed(in, _pdDelay);
        }
        else
            in = _predelay.process(in, pdt);

        in = _input_allpass[0].process(in, _diffusion.v);
        in = _input_allpass[1].process(in, _diffusion.v);
        in = _input_allpass[2].process(in, _diffusion.v);
        in = _input_allpass[3].process(in, _diffusion.v);
        float x = _state;

        float outL = 0.f;
        float outR = 0.f;

        float lfos[NUM_BLOCKS];
        lfos[0] = _lfo.r;
        lfos[1] = _lfo.i;
        lfos[2] = -_lfo.r;
        lfos[3] = -_lfo.i;

        // GG PATCH: the HF clamp's floor and the LF coefficient glide with g (see setInfinite).
        auto hdc = std::clamp(_hf_damp_coefficent.v, 0.01f * (1.f - g), 0.99f);
        auto ldc = g > 0.f ? ldcInfinite : std::clamp(_lf_damp_coefficent.v, 0.01f, 0.99f);
        for (int b = 0; b < NUM_BLOCKS; b++)
        {
            x = x + in;
            for (int c = 0; c < NUM_ALLPASSES_PER_BLOCK; c++)
            {
                x = _allpass[b][c].process(x, _buildup.v);
            }

            x = _hf_damper[b].process_lowpass(x, hdc);
            x = _lf_damper[b].process_highpass(x, ldc);
            // GG PATCH: toward and at infinity, the soft clip.
            if (g > 0.f)
                x = loopClip(x);

            int modulation = (int)(_modulation.v * lfos[b] * (float)DELAY_SUBSAMPLE_RANGE);
            float tap_outL = 0.f;
            float tap_outR = 0.f;
            x = _delay[b].process(x, _tap_timeL[b], tap_outL, _tap_timeR[b], tap_outR, modulation);
            outL += tap_outL * _tap_gainL[b];
            outR += tap_outR * _tap_gainR[b];

            x *= _decay_multiply.v;
        }

        wetL[k] = outL;
        wetR[k] = outR;
        _state = x;
        _decay_multiply.process();
        _diffusion.process();
        _buildup.process();
        _hf_damp_coefficent.process();
        _lfo.process();
        _modulation.process();
    }

    _samplesSinceClear += FXConfig::blockSize; // GG PATCH

    // scale width
    this->applyWidth(wetL, wetR, widthS, widthM);

    mix.fade_2_blocks_inplace(dataL, wetL, dataR, wetR);
}

} // namespace gg::core::space::patched

// GG PATCH: the instantiation Space uses.
#include "space/GgFxConfig.h"

namespace gg::core::space
{
/// Reverb 2 with Giga Grains' patch (S7, S8), under GgFxConfig.
using Rev2Patched = patched::Reverb2<GgFxConfig>;
} // namespace gg::core::space

#endif
