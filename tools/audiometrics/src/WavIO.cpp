// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/metrics/WavIO.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

// The headers only: the implementations are compiled in gg::dr_libs_impl.
#include <dr_flac.h>
#include <dr_mp3.h>
#include <dr_wav.h>

namespace gg::metrics
{
namespace
{
constexpr uint64_t kChunkFrames = 4096;

/// Reads interleaved float frames with `read(frames, buffer) -> framesRead` until it returns 0,
/// and deinterleaves them.
template <typename Read> AudioData readAll(uint32_t channels, uint32_t sampleRate, Read read)
{
    AudioData audio{static_cast<double>(sampleRate), std::vector<std::vector<float>>(channels)};
    std::vector<float> interleaved(kChunkFrames * channels);
    for (;;)
    {
        const uint64_t frames =
            std::min<uint64_t>(read(kChunkFrames, interleaved.data()), kChunkFrames);
        if (frames == 0)
        {
            break;
        }
        for (uint32_t c = 0; c < channels; ++c)
        {
            auto &channel = audio.channels[c];
            for (uint64_t n = 0; n < frames; ++n)
            {
                channel.push_back(interleaved[n * channels + c]);
            }
        }
    }
    return audio;
}

std::optional<AudioData> readWav(const std::string &path)
{
    drwav wav;
    if (!drwav_init_file(&wav, path.c_str(), nullptr))
    {
        return std::nullopt;
    }
    std::optional<AudioData> audio;
    if (wav.channels > 0 && wav.sampleRate > 0)
    {
        audio = readAll(wav.channels, wav.sampleRate, [&](uint64_t frames, float *out) {
            return drwav_read_pcm_frames_f32(&wav, frames, out);
        });
    }
    drwav_uninit(&wav);
    return audio;
}

std::optional<AudioData> readFlac(const std::string &path)
{
    drflac *flac = drflac_open_file(path.c_str(), nullptr);
    if (!flac)
    {
        return std::nullopt;
    }
    std::optional<AudioData> audio;
    if (flac->channels > 0 && flac->sampleRate > 0)
    {
        audio = readAll(flac->channels, flac->sampleRate, [&](uint64_t frames, float *out) {
            return drflac_read_pcm_frames_f32(flac, frames, out);
        });
    }
    drflac_close(flac);
    return audio;
}

std::optional<AudioData> readMp3(const std::string &path)
{
    auto mp3 = std::make_unique<drmp3>(); // large decoder state: keep it off the stack
    if (!drmp3_init_file(mp3.get(), path.c_str(), nullptr))
    {
        return std::nullopt;
    }
    std::optional<AudioData> audio;
    if (mp3->channels > 0 && mp3->sampleRate > 0)
    {
        audio = readAll(mp3->channels, mp3->sampleRate, [&](uint64_t frames, float *out) {
            return drmp3_read_pcm_frames_f32(mp3.get(), frames, out);
        });
    }
    drmp3_uninit(mp3.get());
    return audio;
}
} // namespace

std::optional<AudioData> readAudioFile(const std::string &path)
{
    // WAV and FLAC check their headers; MP3 decoding is the most permissive, so it goes last.
    if (auto audio = readWav(path))
    {
        return audio;
    }
    if (auto audio = readFlac(path))
    {
        return audio;
    }
    return readMp3(path);
}

bool writeWavFloat32(const std::string &path, const AudioData &audio)
{
    if (audio.channels.empty() || audio.channels.size() > std::numeric_limits<uint16_t>::max())
    {
        return false;
    }
    const size_t frames = audio.channels.front().size();
    for (const auto &channel : audio.channels)
    {
        if (channel.size() != frames)
        {
            return false;
        }
    }
    const double rate = audio.sampleRate;
    if (!(rate >= 1.0 && rate <= std::numeric_limits<uint32_t>::max() && rate == std::floor(rate)))
    {
        return false;
    }
    const auto channels = static_cast<uint32_t>(audio.channels.size());
    // RIFF sizes are 32-bit; leave room for the headers.
    const uint64_t dataBytes = static_cast<uint64_t>(frames) * channels * sizeof(float);
    if (dataBytes > std::numeric_limits<uint32_t>::max() - 1024u)
    {
        return false;
    }

    drwav_data_format format{};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = channels;
    format.sampleRate = static_cast<uint32_t>(rate);
    format.bitsPerSample = 32;

    drwav wav;
    if (!drwav_init_file_write(&wav, path.c_str(), &format, nullptr))
    {
        return false;
    }
    bool ok = true;
    std::vector<float> interleaved(kChunkFrames * channels);
    for (size_t start = 0; ok && start < frames; start += kChunkFrames)
    {
        const size_t count = std::min<size_t>(kChunkFrames, frames - start);
        for (size_t n = 0; n < count; ++n)
        {
            for (uint32_t c = 0; c < channels; ++c)
            {
                interleaved[n * channels + c] = audio.channels[c][start + n];
            }
        }
        ok = drwav_write_pcm_frames(&wav, count, interleaved.data()) == count;
    }
    // drwav_uninit writes the final chunk sizes and closes the file; it runs even after a failed
    // write.
    const bool closed = drwav_uninit(&wav) == DRWAV_SUCCESS;
    return ok && closed;
}
} // namespace gg::metrics
