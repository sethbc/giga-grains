// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_XMLTEXT_H
#define GG_CORE_XMLTEXT_H

// Internal to gg-core: what its tinyxml codecs (the MEDI manifest, the machine settings) share
// to keep any bytes but NUL unchanged through an attribute.

#include <string>

namespace gg::core
{
/// tinyxml writes `&#x` through unescaped, as a character reference it was given, and so reads
/// it back as the character: a path holding the text "&#x41;" would come back as "A". Writing
/// every `&` as a reference of its own brings any text back unchanged.
inline std::string escapeAmpersands(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (const char c : s)
    {
        if (c == '&')
        {
            out += "&#x26;";
        }
        else
        {
            out += c;
        }
    }
    return out;
}
} // namespace gg::core

#endif // GG_CORE_XMLTEXT_H
