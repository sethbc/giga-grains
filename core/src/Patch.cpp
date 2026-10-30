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
#include <string_view>
#include <system_error>
#include <utility>

#include "XmlText.h"
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

// A whole decimal unsigned integer that fits a T, or nothing.
template <typename T> std::optional<T> parseDecimal(const char *s)
{
    if (s == nullptr)
    {
        return std::nullopt;
    }
    const auto len = std::strlen(s);
    T n{0};
    const auto [end, ec] = std::from_chars(s, s + len, n, 10);
    if (len == 0 || ec != std::errc{} || end != s + len)
    {
        return std::nullopt;
    }
    return n;
}

// The decimal text of an unsigned integer, NUL-terminated. Never locale-dependent.
std::array<char, 24> decimal(uint64_t n)
{
    std::array<char, 24> text{};
    const auto [end, ec] = std::to_chars(text.data(), text.data() + text.size() - 1, n);
    (void)ec; // 20 digits at most: it always fits
    *end = '\0';
    return text;
}

// A route's layer attributes (gg/core/PatchState.h): A to D, and `special`'s word, "same" for a
// sourceLayer (kSameLayer) or "all" for a targetLayer (kAllLayers).
constexpr std::array<const char *, kLayers> kLayerNames{"A", "B", "C", "D"};
constexpr const char *kSameLayerName = "same";
constexpr const char *kAllLayersName = "all";

const char *layerName(uint8_t layer, uint8_t special, const char *specialName)
{
    if (layer == special)
    {
        return specialName;
    }
    return layer < kLayers ? kLayerNames[layer] : "";
}

std::optional<uint8_t> layerNamed(const char *s, uint8_t special, const char *specialName)
{
    if (s == nullptr)
    {
        return std::nullopt;
    }
    const std::string_view text(s);
    if (text == specialName)
    {
        return special;
    }
    for (uint8_t layer = 0; layer < kLayers; ++layer)
    {
        if (text == kLayerNames[layer])
        {
            return layer;
        }
    }
    return std::nullopt;
}

// The float a `bits` attribute holds, when it agrees with its value's text (`v`, `depth`); nothing
// otherwise, and the text stands.
std::optional<float> exactBits(const char *bits, double text)
{
    const auto exact = hexToBits(bits);
    if (!exact || !(std::fabs(static_cast<double>(*exact) - text) <= kTextTolerance))
    {
        return std::nullopt;
    }
    return exact;
}

// One <route>, written as gg/core/PatchState.h describes; nothing when isSavableRoute refuses it.
std::optional<TiXmlElement> routeElement(size_t slot, const ModRoute &r)
{
    if (!isSavableRoute(r))
    {
        return std::nullopt;
    }
    TiXmlElement e("route");
    e.SetAttribute("slot", decimal(slot + 1).data());
    e.SetAttribute("source", modSourceName(r.source));
    e.SetAttribute("sourceLayer", layerName(r.sourceLayer, kSameLayer, kSameLayerName));
    e.SetAttribute("target", decimal(paramId(r.target)).data());
    e.SetAttribute("targetLayer", layerName(r.targetLayer, kAllLayers, kAllLayersName));
    e.SetDoubleAttribute("depth", static_cast<double>(r.depth));
    e.SetAttribute(kBitsAttribute, bitsToHex(r.depth).data());
    e.SetAttribute("curve", modCurveName(r.curve));
    return e;
}

// The route a <route> element holds, or nothing when any attribute is missing or malformed
// (gg/core/PatchState.h). Its `slot` is the caller's.
std::optional<ModRoute> parseRoute(const TiXmlElement &e)
{
    const auto source =
        e.Attribute("source") != nullptr ? modSourceNamed(e.Attribute("source")) : std::nullopt;
    const auto sourceLayer = layerNamed(e.Attribute("sourceLayer"), kSameLayer, kSameLayerName);
    const auto targetId = parseDecimal<clap_id>(e.Attribute("target"));
    const auto target = targetId ? slotForId(*targetId) : std::nullopt;
    const auto targetLayer = layerNamed(e.Attribute("targetLayer"), kAllLayers, kAllLayersName);
    const auto curve =
        e.Attribute("curve") != nullptr ? modCurveNamed(e.Attribute("curve")) : std::nullopt;
    double text{0};
    if (!source || !sourceLayer || !target || !isValidTarget(*target) || !targetLayer || !curve ||
        e.QueryDoubleAttribute("depth", &text) != TIXML_SUCCESS || !std::isfinite(text))
    {
        return std::nullopt;
    }
    const float depth =
        exactBits(e.Attribute(kBitsAttribute), text).value_or(static_cast<float>(text));
    if (!std::isfinite(depth))
    {
        return std::nullopt; // finite as a double, beyond a float's range
    }
    return ModRoute{*source, *sourceLayer, *target, *targetLayer, depth, *curve};
}

// The routes of a <mod> element: each <route> with a `slot` of 1 to kModSlots sets that slot,
// to its route or, when the rest of it is malformed, to an empty one; in document order, so the
// later of two naming one slot wins. Anything else inside <mod> is ignored.
ModRoutes parseMod(const TiXmlElement *mod)
{
    ModRoutes routes{};
    if (mod == nullptr)
    {
        return routes;
    }
    for (const auto *e = mod->FirstChildElement("route"); e != nullptr;
         e = e->NextSiblingElement("route"))
    {
        const auto slot = parseDecimal<uint32_t>(e->Attribute("slot"));
        if (slot && *slot >= 1 && *slot <= kModSlots)
        {
            routes[*slot - 1] = parseRoute(*e).value_or(ModRoute{});
        }
    }
    return routes;
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
    // Before any parse, here or in PatchBase::fromState: tinyxml's recursion would overflow the
    // stack on a deeply nested document.
    if (!xmlNestingWithin(xml))
    {
        return false;
    }
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

// M1's instrument default for Macro 7 (Macro Stereo), which an M1 state without it, or with a
// NaN, loaded at (spec §6.10).
constexpr float kM1Macro7Default = 0.5f;
} // namespace

bool isSavableRoute(const ModRoute &route) noexcept
{
    const auto layerOk = [](uint8_t layer, uint8_t special) {
        return layer < kLayers || layer == special;
    };
    return route.source != ModSource::None && route.source < ModSource::Count &&
           route.curve < ModCurve::Count && isValidTarget(route.target) &&
           layerOk(route.sourceLayer, kSameLayer) && layerOk(route.targetLayer, kAllLayers) &&
           std::isfinite(route.depth);
}

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
        double text{0};
        if (e->QueryDoubleAttribute("v", &text) != TIXML_SUCCESS)
        {
            return;
        }
        if (const auto exact = exactBits(e->Attribute(kBitsAttribute), text))
        {
            p.value = migrateParamValueFromVersion(&p, *exact, version);
        }
    };

    additionalToState = [this](TiXmlElement &root) {
        TiXmlElement engine("engine");
        engine.SetAttribute("seed", decimal(seed).data());
        root.InsertEndChild(engine);

        TiXmlElement mod("mod");
        for (size_t k = 0; k < kModSlots; ++k)
        {
            if (const auto route = routeElement(k, routes[k]))
            {
                mod.InsertEndChild(*route);
            }
        }
        root.InsertEndChild(mod);
    };
    additionalFromState = [this](TiXmlElement *root, uint32_t version) {
        seed = 0;
        if (const auto *engine = root->FirstChildElement("engine"))
        {
            seed = parseDecimal<uint64_t>(engine->Attribute("seed")).value_or(0);
        }
        // Version 1 has no <mod>: its routes are the migration's, so any <mod> it holds is
        // ignored.
        routes = version >= 2 ? parseMod(root->FirstChildElement("mod")) : ModRoutes{};
    };

    resetToInit();
}

void Patch::migratePatchFromVersion(uint32_t fromVersion)
{
    migratedFromVersion = fromVersion;
    if (fromVersion >= 2)
    {
        return; // a newer version: what loaded stands
    }
    routes = factoryRoutes(mode_);
    if (mode_ != Mode::Instrument)
    {
        return; // the FX's Macro 7 was Macro Feedback in M1 already
    }
    // The exact floats loaded (PatchBase has applied `bits`), clamped to 0–1 as M1 loaded them,
    // a NaN at M1's default. Stereo's range and default are M1's: 0–1, 0.5.
    const auto asM1Loaded = [](float v, float fallback) {
        return std::isnan(v) ? fallback : std::clamp(v, 0.0f, 1.0f);
    };
    Param &stereo = param(ParamSlot::Stereo);
    Param &macro7 = param(ParamSlot::Macro7);
    const float v = asM1Loaded(m1Macro7Loaded_ ? macro7.value : kM1Macro7Default, kM1Macro7Default);
    const float base = asM1Loaded(stereo.value, stereo.meta.defaultVal);
    stereo.value = std::clamp(base + 0.5f * (2.0f * v - 1.0f), 0.0f, 1.0f);
    macro7.value = 0.0f; // Macro Feedback's rest
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
    routes = loaded.routes;
    migratedFromVersion = loaded.migratedFromVersion;
    dirty = false;
    return true;
}
} // namespace gg::core
