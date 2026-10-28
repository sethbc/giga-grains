// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/Decode.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
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
/// A growing buffer's first capacity, in frames, unless the header gives fewer.
constexpr uint64_t kInitialFrames = 65536;

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

/// kMaxFileSeconds of audio at `rate`, in frames (exact: the rate is an integer).
uint64_t maxFrames(uint32_t rate) { return static_cast<uint64_t>(kMaxFileSeconds * rate); }

std::string tooLong(double seconds)
{
    char text[96];
    std::snprintf(text, sizeof text, "the file is %.2f s long; the longest allowed is %.0f s",
                  seconds, kMaxFileSeconds);
    return text;
}

std::string truncated(uint64_t declared, uint64_t held)
{
    return "the file is truncated: its header gives " + std::to_string(declared) +
           " frames, but it holds only " + std::to_string(held);
}

/// What decoding needs from an opened file.
struct Stream
{
    const char *format;
    uint32_t channels;
    uint32_t sampleRate;
    /// The header's frame count; 0 if it gives none (a FLAC may not).
    uint64_t frames;
    /// How many frames short of `frames` a decode may end without counting as truncated.
    uint64_t shortfall;
};

/// The checks every format shares, before anything is allocated: channels, rate and the
/// header's duration.
std::optional<DecodeResult> checkHeader(const fs::path &path, const Stream &stream)
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
    if (stream.frames > maxFrames(stream.sampleRate))
    {
        return failure(path, tooLong(double(stream.frames) / double(stream.sampleRate)));
    }
    return std::nullopt;
}

/// Reads with `read(frames, interleaved) -> frames read` until it returns 0 or `limit` frames
/// have come, handing each piece to `take(interleaved, frames)`. Returns the frames read.
template <typename Read, typename Take>
uint64_t readAll(const Stream &stream, uint64_t limit, Read read, Take take)
{
    const uint64_t chunkFrames = std::max<uint64_t>(1, kChunkSamples / stream.channels);
    std::vector<float> interleaved(chunkFrames * stream.channels);
    uint64_t done = 0;
    while (done < limit)
    {
        const uint64_t want = std::min(chunkFrames, limit - done);
        const uint64_t got = std::min<uint64_t>(read(want, interleaved.data()), want);
        if (got == 0)
        {
            break;
        }
        take(interleaved.data(), done, got);
        done += got;
    }
    return done;
}

/// Decodes a file whose frame count is bounded by its size (uncompressed WAV and AIFF, already
/// checked against it): the buffer is allocated once, at the header's count, and a decode that
/// ends short of it is truncation.
template <typename Read>
DecodeResult decodeBounded(const fs::path &path, const Stream &stream, Read read)
{
    const uint32_t kept = std::min<uint32_t>(stream.channels, 2);
    auto buffer = std::make_shared<SampleBuffer>(kept, static_cast<int64_t>(stream.frames),
                                                 double(stream.sampleRate));
    const uint64_t done =
        readAll(stream, stream.frames, read, [&](const float *in, uint64_t at, uint64_t frames) {
            for (uint32_t c = 0; c < kept; ++c)
            {
                float *out = buffer->channel(c) + at;
                for (uint64_t n = 0; n < frames; ++n)
                {
                    out[n] = in[n * stream.channels + c];
                }
            }
        });
    if (done < stream.frames)
    {
        return failure(path, truncated(stream.frames, done));
    }
    return {DecodedAudio{std::move(buffer), stream.format, stream.channels}, {}};
}

/// Decodes a compressed file (FLAC, MP3, ADPCM WAV), whose header can claim any length however
/// small the file: the planes grow as audio actually arrives, never past the header's count
/// while the decode is within it, so an honest file's planes end exactly full and a lying
/// header's within twice what decoded. Then they're copied into the buffer.
template <typename Read>
DecodeResult decodeGrowing(const fs::path &path, const Stream &stream, Read read)
{
    const uint32_t kept = std::min<uint32_t>(stream.channels, 2);
    const uint64_t limit = maxFrames(stream.sampleRate);
    std::vector<std::vector<float>> planes(kept);
    uint64_t capacity = 0;
    const auto reserve = [&](uint64_t needed) {
        if (needed <= capacity)
        {
            return;
        }
        uint64_t next = std::max(capacity == 0 ? kInitialFrames : 2 * capacity, needed);
        if (needed <= stream.frames)
        {
            next = std::min(next, stream.frames);
        }
        for (auto &plane : planes)
        {
            plane.reserve(next);
        }
        capacity = next;
    };
    // One frame past the limit, to tell a file of exactly the limit from a longer one.
    const uint64_t done =
        readAll(stream, limit + 1, read, [&](const float *in, uint64_t at, uint64_t frames) {
            reserve(at + frames);
            for (uint32_t c = 0; c < kept; ++c)
            {
                auto &plane = planes[c];
                plane.resize(at + frames);
                for (uint64_t n = 0; n < frames; ++n)
                {
                    plane[at + n] = in[n * stream.channels + c];
                }
            }
        });
    if (done > limit)
    {
        char text[96];
        std::snprintf(text, sizeof text,
                      "the file decodes to more than %.0f s, the longest allowed", kMaxFileSeconds);
        return failure(path, text);
    }
    if (stream.frames != 0 && done + stream.shortfall < stream.frames)
    {
        return failure(path, truncated(stream.frames, done));
    }
    if (done == 0)
    {
        return failure(path, "no audio could be decoded from the file");
    }
    auto buffer =
        std::make_shared<SampleBuffer>(kept, static_cast<int64_t>(done), double(stream.sampleRate));
    for (uint32_t c = 0; c < kept; ++c)
    {
        std::copy_n(planes[c].data(), done, buffer->channel(c));
        planes[c] = {};
    }
    return {DecodedAudio{std::move(buffer), stream.format, stream.channels}, {}};
}

/// Runs a decode, turning an allocation failure into a message.
template <typename Decode> DecodeResult guarded(const fs::path &path, Decode decode)
{
    try
    {
        return decode();
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
bool openWav(drwav &wav, const fs::path &path, drwav_chunk_proc onChunk, void *chunkData)
{
    return drwav_init_file_ex_w(&wav, path.c_str(), onChunk, chunkData, 0, nullptr);
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
bool openWav(drwav &wav, const fs::path &path, drwav_chunk_proc onChunk, void *chunkData)
{
    return drwav_init_file_ex(&wav, utf8(path).c_str(), onChunk, chunkData, 0, nullptr);
}
drflac *openFlac(const fs::path &path) { return drflac_open_file(utf8(path).c_str(), nullptr); }
bool openMp3(drmp3 &mp3, const fs::path &path)
{
    return drmp3_init_file(&mp3, utf8(path).c_str(), nullptr);
}
#endif

/// The size a RIFF, RIFX or W64 file's data chunk declares, which dr_wav clamps to the file
/// before deriving its frame count, so truncation would go unnoticed. RF64 declares it in its
/// ds64 chunk, whose sample count dr_wav keeps, and AIFF in its COMM frame count, likewise.
struct DeclaredData
{
    uint64_t bytes{0};
};

drwav_uint64 onWavChunk(void *user, drwav_read_proc, drwav_seek_proc, void *,
                        const drwav_chunk_header *header, drwav_container container,
                        const drwav_fmt *)
{
    // 61746164-ACF3-11D3-8CD1-00C04F8EDB8A, W64's data chunk.
    static constexpr drwav_uint8 kW64Data[16] = {0x64, 0x61, 0x74, 0x61, 0xF3, 0xAC, 0xD3, 0x11,
                                                 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A};
    const bool riffData =
        (container == drwav_container_riff || container == drwav_container_rifx) &&
        std::memcmp(header->id.fourcc, "data", 4) == 0 &&
        header->sizeInBytes != 0xFFFFFFFFu; // "unknown": the rest of the file
    const bool w64Data =
        container == drwav_container_w64 && std::memcmp(header->id.guid, kW64Data, 16) == 0;
    if (riffData || w64Data)
    {
        static_cast<DeclaredData *>(user)->bytes = header->sizeInBytes;
    }
    return 0;
}

/// Whether dr_wav can turn this encoding into float, and how many bytes a frame takes; 0 for
/// one it can't. ADPCM is variable-rate and handled apart.
uint32_t pcmBytesPerFrame(const drwav &wav)
{
    // dr_wav's own rule (drwav_get_bytes_per_pcm_frame): whole bytes from the bit depth, else
    // the block align.
    const uint32_t bytesPerFrame = (wav.bitsPerSample % 8 == 0)
                                       ? uint32_t(wav.bitsPerSample) * wav.channels / 8
                                       : wav.fmt.blockAlign;
    if (wav.channels == 0 || bytesPerFrame == 0 || bytesPerFrame % wav.channels != 0)
    {
        return 0;
    }
    const uint32_t bytesPerSample = bytesPerFrame / wav.channels;
    switch (wav.translatedFormatTag)
    {
    case DR_WAVE_FORMAT_PCM:
        return bytesPerSample <= 8 ? bytesPerFrame : 0; // dr_wav zeroes wider samples
    case DR_WAVE_FORMAT_IEEE_FLOAT:
        return bytesPerSample == 4 || bytesPerSample == 8 ? bytesPerFrame : 0;
    case DR_WAVE_FORMAT_ALAW:
    case DR_WAVE_FORMAT_MULAW:
        return bytesPerSample == 1 ? bytesPerFrame : 0;
    default:
        return 0;
    }
}

std::optional<DecodeResult> tryWav(const fs::path &path, uint64_t fileBytes)
{
    drwav wav;
    DeclaredData declared;
    if (!openWav(wav, path, onWavChunk, &declared))
    {
        return std::nullopt;
    }
    const bool aiff = wav.container == drwav_container_aiff;
    const bool adpcm = wav.translatedFormatTag == DR_WAVE_FORMAT_ADPCM ||
                       wav.translatedFormatTag == DR_WAVE_FORMAT_DVI_ADPCM;
    // ADPCM's last block may hold fewer frames than its count: allow one block, whose frames
    // blockAlign × 2 bounds (at most two per byte).
    Stream stream{aiff ? "aiff" : "wav", wav.channels, wav.sampleRate, wav.totalPCMFrameCount,
                  adpcm ? uint64_t(wav.fmt.blockAlign) * 2 : 0};
    const auto read = [&](uint64_t frames, float *out) {
        return drwav_read_pcm_frames_f32(&wav, frames, out);
    };

    const auto result = [&]() -> DecodeResult {
        const uint32_t bytesPerFrame = pcmBytesPerFrame(wav);
        if (wav.channels != 0 && !adpcm && bytesPerFrame == 0)
        {
            char text[96];
            std::snprintf(text, sizeof text, "unsupported %s encoding (format 0x%04X, %u bits)",
                          aiff ? "AIFF" : "WAV", unsigned(wav.translatedFormatTag),
                          unsigned(wav.bitsPerSample));
            return failure(path, text);
        }
        if (bytesPerFrame != 0)
        {
            // What the header declares, checked below against what the file can hold.
            stream.frames = std::max<uint64_t>(stream.frames, declared.bytes / bytesPerFrame);
        }
        if (auto bad = checkHeader(path, stream))
        {
            return std::move(*bad);
        }
        if (adpcm)
        {
            return guarded(path, [&] { return decodeGrowing(path, stream, read); });
        }
        const uint64_t held = fileBytes > wav.dataChunkDataPos
                                  ? (fileBytes - wav.dataChunkDataPos) / bytesPerFrame
                                  : 0;
        if (stream.frames > held)
        {
            return failure(path, truncated(stream.frames, held));
        }
        if (stream.frames == 0)
        {
            return failure(path, "the file has no audio");
        }
        return guarded(path, [&] { return decodeBounded(path, stream, read); });
    }();
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
    // STREAMINFO's count is exact, or 0 when the encoder didn't know it.
    const Stream stream{"flac", flac->channels, flac->sampleRate, flac->totalPCMFrameCount, 0};
    auto result = checkHeader(path, stream);
    if (!result)
    {
        result = guarded(path, [&] {
            return decodeGrowing(path, stream, [&](uint64_t frames, float *out) {
                return drflac_read_pcm_frames_f32(flac, frames, out);
            });
        });
    }
    drflac_close(flac);
    return result;
}

/// Two MP3 frames of 1152 samples: how far an MP3's decode may legitimately fall short of its
/// header's count (an encoder's delay and padding that its tag accounts for differently).
constexpr uint64_t kMp3Shortfall = 2 * 1152;

std::optional<DecodeResult> tryMp3(const fs::path &path)
{
    auto mp3 = std::make_unique<drmp3>(); // large decoder state: keep it off the stack
    if (!openMp3(*mp3, path))
    {
        return std::nullopt;
    }
    // drmp3_get_pcm_frame_count reads the Xing/Info tag or, without one, scans the frame headers,
    // and returns to the start; it never decodes the audio.
    const Stream stream{"mp3", mp3->channels, mp3->sampleRate, drmp3_get_pcm_frame_count(mp3.get()),
                        kMp3Shortfall};
    auto result = checkHeader(path, stream);
    if (!result)
    {
        result = guarded(path, [&] {
            return decodeGrowing(path, stream, [&](uint64_t frames, float *out) {
                return drmp3_read_pcm_frames_f32(mp3.get(), frames, out);
            });
        });
    }
    drmp3_uninit(mp3.get());
    return result;
}
} // namespace

DecodeResult decodeAudioFile(const fs::path &path)
{
    std::error_code error;
    const auto status = fs::status(path, error);
    if (status.type() == fs::file_type::not_found)
    {
        return failure(path, "the file doesn't exist");
    }
    if (error)
    {
        return failure(path, "the file can't be read: " + error.message());
    }
    if (!fs::is_regular_file(status))
    {
        return failure(path, "not a file");
    }
    const uint64_t fileBytes = fs::file_size(path, error);
    if (error)
    {
        return failure(path, "the file can't be read: " + error.message());
    }
    if (fileBytes == 0)
    {
        return failure(path, "the file is empty");
    }
    if (!std::ifstream(path, std::ios::binary))
    {
        return failure(path, "the file can't be opened");
    }

    // By content, not extension. dr_wav and dr_flac check their headers; MP3 decoding is the most
    // permissive, so it goes last.
    if (auto result = tryWav(path, fileBytes))
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
