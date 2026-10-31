// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_NOTICES_H
#define GG_UI_NOTICES_H

#include <span>
#include <string_view>

namespace gg::ui
{
/// One text of the license set every installer carries (spec §6.10, cmake/licenses.cmake), as
/// the About box shows it.
struct NoticeText
{
    std::string_view name;       ///< Its published name, as in gg-products/licenses/.
    std::string_view sourcePath; ///< The absolute path of the file it was embedded from.
    std::string_view text;       ///< The file's bytes, verbatim.
};

/// The license set in the installer's order: THIRD_PARTY_NOTICES.md, LICENSE, GPL-3.0.txt,
/// CMakeRC-LICENSE.txt, flowbite-icons-LICENSE.txt, then AudioUnitSDK-LICENSE.txt on macOS or
/// ASIO-SDK-LICENSE.txt on Windows. The texts are compiled in at build time
/// (cmake/embed-text.cmake writes Notices.cpp under the build tree), so they live as long as the
/// program.
std::span<const NoticeText> noticeTexts() noexcept;
} // namespace gg::ui

#endif // GG_UI_NOTICES_H
