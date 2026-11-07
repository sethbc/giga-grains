// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)
//
// The tuning table from Scala texts (M3 tuning spec §5.2, T4, T5). tuning-library parses and
// validates; the table is our own port of its mapping arithmetic, `Tuning::Tuning(const Scale &,
// const KeyboardMapping &, bool)` and `withSkippedNotesInterpolated` in
// libs/tuning-library/include/TuningsImpl.h at d6fcc08, carried out in semitones with logDet in
// place of log, no pow, and every cents value and the reference frequency re-parsed exactly, so no
// libm value reaches an entry. The port is MIT code adapted (Copyright 2019-2020 Paul Walker,
// tuning-library's LICENSE.md, carried in THIRD_PARTY_NOTICES.md). This is the only non-test
// file that includes tuning-library.

#include "gg/core/Tuning.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <Tunings.h>

#include "gg/core/MathDet.h"

namespace gg::core
{
namespace
{

/// tuning-library's table: keys −256 to 255, key k at index k + 256.
constexpr int kTableKeys = 512;
constexpr int kTableOffset = 256;
static_assert(kTableKeys == Tunings::Tuning::N);

/// An entry within this of an integer is that integer (T5).
constexpr double kSnapSemitones = 1e-9;
/// Every entry is clamped to ±this (spec §4.2).
constexpr double kMaxSemitones = 1024.0;

/// The pin's window walk stops past this many shifts (TuningsImpl.h's maxWindowShifts).
constexpr int kMaxWindowShifts = 1024;
/// Our guards against a hostile `.kbm` that tuning-library would spend unbounded time or memory
/// on (its scale unwrap allocates count × rotations tones; its per-key loops walk a distance from
/// the middle note one mapping at a time): far past any real file.
constexpr int64_t kMaxUnwrappedTones = int64_t{1} << 18;
constexpr int kMaxMappingNote = 16384;

/// 10^0 to 10^22, each exactly representable.
constexpr std::array<double, 23> kPow10{1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                        1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                        1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }

/// m · 10^exponent, by exact powers of ten: one multiplication or division when |exponent| ≤ 22,
/// else repeated ones (deterministic, if no longer one rounding).
double scaleByPow10(double m, int64_t exponent) noexcept
{
    GG_FP_CONTRACT_OFF
    while (exponent > 22 && std::isfinite(m) && m != 0.0)
    {
        m *= kPow10[22];
        exponent -= 22;
    }
    while (exponent < -22 && m != 0.0)
    {
        m /= kPow10[22];
        exponent += 22;
    }
    if (exponent > 22 || exponent < -22)
    {
        return m; // already inf or 0
    }
    return exponent >= 0 ? m * kPow10[static_cast<size_t>(exponent)]
                         : m / kPow10[static_cast<size_t>(-exponent)];
}

/// Spec §5.2 step 3: the decimal in `text`, divided by 10^shift (2 turns cents into semitones in
/// the same single division).
bool parseDecimalShifted(std::string_view text, int shift, double &out) noexcept
{
    GG_FP_CONTRACT_OFF
    if (const auto bang = text.find('!'); bang != std::string_view::npos)
    {
        text = text.substr(0, bang);
    }
    size_t i = 0;
    while (i < text.size() && isSpace(text[i]))
    {
        ++i;
    }
    bool negative = false;
    if (i < text.size() && (text[i] == '+' || text[i] == '-'))
    {
        negative = text[i] == '-';
        ++i;
    }
    const size_t intBegin = i;
    while (i < text.size() && isDigit(text[i]))
    {
        ++i;
    }
    const std::string_view intDigits = text.substr(intBegin, i - intBegin);
    std::string_view fracDigits;
    if (i < text.size() && text[i] == '.')
    {
        ++i;
        const size_t fracBegin = i;
        while (i < text.size() && isDigit(text[i]))
        {
            ++i;
        }
        fracDigits = text.substr(fracBegin, i - fracBegin);
    }
    if (intDigits.empty() && fracDigits.empty())
    {
        return false;
    }

    // N exactly, when it is below 2^53.
    constexpr uint64_t kExactLimit = uint64_t{1} << 53;
    uint64_t n = 0;
    bool fits = true;
    for (const std::string_view part : {intDigits, fracDigits})
    {
        for (const char c : part)
        {
            n = n * 10u + static_cast<uint64_t>(c - '0');
            if (n >= kExactLimit)
            {
                fits = false;
                break;
            }
        }
        if (!fits)
        {
            break;
        }
    }
    const int64_t places = static_cast<int64_t>(fracDigits.size()) + shift;
    double value = 0.0;
    if (fits && places <= 22)
    {
        value = static_cast<double>(n) / kPow10[static_cast<size_t>(places)];
    }
    else
    {
        // The first 15 significant digits, the rest dropped: value = m · 10^exponent.
        uint64_t m = 0;
        int kept = 0;
        bool significant = false;
        int64_t exponent = -shift;
        for (const char c : intDigits)
        {
            const auto d = static_cast<uint64_t>(c - '0');
            significant = significant || d != 0;
            if (!significant)
            {
                continue;
            }
            if (kept < 15)
            {
                m = m * 10u + d;
                ++kept;
            }
            else
            {
                ++exponent;
            }
        }
        for (const char c : fracDigits)
        {
            const auto d = static_cast<uint64_t>(c - '0');
            significant = significant || d != 0;
            if (!significant)
            {
                --exponent;
                continue;
            }
            if (kept < 15)
            {
                m = m * 10u + d;
                ++kept;
                --exponent;
            }
        }
        value = scaleByPow10(static_cast<double>(m), exponent);
    }
    out = negative ? -value : value;
    return true;
}

/// An integer as atoll reads it (leading white space, a sign, digits), when it fits int64.
bool parseInt64(std::string_view text, int64_t &out) noexcept
{
    size_t i = 0;
    while (i < text.size() && isSpace(text[i]))
    {
        ++i;
    }
    bool negative = false;
    if (i < text.size() && (text[i] == '+' || text[i] == '-'))
    {
        negative = text[i] == '-';
        ++i;
    }
    const uint64_t limit = negative ? uint64_t{1} << 63 : (uint64_t{1} << 63) - 1u;
    uint64_t v = 0;
    bool any = false;
    while (i < text.size() && isDigit(text[i]))
    {
        const auto d = static_cast<uint64_t>(text[i] - '0');
        if (v > (limit - d) / 10u)
        {
            return false;
        }
        v = v * 10u + d;
        any = true;
        ++i;
    }
    if (!any)
    {
        return false;
    }
    out = negative ? static_cast<int64_t>(0u - v) : static_cast<int64_t>(v);
    return true;
}

/// The text without a leading UTF-8 byte-order mark.
std::string_view withoutBom(std::string_view text) noexcept
{
    if (text.starts_with("\xEF\xBB\xBF"))
    {
        text.remove_prefix(3);
    }
    return text;
}

/// The lines as tuning-library's getlineEndingIndependent splits them (LF, CRLF or CR).
std::vector<std::string_view> splitLines(std::string_view text)
{
    std::vector<std::string_view> lines;
    size_t start = 0;
    size_t i = 0;
    while (i < text.size())
    {
        if (text[i] == '\n' || text[i] == '\r')
        {
            lines.push_back(text.substr(start, i - start));
            if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
            {
                ++i;
            }
            ++i;
            start = i;
            continue;
        }
        ++i;
    }
    if (start < text.size())
    {
        lines.push_back(text.substr(start));
    }
    return lines;
}

/// The size and NUL refusals of spec §5.2 step 1.
std::string refuseText(std::string_view text, const char *what)
{
    if (text.size() > kMaxTuningTextBytes)
    {
        return std::string("the ") + what + " is over 64 KiB";
    }
    if (text.find('\0') != std::string_view::npos)
    {
        return std::string("the ") + what + " holds a NUL byte";
    }
    return {};
}

/// T5's snap, then the clamp to ±1024. NaN stays NaN: the caller refuses or replaces it. Task 5's
/// frequencies-to-table conversion reuses it.
double snapAndClampSemitones(double v) noexcept
{
    GG_FP_CONTRACT_OFF
    const double r = std::round(v);
    if (std::abs(v - r) <= kSnapSemitones)
    {
        v = r + 0.0; // + 0.0 makes −0 +0
    }
    if (v < -kMaxSemitones)
    {
        return -kMaxSemitones;
    }
    if (v > kMaxSemitones)
    {
        return kMaxSemitones;
    }
    return v;
}

/// The port (spec §5.2 step 4): TuningsImpl.h's constructor at the pin, statement for statement,
/// in semitones: where the pin's pitches[i] is in octaves offset by 1 (a tone's floatValue is
/// cents / 1200 + 1), ours is 12 · (pitches[i] − 1), and the pin's pitchMod (log2 of the
/// reference over MIDI 0's frequency, less 1) becomes `reference`, 12 · log2(F / kC4Hz), so an
/// entry is 12 · (lptable − log2(kC4Hz / MIDI_0_FREQ)). `tones` holds each tone in semitones.
/// The pin's throws are unreachable here (its own constructor validated the pair first) and are
/// kept as refusals. allowTuningCenterOnUnmapped is false, as in the validation, so its branch
/// is left out.
std::expected<TuningTable, std::string> mapKeys(const Tunings::KeyboardMapping &mapping,
                                                std::vector<double> tones, double reference)
{
    GG_FP_CONTRACT_OFF
    Tunings::KeyboardMapping k = mapping;
    int count = static_cast<int>(tones.size()); // s.count
    if (count <= 0)
    {
        return std::unexpected(std::string("the scale has no notes"));
    }

    int useMiddleNote = k.middleNote;
    if (k.count > 0)
    {
        int shifts = 0;
        auto mapStart = useMiddleNote;
        auto mapEnd = useMiddleNote + k.count;
        while (mapStart > k.tuningConstantNote)
        {
            if (++shifts > kMaxWindowShifts)
            {
                return std::unexpected(std::string("the mapping can't reach its reference key"));
            }
            useMiddleNote -= k.count;
            mapStart = useMiddleNote;
            mapEnd = useMiddleNote + k.count;
        }
        while (mapEnd < k.tuningConstantNote)
        {
            if (++shifts > kMaxWindowShifts)
            {
                return std::unexpected(std::string("the mapping can't reach its reference key"));
            }
            useMiddleNote += k.count;
            mapStart = useMiddleNote;
            mapEnd = useMiddleNote + k.count;
        }
    }

    int kbmRotations = 1;
    for (const auto &kv : k.keys)
    {
        kbmRotations = std::max(kbmRotations, static_cast<int>(std::ceil(1.0 * kv / count)));
    }
    if (kbmRotations > 1)
    {
        // The pin's unwrap: the scale repeated, each copy pushed up by its last tone once more.
        std::vector<double> unwrapped = tones;
        unwrapped.reserve(static_cast<size_t>(count) * static_cast<size_t>(kbmRotations));
        const double backSemitones = tones.back();
        double pushOff = backSemitones;
        for (int i = 1; i < kbmRotations; ++i)
        {
            for (const double t : tones)
            {
                unwrapped.push_back(t + pushOff);
            }
            pushOff += backSemitones;
        }
        tones = std::move(unwrapped);
        count = static_cast<int>(tones.size());
        k.octaveDegrees *= kbmRotations;
        if (k.octaveDegrees == 0)
        {
            k.octaveDegrees = count;
        }
    }
    if (k.octaveDegrees > count)
    {
        return std::unexpected(std::string("the mapping is larger than the scale"));
    }

    const int posPitch0 = kTableOffset + k.tuningConstantNote;
    const int posScale0 = kTableOffset + useMiddleNote;

    int scalePositionOfTuningNote = k.tuningConstantNote - useMiddleNote;
    if (k.count > 0)
    {
        while (scalePositionOfTuningNote >= k.count)
        {
            scalePositionOfTuningNote -= k.count;
        }
        while (scalePositionOfTuningNote < 0)
        {
            scalePositionOfTuningNote += k.count;
        }
        scalePositionOfTuningNote = k.keys[static_cast<size_t>(scalePositionOfTuningNote)];
        if (scalePositionOfTuningNote == -1)
        {
            return std::unexpected(std::string("the mapping tunes an unmapped key"));
        }
    }
    double tuningCenterPitchOffset = 0.0;
    if (scalePositionOfTuningNote == 0)
    {
        tuningCenterPitchOffset = 0;
    }
    else
    {
        double tshift = 0;
        const double dt = tones[static_cast<size_t>(count - 1)];
        while (scalePositionOfTuningNote < 0)
        {
            scalePositionOfTuningNote += count;
            tshift += dt;
        }
        while (scalePositionOfTuningNote > count)
        {
            scalePositionOfTuningNote -= count;
            tshift -= dt;
        }
        if (scalePositionOfTuningNote == 0)
        {
            tuningCenterPitchOffset = -tshift;
        }
        else
        {
            tuningCenterPitchOffset =
                tones[static_cast<size_t>(scalePositionOfTuningNote - 1)] - tshift;
        }
    }

    std::array<double, kTableKeys> pitches{};
    std::array<bool, kTableKeys> mapped{};
    for (int i = 0; i < kTableKeys; ++i)
    {
        const int distanceFromPitch0 = i - posPitch0;
        const int distanceFromScale0 = i - posScale0;
        const auto at = static_cast<size_t>(i);

        if (distanceFromPitch0 == 0)
        {
            pitches[at] = 0.0 + reference; // the pin's pitches[i] = 1, plus pitchMod
            mapped[at] = true;
            continue;
        }

        int rounds = 0;
        int thisRound = 0;
        bool disable = false;
        if (k.count == 0)
        {
            rounds = (distanceFromScale0 - 1) / count;
            thisRound = (distanceFromScale0 - 1) % count;
        }
        else
        {
            int mappingKey = distanceFromScale0 % k.count;
            int rotations = 0;
            if (mappingKey < 0)
            {
                mappingKey += k.count;
            }
            int dt = distanceFromScale0;
            if (dt > 0)
            {
                while (dt >= k.count)
                {
                    dt -= k.count;
                    rotations++;
                }
            }
            else
            {
                while (dt < 0)
                {
                    dt += k.count;
                    rotations--;
                }
            }

            const int cm = k.keys[static_cast<size_t>(mappingKey)];
            int push = 0;
            if (cm < 0)
            {
                disable = true;
            }
            else
            {
                if (cm > count)
                {
                    return std::unexpected(std::string("a key of the mapping is past the scale"));
                }
                push = mappingKey - cm;
            }

            if (k.octaveDegrees > 0 && k.octaveDegrees != k.count)
            {
                rounds = rotations;
                thisRound = cm - 1;
                if (thisRound < 0)
                {
                    thisRound = k.octaveDegrees - 1;
                    rounds--;
                }
            }
            else
            {
                rounds = (distanceFromScale0 - push - 1) / count;
                thisRound = (distanceFromScale0 - push - 1) % count;
            }
        }

        if (thisRound < 0)
        {
            thisRound += count;
            rounds -= 1;
        }

        if (disable)
        {
            pitches[at] = -12.0 + reference; // the pin's pitches[i] = 0, plus pitchMod
            mapped[at] = false;
        }
        else
        {
            if (thisRound < 0 || thisRound >= count)
            {
                return std::unexpected(std::string("a key of the mapping is past the scale"));
            }
            const double p = tones[static_cast<size_t>(thisRound)] +
                             rounds * tones[static_cast<size_t>(count - 1)] -
                             tuningCenterPitchOffset;
            pitches[at] = p + reference;
            mapped[at] = true;
        }
    }

    if (std::find(mapped.begin(), mapped.end(), true) == mapped.end())
    {
        return std::unexpected(std::string("the mapping maps no key"));
    }

    // withSkippedNotesInterpolated at the pin: indices 1 to N − 2, linear by key number between
    // the nearest mapped neighbours (from the uninterpolated table), with the pin's float weights;
    // past either end, the nearest mapped neighbour's value (plan-level call A3).
    std::array<double, kTableKeys> interpolated = pitches;
    for (int i = 1; i < kTableKeys - 1; ++i)
    {
        if (mapped[static_cast<size_t>(i)])
        {
            continue;
        }
        int nxt = i + 1;
        int prv = i - 1;
        while (prv >= 0 && !mapped[static_cast<size_t>(prv)])
        {
            prv--;
        }
        while (nxt < kTableKeys && !mapped[static_cast<size_t>(nxt)])
        {
            nxt++;
        }
        const auto at = static_cast<size_t>(i);
        if (prv < 0)
        {
            interpolated[at] = pitches[static_cast<size_t>(nxt)];
        }
        else if (nxt >= kTableKeys)
        {
            interpolated[at] = pitches[static_cast<size_t>(prv)];
        }
        else
        {
            const auto dist = static_cast<float>(nxt - prv);
            const float frac = static_cast<float>(i - prv) / dist;
            interpolated[at] = (1.0 - frac) * pitches[static_cast<size_t>(prv)] +
                               frac * pitches[static_cast<size_t>(nxt)];
        }
    }

    TuningTable table;
    bool anyMapped = false;
    for (int key = 0; key < 128; ++key)
    {
        const auto at = static_cast<size_t>(kTableOffset + key);
        const double v = interpolated[at];
        if (std::isnan(v))
        {
            return std::unexpected(std::string("the scale's pitches are out of range"));
        }
        table.keySemitones[static_cast<size_t>(key)] = snapAndClampSemitones(v); // step 5
        if (mapped[at])
        {
            anyMapped = true;
        }
        else
        {
            const auto k64 = static_cast<unsigned>(key);
            table.unmapped[k64 >> 6u] |= uint64_t{1} << (k64 & 63u);
        }
    }
    if (!anyMapped)
    {
        return std::unexpected(std::string("the mapping maps no key"));
    }
    return table;
}

/// The `.kbm`'s reference-frequency line, the sixth line not starting with `!`, as
/// readKBMStream finds it.
std::string_view referenceLine(std::string_view kbm)
{
    int seen = 0;
    for (const auto line : splitLines(kbm))
    {
        if (!line.empty() && line[0] == '!')
        {
            continue;
        }
        if (seen++ == 5)
        {
            return line;
        }
    }
    return {};
}

/// Our checks on the parsed mapping, before tuning-library's constructor validates it: what it
/// would spend unbounded time or memory on, and a mapping with no mapped key.
std::string refuseMapping(const Tunings::KeyboardMapping &k, int scaleCount)
{
    if (k.count > 0 && std::all_of(k.keys.begin(), k.keys.end(), [](int key) { return key < 0; }))
    {
        return "the mapping maps no key";
    }
    if (k.middleNote < -kMaxMappingNote || k.middleNote > kMaxMappingNote ||
        k.tuningConstantNote < -kMaxMappingNote || k.tuningConstantNote > kMaxMappingNote)
    {
        return "a note of the mapping is out of range";
    }
    // The pin's kbmRotations, in integers: the unwrapped scale holds count × rotations tones and
    // the octave degree becomes octaveDegrees × rotations. A valid pair has the second at most
    // the first, so bounding both refuses nothing tuning-library could use.
    int64_t rotations = 1;
    for (const int key : k.keys)
    {
        if (key > 0)
        {
            rotations =
                std::max(rotations, (static_cast<int64_t>(key) + scaleCount - 1) / scaleCount);
        }
    }
    if (rotations > 1 &&
        (rotations * scaleCount > kMaxUnwrappedTones ||
         rotations * static_cast<int64_t>(std::max(k.octaveDegrees, 0)) > kMaxUnwrappedTones))
    {
        return "a key of the mapping is out of range";
    }
    return {};
}

/// A `.kbm`'s bytes past ASCII outside its comments: tuning-library would pass them to
/// std::isdigit as negative chars, which is undefined (MSVC's debug CRT asserts).
bool kbmHasHighByte(std::string_view kbm)
{
    for (const auto line : splitLines(kbm))
    {
        if (!line.empty() && line[0] == '!')
        {
            continue;
        }
        for (const char c : line)
        {
            if (static_cast<unsigned char>(c) >= 0x80u)
            {
                return true;
            }
        }
    }
    return false;
}

std::expected<TuningTable, std::string> fromScala(std::string_view sclIn, std::string_view kbmIn)
{
    GG_FP_CONTRACT_OFF
    if (auto why = refuseText(sclIn, "scale"); !why.empty())
    {
        return std::unexpected(std::move(why));
    }
    if (auto why = refuseText(kbmIn, "mapping"); !why.empty())
    {
        return std::unexpected(std::move(why));
    }
    const std::string_view sclText = withoutBom(sclIn);
    const std::string_view kbmText = withoutBom(kbmIn);
    if (!kbmText.empty() && kbmHasHighByte(kbmText))
    {
        return std::unexpected(
            std::string("the mapping holds a byte past ASCII outside a comment"));
    }

    // Step 2: parse, then validate with tuning-library's own constructor.
    Tunings::Scale scale;
    Tunings::KeyboardMapping mapping;
    try
    {
        scale = sclIn.empty() ? Tunings::evenTemperament12NoteScale()
                              : Tunings::parseSCLData(std::string(sclText));
        if (!kbmIn.empty())
        {
            mapping = Tunings::parseKBMData(std::string(kbmText));
        }
    }
    catch (const std::exception &e)
    {
        return std::unexpected(std::string(e.what()));
    }
    catch (...)
    {
        return std::unexpected(std::string("the scale didn't parse"));
    }
    if (scale.count <= 0 || static_cast<int>(scale.tones.size()) != scale.count)
    {
        return std::unexpected(std::string("the scale has no notes"));
    }
    if (auto why = refuseMapping(mapping, scale.count); !why.empty())
    {
        return std::unexpected(std::move(why));
    }
    try
    {
        const Tunings::Tuning validated(scale, mapping);
        (void)validated;
    }
    catch (const std::exception &e)
    {
        return std::unexpected(std::string(e.what()));
    }
    catch (...)
    {
        return std::unexpected(std::string("the scale didn't parse"));
    }

    // Step 3: each tone in semitones, exactly.
    const double log2e = logDet(2.0);
    std::vector<double> tones;
    tones.reserve(scale.tones.size());
    for (const auto &tone : scale.tones)
    {
        std::string_view line = tone.stringRep;
        if (const auto bang = line.find('!'); bang != std::string_view::npos)
        {
            line = line.substr(0, bang);
        }
        double semitones = 0.0;
        if (tone.type == Tunings::Tone::kToneCents)
        {
            if (!parseDecimalShifted(line, 2, semitones) || !std::isfinite(semitones))
            {
                return std::unexpected(std::string("a cents value in the scale is out of range"));
            }
        }
        else
        {
            int64_t n = 0;
            int64_t d = 1;
            const auto slash = line.find('/');
            const bool ok =
                slash == std::string_view::npos
                    ? parseInt64(line, n)
                    : parseInt64(line.substr(0, slash), n) && parseInt64(line.substr(slash + 1), d);
            if (!ok || n <= 0 || d <= 0)
            {
                return std::unexpected(std::string("a ratio in the scale is out of range"));
            }
            semitones = 12.0 * logDet(static_cast<double>(n) / static_cast<double>(d)) / log2e;
        }
        tones.push_back(semitones);
    }

    // The reference: key 60 at kC4Hz exactly without a `.kbm` (spec §5.2), else its frequency
    // parsed exactly, in Hz.
    double frequency = TuningTable::kC4Hz;
    if (!kbmIn.empty())
    {
        if (!parseDecimalShifted(referenceLine(kbmText), 0, frequency) || !(frequency > 0.0) ||
            !std::isfinite(frequency))
        {
            return std::unexpected(
                std::string("the mapping's reference frequency must be positive and finite"));
        }
    }
    const double reference = 12.0 * logDet(frequency / TuningTable::kC4Hz) / log2e;

    return mapKeys(mapping, std::move(tones), reference);
}

} // namespace

std::expected<TuningTable, std::string> tuningFromScala(std::string_view scl, std::string_view kbm)
{
    try
    {
        return fromScala(scl, kbm);
    }
    catch (const std::exception &e)
    {
        return std::unexpected(std::string(e.what()));
    }
    catch (...)
    {
        return std::unexpected(std::string("the scale didn't parse"));
    }
}

std::string scalaDescription(std::string_view scl)
{
    for (const auto line : splitLines(withoutBom(scl)))
    {
        if (!line.empty() && line[0] == '!')
        {
            continue;
        }
        size_t b = 0;
        size_t e = line.size();
        const auto trimmed = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
        while (b < e && trimmed(line[b]))
        {
            ++b;
        }
        while (e > b && trimmed(line[e - 1]))
        {
            --e;
        }
        return std::string(line.substr(b, e - b));
    }
    return {};
}

bool parseExactDecimal(std::string_view text, double &out) noexcept
{
    return parseDecimalShifted(text, 0, out);
}

} // namespace gg::core
