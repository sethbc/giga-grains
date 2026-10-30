// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_MEDIA_COLLECT_H
#define GG_MEDIA_COLLECT_H

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "gg/core/MachineSettings.h"
#include "gg/core/SessionState.h"

/// Collect and save (spec §6.7): every file a session state references, gathered into one folder
/// so the project can move. Not real-time safe: it searches, hashes and copies, on the main
/// thread.
namespace gg::media
{
struct CollectResult
{
    core::SessionState state;         // the input, its entries pointing into the folder
    std::vector<std::string> missing; // the saved paths (UTF-8) of what couldn't be collected
    std::vector<std::string> copied;  // the files written into the folder (UTF-8 paths)
};

/// Copies the file of every sample and filed ring in `state` that can be found into `folder`
/// (created if needed) and returns the state with those entries pointing at the copies.
/// - A sample is found as findSample finds it (with `contentRoots`), a filed ring as
///   findFiledAudio does. A file whose content isn't the entry's (Changed) counts as not found.
/// - The copy is named as the found file is. When that name is taken in `folder` by a file of
///   other content (never overwritten), it is `<stem>-<first 8 hex digits of the SHA-256><ext>`.
///   A file already in `folder` with the entry's content is used as it is, so one file used by
///   two layers, or a second collect, copies nothing.
/// - Each copy's hash is checked against the entry's; a copy that fails is removed and counts as
///   missing.
/// - A collected entry's `absolutePath` is the copy's path; a sample's `relativePath` is the
///   copy's name (relative to `folder`, with `/` separators), and its `contentPath` is as it was.
/// - An embedded entry needs no file and is left alone, as is the AUDI payloads; so is an entry
///   that couldn't be collected, which is listed in `missing` by its saved path.
/// `state` itself is not changed. Nothing throws but std::bad_alloc.
CollectResult collectMedia(const core::SessionState &state, const std::filesystem::path &folder,
                           const core::MachineSettings &settings,
                           std::span<const std::filesystem::path> contentRoots);
} // namespace gg::media

#endif // GG_MEDIA_COLLECT_H
