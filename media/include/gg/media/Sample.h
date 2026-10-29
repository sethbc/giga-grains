// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_MEDIA_SAMPLE_H
#define GG_MEDIA_SAMPLE_H

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "gg/core/MachineSettings.h"
#include "gg/core/SessionState.h"
#include "gg/media/Sha256.h"
#include "gg/source/PreparedSource.h"
#include "gg/source/SampleBuffer.h"

/// Loading a sample the user chose (spec §6.4, §6.6): the decoded audio and its manifest entry,
/// then the source the engine plays. Not real-time safe: these allocate, and loadSample reads the
/// file.
namespace gg::media
{
/// A loaded sample, or the reason it couldn't be.
struct LoadedSample
{
    std::shared_ptr<SampleBuffer> decoded; // the file's rate
    core::MediaEntry entry;                // filled in from the file
    std::string error;                     // non-empty on failure; decoded null
};

/// Decodes `file` (decodeAudioFile), hashes it (hashFile) and fills its entry. Main or loader
/// thread.
/// - The entry is isValidMediaEntry: the SHA-256 and size are the file's; `absolutePath` is
///   `file` made absolute and lexically normal, in UTF-8, symlinks kept as the user chose them;
///   `format` and `channels` (as stored in the file) are decodeAudioFile's; `sampleRate` and
///   `frames` are the file's.
/// - `relativePath` is the file's path relative to `settings.sampleFolder`, with `/` separators
///   so it works on every platform, when the file is inside that folder; otherwise empty. Both
///   are compared with their symlinks resolved (std::filesystem::weakly_canonical), so a folder
///   reached through a symlink (macOS's /var for /private/var, say) still counts.
/// - On failure `decoded` is null, `entry` is default and `error` names the file and the reason:
///   decodeAudioFile's message, or a failure to read the file for its hash.
/// - `knownHash`, when given, is taken as the file's fingerprint instead of hashing it again: a
///   restore passes the one findSample just checked, so the file is read twice, not three times.
/// - A known limitation: the file is read twice, to decode it and then to hash it (or, with
///   `knownHash`, to hash it and then to decode it). A file replaced between the two reads gives
///   an entry whose fingerprint isn't the decoded audio's.
/// - `cancel`, when given, is read after the decode and while hashing (hashFile): once it is set,
///   the load fails, with an error saying so (the sample loader's prompt stop, carry-over [194]).
LoadedSample loadSample(const std::filesystem::path &file, const core::MachineSettings &settings,
                        std::optional<FileHash> knownHash = std::nullopt,
                        const std::atomic<bool> *cancel = nullptr);

/// `decoded` converted to `rate` (convertRate: the same buffer when the rates match), then made a
/// PreparedSource, with the interleaved copy for a stereo buffer. Main or loader thread; the
/// plugin passes the host's rate. nullptr if `cancel` is set during the conversion (convertRate).
/// Throws std::invalid_argument if `decoded` is null or `rate` isn't finite and positive, and
/// std::bad_alloc if memory runs out.
std::shared_ptr<const PreparedSource> prepare(std::shared_ptr<SampleBuffer> decoded, double rate,
                                              const std::atomic<bool> *cancel = nullptr);
} // namespace gg::media

#endif // GG_MEDIA_SAMPLE_H
