// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/MachineSettings.h"

#include <cassert>
#include <cstring>
#include <sstream>

#include "XmlText.h"
#include "tinyxml/tinyxml.h"

namespace gg::core
{
namespace
{
// `s` up to its first NUL: tinyxml would write a NUL through, and no XML parser reads one back.
std::string upToNul(const std::string &s)
{
    assert(s.find('\0') == std::string::npos);
    return s.substr(0, s.find('\0'));
}

// The attribute's value, or empty when it's missing.
std::string attribute(const TiXmlElement &e, const char *name)
{
    const char *value = e.Attribute(name);
    return value != nullptr ? std::string(value) : std::string();
}
} // namespace

std::string toSettingsXml(const MachineSettings &settings)
{
    TiXmlDocument doc;
    TiXmlElement root("settings");
    root.SetAttribute("version", kMachineSettingsVersion);
    root.SetAttribute("sampleFolder", escapeAmpersands(upToNul(settings.sampleFolder)));
    root.SetAttribute("lastLoadFolder", escapeAmpersands(upToNul(settings.lastLoadFolder)));
    doc.InsertEndChild(root);
    std::ostringstream oss;
    oss << doc;
    return oss.str();
}

bool fromSettingsXml(std::string_view xml, MachineSettings &out)
{
    if (xml.find('\0') != std::string_view::npos)
    {
        return false;
    }
    // Byte by byte, as parseMedia reads MEDI (SessionState.cpp): every byte but NUL comes back
    // unchanged, valid UTF-8 or not. tinyxml stops quietly at top-level text it can't read, and
    // returns where it stopped; having read everything, it returns null.
    const std::string text(xml);
    TiXmlDocument doc;
    const char *stopped = doc.Parse(text.c_str(), nullptr, TIXML_ENCODING_UNKNOWN);
    if (doc.Error() || (stopped != nullptr && *stopped != '\0'))
    {
        return false;
    }
    // The <settings> root, alone at the top level, at any version: a newer one still holds the
    // folders this build knows.
    const TiXmlElement *root = doc.RootElement();
    int version{0};
    if (root == nullptr || std::strcmp(root->Value(), "settings") != 0 ||
        doc.FirstChild() != root || root->NextSibling() != nullptr ||
        root->QueryIntAttribute("version", &version) != TIXML_SUCCESS || version < 1)
    {
        return false;
    }
    out.sampleFolder = attribute(*root, "sampleFolder");
    out.lastLoadFolder = attribute(*root, "lastLoadFolder");
    return true;
}
} // namespace gg::core
