// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// gg-render: renders a render spec (RenderSpec.h) offline and deterministically, writes the
// audio, its metrics, its grain-event log and a spectrogram, and compares the metrics and the
// event log with goldens. A render is durationSeconds of stream activity plus a release tail
// until all grains finish (render() in Render.h).
//
//   gg-render <spec.json> [-o out.wav] [--metrics out.json] [--events out.jsonl]
//             [--spectrogram out.png] [--compare golden.metrics.json]
//             [--compare-events golden.events.jsonl] [--block-size N]
//
// Exit status: 0 = pass (every requested comparison matched), 1 = a comparison failed,
// 2 = bad input (arguments, spec, source or golden files) or an output that couldn't be written.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include "Render.h"
#include "RenderSpec.h"
#include "Signals.h"
#include "gg/metrics/AudioData.h"
#include "gg/metrics/Metrics.h"
#include "gg/metrics/Spectrogram.h"
#include "gg/metrics/WavIO.h"

namespace
{
namespace fs = std::filesystem;

constexpr int kExitPass = 0;
constexpr int kExitCompareFailed = 1;
constexpr int kExitBadInput = 2;

int fail(const std::string &message)
{
    fmt::print(stderr, "gg-render: {}\n", message);
    return kExitBadInput;
}

std::optional<std::string> readFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (in.bad())
    {
        return std::nullopt;
    }
    return text;
}

/// Creates the directories path's file goes in, so outputs can go to a fresh build tree.
void makeParentDirectories(const std::string &path)
{
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty())
    {
        std::error_code ignored; // a failure shows up when the file is written
        fs::create_directories(parent, ignored);
    }
}

/// Writes text byte for byte: binary mode, so no platform translates the newlines.
bool writeFile(const std::string &path, const std::string &text)
{
    makeParentDirectories(path);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    return !out.fail();
}

std::string joinLines(const std::vector<std::string> &lines)
{
    std::string text;
    for (const auto &line : lines)
    {
        text += line;
        text += '\n';
    }
    return text;
}

/// The lines of a JSON-lines file, without their newlines. A CR before a newline is dropped too,
/// in case a checkout converted the line endings.
std::vector<std::string> splitLines(const std::string &text)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
        {
            end = text.size();
        }
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        start = end + 1;
    }
    return lines;
}

/// Prints the result of comparing the event log with a golden one; returns whether they match.
bool compareEvents(const std::vector<std::string> &golden, const std::vector<std::string> &actual,
                   const std::string &goldenPath)
{
    std::size_t i = 0;
    while (i < golden.size() && i < actual.size() && golden[i] == actual[i])
    {
        ++i;
    }
    if (i == golden.size() && i == actual.size())
    {
        fmt::print("event log matches {} ({} grains)\n", goldenPath, golden.size());
        return true;
    }
    fmt::print("FAIL: event log differs from {} at line {} ({} golden grains, {} rendered)\n",
               goldenPath, i + 1, golden.size(), actual.size());
    fmt::print("  golden:   {}\n", i < golden.size() ? golden[i] : "(end of log)");
    fmt::print("  rendered: {}\n", i < actual.size() ? actual[i] : "(end of log)");
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    CLI::App app{"Renders a Giga Grains render spec offline and deterministically, and compares "
                 "the result with goldens. It renders durationSeconds of stream activity plus a "
                 "release tail until all grains finish.\nExit status: 0 = pass, 1 = a comparison "
                 "failed, 2 = bad input or an output that couldn't be written.",
                 "gg-render"};
    std::string specPath;
    std::string wavPath;
    std::string metricsPath;
    std::string eventsPath;
    std::string spectrogramPath;
    std::string goldenMetricsPath;
    std::string goldenEventsPath;
    uint32_t blockSize = 0;
    app.add_option("spec", specPath, "The render spec (JSON)")->required();
    app.add_option("-o,--output", wavPath, "Write the render as a 32-bit float WAV");
    app.add_option("--metrics", metricsPath, "Write the render's metrics (JSON)");
    app.add_option("--events", eventsPath, "Write the grain-event log (JSON lines)");
    app.add_option("--spectrogram", spectrogramPath, "Write a spectrogram (PNG)");
    app.add_option("--compare", goldenMetricsPath,
                   "Compare the metrics with a golden .metrics.json, within tolerances");
    app.add_option("--compare-events", goldenEventsPath,
                   "Compare the grain-event log with a golden .events.jsonl, exactly per line "
                   "(CR and final newline ignored)");
    const auto *blockSizeOption =
        app.add_option("--block-size", blockSize, "Override the spec's blockSize")
            ->check(CLI::Range(uint32_t{1}, gg::render::kMaxBlockSize));
    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError &e)
    {
        // --help is a ParseError too, with exit code 0.
        return app.exit(e) == 0 ? kExitPass : kExitBadInput;
    }

    const auto specText = readFile(specPath);
    if (!specText)
    {
        return fail(fmt::format("can't read {}", specPath));
    }
    std::string error;
    auto spec = gg::render::parseRenderSpec(*specText, error);
    if (!spec)
    {
        return fail(fmt::format("{}: {}", specPath, error));
    }
    if (spec->source.kind == gg::render::SourceKind::File &&
        fs::path(spec->source.path).is_relative())
    {
        spec->source.path = (fs::path(specPath).parent_path() / spec->source.path).string();
    }
    if (blockSizeOption->count() > 0)
    {
        spec->blockSize = blockSize;
    }

    // Read the goldens before rendering, so bad input fails fast.
    std::optional<gg::metrics::Metrics> goldenMetrics;
    if (!goldenMetricsPath.empty())
    {
        const auto text = readFile(goldenMetricsPath);
        if (!text)
        {
            return fail(fmt::format("can't read {}", goldenMetricsPath));
        }
        goldenMetrics = gg::metrics::metricsFromJson(*text);
        if (!goldenMetrics)
        {
            return fail(
                fmt::format("{} isn't a metrics file this version reads", goldenMetricsPath));
        }
    }
    std::optional<std::vector<std::string>> goldenEvents;
    if (!goldenEventsPath.empty())
    {
        const auto text = readFile(goldenEventsPath);
        if (!text)
        {
            return fail(fmt::format("can't read {}", goldenEventsPath));
        }
        goldenEvents = splitLines(*text);
    }

    gg::metrics::AudioData audio;
    std::vector<std::string> events;
    try
    {
        audio = gg::render::render(*spec, &events);
    }
    catch (const std::exception &e)
    {
        return fail(fmt::format("{}: {}", specPath, e.what()));
    }
    const gg::metrics::Metrics metrics = gg::metrics::computeMetrics(audio);

    if (!wavPath.empty())
    {
        makeParentDirectories(wavPath);
        if (!gg::metrics::writeWavFloat32(wavPath, audio))
        {
            return fail(fmt::format("can't write {}", wavPath));
        }
    }
    if (!metricsPath.empty() && !writeFile(metricsPath, gg::metrics::toJson(metrics)))
    {
        return fail(fmt::format("can't write {}", metricsPath));
    }
    if (!eventsPath.empty() && !writeFile(eventsPath, joinLines(events)))
    {
        return fail(fmt::format("can't write {}", eventsPath));
    }
    if (!spectrogramPath.empty())
    {
        makeParentDirectories(spectrogramPath);
        if (!gg::metrics::writeSpectrogramPng(spectrogramPath, audio))
        {
            return fail(fmt::format("can't write {}", spectrogramPath));
        }
    }

    const auto frames = static_cast<int64_t>(audio.channels[0].size());
    const int64_t duration = gg::render::framesFor(spec->durationSeconds, spec->sampleRate);
    fmt::print("{}: {} frames at {} Hz ({} + {} release tail), {} grains, peak {:.2f} dB, "
               "clickScore {:.2f}, nonFinite {}, denormals {}\n",
               specPath, frames, spec->sampleRate, duration, frames - duration, events.size(),
               metrics.peakDb, metrics.clickScore, metrics.nonFinite, metrics.denormals);

    bool pass = true;
    if (goldenMetrics)
    {
        const auto result = gg::metrics::compare(*goldenMetrics, metrics);
        if (result.pass)
        {
            fmt::print("metrics match {}\n", goldenMetricsPath);
        }
        else
        {
            pass = false;
            fmt::print("FAIL: metrics differ from {}\n", goldenMetricsPath);
            for (const auto &failure : result.failures)
            {
                fmt::print("  {}\n", failure);
            }
        }
    }
    if (goldenEvents && !compareEvents(*goldenEvents, events, goldenEventsPath))
    {
        pass = false;
    }
    return pass ? kExitPass : kExitCompareFailed;
}
