// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// gg-bench times the prototype grain kernels in Kernels.h. It is the M0 kernel benchmark spike,
// whose numbers set the CPU target in INTENT §16.
//
//   gg-bench [--quick] [--json out.json]
//
// The workload is a 60 s stereo noise source at 48 kHz, read by 2,000 grains of 100 ms. Each
// grain has a random position and a random rate in [0.5, 2]. Every kernel runs at block sizes
// 32, 64 and 256.
//
// Each configuration starts from the same grains, warms up, then makes 5 timed runs and reports
// the median. A run renders blocks until at least 2 s have passed (0.2 s with --quick). A timed
// block includes what an engine does around the kernel: clearing the output and the per-block
// grain update, which advances every grain and respawns those whose window has ended. It also
// sums the output into a checksum, so the compiler can't drop the work; that costs about one
// add per output sample, against 2,000 grain-samples.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#if defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <fstream>
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(__x86_64__))
#include <intrin.h>
#endif

#include "Kernels.h"
#include "gg/core/DenormalGuard.h"
#include "gg/core/Rng.h"

namespace
{
using gg::bench::GrainBatch;
using gg::bench::Kernel;

constexpr double kSampleRate = 48000.0;
constexpr int64_t kSourceFrames = 60 * 48000;
constexpr std::size_t kGrains = 2000;
constexpr double kGrainSamples = 0.1 * kSampleRate;
constexpr double kMinRate = 0.5;
constexpr double kMaxRate = 2.0;
constexpr uint32_t kMaxBlockSize = 256;
constexpr std::array<uint32_t, 3> kBlockSizes{32, 64, kMaxBlockSize};
constexpr int kRuns = 5;
constexpr uint64_t kSeed = 0x67672d62656e6368ull; // "gg-bench"

struct KernelInfo
{
    const char *name;
    Kernel fn;
};

constexpr std::array<KernelInfo, 5> kKernels{{
    {"linear-scalar", gg::bench::renderLinearScalar},
    {"hermite-scalar", gg::bench::renderHermiteScalar},
    {"sinc-scalar", gg::bench::renderSincScalar},
    {"linear-simd", gg::bench::renderLinearSimd},
    {"hermite-simd", gg::bench::renderHermiteSimd},
}};

/// Which quantity a draw is for; each gets its own RNG stream.
enum Stream : uint32_t
{
    kNoiseL,
    kNoiseR,
    kPosition,
    kRate,
    kGain,
    kStartPhase,
};

double draw(Stream stream, uint64_t index)
{
    return gg::rng::uniform01(
        gg::rng::hash({kSeed, 0, stream, gg::rng::Field::SignalNoise, index}));
}

/// The source, and grains that keep reading it: whenever a grain's window ends, the grain
/// respawns at a new random position and rate.
struct Workload
{
    std::vector<float> srcL, srcR;
    GrainBatch grains;
    uint64_t spawns{0}; // grains spawned so far; indexes each spawn's draws

    Workload()
        : srcL(static_cast<std::size_t>(kSourceFrames)),
          srcR(static_cast<std::size_t>(kSourceFrames))
    {
        for (std::size_t i = 0; i < srcL.size(); ++i)
        {
            const auto i64 = static_cast<uint64_t>(i);
            srcL[i] = static_cast<float>(
                0.5 * gg::rng::bipolar(
                          gg::rng::hash({kSeed, 0, kNoiseL, gg::rng::Field::SignalNoise, i64})));
            srcR[i] = static_cast<float>(
                0.5 * gg::rng::bipolar(
                          gg::rng::hash({kSeed, 0, kNoiseR, gg::rng::Field::SignalNoise, i64})));
        }
    }

    /// Back to the same starting grains, with their windows at staggered random phases.
    void reset()
    {
        spawns = 0;
        grains.resize(kGrains);
        for (std::size_t g = 0; g < kGrains; ++g)
        {
            spawn(g, static_cast<float>(draw(kStartPhase, g)));
        }
    }

    void spawn(std::size_t g, float envPhase)
    {
        // Keep every read inside the source, so the kernels run their usual in-bounds path:
        // a grain lives one window plus at most one block past its end, at up to kMaxRate.
        constexpr double margin = 16.0;
        constexpr double reach = kMaxRate * (kGrainSamples + kMaxBlockSize);
        const double span = static_cast<double>(kSourceFrames) - 2.0 * margin - reach;
        const uint64_t s = spawns++;
        grains.pos[g] = margin + span * draw(kPosition, s);
        grains.rate[g] = kMinRate + (kMaxRate - kMinRate) * draw(kRate, s);
        grains.envPhase[g] = envPhase;
        grains.envInc[g] = static_cast<float>(1.0 / kGrainSamples);
        grains.gain[g] = static_cast<float>(0.5 + 0.5 * draw(kGain, s));
    }

    /// Moves every grain on by n samples, respawning those whose window has ended.
    void advance(uint32_t n)
    {
        for (std::size_t g = 0; g < grains.size(); ++g)
        {
            grains.pos[g] += static_cast<double>(n) * grains.rate[g];
            const float phase = grains.envPhase[g] + static_cast<float>(n) * grains.envInc[g];
            if (phase >= 1.0f)
            {
                spawn(g, phase - 1.0f);
            }
            else
            {
                grains.envPhase[g] = phase;
            }
        }
    }
};

volatile double gSink = 0.0; // the rendered output feeds this, so it can't be optimised away

/// Renders blocks for at least `seconds`; returns nanoseconds per stereo grain-sample.
double timeRun(Workload &w, Kernel fn, uint32_t n, double seconds, std::vector<float> &outL,
               std::vector<float> &outR)
{
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    const auto deadline =
        start + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
    double sink = 0.0;
    uint64_t blocks = 0;
    auto now = start;
    do
    {
        std::fill_n(outL.begin(), n, 0.0f);
        std::fill_n(outR.begin(), n, 0.0f);
        fn(w.grains, w.srcL.data(), w.srcR.data(), kSourceFrames, outL.data(), outR.data(), n);
        w.advance(n);
        for (uint32_t i = 0; i < n; ++i)
        {
            sink += outL[i] + outR[i];
        }
        ++blocks;
        now = Clock::now();
    } while (now < deadline);
    gSink = gSink + sink;

    const double ns = std::chrono::duration<double, std::nano>(now - start).count();
    const double grainSamples =
        static_cast<double>(blocks) * n * static_cast<double>(w.grains.size());
    return ns / grainSamples;
}

struct Result
{
    const char *kernel;
    std::size_t grains;
    uint32_t blockSize;
    double nsPerStereoGrainSample;
    double cpuPctOneCoreAt48k;
};

Result measure(Workload &w, const KernelInfo &k, uint32_t n, double seconds)
{
    std::vector<float> outL(n), outR(n);
    w.reset();
    timeRun(w, k.fn, n, seconds / 4.0, outL, outR); // warm-up, discarded
    std::array<double, kRuns> runs{};
    for (double &r : runs)
    {
        r = timeRun(w, k.fn, n, seconds, outL, outR);
    }
    std::sort(runs.begin(), runs.end());
    const double ns = runs[kRuns / 2];
    const double grains = static_cast<double>(w.grains.size());
    return {k.name, w.grains.size(), n, ns, ns * grains * kSampleRate / 1e9 * 100.0};
}

std::string trim(std::string s)
{
    const auto notSpace = [](char c) { return c != ' ' && c != '\t' && c != '\n' && c != '\r'; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

std::string cpuName()
{
    std::string name;
#if defined(__APPLE__)
    char buf[256] = {};
    std::size_t len = sizeof(buf) - 1;
    if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr, 0) == 0)
    {
        name = buf;
    }
#elif defined(__linux__)
    std::ifstream in("/proc/cpuinfo");
    for (std::string line; std::getline(in, line);)
    {
        if (line.rfind("model name", 0) == 0)
        {
            const auto colon = line.find(':');
            if (colon != std::string::npos)
            {
                name = line.substr(colon + 1);
            }
            break;
        }
    }
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(__x86_64__))
    int regs[4] = {};
    __cpuid(regs, static_cast<int>(0x80000000));
    if (static_cast<unsigned>(regs[0]) >= 0x80000004u)
    {
        char brand[49] = {};
        for (int leaf = 0; leaf < 3; ++leaf)
        {
            __cpuid(regs, static_cast<int>(0x80000002u + leaf));
            std::memcpy(brand + 16 * leaf, regs, 16);
        }
        name = brand;
    }
#elif defined(_WIN32)
    if (const char *id = std::getenv("PROCESSOR_IDENTIFIER"))
    {
        name = id;
    }
#endif
    name = trim(name);
    return name.empty() ? "unknown CPU" : name;
}

/// The CPU, plus the architecture this binary was built for (a universal macOS build has two).
std::string machine()
{
#if defined(__aarch64__) || defined(_M_ARM64)
    constexpr const char *arch = "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    constexpr const char *arch = "x86_64";
#else
    constexpr const char *arch = "unknown architecture";
#endif
    return cpuName() + " (" + arch + ")";
}

std::string compiler()
{
#if defined(GG_BENCH_COMPILER)
    return GG_BENCH_COMPILER;
#elif defined(__VERSION__)
    return __VERSION__;
#else
    return "unknown compiler";
#endif
}

std::string jsonString(std::string_view s)
{
    std::string out = "\"";
    for (const char c : s)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += c;
        }
        else if (static_cast<unsigned char>(c) < 0x20)
        {
            char esc[8];
            std::snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned>(c));
            out += esc;
        }
        else
        {
            out += c;
        }
    }
    return out + "\"";
}

bool writeJson(const std::string &path, const std::vector<Result> &results)
{
    std::FILE *f = std::fopen(path.c_str(), "w");
    if (!f)
    {
        return false;
    }
    std::fprintf(f, "{\n  \"machine\": %s,\n  \"compiler\": %s,\n  \"results\": [\n",
                 jsonString(machine()).c_str(), jsonString(compiler()).c_str());
    for (std::size_t i = 0; i < results.size(); ++i)
    {
        const Result &r = results[i];
        std::fprintf(f,
                     "    {\"kernel\": %s, \"grains\": %zu, \"blockSize\": %u, "
                     "\"nsPerStereoGrainSample\": %.4f, \"cpuPctOneCoreAt48k\": %.3f}%s\n",
                     jsonString(r.kernel).c_str(), r.grains, r.blockSize, r.nsPerStereoGrainSample,
                     r.cpuPctOneCoreAt48k, i + 1 < results.size() ? "," : "");
    }
    std::fprintf(f, "  ]\n}\n");
    return std::fclose(f) == 0;
}

void usage(std::FILE *f)
{
    std::fprintf(f, "usage: gg-bench [--quick] [--json out.json]\n"
                    "\n"
                    "Times the prototype grain kernels: 2,000 grains of 100 ms over a 60 s stereo\n"
                    "noise source at 48 kHz, at block sizes 32, 64 and 256. Each result is the\n"
                    "median of 5 runs of at least 2 s each.\n"
                    "\n"
                    "  --quick           runs of 0.2 s instead of 2 s\n"
                    "  --json out.json   also write the results to out.json\n");
}
} // namespace

int main(int argc, char **argv)
{
    bool quick = false;
    std::string jsonPath;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--quick")
        {
            quick = true;
        }
        else if (arg == "--json" && i + 1 < argc)
        {
            jsonPath = argv[++i];
        }
        else if (arg == "--help" || arg == "-h")
        {
            usage(stdout);
            return 0;
        }
        else
        {
            usage(stderr);
            return 2;
        }
    }
    const double seconds = quick ? 0.2 : 2.0;

#if defined(BUILD_IS_DEBUG)
    std::fprintf(stderr, "warning: this is a Debug build; its timings mean little\n");
#endif
#if defined(__APPLE__)
    // Ask for a performance core: the §16 target is a share of one.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif

    std::printf("gg-bench: %zu grains of 100 ms, 60 s stereo noise at 48 kHz, "
                "median of %d runs of >= %.1f s\n",
                kGrains, kRuns, seconds);
    std::printf("machine:  %s\ncompiler: %s\n\n", machine().c_str(), compiler().c_str());
    std::printf("%-16s %6s %18s %16s\n", "kernel", "block", "ns/grain-sample", "% core @48k");

    Workload workload;
    std::vector<Result> results;
    {
        const gg::ScopedDenormalsOff noDenormals;
        for (const KernelInfo &k : kKernels)
        {
            for (const uint32_t n : kBlockSizes)
            {
                const Result r = measure(workload, k, n, seconds);
                std::printf("%-16s %6u %18.4f %16.3f\n", r.kernel, r.blockSize,
                            r.nsPerStereoGrainSample, r.cpuPctOneCoreAt48k);
                std::fflush(stdout);
                results.push_back(r);
            }
        }
    }

    if (!jsonPath.empty())
    {
        if (!writeJson(jsonPath, results))
        {
            std::fprintf(stderr, "error: couldn't write %s\n", jsonPath.c_str());
            return 1;
        }
        std::printf("\nwrote %s\n", jsonPath.c_str());
    }
    return 0;
}
