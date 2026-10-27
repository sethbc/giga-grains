// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "RenderSpec.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

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

    const json &member(std::string_view key) const
    {
        const auto it = j_.find(key);
        if (it == j_.end())
        {
            throw SpecError(fmt::format("{}{} is missing", prefix_, key));
        }
        return *it;
    }

    /// A finite number. (JSON can't spell NaN or infinity, but a literal like 1e999 overflows.)
    double number(std::string_view key) const
    {
        const json &v = member(key);
        const double d = v.is_number() ? v.get<double>() : 0.0;
        if (!v.is_number() || !std::isfinite(d))
        {
            throw SpecError(fmt::format("{}{} must be a finite number", prefix_, key));
        }
        return d;
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

SourceSpec readSource(const json &j)
{
    const Object o(j, "source", "source.",
                   {"kind", "frequencyHz", "startHz", "endHz", "seconds", "amplitude", "path"});
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
    else if (kind == "file")
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
        throw SpecError(fmt::format(
            R"(source.kind must be "sine", "sweep", "noise" or "file", not "{}")", kind));
    }
    s.seconds = o.positive("seconds", kMaxSeconds);
    s.amplitude = o.number("amplitude");
    if (!(s.amplitude >= 0.0))
    {
        throw SpecError("source.amplitude must be >= 0");
    }
    return s;
}

StreamSpec readStream(const json &j)
{
    const Object o(
        j, "stream", "stream.",
        {"grainRateHz", "grainDurationMs", "position", "positionJitter", "pitchSemitones", "gain"});
    StreamSpec s;
    s.grainRateHz = o.number("grainRateHz");
    s.grainDurationMs = o.number("grainDurationMs");
    s.position = o.number("position");
    s.positionJitter = o.number("positionJitter");
    s.pitchSemitones = o.number("pitchSemitones");
    s.gain = o.number("gain");
    return s;
}

RenderSpec readSpec(const json &j)
{
    const Object o(j, "the spec", "",
                   {"sampleRate", "durationSeconds", "blockSize", "seed", "source", "stream"});
    RenderSpec spec;
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
    spec.source = readSource(o.member("source"));
    spec.stream = readStream(o.member("stream"));
    return spec;
}
} // namespace

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
