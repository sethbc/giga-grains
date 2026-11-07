// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The tuning table from Scala texts (M3 tuning spec §4.2, §5.2, §7): the exact cents parse, the
// refusals, unmapped keys, and the deterministic port checked against tuning-library itself over
// its own corpus. libm (log2, strtod) appears here only as the independent reference.

#include <algorithm>
#include <bit>
#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <Tunings.h>

#include "gg/core/Tuning.h"

using gg::core::parseExactDecimal;
using gg::core::scalaDescription;
using gg::core::tuningFromScala;
using gg::core::TuningTable;

namespace
{

/// A `.scl` text: a comment, the description, the count and one tone a line.
std::string makeScl(const std::string &description, const std::vector<std::string> &tones,
                    const char *eol = "\n")
{
    std::string s = std::string("! test.scl") + eol + "!" + eol + description + eol + " " +
                    std::to_string(tones.size()) + eol + "!" + eol;
    for (const auto &t : tones)
    {
        s += " " + t + eol;
    }
    return s;
}

/// A `.kbm` text: size, first and last keys, middle note, reference key and frequency, octave
/// degree and the keys (`x` for an unmapped one).
std::string makeKbm(int size, int first, int last, int middle, int reference,
                    const std::string &frequency, int degree,
                    const std::vector<std::string> &keys = {})
{
    std::ostringstream o;
    o << "! test.kbm\n"
      << size << "\n"
      << first << "\n"
      << last << "\n"
      << middle << "\n"
      << reference << "\n"
      << frequency << "\n"
      << degree << "\n";
    for (const auto &k : keys)
    {
        o << k << "\n";
    }
    return o.str();
}

std::vector<std::string> twelveEdoTones()
{
    std::vector<std::string> t;
    for (int i = 1; i <= 12; ++i)
    {
        t.push_back(std::to_string(i * 100) + ".0");
    }
    return t;
}

/// Cents with 12 decimals (15 significant digits for steps under 1000 cents).
std::string cents(double c) { return std::format("{:.12f}", c); }

std::string readFile(const std::filesystem::path &p)
{
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool sameBytes(const TuningTable &a, const TuningTable &b)
{
    return std::memcmp(&a, &b, sizeof(TuningTable)) == 0;
}

/// The pentatonic scale on the white keys C D E G A (degrees 0 to 4), A4 = 440 on key 69.
const std::vector<std::string> kPentatonic{"9/8", "5/4", "3/2", "5/3", "2/1"};
std::string pentatonicKbm()
{
    return makeKbm(12, 0, 127, 60, 69, "440.0", 5,
                   {"0", "x", "1", "x", "2", "x", "x", "3", "x", "4", "x", "x"});
}

} // namespace

TEST_CASE("a twelve edo scl alone is exactly twelve tet", "[core][tuning]")
{
    const auto t = tuningFromScala(makeScl("12-EDO", twelveEdoTones()), "");
    REQUIRE(t.has_value());
    CHECK(sameBytes(*t, TuningTable::twelveTet()));
    CHECK(TuningTable::twelveTet().keySemitones[60] == 0.0);
    CHECK(TuningTable::twelveTet().keySemitones[0] == -60.0);
    CHECK(TuningTable::twelveTet().keySemitones[127] == 67.0);
}

TEST_CASE("a twelve edo scl with a440 on key 69 is exactly twelve tet", "[core][tuning]")
{
    const auto scl = makeScl("12-EDO", twelveEdoTones());
    for (const auto &kbm : {makeKbm(0, 0, 127, 60, 69, "440.0", 0),
                            makeKbm(12, 0, 127, 60, 69, "440", 12,
                                    {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"}),
                            makeKbm(0, 0, 127, 57, 57, "220.000", 0)})
    {
        CAPTURE(kbm);
        const auto t = tuningFromScala(scl, kbm);
        REQUIRE(t.has_value());
        CHECK(sameBytes(*t, TuningTable::twelveTet()));
    }
}

TEST_CASE("an empty scl with an empty kbm is twelve tet", "[core][tuning]")
{
    const auto t = tuningFromScala("", "");
    REQUIRE(t.has_value());
    CHECK(sameBytes(*t, TuningTable::twelveTet()));
}

TEST_CASE("a kbm alone maps twelve edo", "[core][tuning]")
{
    const auto t = tuningFromScala("", makeKbm(0, 0, 127, 60, 69, "432.0", 0));
    REQUIRE(t.has_value());
    const double offset = 12.0 * std::log2(432.0 / 440.0);
    for (int k = 0; k < 128; ++k)
    {
        CAPTURE(k);
        CHECK(std::abs(t->keySemitones[static_cast<size_t>(k)] - ((k - 60) + offset)) < 1e-12);
        CHECK_FALSE(t->isUnmapped(k));
    }
    CHECK(std::abs(t->keySemitones[69] - (9.0 + offset)) < 1e-12);
}

TEST_CASE("nineteen edo, bohlen pierce and a just scale match their formulas", "[core][tuning]")
{
    SECTION("19-EDO")
    {
        std::vector<std::string> tones;
        for (int j = 1; j < 19; ++j)
        {
            tones.push_back(cents(1200.0 * j / 19.0));
        }
        tones.emplace_back("2/1");
        const auto t = tuningFromScala(makeScl("19-EDO", tones), "");
        REQUIRE(t.has_value());
        for (int k = 0; k < 128; ++k)
        {
            CAPTURE(k);
            CHECK(std::abs(t->keySemitones[static_cast<size_t>(k)] - (k - 60) * 12.0 / 19.0) <
                  1e-12);
        }
    }
    SECTION("Bohlen-Pierce")
    {
        const double step = 1200.0 * std::log2(3.0) / 13.0;
        std::vector<std::string> tones;
        for (int j = 1; j < 13; ++j)
        {
            tones.push_back(cents(step * j));
        }
        tones.emplace_back("3/1");
        const auto t = tuningFromScala(makeScl("Bohlen-Pierce", tones), "");
        REQUIRE(t.has_value());
        for (int k = 0; k < 128; ++k)
        {
            CAPTURE(k);
            CHECK(std::abs(t->keySemitones[static_cast<size_t>(k)] -
                           (k - 60) * 12.0 * std::log2(3.0) / 13.0) < 1e-12);
        }
    }
    SECTION("5-limit just")
    {
        const std::vector<std::pair<int, int>> ratios{{16, 15}, {9, 8},   {6, 5},  {5, 4},
                                                      {4, 3},   {45, 32}, {3, 2},  {8, 5},
                                                      {5, 3},   {9, 5},   {15, 8}, {2, 1}};
        std::vector<std::string> tones;
        for (const auto &[n, d] : ratios)
        {
            tones.push_back(std::to_string(n) + "/" + std::to_string(d));
        }
        const auto t = tuningFromScala(makeScl("5-limit", tones), "");
        REQUIRE(t.has_value());
        for (int k = 0; k < 128; ++k)
        {
            CAPTURE(k);
            const int rel = k - 60;
            const int octave = rel >= 0 ? rel / 12 : -((-rel + 11) / 12);
            const int degree = rel - 12 * octave;
            const double within =
                degree == 0 ? 0.0
                            : 12.0 * std::log2(static_cast<double>(ratios[degree - 1].first) /
                                               ratios[degree - 1].second);
            CHECK(std::abs(t->keySemitones[static_cast<size_t>(k)] - (12.0 * octave + within)) <
                  1e-12);
        }
    }
}

TEST_CASE("tuning cents parse is exact", "[core][tuning]")
{
    const auto parsed = [](const char *text) {
        double v = -999.0;
        REQUIRE(parseExactDecimal(text, v));
        return v;
    };
    // Correctly rounded references (strtod is correctly rounded on every platform we build).
    for (const char *text :
         {"63.1578947368421052631578947", "-12.5", "+700.", "700.0 ! fifth", "1200 cents",
          "701.955000865387000000000000000", "\t 38.70968", "0.000001", ".5", "-0.25"})
    {
        CAPTURE(text);
        const std::string number(text, std::strcspn(text, "!c"));
        CHECK(parsed(text) == std::strtod(number.c_str(), nullptr));
    }
    CHECK(std::bit_cast<uint64_t>(parsed("63.1578947368421052631578947")) == 0x404F9435E50D7943ull);
    CHECK(std::bit_cast<uint64_t>(parsed("-12.5")) == 0xC029000000000000ull);
    CHECK(parsed("+700.") == 700.0);
    CHECK(parsed("700.0 ! fifth") == 700.0);
    CHECK(parsed("1200 cents") == 1200.0);

    // Past 15 significant digits with N ≥ 2^53 the spec drops the rest first (§5.2 step 3):
    // 701.955000865387 and its 15 more digits gives 701.955000865387's double, within 1e-12 of
    // the correctly rounded value.
    const double longCents = parsed("701.955000865387417744486732737");
    CHECK(std::bit_cast<uint64_t>(longCents) == 0x4085EFA3D77E63E9ull);
    CHECK(longCents == 701955000865387.0 / 1e12);
    CHECK(std::abs(longCents - std::strtod("701.955000865387417744486732737", nullptr)) < 1e-12);

    double untouched = 5.0;
    for (const char *bad : {"", "  ", "!700.0", "abc", ".", "-", "+.", "- 1"})
    {
        CAPTURE(bad);
        CHECK_FALSE(parseExactDecimal(bad, untouched));
    }
    CHECK(untouched == 5.0);
}

TEST_CASE("tuning cents parse ignores CR, BOM and tabs", "[core][tuning]")
{
    const std::vector<std::string> tones{"63.15789", "126.31579", "189.47368", "3/2", "1200.0"};
    const auto lf = tuningFromScala(makeScl("five tones", tones), "");
    REQUIRE(lf.has_value());

    std::vector<std::string> tabbed;
    for (const auto &t : tones)
    {
        tabbed.push_back("\t" + t);
    }
    const std::string crlf = makeScl("five tones", tones, "\r\n");
    const std::string bom = "\xEF\xBB\xBF" + makeScl("five tones", tones);
    const std::string latin1 = makeScl("cinq tons \xE9t\xE9", tones);
    const std::string tabs = makeScl("five tones", tabbed);
    const std::string all = "\xEF\xBB\xBF" + makeScl("cinq tons \xE9t\xE9", tabbed, "\r\n");
    for (const auto &text : {crlf, bom, latin1, tabs, all})
    {
        CAPTURE(text);
        const auto t = tuningFromScala(text, "");
        REQUIRE(t.has_value());
        CHECK(sameBytes(*t, *lf));
    }
    // And a .kbm with CRLF and a BOM.
    const std::string kbmLf = makeKbm(0, 0, 127, 60, 69, "432.0", 0);
    std::string kbmCrlf = "\xEF\xBB\xBF";
    for (char c : kbmLf)
    {
        kbmCrlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
    }
    const auto a = tuningFromScala(makeScl("five tones", tones), kbmLf);
    const auto b = tuningFromScala(all, kbmCrlf);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(sameBytes(*a, *b));

    CHECK(scalaDescription(all) == "cinq tons \xE9t\xE9");
    CHECK(scalaDescription(makeScl("  padded\t ", tones)) == "padded");
    CHECK(scalaDescription("") == "");
    CHECK(scalaDescription("! only a comment\n") == "");
}

TEST_CASE("tuning ignores the locale", "[core][tuning]")
{
    const std::vector<std::string> tones{"63.15789", "126.31579", "3/2", "1200.0"};
    const auto scl = makeScl("four", tones);
    const auto kbm = makeKbm(0, 0, 127, 60, 69, "432.5", 0);
    const auto before = tuningFromScala(scl, kbm);
    REQUIRE(before.has_value());
    struct RestoreLocale
    {
        ~RestoreLocale() { std::setlocale(LC_ALL, "C"); }
    } restore;
    if (std::setlocale(LC_ALL, "de_DE.UTF-8") == nullptr)
    {
        SUCCEED("no de_DE.UTF-8 locale here; the check is skipped");
        return;
    }
    REQUIRE(std::strtod("1,5", nullptr) == 1.5); // the comma locale is in effect
    const auto during = tuningFromScala(scl, kbm);
    REQUIRE(during.has_value());
    CHECK(sameBytes(*before, *during));
}

TEST_CASE("tuning refuses oversized texts, NULs and bad mappings", "[core][tuning]")
{
    const auto twelve = makeScl("12-EDO", twelveEdoTones());
    const auto refusal = [](std::string_view scl, std::string_view kbm) {
        const auto t = tuningFromScala(scl, kbm);
        REQUIRE_FALSE(t.has_value());
        return t.error();
    };

    std::string big = twelve;
    big.resize(gg::core::kMaxTuningTextBytes + 1, '\n');
    CHECK(refusal(big, "") == "the scale is over 64 KiB");
    CHECK(refusal("", big) == "the mapping is over 64 KiB");
    std::string atLimit = twelve;
    atLimit.resize(gg::core::kMaxTuningTextBytes, '\n');
    CHECK(tuningFromScala(atLimit, "").has_value());

    std::string nul = twelve;
    nul[3] = '\0';
    CHECK(refusal(nul, "") == "the scale holds a NUL byte");
    std::string kbmNul = makeKbm(0, 0, 127, 60, 69, "440.0", 0);
    kbmNul[2] = '\0';
    CHECK(refusal("", kbmNul) == "the mapping holds a NUL byte");

    // tuning-library's own messages.
    CHECK(refusal("! none\n!\nno notes\n 0\n", "") == "Invalid SCL note count.");
    CHECK(refusal(makeScl("five", {"200.0", "400.0", "700.0", "900.0", "1200.0"}),
                  makeKbm(5, 0, 127, 60, 60, "261.6255653005986", 7, {"0", "1", "2", "3", "4"})) ==
          "Unable to apply mapping of size 7 to smaller scale of size 5");
    CHECK(refusal(twelve, makeKbm(12, 0, 127, 60, 61, "440.0", 12,
                                  {"0", "x", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"}))
              .starts_with("Keyboard mapping is tuning an unmapped key."));
    CHECK(refusal(twelve, "12\n0\n127\n60\n69\n440.0\n").starts_with("Incomplete KBM stream."));

    // Ours.
    CHECK(refusal(twelve, makeKbm(3, 0, 127, 60, 60, "440.0", 12, {"x", "x", "x"})) ==
          "the mapping maps no key");
    CHECK(refusal(makeScl("negative", {"-3/2", "2/1"}), "") ==
          "a ratio in the scale is out of range");
    CHECK(refusal(makeScl("huge", {"99999999999999999999/2", "2/1"}), "") ==
          "a ratio in the scale is out of range");

    // Ours, against a hostile .kbm that tuning-library would spend unbounded time or memory on:
    // a scale degree two billion steps up (its unwrap), a middle note a billion keys away, and
    // a byte past ASCII in a data line (its std::isdigit on a negative char).
    CHECK(
        refusal(makeScl("one", {"2/1"}), makeKbm(1, 0, 127, 60, 60, "440.0", 1, {"2000000000"})) ==
        "a key of the mapping is out of range");
    CHECK(refusal("", makeKbm(0, 0, 127, 1000000000, 60, "440.0", 0)) ==
          "a note of the mapping is out of range");
    CHECK(refusal("", makeKbm(0, 0, 127, 60, 69, "440.0\xE9", 0)) ==
          "the mapping holds a byte past ASCII outside a comment");
    CHECK(tuningFromScala("", "! a comment \xE9 is fine\n" + makeKbm(0, 0, 127, 60, 69, "440.0", 0))
              .has_value());
}

TEST_CASE("tuning agrees with tuning-library at the edges of its parse", "[core][tuning]")
{
    const auto theyAccept = [](const std::string &kbm) {
        try
        {
            (void)Tunings::Tuning(Tunings::evenTemperament12NoteScale(),
                                  Tunings::parseKBMData(kbm));
            return true;
        }
        catch (const std::exception &)
        {
            return false;
        }
    };

    SECTION("a byte past ASCII after the keys is trailing data, accepted by both")
    {
        const std::string kbm = "0\n0\n127\n60\n69\n440.0\n0\nnot\xE9s\n";
        REQUIRE(theyAccept(kbm));
        const auto ours = tuningFromScala("", kbm);
        REQUIRE(ours.has_value());
        const auto plain = tuningFromScala("", "0\n0\n127\n60\n69\n440.0\n0\n");
        REQUIRE(plain.has_value());
        CHECK(sameBytes(*ours, *plain));
        // After a one-key map's key line, too.
        const std::string afterKey = "1\n0\n127\n60\n60\n440\n1\n0\n\xE9t\xE9\n";
        REQUIRE(theyAccept(afterKey));
        CHECK(tuningFromScala("", afterKey).has_value());
    }
    SECTION("a byte past ASCII in a header or key line is refused by both")
    {
        for (const std::string &kbm : {std::string("0\n0\n127\n60\n69\n440.0\xE9\n0\n"),
                                       std::string("1\n0\n127\n60\n60\n440\n1\n0\xE9\n")})
        {
            CAPTURE(kbm);
            const auto ours = tuningFromScala("", kbm);
            REQUIRE_FALSE(ours.has_value());
            CHECK(ours.error() == "the mapping holds a byte past ASCII outside a comment");
        }
    }
    SECTION("a cents value with an exponent is refused")
    {
        for (const char *tone : {"1.2e2", "1.2E2", "1.e+2", "100.0e-1"})
        {
            CAPTURE(tone);
            const auto scl = makeScl("exponent", {tone, "2/1"});
            // tuning-library's stream reads the exponent (120 cents for 1.2e2); our exact parse
            // doesn't, so rather than read 1.2 we refuse.
            CHECK(Tunings::parseSCLData(scl).tones[0].cents != 1.2);
            const auto ours = tuningFromScala(scl, "");
            REQUIRE_FALSE(ours.has_value());
            CHECK(ours.error() == "a cents value in the scale has an exponent");
        }
        // An `e` that starts trailing text is no exponent.
        CHECK(tuningFromScala(makeScl("text", {"100.0 east", "2/1"}), "").has_value());
    }
    SECTION("a mapping whose 128 keys are all unmapped is refused, stricter than the pin")
    {
        // 300 keys, only the first mapped, its window starting at key 200: the mapped keys of the
        // 512 are -100 and 200, none of 0 to 127. tuning-library accepts it; we refuse (spec §5.2
        // step 4: a table with no mapped key plays nothing).
        std::vector<std::string> keys(300, "x");
        keys[0] = "0";
        const auto kbm = makeKbm(300, 0, 127, 200, 200, "440.0", 0, keys);
        REQUIRE(theyAccept(kbm));
        const auto ours = tuningFromScala("", kbm);
        REQUIRE_FALSE(ours.has_value());
        CHECK(ours.error() == "the mapping maps no key");
    }
}

TEST_CASE("tuning refuses a nonpositive or nonfinite reference", "[core][tuning]")
{
    const std::string inf400 = "1" + std::string(400, '0') + ".0";
    for (const std::string &f :
         {std::string("0"), std::string("0.000"), std::string("-440"), std::string("inf"),
          std::string("1e400"), std::string("1e30"), std::string("x"), inf400})
    {
        CAPTURE(f);
        const auto t = tuningFromScala("", makeKbm(0, 0, 127, 60, 69, f, 0));
        REQUIRE_FALSE(t.has_value());
        CHECK_FALSE(t.error().empty());
    }
    const auto zero = tuningFromScala("", makeKbm(0, 0, 127, 60, 69, "0.0", 0));
    REQUIRE_FALSE(zero.has_value());
    CHECK(zero.error() == "the mapping's reference frequency must be positive and finite");
}

TEST_CASE("tuning clamps an extreme reference", "[core][tuning]")
{
    const auto check = [](const gg::core::TuningTable &t) {
        for (double s : t.keySemitones)
        {
            CHECK(std::isfinite(s));
            CHECK(s >= -1024.0);
            CHECK(s <= 1024.0);
        }
    };
    const auto high = tuningFromScala("", makeKbm(0, 0, 127, 0, 0, "20000", 0));
    REQUIRE(high.has_value());
    check(*high);
    CHECK(std::abs(high->keySemitones[0] - 12.0 * std::log2(20000.0 / TuningTable::kC4Hz)) < 1e-12);

    const std::string tiny = "0." + std::string(300, '0') + "1";
    const auto low = tuningFromScala("", makeKbm(0, 0, 127, 60, 60, tiny, 0));
    REQUIRE(low.has_value());
    check(*low);
    CHECK(low->keySemitones[127] == -1024.0);

    // A one-step scale of ten octaves: keys 60 ± n reach ±120·n semitones, clamped.
    const auto wide = tuningFromScala(makeScl("ten octaves", {"12000.0"}), "");
    REQUIRE(wide.has_value());
    check(*wide);
    CHECK(wide->keySemitones[60] == 0.0);
    CHECK(wide->keySemitones[61] == 120.0);
    CHECK(wide->keySemitones[69] == 1024.0);
    CHECK(wide->keySemitones[0] == -1024.0);
}

TEST_CASE("unmapped keys are marked and interpolated as at the pin", "[core][tuning]")
{
    const auto t = tuningFromScala(makeScl("pentatonic", kPentatonic), pentatonicKbm());
    REQUIRE(t.has_value());

    const double ratioOf[5] = {1.0, 9.0 / 8.0, 5.0 / 4.0, 3.0 / 2.0, 5.0 / 3.0};
    const int degreeOf[12] = {0, -1, 1, -1, 2, -1, -1, 3, -1, 4, -1, -1};
    const auto expectedMapped = [&](int k) {
        const int rel = k - 60;
        const int octave = rel >= 0 ? rel / 12 : -((-rel + 11) / 12);
        const int degree = degreeOf[rel - 12 * octave];
        // A4 (degree 4 of octave 0) is 440 Hz.
        return 12.0 * std::log2(440.0 / TuningTable::kC4Hz) +
               12.0 * std::log2(ratioOf[degree] / ratioOf[4]) + 12.0 * octave;
    };
    for (int k = 0; k < 128; ++k)
    {
        CAPTURE(k);
        const int pc = k % 12;
        const bool unmapped =
            pc == 1 || pc == 3 || pc == 5 || pc == 6 || pc == 8 || pc == 10 || pc == 11;
        CHECK(t->isUnmapped(k) == unmapped);
        if (!unmapped)
        {
            CHECK(std::abs(t->keySemitones[static_cast<size_t>(k)] - expectedMapped(k)) < 1e-12);
            continue;
        }
        int prv = k - 1;
        int nxt = k + 1;
        while (t->isUnmapped(prv))
        {
            --prv;
        }
        while (t->isUnmapped(nxt))
        {
            ++nxt;
        }
        const double frac = static_cast<double>(k - prv) / (nxt - prv);
        const double expected = (1.0 - frac) * expectedMapped(prv) + frac * expectedMapped(nxt);
        CHECK(std::abs(t->keySemitones[static_cast<size_t>(k)] - expected) < 1e-6);
    }
    CHECK_FALSE(t->isUnmapped(-1));
    CHECK_FALSE(t->isUnmapped(128));

    // Below the first mapped key (A3): a 600-key mapping whose window starts at key 10 maps keys
    // 10 upward; 0 to 9, below every mapped key of the 512, take key 10's value.
    std::vector<std::string> keys;
    for (int j = 0; j < 600; ++j)
    {
        keys.push_back(j < 246 ? std::to_string(j % 12) : std::string("x"));
    }
    const auto edge = tuningFromScala(makeScl("12-EDO", twelveEdoTones()),
                                      makeKbm(600, 0, 127, 10, 69, "440.0", 12, keys));
    REQUIRE(edge.has_value());
    for (int k = 0; k < 10; ++k)
    {
        CAPTURE(k);
        CHECK(edge->isUnmapped(k));
        CHECK(edge->keySemitones[static_cast<size_t>(k)] == edge->keySemitones[10]);
    }
    CHECK_FALSE(edge->isUnmapped(10));

    // And tuning-library's own interpolated table agrees on all 128.
    const auto theirs =
        Tunings::Tuning(Tunings::evenTemperament12NoteScale(),
                        Tunings::parseKBMData(makeKbm(600, 0, 127, 10, 69, "440.0", 12, keys)))
            .withSkippedNotesInterpolated();
    for (int k = 0; k < 128; ++k)
    {
        CAPTURE(k);
        CHECK(edge->isUnmapped(k) == !theirs.isMidiNoteMapped(k));
        CHECK(std::abs(edge->keySemitones[static_cast<size_t>(k)] -
                       12.0 * std::log2(theirs.frequencyForMidiNote(k) / TuningTable::kC4Hz)) <
              1e-9);
    }
}

TEST_CASE("the port matches tuning-library over its corpus", "[core][tuning]")
{
    namespace fs = std::filesystem;
    const fs::path root = GG_TUNING_LIBRARY_DATA;
    REQUIRE(fs::is_directory(root));

    std::vector<fs::path> dirs{root};
    for (const auto &e : fs::directory_iterator(root))
    {
        if (e.is_directory() && e.path().filename() != "bad")
        {
            dirs.push_back(e.path());
        }
    }
    std::sort(dirs.begin(), dirs.end());

    int accepted = 0;
    int refused = 0;
    double largest = 0.0;
    const auto compare = [&](const std::string &label, const std::string &scl,
                             const std::string &kbm) {
        INFO(label);
        bool theyAccept = false;
        Tunings::Tuning theirs;
        try
        {
            const auto scale =
                scl.empty() ? Tunings::evenTemperament12NoteScale() : Tunings::parseSCLData(scl);
            const auto mapping =
                kbm.empty() ? Tunings::KeyboardMapping() : Tunings::parseKBMData(kbm);
            theirs = Tunings::Tuning(scale, mapping).withSkippedNotesInterpolated();
            theyAccept = true;
        }
        catch (const std::exception &)
        {
        }
        const auto ours = tuningFromScala(scl, kbm);
        if (!theyAccept)
        {
            ++refused;
            CHECK_FALSE(ours.has_value());
            return;
        }
        ++accepted;
        REQUIRE(ours.has_value());
        for (int k = 0; k < 128; ++k)
        {
            CAPTURE(k);
            CHECK(ours->isUnmapped(k) == !theirs.isMidiNoteMapped(k));
            const double ref =
                12.0 * std::log2(theirs.frequencyForMidiNote(k) / TuningTable::kC4Hz);
            const double diff = std::abs(ours->keySemitones[static_cast<size_t>(k)] - ref);
            largest = std::max(largest, diff);
            CHECK(diff < 1e-9);
        }
    };

    // Every .scl and .kbm of every folder but bad/, each scale with every mapping (spec §7).
    std::vector<fs::path> scls;
    std::vector<fs::path> kbms;
    for (const auto &dir : dirs)
    {
        for (const auto &e : fs::directory_iterator(dir))
        {
            if (!e.is_regular_file())
            {
                continue;
            }
            if (e.path().extension() == ".scl")
            {
                scls.push_back(e.path());
            }
            else if (e.path().extension() == ".kbm")
            {
                kbms.push_back(e.path());
            }
        }
    }
    std::sort(scls.begin(), scls.end());
    std::sort(kbms.begin(), kbms.end());
    const auto name = [&](const fs::path &p) { return fs::relative(p, root).generic_string(); };
    compare("(12-EDO) alone", "", "");
    for (const auto &k : kbms)
    {
        compare("(12-EDO) + " + name(k), "", readFile(k));
    }
    for (const auto &sp : scls)
    {
        const auto sclText = readFile(sp);
        compare(name(sp) + " alone", sclText, "");
        for (const auto &k : kbms)
        {
            compare(name(sp) + " + " + name(k), sclText, readFile(k));
        }
    }
    const int pairs = accepted + refused;
    CHECK(pairs == static_cast<int>(1 + kbms.size() + scls.size() * (kbms.size() + 1)));

    // bad/: each .scl alone and each .kbm over 12-EDO, where we agree with tuning-library. It
    // refuses all but three, whose trailing data it accepts by design (its own alltests.cpp:
    // "Trailing data is OK"); those three tables must match like any other.
    int bad = 0;
    const int acceptedBefore = accepted;
    std::vector<fs::path> badFiles;
    for (const auto &e : fs::directory_iterator(root / "bad"))
    {
        badFiles.push_back(e.path());
    }
    std::sort(badFiles.begin(), badFiles.end());
    std::vector<std::string> badAccepted;
    for (const auto &p : badFiles)
    {
        const bool isScl = p.extension() == ".scl";
        if (!isScl && p.extension() != ".kbm")
        {
            continue; // .ascl is out of scope (T14)
        }
        const auto text = readFile(p);
        const int before = accepted;
        compare("bad/" + p.filename().string(), isScl ? text : std::string(),
                isScl ? std::string() : text);
        if (accepted != before)
        {
            badAccepted.push_back(p.filename().string());
        }
        ++bad;
    }
    CHECK(badAccepted ==
          std::vector<std::string>{"empty-extra.kbm", "extraline-long.kbm", "extraline.scl"});
    CHECK(bad - (accepted - acceptedBefore) >= 7);

    INFO("pairs " << pairs << "; with bad/, accepted " << accepted << ", refused " << refused
                  << ", bad " << bad << ", largest " << largest);
    CHECK(accepted > 100);
    CHECK(bad >= 8);
    CHECK(largest < 1e-9);
}
