// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/Sample.h"

#include <cmath>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "gg/media/Convert.h"
#include "gg/media/Decode.h"
#include "gg/media/SamplePool.h"
#include "gg/media/Sha256.h"
#include "gg/media/Utf8Path.h"

namespace gg::media
{
namespace fs = std::filesystem;

namespace
{
LoadedSample failure(const fs::path &file, const std::string &reason)
{
    LoadedSample out;
    out.error = displayName(file) + ": " + reason;
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
    const auto folder = fromUtf8(sampleFolder);
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
    return toGenericUtf8(relative).value_or(std::string());
}
} // namespace

LoadedSample loadSample(const fs::path &file, const core::MachineSettings &settings,
                        uint64_t maxDecodedBytes, std::optional<FileHash> knownHash,
                        const std::atomic<bool> *cancel)
{
    DecodeResult decoded = decodeAudioFile(file, maxDecodedBytes, cancel);
    if (!decoded.audio)
    {
        LoadedSample out;
        out.error = std::move(decoded.error);
        return out;
    }
    const auto cancelled = [cancel] {
        return cancel != nullptr && cancel->load(std::memory_order_relaxed);
    };
    const auto hash = knownHash ? std::move(knownHash) : hashFile(file, cancel);
    if (cancelled())
    {
        return failure(file, "cancelled");
    }
    if (!hash)
    {
        return failure(file, "the file can't be read");
    }
    std::error_code error;
    const fs::path absolute = fs::absolute(file, error).lexically_normal();
    const auto absoluteText = error ? std::nullopt : toUtf8(absolute);
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

std::shared_ptr<const PreparedSource> prepare(std::shared_ptr<SampleBuffer> decoded, double rate,
                                              const std::atomic<bool> *cancel, const PoolKey *share)
{
    if (decoded == nullptr)
    {
        throw std::invalid_argument("prepare: no decoded buffer");
    }
    if (!std::isfinite(rate) || rate <= 0.0)
    {
        throw std::invalid_argument("prepare: the rate must be finite and positive");
    }
    if (share != nullptr)
    {
        if (auto live = SamplePool::process().find(*share))
        {
            return live;
        }
    }
    auto converted = convertRate(std::move(decoded), rate, cancel);
    if (converted == nullptr)
    {
        return nullptr;
    }
    auto source = PreparedSource::make(std::move(converted));
    if (share != nullptr)
    {
        SamplePool::process().insert(*share, source);
    }
    return source;
}
} // namespace gg::media
