// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_MEDIA_UTF8PATH_H
#define GG_MEDIA_UTF8PATH_H

// Internal to gg-media: paths to and from the UTF-8 the manifest and the settings hold.

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace gg::media::detail
{
/// `path` as UTF-8. On Windows, nullopt for a wide path that isn't valid UTF-16 (an unpaired
/// surrogate), which the conversion refuses; elsewhere the bytes are copied.
inline std::optional<std::string> toUtf8(const std::filesystem::path &path)
{
    try
    {
        const auto text = path.u8string();
        return std::string(text.begin(), text.end());
    }
    catch (const std::system_error &)
    {
        return std::nullopt;
    }
}

/// `path` as UTF-8 with `/` separators on every platform (its generic form), or nullopt as for
/// toUtf8.
inline std::optional<std::string> toGenericUtf8(const std::filesystem::path &path)
{
    try
    {
        const auto text = path.generic_u8string();
        return std::string(text.begin(), text.end());
    }
    catch (const std::system_error &)
    {
        return std::nullopt;
    }
}

/// The path that UTF-8 `text` names. On Windows, nullopt for text that isn't valid UTF-8, which
/// the conversion to a wide path refuses; elsewhere the bytes are copied.
inline std::optional<std::filesystem::path> fromUtf8(const std::string &text)
{
    try
    {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }
    catch (const std::system_error &)
    {
        return std::nullopt;
    }
}

/// The file's name, for messages: UTF-8, without its directory; empty if it can't be converted.
inline std::string displayName(const std::filesystem::path &path)
{
    return toUtf8(path.filename()).value_or(std::string());
}
} // namespace gg::media::detail

#endif // GG_MEDIA_UTF8PATH_H
