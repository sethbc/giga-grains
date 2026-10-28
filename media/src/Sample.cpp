// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/Sample.h"

#include <optional>
#include <system_error>
#include <utility>

#include "Utf8Path.h"
#include "gg/media/Decode.h"
#include "gg/media/Sha256.h"

namespace gg::media
{
namespace fs = std::filesystem;

namespace
{
LoadedSample failure(const fs::path &file, const std::string &reason)
{
    LoadedSample out;
    out.error = detail::displayName(file) + ": " + reason;
    return out;
}

/// `file`'s path inside `sampleFolder` (UTF-8), with `/` separators, or empty when it isn't
/// inside it. Both are compared with their symlinks resolved.
std::string relativeTo(const fs::path &file, const std::string &sampleFolder)
{
    if (sampleFolder.empty())
    {
        return {};
    }
    const auto folder = detail::fromUtf8(sampleFolder);
    if (!folder)
    {
        return {};
    }
    std::error_code error;
    const fs::path canonicalFolder = fs::weakly_canonical(*folder, error);
    if (error)
    {
        return {};
    }
    const fs::path canonicalFile = fs::weakly_canonical(file, error);
    if (error)
    {
        return {};
    }
    // Empty when the roots differ (another drive); `..` first when the file is outside.
    const fs::path relative = canonicalFile.lexically_relative(canonicalFolder);
    if (relative.empty() || relative == "." || *relative.begin() == "..")
    {
        return {};
    }
    const auto text = relative.generic_u8string();
    return {text.begin(), text.end()};
}
} // namespace

LoadedSample loadSample(const fs::path &file, const core::MachineSettings &settings)
{
    DecodeResult decoded = decodeAudioFile(file);
    if (!decoded.audio)
    {
        LoadedSample out;
        out.error = std::move(decoded.error);
        return out;
    }
    const auto hash = hashFile(file);
    if (!hash)
    {
        return failure(file, "the file can't be read");
    }
    std::error_code error;
    const fs::path absolute = fs::absolute(file, error).lexically_normal();
    const auto absoluteText = error ? std::nullopt : detail::toUtf8(absolute);
    if (!absoluteText)
    {
        return failure(file, "the file's path can't be recorded");
    }

    const SampleBuffer &buffer = *decoded.audio->buffer;
    LoadedSample out;
    out.entry.sha256Hex = hash->sha256Hex;
    out.entry.fileSize = hash->size;
    out.entry.absolutePath = *absoluteText;
    out.entry.relativePath = relativeTo(absolute, settings.sampleFolder);
    out.entry.format = decoded.audio->format;
    out.entry.channels = decoded.audio->fileChannels;
    out.entry.sampleRate = buffer.sampleRate();
    out.entry.frames = buffer.frames();
    out.entry.embed = false;
    out.decoded = std::move(decoded.audio->buffer);
    return out;
}
} // namespace gg::media
