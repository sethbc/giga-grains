// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_TESTS_SUPPORT_UTF8_H
#define GG_TESTS_SUPPORT_UTF8_H

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include "gg/media/Utf8Path.h"

/// The tests' UTF-8 paths, through the one shared conversion (gg/media/Utf8Path.h). They throw
/// where it refuses, so a test that meant valid input fails loudly.
namespace gg::test
{
/// `path` as UTF-8.
inline std::string utf8(const std::filesystem::path &path)
{
    auto text = gg::media::toUtf8(path);
    if (!text)
    {
        throw std::invalid_argument("gg::test::utf8: the path isn't valid UTF-16");
    }
    return std::move(*text);
}

/// The path UTF-8 `text` names.
inline std::filesystem::path pathOf(const std::string &text)
{
    auto path = gg::media::fromUtf8(text);
    if (!path)
    {
        throw std::invalid_argument("gg::test::pathOf: the text isn't valid UTF-8");
    }
    return std::move(*path);
}
} // namespace gg::test

#endif // GG_TESTS_SUPPORT_UTF8_H
