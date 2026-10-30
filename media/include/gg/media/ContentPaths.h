// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_MEDIA_CONTENTPATHS_H
#define GG_MEDIA_CONTENTPATHS_H

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "gg/core/MachineSettings.h"
#include "gg/media/SettingsFile.h"
#include "gg/media/Utf8Path.h"

/// The content search path (the presets spec §6.4): the folders the factory content (its
/// presets, its samples and its content-version.txt) is found in, a content-relative sample
/// reference resolved against them, and the user's presets folder and favorites file beside the
/// settings file. Header-only, JUCE-free, so gg-render and the plugins share it. Each function
/// reads the environment (and resolveContentPath the filesystem) and allocates: main thread or a
/// worker, never the audio thread.
namespace gg::media
{
namespace detail
{
/// The environment variable `name` as a path; nullopt when it is unset or empty.
#if defined(_WIN32)
inline std::optional<std::filesystem::path> environmentPath(const wchar_t *name)
{
    if (const wchar_t *value = ::_wgetenv(name); value != nullptr && *value != 0)
    {
        return std::filesystem::path(value);
    }
    return std::nullopt;
}
#else
inline std::optional<std::filesystem::path> environmentPath(const char *name)
{
    if (const char *value = std::getenv(name); value != nullptr && *value != 0)
    {
        return std::filesystem::path(value);
    }
    return std::nullopt;
}
#endif

/// The environment variable `name` as an absolute path; nullopt when it is unset, empty or
/// relative.
#if defined(_WIN32)
inline std::optional<std::filesystem::path> absoluteEnvironmentPath(const wchar_t *name)
#else
inline std::optional<std::filesystem::path> absoluteEnvironmentPath(const char *name)
#endif
{
    auto path = environmentPath(name);
    if (path && !path->is_absolute())
    {
        return std::nullopt;
    }
    return path;
}

#if !defined(_WIN32) && !defined(__APPLE__)
/// The XDG base directory spec's data home: $XDG_DATA_HOME when absolute (a relative one is
/// invalid and ignored), else ~/.local/share; nullopt when HOME is unset or empty too.
inline std::optional<std::filesystem::path> xdgDataHome()
{
    if (auto data = absoluteEnvironmentPath("XDG_DATA_HOME"))
    {
        return data;
    }
    if (auto home = environmentPath("HOME"))
    {
        return *home / ".local" / "share";
    }
    return std::nullopt;
}
#endif
} // namespace detail

/// The content roots, in the order a content-relative reference is resolved against them, each
/// only when known and not empty:
/// 1. $GG_CONTENT_DIR (tests, gg-render, a developer tree);
/// 2. the user's content folder, `settings.contentFolder` (UTF-8; left out if it isn't valid
///    UTF-8 on Windows);
/// 3. the per-user install: macOS ~/Library/Application Support/sethbc/Giga Grains, Windows
///    %LOCALAPPDATA%\sethbc\Giga Grains, Linux $XDG_DATA_HOME/sethbc/Giga Grains (default
///    ~/.local/share/sethbc/Giga Grains);
/// 4. the system install: macOS /Library/Application Support/sethbc/Giga Grains, Windows
///    %ProgramData%\sethbc\Giga Grains, Linux each entry of $XDG_DATA_DIRS (default
///    /usr/local/share:/usr/share) plus sethbc/Giga Grains, in its order.
/// `~` is $HOME. As the XDG base directory spec says, a relative XDG_DATA_HOME, or a relative or
/// empty entry of XDG_DATA_DIRS, is ignored; on Windows a relative LOCALAPPDATA or ProgramData is
/// too. Each root is returned as built, with no check that it exists.
inline std::vector<std::filesystem::path> contentRoots(const core::MachineSettings &settings)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> roots;
#if defined(_WIN32)
    if (auto dev = detail::environmentPath(L"GG_CONTENT_DIR"))
#else
    if (auto dev = detail::environmentPath("GG_CONTENT_DIR"))
#endif
    {
        roots.push_back(std::move(*dev));
    }
    if (!settings.contentFolder.empty())
    {
        if (auto folder = fromUtf8(settings.contentFolder))
        {
            roots.push_back(std::move(*folder));
        }
    }
#if defined(_WIN32)
    if (auto local = detail::absoluteEnvironmentPath(L"LOCALAPPDATA"))
    {
        roots.push_back(*local / L"sethbc" / L"Giga Grains");
    }
    if (auto common = detail::absoluteEnvironmentPath(L"ProgramData"))
    {
        roots.push_back(*common / L"sethbc" / L"Giga Grains");
    }
#elif defined(__APPLE__)
    if (auto home = detail::environmentPath("HOME"))
    {
        roots.push_back(*home / "Library" / "Application Support" / "sethbc" / "Giga Grains");
    }
    roots.emplace_back("/Library/Application Support/sethbc/Giga Grains");
#else
    if (auto data = detail::xdgDataHome())
    {
        roots.push_back(*data / "sethbc" / "Giga Grains");
    }
    std::string dirs = "/usr/local/share:/usr/share";
    if (const char *value = std::getenv("XDG_DATA_DIRS"); value != nullptr && *value != 0)
    {
        dirs = value;
    }
    for (std::size_t start = 0; start <= dirs.size();)
    {
        const std::size_t end = std::min(dirs.find(':', start), dirs.size());
        const fs::path dir(dirs.substr(start, end - start));
        if (!dir.empty() && dir.is_absolute())
        {
            roots.push_back(dir / "sethbc" / "Giga Grains");
        }
        start = end + 1;
    }
#endif
    return roots;
}

/// The first `root / contentRelative` of `roots`, in order, that is a regular file (a link to
/// one counts); nullopt when none is. `contentRelative` is UTF-8 with `/` separators, relative
/// to a content root (`Samples/Bowed/cello.flac`), and is refused, nullopt without looking,
/// when it is empty, holds a backslash or a NUL, isn't valid UTF-8 (on Windows), has a root (is
/// absolute, or on Windows names a drive or a server), or has a `..` segment: a reference never
/// reaches outside its root.
inline std::optional<std::filesystem::path>
resolveContentPath(std::string_view contentRelative, std::span<const std::filesystem::path> roots)
{
    namespace fs = std::filesystem;
    if (contentRelative.empty() || contentRelative.find('\\') != std::string_view::npos ||
        contentRelative.find('\0') != std::string_view::npos)
    {
        return std::nullopt;
    }
    const auto relative = fromUtf8(std::string(contentRelative));
    if (!relative || relative->has_root_path())
    {
        return std::nullopt;
    }
    for (const auto &segment : *relative)
    {
        if (segment == "..")
        {
            return std::nullopt;
        }
    }
    for (const auto &root : roots)
    {
        fs::path candidate = root / *relative;
        std::error_code error;
        if (fs::is_regular_file(candidate, error))
        {
            return candidate;
        }
    }
    return std::nullopt;
}

/// A content root's factory presets folder.
inline std::filesystem::path factoryPresetsFolder(const std::filesystem::path &root)
{
    return root / "Presets";
}

/// A content root's content-version.txt.
inline std::filesystem::path contentVersionFile(const std::filesystem::path &root)
{
    return root / "content-version.txt";
}

/// The user presets folder, where "Save as…" writes: beside the settings file
/// (`<its folder>/Presets`; for the default file macOS ~/Library/Application Support/Giga
/// Grains/Presets and Windows %APPDATA%\Giga Grains\Presets), except on Linux for the default
/// settings file (defaultSettingsPath), which is configuration while presets are data:
/// $XDG_DATA_HOME/giga-grains/presets (default ~/.local/share/giga-grains/presets, and beside
/// the settings file when HOME is unset or empty too). Empty when `settingsFile` is, so nothing
/// is ever written to a relative folder.
inline std::filesystem::path userPresetsFolder(const std::filesystem::path &settingsFile)
{
    if (settingsFile.empty())
    {
        return {};
    }
#if !defined(_WIN32) && !defined(__APPLE__)
    if (settingsFile == defaultSettingsPath())
    {
        if (auto data = detail::xdgDataHome())
        {
            return *data / "giga-grains" / "presets";
        }
    }
#endif
    return settingsFile.parent_path() / "Presets";
}

/// The per-machine favorites file, beside the settings file on every platform. Empty when
/// `settingsFile` is.
inline std::filesystem::path favoritesFile(const std::filesystem::path &settingsFile)
{
    if (settingsFile.empty())
    {
        return {};
    }
    return settingsFile.parent_path() / "favorites.txt";
}
} // namespace gg::media

#endif // GG_MEDIA_CONTENTPATHS_H
