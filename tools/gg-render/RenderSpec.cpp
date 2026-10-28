// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "RenderSpec.h"

#include <algorithm>
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

/// Which signal readSource reads: a sample-mode "source", or an FX "input", which is
/// synthesized at the render's rate and so has no "sampleRate" and can't be a file.
enum class SignalRole
{
    Source,
    Input,
};

SourceSpec readSource(const json &j, SignalRole role)
{
    const bool input = role == SignalRole::Input;
    const std::string name = input ? "input" : "source";
    const Object o =
        input ? Object(j, name, name + ".",
                       {"kind", "frequencyHz", "startHz", "endHz", "seconds", "amplitude"})
              : Object(j, name, name + ".",
                       {"kind", "frequencyHz", "startHz", "endHz", "seconds", "amplitude",
                        "sampleRate", "path"});
    SourceSpec s;
    const std::string kind = o.string("kind");
    if (kind == "sine")
    {
        s.kind = SourceKind::Sine;
        s.frequencyHz = o.positive("frequencyHz");
    }
    else if (kind == "sweep")
    {
        s.kind = SourceKind::Sweep;
        s.startHz = o.positive("startHz");
        s.endHz = o.positive("endHz");
    }
    else if (kind == "noise")
    {
        s.kind = SourceKind::Noise;
    }
    else if (kind == "file" && !input)
    {
        s.kind = SourceKind::File;
        s.path = o.string("path");
        if (s.path.empty())
        {
            throw SpecError("source.path must not be empty");
        }
        return s;
    }
    else
    {
        throw SpecError(
            input
                ? fmt::format(R"(input.kind must be "sine", "sweep" or "noise", not "{}")", kind)
                : fmt::format(R"(source.kind must be "sine", "sweep", "noise" or "file", not "{}")",
                              kind));
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
                    "pitchSemitones", "gain", "distribution"});
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
    return s;
}

/// The parameter names, for error messages: "Mix, Level, ..., Freeze".
std::string parameterNames()
{
    std::string names;
    for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        names +=
            (s == 0 ? "" : ", ") + gg::core::paramMeta(static_cast<gg::core::ParamSlot>(s)).name;
    }
    return names;
}

/// v as a value for the parameter in `slot`: a finite number that, converted to float as the
/// Processor converts it, is in the parameter's range. `key` names it in the error.
double parameterValue(const json &v, gg::core::ParamSlot slot, std::string_view key)
{
    const double d = finiteNumber(v, key);
    const auto &meta = gg::core::paramMeta(slot);
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

std::vector<std::pair<std::string, double>> readParams(const json &j)
{
    if (!j.is_object())
    {
        throw SpecError("params must be a JSON object");
    }
    std::vector<std::pair<std::string, double>> params;
    for (const auto &item : j.items())
    {
        const std::string key = fmt::format("params.{}", item.key());
        const auto slot = paramSlotNamed(item.key());
        if (!slot)
        {
            throw SpecError(
                fmt::format("unknown key {}: the parameters are {}", key, parameterNames()));
        }
        params.emplace_back(item.key(), parameterValue(item.value(), *slot, key));
    }
    return params;
}

std::vector<AutomationPoint> readAutomation(const json &j, double sampleRate,
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
        // The first test keeps framesFor's argument in range.
        if (!(p.timeSeconds >= 0.0 && p.timeSeconds <= durationSeconds &&
              framesFor(p.timeSeconds, sampleRate) < end))
        {
            throw SpecError(
                fmt::format("{}.time must be >= 0 and round to a frame before the render's end, "
                            "durationSeconds",
                            name));
        }
        if (!points.empty() && p.timeSeconds < points.back().timeSeconds)
        {
            throw SpecError(fmt::format("{}.time is before automation[{}].time: list the points "
                                        "in time order",
                                        name, i - 1));
        }
        p.param = o.string("param");
        const auto slot = paramSlotNamed(p.param);
        if (!slot)
        {
            throw SpecError(fmt::format(R"({}.param must be one of {}, not "{}")", name,
                                        parameterNames(), p.param));
        }
        p.value = parameterValue(o.member("value"), *slot, name + ".value");
        points.push_back(std::move(p));
    }
    return points;
}

/// The FX keys of a spec whose sampleRate and durationSeconds are already read.
FxSpec readFx(const Object &o, const RenderSpec &spec)
{
    FxSpec fx;
    fx.input = readSource(o.member("input"), SignalRole::Input);
    const uint64_t channels = o.unsignedInteger("channels");
    if (channels < 1 || channels > 2)
    {
        throw SpecError("channels must be 1 or 2");
    }
    fx.channels = static_cast<uint32_t>(channels);
    if (o.has("params"))
    {
        fx.params = readParams(o.member("params"));
    }
    if (o.has("automation"))
    {
        fx.automation =
            readAutomation(o.member("automation"), spec.sampleRate, spec.durationSeconds);
    }
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
    if (it->is_string() && it->get<std::string>() == "fx")
    {
        return RenderMode::Fx;
    }
    throw SpecError(fmt::format(R"(mode must be "sample" or "fx", not {})", it->dump()));
}

RenderSpec readSpec(const json &j)
{
    RenderSpec spec;
    spec.mode = readMode(j);
    const bool fx = spec.mode == RenderMode::Fx;
    const Object o = fx ? Object(j, "the spec", "",
                                 {"mode", "sampleRate", "durationSeconds", "blockSize", "seed",
                                  "input", "channels", "params", "automation"})
                        : Object(j, "the spec", "",
                                 {"mode", "sampleRate", "durationSeconds", "blockSize", "seed",
                                  "source", "stream"});
    spec.sampleRate = o.number("sampleRate");
    if (!(spec.sampleRate >= 1.0 && spec.sampleRate <= kMaxSampleRate &&
          spec.sampleRate == std::floor(spec.sampleRate)))
    {
        throw SpecError(
            fmt::format("sampleRate must be a whole number of Hz from 1 to {}", kMaxSampleRate));
    }
    spec.durationSeconds = o.positive("durationSeconds", kMaxSeconds);
    const uint64_t blockSize = o.unsignedInteger("blockSize");
    if (blockSize < 1 || blockSize > kMaxBlockSize)
    {
        throw SpecError(fmt::format("blockSize must be from 1 to {}", kMaxBlockSize));
    }
    spec.blockSize = static_cast<uint32_t>(blockSize);
    spec.seed = o.unsignedInteger("seed");
    if (fx)
    {
        spec.fx = readFx(o, spec);
    }
    else
    {
        spec.source = readSource(o.member("source"), SignalRole::Source);
        spec.stream = readStream(o.member("stream"));
    }
    return spec;
}
} // namespace

std::optional<gg::core::ParamSlot> paramSlotNamed(std::string_view name)
{
    for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        const auto slot = static_cast<gg::core::ParamSlot>(s);
        if (gg::core::paramMeta(slot).name == name)
        {
            return slot;
        }
    }
    return std::nullopt;
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
