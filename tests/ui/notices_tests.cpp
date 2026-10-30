// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The editor's embedded notices (spec §6.10, carry-over row 221): the About box shows the texts
// cmake/embed-text.cmake compiled into gg-ui, so they must be the installer's license set
// (cmake/licenses.cmake) in its order, each byte-equal to the file it was made from. Built only
// with the editor (gg-ui).

#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/ui/Notices.h"

namespace
{
// The file's bytes as they are on disk: binary, so no line-ending translation on Windows.
std::string readFile(std::string_view path)
{
    std::ifstream in(std::string(path), std::ios::binary);
    REQUIRE(in.good());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
} // namespace

TEST_CASE("the embedded notices are the license set, byte for byte", "[ui]")
{
    std::vector<std::string_view> expected{"THIRD_PARTY_NOTICES.md", "LICENSE", "GPL-3.0.txt",
                                           "CMakeRC-LICENSE.txt", "flowbite-icons-LICENSE.txt"};
#if defined(__APPLE__)
    expected.push_back("AudioUnitSDK-LICENSE.txt");
#elif defined(_WIN32)
    expected.push_back("ASIO-SDK-LICENSE.txt");
#endif

    const auto notices = gg::ui::noticeTexts();
    REQUIRE(notices.size() == expected.size());
    for (std::size_t i = 0; i < notices.size(); ++i)
    {
        const auto &notice = notices[i];
        INFO("notice " << i << ": " << notice.name << " from " << notice.sourcePath);
        CHECK(notice.name == expected[i]);
        const std::string onDisk = readFile(notice.sourcePath);
        CHECK(!onDisk.empty());
        // The sizes first, so a mismatch reports a number before the texts themselves.
        REQUIRE(notice.text.size() == onDisk.size());
        CHECK(notice.text == onDisk);
    }

    // The notices come first, beginning with the file's own first line (read, not hard-coded).
    const std::string notices0 = readFile(notices[0].sourcePath);
    const std::string firstLine = notices0.substr(0, notices0.find('\n'));
    CHECK(firstLine.starts_with("# "));
    CHECK(notices[0].text.starts_with(firstLine));
    CHECK(notices[0].sourcePath.ends_with("/THIRD_PARTY_NOTICES.md"));
}
