// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The shared UTF-8 path helpers (gg/media/Utf8Path.h), which gg-media, the plugins' loader and
// editor, gg-render and the tests all convert paths with. On Windows a path is wide, so the
// conversion can refuse; the Windows leg runs those sections.

#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "gg/media/Utf8Path.h"
#include "media/AudioFixtures.h"

namespace
{
namespace fs = std::filesystem;
using gg::media::displayName;
using gg::media::fromUtf8;
using gg::media::toGenericUtf8;
using gg::media::toUtf8;

// "sämple ✓" in UTF-8, spelled out byte by byte.
const std::string kNonAscii = "s\xc3\xa4mple \xe2\x9c\x93";
} // namespace

TEST_CASE("utf8 path helpers round-trip non-ASCII names", "[media]")
{
    const auto path = fromUtf8(kNonAscii + "/kick.wav");
    REQUIRE(path.has_value());
    REQUIRE(*path == fs::path(u8"sämple ✓") / "kick.wav");
    REQUIRE(path->filename() == "kick.wav");
    REQUIRE(toGenericUtf8(*path) == kNonAscii + "/kick.wav");
    REQUIRE(toUtf8(path->parent_path()) == kNonAscii);
    REQUIRE(displayName(*path) == "kick.wav");
    REQUIRE(displayName(fs::path(u8"sämple ✓")) == kNonAscii);
    REQUIRE(fromUtf8("") == fs::path());
    REQUIRE(toUtf8(fs::path()) == "");
}

TEST_CASE("utf8 path helpers name a file the filesystem opens", "[media]")
{
    // The path made from UTF-8 is the native one: on Windows the wide name, so the file written
    // through it is found again by its wide spelling, and its directory listing gives the same
    // UTF-8 back.
    const auto dir = gg::test::scratchDir("utf8-path");
    const auto file = fromUtf8(toUtf8(dir).value() + "/" + kNonAscii + ".txt");
    REQUIRE(file.has_value());
    std::ofstream(*file) << "x";
    REQUIRE(fs::exists(dir / fs::path(u8"sämple ✓.txt")));
    bool listed = false;
    for (const auto &entry : fs::directory_iterator(dir))
    {
        listed = listed || toUtf8(entry.path().filename()) == kNonAscii + ".txt";
    }
    REQUIRE(listed);
}

TEST_CASE("utf8 path helpers refuse what the platform can't convert", "[media]")
{
#if defined(_WIN32)
    SECTION("text that isn't valid UTF-8 names no path")
    {
        REQUIRE_FALSE(fromUtf8("bad \xff name.wav").has_value());
        REQUIRE_FALSE(fromUtf8("cut short \xe2\x9c").has_value());
    }
    SECTION("a wide path with an unpaired surrogate has no UTF-8")
    {
        const fs::path lone(std::wstring(L"lone \xD800 surrogate.wav"));
        REQUIRE_FALSE(toUtf8(lone).has_value());
        REQUIRE_FALSE(toGenericUtf8(lone).has_value());
        REQUIRE(displayName(lone).empty());
    }
#else
    SECTION("a path is its bytes, valid UTF-8 or not")
    {
        const std::string bytes = "bad \xff name.wav";
        const auto path = fromUtf8(bytes);
        REQUIRE(path.has_value());
        REQUIRE(path->native() == bytes);
        REQUIRE(toUtf8(*path) == bytes);
        REQUIRE(displayName(*path) == bytes);
    }
#endif
}
