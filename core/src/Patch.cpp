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

// These parsers return whether they read a value into `out` (left alone otherwise), rather than
// a std::optional: GCC 13's -Wmaybe-uninitialized at -O3 misreads an optional's payload as
// uninitialized once it is inlined into a longer condition (CI's Linux leg).

// The float written by bitsToHex: exactly 8 hex digits.
bool hexToBits(const char *s, float &out)
{
    if (s == nullptr || std::strlen(s) != 8)
    {
        return false;
    }
    uint32_t u{0};
    const auto [end, ec] = std::from_chars(s, s + 8, u, 16);
    if (ec != std::errc{} || end != s + 8)
    {
        return false;
    }
    out = std::bit_cast<float>(u);
    return true;
}

// A whole decimal unsigned integer that fits a T.
template <typename T> bool parseDecimal(const char *s, T &out)
{
    if (s == nullptr)
    {
        return false;
    }
    const auto len = std::strlen(s);
    T n{0};
    const auto [end, ec] = std::from_chars(s, s + len, n, 10);
    if (len == 0 || ec != std::errc{} || end != s + len)
    {
        return false;
    }
    out = n;
    return true;
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

// Neither a layer (0 to 3) nor kSameLayer or kAllLayers: what layerNamed returns for any other
// text.
constexpr uint8_t kNoLayer = 0xFD;
static_assert(kNoLayer >= kLayers && kNoLayer != kSameLayer && kNoLayer != kAllLayers);

// The layer `s` names: A to D, or `special` for `specialName`; kNoLayer for anything else.
uint8_t layerNamed(const char *s, uint8_t special, const char *specialName)
{
    if (s == nullptr)
    {
        return kNoLayer;
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
    return kNoLayer;
}

// The float a `bits` attribute holds, into `out`, when it agrees with its value's text (`v`,
// `depth`); false otherwise, and the text stands.
bool exactBits(const char *bits, double text, float &out)
{
    float exact{0.0f};
    if (!hexToBits(bits, exact) ||
        !(std::fabs(static_cast<double>(exact) - text) <= kTextTolerance))
    {
        return false;
    }
    out = exact;
    return true;
}

// One <route>, written as gg/core/PatchState.h describes. `r` must be isSavableRoute.
TiXmlElement routeElement(size_t slot, const ModRoute &r)
{
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

// The route a <route> element holds, or an empty slot (ModRoute{}) when any attribute is missing
// or malformed (gg/core/PatchState.h). Its `slot` is the caller's. Every field is held as a plain
// value, its enumeration's Count (or kNoLayer) when unread, never as a std::optional (GCC 13's
// -Wmaybe-uninitialized, above).
ModRoute parseRoute(const TiXmlElement &e)
{
    ModSource source = ModSource::Count;
    if (const char *name = e.Attribute("source"))
    {
        if (const auto named = modSourceNamed(name))
        {
            source = *named;
        }
    }
    ModCurve curve = ModCurve::Count;
    if (const char *name = e.Attribute("curve"))
    {
        if (const auto named = modCurveNamed(name))
        {
            curve = *named;
        }
    }
    ParamSlot target = ParamSlot::Count;
    if (clap_id id{0}; parseDecimal(e.Attribute("target"), id))
    {
        if (const auto slot = slotForId(id))
        {
            target = *slot;
        }
    }
    const uint8_t sourceLayer = layerNamed(e.Attribute("sourceLayer"), kSameLayer, kSameLayerName);
    const uint8_t targetLayer = layerNamed(e.Attribute("targetLayer"), kAllLayers, kAllLayersName);
    double text{0};
    if (source == ModSource::Count || curve == ModCurve::Count || target == ParamSlot::Count ||
        !isValidTarget(target) || sourceLayer == kNoLayer || targetLayer == kNoLayer ||
        e.QueryDoubleAttribute("depth", &text) != TIXML_SUCCESS || !std::isfinite(text))
    {
        return ModRoute{};
    }
    float depth = static_cast<float>(text);
    exactBits(e.Attribute(kBitsAttribute), text, depth); // the exact float, when it agrees
    if (!std::isfinite(depth))
    {
        return ModRoute{}; // finite as a double, beyond a float's range
    }
    return ModRoute{source, sourceLayer, target, targetLayer, depth, curve};
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
        uint32_t slot{0};
        if (parseDecimal(e->Attribute("slot"), slot) && slot >= 1 && slot <= kModSlots)
        {
            routes[slot - 1] = parseRoute(*e);
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

// The text of a <preset> attribute: its bytes to the first NUL (which an attribute can't hold),
// every `&` escaped as MEDI's paths are (gg/core/SessionState.h).
std::string attributeText(const std::string &s) { return escapeAmpersands(std::string(s.c_str())); }

// The <preset> element of `info` (gg/core/PatchState.h). Its name is the root's.
TiXmlElement presetElement(const PresetInfo &info)
{
    std::string tags;
    for (const auto &tag : info.tags)
    {
        tags += tags.empty() ? "" : " ";
        tags += tag;
    }
    TiXmlElement e("preset");
    e.SetAttribute("product", modeName(info.product));
    e.SetAttribute("author", attributeText(info.author));
    e.SetAttribute("description", attributeText(info.description));
    e.SetAttribute("tags", attributeText(tags));
    return e;
}

// M1's instrument default for Macro 7 (Macro Stereo), which an M1 state without it, or with a
// NaN, loaded at (spec §6.10).
constexpr float kM1Macro7Default = 0.5f;
} // namespace

static_assert(sizeof(Patch::name) == kMaxPatchNameBytes + 1);

std::string patchName(std::string_view name)
{
    name = name.substr(0, name.find('\0'));
    if (name.size() <= kMaxPatchNameBytes)
    {
        return std::string(name);
    }
    size_t n = kMaxPatchNameBytes;
    // name[n] is the first byte cut: a continuation byte there means the character straddles
    // the cut.
    for (int back = 0; back < 3 && n > 0 && (static_cast<unsigned char>(name[n]) & 0xc0u) == 0x80u;
         ++back)
    {
        --n;
    }
    return std::string(name.substr(0, n));
}

bool readPresetElement(const TiXmlElement &root, std::string_view name, PresetInfo &out)
{
    const TiXmlElement *e = root.FirstChildElement("preset");
    if (e == nullptr)
    {
        return false;
    }
    const char *product = e->Attribute("product");
    const auto mode = modeNamed(product != nullptr ? product : "");
    if (!mode)
    {
        return false;
    }
    const auto text = [&](const char *attribute) {
        const char *v = e->Attribute(attribute);
        return std::string(v != nullptr ? v : "");
    };
    PresetInfo info;
    info.name = std::string(name);
    info.product = *mode;
    info.author = text("author");
    info.description = text("description");
    const std::string tags = text("tags");
    for (size_t from = 0; from <= tags.size();)
    {
        const size_t space = std::min(tags.find(' ', from), tags.size());
        const std::string_view word = std::string_view(tags).substr(from, space - from);
        if (isPresetTag(word))
        {
            info.tags.emplace_back(word);
        }
        from = space + 1;
    }
    out = std::move(info);
    return true;
}

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
        if (float exact{0.0f}; exactBits(e->Attribute(kBitsAttribute), text, exact))
        {
            p.value = migrateParamValueFromVersion(&p, exact, version);
        }
    };

    additionalToState = [this](TiXmlElement &root) {
        // PatchBase wrote `name` as it is; escaped, any text comes back unchanged.
        root.SetAttribute("name", escapeAmpersands(name));

        TiXmlElement engine("engine");
        engine.SetAttribute("seed", decimal(seed).data());
        root.InsertEndChild(engine);

        TiXmlElement mod("mod");
        for (size_t k = 0; k < kModSlots; ++k)
        {
            if (isSavableRoute(routes[k]))
            {
                mod.InsertEndChild(routeElement(k, routes[k]));
            }
        }
        root.InsertEndChild(mod);

        if (preset)
        {
            root.InsertEndChild(presetElement(*preset));
        }
    };
    additionalFromState = [this](TiXmlElement *root, uint32_t version) {
        seed = 0;
        if (const auto *engine = root->FirstChildElement("engine"))
        {
            parseDecimal(engine->Attribute("seed"), seed); // left 0 when malformed
        }
        // Version 1 has no <mod>: its routes are the migration's, so any <mod> it holds is
        // ignored.
        routes = version >= 2 ? parseMod(root->FirstChildElement("mod")) : ModRoutes{};

        // PatchBase kept the name's first 255 bytes, wherever they end; keep them to a whole
        // character. With no `name`, it stays "Init".
        if (const char *text = root->Attribute("name"))
        {
            const std::string kept = patchName(text);
            std::memset(name, 0, sizeof(name));
            std::memcpy(name, kept.data(), kept.size());
        }
        preset.reset();
        if (PresetInfo info; readPresetElement(*root, name, info))
        {
            preset = std::move(info);
        }
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
    preset = std::move(loaded.preset);
    migratedFromVersion = loaded.migratedFromVersion;
    dirty = false;
    return true;
}

bool presetInfoOfXml(std::string_view xml, PresetInfo &info)
{
    // What isLoadable checks of the document and its root, but for <params> and the version,
    // which only the values need.
    const std::string text(xml);
    if (!xmlNestingWithin(text))
    {
        return false; // tinyxml's recursion would overflow the stack
    }
    TiXmlDocument doc;
    doc.Parse(text.c_str());
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
    const char *name = root->Attribute("name");
    return readPresetElement(*root, patchName(name != nullptr ? name : "Init"), info);
}
} // namespace gg::core
