// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "Patch.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <optional>
#include <system_error>
#include <utility>

#include "tinyxml/tinyxml.h"

namespace gg::core
{
namespace
{
using md_t = sst::basic_blocks::params::ParamMetaData;

constexpr const char *kBitsAttribute = "bits";

// The largest gap between a float and its `v` text read back as a double: half of the 12th
// decimal (5e-13), plus the rounding of reading it, below 1e-13 for any value of magnitude
// under 1000. A `bits` value further from `v` than this isn't the float `v` was written from.
constexpr double kTextTolerance = 1e-12;

// A float's bits as 8 lowercase hex digits, NUL-terminated. Never locale-dependent.
std::array<char, 9> bitsToHex(float v)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    auto u = std::bit_cast<uint32_t>(v);
    std::array<char, 9> out{};
    for (int i = 7; i >= 0; --i)
    {
        out[static_cast<size_t>(i)] = kDigits[u & 0xfu];
        u >>= 4;
    }
    return out;
}

// The float written by bitsToHex: exactly 8 hex digits, or nothing.
std::optional<float> hexToBits(const char *s)
{
    if (s == nullptr || std::strlen(s) != 8)
    {
        return std::nullopt;
    }
    uint32_t u{0};
    const auto [end, ec] = std::from_chars(s, s + 8, u, 16);
    if (ec != std::errc{} || end != s + 8)
    {
        return std::nullopt;
    }
    return std::bit_cast<float>(u);
}

// A whole decimal uint64, or nothing.
std::optional<uint64_t> parseSeed(const char *s)
{
    if (s == nullptr)
    {
        return std::nullopt;
    }
    const auto len = std::strlen(s);
    uint64_t seed{0};
    const auto [end, ec] = std::from_chars(s, s + len, seed, 10);
    if (len == 0 || ec != std::errc{} || end != s + len)
    {
        return std::nullopt;
    }
    return seed;
}

float sanitize(float v, const md_t &m)
{
    if (std::isnan(v))
    {
        return m.defaultVal;
    }
    return std::clamp(v, m.minVal, m.maxVal);
}

// What PatchBase::fromState needs to return true, checked without touching a patch. PatchBase
// itself doesn't check that the XML is well formed: tinyxml keeps what it parsed before an error,
// so a truncated state would load as far as it got.
bool isLoadable(const std::string &xml)
{
    TiXmlDocument doc;
    doc.Parse(xml.c_str());
    if (doc.Error())
    {
        return false;
    }
    const auto *root = doc.FirstChildElement("patch");
    if (root == nullptr)
    {
        return false;
    }
    const char *id = root->Attribute("id");
    if (id == nullptr || std::strcmp(id, Patch::id) != 0)
    {
        return false;
    }
    int version{0};
    if (root->QueryIntAttribute("version", &version) != TIXML_SUCCESS)
    {
        return false;
    }
    return root->FirstChildElement("params") != nullptr;
}

template <size_t... I>
std::array<Param, kParamCount> makeValues(Mode mode, std::index_sequence<I...>)
{
    return {Param{paramMeta(static_cast<ParamSlot>(I), mode)}...};
}
} // namespace

std::array<Param, kParamCount> Patch::defaultValues(Mode mode)
{
    return makeValues(mode, std::make_index_sequence<kParamCount>{});
}

Patch::Patch(Mode mode) : mode_(mode), values(defaultValues(mode))
{
    for (auto &p : values)
    {
        pushSingleParam(&p);
    }

    toAdditionalParamAttributes = [](const Param &p, TiXmlElement &e) {
        e.SetAttribute(kBitsAttribute, bitsToHex(p.value).data());
    };
    // PatchBase has already set the value from `v` (through the migration hook) when it calls
    // this; the exact bits replace it, through the same hook.
    fromAdditionalParamAttributes = [this](Param &p, TiXmlElement *e, uint32_t version) {
        const auto exact = hexToBits(e->Attribute(kBitsAttribute));
        double text{0};
        if (!exact || e->QueryDoubleAttribute("v", &text) != TIXML_SUCCESS)
        {
            return;
        }
        if (!(std::fabs(static_cast<double>(*exact) - text) <= kTextTolerance))
        {
            return;
        }
        p.value = migrateParamValueFromVersion(&p, *exact, version);
    };

    additionalToState = [this](TiXmlElement &root) {
        std::array<char, 24> text{};
        const auto [end, ec] = std::to_chars(text.data(), text.data() + text.size() - 1, seed);
        (void)ec; // 20 digits at most: it always fits
        *end = '\0';
        TiXmlElement engine("engine");
        engine.SetAttribute("seed", text.data());
        root.InsertEndChild(engine);
    };
    additionalFromState = [this](TiXmlElement *root, uint32_t) {
        seed = 0;
        if (const auto *engine = root->FirstChildElement("engine"))
        {
            seed = parseSeed(engine->Attribute("seed")).value_or(0);
        }
    };

    resetToInit();
}

bool Patch::fromState(const std::string &xml)
{
    if (!isLoadable(xml))
    {
        return false;
    }

    // PatchBase::fromState resets the patch it loads into before it parses, so load into a new
    // one and take its values only when the load succeeds.
    Patch loaded(mode_);
    loaded.dawExtraStateFrom = dawExtraStateFrom;
    if (!loaded.PatchBase::fromState(xml))
    {
        return false;
    }

    for (size_t i = 0; i < kParamCount; ++i)
    {
        values[i].value = sanitize(loaded.values[i].value, values[i].meta);
    }
    std::memcpy(name, loaded.name, sizeof(name));
    name[sizeof(name) - 1] = '\0';
    seed = loaded.seed;
    migratedFromVersion = loaded.migratedFromVersion;
    dirty = false;
    return true;
}
} // namespace gg::core
