// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_MEDIA_SAMPLE_H
#define GG_MEDIA_SAMPLE_H

#include <filesystem>
#include <memory>
#include <string>

#include "gg/core/MachineSettings.h"
#include "gg/core/SessionState.h"
#include "gg/source/SampleBuffer.h"

/// Loading a sample the user chose (spec §6.4, §6.6): the decoded audio and its manifest entry.
/// Not real-time safe: it reads the file twice, to decode and to hash it, and allocates.
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
LoadedSample loadSample(const std::filesystem::path &file, const core::MachineSettings &settings);
} // namespace gg::media

#endif // GG_MEDIA_SAMPLE_H
