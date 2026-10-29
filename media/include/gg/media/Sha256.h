// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_MEDIA_SHA256_H
#define GG_MEDIA_SHA256_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

/// A sample file's fingerprint (spec §6.4): the SHA-256 of its bytes (FIPS 180-4), with its size
/// beside it so a search can skip candidates by size before hashing. In-house, no dependency.
/// Not real-time safe: these allocate and read files.
namespace gg::media
{
/// A file's fingerprint: its SHA-256 as 64 lowercase hex digits, and its size in bytes.
struct FileHash
{
    std::string sha256Hex;
    uint64_t size;
};

/// The fingerprint of the file at `path`, read in 64 KiB pieces, or nullopt if it can't be
/// opened or read to its end. `cancel`, when given, is read before each piece: once it is set,
/// the hash stops and gives nullopt (the sample loader's prompt stop, carry-over [194]).
std::optional<FileHash> hashFile(const std::filesystem::path &path,
                                 const std::atomic<bool> *cancel = nullptr);

/// The SHA-256 of `bytes`, as 64 lowercase hex digits.
std::string sha256Hex(std::span<const std::byte> bytes);
} // namespace gg::media

#endif // GG_MEDIA_SHA256_H
