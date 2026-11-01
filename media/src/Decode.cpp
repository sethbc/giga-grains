// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/Decode.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include "gg/media/Utf8Path.h"

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
/// Ordinary audio decodes to well under it (FLAC to 2 to 8, ADPCM to 8, a 128 kb/s stereo MP3 to
/// 22); a header claiming more, like a file of near-silence (a FLAC of constant subframes), has
/// to prove its length by decoding.
constexpr uint64_t kVouchedBytesPerFileByte = 64;
/// How many bytes an uncompressed WAV's or AIFF's declared audio may run past the end of the file
/// without counting as truncated (ruling R10): some writers leave the data size a little larger
/// than what they wrote.
constexpr uint64_t kDataSizeSlack = 1024;

/// What an error starts with: the file's name and ": ", or nothing for bytes in memory.
using Label = std::string;

DecodeResult failure(const Label &label, const std::string &reason)
{
    return {std::nullopt, label + reason};
}

constexpr uint64_t kMiB = uint64_t{1} << 20;

/// The planar float bytes `frames` frames of `kept` channels decode to, saturating at the
/// largest uint64_t rather than wrapping (checkFormat has refused 0 channels; 0 gives 0).
uint64_t planarBytes(uint64_t frames, uint32_t kept)
{
    const uint64_t frameBytes = uint64_t(kept) * sizeof(float);
    if (frameBytes == 0)
    {
        return 0;
    }
    return frames > std::numeric_limits<uint64_t>::max() / frameBytes
               ? std::numeric_limits<uint64_t>::max()
               : frames * frameBytes;
}

/// A refusal by the bound: the need rounded up to whole MiB, the bound down, so the need always
/// reads as the larger.
std::string overBound(uint64_t need, uint64_t bound)
{
    const uint64_t needMiB = need / kMiB + (need % kMiB != 0 ? 1 : 0);
    return "needs " + std::to_string(needMiB) + " MB, the memory cap allows " +
           std::to_string(bound / kMiB) + " MB";
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
    /// decodeAudioFile's cancel flag, or nullptr: readAll reads it before each chunk.
    const std::atomic<bool> *cancel;
    /// The most planar float bytes the audio may decode to.
    uint64_t maxBytes;

    /// The channels kept: at most two.
    uint32_t kept() const noexcept { return std::min<uint32_t>(channels, 2); }
};

/// What readAll throws when the cancel flag is set; guarded() makes it the decode's error.
struct Cancelled : std::exception
{
    const char *what() const noexcept override { return "cancelled"; }
};

/// The format checks every format shares, before anything is allocated: channels and rate.
std::optional<DecodeResult> checkFormat(const Label &label, const Stream &stream)
{
    if (stream.channels == 0)
    {
        return failure(label, "the file has no channels");
    }
    if (stream.sampleRate == 0 || stream.sampleRate > kMaxSampleRate)
    {
        return failure(label, "unsupported sample rate of " + std::to_string(stream.sampleRate) +
                                  " Hz (the highest is 768000 Hz)");
    }
    return std::nullopt;
}

/// The header's frame count, decoded as planar float, checked against the bound before anything
/// is allocated.
std::optional<DecodeResult> checkLength(const Label &label, const Stream &stream)
{
    const uint64_t need = planarBytes(stream.frames, stream.kept());
    if (need > stream.maxBytes)
    {
        return failure(label, overBound(need, stream.maxBytes));
    }
    return std::nullopt;
}

/// checkFormat, then checkLength.
std::optional<DecodeResult> checkHeader(const Label &label, const Stream &stream)
{
    if (auto bad = checkFormat(label, stream))
    {
        return bad;
    }
    return checkLength(label, stream);
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
        if (stream.cancel != nullptr && stream.cancel->load(std::memory_order_relaxed))
        {
            throw Cancelled{};
        }
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
DecodeResult decodeBounded(const Label &label, const Stream &stream, Read read)
{
    const uint32_t kept = stream.kept();
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
        return failure(label, truncated(stream.frames, done));
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
DecodeResult decodeCompressed(const Label &label, const Stream &stream, uint64_t fileBytes,
                              Read read)
{
    const uint32_t kept = stream.kept();
    // The most frames the bound allows.
    const uint64_t limit = stream.maxBytes / (uint64_t(kept) * sizeof(float));
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
        // How much more it would decode to is unknown: decoding on to find out could take
        // as long as the file's near-silence lasts.
        return failure(label, "decodes to more than the memory cap allows (" +
                                  std::to_string(stream.maxBytes / kMiB) + " MB)");
    }
    if (stream.frames != 0 && done + stream.shortfall < stream.frames)
    {
        return failure(label, truncated(stream.frames, done));
    }
    if (done == 0)
    {
        return failure(label, "no audio could be decoded from the file");
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
template <typename Decode> DecodeResult guarded(const Label &label, Decode decode)
{
    try
    {
        return decode();
    }
    catch (const std::bad_alloc &)
    {
        return failure(label, "not enough memory to load the file");
    }
    catch (const std::exception &e)
    {
        return failure(label, e.what());
    }
}

/// What the decoders read: a file opened for reading by its native path (wide on Windows, where
/// a narrow one would go through the ANSI code page; the path's own bytes elsewhere), or a file's
/// bytes in memory (decodeAudioBytes). It is opened once per decode, and dr_wav, dr_flac and
/// dr_mp3 each read it in turn through the same read, seek and tell callbacks (the three
/// libraries' callback types coincide, and so do their seek origins), whichever it is: the rules
/// are the decoders', never the input's.
///
/// The seek callback refuses any position before the start or past the end of the file. dr_wav
/// skips a chunk by seeking forward in steps of at most 2^31 − 1 bytes, so a chunk size that
/// wraps (an RF64 ds64 or W64 chunk declaring less than its own header) or is merely huge would
/// take billions of steps if, as with fseek, every seek past the end succeeded: refused, the
/// first step fails and so does the open.
class Input
{
  public:
    explicit Input(const fs::path &path)
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
    /// `bytes`, which must outlive the Input.
    explicit Input(std::string_view bytes) noexcept : memory_(bytes.data()), size_(bytes.size()) {}
    ~Input()
    {
        if (file_ != nullptr)
        {
            std::fclose(file_);
        }
    }
    Input(const Input &) = delete;
    Input &operator=(const Input &) = delete;

    explicit operator bool() const noexcept { return file_ != nullptr || memory_ != nullptr; }
    /// The file's size when it was opened, in bytes.
    uint64_t size() const noexcept { return size_; }
    /// Where the next read starts, in bytes from the start of the file.
    uint64_t position() const noexcept { return position_; }

    /// Reads exactly `bytes` bytes at `at` into `out`; false if they aren't all there. It moves
    /// the file position: harmless under a live dr_wav only because the callers use it where
    /// dr_wav reads nothing more (a data size of 0 leaves it no audio to read).
    bool readAt(uint64_t at, void *out, size_t bytes)
    {
        if (at > size_ || !seekTo(int64_t(at), SEEK_SET))
        {
            return false;
        }
        position_ = at;
        return onRead(this, out, bytes) == bytes;
    }

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
        auto &self = *static_cast<Input *>(user);
        size_t got = 0;
        if (self.memory_ != nullptr)
        {
            got = static_cast<size_t>(std::min<uint64_t>(bytes, self.size_ - self.position_));
            std::memcpy(out, self.memory_ + self.position_, got);
        }
        else
        {
            got = std::fread(out, 1, bytes, self.file_);
        }
        self.position_ += got;
        return got;
    }
    /// Origin is drwav_seek_origin, drflac_seek_origin or drmp3_seek_origin.
    template <typename Origin> static uint32_t onSeek(void *user, int offset, Origin origin)
    {
        auto &self = *static_cast<Input *>(user);
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
    /// drwav_int64, drflac_int64 and drmp3_int64 are one type (asserted below).
    static uint32_t onTell(void *user, drwav_int64 *cursor)
    {
        *cursor = static_cast<drwav_int64>(static_cast<Input *>(user)->position_);
        return 1;
    }

  private:
    /// Moves the file to `offset` from `whence`. In memory every position the callers ask for is
    /// within the bytes (they check), and the position is all there is to move.
    bool seekTo(int64_t offset, int whence)
    {
        if (memory_ != nullptr)
        {
            return true;
        }
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
    const char *memory_{nullptr}; // the bytes, when in memory; file_ is then null
    uint64_t size_{0};
    uint64_t position_{0};
};

static_assert(int(DRWAV_SEEK_SET) == int(DRFLAC_SEEK_SET) &&
              int(DRWAV_SEEK_SET) == int(DRMP3_SEEK_SET) &&
              int(DRWAV_SEEK_CUR) == int(DRFLAC_SEEK_CUR) &&
              int(DRWAV_SEEK_CUR) == int(DRMP3_SEEK_CUR) &&
              int(DRWAV_SEEK_END) == int(DRFLAC_SEEK_END) &&
              int(DRWAV_SEEK_END) == int(DRMP3_SEEK_END));
static_assert(std::is_same_v<drwav_int64, drflac_int64> &&
              std::is_same_v<drwav_int64, drmp3_int64>);

/// The size a RIFF, RIFX or W64 file's data chunk declares, which dr_wav clamps to the file
/// before deriving its frame count, so truncation would go unnoticed. RF64 declares it in its
/// ds64 chunk, whose sample count dr_wav keeps, and AIFF in its COMM frame count, likewise.
struct DeclaredData
{
    uint64_t bytes{0};
    /// Whether a RIFF or RIFX data chunk declared `bytes` (0 included).
    bool riff{false};
    /// Whether that container is RIFX, whose chunk sizes are big-endian.
    bool rifx{false};
    /// Where the data chunk's audio starts, in bytes from the start of the file.
    uint64_t start{0};
    /// The fmt chunk's block align when the data chunk was seen; 0 if none came before it.
    uint32_t blockAlign{0};
};

drwav_uint64 onWavChunk(void *user, drwav_read_proc, drwav_seek_proc, void *file,
                        const drwav_chunk_header *header, drwav_container container,
                        const drwav_fmt *fmt)
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
        declared.rifx = riffData && container == drwav_container_rifx;
        // dr_wav has just read the chunk's header: the file sits at its audio.
        declared.start = static_cast<Input *>(file)->position();
        declared.blockAlign = fmt != nullptr ? fmt->blockAlign : 0;
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

/// A RIFF data size of 0 with at least `minBytes` (a frame, or an ADPCM block) after the start
/// of the audio: a recording whose header was never finished, which dr_wav reads as no frames
/// (or, for ADPCM, refuses to open). Unless what follows reads as a chunk header whose chunk fits
/// in the file: then the file is an empty one with more chunks after its data, and nullopt.
std::optional<DecodeResult> unfinished(Input &file, const Label &label,
                                       const DeclaredData &declared, uint64_t minBytes)
{
    const uint64_t fileBytes = file.size();
    const uint64_t held = fileBytes > declared.start ? fileBytes - declared.start : 0;
    if (!declared.riff || declared.bytes != 0 || held < std::max<uint64_t>(minBytes, 1))
    {
        return std::nullopt;
    }
    uint8_t chunk[8];
    // readAt moves the file position; dr_wav reads nothing after this (its data size is 0).
    if (held >= 8 && file.readAt(declared.start, chunk, sizeof chunk))
    {
        const bool printableId =
            std::all_of(chunk, chunk + 4, [](uint8_t c) { return c >= 0x20 && c <= 0x7E; });
        const uint64_t size = declared.rifx
                                  ? uint64_t(chunk[7]) | uint64_t(chunk[6]) << 8 |
                                        uint64_t(chunk[5]) << 16 | uint64_t(chunk[4]) << 24
                                  : uint64_t(chunk[4]) | uint64_t(chunk[5]) << 8 |
                                        uint64_t(chunk[6]) << 16 | uint64_t(chunk[7]) << 24;
        if (printableId && size <= held - 8)
        {
            return std::nullopt;
        }
    }
    return failure(label, "the file is truncated: its header gives 0 bytes of audio, as a "
                          "recording never finished leaves it, but " +
                              std::to_string(held) + " bytes follow");
}

std::optional<DecodeResult> tryWav(Input &file, const Label &label, const std::atomic<bool> *cancel,
                                   uint64_t maxBytes)
{
    drwav wav;
    DeclaredData declared;
    if (!drwav_init_ex(&wav, Input::onRead, Input::onSeek<drwav_seek_origin>, Input::onTell,
                       onWavChunk, &file, &declared, 0, nullptr))
    {
        // dr_wav refuses an ADPCM data chunk of 0 bytes outright.
        return unfinished(file, label, declared, declared.blockAlign);
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
    Stream stream{aiff ? "aiff" : "wav",
                  wav.channels,
                  wav.sampleRate,
                  wav.totalPCMFrameCount,
                  adpcm ? uint64_t(wav.fmt.blockAlign) * 2 : 0,
                  cancel,
                  maxBytes};
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
            return failure(label, text);
        }
        if (auto bad = checkFormat(label, stream))
        {
            return std::move(*bad);
        }
        if (auto bad =
                unfinished(file, label, declared, adpcm ? wav.fmt.blockAlign : bytesPerFrame))
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
                return failure(label, truncatedBytes(declared.bytes, heldBytes));
            }
            if (auto bad = checkLength(label, stream))
            {
                return std::move(*bad);
            }
            return guarded(label, [&] { return decodeCompressed(label, stream, fileBytes, read); });
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
                return failure(label, truncated(stream.frames, held));
            }
            stream.frames = held;
        }
        if (auto bad = checkLength(label, stream))
        {
            return std::move(*bad);
        }
        if (stream.frames == 0)
        {
            return failure(label, "the file has no audio");
        }
        return guarded(label, [&] { return decodeBounded(label, stream, read); });
    }();
    drwav_uninit(&wav);
    return result;
}

std::optional<DecodeResult> tryFlac(Input &file, const Label &label,
                                    const std::atomic<bool> *cancel, uint64_t maxBytes)
{
    drflac *flac = drflac_open(Input::onRead, Input::onSeek<drflac_seek_origin>, Input::onTell,
                               &file, nullptr);
    if (!flac)
    {
        return std::nullopt;
    }
    // STREAMINFO's count is exact, or 0 when the encoder didn't know it.
    const uint64_t headerFrames = flac->totalPCMFrameCount;
    const Stream stream{"flac", flac->channels, flac->sampleRate, headerFrames,
                        0,      cancel,         maxBytes};
    auto result = checkHeader(label, stream);
    if (!result)
    {
        result = guarded(label, [&] {
            return decodeCompressed(label, stream, file.size(), [&](uint64_t frames, float *out) {
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

std::optional<DecodeResult> tryMp3(Input &file, const Label &label, const std::atomic<bool> *cancel,
                                   uint64_t maxBytes)
{
    auto mp3 = std::make_unique<drmp3>(); // large decoder state: keep it off the stack
    if (!drmp3_init(mp3.get(), Input::onRead, Input::onSeek<drmp3_seek_origin>, Input::onTell,
                    nullptr, &file, nullptr))
    {
        return std::nullopt;
    }
    // drmp3_get_pcm_frame_count reads the Xing/Info tag or, without one, scans the frame headers,
    // and returns to the start; it never decodes the audio.
    const uint64_t headerFrames = drmp3_get_pcm_frame_count(mp3.get());
    const Stream stream{"mp3",         mp3->channels, mp3->sampleRate, headerFrames,
                        kMp3Shortfall, cancel,        maxBytes};
    auto result = checkHeader(label, stream);
    if (!result)
    {
        result = guarded(label, [&] {
            return decodeCompressed(label, stream, file.size(), [&](uint64_t frames, float *out) {
                return drmp3_read_pcm_frames_f32(mp3.get(), frames, out);
            });
        });
    }
    drmp3_uninit(mp3.get());
    return result;
}
} // namespace

namespace
{
/// Tries dr_wav, dr_flac and dr_mp3 on `input` in turn. By content, not extension: dr_wav and
/// dr_flac check their headers; MP3 decoding is the most permissive, so it goes last. Each starts
/// from the beginning.
DecodeResult decodeInput(Input &input, const Label &label, const std::atomic<bool> *cancel,
                         uint64_t maxBytes)
{
    using Try = std::optional<DecodeResult> (*)(Input &, const Label &, const std::atomic<bool> *,
                                                uint64_t);
    for (const Try attempt : {Try(tryWav), Try(tryFlac), Try(tryMp3)})
    {
        if (!input.rewind())
        {
            return failure(label, "the file can't be read");
        }
        if (auto result = attempt(input, label, cancel, maxBytes))
        {
            return std::move(*result);
        }
    }
    return failure(label, "not a WAV, AIFF, FLAC or MP3 file, or one in an encoding that can't be "
                          "read");
}
} // namespace

DecodeResult decodeAudioFile(const fs::path &path, uint64_t maxDecodedBytes,
                             const std::atomic<bool> *cancel)
{
    const Label label = displayName(path) + ": ";
    std::error_code error;
    const auto status = fs::status(path, error);
    if (status.type() == fs::file_type::not_found)
    {
        return failure(label, "the file doesn't exist");
    }
    if (error)
    {
        return failure(label, "the file can't be read: " + error.message());
    }
    if (!fs::is_regular_file(status))
    {
        return failure(label, "not a file");
    }
    const uint64_t fileBytes = fs::file_size(path, error);
    if (error)
    {
        return failure(label, "the file can't be read: " + error.message());
    }
    if (fileBytes == 0)
    {
        return failure(label, "the file is empty");
    }
    Input file(path);
    if (!file)
    {
        return failure(label, "the file can't be opened");
    }
    return decodeInput(file, label, cancel, maxDecodedBytes);
}

DecodeResult decodeAudioBytes(std::string_view bytes, uint64_t maxDecodedBytes)
{
    if (bytes.empty())
    {
        return failure({}, "the file is empty");
    }
    Input input(bytes);
    return decodeInput(input, {}, nullptr, maxDecodedBytes);
}
} // namespace gg::media
