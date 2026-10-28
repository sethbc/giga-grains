// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The kernel cases time the prototype grain kernels in Kernels.h. They are the M0 kernel
// benchmark spike, whose numbers set the CPU target in INTENT §16.
//
// The workload is a 60 s stereo noise source at 48 kHz, read by Options::grains grains (2,000 by
// default) of 100 ms. Each grain has a random position and a random rate in [0.5, 2]. A timed
// block includes what an engine does around the kernel: clearing the output and the per-block
// grain update, which advances every grain and respawns those whose window has ended. It also
// sums the output into a checksum, so the compiler can't drop the work; that costs about one add
// per output sample, against 2,000 grain-samples.
//
// Every configuration (kernel x block size) shares the one read-only source, and starts from the
// same grains; each then keeps its own grains from round to round. Its warm-up renders for a
// quarter of a run.

#include "Bench.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <fstream>
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(__x86_64__))
#include <intrin.h>
#endif

#include <nlohmann/json.hpp>

#include "Kernels.h"
#include "gg/core/DenormalGuard.h"
#include "gg/core/Rng.h"

namespace gg::bench
{
namespace
{
constexpr int kSchemaVersion = 2;
constexpr double kSampleRate = 48000.0;
constexpr int64_t kSourceFrames = 60 * 48000;
constexpr double kGrainSamples = 0.1 * kSampleRate;
constexpr double kMinRate = 0.5;
constexpr double kMaxRate = 2.0;
constexpr uint32_t kMaxBlockSize = 256;
constexpr std::array<uint32_t, 3> kBlockSizes{32, 64, kMaxBlockSize};
constexpr uint64_t kSeed = 0x67672d62656e6368ull; // "gg-bench"

struct KernelInfo
{
    const char *name;
    Kernel fn;
};

constexpr std::array<KernelInfo, 5> kKernels{{
    {"linear-scalar", renderLinearScalar},
    {"hermite-scalar", renderHermiteScalar},
    {"sinc-scalar", renderSincScalar},
    {"linear-simd", renderLinearSimd},
    {"hermite-simd", renderHermiteSimd},
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

/// The 60 s stereo noise source every kernel configuration reads.
struct KernelSource
{
    std::vector<float> srcL, srcR;

    KernelSource()
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
};

volatile double gSink = 0.0; // the rendered output feeds this, so it can't be optimised away

/// One kernel at one block size, with grains that keep reading the source: whenever a grain's
/// window ends, the grain respawns at a new random position and rate.
class KernelConfiguration final : public Configuration
{
  public:
    KernelConfiguration(const KernelInfo &kernel, uint32_t blockSize, const KernelSource &source,
                        uint32_t grains, double warmUpSeconds)
        : Configuration(kernel.name, "kernel", blockSize), fn_(kernel.fn), source_(source),
          warmUpSeconds_(warmUpSeconds), outL_(blockSize), outR_(blockSize)
    {
        // The same starting grains for every configuration, with their windows at staggered
        // random phases.
        grains_.resize(grains);
        for (std::size_t g = 0; g < grains; ++g)
        {
            spawn(g, static_cast<float>(draw(kStartPhase, g)));
        }
    }

    void warmUp() override { timeRun(warmUpSeconds_); }

    double timeRun(double seconds) override
    {
        using Clock = std::chrono::steady_clock;
        const uint32_t n = blockSize();
        const auto start = Clock::now();
        const auto deadline = start + std::chrono::duration_cast<Clock::duration>(
                                          std::chrono::duration<double>(seconds));
        double sink = 0.0;
        uint64_t blocks = 0;
        auto now = start;
        do
        {
            std::fill_n(outL_.begin(), n, 0.0f);
            std::fill_n(outR_.begin(), n, 0.0f);
            fn_(grains_, source_.srcL.data(), source_.srcR.data(), kSourceFrames, outL_.data(),
                outR_.data(), n);
            advance(n);
            for (uint32_t i = 0; i < n; ++i)
            {
                sink += outL_[i] + outR_[i];
            }
            ++blocks;
            now = Clock::now();
        } while (now < deadline);
        gSink = gSink + sink;

        const double ns = std::chrono::duration<double, std::nano>(now - start).count();
        const double grainSamples =
            static_cast<double>(blocks) * n * static_cast<double>(grains_.size());
        return ns / grainSamples;
    }

    double grains() const override { return static_cast<double>(grains_.size()); }

  private:
    void spawn(std::size_t g, float envPhase)
    {
        // Keep every read inside the source, so the kernels run their usual in-bounds path:
        // a grain lives one window plus at most one block past its end, at up to kMaxRate.
        constexpr double margin = 16.0;
        constexpr double reach = kMaxRate * (kGrainSamples + kMaxBlockSize);
        const double span = static_cast<double>(kSourceFrames) - 2.0 * margin - reach;
        const uint64_t s = spawns_++;
        grains_.pos[g] = margin + span * draw(kPosition, s);
        grains_.rate[g] = kMinRate + (kMaxRate - kMinRate) * draw(kRate, s);
        grains_.envPhase[g] = envPhase;
        grains_.envInc[g] = static_cast<float>(1.0 / kGrainSamples);
        grains_.gain[g] = static_cast<float>(0.5 + 0.5 * draw(kGain, s));
    }

    /// Moves every grain on by n samples, respawning those whose window has ended.
    void advance(uint32_t n)
    {
        for (std::size_t g = 0; g < grains_.size(); ++g)
        {
            grains_.pos[g] += static_cast<double>(n) * grains_.rate[g];
            const float phase = grains_.envPhase[g] + static_cast<float>(n) * grains_.envInc[g];
            if (phase >= 1.0f)
            {
                spawn(g, phase - 1.0f);
            }
            else
            {
                grains_.envPhase[g] = phase;
            }
        }
    }

    Kernel fn_;
    const KernelSource &source_;
    double warmUpSeconds_;
    GrainBatch grains_;
    uint64_t spawns_{0}; // grains spawned so far; indexes each spawn's draws
    std::vector<float> outL_, outR_;
};

bool selected(const Options &options, const std::string &name)
{
    return options.cases.empty() ||
           std::find(options.cases.begin(), options.cases.end(), name) != options.cases.end();
}

CaseResult summarise(const Configuration &c, std::vector<double> runs)
{
    CaseResult r;
    r.name = c.name();
    r.kind = c.kind();
    r.blockSize = c.blockSize();
    r.grains = c.grains();
    r.median = quantile(runs, 0.5);
    r.min = *std::min_element(runs.begin(), runs.end());
    r.iqr = quantile(runs, 0.75) - quantile(runs, 0.25);
    r.cpuPctOneCoreAt48k = r.median * r.grains * kSampleRate / 1e9 * 100.0;
    r.nsPerGrainSample = std::move(runs);
    return r;
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
} // namespace

std::vector<std::string> caseNames()
{
    std::vector<std::string> names;
    for (const KernelInfo &k : kKernels)
    {
        names.emplace_back(k.name);
    }
    return names;
}

void validate(const Options &options)
{
    if (!(options.runSeconds > 0.0) || !std::isfinite(options.runSeconds))
    {
        throw std::invalid_argument("the run length must be a positive number of seconds");
    }
    if (options.rounds < 1)
    {
        throw std::invalid_argument("there must be at least 1 round");
    }
    if (options.grains < 1)
    {
        throw std::invalid_argument("there must be at least 1 grain");
    }
    const std::vector<std::string> names = caseNames();
    for (const std::string &c : options.cases)
    {
        if (std::find(names.begin(), names.end(), c) == names.end())
        {
            throw std::invalid_argument("unknown case: " + c);
        }
    }
}

Report run(const Options &options, const Progress &progress)
{
    validate(options);
    Report report{options, machine(), compiler(), {}};

    const gg::ScopedDenormalsOff noDenormals;

    // 1. Build every configuration, in report order.
    std::optional<KernelSource> kernelSource;
    std::vector<std::unique_ptr<Configuration>> configurations;
    for (const KernelInfo &k : kKernels)
    {
        if (!selected(options, k.name))
        {
            continue;
        }
        if (!kernelSource)
        {
            kernelSource.emplace();
        }
        for (const uint32_t n : kBlockSizes)
        {
            configurations.push_back(std::make_unique<KernelConfiguration>(
                k, n, *kernelSource, options.grains, options.runSeconds / 4.0));
        }
    }

    // 2. Warm each up once.
    for (const auto &c : configurations)
    {
        c->warmUp();
    }

    // 3. The rounds: every configuration once per round, in roundOrder()'s varying order.
    const auto count = static_cast<uint32_t>(configurations.size());
    std::vector<std::vector<double>> runs(count);
    for (int r = 0; r < options.rounds; ++r)
    {
        for (const uint32_t i : roundOrder(count, r))
        {
            runs[i].push_back(configurations[i]->timeRun(options.runSeconds));
        }
        if (progress)
        {
            progress(r + 1, options.rounds);
        }
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        report.results.push_back(summarise(*configurations[i], std::move(runs[i])));
    }
    return report;
}

std::string toJson(const Report &report)
{
    nlohmann::ordered_json results = nlohmann::ordered_json::array();
    for (const CaseResult &r : report.results)
    {
        results.push_back({
            {"name", r.name},
            {"kind", r.kind},
            {"blockSize", r.blockSize},
            {"grains", r.grains},
            {"runs", r.nsPerGrainSample},
            {"median", r.median},
            {"min", r.min},
            {"iqr", r.iqr},
            {"cpuPctOneCoreAt48k", r.cpuPctOneCoreAt48k},
        });
    }
    const nlohmann::ordered_json j{
        {"schemaVersion", kSchemaVersion},
        {"machine", report.machine},
        {"compiler", report.compiler},
        {"commit", report.options.commit},
        {"runSeconds", report.options.runSeconds},
        {"rounds", report.options.rounds},
        {"results", std::move(results)},
        {"ratios", nlohmann::ordered_json::array()},
    };
    return j.dump(2) + "\n";
}

std::vector<uint32_t> roundOrder(uint32_t configurations, int round)
{
    std::vector<uint32_t> order(configurations);
    if (configurations == 0)
    {
        return order;
    }
    const uint32_t n = configurations;
    const uint32_t shift = static_cast<uint32_t>(round) % n;
    const bool reversed = round % 2 != 0;
    for (uint32_t i = 0; i < n; ++i)
    {
        order[i] = reversed ? (shift + n - 1 - i) % n : (i + shift) % n;
    }
    return order;
}

double quantile(std::vector<double> v, double q)
{
    if (v.empty())
    {
        return std::nan("");
    }
    std::sort(v.begin(), v.end());
    const double h = std::clamp(q, 0.0, 1.0) * static_cast<double>(v.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(h));
    const std::size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (h - static_cast<double>(lo)) * (v[hi] - v[lo]);
}

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
} // namespace gg::bench
