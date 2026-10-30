// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// gg-bench is the CPU benchmark and CI's CPU gate (INTENT §16, §19). It times the real engine's
// render path, the engine cases, through Engine::process: engine-sample is X, the §16
// headline's case, beside engine-sample-eco, engine-live (the FX's figure), engine-full-pool and
// the four-layer cases engine-layers and engine-layers-full (report-only). It times them against
// the reference kernel, M0's hermite-simd frozen in Kernels.h, and reports each engine case's ratio
// to it, which the CI gate checks. It also still times M0's prototype kernels in Kernels.h. It
// began as the M0 kernel benchmark spike and was made gate-grade in M1 (carry-over B-I2, [133],
// [132]; spec §5). With --canvas it times the canvas renderer instead (the M0 canvas stress spike,
// see below), and with --space, Space.
//
//   gg-bench [--quick | --seconds S] [--rounds N] [--grains N] [--cases a,b] [--commit SHA]
//            [--json out.json] [--baseline baseline.json]
//   gg-bench --make-baseline <leg> <out.json> [--merge <old.json>] <in.json>...
//   gg-bench --canvas [--quick] [--json out.json]
//   gg-bench --space [--quick | --seconds S] [--rounds N] [--commit SHA] [--json out.json]
//
// An option that doesn't apply to the mode chosen (--grains with --space, say, or anything
// beside --make-baseline) is bad usage: exit 2, never silently ignored.
//
// The timing itself is in Bench.h and Bench.cpp: by default 2,000 grains of 100 ms over a 60 s
// stereo noise source at 48 kHz, every case at block sizes 32, 64 and 256. Each configuration
// warms up once, then every round times each configuration once, for at least 2 s (0.2 s with
// --quick), in an order rotated by the round and reversed in odd rounds. The default is 5
// rounds. gg-bench prints each configuration's median, min and IQR over the rounds, then each
// engine case's ratio to the reference kernel; the JSON (schema 2) keeps every run.
//
// --baseline checks the ratios against a baseline file (tools/gg-bench/baselines/README.md): its
// entry for this machine's CPU model, since ratios differ between models. It prints a verdict
// for each ratio, and names each of the entry's ratios the run lacks, or prints "no baseline for
// <machine>; no verdict" when the file has no entry for this model. The exit status is 0 if none
// regressed and none was missing (or there was no verdict), 3 if any regressed or was missing, 1
// for an error (an unreadable or malformed file, say, or a run with no ratios to check against
// an entry) and 2 for bad usage. CI reports it without failing until 1.0.0 (ruling 3; INTENT §23
// #61).
// --make-baseline writes a baseline for a CI leg from the JSON of runs already made, one entry
// per CPU model they ran on, and exits. With --merge <old.json> it keeps old.json's entries for
// the models the runs don't have, so a model can be added, or remade, without the runs the other
// entries came from.
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
//
// --space times Space's per-sample stage, the diffuser then the grid-aligned reverb, on stereo
// noise at 48 kHz, active, with Diffusion 1, Size 0.5 and Decay 1.0 (Bench.h has every setting).
// It prints the median, min and IQR of ns per stereo sample over 5 rounds of >= 2 s (0.2 s with
// --quick; --seconds and --rounds set them) and the median's % of one core at 48 kHz.
// It is report-only, with no baseline and no ratio; its JSON is its own document, apart from
// schema 2. Sub-project 6 publishes the figure beside the grain engine's.

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

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

/// The p-th quantile (0..1) of v, nearest rank: the canvas reports a measured frame time, where
/// gg::bench::quantile interpolates between two. v must not be empty.
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

// ---- --space: Space's cost ----

void usage(std::FILE *f);

int runSpace(const gg::bench::Options &options, const std::string &jsonPath)
{
    const gg::bench::SpaceOptions spaceOptions{
        .runSeconds = options.runSeconds, .rounds = options.rounds, .commit = options.commit};
    std::string text;
    int status = 0;
    try
    {
        status = gg::bench::spaceMain(spaceOptions, jsonPath, text, [](int round, int rounds) {
            std::fprintf(stderr, "gg-bench: round %d of %d done\n", round, rounds);
        });
    }
    catch (const std::invalid_argument &e)
    {
        std::fprintf(stderr, "error: %s\n\n", e.what());
        usage(stderr);
        return 2;
    }
    std::fputs(text.c_str(), stdout);
    return status;
}

void usage(std::FILE *f)
{
    std::string cases;
    for (const std::string &c : gg::bench::caseNames())
    {
        cases += (cases.empty() ? "" : ", ") + c;
    }
    std::fprintf(
        f,
        "usage: gg-bench [--quick | --seconds S] [--rounds N] [--grains N] [--cases a,b]\n"
        "                [--commit SHA] [--json out.json] [--baseline baseline.json]\n"
        "       gg-bench --make-baseline <leg> <out.json> [--merge <old.json>] <in.json>...\n"
        "       gg-bench --canvas [--quick] [--json out.json]\n"
        "       gg-bench --space [--quick | --seconds S] [--rounds N] [--commit SHA]\n"
        "                [--json out.json]\n"
        "\n"
        "Times the engine, the reference kernel and M0's prototype grain kernels: 2,000\n"
        "grains of 100 ms over 60 s of stereo noise at 48 kHz, at block sizes 32, 64 and\n"
        "256. Every configuration runs once per round, for at least 2 s, in an order\n"
        "that changes from round to round; each result is the median, min and IQR of its\n"
        "5 runs. Each engine case's ratio is its time over the reference's, round by\n"
        "round.\n"
        "\n"
        "With --canvas, times the canvas splat renderer instead: clear, splat and\n"
        "resolve at 3200 x 1200 with 4,096, 8,192 and 16,384 grains, over 240 frames,\n"
        "and prints the results as JSON.\n"
        "\n"
        "With --space, times Space instead: the diffuser and the reverb on stereo noise\n"
        "at 48 kHz, in ns per stereo sample over 5 rounds of >= 2 s. Report-only: no\n"
        "baseline and no ratio.\n"
        "\n"
        "  --quick           runs of 0.2 s instead of 2 s (30 canvas frames, not 240)\n"
        "  --seconds S       runs of S seconds (the last of --quick and --seconds wins)\n"
        "  --rounds N        N rounds (default 5)\n"
        "  --grains N        N grains (default 2000)\n"
        "  --cases a,b       only these cases (default all):\n"
        "                    %s\n"
        "  --commit SHA      the commit to record in the JSON (default \"unknown\")\n"
        "  --json out.json   also write the results to out.json\n"
        "  --baseline file   check the ratios against this CPU model's entry in a\n"
        "                    baseline; exit 3 if any regressed or is missing\n"
        "  --make-baseline <leg> <out.json> [--merge <old.json>] <in.json>...\n"
        "                    write a baseline for <leg> from gg-bench JSON runs, one entry\n"
        "                    per CPU model, and exit; with --merge, keep old.json's entries\n"
        "                    for the models the runs don't replace\n"
        "  --canvas          time the canvas renderer, not the grain kernels\n"
        "  --space           time Space (the diffuser and the reverb), not the grain\n"
        "                    kernels\n"
        "\n"
        "An option the mode doesn't take is bad usage.\n"
        "\n"
        "Exit status: 0 done (no ratio regressed), 1 error, 2 bad usage, 3 a ratio\n"
        "regressed or a baseline ratio is missing from the run.\n",
        cases.c_str());
}

/// The options each mode takes, beside its own: anything else given with it is bad usage.
bool applies(std::string_view option, bool canvas, bool space, bool makeBaseline)
{
    const auto among = [option](std::initializer_list<std::string_view> list) {
        return std::find(list.begin(), list.end(), option) != list.end();
    };
    if (makeBaseline)
    {
        return option == "--make-baseline";
    }
    if (canvas)
    {
        return among({"--canvas", "--quick", "--json"});
    }
    if (space)
    {
        return among({"--space", "--quick", "--seconds", "--rounds", "--commit", "--json"});
    }
    return !among({"--canvas", "--space", "--make-baseline"});
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
    std::printf("\n%-18s %6s %9s %12s %12s %12s %12s\n", "case", "block", "grains", "median", "min",
                "IQR", "% core @48k");
    for (const gg::bench::CaseResult &r : report.results)
    {
        std::printf("%-18s %6u %9.1f %12.4f %12.4f %12.4f %12.3f\n", r.name.c_str(), r.blockSize,
                    r.grains, r.median, r.min, r.iqr, r.cpuPctOneCoreAt48k);
    }
    std::printf("\nmedian, min and IQR: ns per stereo grain-sample over %d rounds of >= %g s; "
                "%% core from the median\n"
                "grains: an engine case's mean active grains\n",
                o.rounds, o.runSeconds);

    if (report.ratios.empty())
    {
        return;
    }
    std::printf("\n%-18s %6s %12s %12s\n", "ratio", "block", "median", "IQR");
    for (const gg::bench::Ratio &r : report.ratios)
    {
        std::printf("%-18s %6u %12.4f %12.4f\n", r.name.c_str(), r.blockSize, r.median, r.iqr);
    }
    std::printf("\nratio: the case's ns per grain-sample over reference's, in the same round; "
                "median and IQR over the rounds\n");
}

/// Prints the verdicts and the baseline entries the run lacks, or that there are none for this
/// machine; returns comparison.failures(): the ratios that regressed plus the missing entries.
std::size_t printVerdicts(const gg::bench::Comparison &comparison, const std::string &machine,
                          const std::string &path, const nlohmann::json &baseline)
{
    if (!comparison.found)
    {
        std::printf("\nbaseline: %s (leg %s)\nno baseline for %s; no verdict\n", path.c_str(),
                    baseline["leg"].get<std::string>().c_str(), machine.c_str());
        return 0;
    }
    const std::vector<gg::bench::Verdict> &verdicts = comparison.verdicts;
    std::printf("\nbaseline: %s (leg %s, machine %s, commit %s, %u run%s)\n", path.c_str(),
                baseline["leg"].get<std::string>().c_str(), machine.c_str(),
                comparison.commit.c_str(), comparison.runs, comparison.runs == 1 ? "" : "s");
    std::printf("\n%-18s %6s %12s %12s  %s\n", "case", "block", "ratio", "limit", "verdict");
    std::size_t regressed = 0;
    for (const gg::bench::Verdict &v : verdicts)
    {
        const bool known = !std::isnan(v.limit);
        if (known)
        {
            std::printf("%-18s %6u %12.4f %12.4f  %s\n", v.name.c_str(), v.blockSize, v.ratio,
                        v.limit, v.regressed ? "regressed" : "pass");
        }
        else
        {
            std::printf("%-18s %6u %12.4f %12s  %s\n", v.name.c_str(), v.blockSize, v.ratio, "-",
                        "not in the baseline");
        }
        regressed += v.regressed ? 1 : 0;
    }
    for (const gg::bench::Verdict &v : comparison.missing)
    {
        std::printf("%-18s %6u %12s %12.4f  %s\n", v.name.c_str(), v.blockSize, "-", v.limit,
                    "missing from this run");
    }
    std::string missing;
    if (!comparison.missing.empty())
    {
        missing = ", " + std::to_string(comparison.missing.size()) + " baseline " +
                  (comparison.missing.size() == 1 ? "entry" : "entries") + " missing from this run";
    }
    std::printf("\nverdict: %zu of %zu ratios regressed%s (limit: the baseline's ratio x (1 + its "
                "tolerance))\n",
                regressed, verdicts.size(), missing.c_str());
    return comparison.failures();
}

/// The JSON in the file at path; throws on a file it can't read or JSON it can't parse.
nlohmann::json readJson(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        throw std::runtime_error("couldn't read " + path);
    }
    try
    {
        return nlohmann::json::parse(in);
    }
    catch (const nlohmann::json::exception &e)
    {
        throw std::runtime_error(path + ": " + e.what());
    }
}

/// Writes text to path; false if it couldn't.
bool writeFile(const std::string &path, const std::string &text)
{
    std::FILE *f = std::fopen(path.c_str(), "w");
    const bool written = f != nullptr && std::fputs(text.c_str(), f) >= 0;
    const bool closed = f != nullptr && std::fclose(f) == 0;
    return written && closed;
}

/// --make-baseline's values, <leg> <out.json> [--merge <old.json>] <in.json>..., are well formed:
/// at least one input, and --merge only right after <out.json>, with its file.
bool makeBaselineArgsOk(const std::vector<std::string> &args)
{
    const std::size_t inputs = args.size() > 2 && args[2] == "--merge" ? 4 : 2;
    return args.size() > inputs && std::find(args.begin() + static_cast<std::ptrdiff_t>(inputs),
                                             args.end(), "--merge") == args.end();
}

/// --make-baseline <leg> <out.json> [--merge <old.json>] <in.json>...: args are the values after
/// the option, which makeBaselineArgsOk() accepts.
int makeBaseline(const std::vector<std::string> &args)
{
    const bool merge = args[2] == "--merge";
    try
    {
        // Every file is read before the output is written, so <out.json> may be <old.json>.
        nlohmann::json old;
        if (merge)
        {
            old = readJson(args[3]);
        }
        std::vector<nlohmann::json> runs;
        for (std::size_t i = merge ? 4 : 2; i < args.size(); ++i)
        {
            runs.push_back(readJson(args[i]));
        }
        const nlohmann::json made = gg::bench::makeBaseline(args[0], runs);
        const nlohmann::json baseline = merge ? gg::bench::mergeBaseline(old, made) : made;
        if (!writeFile(args[1],
                       baseline.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) +
                           "\n"))
        {
            std::fprintf(stderr, "error: couldn't write %s\n", args[1].c_str());
            return 1;
        }
        std::printf("wrote %s: leg %s, %zu runs%s%s\n", args[1].c_str(), args[0].c_str(),
                    runs.size(), merge ? ", merged into " : "", merge ? args[3].c_str() : "");
        const auto has = [](const nlohmann::json &b, const nlohmann::json &machine) {
            return std::any_of(
                b["machines"].begin(), b["machines"].end(),
                [&machine](const nlohmann::json &m) { return m["machine"] == machine; });
        };
        for (const nlohmann::json &m : baseline["machines"])
        {
            const auto machineRuns = m["runs"].get<std::size_t>();
            const char *how = !merge                     ? ""
                              : !has(made, m["machine"]) ? " (kept)"
                              : has(old, m["machine"])   ? " (replaced)"
                                                         : " (new)";
            std::printf("  %s: %zu ratios from %zu run%s%s\n",
                        m["machine"].get<std::string>().c_str(), m["ratios"].size(), machineRuns,
                        machineRuns == 1 ? "" : "s", how);
        }
        return 0;
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
} // namespace

int main(int argc, char **argv)
{
    bool quick = false;
    bool canvas = false;
    bool space = false;
    std::string jsonPath;
    std::string baselinePath;
    std::vector<std::string> makeBaselineArgs; // non-empty: --make-baseline's values
    std::vector<std::string_view> given;       // every option, for applies()
    gg::bench::Options options;
    for (int i = 1; i < argc; ++i)
    {
        const int at = i; // the option; i moves on past its value, if it takes one
        const std::string_view arg = argv[i];
        given.push_back(arg);
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
        else if (arg == "--space")
        {
            space = true;
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
        else if (arg == "--baseline" && hasValue)
        {
            baselinePath = argv[++i];
        }
        else if (arg == "--make-baseline" && argc - i > 3)
        {
            // <leg> <out.json> [--merge <old.json>] <in.json>...: it takes the rest of the
            // command line.
            makeBaselineArgs.assign(argv + i + 1, argv + argc);
            i = argc - 1;
            ok = makeBaselineArgsOk(makeBaselineArgs);
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

    for (const std::string_view option : given)
    {
        if (!applies(option, canvas, space, !makeBaselineArgs.empty()))
        {
            const char *mode = !makeBaselineArgs.empty() ? "--make-baseline"
                               : canvas                  ? "--canvas"
                               : space                   ? "--space"
                                                         : "the grain kernels' timing";
            std::fprintf(stderr, "error: %.*s doesn't apply with %s\n\n",
                         static_cast<int>(option.size()), option.data(), mode);
            usage(stderr);
            return 2;
        }
    }

    if (!makeBaselineArgs.empty())
    {
        return makeBaseline(makeBaselineArgs);
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
    if (space)
    {
        return runSpace(options, jsonPath);
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

    // Read and check the baseline before timing anything, so a bad one fails at once.
    nlohmann::json baseline;
    if (!baselinePath.empty())
    {
        try
        {
            baseline = readJson(baselinePath);
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 1;
        }
        try
        {
            gg::bench::checkBaseline(baseline);
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "error: %s: %s\n", baselinePath.c_str(), e.what());
            return 1;
        }
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
        if (!writeFile(jsonPath, gg::bench::toJson(report)))
        {
            std::fprintf(stderr, "error: couldn't write %s\n", jsonPath.c_str());
            return 1;
        }
        std::printf("\nwrote %s\n", jsonPath.c_str());
    }

    if (!baselinePath.empty())
    {
        gg::bench::Comparison comparison;
        try
        {
            comparison = gg::bench::compare(report, baseline);
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "error: %s: %s\n", baselinePath.c_str(), e.what());
            return 1;
        }
        const std::size_t failures =
            printVerdicts(comparison, report.machine, baselinePath, baseline);
        return failures > 0 ? 3 : 0;
    }
    return 0;
}
