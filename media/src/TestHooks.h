// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_MEDIA_TESTHOOKS_H
#define GG_MEDIA_TESTHOOKS_H

// Internal to gg-media, and reached only by the tests: hooks into findSample's scan and
// saveSettings's steps, so the tests can see the order of the steps and make a step fail the way
// a filesystem can (EPERM, EIO, a sharing violation) where no real file would. Unset (nullptr),
// as they are unless a test sets them, they cost one atomic load per step. Each test case runs in
// a process of its own, so a hook set by one never reaches another.

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

/// How many candidate files findSample has hashed so far in this process: a file reached by two
/// spellings (a link, or a normalization-insensitive file system) is hashed once.
unsigned candidateHashCount();

/// saveSettings's steps.
enum class SaveStep
{
    Write,  // has written the temporary file whole, not yet flushed to the disk
    Sync,   // has flushed it to the disk (fsync, F_FULLFSYNC on macOS, FlushFileBuffers on
            // Windows), not yet closed it
    Rename, // about to rename it over the settings file: once per attempt
};

/// Called at each step of a save with the temporary file. An error it returns is taken as that
/// step's own: the save fails, or for Rename that attempt does (retried if transient).
using SaveHook = std::error_code (*)(SaveStep step, const std::filesystem::path &temporary);

/// Sets the save's hook (nullptr: none). Not to be called while a save runs.
void setSaveHook(SaveHook hook);

/// An error saveSettings takes as transient, and retries a rename after: on Windows a sharing
/// violation, as another program holding the settings file open gives; elsewhere EBUSY.
std::error_code transientRenameError();
} // namespace gg::media::detail

#endif // GG_MEDIA_TESTHOOKS_H
