// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "XmlText.h"

#include <algorithm>
#include <string>

#include "tinyxml/tinyxml.h"

namespace gg::core
{
std::size_t tinyxmlDepth(std::string_view xml)
{
    const std::string text(xml);
    TiXmlDocument doc;
    doc.Parse(text.c_str(), nullptr, TIXML_ENCODING_UNKNOWN);
    // The tree walked without recursion. Only elements have children; every node tinyxml made
    // is linked in, even when its parse failed.
    std::size_t deepest = 0;
    std::size_t depth = 0; // the elements around `node`
    const TiXmlNode *node = doc.FirstChild();
    while (node != nullptr)
    {
        if (node->ToElement() != nullptr)
        {
            deepest = std::max(deepest, depth + 1);
            if (node->FirstChild() != nullptr)
            {
                ++depth;
                node = node->FirstChild();
                continue;
            }
        }
        while (node != nullptr && node->NextSibling() == nullptr)
        {
            node = node->Parent();
            if (node == &doc)
            {
                node = nullptr;
            }
            else
            {
                --depth;
            }
        }
        if (node != nullptr)
        {
            node = node->NextSibling();
        }
    }
    return deepest;
}
} // namespace gg::core
