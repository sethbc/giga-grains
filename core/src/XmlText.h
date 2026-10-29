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

/// The deepest element nesting gg-core parses, the root counted as 1 and a self-closed element as
/// a level. Every document gg-core writes nests at most 3 deep (<patch><params><p/>).
inline constexpr std::size_t kMaxXmlDepth = 64;

namespace xml_detail
{
// The ASCII white space tinyxml's IsWhiteSpace (isspace) skips.
constexpr bool isSpace(unsigned char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

// How many bytes tinyxml's UTF-8 reading (its utf8ByteTable) takes as one character from a byte:
// 2 to 4 from a lead byte, else 1.
constexpr std::size_t utf8Length(unsigned char c)
{
    return c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 1;
}

// Whether every `&#` in `xml` starts a numeric character reference, `&#` digits `;` or `&#x` hex
// digits `;`. tinyxml finds a reference's end at the first `;` anywhere after it and reads back
// from there to the `#` or `x`, so a reference of any other shape can hide markup (`&#x</a>x;`
// is one reference to tinyxml).
inline bool characterReferencesWellFormed(std::string_view xml)
{
    for (std::size_t at = xml.find("&#"); at != std::string_view::npos; at = xml.find("&#", at + 2))
    {
        std::size_t i = at + 2;
        const bool hex = i < xml.size() && xml[i] == 'x';
        i += hex ? 1 : 0;
        const std::size_t digits = i;
        while (i < xml.size() &&
               ((xml[i] >= '0' && xml[i] <= '9') ||
                (hex && ((xml[i] >= 'a' && xml[i] <= 'f') || (xml[i] >= 'A' && xml[i] <= 'F')))))
        {
            ++i;
        }
        if (i == digits || i == xml.size() || xml[i] != ';')
        {
            return false;
        }
    }
    return true;
}

// Whether every byte tinyxml's UTF-8 reading takes as a lead byte is followed by as many bytes
// from 0x80 up as the character takes: then a character it reads never swallows markup (an
// ASCII byte), wherever the reading starts, and never runs past the end.
inline bool utf8LeadBytesComplete(std::string_view xml)
{
    for (std::size_t i = 0; i < xml.size(); ++i)
    {
        const std::size_t length = utf8Length(static_cast<unsigned char>(xml[i]));
        if (length > xml.size() - i)
        {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k)
        {
            if (static_cast<unsigned char>(xml[i + k]) < 0x80)
            {
                return false;
            }
        }
    }
    return true;
}

// The index just past the XML declaration starting at `at` (`<?xml`, in any case), or npos to
// refuse it. tinyxml reads a declaration's `version`, `encoding` and `standalone` values
// quoted, so a quoted `>` doesn't end it, but whether it takes a name as one of those three
// depends on the locale's case folding. So only a declaration that ends at its first `>`
// whichever way it reads is accepted: ASCII names and values, each value quoted and holding no
// white space, quote, `<`, `>` or `&`.
inline std::size_t declarationEnd(std::string_view xml, std::size_t at)
{
    const auto isNameByte = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-' || c == '.' || c == ':';
    };
    std::size_t i = at + 5;
    for (;;)
    {
        const std::size_t spaceFrom = i;
        while (i < xml.size() && isSpace(static_cast<unsigned char>(xml[i])))
        {
            ++i;
        }
        if (i + 1 < xml.size() && xml[i] == '?' && xml[i + 1] == '>')
        {
            return i + 2;
        }
        // An attribute: white space before it, a name, `=` and a quoted value.
        if (i == spaceFrom || i == xml.size() || !isNameByte(xml[i]))
        {
            return std::string_view::npos;
        }
        while (i < xml.size() && isNameByte(xml[i]))
        {
            ++i;
        }
        while (i < xml.size() && isSpace(static_cast<unsigned char>(xml[i])))
        {
            ++i;
        }
        if (i == xml.size() || xml[i] != '=')
        {
            return std::string_view::npos;
        }
        ++i;
        while (i < xml.size() && isSpace(static_cast<unsigned char>(xml[i])))
        {
            ++i;
        }
        if (i == xml.size() || (xml[i] != '"' && xml[i] != '\''))
        {
            return std::string_view::npos;
        }
        const char quote = xml[i++];
        while (i < xml.size() && xml[i] != quote)
        {
            const auto c = static_cast<unsigned char>(xml[i]);
            if (c <= ' ' || c >= 0x7f || c == '"' || c == '\'' || c == '<' || c == '>' || c == '&')
            {
                return std::string_view::npos;
            }
            ++i;
        }
        if (i == xml.size())
        {
            return std::string_view::npos;
        }
        ++i;
    }
}

// Whether `a` starts with `b`, ASCII letters in either case.
inline bool startsWithNoCase(std::string_view a, std::string_view b)
{
    if (a.size() < b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < b.size(); ++i)
    {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; };
        if (lower(a[i]) != lower(b[i]))
        {
            return false;
        }
    }
    return true;
}
} // namespace xml_detail

/// Whether `xml` nests elements at most `maxDepth` deep as tinyxml reads it, by passes over its
/// bytes that allocate nothing. Every tinyxml Parse in gg-core runs only after this check, which
/// must never count shallower than tinyxml parses: tinyxml parses nested elements recursively,
/// so a crafted document of a million nested elements (a few megabytes) would overflow the stack.
///
/// The count follows tinyxml's own reading (TiXmlNode::Identify and each node's Parse), on a
/// document gg-core parses with TIXML_ENCODING_UNKNOWN (as every caller does):
/// - `<` followed by a letter, `_` or a byte from 0x7f up opens an element, one level deeper. Its
///   tag ends at the first `>` outside an attribute value; a value is quoted after `=` and white
///   space, and ends at its own quote. The element closes at once when `/>` ends the tag.
/// - `</` closes one level (never below 0) and ends at the first `>`.
/// - A comment runs to `-->`, a CDATA section to `]]>`, and any other `<!`, and any `<` followed
///   by anything else (`<1`, `< `), is an unknown node that runs to the first `>`, quotes or
///   not.
/// - Text runs to the next `<`.
///
/// Where tinyxml's reading isn't the bytes' plain reading, the document is refused instead:
/// - `<?` anywhere but in the document's first node: tinyxml reads a declaration's values quoted,
///   so one inside an element could hide its closing tags. In the first node it must be a
///   declaration, `<?xml`, that ends at its first `>` however tinyxml reads it
///   (xml_detail::declarationEnd).
/// - An attribute value that isn't quoted (tinyxml reads one to white space, `/` or `>`), or a
///   byte from 0x80 up between `=` and the quote (white space, in some locales).
/// - A `/` in a tag that doesn't end it: tinyxml stops there with an error.
/// - Any `&#` that doesn't start a well-formed numeric character reference
///   (xml_detail::characterReferencesWellFormed).
/// - After a byte-order mark or a declaration, which turn on tinyxml's UTF-8 reading, a lead byte
///   without its continuation bytes (xml_detail::utf8LeadBytesComplete).
///
/// So it may refuse a document tinyxml would read (a malformed one, or one gg-core never
/// writes), but it never passes one tinyxml would nest deeper than `maxDepth`.
inline bool xmlNestingWithin(std::string_view xml, std::size_t maxDepth = kMaxXmlDepth)
{
    using namespace xml_detail;
    if (!characterReferencesWellFormed(xml))
    {
        return false;
    }
    const std::size_t n = xml.size();
    // The index just past the first `terminator` at or after `from`; n when there is none.
    const auto past = [&](std::size_t from, std::string_view terminator) {
        const std::size_t at = xml.find(terminator, from);
        return at == std::string_view::npos ? n : at + terminator.size();
    };
    bool utf8 = xml.starts_with("\xef\xbb\xbf"); // tinyxml's UTF-8 reading, from a BOM
    bool firstNode = true;
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
        const auto next = rest.size() > 1 ? static_cast<unsigned char>(rest[1]) : 0;
        if (rest.starts_with("<?"))
        {
            if (!firstNode || !startsWithNoCase(rest, "<?xml"))
            {
                return false;
            }
            i = declarationEnd(xml, i);
            if (i == std::string_view::npos)
            {
                return false;
            }
            utf8 = true; // whatever the encoding it names: refusing more is safe
        }
        else if (rest.starts_with("<!--"))
        {
            i = past(i + 4, "-->");
        }
        else if (rest.starts_with("<![CDATA["))
        {
            i = past(i + 9, "]]>");
        }
        else if (rest.starts_with("</"))
        {
            depth = depth > 0 ? depth - 1 : 0;
            i = past(i + 2, ">");
        }
        else if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') || next == '_' ||
                 next >= 0x7f)
        {
            // An element's tag, to its end.
            std::size_t j = i + 1;
            bool selfClosed = false;
            while (j < n && xml[j] != '>')
            {
                if (xml[j] == '/')
                {
                    if (j + 1 < n && xml[j + 1] != '>')
                    {
                        return false;
                    }
                    selfClosed = true;
                }
                else if (xml[j] == '=')
                {
                    std::size_t k = j + 1;
                    while (k < n && isSpace(static_cast<unsigned char>(xml[k])))
                    {
                        ++k;
                    }
                    if (k < n && xml[k] != '"' && xml[k] != '\'')
                    {
                        return false;
                    }
                    if (k < n)
                    {
                        const std::size_t close = xml.find(xml[k], k + 1);
                        j = close == std::string_view::npos ? n : close + 1;
                        continue;
                    }
                }
                ++j;
            }
            // A self-closed element is a level too, while tinyxml parses it.
            if (depth + 1 > maxDepth)
            {
                return false;
            }
            depth += selfClosed ? 0 : 1;
            i = j < n ? j + 1 : n;
        }
        else
        {
            // `<!` (a DOCTYPE, say), or `<` and anything else: tinyxml's unknown node.
            i = past(i + 1, ">");
        }
        firstNode = false;
    }
    return !utf8 || utf8LeadBytesComplete(xml);
}

/// For tests (XmlText.cpp): how deep tinyxml nests elements parsing `xml` as gg-core does
/// (TIXML_ENCODING_UNKNOWN), counting those of a parse that stopped at an error, which tinyxml
/// keeps; the root is 1. Only for a document small enough to parse safely, so the tests can check
/// xmlNestingWithin against tinyxml itself.
std::size_t tinyxmlDepth(std::string_view xml);
} // namespace gg::core

#endif // GG_CORE_XMLTEXT_H
