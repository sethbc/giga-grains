// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_TUNING_H
#define GG_CORE_TUNING_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <type_traits>

namespace gg::core
{

/// How each key becomes a pitch (M3 tuning spec §4.2, T5): per key k, the key term
/// s(k) = 12·log2(f(k) / kC4Hz), the semitones of the key's tuned frequency above 12-TET's C4 at
/// A440. In 12-TET s(k) is exactly k − 60. Every entry is finite and within ±1024. Bit k of
/// `unmapped` is set when key k's note-on is dropped (T7); its entry is then interpolated, read
/// only by a note already held when a change unmapped it.
struct TuningTable
{
    std::array<double, 128> keySemitones{};
    std::array<uint64_t, 2> unmapped{};

    /// The double nearest 440 · 2^(−9/12): C4 at A440, rate 1.
    static constexpr double kC4Hz = 261.6255653005986;

    /// The identity table: k − 60.0 for every key, nothing unmapped.
    static TuningTable twelveTet() noexcept
    {
        TuningTable t;
        for (size_t k = 0; k < t.keySemitones.size(); ++k)
        {
            t.keySemitones[k] = static_cast<double>(k) - 60.0;
        }
        return t;
    }

    /// True when key (0 to 127) is unmapped; false for a key outside that range.
    bool isUnmapped(int key) const noexcept
    {
        if (key < 0 || key > 127)
        {
            return false;
        }
        const auto k = static_cast<unsigned>(key);
        return ((unmapped[k >> 6u] >> (k & 63u)) & 1u) != 0;
    }

    friend bool operator==(const TuningTable &, const TuningTable &) = default;
};
static_assert(std::is_trivially_copyable_v<TuningTable>);

/// The MTS-ESP poller's snapshot (spec §4.3): the master's table, and whether one is connected.
struct MtsTuning
{
    TuningTable table{TuningTable::twelveTet()};
    bool connected{false};
};
static_assert(std::is_trivially_copyable_v<MtsTuning>);

/// The largest `.scl` or `.kbm` text a load takes (spec §5.2 step 1).
inline constexpr size_t kMaxTuningTextBytes = 65536;

/// The tuning table of a Scala scale and keyboard mapping (spec §5.2), off the audio thread only:
/// it allocates. tuning-library parses and validates both texts, and a pair it refuses is refused
/// with its message; the table is then computed by a deterministic port of its mapping arithmetic
/// (T4), so no libm value reaches an entry. An empty `scl` is 12-EDO; an empty `kbm` is the
/// standard mapping with key 60 at kC4Hz. Never throws: a refusal is the error's message.
std::expected<TuningTable, std::string> tuningFromScala(std::string_view scl, std::string_view kbm);

/// The scale's description: the first line not starting with `!`, after a leading UTF-8 BOM,
/// trimmed of spaces, tabs and CR. The bytes are returned as they are (never transcoded); empty
/// for an empty `scl`.
std::string scalaDescription(std::string_view scl);

/// Spec §5.2 step 3's exact decimal parse, locale-free and without strtod: everything from a `!`
/// on is ignored; then leading white space, an optional sign, digits, an optional `.` and
/// digits (at least one digit in all); any text after the number is ignored. With N the digits
/// as an integer and f the count after the point, the value is the one correctly rounded
/// division N / 10^f when N < 2^53 and f ≤ 22; otherwise the digits past the first 15
/// significant ones are dropped first. False, and `out` untouched, when there is no number.
bool parseExactDecimal(std::string_view text, double &out) noexcept;

} // namespace gg::core

#endif // GG_CORE_TUNING_H
