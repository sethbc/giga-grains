// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_XMLTEXT_H
#define GG_CORE_XMLTEXT_H

// Internal to gg-core: what its tinyxml codecs (the PTCH patch, the MEDI manifest, the machine
// settings) share: keeping any bytes but NUL unchanged through an attribute, and bounding how deep
// a document nests before tinyxml parses it.

#include <cstddef>
#include <string>
#include <string_view>

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

/// The deepest element nesting gg-core parses, the root counted as 1. Every document gg-core
/// writes nests at most 3 deep (<patch><params><p/>).
inline constexpr std::size_t kMaxXmlDepth = 64;

/// Whether `xml` nests elements at most `maxDepth` deep, by a single pass over its bytes that
/// allocates nothing. Every tinyxml Parse in gg-core runs only after this check: tinyxml parses
/// nested elements recursively, so a crafted document of a million nested elements (a few
/// megabytes) would overflow the stack.
///
/// The count follows tinyxml's own reading, erring only towards a deeper count: `<` opens an
/// element unless it starts a comment (to `-->`), a CDATA section (to `]]>`), an end tag (`</`,
/// one level up, never below 0) or another `<!` or `<?` construct (to the first `>`). An element's
/// tag ends at the first `>` outside an attribute value (quoted after `=`, as tinyxml quotes
/// them), and closes at once when `/` comes right before that `>`. In text between tags, where
/// tinyxml reads every `<` as a node, quotes don't matter.
inline bool xmlNestingWithin(std::string_view xml, std::size_t maxDepth = kMaxXmlDepth)
{
    const std::size_t n = xml.size();
    // The index just past the first `terminator` at or after `from`; n when there is none.
    const auto past = [&](std::size_t from, std::string_view terminator) {
        const std::size_t at = xml.find(terminator, from);
        return at == std::string_view::npos ? n : at + terminator.size();
    };
    std::size_t depth = 0;
    std::size_t i = 0;
    while (i < n)
    {
        if (xml[i] != '<')
        {
            ++i;
            continue;
        }
        const std::string_view rest = xml.substr(i);
        if (rest.starts_with("<!--"))
        {
            i = past(i + 4, "-->");
        }
        else if (rest.starts_with("<![CDATA["))
        {
            i = past(i + 9, "]]>");
        }
        else if (rest.starts_with("<!") || rest.starts_with("<?"))
        {
            i = past(i + 2, ">");
        }
        else if (rest.starts_with("</"))
        {
            depth = depth > 0 ? depth - 1 : 0;
            i = past(i + 2, ">");
        }
        else
        {
            // An element's tag, to its end.
            std::size_t j = i + 1;
            while (j < n && xml[j] != '>')
            {
                if (xml[j] == '=')
                {
                    // A quoted value ends only at its own quote. tinyxml skips white space
                    // before it, and in UTF-8 byte-order marks too: every control character,
                    // space and non-ASCII byte is skipped here, which can only count deeper.
                    std::size_t k = j + 1;
                    while (k < n && (static_cast<unsigned char>(xml[k]) <= ' ' ||
                                     static_cast<unsigned char>(xml[k]) >= 0x80))
                    {
                        ++k;
                    }
                    if (k < n && (xml[k] == '"' || xml[k] == '\''))
                    {
                        const std::size_t close = xml.find(xml[k], k + 1);
                        j = close == std::string_view::npos ? n : close + 1;
                        continue;
                    }
                }
                ++j;
            }
            const bool selfClosed = j < n && xml[j - 1] == '/';
            if (!selfClosed && ++depth > maxDepth)
            {
                return false;
            }
            i = j < n ? j + 1 : n;
        }
    }
    return true;
}
} // namespace gg::core

#endif // GG_CORE_XMLTEXT_H
