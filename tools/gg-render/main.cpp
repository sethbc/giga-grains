// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// gg-render: renders a render spec (RenderSpec.h) offline and deterministically, writes the
// audio, its metrics, its grain-event log and a spectrogram, and compares the metrics and the
// event log with goldens. A sample-mode render is durationSeconds of stream activity plus a
// release tail until all grains finish; an instrument or FX render runs Giga Grains' or Giga
// Grains FX's Processor, over a sample or an input, for exactly durationSeconds (render() in
// Render.h).
//
//   gg-render <spec.json> [-o out.wav] [--metrics out.json] [--events out.jsonl]
//             [--spectrogram out.png] [--compare golden.metrics.json]
//             [--compare-events golden.events.jsonl] [--block-size N] [--media-dir DIR]
//             [--content-dir DIR] [--captures-dir DIR] [--save-state out.ggst]
//   gg-render --write-media DIR
//
// --media-dir resolves the spec's relative file paths against DIR instead of the spec's
// directory. --content-dir names a content root, --captures-dir the folder audio past the embed
// limit is filed in, and --save-state writes the session state at the render's end (RenderOptions
// in Render.h; instrument and FX specs only). --write-media writes the test media the golden
// cases read and the two state fixtures (writeTestMedia in Signals.h) into DIR, and does nothing
// else.
//
// Exit status: 0 = pass (every requested comparison matched, or the media were written),
// 1 = a comparison failed, 2 = bad input (arguments, spec, source, sample, input or golden files)
// or an output that couldn't be written, 3 = an internal error (anything else thrown, such as
// std::logic_error or std::bad_alloc). Every path is UTF-8 (on Windows the arguments are
// converted to it, and files are opened with wide paths).

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
using gg::render::pathFromUtf8;

using gg::render::kExitBadInput;
using gg::render::kExitCompareFailed;
using gg::render::kExitPass;

int fail(const std::string &message)
{
    fmt::print(stderr, "gg-render: {}\n", message);
    return kExitBadInput;
}

std::optional<std::string> readFile(const std::string &path)
{
    std::ifstream in(pathFromUtf8(path), std::ios::binary);
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
    const fs::path parent = pathFromUtf8(path).parent_path();
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
    std::ofstream out(pathFromUtf8(path), std::ios::binary | std::ios::trunc);
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

/// gg-render's command line; main() turns what it throws into an exit status.
int run(int argc, char **argv)
{
    CLI::App app{"Renders a Giga Grains render spec offline and deterministically, and compares "
                 "the result with goldens. A sample-mode spec renders durationSeconds of stream "
                 "activity plus a release tail until all grains finish; an instrument spec "
                 "(\"mode\": \"instrument\") or FX spec (\"mode\": \"fx\") runs Giga Grains' "
                 "or Giga Grains FX's processing, over a sample or an input, for exactly "
                 "durationSeconds.\nExit status: 0 = pass, 1 = a comparison failed, 2 = bad input "
                 "or an output that couldn't be written, 3 = an internal error.",
                 "gg-render"};
    argv = app.ensure_utf8(argv);
    std::string specPath;
    std::string wavPath;
    std::string metricsPath;
    std::string eventsPath;
    std::string spectrogramPath;
    std::string goldenMetricsPath;
    std::string goldenEventsPath;
    uint32_t blockSize = 0;
    std::string mediaDir;
    std::string contentDir;
    std::string capturesDir;
    std::string saveStatePath;
    std::string writeMediaDir;
    // Every option but --write-media, which takes none of them.
    std::vector<CLI::Option *> renderOptions;
    renderOptions.push_back(app.add_option("spec", specPath, "The render spec (JSON)"));
    renderOptions.push_back(
        app.add_option("-o,--output", wavPath, "Write the render as a 32-bit float WAV"));
    renderOptions.push_back(
        app.add_option("--metrics", metricsPath, "Write the render's metrics (JSON)"));
    renderOptions.push_back(
        app.add_option("--events", eventsPath, "Write the grain-event log (JSON lines)"));
    renderOptions.push_back(
        app.add_option("--spectrogram", spectrogramPath, "Write a spectrogram (PNG)"));
    renderOptions.push_back(
        app.add_option("--compare", goldenMetricsPath,
                       "Compare the metrics with a golden .metrics.json, within tolerances"));
    renderOptions.push_back(
        app.add_option("--compare-events", goldenEventsPath,
                       "Compare the grain-event log with a golden .events.jsonl, exactly per "
                       "line (CR and final newline ignored)"));
    auto *const blockSizeOption =
        app.add_option("--block-size", blockSize, "Override the spec's blockSize")
            ->check(CLI::Range(uint32_t{1}, gg::render::kMaxBlockSize));
    renderOptions.push_back(blockSizeOption);
    renderOptions.push_back(
        app.add_option("--media-dir", mediaDir,
                       "Resolve the spec's relative file paths against this directory instead "
                       "of the spec's"));
    renderOptions.push_back(app.add_option(
        "--content-dir", contentDir,
        "A content root: where a restored state's samples are found by their content path "
        "(besides the spec's directory or --media-dir)"));
    renderOptions.push_back(app.add_option(
        "--captures-dir", capturesDir,
        "Where --save-state files audio past the 32 MiB embed limit, and where a state's filed "
        "rings are found"));
    renderOptions.push_back(app.add_option(
        "--save-state", saveStatePath,
        "Write the session state (rings, samples, values) at the render's end; instrument and "
        "FX specs only"));
    auto *const writeMediaOption = app.add_option(
        "--write-media", writeMediaDir,
        "Write the test media the golden cases read into this directory, and exit; it takes no "
        "other option");
    for (CLI::Option *option : renderOptions)
    {
        writeMediaOption->excludes(option);
    }
    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError &e)
    {
        // --help is a ParseError too, with exit code 0.
        return app.exit(e) == 0 ? kExitPass : kExitBadInput;
    }

    if (!writeMediaDir.empty())
    {
        std::string error;
        if (!gg::render::writeTestMedia(writeMediaDir, error))
        {
            return fail(error);
        }
        fmt::print("wrote {}, {}, {}, {}, {} and {} in {}\n", gg::render::kToneMedium,
                   gg::render::kLoopMedium, gg::render::kSineMedium, gg::render::kNoiseMedium,
                   gg::render::kFrozenRingState, gg::render::kFourSamplesState, writeMediaDir);
        return kExitPass;
    }
    if (specPath.empty())
    {
        return fail("no render spec given (see --help)");
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
    const std::string baseDir = mediaDir.empty()
                                    ? gg::render::utf8FromPath(pathFromUtf8(specPath).parent_path())
                                    : mediaDir;
    gg::render::resolvePaths(*spec, baseDir);
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
        audio = gg::render::render(*spec, &events,
                                   {.contentDir = contentDir,
                                    .capturesDir = capturesDir,
                                    .saveStatePath = saveStatePath,
                                    .mediaDir = baseDir.empty() ? "." : baseDir});
    }
    catch (const gg::render::BadInput &e)
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
} // namespace

int main(int argc, char **argv)
{
    // Anything thrown but bad input is an internal error, exit 3 (tested in
    // tests/tools/render_spec_tests.cpp).
    return gg::render::guarded([&] { return run(argc, argv); });
}
