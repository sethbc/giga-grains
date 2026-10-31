// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/SettingsFile.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "TestHooks.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <process.h>
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <pwd.h>
#include <unistd.h>
#endif

namespace gg::media
{
namespace fs = std::filesystem;

namespace
{
/// The most loadSettings reads: the settings are a few hundred bytes.
constexpr std::size_t kMaxSettingsBytes = std::size_t{1} << 20;

/// How many times saveSettings tries its rename while it fails transiently (on Windows, another
/// program holding the settings file open), and the wait between tries: at most about half a
/// second in all, on the main thread.
constexpr int kRenameAttempts = 20;
constexpr auto kRenameRetryDelay = std::chrono::milliseconds(25);

/// How much older than the settings file a save has just written another save's temporary file
/// must be for the save to take it as a crashed save's and remove it. A save takes milliseconds,
/// so this leaves any save still running alone, another process's included; and both times are
/// the filesystem's, so no clock skew between machines sharing a folder matters.
constexpr auto kStaleAfter = std::chrono::minutes(10);

/// saveSettings's test hook (TestHooks.h); nullptr unless a test sets one.
std::atomic<detail::SaveHook> saveHook{nullptr};

/// The test hook's error at `step`, or none.
std::error_code hookAt(detail::SaveStep step, const fs::path &temporary)
{
    const auto hook = saveHook.load(std::memory_order_relaxed);
    return hook != nullptr ? hook(step, temporary) : std::error_code();
}

/// The calling thread's last error: GetLastError on Windows, errno elsewhere.
std::error_code lastError()
{
#if defined(_WIN32)
    return {static_cast<int>(::GetLastError()), std::system_category()};
#else
    return {errno, std::generic_category()};
#endif
}

/// Whether a failed rename is worth trying again shortly: on Windows, the settings file is open
/// in another program (a sharing or lock violation, or access denied, which a file pending
/// deletion also gives); elsewhere EBUSY. POSIX renames over an open file, so needs no retry.
bool isTransient(const std::error_code &error)
{
#if defined(_WIN32)
    if (error.category() != std::system_category())
    {
        return false;
    }
    switch (error.value())
    {
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_ACCESS_DENIED:
        return true;
    default:
        return false;
    }
#else
    return error == std::errc::device_or_resource_busy;
#endif
}

#if !defined(_WIN32)
/// Flushes `fd`'s data to the disk. On macOS fsync only hands it to the drive, which may keep
/// it in its cache; F_FULLFSYNC has the drive write it, and fsync is the fallback on a
/// filesystem without it (SMB, say). 0 on success, else -1 with errno set.
int syncToDisk(int fd)
{
#if defined(__APPLE__)
    if (::fcntl(fd, F_FULLFSYNC) == 0)
    {
        return 0;
    }
#endif
    int result = 0;
    do
    {
        result = ::fsync(fd);
    } while (result != 0 && errno == EINTR);
    return result;
}
#endif

/// Writes `bytes` to the file `path`, created or emptied, and flushes it to the disk before
/// closing it, so a crash or power loss after the rename that follows can't leave the settings
/// file empty or partial.
std::error_code writeDurably(const fs::path &path, std::string_view bytes)
{
    std::error_code error;
    std::size_t done = 0;
#if defined(_WIN32)
    const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return lastError();
    }
    while (!error && done < bytes.size())
    {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - done, 1u << 30));
        DWORD wrote = 0;
        if (!::WriteFile(file, bytes.data() + done, chunk, &wrote, nullptr))
        {
            error = lastError();
        }
        else if (wrote == 0)
        {
            error = std::make_error_code(std::errc::io_error);
        }
        done += wrote;
    }
    if (!error)
    {
        error = hookAt(detail::SaveStep::Write, path);
    }
    if (!error && !::FlushFileBuffers(file))
    {
        error = lastError();
    }
    if (!error)
    {
        error = hookAt(detail::SaveStep::Sync, path);
    }
    if (!::CloseHandle(file) && !error)
    {
        error = lastError();
    }
#else
    const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (file < 0)
    {
        return lastError();
    }
    while (!error && done < bytes.size())
    {
        const ssize_t wrote = ::write(file, bytes.data() + done, bytes.size() - done);
        if (wrote < 0)
        {
            if (errno != EINTR)
            {
                error = lastError();
            }
        }
        else if (wrote == 0)
        {
            error = std::make_error_code(std::errc::io_error);
        }
        else
        {
            done += static_cast<std::size_t>(wrote);
        }
    }
    if (!error)
    {
        error = hookAt(detail::SaveStep::Write, path);
    }
    if (!error && syncToDisk(file) != 0)
    {
        error = lastError();
    }
    if (!error)
    {
        error = hookAt(detail::SaveStep::Sync, path);
    }
    if (::close(file) != 0 && !error)
    {
        error = lastError();
    }
#endif
    return error;
}

/// Renames `from` over `to`, retrying a transient failure up to kRenameAttempts times in all.
/// On Windows NTFS journals the rename: after a crash the old file or the new one is whole,
/// though the rename is not necessarily durable when MoveFileExW returns (MOVEFILE_WRITE_THROUGH
/// matters only for a cross-volume copy and delete).
std::error_code renameOver(const fs::path &from, const fs::path &to)
{
    std::error_code error;
    for (int attempt = 0; attempt < kRenameAttempts; ++attempt)
    {
        if (attempt > 0)
        {
            std::this_thread::sleep_for(kRenameRetryDelay);
        }
        error = hookAt(detail::SaveStep::Rename, from);
        if (!error)
        {
#if defined(_WIN32)
            if (!::MoveFileExW(from.c_str(), to.c_str(),
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                error = lastError();
            }
#else
            if (::rename(from.c_str(), to.c_str()) != 0)
            {
                error = lastError();
            }
#endif
        }
        if (!isTransient(error))
        {
            return error;
        }
    }
    return error;
}

/// Flushes `folder`'s entries to the disk, so the rename into it survives a power loss. Best
/// effort: the save has already succeeded. Nothing on Windows, whose NTFS journals the rename.
void syncFolder(const fs::path &folder)
{
#if !defined(_WIN32)
    const int fd = ::open(folder.empty() ? "." : folder.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd >= 0)
    {
        ::fsync(fd);
        ::close(fd);
    }
#else
    (void)folder;
#endif
}

/// Whether `name` is a temporary file name saveSettings gives the settings file named `base`:
/// "<base>.<process ID>.<count>.tmp", both numbers decimal digits.
bool isTemporaryName(const fs::path::string_type &name, const fs::path::string_type &base)
{
    using Char = fs::path::value_type;
    const auto digits = [](std::basic_string_view<Char> text) {
        return !text.empty() && std::all_of(text.begin(), text.end(), [](Char c) {
            return c >= Char('0') && c <= Char('9');
        });
    };
    const fs::path::string_type suffix{Char('.'), Char('t'), Char('m'), Char('p')};
    if (name.size() <= base.size() + 1 + suffix.size() || name.compare(0, base.size(), base) != 0 ||
        name[base.size()] != Char('.') ||
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
    {
        return false;
    }
    const std::basic_string_view<Char> numbers(name.data() + base.size() + 1,
                                               name.size() - base.size() - 1 - suffix.size());
    const auto dot = numbers.find(Char('.'));
    return dot != numbers.npos && digits(numbers.substr(0, dot)) && digits(numbers.substr(dot + 1));
}

/// Removes the temporary files that earlier saves of `file` left when they crashed before their
/// rename: regular files with a save's temporary name more than kStaleAfter older than `file`,
/// which has just been written. Best effort: the save has already succeeded.
void removeStaleTemporaries(const fs::path &file)
{
    std::error_code error;
    const auto written = fs::last_write_time(file, error);
    if (error)
    {
        return;
    }
    std::vector<fs::path> stale;
    const fs::path folder = file.has_parent_path() ? file.parent_path() : fs::path(".");
    for (fs::directory_iterator it(folder, error); !error && it != fs::directory_iterator();
         it.increment(error))
    {
        const fs::path &path = it->path();
        std::error_code ignored;
        if (isTemporaryName(path.filename().native(), file.filename().native()) &&
            fs::is_regular_file(it->symlink_status(ignored)) && !ignored)
        {
            const auto time = fs::last_write_time(path, ignored);
            if (!ignored && time < written - kStaleAfter)
            {
                stale.push_back(path);
            }
        }
    }
    for (const auto &path : stale)
    {
        std::error_code ignored;
        fs::remove(path, ignored);
    }
}

/// `file`'s first kMaxSettingsBytes + 1 bytes (one past the cap, to tell a file at the cap from
/// a longer one), or nullopt if it can't be read. On Windows the file is opened sharing delete
/// access, so a save in another instance can rename over it while it is read.
std::optional<std::string> readCapped(const fs::path &file)
{
    std::string text(kMaxSettingsBytes + 1, '\0');
    std::size_t got = 0;
#if defined(_WIN32)
    const HANDLE handle = ::CreateFileW(file.c_str(), GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return std::nullopt;
    }
    bool failed = false;
    while (got < text.size())
    {
        DWORD read = 0;
        if (!::ReadFile(handle, text.data() + got, static_cast<DWORD>(text.size() - got), &read,
                        nullptr))
        {
            failed = true;
            break;
        }
        if (read == 0)
        {
            break;
        }
        got += read;
    }
    ::CloseHandle(handle);
    if (failed)
    {
        return std::nullopt;
    }
#else
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    got = static_cast<std::size_t>(in.gcount());
    if (in.bad())
    {
        return std::nullopt;
    }
#endif
    text.resize(got);
    return text;
}

#if !defined(_WIN32)
/// $HOME if it is an absolute path, else the user's home directory from the password database;
/// empty if neither is.
fs::path homeFolder()
{
    if (const char *home = std::getenv("HOME"); home != nullptr && fs::path(home).is_absolute())
    {
        return home;
    }
    if (const passwd *user = ::getpwuid(::getuid());
        user != nullptr && user->pw_dir != nullptr && fs::path(user->pw_dir).is_absolute())
    {
        return user->pw_dir;
    }
    return {};
}
#endif

int processId()
{
#if defined(_WIN32)
    return ::_getpid();
#else
    return static_cast<int>(::getpid());
#endif
}
} // namespace

fs::path defaultSettingsPath()
{
#if defined(_WIN32)
    const wchar_t *appData = ::_wgetenv(L"APPDATA");
    if (appData == nullptr || !fs::path(appData).is_absolute())
    {
        return {};
    }
    return fs::path(appData) / L"Giga Grains" / L"settings.xml";
#elif defined(__APPLE__)
    const fs::path home = homeFolder();
    if (home.empty())
    {
        return {};
    }
    return home / "Library" / "Application Support" / "Giga Grains" / "settings.xml";
#else
    // The XDG base directory spec: a relative XDG_CONFIG_HOME is invalid and ignored.
    fs::path config;
    if (const char *xdg = std::getenv("XDG_CONFIG_HOME");
        xdg != nullptr && fs::path(xdg).is_absolute())
    {
        config = xdg;
    }
    else if (const fs::path home = homeFolder(); !home.empty())
    {
        config = home / ".config";
    }
    else
    {
        return {};
    }
    return config / "giga-grains" / "settings.xml";
#endif
}

fs::path defaultCapturesFolder()
{
#if defined(_WIN32)
    const wchar_t *profile = ::_wgetenv(L"USERPROFILE");
    if (profile == nullptr || !fs::path(profile).is_absolute())
    {
        return {};
    }
    return fs::path(profile) / L"Music" / L"Giga Grains" / L"Captures";
#else
    fs::path music;
#if !defined(__APPLE__)
    if (const char *xdg = std::getenv("XDG_MUSIC_DIR");
        xdg != nullptr && fs::path(xdg).is_absolute())
    {
        music = xdg;
    }
#endif
    if (music.empty())
    {
        const fs::path home = homeFolder();
        if (home.empty())
        {
            return {};
        }
        music = home / "Music";
    }
    return music / "Giga Grains" / "Captures";
#endif
}

core::MachineSettings loadSettings(const fs::path &file)
{
    std::error_code error;
    if (!fs::is_regular_file(file, error))
    {
        return {};
    }
    const auto text = readCapped(file);
    if (!text || text->size() > kMaxSettingsBytes)
    {
        return {};
    }
    core::MachineSettings settings;
    if (!core::fromSettingsXml(*text, settings))
    {
        return {};
    }
    return settings;
}

bool saveSettings(const fs::path &file, const core::MachineSettings &settings)
{
    if (settings.sampleFolder.find('\0') != std::string::npos ||
        settings.lastLoadFolder.find('\0') != std::string::npos ||
        settings.capturesFolder.find('\0') != std::string::npos ||
        settings.theme.find('\0') != std::string::npos ||
        settings.contentFolder.find('\0') != std::string::npos)
    {
        return false;
    }
    std::error_code error;
    if (file.has_parent_path())
    {
        fs::create_directories(file.parent_path(), error);
        if (error)
        {
            return false;
        }
    }
    // A temporary file of this save's own beside `file`, renamed over it once written whole and
    // flushed to the disk. The process ID and a process-wide count make its name unique, though
    // every plugin instance in a host shares the process: a save in another process or instance
    // writes its own, and the last rename wins (ruling R16).
    static std::atomic<uint64_t> saves{0};
    fs::path temporary = file;
    temporary +=
        "." + std::to_string(processId()) + "." + std::to_string(saves.fetch_add(1)) + ".tmp";
    if (writeDurably(temporary, core::toSettingsXml(settings)) || renameOver(temporary, file))
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
    syncFolder(file.parent_path());
    removeStaleTemporaries(file);
    return true;
}

namespace detail
{
void setSaveHook(SaveHook hook) { saveHook.store(hook); }

std::error_code transientRenameError()
{
#if defined(_WIN32)
    return {ERROR_SHARING_VIOLATION, std::system_category()};
#else
    return std::make_error_code(std::errc::device_or_resource_busy);
#endif
}
} // namespace detail
} // namespace gg::media
