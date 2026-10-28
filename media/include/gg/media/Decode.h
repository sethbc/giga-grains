// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_MEDIA_DECODE_H
#define GG_MEDIA_DECODE_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "gg/source/SampleBuffer.h"

/// Decoding a sample file (spec §6.4) with the pinned dr_libs. Not real-time safe: it opens the
/// file, allocates the whole buffer and decodes into it, so it runs on a loader thread or, when a
/// session is restored, the main thread.
namespace gg::media
{
/// The longest file decodeAudioFile accepts, in seconds of the file's own audio (frames / the
/// file's rate), whatever the host's rate. M2's memory cap replaces it.
inline constexpr double kMaxFileSeconds = 600.0;

/// A decoded file, at its own sample rate.
struct DecodedAudio
{
    std::shared_ptr<SampleBuffer> buffer; ///< planar float, the file's rate, 1 or 2 channels
    std::string format;                   ///< "wav", "aiff", "flac" or "mp3"
    uint32_t fileChannels;                ///< as stored in the file, before keeping two
};

/// Either the audio or, when `audio` is nullopt, an error naming the file and the reason, to show
/// the user.
struct DecodeResult
{
    std::optional<DecodedAudio> audio;
    std::string error;
};

/// Decodes the WAV (RIFF, RF64 or W64; PCM 8–32-bit or float), AIFF/AIFC, FLAC or MP3 file at
/// `path`, a path made from UTF-8.
/// - The format comes from the file's content, never its extension: dr_wav (which also reads
///   AIFF) is tried first, then dr_flac, then dr_mp3, the most permissive.
/// - A mono file stays mono. A file with two or more channels keeps its first two;
///   `fileChannels` says how many it had.
/// - A file longer than kMaxFileSeconds, by the frame count in its header (for an MP3, the count
///   dr_mp3 reports), is refused before its buffer is allocated. So is a sample rate above
///   768 kHz, which bounds the buffer at 600 s × 768 kHz × 2 channels.
/// - A file that ends before the frame count its header gives (a truncated recording) keeps the
///   frames it has. One with no frames at all is an error.
/// On Windows the file is opened through dr_libs' wide-character (`_w`) functions.
DecodeResult decodeAudioFile(const std::filesystem::path &path);
} // namespace gg::media

#endif // GG_MEDIA_DECODE_H
