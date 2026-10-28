// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// gg-bench times the prototype grain kernels in Kernels.h. It is the M0 kernel benchmark spike,
// whose numbers set the CPU target in INTENT §16, made gate-grade for M1 (carry-over B-I2,
// [133], [132]). With --canvas it times the canvas renderer instead (the M0 canvas stress spike,
// see below).
//
//   gg-bench [--quick | --seconds S] [--rounds N] [--grains N] [--cases a,b] [--commit SHA]
//            [--json out.json]
//   gg-bench --canvas [--quick] [--json out.json]
//
// The timing itself is in Bench.h and Bench.cpp: by default 2,000 grains of 100 ms over a 60 s
// stereo noise source at 48 kHz, every kernel at block sizes 32, 64 and 256. Each configuration
// warms up once, then every round times each configuration once, for at least 2 s (0.2 s with
// --quick), in an order rotated by the round and reversed in odd rounds. The default is 5
// rounds. gg-bench prints each configuration's median, min and IQR over the rounds; the JSON
// (schema 2) keeps every run.
//
// --canvas times gg-ui-core's splat renderer, headless, at 3200 x 1200: a 1600 x 600 canvas on
// a 2x display. Each frame generates the editor's stress workload (4,096, 8,192 or 16,384
// grains), then clears the canvas, splats the grains and resolves them to ARGB, as the editor
// does before drawing the image. After a warm-up it times 240 frames (30 with --quick) and
// prints, as JSON on stdout, the median of each stage plus the median, mean and 95th percentile
// of a frame's clear + splat + resolve. Generating the synthetic grains is reported but kept out
// of that total: the real canvas gets its grains from the engine. INTENT §16 budgets 3 ms of
// UI-thread time per frame with 8,000 grains; drawing the image costs extra, which only the
// editor's own "frame ms" readout measures.

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#endif

#include "Bench.h"
#include "gg/ui/SplatRenderer.h"
#include "gg/ui/StressGrains.h"

namespace
{
using gg::bench::compiler;
using gg::bench::machine;

volatile double gSink = 0.0; // the rendered output feeds this, so it can't be optimised away

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

// ---- --canvas: the canvas stress spike ----

constexpr uint32_t kCanvasWidth = 3200;
constexpr uint32_t kCanvasHeight = 1200;
constexpr float kCanvasPixelScale = 2.0f; // a 1600 x 600 canvas on a 2x display
constexpr std::array<uint32_t, 3> kCanvasGrains{4096, 8192, 16384};
constexpr int kCanvasWarmupFrames = 20;
constexpr double kCanvasBudgetMs = 3.0; // INTENT §16, at 8,000 grains
constexpr uint32_t kCanvasTint = 0xFFC46B;

struct CanvasResult
{
    uint32_t grains;
    double generateMs; // median; not part of the total
    double clearMs;    // median
    double splatMs;    // median
    double resolveMs;  // median
    double totalMs;    // median of clear + splat + resolve
    double totalMsMean;
    double totalMsP95;
};

/// The p-th quantile (0..1) of v, nearest rank. v must not be empty.
double quantile(std::vector<double> v, double p)
{
    std::sort(v.begin(), v.end());
    const auto rank = static_cast<std::size_t>(p * static_cast<double>(v.size() - 1) + 0.5);
    return v[std::min(rank, v.size() - 1)];
}

CanvasResult measureCanvas(uint32_t grains, int frames, gg::ui::SplatRenderer &canvas,
                           std::vector<uint32_t> &pixels)
{
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::duration d) {
        return std::chrono::duration<double, std::milli>(d).count();
    };
    std::vector<gg::ui::Splat> splats(grains);
    std::vector<double> generateMs, clearMs, splatMs, resolveMs, totalMs;
    uint64_t sink = 0;
    for (int f = 0; f < kCanvasWarmupFrames + frames; ++f)
    {
        const auto t0 = Clock::now();
        gg::ui::stressGrains(static_cast<uint64_t>(f), grains, kCanvasWidth, kCanvasHeight,
                             kCanvasPixelScale, splats.data());
        const auto t1 = Clock::now();
        canvas.clear();
        const auto t2 = Clock::now();
        for (const gg::ui::Splat &s : splats)
        {
            canvas.add(s.x, s.y, s.radius, s.intensity);
        }
        const auto t3 = Clock::now();
        canvas.resolveToArgb(pixels.data(), kCanvasWidth, kCanvasTint);
        const auto t4 = Clock::now();
        sink += pixels[(static_cast<std::size_t>(f) * 7919u) % pixels.size()];
        if (f >= kCanvasWarmupFrames)
        {
            generateMs.push_back(ms(t1 - t0));
            clearMs.push_back(ms(t2 - t1));
            splatMs.push_back(ms(t3 - t2));
            resolveMs.push_back(ms(t4 - t3));
            totalMs.push_back(ms(t4 - t1));
        }
    }
    gSink = gSink + static_cast<double>(sink);

    double mean = 0.0;
    for (const double t : totalMs)
    {
        mean += t;
    }
    mean /= static_cast<double>(totalMs.size());
    return {grains,
            quantile(generateMs, 0.5),
            quantile(clearMs, 0.5),
            quantile(splatMs, 0.5),
            quantile(resolveMs, 0.5),
            quantile(totalMs, 0.5),
            mean,
            quantile(totalMs, 0.95)};
}

void writeCanvasJson(std::FILE *f, int frames, const std::vector<CanvasResult> &results)
{
    std::fprintf(f,
                 "{\n  \"benchmark\": \"canvas\",\n  \"machine\": %s,\n  \"compiler\": %s,\n"
                 "  \"width\": %u,\n  \"height\": %u,\n  \"pixelScale\": %.1f,\n"
                 "  \"frames\": %d,\n  \"budgetMs\": %.1f,\n  \"results\": [\n",
                 jsonString(machine()).c_str(), jsonString(compiler()).c_str(), kCanvasWidth,
                 kCanvasHeight, static_cast<double>(kCanvasPixelScale), frames, kCanvasBudgetMs);
    for (std::size_t i = 0; i < results.size(); ++i)
    {
        const CanvasResult &r = results[i];
        std::fprintf(f,
                     "    {\"grains\": %u, \"generateMs\": %.3f, \"clearMs\": %.3f, "
                     "\"splatMs\": %.3f, \"resolveMs\": %.3f, \"totalMs\": %.3f, "
                     "\"totalMsMean\": %.3f, \"totalMsP95\": %.3f}%s\n",
                     r.grains, r.generateMs, r.clearMs, r.splatMs, r.resolveMs, r.totalMs,
                     r.totalMsMean, r.totalMsP95, i + 1 < results.size() ? "," : "");
    }
    std::fprintf(f, "  ]\n}\n");
}

int runCanvas(bool quick, const std::string &jsonPath)
{
    const int frames = quick ? 30 : 240;
    gg::ui::SplatRenderer canvas(kCanvasWidth, kCanvasHeight);
    std::vector<uint32_t> pixels(static_cast<std::size_t>(kCanvasWidth) * kCanvasHeight);

    std::vector<CanvasResult> results;
    for (const uint32_t grains : kCanvasGrains)
    {
        results.push_back(measureCanvas(grains, frames, canvas, pixels));
    }

    writeCanvasJson(stdout, frames, results);
    if (!jsonPath.empty())
    {
        std::FILE *f = std::fopen(jsonPath.c_str(), "w");
        if (f == nullptr)
        {
            std::fprintf(stderr, "error: couldn't write %s\n", jsonPath.c_str());
            return 1;
        }
        writeCanvasJson(f, frames, results);
        if (std::fclose(f) != 0)
        {
            std::fprintf(stderr, "error: couldn't write %s\n", jsonPath.c_str());
            return 1;
        }
    }
    return 0;
}

void usage(std::FILE *f)
{
    std::string cases;
    for (const std::string &c : gg::bench::caseNames())
    {
        cases += (cases.empty() ? "" : ", ") + c;
    }
    std::fprintf(f,
                 "usage: gg-bench [--quick | --seconds S] [--rounds N] [--grains N] [--cases a,b]\n"
                 "                [--commit SHA] [--json out.json]\n"
                 "       gg-bench --canvas [--quick] [--json out.json]\n"
                 "\n"
                 "Times the prototype grain kernels: 2,000 grains of 100 ms over a 60 s stereo\n"
                 "noise source at 48 kHz, at block sizes 32, 64 and 256. Every configuration runs\n"
                 "once per round, for at least 2 s, in an order that changes from round to round;\n"
                 "each result is the median, min and IQR of its 5 runs.\n"
                 "\n"
                 "With --canvas, times the canvas splat renderer instead: clear, splat and\n"
                 "resolve at 3200 x 1200 with 4,096, 8,192 and 16,384 grains, over 240 frames,\n"
                 "and prints the results as JSON.\n"
                 "\n"
                 "  --quick           runs of 0.2 s instead of 2 s (30 canvas frames, not 240)\n"
                 "  --seconds S       runs of S seconds (the last of --quick and --seconds wins)\n"
                 "  --rounds N        N rounds (default 5)\n"
                 "  --grains N        N grains (default 2000)\n"
                 "  --cases a,b       only these cases (default all):\n"
                 "                    %s\n"
                 "  --commit SHA      the commit to record in the JSON (default \"unknown\")\n"
                 "  --json out.json   also write the results to out.json\n"
                 "  --canvas          time the canvas renderer, not the grain kernels\n",
                 cases.c_str());
}

/// The whole of s as a double.
bool parseDouble(const char *s, double &out)
{
    errno = 0;
    char *end = nullptr;
    out = std::strtod(s, &end);
    return end != s && *end == '\0' && errno == 0;
}

/// The whole of s as an integer in [lo, hi].
template <typename T> bool parseInt(std::string_view s, T lo, T hi, T &out)
{
    long long v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || end != s.data() + s.size() || v < static_cast<long long>(lo) ||
        v > static_cast<long long>(hi))
    {
        return false;
    }
    out = static_cast<T>(v);
    return true;
}

/// "a,b,c" as {"a", "b", "c"}; false if any name is empty.
bool parseCases(std::string_view s, std::vector<std::string> &out)
{
    out.clear();
    for (;;)
    {
        const auto comma = s.find(',');
        const std::string_view name = s.substr(0, comma);
        if (name.empty())
        {
            return false;
        }
        out.emplace_back(name);
        if (comma == std::string_view::npos)
        {
            return true;
        }
        s.remove_prefix(comma + 1);
    }
}

void printReport(const gg::bench::Report &report)
{
    const gg::bench::Options &o = report.options;
    std::printf("\n%-16s %6s %12s %12s %12s %12s\n", "case", "block", "median", "min", "IQR",
                "% core @48k");
    for (const gg::bench::CaseResult &r : report.results)
    {
        std::printf("%-16s %6u %12.4f %12.4f %12.4f %12.3f\n", r.name.c_str(), r.blockSize,
                    r.median, r.min, r.iqr, r.cpuPctOneCoreAt48k);
    }
    std::printf("\nmedian, min and IQR: ns per stereo grain-sample over %d rounds of >= %g s; "
                "%% core from the median\n",
                o.rounds, o.runSeconds);
}
} // namespace

int main(int argc, char **argv)
{
    bool quick = false;
    bool canvas = false;
    std::string jsonPath;
    gg::bench::Options options;
    for (int i = 1; i < argc; ++i)
    {
        const int at = i; // the option; i moves on past its value, if it takes one
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        bool ok = true;
        if (arg == "--quick")
        {
            quick = true;
            options.runSeconds = 0.2;
        }
        else if (arg == "--canvas")
        {
            canvas = true;
        }
        else if (arg == "--seconds" && hasValue)
        {
            ok = parseDouble(argv[++i], options.runSeconds);
        }
        else if (arg == "--rounds" && hasValue)
        {
            ok = parseInt(argv[++i], 1, std::numeric_limits<int>::max(), options.rounds);
        }
        else if (arg == "--grains" && hasValue)
        {
            ok = parseInt<uint32_t>(argv[++i], 1, std::numeric_limits<uint32_t>::max(),
                                    options.grains);
        }
        else if (arg == "--cases" && hasValue)
        {
            ok = parseCases(argv[++i], options.cases);
        }
        else if (arg == "--commit" && hasValue)
        {
            options.commit = argv[++i];
        }
        else if (arg == "--json" && hasValue)
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
            ok = false;
        }
        if (!ok)
        {
            std::fprintf(stderr, "error: bad argument: %s%s%s\n\n", argv[at], i > at ? " " : "",
                         i > at ? argv[i] : "");
            usage(stderr);
            return 2;
        }
    }

#if defined(BUILD_IS_DEBUG)
    std::fprintf(stderr, "warning: this is a Debug build; its timings mean little\n");
#endif
#if defined(__APPLE__)
    // Ask for a performance core: the §16 targets are a share of one.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif

    if (canvas)
    {
        return runCanvas(quick, jsonPath);
    }

    try
    {
        gg::bench::validate(options);
    }
    catch (const std::invalid_argument &e)
    {
        std::fprintf(stderr, "error: %s\n\n", e.what());
        usage(stderr);
        return 2;
    }

    std::printf("gg-bench: %u grains of 100 ms, 60 s stereo noise at 48 kHz, %d interleaved "
                "rounds of >= %g s\n",
                options.grains, options.rounds, options.runSeconds);
    std::printf("commit:   %s\nmachine:  %s\ncompiler: %s\n", options.commit.c_str(),
                machine().c_str(), compiler().c_str());
    std::fflush(stdout);

    const gg::bench::Report report = gg::bench::run(options, [](int round, int rounds) {
        std::fprintf(stderr, "gg-bench: round %d of %d done\n", round, rounds);
    });
    printReport(report);

    if (!jsonPath.empty())
    {
        const std::string json = gg::bench::toJson(report);
        std::FILE *f = std::fopen(jsonPath.c_str(), "w");
        const bool written = f != nullptr && std::fputs(json.c_str(), f) >= 0;
        if (f == nullptr || std::fclose(f) != 0 || !written)
        {
            std::fprintf(stderr, "error: couldn't write %s\n", jsonPath.c_str());
            return 1;
        }
        std::printf("\nwrote %s\n", jsonPath.c_str());
    }
    return 0;
}
