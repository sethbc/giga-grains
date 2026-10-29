// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The kernel cases time the prototype grain kernels in Kernels.h. They are the M0 kernel
// benchmark spike, whose numbers set the CPU target in INTENT §16. The reference case is one of
// them, renderHermiteSimd, under its own name.
//
// The workload is a 60 s stereo noise source at 48 kHz, read by Options::grains grains (2,000 by
// default) of 100 ms. Each grain has a random position and a random rate in [0.5, 2]. A timed
// block includes what an engine does around the kernel: clearing the output and the per-block
// grain update, which advances every grain and respawns those whose window has ended. It also
// sums the output into a checksum, so the compiler can't drop the work; that costs about one add
// per output sample, against 2,000 grain-samples.
//
// Every kernel configuration (kernel x block size) shares the one read-only source, and starts
// from the same grains; each then keeps its own grains from round to round. Its warm-up renders
// for a quarter of a run.
//
// The engine cases (spec §5): engine-sample (X), engine-sample-eco (X at the Eco tier),
// engine-live and engine-full-pool. Each owns a gg::Engine (48 kHz, maxBlockSize 256, seeded with
// the bench seed) rendering one Cloud stream of Options::grains overlapping grains of 100 ms
// (density grains / 0.1 Hz, periodic) at +7 semitones, stereo 1, over the same noise: in Sample
// mode the noise is the source; in Live mode it is the input, fed block by block. The timed block
// is one Engine::process call. A configuration's grain count is the mean of activeGrains() after
// each timed block, and a run's ns per grain-sample is its time over the frames it rendered times
// that run's mean. Each warms up for 2 grain durations of audio, engine-live for 16 s, so its
// ring is full.
//
// --space (runSpace) times Space's per-sample stage, the diffuser then the grid-aligned reverb,
// over 1 s of the same stereo noise, looped, in blocks of 256 frames. It checks the clock once a
// block, never inside one, and sums the return into a checksum, as the kernel cases do.

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
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(__x86_64__))
#include <intrin.h>
#endif

#include <nlohmann/json.hpp>

#include "Kernels.h"
#include "gg/core/DenormalGuard.h"
#include "gg/core/Diffuser.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Rng.h"
#include "gg/core/SpaceReverb.h"
#include "gg/engine/Engine.h"
#include "gg/grain/Tier.h"
#include "gg/source/SampleBuffer.h"
#include "gg/source/SourceMode.h"

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
constexpr double kMaxRunSeconds = 3600.0; // beyond it, duration_cast to the clock could overflow
constexpr const char *kReference = "reference";
// The live case's input wraps around the noise without splitting a block.
static_assert(kSourceFrames % kMaxBlockSize == 0 && kMaxBlockSize % 32 == 0 &&
              kMaxBlockSize % 64 == 0);

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

/// The CPU gate's yardstick: M0's hermite-simd, frozen (see Kernels.h).
constexpr KernelInfo kReferenceKernel{kReference, renderHermiteSimd};

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

/// Frame `index` of the bench noise's channel `stream` (kNoiseL or kNoiseR), in [-0.5, 0.5].
float noiseSample(Stream stream, uint64_t index)
{
    return static_cast<float>(0.5 * gg::rng::bipolar(gg::rng::hash(
                                        {kSeed, 0, stream, gg::rng::Field::SignalNoise, index})));
}

/// The 60 s stereo noise every configuration reads: the kernels' source, engine-sample's
/// SampleBuffer and engine-live's input.
struct Noise
{
    std::shared_ptr<gg::SampleBuffer> buffer;

    Noise() : buffer(std::make_shared<gg::SampleBuffer>(2, kSourceFrames, kSampleRate))
    {
        float *l = buffer->channel(0);
        float *r = buffer->channel(1);
        for (int64_t i = 0; i < kSourceFrames; ++i)
        {
            l[i] = noiseSample(kNoiseL, static_cast<uint64_t>(i));
            r[i] = noiseSample(kNoiseR, static_cast<uint64_t>(i));
        }
    }

    const float *left() const { return buffer->channel(0); }
    const float *right() const { return buffer->channel(1); }
};

volatile double gSink = 0.0; // the rendered output feeds this, so it can't be optimised away

/// One kernel at one block size, with grains that keep reading the source: whenever a grain's
/// window ends, the grain respawns at a new random position and rate.
class KernelConfiguration final : public Configuration
{
  public:
    KernelConfiguration(const KernelInfo &kernel, uint32_t blockSize, const Noise &source,
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
            fn_(grains_, source_.left(), source_.right(), kSourceFrames, outL_.data(), outR_.data(),
                n);
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
    const Noise &source_;
    double warmUpSeconds_;
    GrainBatch grains_;
    uint64_t spawns_{0}; // grains spawned so far; indexes each spawn's draws
    std::vector<float> outL_, outR_;
};

/// What an engine case sets up: the engine, its Cloud stream, the source it reads and its
/// warm-up in seconds of audio.
struct EngineSetup
{
    gg::EngineConfig config;
    gg::CloudParams params;
    gg::SourceMode mode{gg::SourceMode::Sample};
    double warmUpSeconds{0.0};
};

/// engine-sample, X (ruling 4): the headline's Sample-mode case. Spray 0.45 keeps every read
/// inside [0.05, 0.95] of the source, so no grain piles up at a clamped end.
EngineSetup engineSample(uint32_t grains)
{
    EngineSetup s;
    s.config = {.sampleRate = kSampleRate,
                .maxBlockSize = kMaxBlockSize,
                .grainCapacity = 4096,
                .seed = kSeed};
    s.params = {.densityHz = static_cast<double>(grains) / 0.1,
                .distribution = 0.0,
                .grainDurationSec = 0.1,
                .position = 0.5,
                .spray = 0.45,
                .pitchSemitones = 7.0,
                .stereo = 1.0,
                .gain = 0.5f};
    s.warmUpSeconds = 2.0 * s.params.grainDurationSec;
    return s;
}

struct EngineCase
{
    const char *name;
    EngineSetup (*setup)(uint32_t grains);
};

/// The engine cases, in report order. Each is engine-sample with its own changes.
constexpr std::array<EngineCase, 4> kEngineCases{{
    {"engine-sample", engineSample},
    // X at the Eco tier: every grain reads with linear interpolation (ruling 2).
    {"engine-sample-eco",
     [](uint32_t grains) {
         EngineSetup s = engineSample(grains);
         s.params.quality = gg::Tier::Eco;
         return s;
     }},
    // The FX's figure: the same stream over a 16 s live ring, reading 4 to 12 s behind the head.
    {"engine-live",
     [](uint32_t grains) {
         EngineSetup s = engineSample(grains);
         s.config.liveSeconds = 16.0;
         s.mode = gg::SourceMode::Live;
         s.params.position = 4.0;
         s.params.spray = 8.0;
         s.warmUpSeconds = 16.0; // fills the ring
         return s;
     }},
    // Admission under saturation: grains of 1 s at the same density ask for 10x the pool.
    {"engine-full-pool",
     [](uint32_t grains) {
         EngineSetup s = engineSample(grains);
         s.config.grainCapacity = grains + 48;
         s.params.grainDurationSec = 1.0;
         s.warmUpSeconds = 2.0 * s.params.grainDurationSec;
         return s;
     }},
}};

/// One engine case at one block size: an Engine that keeps rendering from round to round.
class EngineConfiguration final : public Configuration
{
  public:
    EngineConfiguration(const EngineCase &engineCase, uint32_t blockSize, const Noise &noise,
                        uint32_t grains)
        : Configuration(engineCase.name, "engine", blockSize), setup_(engineCase.setup(grains)),
          engine_(setup_.config), noise_(noise), outL_(blockSize), outR_(blockSize)
    {
        if (setup_.mode == gg::SourceMode::Sample)
        {
            engine_.setSource(noise.buffer);
        }
        engine_.setCloudParams(setup_.params);
        engine_.setSourceMode(setup_.mode);
    }

    void warmUp() override
    {
        const auto frames = static_cast<int64_t>(std::ceil(setup_.warmUpSeconds * kSampleRate));
        for (int64_t done = 0; done < frames; done += blockSize())
        {
            processBlock();
        }
    }

    double timeRun(double seconds) override
    {
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        const auto deadline = start + std::chrono::duration_cast<Clock::duration>(
                                          std::chrono::duration<double>(seconds));
        uint64_t blocks = 0;
        uint64_t active = 0; // activeGrains() summed over the blocks
        auto now = start;
        do
        {
            processBlock();
            active += engine_.activeGrains();
            ++blocks;
            now = Clock::now();
        } while (now < deadline);
        gSink = gSink + outL_[0] + outR_[blockSize() - 1];
        timedBlocks_ += blocks;
        timedActive_ += active;

        const double ns = std::chrono::duration<double, std::nano>(now - start).count();
        // Frames x the run's mean grains: the grain-samples rendered.
        const double grainSamples =
            static_cast<double>(std::max<uint64_t>(active, 1)) * static_cast<double>(blockSize());
        return ns / grainSamples;
    }

    double grains() const override
    {
        return timedBlocks_ == 0
                   ? 0.0
                   : static_cast<double>(timedActive_) / static_cast<double>(timedBlocks_);
    }

  private:
    /// One Engine::process call of one block; in Live mode, with the next block of the noise as
    /// its input.
    void processBlock()
    {
        const uint32_t n = blockSize();
        float *const out[2] = {outL_.data(), outR_.data()};
        if (setup_.mode == gg::SourceMode::Live)
        {
            const float *const in[2] = {noise_.left() + inputAt_, noise_.right() + inputAt_};
            engine_.process(in, 2, out, 2, n);
            inputAt_ = (inputAt_ + n) % kSourceFrames;
        }
        else
        {
            engine_.process(out, 2, n);
        }
    }

    EngineSetup setup_;
    gg::Engine engine_;
    const Noise &noise_;
    int64_t inputAt_{0}; // the live input's next frame in the noise
    uint64_t timedBlocks_{0};
    uint64_t timedActive_{0};
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
    // ns per frame x 48,000 frames / 1e9 ns, as a percentage. For an engine case the grains are
    // the mean over every timed block, so this is the median run's ns per frame to within the
    // runs' small differences in mean grains.
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
    names.emplace_back(kReferenceKernel.name);
    for (const EngineCase &e : kEngineCases)
    {
        names.emplace_back(e.name);
    }
    return names;
}

void validate(const Options &options)
{
    if (!(options.runSeconds > 0.0 && options.runSeconds <= kMaxRunSeconds))
    {
        throw std::invalid_argument("the run length must be more than 0 and at most 3600 seconds");
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
    std::optional<Noise> noise; // built once, only if a case is selected
    const auto source = [&noise]() -> const Noise & {
        if (!noise)
        {
            noise.emplace();
        }
        return *noise;
    };
    std::vector<std::unique_ptr<Configuration>> configurations;
    std::vector<KernelInfo> kernels(kKernels.begin(), kKernels.end());
    kernels.push_back(kReferenceKernel);
    for (const KernelInfo &k : kernels)
    {
        if (!selected(options, k.name))
        {
            continue;
        }
        for (const uint32_t n : kBlockSizes)
        {
            configurations.push_back(std::make_unique<KernelConfiguration>(
                k, n, source(), options.grains, options.runSeconds / 4.0));
        }
    }
    for (const EngineCase &e : kEngineCases)
    {
        if (!selected(options, e.name))
        {
            continue;
        }
        for (const uint32_t n : kBlockSizes)
        {
            configurations.push_back(
                std::make_unique<EngineConfiguration>(e, n, source(), options.grains));
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
    report.ratios = computeRatios(report);
    return report;
}

std::vector<Ratio> computeRatios(const Report &report)
{
    std::vector<Ratio> ratios;
    for (const CaseResult &c : report.results)
    {
        if (c.kind != "engine")
        {
            continue;
        }
        const auto reference =
            std::find_if(report.results.begin(), report.results.end(), [&c](const CaseResult &r) {
                return r.name == kReference && r.blockSize == c.blockSize;
            });
        if (reference == report.results.end())
        {
            continue;
        }
        if (reference->nsPerGrainSample.size() != c.nsPerGrainSample.size() ||
            c.nsPerGrainSample.empty())
        {
            throw std::invalid_argument(c.name + " and " + kReference +
                                        " have different numbers of runs");
        }
        Ratio ratio{.name = c.name, .blockSize = c.blockSize};
        for (std::size_t r = 0; r < c.nsPerGrainSample.size(); ++r)
        {
            ratio.runs.push_back(c.nsPerGrainSample[r] / reference->nsPerGrainSample[r]);
        }
        ratio.median = quantile(ratio.runs, 0.5);
        ratio.iqr = quantile(ratio.runs, 0.75) - quantile(ratio.runs, 0.25);
        ratios.push_back(std::move(ratio));
    }
    return ratios;
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
    nlohmann::ordered_json ratios = nlohmann::ordered_json::array();
    for (const Ratio &r : report.ratios)
    {
        ratios.push_back({
            {"name", r.name},
            {"blockSize", r.blockSize},
            {"runs", r.runs},
            {"median", r.median},
            {"iqr", r.iqr},
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
        {"ratios", std::move(ratios)},
    };
    return j.dump(2, ' ', false, nlohmann::ordered_json::error_handler_t::replace) + "\n";
}

namespace
{
constexpr int kBaselineSchemaVersion = 2;
constexpr double kMinTolerance = 0.10;

[[noreturn]] void badBaseline(const std::string &why)
{
    throw std::invalid_argument("bad baseline: " + why);
}

bool isString(const nlohmann::json &j, const char *key)
{
    return j.contains(key) && j[key].is_string();
}

/// Checks one machine's ratios: see compare().
void checkRatios(const nlohmann::json &ratios)
{
    for (const nlohmann::json &e : ratios)
    {
        if (!e.is_object() || !isString(e, "case") || !e.contains("blockSize") ||
            !e["blockSize"].is_number_unsigned() || !e.contains("ratio") ||
            !e["ratio"].is_number() || !e.contains("tolerance") || !e["tolerance"].is_number())
        {
            badBaseline("every ratio needs a case, a blockSize, a ratio and a tolerance");
        }
        const auto ratio = e["ratio"].get<double>();
        const auto tolerance = e["tolerance"].get<double>();
        if (!(ratio > 0.0) || !std::isfinite(ratio) || !(tolerance >= 0.0) ||
            !std::isfinite(tolerance))
        {
            badBaseline("every ratio must be positive and every tolerance >= 0, both finite");
        }
    }
}

/// Checks a baseline's shape: see compare().
void checkBaseline(const nlohmann::json &b)
{
    if (!b.is_object())
    {
        badBaseline("not a JSON object");
    }
    if (!b.contains("schemaVersion") || !b["schemaVersion"].is_number_integer() ||
        b["schemaVersion"].get<int64_t>() != kBaselineSchemaVersion)
    {
        badBaseline("schemaVersion must be 2");
    }
    if (!isString(b, "leg"))
    {
        badBaseline("leg must be a string");
    }
    if (!b.contains("machines") || !b["machines"].is_array())
    {
        badBaseline("machines must be an array");
    }
    std::vector<std::string> seen;
    for (const nlohmann::json &m : b["machines"])
    {
        if (!m.is_object() || !isString(m, "machine") || !isString(m, "commit") ||
            !m.contains("runs") || !m["runs"].is_number_integer() || m["runs"].get<int64_t>() < 1 ||
            !m.contains("ratios") || !m["ratios"].is_array())
        {
            badBaseline("every machine needs a machine, a commit, runs >= 1 and ratios");
        }
        const auto name = m["machine"].get<std::string>();
        if (std::find(seen.begin(), seen.end(), name) != seen.end())
        {
            badBaseline("two entries for " + name);
        }
        seen.push_back(name);
        checkRatios(m["ratios"]);
    }
}

/// Checks that j is a gg-bench report with ratios; `which` names it in the error.
void checkRun(const nlohmann::json &j, const std::string &which)
{
    if (!j.is_object() || !j.contains("schemaVersion") || !j["schemaVersion"].is_number_integer() ||
        j["schemaVersion"].get<int64_t>() != kSchemaVersion || !j.contains("ratios") ||
        !j["ratios"].is_array() || !isString(j, "commit") || !isString(j, "machine"))
    {
        throw std::invalid_argument(which + " isn't a gg-bench report (schema 2)");
    }
}

/// One machine's baseline entry from its runs (indexes into `runs`).
nlohmann::json machineEntry(const std::string &machine, const std::vector<nlohmann::json> &runs,
                            const std::vector<std::size_t> &mine)
{
    // Each run's (case, block size) -> its median ratio, in the machine's first run's order.
    using Key = std::pair<std::string, uint32_t>;
    std::vector<Key> keys;
    std::vector<std::vector<double>> medians; // [key][run]
    std::vector<std::string> commits;
    for (std::size_t n = 0; n < mine.size(); ++n)
    {
        const nlohmann::json &j = runs[mine[n]];
        const std::string which = "run " + std::to_string(mine[n] + 1);
        const auto commit = j["commit"].get<std::string>();
        if (std::find(commits.begin(), commits.end(), commit) == commits.end())
        {
            commits.push_back(commit);
        }
        const nlohmann::json &ratios = j["ratios"];
        if (n > 0 && ratios.size() != keys.size())
        {
            throw std::invalid_argument(which + " has different ratios from run " +
                                        std::to_string(mine[0] + 1) + " on " + machine);
        }
        for (const nlohmann::json &r : ratios)
        {
            if (!r.is_object() || !isString(r, "name") || !r.contains("blockSize") ||
                !r["blockSize"].is_number_unsigned() || !r.contains("median") ||
                !r["median"].is_number())
            {
                throw std::invalid_argument(which + " has a ratio without a name, a blockSize "
                                                    "and a median");
            }
            const Key key{r["name"].get<std::string>(), r["blockSize"].get<uint32_t>()};
            if (n == 0)
            {
                keys.push_back(key);
                medians.emplace_back();
            }
            const auto at = std::find(keys.begin(), keys.end(), key);
            if (at == keys.end())
            {
                throw std::invalid_argument(which + " has a ratio run " +
                                            std::to_string(mine[0] + 1) + " doesn't: " + key.first +
                                            " at block " + std::to_string(key.second));
            }
            medians[static_cast<std::size_t>(at - keys.begin())].push_back(
                r["median"].get<double>());
        }
    }

    nlohmann::json entries = nlohmann::json::array();
    for (std::size_t k = 0; k < keys.size(); ++k)
    {
        const std::vector<double> &m = medians[k];
        if (m.size() != mine.size())
        {
            throw std::invalid_argument("the runs on " + machine + " have different ratios for " +
                                        keys[k].first + " at block " +
                                        std::to_string(keys[k].second));
        }
        const double ratio = quantile(m, 0.5);
        const auto [lo, hi] = std::minmax_element(m.begin(), m.end());
        const double tolerance = std::max(kMinTolerance, 2.0 * (*hi - *lo) / ratio);
        entries.push_back({{"case", keys[k].first},
                           {"blockSize", keys[k].second},
                           {"ratio", ratio},
                           {"tolerance", tolerance}});
    }
    std::string commit;
    for (const std::string &c : commits)
    {
        commit += (commit.empty() ? "" : ",") + c;
    }
    return {{"machine", machine},
            {"commit", commit},
            {"runs", mine.size()},
            {"ratios", std::move(entries)}};
}
} // namespace

Comparison compare(const Report &report, const nlohmann::json &baseline)
{
    checkBaseline(baseline);
    Comparison c;
    const nlohmann::json *entry = nullptr;
    for (const nlohmann::json &m : baseline["machines"])
    {
        if (m["machine"].get<std::string>() == report.machine)
        {
            entry = &m;
            break;
        }
    }
    if (entry == nullptr)
    {
        return c;
    }
    c.found = true;
    c.commit = (*entry)["commit"].get<std::string>();
    c.runs = (*entry)["runs"].get<uint32_t>();
    for (const Ratio &r : report.ratios)
    {
        Verdict v{.name = r.name,
                  .blockSize = r.blockSize,
                  .ratio = r.median,
                  .limit = std::nan(""),
                  .regressed = false};
        for (const nlohmann::json &e : (*entry)["ratios"])
        {
            if (e["case"].get<std::string>() == r.name &&
                e["blockSize"].get<uint64_t>() == r.blockSize)
            {
                v.limit = e["ratio"].get<double>() * (1.0 + e["tolerance"].get<double>());
                v.regressed = v.ratio > v.limit;
                break;
            }
        }
        c.verdicts.push_back(std::move(v));
    }
    return c;
}

nlohmann::json makeBaseline(const std::string &leg, const std::vector<nlohmann::json> &runs)
{
    if (runs.empty())
    {
        throw std::invalid_argument("a baseline needs at least one run");
    }
    // Group the runs by machine, in the order the machines first appear.
    std::vector<std::string> machines;
    std::vector<std::vector<std::size_t>> groups;
    for (std::size_t run = 0; run < runs.size(); ++run)
    {
        checkRun(runs[run], "run " + std::to_string(run + 1));
        const auto machine = runs[run]["machine"].get<std::string>();
        const auto at = std::find(machines.begin(), machines.end(), machine);
        if (at == machines.end())
        {
            machines.push_back(machine);
            groups.push_back({run});
        }
        else
        {
            groups[static_cast<std::size_t>(at - machines.begin())].push_back(run);
        }
    }

    nlohmann::json entries = nlohmann::json::array();
    for (std::size_t m = 0; m < machines.size(); ++m)
    {
        entries.push_back(machineEntry(machines[m], runs, groups[m]));
    }
    return {
        {"schemaVersion", kBaselineSchemaVersion}, {"leg", leg}, {"machines", std::move(entries)}};
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

namespace
{
constexpr int64_t kSpaceNoiseFrames = 48000; // 1 s, looped frame by frame
constexpr uint32_t kSpaceBlock = 256;        // frames between two reads of the clock
constexpr double kSpaceWarmUpSeconds = 2.0;  // of audio: one Decay of 1.0 (2 s)

/// Space's bench settings: Diffusion 1, Amount 1, Size 0.5 (roomSize 0), Decay 1.0, Damping 0.3,
/// Modulation 0.25, no pre-delay. The FX's Dry to Reverb doesn't reach these two stages.
constexpr gg::core::SpaceParams kSpaceParams{.diffusion = 1.0f,
                                             .amount = 1.0f,
                                             .drySend = 0.0f,
                                             .decayLog2 = 1.0f,
                                             .infinite = false,
                                             .roomSize = 0.0f,
                                             .hfDamping = 0.3f,
                                             .modulation = 0.25f,
                                             .predelaySeconds = 0.0f};

/// The diffuser and the reverb, chained as Space runs them on a layer's grain sum, running
/// on and on over looped noise.
class SpaceStage
{
  public:
    SpaceStage()
        : reverb_(std::make_unique<gg::core::SpaceReverb>(kSampleRate)), left_(kSpaceNoiseFrames),
          right_(kSpaceNoiseFrames)
    {
        diffuser_.prepare(kSampleRate);
        for (int64_t i = 0; i < kSpaceNoiseFrames; ++i)
        {
            left_[i] = noiseSample(kNoiseL, static_cast<uint64_t>(i));
            right_[i] = noiseSample(kNoiseR, static_cast<uint64_t>(i));
        }
    }

    void warmUp()
    {
        const auto frames = static_cast<int64_t>(kSpaceWarmUpSeconds * kSampleRate);
        double sink = 0.0;
        for (int64_t done = 0; done < frames; done += kSpaceBlock)
        {
            sink += processBlock();
        }
        gSink = gSink + sink;
    }

    /// Renders blocks for at least `seconds`; returns ns per stereo sample.
    double timeRun(double seconds)
    {
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        const auto deadline = start + std::chrono::duration_cast<Clock::duration>(
                                          std::chrono::duration<double>(seconds));
        double sink = 0.0;
        uint64_t blocks = 0;
        auto now = start;
        do
        {
            sink += processBlock();
            ++blocks;
            now = Clock::now();
        } while (now < deadline);
        gSink = gSink + sink;

        const double ns = std::chrono::duration<double, std::nano>(now - start).count();
        return ns / (static_cast<double>(blocks) * kSpaceBlock);
    }

  private:
    /// One block of kSpaceBlock frames through the diffuser and the reverb; returns the sum of
    /// the return.
    double processBlock()
    {
        float sum = 0.0f;
        for (uint32_t k = 0; k < kSpaceBlock; ++k)
        {
            float l = left_[at_];
            float r = right_[at_];
            diffuser_.process(l, r, kSpaceParams.diffusion);
            float outL = 0.0f, outR = 0.0f;
            reverb_->processSample(t_, l, r, kSpaceParams, true, outL, outR);
            sum += outL + outR;
            ++t_;
            at_ = at_ + 1 == kSpaceNoiseFrames ? 0 : at_ + 1;
        }
        return static_cast<double>(sum);
    }

    gg::core::Diffuser diffuser_;
    std::unique_ptr<gg::core::SpaceReverb> reverb_; // ~14.5 MB: on the heap
    std::vector<float> left_, right_;
    int64_t at_{0}; // the next frame of the noise
    int64_t t_{0};  // the reverb's absolute time
};

/// printf's formatting of a short line (numbers and fixed labels only).
template <typename... Args> std::string formatLine(const char *format, Args... args)
{
    char line[256];
    const int n = std::snprintf(line, sizeof(line), format, args...);
    return n > 0 ? std::string(line,
                               std::min<std::size_t>(static_cast<std::size_t>(n), sizeof(line) - 1))
                 : std::string();
}

bool writeText(const std::string &path, const std::string &text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
    out.close();
    return static_cast<bool>(out);
}
} // namespace

void validate(const SpaceOptions &options)
{
    if (!(options.runSeconds > 0.0 && options.runSeconds <= kMaxRunSeconds))
    {
        throw std::invalid_argument("the run length must be more than 0 and at most 3600 seconds");
    }
    if (options.rounds < 1)
    {
        throw std::invalid_argument("there must be at least 1 round");
    }
}

SpaceReport runSpace(const SpaceOptions &options, const Progress &progress)
{
    validate(options);
    SpaceReport report{.options = options, .machine = machine(), .compiler = compiler()};

    const gg::ScopedDenormalsOff noDenormals;
    SpaceStage stage;
    stage.warmUp();
    for (int r = 0; r < options.rounds; ++r)
    {
        report.nsPerSample.push_back(stage.timeRun(options.runSeconds));
        if (progress)
        {
            progress(r + 1, options.rounds);
        }
    }

    const std::vector<double> &runs = report.nsPerSample;
    report.median = quantile(runs, 0.5);
    report.min = *std::min_element(runs.begin(), runs.end());
    report.iqr = quantile(runs, 0.75) - quantile(runs, 0.25);
    // ns per stereo sample x 48,000 samples / 1e9 ns, as a percentage.
    report.cpuPctOneCoreAt48k = report.median * kSampleRate / 1e9 * 100.0;
    return report;
}

std::string toJson(const SpaceReport &report)
{
    const gg::core::SpaceParams &p = kSpaceParams;
    const nlohmann::ordered_json j{
        {"benchmark", "space"},
        {"machine", report.machine},
        {"compiler", report.compiler},
        {"commit", report.options.commit},
        {"sampleRate", kSampleRate},
        {"runSeconds", report.options.runSeconds},
        {"rounds", report.options.rounds},
        {"settings",
         {
             {"diffusion", p.diffusion},
             {"amount", p.amount},
             {"size", (p.roomSize + 1.0f) / 2.0f},
             {"decayLog2", p.decayLog2},
             {"infinite", p.infinite},
             {"hfDamping", p.hfDamping},
             {"modulation", p.modulation},
             {"predelaySeconds", p.predelaySeconds},
             {"active", true},
         }},
        {"runs", report.nsPerSample},
        {"nsPerSample", report.median},
        {"min", report.min},
        {"iqr", report.iqr},
        {"cpuPctOneCoreAt48k", report.cpuPctOneCoreAt48k},
    };
    return j.dump(2, ' ', false, nlohmann::ordered_json::error_handler_t::replace) + "\n";
}

int spaceMain(const SpaceOptions &options, const std::string &jsonPath, std::string &out,
              const Progress &progress)
{
    const SpaceReport report = runSpace(options, progress); // validates the options
    const gg::core::SpaceParams &p = kSpaceParams;

    out +=
        formatLine("gg-bench --space: the diffuser + reverb on stereo noise at 48 kHz, %d rounds "
                   "of >= %g s\n",
                   options.rounds, options.runSeconds);
    out += formatLine("settings: Diffusion %g, Amount %g, Size %g, Decay %g%s, Damping %g, "
                      "Modulation %g, Pre-delay %g, active\n",
                      double(p.diffusion), double(p.amount), double((p.roomSize + 1.0f) / 2.0f),
                      double(p.decayLog2), p.infinite ? " (infinite)" : "", double(p.hfDamping),
                      double(p.modulation), double(p.predelaySeconds));
    out += "commit:   " + options.commit + "\nmachine:  " + report.machine +
           "\ncompiler: " + report.compiler + "\n";
    out +=
        formatLine("\n%-18s %12s %12s %12s %12s\n", "stage", "median", "min", "IQR", "% core @48k");
    out += formatLine("%-18s %12.3f %12.3f %12.3f %12.3f\n", "space", report.median, report.min,
                      report.iqr, report.cpuPctOneCoreAt48k);
    out += "\nmedian, min and IQR: ns per stereo sample over the rounds; % core from the median\n"
           "report-only: no baseline and no ratio\n";

    if (!jsonPath.empty())
    {
        if (!writeText(jsonPath, toJson(report)))
        {
            std::fprintf(stderr, "error: couldn't write %s\n", jsonPath.c_str());
            return 1;
        }
        out += "\nwrote " + jsonPath + "\n";
    }
    return 0;
}
} // namespace gg::bench
