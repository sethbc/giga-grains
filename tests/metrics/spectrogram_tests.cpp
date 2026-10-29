// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The spectrogram PNG read back: its pixel format, its orientation in time and frequency and its
// grayscale mapping (carry-over [63]), and a non-ASCII path (carry-over [181]).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/metrics/AudioData.h"
#include "gg/metrics/Spectrogram.h"

// stb_image's PNG reader, for this file only: nothing else in the project reads images, so
// gg::stb_impl compiles just the writer.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

namespace fs = std::filesystem;
using gg::metrics::AudioData;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kHeight = 1024;
// The STFT's bins are 48000 / 2048 = 23.4375 Hz apart; row 0 is bin 1024 (Nyquist), and bin k is
// on row 1024 - k.
constexpr double kBinHz = kSampleRate / 2048.0;

/// frames of silence, then a sine of `amp` on bin `bin` from frame `onset` on.
std::vector<float> toneFrom(size_t onset, int bin, double amp, size_t frames)
{
    std::vector<float> out(frames, 0.0f);
    const double freq = bin * kBinHz;
    for (size_t n = onset; n < frames; ++n)
    {
        const double phase = 2.0 * std::numbers::pi * freq * static_cast<double>(n) / kSampleRate;
        out[n] = static_cast<float>(amp * std::sin(phase));
    }
    return out;
}

fs::path scratch(const fs::path &name)
{
    const fs::path dir = fs::path{GG_TEST_OUTPUT_DIR} / "spectrogram";
    fs::create_directories(dir);
    return dir / name;
}

std::string utf8(const fs::path &p)
{
    const std::u8string s = p.u8string();
    return {s.begin(), s.end()};
}

/// A decoded PNG: its size, the channel count stored in the file, and its pixels as 8-bit gray.
struct Image
{
    int width{0};
    int height{0};
    int fileChannels{0};
    std::vector<uint8_t> gray;

    /// The pixel's level, 0 (black) to 255 (white), as an int so a failed check prints a number.
    int at(int row, int column) const
    {
        return gray[static_cast<size_t>(row) * static_cast<size_t>(width) +
                    static_cast<size_t>(column)];
    }

    /// The row of the brightest pixel in `column` (the topmost on a tie).
    int brightestRow(int column) const
    {
        int best = 0;
        for (int row = 1; row < height; ++row)
        {
            if (at(row, column) > at(best, column))
            {
                best = row;
            }
        }
        return best;
    }
};

/// Reads the PNG at `path` (opened as a std::filesystem::path, so a non-ASCII name works on
/// Windows too) and decodes it with stb_image.
Image readPng(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(in), {}};
    REQUIRE(bytes.size() > 26);
    // IHDR's bit depth and colour type: 8-bit grayscale (type 0), so no palette or RGB to undo.
    CHECK(bytes[24] == 8);
    CHECK(bytes[25] == 0);

    Image image;
    const auto deleter = [](stbi_uc *p) { stbi_image_free(p); };
    const std::unique_ptr<stbi_uc, decltype(deleter)> pixels(
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &image.width,
                              &image.height, &image.fileChannels, 1),
        deleter);
    if (pixels == nullptr)
    {
        FAIL("stb_image can't decode it: " << stbi_failure_reason());
    }
    image.gray.assign(pixels.get(), pixels.get() + static_cast<size_t>(image.width) *
                                                       static_cast<size_t>(image.height));
    return image;
}
} // namespace

TEST_CASE("spectrogram png is grayscale with low tones at the bottom and time to the right",
          "[metrics]")
{
    // One second at 48 kHz: 91 frames. A low tone (bin 64, 1500 Hz) in the left channel from the
    // first sample, and a high tone (bin 768, 18 kHz) in the right from the middle on. The mono
    // mix halves each, so each reads 20·log10(0.25) = -12.04 dB.
    const size_t frames = 48000;
    const AudioData audio{kSampleRate,
                          {toneFrom(0, 64, 0.5, frames), toneFrom(frames / 2, 768, 0.5, frames)}};
    const fs::path path = scratch("orientation.png");
    REQUIRE(gg::metrics::writeSpectrogramPng(utf8(path), audio));

    const Image image = readPng(path);
    REQUIRE(image.fileChannels == 1);
    REQUIRE(image.width == 91);
    REQUIRE(image.height == kHeight);

    // The level mapping: clamp((dB + 120) / 120, 0, 1) × 255, so -12.04 dB is 229.4, rounded to
    // 229. A column well inside each half, clear of the frame that straddles the high tone's
    // onset (frames start every 512 samples and are 2048 long).
    const int early = 10;
    const int late = 80;
    const int lowRow = kHeight - 64;
    const int highRow = kHeight - 768;
    CHECK(image.brightestRow(early) == lowRow);
    CHECK(image.at(lowRow, early) == 229);
    // Low frequencies at the bottom, high at the top: the high tone lights an upper row.
    CHECK(highRow < lowRow);
    CHECK(image.at(highRow, late) == 229);
    CHECK(image.at(lowRow, late) == 229);

    // Time runs left to right: before the high tone starts its row is black (below -120 dB; a
    // Hann window's leakage from bin 64 doesn't reach bin 768).
    CHECK(image.at(highRow, early) == 0);
    CHECK(image.at(highRow, 0) == 0);

    // Brightness falls off the peak: a row 40 bins away is far darker than the tone's.
    CHECK(image.at(lowRow - 40, early) < 64);
}

TEST_CASE("spectrogram png writes to a non-ASCII UTF-8 path", "[metrics]")
{
    const fs::path path = scratch(fs::path(u8"spéctrogram ✓ 音.png"));
    fs::remove(path);
    const AudioData audio{kSampleRate, {toneFrom(0, 64, 0.5, 4096)}};
    REQUIRE(gg::metrics::writeSpectrogramPng(utf8(path), audio));
    REQUIRE(fs::exists(path));

    const Image image = readPng(path);
    CHECK(image.fileChannels == 1);
    CHECK(image.height == kHeight);
    CHECK(image.brightestRow(0) == kHeight - 64);
}
