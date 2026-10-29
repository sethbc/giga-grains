// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_MEDIA_TESTHOOKS_H
#define GG_MEDIA_TESTHOOKS_H

// Internal to gg-media, and reached only by the tests: a hook into findSample's scan, so the
// tests can make a step fail the way a filesystem can (EPERM, EIO) where no real file would.
// Unset (nullptr), as it is unless a test sets it, it costs one atomic load per step. Each test
// case runs in a process of its own, so a hook set by one never reaches another.

#include <filesystem>
#include <system_error>

namespace gg::media::detail
{
/// Where findSample's scan of the sample folder is.
enum class ScanStep
{
    Open, // about to open a folder, the sample folder included, to list it
    Next, // has listed an entry of a folder and moved on to the next
};

/// Called at each step of the scan with the folder: the one about to be opened, or the one being
/// listed. An error it returns is taken as that step's own: the folder can't be opened, or the
/// rest of it can't be listed. It may change the filesystem (remove the folder, say) and return
/// no error, so the step meets the change.
using ScanHook = std::error_code (*)(ScanStep step, const std::filesystem::path &folder);

/// Sets the scan's hook (nullptr: none). Not to be called while a search runs.
void setScanHook(ScanHook hook);
} // namespace gg::media::detail

#endif // GG_MEDIA_TESTHOOKS_H
