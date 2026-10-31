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
//             [--preset file.ggpreset] [--content-dir DIR]
//   gg-render --write-media DIR
//   gg-render --pack-preset <source.json> -o <out.ggpreset> [--content-dir DIR]
//
// --media-dir resolves the spec's relative file paths against DIR instead of the spec's
// directory. --preset sets the spec's "preset" key, or overrides it, and is resolved like it
// (RenderSpec::preset). --content-dir is the content folder a preset's content-relative samples
// are taken from; until MEDI version 2 (after sub-project 2 merges) no preset names a sample, and
// it is accepted and unused. --write-media writes the test media the golden cases read
// (writeTestMedia in Signals.h) into DIR, and does nothing else. --pack-preset compiles a preset
// source (PresetSource.h) into the preset file -o names, and takes no other option but
// --content-dir.
//
// Exit status: 0 = pass (every requested comparison matched, the media were written or the preset
// packed), 1 = a comparison failed, 2 = bad input (arguments, spec, source, sample, input, preset,
// preset source or golden files) or an output that couldn't be written, 3 = an internal error
// (anything else thrown, such as std::logic_error or std::bad_alloc). Every path is UTF-8 (on
// Windows the arguments are converted to it, and files are opened with wide paths).

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

#include "PresetSource.h"
#include "Render.h"
#include "RenderSpec.h"
#include "Signals.h"
#include "gg/core/PresetInfo.h"
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
        fmt::print("event log matches {} ({} lines)\n", goldenPath, golden.size());
        return true;
    }
    fmt::print("FAIL: event log differs from {} at line {} ({} golden lines, {} rendered)\n",
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
    std::string presetPath;
    std::string contentDir;
    std::string writeMediaDir;
    std::string packSourcePath;
    // Every option but --write-media, which takes none of them, and --pack-preset, which takes
    // only -o and --content-dir.
    std::vector<CLI::Option *> renderOptions;
    renderOptions.push_back(app.add_option("spec", specPath, "The render spec (JSON)"));
    auto *const outputOption = app.add_option(
        "-o,--output", wavPath,
        "Write the render as a 32-bit float WAV; with --pack-preset, the preset file to write");
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
    auto *const presetOption = app.add_option(
        "--preset", presetPath,
        "Apply this preset file before the spec's own params and mod: sets the spec's \"preset\" "
        "key, or overrides it, resolved like it (against the spec's directory or --media-dir)");
    renderOptions.push_back(presetOption);
    auto *const contentDirOption =
        app.add_option("--content-dir", contentDir,
                       "The content folder a preset's content-relative samples are taken from");
    auto *const writeMediaOption = app.add_option(
        "--write-media", writeMediaDir,
        "Write the test media the golden cases read into this directory, and exit; it takes no "
        "other option");
    auto *const packPresetOption = app.add_option(
        "--pack-preset", packSourcePath,
        "Compile this preset source (JSON) into the preset file -o names, and exit; it takes no "
        "other option but --content-dir");
    for (CLI::Option *option : renderOptions)
    {
        writeMediaOption->excludes(option);
        packPresetOption->excludes(option);
    }
    writeMediaOption->excludes(outputOption);
    writeMediaOption->excludes(contentDirOption);
    writeMediaOption->excludes(packPresetOption);
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
        fmt::print("wrote {} and {} in {}\n", gg::render::kToneMedium, gg::render::kLoopMedium,
                   writeMediaDir);
        return kExitPass;
    }
    if (!packSourcePath.empty())
    {
        if (wavPath.empty())
        {
            return fail("--pack-preset needs -o <out.ggpreset>");
        }
        const auto sourceText = readFile(packSourcePath);
        if (!sourceText)
        {
            return fail(fmt::format("can't read {}", packSourcePath));
        }
        std::string error;
        const auto source = gg::render::parsePresetSource(*sourceText, error);
        if (!source)
        {
            return fail(fmt::format("{}: {}", packSourcePath, error));
        }
        std::optional<fs::path> contentRoot;
        if (!contentDir.empty())
        {
            contentRoot = pathFromUtf8(contentDir);
        }
        const std::string bytes = gg::render::packPreset(*source, contentRoot, error);
        if (bytes.empty())
        {
            return fail(fmt::format("{}: {}", packSourcePath, error));
        }
        if (!writeFile(wavPath, bytes))
        {
            return fail(fmt::format("can't write {}", wavPath));
        }
        fmt::print("packed {} ({}) into {}\n", source->info.name,
                   gg::core::modeName(source->info.product), wavPath);
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
    // Given at all, even empty (T4): an empty one is refused as the spec's empty key is.
    if (presetOption->count() > 0)
    {
        if (presetPath.empty())
        {
            return fail(fmt::format("{}: --preset must not be empty", specPath));
        }
        if (spec->mode == gg::render::RenderMode::Sample)
        {
            return fail(fmt::format("{}: --preset needs an instrument or FX spec", specPath));
        }
        spec->preset = presetPath;
    }
    gg::render::resolvePaths(
        *spec, mediaDir.empty() ? gg::render::utf8FromPath(pathFromUtf8(specPath).parent_path())
                                : mediaDir);
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
    fmt::print("{}: {} frames at {} Hz ({} + {} release tail), {} grain events, peak {:.2f} dB, "
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
