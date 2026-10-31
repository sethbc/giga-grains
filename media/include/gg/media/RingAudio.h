// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_MEDIA_RINGAUDIO_H
#define GG_MEDIA_RINGAUDIO_H

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gg/core/SessionState.h"
#include "gg/source/RingStorage.h"
#include "gg/source/SampleBuffer.h"

/// A live ring as a float32 WAV file and back (spec §6.6): the bytes of an embedded payload or a
/// file in the captures folder, and the storage a restored session hands its layer. Main or loader
/// thread: these allocate and read files, never the audio thread.
namespace gg::media
{
/// The window as a stereo SampleBuffer at `rate`: `frames` frames read from `slots` (interleaved
/// stereo, frame f at slot f mod `capacity`), the oldest, absolute frame `oldest`, first. Copies
/// the floats as they are. Throws std::invalid_argument for a null `slots`, a `capacity` below 1,
/// `frames` negative or above `capacity`, a negative `oldest`, or a `rate` that isn't finite and
/// positive, and std::bad_alloc.
std::shared_ptr<SampleBuffer> ringToBuffer(const float *slots, int64_t capacity, int64_t oldest,
                                           int64_t frames, double rate);

/// A window's WAV bytes (encodeFloat32Wav) with their fingerprint, the pair a RingEntry records.
struct EncodedRing
{
    std::string wavBytes;
    std::string sha256Hex; // of wavBytes, 64 lowercase hex digits
    uint64_t size;         // wavBytes.size()
};

/// `window` as WAV bytes, hashed. Throws as encodeFloat32Wav does.
EncodedRing encodeRing(const SampleBuffer &window);

/// A saved ring as storage for a layer's ring.
struct RestoredRing
{
    std::unique_ptr<RingStorage> storage; // null on failure
    int64_t frames{0};                    // the frames restored, at the front of the storage
    std::vector<int64_t> splices;         // the marks, in frames from the storage's first frame
    std::string error;                    // non-empty on failure
};

/// Restores `entry`'s ring into a RingStorage of `ringFrames` frames (plus `maxBlockSize`).
/// - The audio is the WAV in `wavBytes` when that isn't empty (an embedded payload, or a filed
///   one the caller found and read), otherwise the file at `entry.absolutePath`. It is decoded
///   within `maxDecodedBytes` (decodeAudioBytes, decodeAudioFile) and must hold `entry.frames`
///   frames.
/// - When its rate is not `rate` it is converted (convertRate) and the marks scale with it:
///   each is llround(mark × rate / the audio's rate), the frame count likewise.
/// - A window longer than `ringFrames` keeps its newest `ringFrames` frames, and the marks move
///   with them: those before the kept part go. The kept frames are at slots 0 .. frames - 1
///   (slot s's channels at 2s, 2s + 1) and everything after is silence; the guard slots equal
///   slots 0 .. 14, as a running ring keeps them.
/// - `splices` is ascending, without repeats, and every mark is below `frames`.
/// - On any failure `error` says why and `storage` is null. Mono audio is kept on both channels.
RestoredRing restoreRing(const core::RingEntry &entry, std::string_view wavBytes, double rate,
                         int64_t ringFrames, uint32_t maxBlockSize, uint64_t maxDecodedBytes);

/// restoreRing for a window already in memory, never decoded (a ring the plugin kept at a
/// deactivation, MediaSession): `window`, which must hold `entry.frames` frames, converted from its
/// own rate to `rate` when they differ, then kept, laid out and marked as restoreRing does.
/// `window` is read, never written (convertRate gives a new buffer). Fails as restoreRing does,
/// and for a null `window`.
RestoredRing restoreRingFrom(const core::RingEntry &entry, std::shared_ptr<SampleBuffer> window,
                             double rate, int64_t ringFrames, uint32_t maxBlockSize);
} // namespace gg::media

#endif // GG_MEDIA_RINGAUDIO_H
