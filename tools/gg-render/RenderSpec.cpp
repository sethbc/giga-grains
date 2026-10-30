// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "RenderSpec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "Signals.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/SessionState.h"
#include "gg/media/Utf8Path.h"
#include "sst/basic-blocks/params/ParamMetadata.h"

namespace gg::render
{
namespace
{
using json = nlohmann::json;

/// Thrown while reading an invalid spec; parseRenderSpec turns it into the error message.
struct SpecError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// v as a finite number; `key` names it in the error. (JSON can't spell NaN or infinity, but a
/// literal like 1e999 overflows.)
double finiteNumber(const json &v, std::string_view key)
{
    const double d = v.is_number() ? v.get<double>() : 0.0;
    if (!v.is_number() || !std::isfinite(d))
    {
        throw SpecError(fmt::format("{} must be a finite number", key));
    }
    return d;
}

/// One JSON object of the spec. Keys are reported with the object's prefix, e.g.
/// "stream.gain".
class Object
{
  public:
    /// Requires j to be an object whose keys are all in `known`.
    Object(const json &j, std::string_view name, std::string prefix,
           std::initializer_list<std::string_view> known)
        : j_(j), prefix_(std::move(prefix))
    {
        if (!j_.is_object())
        {
            throw SpecError(fmt::format("{} must be a JSON object", name));
        }
        for (const auto &item : j_.items())
        {
            if (std::find(known.begin(), known.end(), item.key()) == known.end())
            {
                throw SpecError(fmt::format("unknown key {}{}", prefix_, item.key()));
            }
        }
    }

    bool has(std::string_view key) const { return j_.find(key) != j_.end(); }

    const json &member(std::string_view key) const
    {
        const auto it = j_.find(key);
        if (it == j_.end())
        {
            throw SpecError(fmt::format("{}{} is missing", prefix_, key));
        }
        return *it;
    }

    /// A finite number.
    double number(std::string_view key) const
    {
        return finiteNumber(member(key), fmt::format("{}{}", prefix_, key));
    }

    /// A number in (0, max].
    double positive(std::string_view key, double max = std::numeric_limits<double>::max()) const
    {
        const double d = number(key);
        if (!(d > 0.0 && d <= max))
        {
            throw SpecError(
                max == std::numeric_limits<double>::max()
                    ? fmt::format("{}{} must be > 0", prefix_, key)
                    : fmt::format("{}{} must be > 0 and at most {}", prefix_, key, max));
        }
        return d;
    }

    /// A JSON integer >= 0 (not a float, even an integral one).
    uint64_t unsignedInteger(std::string_view key) const
    {
        const json &v = member(key);
        if (!v.is_number_unsigned())
        {
            throw SpecError(fmt::format("{}{} must be an integer >= 0", prefix_, key));
        }
        return v.get<uint64_t>();
    }

    /// A JSON integer from 0 to max (not a float, even an integral one).
    uint64_t integerUpTo(std::string_view key, uint64_t max) const
    {
        const json &v = member(key);
        if (!v.is_number_unsigned() || v.get<uint64_t>() > max)
        {
            throw SpecError(fmt::format("{}{} must be an integer from 0 to {}", prefix_, key, max));
        }
        return v.get<uint64_t>();
    }

    std::string string(std::string_view key) const
    {
        const json &v = member(key);
        if (!v.is_string())
        {
            throw SpecError(fmt::format("{}{} must be a string", prefix_, key));
        }
        return v.get<std::string>();
    }

  private:
    const json &j_;
    std::string prefix_;
};

/// Which signal readSource reads: a sample-mode "source", an instrument's "sample" (or a layer's,
/// in "samples") or an FX "input". The last two are made at the render's rate, so have no
/// "sampleRate".
enum class SignalRole
{
    Source,
    Sample,
    Input,
};

/// The signal `j`, named in errors by `key` ("samples.B"), or by its role's key when that is
/// empty.
SourceSpec readSource(const json &j, SignalRole role, std::string key = {})
{
    const std::string name = !key.empty()                 ? std::move(key)
                             : role == SignalRole::Source ? "source"
                             : role == SignalRole::Sample ? "sample"
                                                          : "input";
    const Object o =
        role == SignalRole::Source
            ? Object(j, name, name + ".",
                     {"kind", "frequencyHz", "startHz", "endHz", "seconds", "amplitude",
                      "sampleRate", "path"})
            : Object(j, name, name + ".",
                     {"kind", "frequencyHz", "startHz", "endHz", "seconds", "amplitude", "path"});
    SourceSpec s;
    const std::string kind = o.string("kind");
    // Each kind takes only its own keys (carry-over [66]): a key only another kind reads would go
    // unread, so a sine's "startHz": "x" would parse. A built-in kind lists "sampleRate", which
    // Object has already refused unless this is a sample-mode source.
    const auto onlyKeys = [&](std::initializer_list<std::string_view> own) {
        for (const auto &item : j.items())
        {
            if (std::find(own.begin(), own.end(), item.key()) == own.end())
            {
                throw SpecError(
                    fmt::format(R"({}.{} doesn't apply to kind "{}")", name, item.key(), kind));
            }
        }
    };
    if (kind == "sine")
    {
        onlyKeys({"kind", "frequencyHz", "seconds", "amplitude", "sampleRate"});
        s.kind = SourceKind::Sine;
        s.frequencyHz = o.positive("frequencyHz");
    }
    else if (kind == "sweep")
    {
        onlyKeys({"kind", "startHz", "endHz", "seconds", "amplitude", "sampleRate"});
        s.kind = SourceKind::Sweep;
        s.startHz = o.positive("startHz");
        s.endHz = o.positive("endHz");
    }
    else if (kind == "noise")
    {
        onlyKeys({"kind", "seconds", "amplitude", "sampleRate"});
        s.kind = SourceKind::Noise;
    }
    else if (kind == "file")
    {
        onlyKeys({"kind", "path"});
        s.kind = SourceKind::File;
        s.path = o.string("path");
        if (s.path.empty())
        {
            throw SpecError(fmt::format("{}.path must not be empty", name));
        }
        return s;
    }
    else
    {
        throw SpecError(fmt::format(
            R"({}.kind must be "sine", "sweep", "noise" or "file", not "{}")", name, kind));
    }
    s.seconds = o.positive("seconds", kMaxSeconds);
    s.amplitude = o.number("amplitude");
    if (!(s.amplitude >= 0.0))
    {
        throw SpecError(fmt::format("{}.amplitude must be >= 0", name));
    }
    if (o.has("sampleRate"))
    {
        s.sampleRate = o.positive("sampleRate", kMaxSampleRate);
    }
    return s;
}

StreamSpec readStream(const json &j)
{
    const Object o(j, "stream", "stream.",
                   {"grainRateHz", "grainDurationMs", "position", "positionJitter",
                    "pitchSemitones", "gain", "distribution", "quality"});
    StreamSpec s;
    s.grainRateHz = o.number("grainRateHz");
    s.grainDurationMs = o.number("grainDurationMs");
    if (s.grainDurationMs > kMaxSeconds * 1000.0)
    {
        // The release tail lasts up to one grain, so this also bounds the render's length.
        throw SpecError(
            fmt::format("stream.grainDurationMs must be at most {}", kMaxSeconds * 1000.0));
    }
    s.position = o.number("position");
    s.positionJitter = o.number("positionJitter");
    s.pitchSemitones = o.number("pitchSemitones");
    s.gain = o.number("gain");
    if (o.has("distribution"))
    {
        s.distribution = o.number("distribution");
    }
    if (o.has("quality"))
    {
        const json &q = o.member("quality");
        if (q == "eco")
        {
            s.quality = Tier::Eco;
        }
        else if (q != "normal")
        {
            throw SpecError(R"(stream.quality must be "eco" or "normal")");
        }
    }
    return s;
}

/// Layer A's slot of the layer parameter whose M1 name, before layers named their parameters with
/// their letter ("Size" for "A Size"; M2 sub-project 1's spec §6.9), is `name`, so that every M1
/// spec still parses; nothing for any other name.
std::optional<gg::core::ParamSlot> m1LayerAlias(std::string_view name)
{
    static constexpr std::array<std::string_view, 11> kM1Names{
#define GG_M1_NAME(enumName, index, display) display,
        GG_LAYER_PARAMS_M1(GG_M1_NAME)
#undef GG_M1_NAME
    };
    for (std::size_t i = 0; i < kM1Names.size(); ++i)
    {
        if (kM1Names[i] == name)
        {
            return gg::core::layerSlot(static_cast<gg::core::LayerParam>(i), 0);
        }
    }
    return std::nullopt;
}

/// `mode`'s product's parameter names, for error messages: "Mix, Level, A Position, ..., Level D".
std::string parameterNames(gg::core::Mode mode)
{
    std::string names;
    for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        names += (s == 0 ? "" : ", ") +
                 gg::core::paramMeta(static_cast<gg::core::ParamSlot>(s), mode).name;
    }
    return names;
}

/// The parameter named `name` in `mode`'s product: paramSlotNamed's, unless that name is the other
/// product's alone ("Macro Level" in an FX spec, say). A bare M1 name ("Size") is layer A's in
/// either product.
std::optional<gg::core::ParamSlot> paramSlotNamedIn(std::string_view name, gg::core::Mode mode)
{
    if (const auto alias = m1LayerAlias(name))
    {
        return alias;
    }
    const auto slot = paramSlotNamed(name);
    if (slot && gg::core::paramMeta(*slot, mode).name == name)
    {
        return slot;
    }
    return std::nullopt;
}

/// v as a value for the parameter in `slot` of `mode`'s product: a finite number that, converted
/// to float as the Processor converts it, is in the parameter's range. `key` names it in the
/// error.
double parameterValue(const json &v, gg::core::ParamSlot slot, gg::core::Mode mode,
                      std::string_view key)
{
    const double d = finiteNumber(v, key);
    const auto &meta = gg::core::paramMeta(slot, mode);
    // The float conversion is defined only within float's range.
    const bool fits = std::abs(d) <= std::numeric_limits<float>::max();
    const float f = fits ? static_cast<float>(d) : 0.0f;
    if (!fits || !(f >= meta.minVal && f <= meta.maxVal))
    {
        throw SpecError(fmt::format("{} must be in {}'s range, {} to {}", key, meta.name,
                                    meta.minVal, meta.maxVal));
    }
    return d;
}

std::vector<std::pair<std::string, double>> readParams(const json &j, gg::core::Mode mode)
{
    if (!j.is_object())
    {
        throw SpecError("params must be a JSON object");
    }
    std::vector<std::pair<std::string, double>> params;
    for (const auto &item : j.items())
    {
        const std::string key = fmt::format("params.{}", item.key());
        const auto slot = paramSlotNamedIn(item.key(), mode);
        if (!slot)
        {
            throw SpecError(
                fmt::format("unknown key {}: the parameters are {}", key, parameterNames(mode)));
        }
        params.emplace_back(item.key(), parameterValue(item.value(), *slot, mode, key));
    }
    return params;
}

/// The message for a time that isn't >= 0 or doesn't round to a frame before the render's end.
std::string timeError(std::string_view name)
{
    return fmt::format(
        "{}.time must be >= 0 and round to a frame before the render's end (durationSeconds)",
        name);
}

/// Whether `seconds` is >= 0 and rounds to a frame before frame `end`, durationSeconds' frame.
bool beforeEnd(double seconds, double durationSeconds, double sampleRate, int64_t end)
{
    // The first test keeps framesFor's argument in range.
    return seconds >= 0.0 && seconds <= durationSeconds && framesFor(seconds, sampleRate) < end;
}

std::vector<AutomationPoint> readAutomation(const json &j, gg::core::Mode mode, double sampleRate,
                                            double durationSeconds)
{
    if (!j.is_array())
    {
        throw SpecError("automation must be a JSON array");
    }
    const int64_t end = framesFor(durationSeconds, sampleRate);
    std::vector<AutomationPoint> points;
    for (std::size_t i = 0; i < j.size(); ++i)
    {
        const std::string name = fmt::format("automation[{}]", i);
        const Object o(j[i], name, name + ".", {"time", "param", "value"});
        AutomationPoint p;
        p.timeSeconds = o.number("time");
        if (!beforeEnd(p.timeSeconds, durationSeconds, sampleRate, end))
        {
            throw SpecError(timeError(name));
        }
        if (!points.empty() && p.timeSeconds < points.back().timeSeconds)
        {
            throw SpecError(fmt::format("{}.time is before automation[{}].time: list the points "
                                        "in time order",
                                        name, i - 1));
        }
        p.param = o.string("param");
        const auto slot = paramSlotNamedIn(p.param, mode);
        if (!slot)
        {
            throw SpecError(fmt::format(R"({}.param must be one of {}, not "{}")", name,
                                        parameterNames(mode), p.param));
        }
        // Ring Length is sized before the render and is neither automatable nor modulatable.
        if (const auto ref = gg::core::layerOf(*slot);
            ref && ref->param == gg::core::LayerParam::RingLength)
        {
            throw SpecError(fmt::format("{}.param: Ring Length can't be automated; set it in "
                                        "params",
                                        name));
        }
        p.value = parameterValue(o.member("value"), *slot, mode, name + ".value");
        points.push_back(std::move(p));
    }
    return points;
}

std::vector<NoteSpec> readNotes(const json &j, double sampleRate, double durationSeconds)
{
    if (!j.is_array())
    {
        throw SpecError("notes must be a JSON array");
    }
    const int64_t end = framesFor(durationSeconds, sampleRate);
    std::vector<NoteSpec> notes;
    for (std::size_t i = 0; i < j.size(); ++i)
    {
        const std::string name = fmt::format("notes[{}]", i);
        const Object o(j[i], name, name + ".", {"time", "key", "duration", "channel", "velocity"});
        NoteSpec n;
        n.timeSeconds = o.number("time");
        if (!beforeEnd(n.timeSeconds, durationSeconds, sampleRate, end))
        {
            throw SpecError(timeError(name));
        }
        if (!notes.empty() && n.timeSeconds < notes.back().timeSeconds)
        {
            throw SpecError(fmt::format(
                "{}.time is before notes[{}].time: list the notes in time order", name, i - 1));
        }
        n.key = static_cast<uint8_t>(o.integerUpTo("key", 127));
        n.durationSeconds = o.positive("duration", kMaxSeconds);
        // Ruling R17: an Off on its On's frame would sort before it and leave the key held.
        if (framesFor(n.timeSeconds + n.durationSeconds, sampleRate) ==
            framesFor(n.timeSeconds, sampleRate))
        {
            throw SpecError(fmt::format("{}.duration is shorter than one frame at the render's "
                                        "rate: the note would end on the frame it starts",
                                        name));
        }
        if (o.has("channel"))
        {
            n.channel = static_cast<uint8_t>(o.integerUpTo("channel", 15));
        }
        if (o.has("velocity"))
        {
            n.velocity = static_cast<uint8_t>(o.integerUpTo("velocity", 127));
        }
        notes.push_back(n);
    }
    return notes;
}

/// The layer (0 to 3) that the letter `text` ("A" to "D") names; nothing for any other text.
std::optional<uint8_t> layerOfLetter(std::string_view text)
{
    if (text.size() == 1 && text[0] >= 'A' && text[0] < 'A' + gg::core::kLayers)
    {
        return static_cast<uint8_t>(text[0] - 'A');
    }
    return std::nullopt;
}

/// "samples": a source per layer letter, in layer order.
std::vector<std::pair<uint8_t, SourceSpec>> readLayerSamples(const json &j)
{
    const Object o(j, "samples", "samples.", {"A", "B", "C", "D"});
    static_assert(gg::core::kLayers == 4, "the letters above are the layers'");
    std::vector<std::pair<uint8_t, SourceSpec>> samples;
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        const std::string letter(1, static_cast<char>('A' + layer));
        if (o.has(letter))
        {
            samples.emplace_back(
                layer, readSource(o.member(letter), SignalRole::Sample, "samples." + letter));
        }
    }
    return samples;
}

/// Every source's name (gg::core::modSourceName), for error messages: "None, Macro 1, ...".
std::string modSourceNames()
{
    std::string names;
    for (std::size_t s = 0; s < static_cast<std::size_t>(gg::core::ModSource::Count); ++s)
    {
        names += (s == 0 ? "" : ", ");
        names += gg::core::modSourceName(static_cast<gg::core::ModSource>(s));
    }
    return names;
}

/// Every curve's name (gg::core::modCurveName), for error messages.
std::string modCurveNames()
{
    std::string names;
    for (std::size_t c = 0; c < static_cast<std::size_t>(gg::core::ModCurve::Count); ++c)
    {
        names += (c == 0 ? "" : ", ");
        names += gg::core::modCurveName(static_cast<gg::core::ModCurve>(c));
    }
    return names;
}

/// One route of "mod", named in errors by `name` ("mod[3]"), with the parameter names of `mode`'s
/// product. See RouteSpec.
RouteSpec readRoute(const json &j, const std::string &name, gg::core::Mode mode)
{
    const Object o(j, name, name + ".",
                   {"slot", "source", "sourceLayer", "target", "targetLayer", "depth", "curve"});
    RouteSpec r;
    const json &slot = o.member("slot");
    if (!slot.is_number_unsigned() || slot.get<uint64_t>() < 1 ||
        slot.get<uint64_t>() > gg::core::kModSlots)
    {
        throw SpecError(
            fmt::format("{}.slot must be an integer from 1 to {}", name, gg::core::kModSlots));
    }
    r.slot = static_cast<uint8_t>(slot.get<uint64_t>());
    r.source = o.string("source");
    const auto source = gg::core::modSourceNamed(r.source);
    if (!source)
    {
        throw SpecError(
            fmt::format(R"({}.source is not a modulation source: "{}" (the sources are )"
                        "{})",
                        name, r.source, modSourceNames()));
    }
    if (*source == gg::core::ModSource::None)
    {
        // An empty slot: any other key would go unread.
        for (const auto &item : j.items())
        {
            if (item.key() != "slot" && item.key() != "source")
            {
                throw SpecError(
                    fmt::format(R"({}.{} doesn't apply to source "None")", name, item.key()));
            }
        }
        return r;
    }
    if (o.has("sourceLayer"))
    {
        r.sourceLayer = o.string("sourceLayer");
        if (r.sourceLayer != "same" && !layerOfLetter(r.sourceLayer))
        {
            throw SpecError(
                fmt::format(R"({}.sourceLayer must be "A", "B", "C", "D" or "same")", name));
        }
    }
    r.target = o.string("target");
    const auto target = paramSlotNamedIn(r.target, mode);
    if (!target)
    {
        throw SpecError(fmt::format(R"({}.target must be one of {}, not "{}")", name,
                                    parameterNames(mode), r.target));
    }
    // A layer kind is named by layer A's slot, and targetLayer picks the layers (gg::core::
    // isValidTarget).
    const auto ref = gg::core::layerOf(*target);
    const auto layer =
        ref ? std::optional<uint8_t>(ref->layer) : gg::core::perLayerGlobalLayer(*target);
    if (layer && *layer != 0)
    {
        throw SpecError(fmt::format("{}.target names layer {}; give the layer-A name and "
                                    "targetLayer",
                                    name, static_cast<char>('A' + *layer)));
    }
    if (!gg::core::isValidTarget(*target))
    {
        throw SpecError(fmt::format("{}.target is not a modulation target", name));
    }
    if (o.has("targetLayer"))
    {
        if (!gg::core::isLayerTarget(*target))
        {
            throw SpecError(fmt::format(R"({}.targetLayer doesn't apply to the global target "{}")",
                                        name, r.target));
        }
        r.targetLayer = o.string("targetLayer");
        if (r.targetLayer != "all" && !layerOfLetter(r.targetLayer))
        {
            throw SpecError(
                fmt::format(R"({}.targetLayer must be "A", "B", "C", "D" or "all")", name));
        }
    }
    r.depth = o.number("depth");
    // The Processor takes it as a float, a conversion defined only within float's range.
    if (!(std::abs(r.depth) <= std::numeric_limits<float>::max()))
    {
        throw SpecError(fmt::format("{}.depth must be a finite number", name));
    }
    if (o.has("curve"))
    {
        r.curve = o.string("curve");
        if (!gg::core::modCurveNamed(r.curve))
        {
            throw SpecError(fmt::format(R"({}.curve must be one of {}, not "{}")", name,
                                        modCurveNames(), r.curve));
        }
    }
    return r;
}

/// "mod": routes, each slot named once.
std::vector<RouteSpec> readMod(const json &j, gg::core::Mode mode)
{
    if (!j.is_array())
    {
        throw SpecError("mod must be a JSON array");
    }
    std::vector<RouteSpec> routes;
    std::array<bool, gg::core::kModSlots> named{};
    for (std::size_t i = 0; i < j.size(); ++i)
    {
        const std::string name = fmt::format("mod[{}]", i);
        RouteSpec r = readRoute(j[i], name, mode);
        if (named[r.slot - 1u])
        {
            throw SpecError(fmt::format("{}.slot {} is named twice", name, r.slot));
        }
        named[r.slot - 1u] = true;
        routes.push_back(std::move(r));
    }
    return routes;
}

std::vector<ControlSpec> readControls(const json &j, double sampleRate, double durationSeconds)
{
    if (!j.is_array())
    {
        throw SpecError("controls must be a JSON array");
    }
    const int64_t end = framesFor(durationSeconds, sampleRate);
    std::vector<ControlSpec> controls;
    for (std::size_t i = 0; i < j.size(); ++i)
    {
        const std::string name = fmt::format("controls[{}]", i);
        const Object o(j[i], name, name + ".", {"time", "kind", "value", "channel"});
        ControlSpec c;
        c.timeSeconds = o.number("time");
        if (!beforeEnd(c.timeSeconds, durationSeconds, sampleRate, end))
        {
            throw SpecError(timeError(name));
        }
        if (!controls.empty() && c.timeSeconds < controls.back().timeSeconds)
        {
            throw SpecError(fmt::format("{}.time is before controls[{}].time: list the controls "
                                        "in time order",
                                        name, i - 1));
        }
        c.kind = o.string("kind");
        if (c.kind != "modWheel" && c.kind != "aftertouch" && c.kind != "pitchBend")
        {
            throw SpecError(fmt::format("{}.kind must be modWheel, aftertouch or pitchBend", name));
        }
        c.value = o.number("value");
        if (!(c.value >= 0.0 && c.value <= 1.0))
        {
            throw SpecError(fmt::format("{}.value must be from 0 to 1", name));
        }
        if (o.has("channel"))
        {
            c.channel = static_cast<uint8_t>(o.integerUpTo("channel", 15));
        }
        controls.push_back(std::move(c));
    }
    return controls;
}

TransportSpec readTransport(const json &j)
{
    const Object o(j, "transport", "transport.", {"tempoBpm", "playing", "startBeat"});
    TransportSpec t;
    t.tempoBpm = o.positive("tempoBpm");
    if (o.has("playing"))
    {
        const json &playing = o.member("playing");
        if (!playing.is_boolean())
        {
            throw SpecError("transport.playing must be true or false");
        }
        t.playing = playing.get<bool>();
    }
    if (o.has("startBeat"))
    {
        t.startBeat = o.number("startBeat");
        if (!(t.startBeat >= 0.0))
        {
            throw SpecError("transport.startBeat must be >= 0");
        }
    }
    return t;
}

/// "operations": ring operations in time order (OperationSpec).
std::vector<OperationSpec> readOperations(const json &j, double sampleRate, double durationSeconds)
{
    if (!j.is_array())
    {
        throw SpecError("operations must be a JSON array");
    }
    const int64_t end = framesFor(durationSeconds, sampleRate);
    std::vector<OperationSpec> operations;
    for (std::size_t i = 0; i < j.size(); ++i)
    {
        const std::string name = fmt::format("operations[{}]", i);
        const Object o(j[i], name, name + ".",
                       {"time", "layer", "op", "behind", "splice", "into", "file"});
        OperationSpec op;
        op.timeSeconds = o.number("time");
        if (!beforeEnd(op.timeSeconds, durationSeconds, sampleRate, end))
        {
            throw SpecError(timeError(name));
        }
        if (!operations.empty() && op.timeSeconds < operations.back().timeSeconds)
        {
            throw SpecError(fmt::format("{}.time is before operations[{}].time: list the "
                                        "operations in time order",
                                        name, i - 1));
        }
        const auto layer = layerOfLetter(o.string("layer"));
        if (!layer)
        {
            throw SpecError(fmt::format(R"({}.layer must be "A", "B", "C" or "D")", name));
        }
        op.layer = *layer;
        op.op = o.string("op");
        if (op.op != "splice" && op.op != "clearSplices" && op.op != "capture")
        {
            throw SpecError(fmt::format(
                R"({}.op must be "splice", "clearSplices" or "capture", not "{}")", name, op.op));
        }
        // A key only another op reads would go unread.
        const auto only = [&](std::string_view key, std::string_view owner) {
            if (o.has(key) && op.op != owner)
            {
                throw SpecError(fmt::format(R"({}.{} doesn't apply to op "{}")", name, key, op.op));
            }
        };
        only("behind", "splice");
        only("splice", "capture");
        only("into", "capture");
        only("file", "capture");
        if (o.has("behind"))
        {
            op.behind = o.number("behind");
            if (!(op.behind >= 0.0 && op.behind <= gg::core::kRingMaxSeconds))
            {
                throw SpecError(
                    fmt::format("{}.behind must be from 0 to {}", name, gg::core::kRingMaxSeconds));
            }
        }
        if (op.op == "capture")
        {
            if (o.has("splice"))
            {
                op.splice = static_cast<uint32_t>(o.integerUpTo("splice", gg::core::kMaxSplices));
            }
            if (o.has("into"))
            {
                const auto into = layerOfLetter(o.string("into"));
                if (!into)
                {
                    throw SpecError(fmt::format(R"({}.into must be "A", "B", "C" or "D")", name));
                }
                op.into = *into;
            }
            if (o.has("file"))
            {
                op.file = o.string("file");
                if (op.file.empty())
                {
                    throw SpecError(fmt::format("{}.file must not be empty", name));
                }
            }
            if (!op.into && op.file.empty())
            {
                throw SpecError(
                    fmt::format("{} needs into or file: a capture that goes nowhere", name));
            }
        }
        operations.push_back(std::move(op));
    }
    return operations;
}

/// "memoryCapMiB": a whole number of MiB, kMemoryCapMinMiB to kMemoryCapMaxMiB.
uint32_t readMemoryCap(const Object &o)
{
    const uint64_t cap = o.unsignedInteger("memoryCapMiB");
    if (cap < gg::core::kMemoryCapMinMiB || cap > gg::core::kMemoryCapMaxMiB)
    {
        throw SpecError(fmt::format("memoryCapMiB must be an integer from {} to {}",
                                    gg::core::kMemoryCapMinMiB, gg::core::kMemoryCapMaxMiB));
    }
    return static_cast<uint32_t>(cap);
}

/// The keys instrument and FX mode share, all optional: "params", "automation", "notes",
/// "samples", "mod", "controls", "transport", "state", "operations" and "memoryCapMiB". `Spec` is
/// InstrumentSpec or FxSpec.
template <typename Spec>
void readProcessorKeys(const Object &o, const RenderSpec &spec, gg::core::Mode mode, Spec &out)
{
    auto &params = out.params;
    auto &automation = out.automation;
    auto &notes = out.notes;
    if (o.has("params"))
    {
        params = readParams(o.member("params"), mode);
    }
    if (o.has("automation"))
    {
        automation =
            readAutomation(o.member("automation"), mode, spec.sampleRate, spec.durationSeconds);
    }
    if (o.has("notes"))
    {
        notes = readNotes(o.member("notes"), spec.sampleRate, spec.durationSeconds);
    }
    if (o.has("samples"))
    {
        out.layerSamples = readLayerSamples(o.member("samples"));
    }
    if (o.has("mod"))
    {
        out.mod = readMod(o.member("mod"), mode);
    }
    if (o.has("controls"))
    {
        out.controls = readControls(o.member("controls"), spec.sampleRate, spec.durationSeconds);
    }
    if (o.has("transport"))
    {
        out.transport = readTransport(o.member("transport"));
    }
    if (o.has("state"))
    {
        out.statePath = o.string("state");
        if (out.statePath.empty())
        {
            throw SpecError("state must not be empty");
        }
    }
    if (o.has("operations"))
    {
        out.operations =
            readOperations(o.member("operations"), spec.sampleRate, spec.durationSeconds);
    }
    if (o.has("memoryCapMiB"))
    {
        out.memoryCapMiB = readMemoryCap(o);
    }
}

/// The instrument keys of a spec whose sampleRate and durationSeconds are already read.
InstrumentSpec readInstrument(const Object &o, const RenderSpec &spec)
{
    InstrumentSpec instrument;
    // "sample" may be left out beside a "state", whose layer A sample is then used.
    if (o.has("sample") || !o.has("state"))
    {
        instrument.sample = readSource(o.member("sample"), SignalRole::Sample);
    }
    else
    {
        instrument.noSample = true;
    }
    readProcessorKeys(o, spec, gg::core::Mode::Instrument, instrument);
    // "sample" is layer A's.
    if (!instrument.noSample && !instrument.layerSamples.empty() &&
        instrument.layerSamples.front().first == 0)
    {
        throw SpecError("samples.A duplicates sample");
    }
    return instrument;
}

/// The FX keys of a spec whose sampleRate and durationSeconds are already read.
FxSpec readFx(const Object &o, const RenderSpec &spec)
{
    FxSpec fx;
    fx.input = readSource(o.member("input"), SignalRole::Input);
    // A file input has the file's own channels, so "channels" has no meaning there.
    if (fx.input.kind == SourceKind::File)
    {
        if (o.has("channels"))
        {
            throw SpecError(
                "channels must be left out with a file input: the input has the file's own "
                "channels");
        }
    }
    else
    {
        const uint64_t channels = o.unsignedInteger("channels");
        if (channels < 1 || channels > 2)
        {
            throw SpecError("channels must be 1 or 2");
        }
        fx.channels = static_cast<uint32_t>(channels);
    }
    readProcessorKeys(o, spec, gg::core::Mode::Fx, fx);
    return fx;
}

/// The spec's "mode", Sample if there is none. Read before the spec's keys are checked, since
/// the keys allowed depend on it.
RenderMode readMode(const json &j)
{
    const auto it = j.find("mode"); // end() if j isn't an object
    if (it == j.end())
    {
        return RenderMode::Sample;
    }
    if (it->is_string() && it->get<std::string>() == "sample")
    {
        return RenderMode::Sample;
    }
    if (it->is_string() && it->get<std::string>() == "instrument")
    {
        return RenderMode::Instrument;
    }
    if (it->is_string() && it->get<std::string>() == "fx")
    {
        return RenderMode::Fx;
    }
    throw SpecError(
        fmt::format(R"(mode must be "sample", "instrument" or "fx", not {})", it->dump()));
}

/// The spec as an Object whose keys are all `mode`'s.
Object topLevel(const json &j, RenderMode mode)
{
    switch (mode)
    {
    case RenderMode::Instrument:
        return {j,
                "the spec",
                "",
                {"mode", "sampleRate", "durationSeconds", "blockSize", "seed", "sample", "params",
                 "automation", "notes", "samples", "mod", "controls", "transport", "state",
                 "operations", "memoryCapMiB"}};
    case RenderMode::Fx:
        return {j,
                "the spec",
                "",
                {"mode", "sampleRate", "durationSeconds", "blockSize", "seed", "input", "channels",
                 "params", "automation", "notes", "samples", "mod", "controls", "transport",
                 "state", "operations", "memoryCapMiB"}};
    case RenderMode::Sample:
        break;
    }
    return {j,
            "the spec",
            "",
            {"mode", "sampleRate", "durationSeconds", "blockSize", "seed", "source", "stream"}};
}

RenderSpec readSpec(const json &j)
{
    RenderSpec spec;
    spec.mode = readMode(j);
    const Object o = topLevel(j, spec.mode);
    spec.sampleRate = o.number("sampleRate");
    if (!(spec.sampleRate >= 1.0 && spec.sampleRate <= kMaxSampleRate &&
          spec.sampleRate == std::floor(spec.sampleRate)))
    {
        throw SpecError(
            fmt::format("sampleRate must be a whole number of Hz from 1 to {}", kMaxSampleRate));
    }
    spec.durationSeconds = o.positive("durationSeconds", kMaxSeconds);
    if (framesFor(spec.durationSeconds, spec.sampleRate) < 1)
    {
        throw SpecError("durationSeconds must round to at least one frame at sampleRate");
    }
    const uint64_t blockSize = o.unsignedInteger("blockSize");
    if (blockSize < 1 || blockSize > kMaxBlockSize)
    {
        throw SpecError(fmt::format("blockSize must be from 1 to {}", kMaxBlockSize));
    }
    spec.blockSize = static_cast<uint32_t>(blockSize);
    spec.seed = o.unsignedInteger("seed");
    switch (spec.mode)
    {
    case RenderMode::Sample:
        spec.source = readSource(o.member("source"), SignalRole::Source);
        spec.stream = readStream(o.member("stream"));
        break;
    case RenderMode::Instrument:
        spec.instrument = readInstrument(o, spec);
        break;
    case RenderMode::Fx:
        spec.fx = readFx(o, spec);
        break;
    }
    return spec;
}
} // namespace

std::optional<gg::core::ParamSlot> paramSlotNamed(std::string_view name)
{
    // A bare M1 name is layer A's (spec §6.9). Then either product's names: the FX's first, then
    // the instrument's, which differ only for Macro 8. No name is one slot's in one product and
    // another's in the other.
    if (const auto alias = m1LayerAlias(name))
    {
        return alias;
    }
    for (const auto mode : {gg::core::Mode::Fx, gg::core::Mode::Instrument})
    {
        for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
        {
            const auto slot = static_cast<gg::core::ParamSlot>(s);
            if (gg::core::paramMeta(slot, mode).name == name)
            {
                return slot;
            }
        }
    }
    return std::nullopt;
}

std::filesystem::path pathFromUtf8(const std::string &text)
{
    auto path = gg::media::fromUtf8(text);
    if (!path)
    {
        throw BadInput(fmt::format("the path {} isn't valid UTF-8", text));
    }
    return std::move(*path);
}

std::string utf8FromPath(const std::filesystem::path &path)
{
    auto text = gg::media::toUtf8(path);
    if (!text)
    {
        throw BadInput("a path can't be written as UTF-8");
    }
    return std::move(*text);
}

void resolvePaths(RenderSpec &spec, const std::string &baseDir)
{
    std::vector<SourceSpec *> sources{&spec.source, &spec.instrument.sample, &spec.fx.input};
    for (auto *layerSamples : {&spec.instrument.layerSamples, &spec.fx.layerSamples})
    {
        for (auto &layerSample : *layerSamples)
        {
            sources.push_back(&layerSample.second);
        }
    }
    for (std::string *state : {&spec.instrument.statePath, &spec.fx.statePath})
    {
        if (!state->empty())
        {
            const std::filesystem::path path = pathFromUtf8(*state);
            if (path.is_relative())
            {
                *state = utf8FromPath(pathFromUtf8(baseDir) / path);
            }
        }
    }
    for (SourceSpec *s : sources)
    {
        if (s->kind != SourceKind::File || s->path.empty())
        {
            continue;
        }
        const std::filesystem::path path = pathFromUtf8(s->path);
        if (path.is_relative())
        {
            s->path = utf8FromPath(pathFromUtf8(baseDir) / path);
        }
    }
}

std::optional<RenderSpec> parseRenderSpec(const std::string &text, std::string &error)
{
    error.clear();
    try
    {
        return readSpec(json::parse(text));
    }
    catch (const json::exception &e)
    {
        error = fmt::format("not valid JSON: {}", e.what());
    }
    catch (const SpecError &e)
    {
        error = e.what();
    }
    return std::nullopt;
}
} // namespace gg::render
