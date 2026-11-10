// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#pragma once

#include <sst/basic-blocks/simd/setup.h>
#include <sst/cpputils/rtsan_support.h>

namespace gg::core::fx
{
/// The drive's 2x half-band (color spec C4): a sample-by-sample port of sst-filters'
/// `HalfRateFilter(4, true)` (order 8, steep), bit for bit with its `process_block_U2` and
/// `process_block_D2`. Stereo; one direction per instance (the up and the down filter keep
/// separate state, as the library's two instances would).
///
/// The library's SIMD layout is kept: lanes {L:B, L:A, R:B, R:A}, four cascaded allpass sections,
/// each running two sub-samples per call, so every operation is the library's own and the result
/// is the same on every platform. (The library's process_sample_D2 returns one branch alone; this
/// is the block D2's sum.) The up path keeps the library's 0.5 gain (`process_block_U2`).
/// No allocation; no state beyond the members.
class HalfBand2x
{
  public:
    HalfBand2x() noexcept { reset(); }

    /// Clears the filter state.
    void reset() noexcept SST_CPPUTILS_NONBLOCKING
    {
        for (auto i = 0; i < kSections; ++i)
        {
            x0_[i] = x1_[i] = x2_[i] = y0_[i] = y1_[i] = y2_[i] = SIMD_MM(setzero_ps)();
        }
        oldout_ = SIMD_MM(setzero_ps)();
    }

    /// One base-rate sample in, two oversampled samples out (oldest first).
    void up(float l, float r, float outL[2], float outR[2]) noexcept SST_CPPUTILS_NONBLOCKING
    {
        SIMD_M128 o0 = SIMD_MM(set_ps)(r, r, l, l);
        SIMD_M128 o1 = SIMD_MM(setzero_ps)();
        cascade(o0, o1);

        const auto half = SIMD_MM(set_ps1)(0.5f);
        SIMD_M128 o[2]{o0, o1};
        auto faR = SIMD_MM(setzero_ps)();
        auto fbR = SIMD_MM(setzero_ps)();
        for (int k = 0; k < 2; ++k)
        {
            auto vL = SIMD_MM(add_ss)(o[k], oldout_);
            vL = SIMD_MM(mul_ss)(vL, half);
            SIMD_MM(store_ss)(&outL[k], vL);

            faR = SIMD_MM(movehl_ps)(faR, o[k]);
            fbR = SIMD_MM(movehl_ps)(fbR, oldout_);
            auto vR = SIMD_MM(add_ss)(faR, fbR);
            vR = SIMD_MM(mul_ss)(vR, half);
            SIMD_MM(store_ss)(&outR[k], vR);

            oldout_ = SIMD_MM(shuffle_ps)(o[k], o[k], SIMD_MM_SHUFFLE(3, 3, 1, 1));
        }
    }

    /// Two oversampled samples in, one base-rate sample out.
    void down(const float inL[2], const float inR[2], float &l,
              float &r) noexcept SST_CPPUTILS_NONBLOCKING
    {
        SIMD_M128 o0 = SIMD_MM(set_ps)(inR[0], inR[0], inL[0], inL[0]);
        SIMD_M128 o1 = SIMD_MM(set_ps)(inR[1], inR[1], inL[1], inL[1]);
        cascade(o0, o1);

        // The block D2's reconstruction: A branch of the first sub-sample plus B branch of the
        // second, halved.
        const auto half = SIMD_MM(set_ps1)(0.5f);
        auto tL0 = SIMD_MM(shuffle_ps)(o0, o0, SIMD_MM_SHUFFLE(1, 1, 1, 1));
        auto tR0 = SIMD_MM(shuffle_ps)(o0, o0, SIMD_MM_SHUFFLE(3, 3, 3, 3));
        auto aL = SIMD_MM(add_ss)(tL0, o1);
        auto aR = SIMD_MM(movehl_ps)(SIMD_MM(setzero_ps)(), o1);
        aR = SIMD_MM(add_ss)(aR, tR0);
        aL = SIMD_MM(mul_ss)(aL, half);
        aR = SIMD_MM(mul_ss)(aR, half);
        SIMD_MM(store_ss)(&l, aL);
        SIMD_MM(store_ss)(&r, aR);
    }

  private:
    static constexpr int kSections = 4;

    // The library's allpass step for two consecutive sub-samples through each section.
    void cascade(SIMD_M128 &o0, SIMD_M128 &o1) noexcept
    {
        // va[j] = set_ps(b, a, b, a): lanes {a, b, a, b} from the library's a and b arrays.
        const float a[kSections] = {0.07711507983241622f, 0.4820706250610472f, 0.7968204713315797f,
                                    0.9412514277740471f};
        const float b[kSections] = {0.2659685265210946f, 0.6651041532634957f, 0.8841015085506159f,
                                    0.9820054141886075f};
        for (auto j = 0; j < kSections; ++j)
        {
            auto tx0 = x0_[j];
            auto tx1 = x1_[j];
            auto tx2 = x2_[j];
            auto ty0 = y0_[j];
            auto ty1 = y1_[j];
            auto ty2 = y2_[j];
            const auto ta = SIMD_MM(set_ps)(b[j], a[j], b[j], a[j]);

            tx2 = tx1;
            tx1 = tx0;
            tx0 = o0;
            ty2 = ty1;
            ty1 = ty0;
            ty0 = SIMD_MM(add_ps)(tx2, SIMD_MM(mul_ps)(SIMD_MM(sub_ps)(tx0, ty2), ta));
            o0 = ty0;

            tx2 = tx1;
            tx1 = tx0;
            tx0 = o1;
            ty2 = ty1;
            ty1 = ty0;
            ty0 = SIMD_MM(add_ps)(tx2, SIMD_MM(mul_ps)(SIMD_MM(sub_ps)(tx0, ty2), ta));
            o1 = ty0;

            x0_[j] = tx0;
            x1_[j] = tx1;
            x2_[j] = tx2;
            y0_[j] = ty0;
            y1_[j] = ty1;
            y2_[j] = ty2;
        }
    }

    SIMD_M128 x0_[kSections], x1_[kSections], x2_[kSections];
    SIMD_M128 y0_[kSections], y1_[kSections], y2_[kSections];
    SIMD_M128 oldout_;
};
} // namespace gg::core::fx
