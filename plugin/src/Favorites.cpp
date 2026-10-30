// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "Favorites.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace gg::plugin
{
namespace
{
namespace fs = std::filesystem;

/// Whether `line` holds nothing but white space.
bool blank(std::string_view line)
{
    return std::all_of(line.begin(), line.end(), [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
    });
}

long processId()
{
#if defined(_WIN32)
    return static_cast<long>(::_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

/// A temporary file name beside `file`, unique in this process and, by the process ID, among
/// the instances of other processes saving the same file.
fs::path temporaryFor(const fs::path &file)
{
    static std::atomic<unsigned long> count{0};
    fs::path temporary = file;
    temporary += ".tmp-" + std::to_string(processId()) + "-" + std::to_string(count++);
    return temporary;
}
} // namespace

std::vector<std::string> loadFavorites(const fs::path &file)
{
    std::vector<std::string> keys;
    if (file.empty())
    {
        return keys;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        return keys;
    }
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    for (std::size_t start = 0; start < text.size();)
    {
        const std::size_t end = std::min(text.find('\n', start), text.size());
        std::string_view line(text.data() + start, end - start);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        if (!blank(line))
        {
            keys.emplace_back(line);
        }
        start = end + 1;
    }
    return keys;
}

bool saveFavorites(const fs::path &file, std::span<const std::string> keys)
{
    if (file.empty())
    {
        return false;
    }
    std::string text;
    for (const auto &key : keys)
    {
        if (key.find_first_of("\r\n") != std::string::npos)
        {
            return false;
        }
        if (!key.empty())
        {
            text += key;
            text += '\n';
        }
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
    return !writeReplacing(file, text);
}

std::error_code writeReplacing(const fs::path &file, std::string_view bytes)
{
    const fs::path temporary = temporaryFor(file);
    std::error_code error;
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out)
        {
            error = std::make_error_code(std::errc::io_error);
        }
    }
    if (!error)
    {
        fs::rename(temporary, file, error);
    }
    if (error)
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
    }
    return error;
}
} // namespace gg::plugin
