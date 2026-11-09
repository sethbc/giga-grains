// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)
//
// Carry-over row 403's probe, on a throwaway branch only: no product code, so a mismatch here
// cannot be the ring reader, the seqlock or the engine. CI runs it under Rosetta beside the
// x86_64 slice's three shards. Three threads, until the stop file exists:
//   fresh   the failing test's shape: a new buffer each pass, filled, then compared;
//   reuse   the same, one buffer for the whole run;
//   sse     the kernel tests' shape: SSE arithmetic compared bit for bit with a scalar reference.
// A line starting MISMATCH is a value this thread wrote or computed that it then saw differ, with
// the bits the compare actually held. Always exits 0: the output is the result.

#include <bit>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

#include <immintrin.h>
#include <unistd.h>

namespace
{
constexpr int64_t kFrames = 48000;
constexpr int kMaxReports = 40;
const char *stopPath = nullptr;

bool stopRequested() { return access(stopPath, F_OK) == 0; }

/// Makes the compiler forget what it knows about the memory behind p, so the compares reload it.
inline void opaque(const void *p) { asm volatile("" : : "r"(p) : "memory"); }

uint32_t bits(float f) { return std::bit_cast<uint32_t>(f); }

void rampMode(const char *name, bool fresh)
{
    std::unique_ptr<float[]> leftStore(new float[kFrames]);
    std::unique_ptr<float[]> rightStore(new float[kFrames]);
    int64_t t = 0;
    uint64_t passes = 0;
    uint64_t compares = 0;
    uint64_t mismatches = 0;
    while (!stopRequested())
    {
        if (fresh)
        {
            leftStore.reset(new float[kFrames]);
            rightStore.reset(new float[kFrames]);
        }
        float *left = leftStore.get();
        float *right = rightStore.get();
        for (int64_t f = 0; f < kFrames; ++f)
        {
            const auto v = static_cast<float>((t + f) % 1000000);
            left[f] = v;
            right[f] = -v;
        }
#ifdef ROW403_INJECT
        if (passes == 3)
        {
            left[100] += 1.0f; // the local negative control: the probe must report this
        }
#endif
        opaque(left);
        opaque(right);
        for (int64_t f = 0; f < kFrames; ++f)
        {
            const auto v = static_cast<float>((t + f) % 1000000);
            const float l = left[f];
            const float r = right[f];
            ++compares;
            if (l != v || r != -v)
            {
                ++mismatches;
                if (mismatches <= kMaxReports)
                {
                    const float againL = static_cast<const volatile float *>(left)[f];
                    const float againR = static_cast<const volatile float *>(right)[f];
                    const auto expected = static_cast<float>((t + f) % 1000000);
                    int64_t rescan = 0;
                    for (int64_t g = 0; g < kFrames; ++g)
                    {
                        const auto w = static_cast<float>((t + g) % 1000000);
                        rescan += left[g] != w || right[g] != -w ? 1 : 0;
                    }
                    std::printf("MISMATCH %s pass %llu frame %lld: compare held L %08x R %08x v "
                                "%08x; recomputed v %08x; memory now L %08x R %08x; held operands "
                                "%s; rescan bad %lld\n",
                                name, static_cast<unsigned long long>(passes),
                                static_cast<long long>(f), bits(l), bits(r), bits(v),
                                bits(expected), bits(againL), bits(againR),
                                bits(l) == bits(expected) && bits(r) == bits(-expected)
                                    ? "right (the compare misfired)"
                                    : "wrong (a load or a conversion)",
                                static_cast<long long>(rescan));
                    std::fflush(stdout);
                }
            }
        }
        t += 256;
        ++passes;
    }
    std::printf("%s: passes %llu, compares %llu, mismatches %llu\n", name,
                static_cast<unsigned long long>(passes), static_cast<unsigned long long>(compares),
                static_cast<unsigned long long>(mismatches));
    std::fflush(stdout);
}

void sseMode()
{
    constexpr int kSize = 4096;
    std::vector<float> a(kSize), b(kSize), c(kSize), reference(kSize), out(kSize);
    uint32_t state = 12345u;
    const auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) * 0x1.0p-24f * 2.0f - 1.0f;
    };
    for (int i = 0; i < kSize; ++i)
    {
        a[i] = next();
        b[i] = next();
        c[i] = next();
        reference[i] = a[i] * b[i] + c[i];
    }
#ifdef ROW403_INJECT
    reference[7] += 1.0f; // the local negative control
#endif
    const auto render = [&] {
        for (int i = 0; i + 4 <= kSize; i += 4)
        {
            const __m128 x = _mm_loadu_ps(a.data() + i);
            const __m128 y = _mm_loadu_ps(b.data() + i);
            const __m128 z = _mm_loadu_ps(c.data() + i);
            _mm_storeu_ps(out.data() + i, _mm_add_ps(_mm_mul_ps(x, y), z));
        }
        opaque(out.data());
    };
    uint64_t passes = 0;
    uint64_t compares = 0;
    uint64_t mismatches = 0;
    while (!stopRequested())
    {
        for (int pass = 0; pass < 2000; ++pass)
        {
            render();
            for (int i = 0; i < kSize; ++i)
            {
                ++compares;
                const uint32_t got = bits(out[i]);
                const uint32_t want = bits(reference[i]);
                if (got != want)
                {
                    ++mismatches;
                    if (mismatches <= kMaxReports)
                    {
                        render();
                        std::printf("MISMATCH sse pass %llu index %d: got %08x want %08x; a second "
                                    "render holds %08x\n",
                                    static_cast<unsigned long long>(passes), i, got, want,
                                    bits(out[i]));
                        std::fflush(stdout);
                    }
                }
            }
            ++passes;
        }
    }
    std::printf("sse: passes %llu, compares %llu, mismatches %llu\n",
                static_cast<unsigned long long>(passes), static_cast<unsigned long long>(compares),
                static_cast<unsigned long long>(mismatches));
    std::fflush(stdout);
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: row403-probe <stop-file>\n");
        return 2;
    }
    stopPath = argv[1];
    std::thread fresh(rampMode, "fresh", true);
    std::thread reuse(rampMode, "reuse", false);
    std::thread sse(sseMode);
    fresh.join();
    reuse.join();
    sse.join();
    return 0;
}
