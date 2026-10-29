// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/metrics/Spectrogram.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

// The header only: the implementation is compiled in gg::stb_impl.
#include <stb_image_write.h>

#include "Analysis.h"

namespace gg::metrics
{
namespace
{
constexpr size_t kHop = 512;
constexpr size_t kHeight = 1024;
constexpr double kRangeDb = 120.0;

/// stbi_write_func: appends the encoder's bytes to the std::vector<uint8_t> at `context`.
void appendBytes(void *context, void *data, int size)
{
    auto &bytes = *static_cast<std::vector<uint8_t> *>(context);
    const auto *begin = static_cast<const uint8_t *>(data);
    bytes.insert(bytes.end(), begin, begin + size);
}

/// Writes `bytes` to the file at the UTF-8 `path`. The stream is opened from a
/// std::filesystem::path made from the UTF-8 text, so on Windows a non-ASCII name opens through
/// the wide API, not the ANSI code page (carry-over [181]); text that isn't valid UTF-8 there
/// fails. Elsewhere the bytes are the path.
bool writeFile(const std::string &path, const std::vector<uint8_t> &bytes)
{
    std::filesystem::path file;
    try
    {
        file = std::filesystem::path(std::u8string(path.begin(), path.end()));
    }
    catch (const std::system_error &)
    {
        return false;
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    return !out.fail();
}
} // namespace

bool writeSpectrogramPng(const std::string &path, const AudioData &audio)
{
    using detail::Stft;
    static_assert(kHeight == Stft::kBins - 1, "one row per bin, DC left out");

    const std::vector<double> mono = detail::monoMix(audio);
    const size_t frames = Stft::frameCount(mono.size(), kHop);
    const size_t width = std::max<size_t>(frames, 1);
    if (width > static_cast<size_t>(std::numeric_limits<int>::max()) / kHeight)
    {
        return false;
    }

    std::vector<uint8_t> pixels(width * kHeight, 0);
    Stft stft;
    // A sine of amplitude A on a bin below Nyquist has |X| = A · Σw / 2.
    const double amplitudeScale = 2.0 / stft.windowSum();
    std::vector<double> power;
    for (size_t f = 0; f < frames; ++f)
    {
        stft.powerSpectrum(mono, f * kHop, power);
        for (size_t row = 0; row < kHeight; ++row)
        {
            const size_t k = Stft::kBins - 1 - row; // row 0 is Nyquist, the last row is bin 1
            const double scale = k == Stft::kBins - 1 ? amplitudeScale / 2.0 : amplitudeScale;
            const double amplitudeSquared = power[k] * scale * scale;
            double level = 0.0;
            if (amplitudeSquared > 0.0)
            {
                const double db = 10.0 * std::log10(amplitudeSquared);
                level = std::clamp((db + kRangeDb) / kRangeDb, 0.0, 1.0);
            }
            pixels[row * width + f] = static_cast<uint8_t>(std::lround(level * 255.0));
        }
    }

    // Encoded in memory, then written by writeFile: stbi_write_png would open the path narrow.
    const int w = static_cast<int>(width);
    std::vector<uint8_t> png;
    if (stbi_write_png_to_func(appendBytes, &png, w, static_cast<int>(kHeight), 1, pixels.data(),
                               w) == 0)
    {
        return false;
    }
    return writeFile(path, png);
}
} // namespace gg::metrics
