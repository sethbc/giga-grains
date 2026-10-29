// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_TESTS_SUPPORT_DEEPXML_H
#define GG_TESTS_SUPPORT_DEEPXML_H

#include <cstddef>
#include <string>
#include <vector>

namespace gg::test
{
/// A document that tinyxml reads as a million nested elements, while a scan that doesn't read it
/// as tinyxml does would count it shallow (carry-over: "Deeply nested XML… pre-scan can be
/// bypassed"). Parsed, each overflows the stack. gg-core must refuse every one before tinyxml
/// parses it: the patch (PTCH), the media manifest (MEDI) and the settings file.
///
/// A test builds `head`, then the document's own root element with `body` inside it: `head` goes
/// before everything (a declaration or a byte-order mark), `body` inside the root.
struct DeepXml
{
    const char *name;
    std::string head;
    std::string body;
};

inline std::string repeated(const std::string &s, std::size_t n)
{
    std::string out;
    out.reserve(s.size() * n);
    for (std::size_t i = 0; i < n; ++i)
    {
        out += s;
    }
    return out;
}

/// Every shape, `n` levels deep.
inline std::vector<DeepXml> deepXmlShapes(std::size_t n = 1'000'000)
{
    const std::string open = repeated("<a>", n);
    return {
        // A `<` not followed by a name start is an unknown node to tinyxml, which ends at the
        // first `>` whatever the quotes: the opens are elements, not an attribute's value.
        {"an unknown node's quotes", "", R"(<1 x=")" + open + R"(">)"},
        // A declaration's quoted values hide `>` from a scan that ends it at the first `>`:
        // tinyxml reads `</a>` inside the value, so each <a> stays open.
        {"a declaration in content", "", repeated(R"(<a><?xml version="></a>"?>)", n)},
        // The same at the top: a scan that ends the declaration at its first `>` reads a comment
        // that never ends, and so misses every open after it.
        {"a declaration at the top", R"(<?xml version="><!--"?>)", open},
        // tinyxml's numeric character reference runs to the first `;` and back to its `x`,
        // hiding what lies between: here each </a>.
        {"a character reference", "", repeated("<a>&#x</a>x;", n)},
        // After a declaration, or a UTF-8 byte-order mark, tinyxml reads text as UTF-8: a lead
        // byte takes the next bytes with it, whatever they are; here the `</` of each </a>.
        {"a UTF-8 lead byte after a declaration", R"(<?xml version="1.0"?>)",
         repeated("<a>\xe0</a>", n)},
        {"a UTF-8 lead byte after a byte-order mark", "\xef\xbb\xbf", repeated("<a>\xe0</a>", n)},
    };
}
} // namespace gg::test

#endif // GG_TESTS_SUPPORT_DEEPXML_H
