// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_RENDER_SPECREADERS_H
#define GG_RENDER_SPECREADERS_H

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "RenderSpec.h"
#include "gg/core/ParamIds.h"

/// gg-render's internal JSON readers, shared by the render spec's parser (RenderSpec.cpp) and the
/// preset source's (PresetSource.cpp; M2 sub-project 5's spec §6.6), so that a preset source names
/// its parameters and routes exactly as a render spec does. Not part of gg-render-lib's interface:
/// only those two files include it. Each reader throws SpecError, which its parser turns into the
/// error message.
namespace gg::render::detail
{
using json = nlohmann::json;

/// Thrown while reading an invalid spec; parseRenderSpec turns it into the error message.
struct SpecError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// v as a finite number; `key` names it in the error. (JSON can't spell NaN or infinity, but a
/// literal like 1e999 overflows.)
double finiteNumber(const json &v, std::string_view key);

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

/// `mode`'s product's parameter names, for error messages: "Mix, Level, A Position, ..., Budget".
std::string parameterNames(gg::core::Mode mode);

/// The parameter named `name` in `mode`'s product: paramSlotNamed's, unless that name is the other
/// product's alone ("Macro Level" in an FX spec, say). A bare M1 name ("Size"), or "Quality", is
/// layer A's in either product.
std::optional<gg::core::ParamSlot> paramSlotNamedIn(std::string_view name, gg::core::Mode mode);

/// v as a value for the parameter in `slot` of `mode`'s product: a finite number that, converted
/// to float as the Processor converts it, is in the parameter's range. `key` names it in the
/// error.
double parameterValue(const json &v, gg::core::ParamSlot slot, gg::core::Mode mode,
                      std::string_view key);

/// "params": base values by parameter name, of `mode`'s product, in the JSON's order.
std::vector<std::pair<std::string, double>> readParams(const json &j, gg::core::Mode mode);

/// The layer (0 to 3) that the letter `text` ("A" to "D") names; nothing for any other text.
std::optional<uint8_t> layerOfLetter(std::string_view text);

/// "mod": routes, each slot named once, with the parameter names of `mode`'s product. See
/// RouteSpec.
std::vector<RouteSpec> readMod(const json &j, gg::core::Mode mode);
} // namespace gg::render::detail

#endif // GG_RENDER_SPECREADERS_H
