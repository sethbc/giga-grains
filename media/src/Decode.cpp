// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/Decode.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <new>
#include <system_error>
#include <utility>
#include <vector>

#include "Utf8Path.h"

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
/// The frames each channel of a growing decode's blocks holds.
constexpr uint64_t kBlockFrames = 65536;
/// How many bytes of decoded audio a compressed file's header may claim for each byte of the
/// file, for the buffer to be allocated at the header's count before any audio is decoded.
/// Ordinary audio decodes to well under it (FLAC to 2 to 4, ADPCM to 8, a 128 kb/s stereo MP3 to
/// 22); a header claiming more, like a file of near-silence (a FLAC of constant subframes), has
/// to prove its length by decoding.
constexpr uint64_t kVouchedBytesPerFileByte = 64;
/// How many bytes an uncompressed WAV's or AIFF's declared audio may run past the end of the file
/// without counting as truncated (ruling R10): some writers leave the data size a little larger
/// than what they wrote.
constexpr uint64_t kDataSizeSlack = 1024;

DecodeResult failure(const fs::path &path, const std::string &reason)
{
    return {std::nullopt, detail::displayName(path) + ": " + reason};
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

std::string truncatedBytes(uint64_t declared, uint64_t held)
{
    return "the file is truncated: its header gives " + std::to_string(declared) +
           " bytes of audio, but it holds only " + std::to_string(held);
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

/// The format checks every format shares, before anything is allocated: channels and rate.
std::optional<DecodeResult> checkFormat(const fs::path &path, const Stream &stream)
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
    return std::nullopt;
}

/// The header's duration, checked against kMaxFileSeconds. The rate must have passed checkFormat.
std::optional<DecodeResult> checkLength(const fs::path &path, const Stream &stream)
{
    if (stream.frames > maxFrames(stream.sampleRate))
    {
        return failure(path, tooLong(double(stream.frames) / double(stream.sampleRate)));
    }
    return std::nullopt;
}

/// checkFormat, then checkLength.
std::optional<DecodeResult> checkHeader(const fs::path &path, const Stream &stream)
{
    if (auto bad = checkFormat(path, stream))
    {
        return bad;
    }
    return checkLength(path, stream);
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
/// small the file.
/// - When the header's count is no more than the file's size vouches for
///   (kVouchedBytesPerFileByte), the audio is decoded straight into one vector of that many
///   frames, which the buffer then adopts: an honest file's audio is held once.
/// - Frames beyond it, or all of them when the header gives no count or claims more than the
///   file vouches for, go into blocks of kBlockFrames allocated as they arrive, which are then
///   copied into one vector: the peak is about twice the audio that decoded, whatever the header
///   claims.
template <typename Read>
DecodeResult decodeCompressed(const fs::path &path, const Stream &stream, uint64_t fileBytes,
                              Read read)
{
    const uint32_t kept = std::min<uint32_t>(stream.channels, 2);
    const uint64_t limit = maxFrames(stream.sampleRate);
    // stream.frames passed checkLength, so this can't overflow.
    const uint64_t headFrames =
        stream.frames * kept * sizeof(float) / kVouchedBytesPerFileByte <= fileBytes ? stream.frames
                                                                                     : 0;
    // Channel c's frames at c × headFrames, as SampleBuffer lays them out; and the blocks, channel
    // c's at c × kBlockFrames.
    std::vector<float> head(kept * headFrames);
    std::vector<std::vector<float>> blocks;
    const auto take = [&](const float *in, uint64_t at, uint64_t frames) {
        while (frames > 0)
        {
            float *base = nullptr;
            uint64_t stride = 0;
            uint64_t offset = 0;
            if (at < headFrames)
            {
                base = head.data();
                stride = headFrames;
                offset = at;
            }
            else
            {
                const uint64_t index = (at - headFrames) / kBlockFrames;
                if (index == blocks.size())
                {
                    blocks.emplace_back(kept * kBlockFrames);
                }
                base = blocks[index].data();
                stride = kBlockFrames;
                offset = (at - headFrames) % kBlockFrames;
            }
            const uint64_t run = std::min(frames, stride - offset);
            for (uint32_t c = 0; c < kept; ++c)
            {
                float *out = base + c * stride + offset;
                for (uint64_t n = 0; n < run; ++n)
                {
                    out[n] = in[n * stream.channels + c];
                }
            }
            in += run * stream.channels;
            at += run;
            frames -= run;
        }
    };
    // One frame past the limit, to tell a file of exactly the limit from a longer one.
    const uint64_t done = readAll(stream, limit + 1, read, take);
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
    std::vector<float> samples;
    if (done <= headFrames)
    {
        // Within the header's count: the channels close up to `done` apart, in place, and the
        // vector shrinks without reallocating.
        for (uint32_t c = 1; c < kept; ++c)
        {
            std::memmove(head.data() + c * done, head.data() + c * headFrames,
                         done * sizeof(float));
        }
        head.resize(kept * done);
        samples = std::move(head);
    }
    else
    {
        samples.resize(kept * done);
        for (uint32_t c = 0; c < kept; ++c)
        {
            float *out = samples.data() + c * done;
            std::copy_n(head.data() + c * headFrames, headFrames, out);
            for (size_t b = 0; b < blocks.size(); ++b)
            {
                const uint64_t at = headFrames + b * kBlockFrames;
                std::copy_n(blocks[b].data() + c * kBlockFrames, std::min(kBlockFrames, done - at),
                            out + at);
            }
        }
    }
    auto buffer = std::make_shared<SampleBuffer>(kept, static_cast<int64_t>(done),
                                                 double(stream.sampleRate), std::move(samples));
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

/// A file opened for reading by its native path: wide on Windows (where a narrow one would go
/// through the ANSI code page), the path's own bytes elsewhere. It is opened once per decode, and
/// dr_wav, dr_flac and dr_mp3 each read it in turn through the same read, seek and tell callbacks
/// (the three libraries' callback types coincide, and so do their seek origins).
///
/// The seek callback refuses any position before the start or past the end of the file. dr_wav
/// skips a chunk by seeking forward in steps of at most 2^31 − 1 bytes, so a chunk size that
/// wraps (an RF64 ds64 or W64 chunk declaring less than its own header) or is merely huge would
/// take billions of steps if, as with fseek, every seek past the end succeeded: refused, the
/// first step fails and so does the open.
class InputFile
{
  public:
    explicit InputFile(const fs::path &path)
    {
#if defined(_WIN32)
        file_ = _wfopen(path.c_str(), L"rb");
#else
        file_ = std::fopen(path.c_str(), "rb");
#endif
        if (file_ != nullptr && (!seekTo(0, SEEK_END) || !tellNow(size_) || !seekTo(0, SEEK_SET)))
        {
            std::fclose(file_);
            file_ = nullptr;
        }
    }
    ~InputFile()
    {
        if (file_ != nullptr)
        {
            std::fclose(file_);
        }
    }
    InputFile(const InputFile &) = delete;
    InputFile &operator=(const InputFile &) = delete;

    explicit operator bool() const noexcept { return file_ != nullptr; }
    /// The file's size when it was opened, in bytes.
    uint64_t size() const noexcept { return size_; }

    /// Back to the start, for the next decoder to try.
    bool rewind()
    {
        if (!seekTo(0, SEEK_SET))
        {
            return false;
        }
        position_ = 0;
        return true;
    }

    static size_t onRead(void *user, void *out, size_t bytes)
    {
        auto &self = *static_cast<InputFile *>(user);
        const size_t got = std::fread(out, 1, bytes, self.file_);
        self.position_ += got;
        return got;
    }
    /// Origin is drwav_seek_origin, drflac_seek_origin or drmp3_seek_origin.
    template <typename Origin> static uint32_t onSeek(void *user, int offset, Origin origin)
    {
        auto &self = *static_cast<InputFile *>(user);
        const int64_t base = int(origin) == DRWAV_SEEK_SET   ? 0
                             : int(origin) == DRWAV_SEEK_CUR ? int64_t(self.position_)
                                                             : int64_t(self.size_);
        const int64_t target = base + offset;
        if (target < 0 || uint64_t(target) > self.size_ || !self.seekTo(target, SEEK_SET))
        {
            return 0;
        }
        self.position_ = uint64_t(target);
        return 1;
    }
    static uint32_t onTell(void *user, long long *cursor)
    {
        *cursor = static_cast<long long>(static_cast<InputFile *>(user)->position_);
        return 1;
    }

  private:
    bool seekTo(int64_t offset, int whence)
    {
#if defined(_WIN32)
        return _fseeki64(file_, offset, whence) == 0;
#else
        return fseeko(file_, off_t(offset), whence) == 0;
#endif
    }
    bool tellNow(uint64_t &out)
    {
#if defined(_WIN32)
        const int64_t at = _ftelli64(file_);
#else
        const int64_t at = ftello(file_);
#endif
        if (at < 0)
        {
            return false;
        }
        out = uint64_t(at);
        return true;
    }

    std::FILE *file_{nullptr};
    uint64_t size_{0};
    uint64_t position_{0};
};

static_assert(int(DRWAV_SEEK_SET) == int(DRFLAC_SEEK_SET) &&
              int(DRWAV_SEEK_SET) == int(DRMP3_SEEK_SET) &&
              int(DRWAV_SEEK_CUR) == int(DRFLAC_SEEK_CUR) &&
              int(DRWAV_SEEK_CUR) == int(DRMP3_SEEK_CUR) &&
              int(DRWAV_SEEK_END) == int(DRFLAC_SEEK_END) &&
              int(DRWAV_SEEK_END) == int(DRMP3_SEEK_END));

/// The size a RIFF, RIFX or W64 file's data chunk declares, which dr_wav clamps to the file
/// before deriving its frame count, so truncation would go unnoticed. RF64 declares it in its
/// ds64 chunk, whose sample count dr_wav keeps, and AIFF in its COMM frame count, likewise.
struct DeclaredData
{
    uint64_t bytes{0};
    /// Whether a RIFF or RIFX data chunk declared `bytes` (0 included).
    bool riff{false};
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
        auto &declared = *static_cast<DeclaredData *>(user);
        declared.bytes = header->sizeInBytes;
        declared.riff = riffData;
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

std::optional<DecodeResult> tryWav(InputFile &file, const fs::path &path)
{
    drwav wav;
    DeclaredData declared;
    if (!drwav_init_ex(&wav, InputFile::onRead, InputFile::onSeek<drwav_seek_origin>,
                       InputFile::onTell, onWavChunk, &file, &declared, 0, nullptr))
    {
        return std::nullopt;
    }
    const uint64_t fileBytes = file.size();
    // The bytes from the start of the audio to the end of the file.
    const uint64_t heldBytes =
        fileBytes > wav.dataChunkDataPos ? fileBytes - wav.dataChunkDataPos : 0;
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
        if (auto bad = checkFormat(path, stream))
        {
            return std::move(*bad);
        }
        if (adpcm)
        {
            // dr_wav clamps the data size to the file and, without a fact chunk, counts the
            // frames in what is left, so a short file would read as a short sound. Up to one
            // block missing is the shortfall the decode allows anyway.
            if (declared.bytes > heldBytes && declared.bytes - heldBytes > wav.fmt.blockAlign)
            {
                return failure(path, truncatedBytes(declared.bytes, heldBytes));
            }
            if (auto bad = checkLength(path, stream))
            {
                return std::move(*bad);
            }
            return guarded(path, [&] { return decodeCompressed(path, stream, fileBytes, read); });
        }
        // A data size of 0 with audio after it is a recording whose header was never finished:
        // dr_wav reads it as no frames at all.
        if (declared.riff && declared.bytes == 0 && heldBytes >= bytesPerFrame)
        {
            return failure(path, "the file is truncated: its header gives 0 bytes of audio, as a "
                                 "recording never finished leaves it, but " +
                                     std::to_string(heldBytes) + " bytes follow");
        }
        // What the header declares, checked against what the file holds before its length is:
        // a recording never finalized declares a placeholder size, and is truncated, not long.
        stream.frames = std::max<uint64_t>(stream.frames, declared.bytes / bytesPerFrame);
        const uint64_t held = heldBytes / bytesPerFrame;
        if (stream.frames > held)
        {
            // Within kDataSizeSlack the file is read as it is (ruling R10). The bytes missing are
            // missing × bytesPerFrame − heldBytes % bytesPerFrame, computed only when `missing`
            // is small enough not to overflow: a larger count is beyond the slack anyway.
            const uint64_t missing = stream.frames - held;
            if (missing > kDataSizeSlack / bytesPerFrame + 1 ||
                missing * bytesPerFrame - heldBytes % bytesPerFrame > kDataSizeSlack)
            {
                return failure(path, truncated(stream.frames, held));
            }
            stream.frames = held;
        }
        if (auto bad = checkLength(path, stream))
        {
            return std::move(*bad);
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

std::optional<DecodeResult> tryFlac(InputFile &file, const fs::path &path)
{
    drflac *flac = drflac_open(InputFile::onRead, InputFile::onSeek<drflac_seek_origin>,
                               InputFile::onTell, &file, nullptr);
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
            return decodeCompressed(path, stream, file.size(), [&](uint64_t frames, float *out) {
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

std::optional<DecodeResult> tryMp3(InputFile &file, const fs::path &path)
{
    auto mp3 = std::make_unique<drmp3>(); // large decoder state: keep it off the stack
    if (!drmp3_init(mp3.get(), InputFile::onRead, InputFile::onSeek<drmp3_seek_origin>,
                    InputFile::onTell, nullptr, &file, nullptr))
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
            return decodeCompressed(path, stream, file.size(), [&](uint64_t frames, float *out) {
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
    InputFile file(path);
    if (!file)
    {
        return failure(path, "the file can't be opened");
    }

    // By content, not extension. dr_wav and dr_flac check their headers; MP3 decoding is the most
    // permissive, so it goes last. Each starts from the beginning of the file.
    using Try = std::optional<DecodeResult> (*)(InputFile &, const fs::path &);
    for (const Try attempt : {Try(tryWav), Try(tryFlac), Try(tryMp3)})
    {
        if (!file.rewind())
        {
            return failure(path, "the file can't be read");
        }
        if (auto result = attempt(file, path))
        {
            return std::move(*result);
        }
    }
    return failure(path, "not a WAV, AIFF, FLAC or MP3 file, or one in an encoding that can't be "
                         "read");
}
} // namespace gg::media
