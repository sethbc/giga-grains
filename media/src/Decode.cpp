// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/Decode.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <fstream>
#include <new>
#include <system_error>
#include <utility>
#include <vector>

// The headers only: the implementations are compiled in gg::dr_libs_impl.
#include <dr_flac.h>
#include <dr_mp3.h>
#include <dr_wav.h>

namespace gg::media
{
namespace
{
namespace fs = std::filesystem;

/// The highest sample rate accepted, as gg-render's: it bounds the buffer a file can ask for.
constexpr uint32_t kMaxSampleRate = 768000;
/// Samples decoded per read, through an interleaved scratch buffer: 4096 stereo frames, and at
/// least one frame however many channels a file has (a WAV may claim 65535).
constexpr uint64_t kChunkSamples = 8192;

/// The file's name, for messages: UTF-8, without its directory.
std::string displayName(const fs::path &path)
{
    const auto name = path.filename().u8string();
    return {name.begin(), name.end()};
}

DecodeResult failure(const fs::path &path, const std::string &reason)
{
    return {std::nullopt, displayName(path) + ": " + reason};
}

/// What decode() needs from an opened file.
struct Stream
{
    const char *format;
    uint32_t channels;
    uint32_t sampleRate;
    uint64_t frames; ///< from the header
};

/// Checks `stream`'s header, allocates the buffer and fills it with `read(frames, interleaved) ->
/// frames read`, keeping the first two channels.
template <typename Read> DecodeResult decode(const fs::path &path, const Stream &stream, Read read)
{
    if (stream.channels == 0)
    {
        return failure(path, "the file has no channels");
    }
    if (stream.sampleRate == 0 || stream.sampleRate > kMaxSampleRate)
    {
        return failure(path, "unsupported sample rate of " + std::to_string(stream.sampleRate) +
                                 " Hz (the highest is 768000 Hz)");
    }
    if (stream.frames == 0)
    {
        return failure(path, "the file has no audio");
    }
    // Before allocating: the header's frame count alone decides.
    const double seconds = double(stream.frames) / double(stream.sampleRate);
    if (seconds > kMaxFileSeconds)
    {
        char text[96];
        std::snprintf(text, sizeof text, "the file is %.2f s long; the longest allowed is %.0f s",
                      seconds, kMaxFileSeconds);
        return failure(path, text);
    }

    const uint32_t kept = std::min<uint32_t>(stream.channels, 2);
    try
    {
        auto buffer = std::make_shared<SampleBuffer>(kept, static_cast<int64_t>(stream.frames),
                                                     double(stream.sampleRate));
        const uint64_t chunkFrames = std::max<uint64_t>(1, kChunkSamples / stream.channels);
        std::vector<float> interleaved(chunkFrames * stream.channels);
        uint64_t done = 0;
        while (done < stream.frames)
        {
            const uint64_t want = std::min(chunkFrames, stream.frames - done);
            const uint64_t got = std::min<uint64_t>(read(want, interleaved.data()), want);
            if (got == 0)
            {
                break;
            }
            for (uint32_t c = 0; c < kept; ++c)
            {
                float *out = buffer->channel(c) + done;
                for (uint64_t n = 0; n < got; ++n)
                {
                    out[n] = interleaved[n * stream.channels + c];
                }
            }
            done += got;
        }
        if (done == 0)
        {
            return failure(path, "no audio could be decoded from the file");
        }
        if (done < stream.frames)
        {
            // The file ends early, as an interrupted recording does: keep what it holds.
            auto shorter = std::make_shared<SampleBuffer>(kept, static_cast<int64_t>(done),
                                                          double(stream.sampleRate));
            for (uint32_t c = 0; c < kept; ++c)
            {
                std::copy_n(buffer->channel(c), done, shorter->channel(c));
            }
            buffer = std::move(shorter);
        }
        return {DecodedAudio{std::move(buffer), stream.format, stream.channels}, {}};
    }
    catch (const std::bad_alloc &)
    {
        return failure(path, "not enough memory to load the file");
    }
    catch (const std::exception &e)
    {
        return failure(path, e.what());
    }
}

// The open functions: dr_libs' wide-character entry points on Windows, where a narrow path
// would go through the ANSI code page; UTF-8 elsewhere.
#if defined(_WIN32)
bool openWav(drwav &wav, const fs::path &path)
{
    return drwav_init_file_w(&wav, path.c_str(), nullptr);
}
drflac *openFlac(const fs::path &path) { return drflac_open_file_w(path.c_str(), nullptr); }
bool openMp3(drmp3 &mp3, const fs::path &path)
{
    return drmp3_init_file_w(&mp3, path.c_str(), nullptr);
}
#else
std::string utf8(const fs::path &path)
{
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}
bool openWav(drwav &wav, const fs::path &path)
{
    return drwav_init_file(&wav, utf8(path).c_str(), nullptr);
}
drflac *openFlac(const fs::path &path) { return drflac_open_file(utf8(path).c_str(), nullptr); }
bool openMp3(drmp3 &mp3, const fs::path &path)
{
    return drmp3_init_file(&mp3, utf8(path).c_str(), nullptr);
}
#endif

std::optional<DecodeResult> tryWav(const fs::path &path)
{
    drwav wav;
    if (!openWav(wav, path))
    {
        return std::nullopt;
    }
    const Stream stream{wav.container == drwav_container_aiff ? "aiff" : "wav", wav.channels,
                        wav.sampleRate, wav.totalPCMFrameCount};
    auto result = decode(path, stream, [&](uint64_t frames, float *out) {
        return drwav_read_pcm_frames_f32(&wav, frames, out);
    });
    drwav_uninit(&wav);
    return result;
}

std::optional<DecodeResult> tryFlac(const fs::path &path)
{
    drflac *flac = openFlac(path);
    if (!flac)
    {
        return std::nullopt;
    }
    const Stream stream{"flac", flac->channels, flac->sampleRate, flac->totalPCMFrameCount};
    auto result = decode(path, stream, [&](uint64_t frames, float *out) {
        return drflac_read_pcm_frames_f32(flac, frames, out);
    });
    drflac_close(flac);
    return result;
}

std::optional<DecodeResult> tryMp3(const fs::path &path)
{
    auto mp3 = std::make_unique<drmp3>(); // large decoder state: keep it off the stack
    if (!openMp3(*mp3, path))
    {
        return std::nullopt;
    }
    // drmp3_get_pcm_frame_count reads the Xing/Info tag or, without one, scans the frame headers,
    // and returns to the start; it never decodes the audio.
    const Stream stream{"mp3", mp3->channels, mp3->sampleRate,
                        drmp3_get_pcm_frame_count(mp3.get())};
    auto result = decode(path, stream, [&](uint64_t frames, float *out) {
        return drmp3_read_pcm_frames_f32(mp3.get(), frames, out);
    });
    drmp3_uninit(mp3.get());
    return result;
}
} // namespace

DecodeResult decodeAudioFile(const fs::path &path)
{
    std::error_code error;
    const auto status = fs::status(path, error);
    if (!fs::exists(status))
    {
        return failure(path, "the file doesn't exist");
    }
    if (!fs::is_regular_file(status))
    {
        return failure(path, "not a file");
    }
    if (fs::file_size(path, error) == 0 && !error)
    {
        return failure(path, "the file is empty");
    }
    if (!std::ifstream(path, std::ios::binary))
    {
        return failure(path, "the file can't be opened");
    }

    // By content, not extension. dr_wav and dr_flac check their headers; MP3 decoding is the most
    // permissive, so it goes last.
    if (auto result = tryWav(path))
    {
        return std::move(*result);
    }
    if (auto result = tryFlac(path))
    {
        return std::move(*result);
    }
    if (auto result = tryMp3(path))
    {
        return std::move(*result);
    }
    return failure(path, "not a WAV, AIFF, FLAC or MP3 file, or one in an encoding that can't be "
                         "read");
}
} // namespace gg::media
