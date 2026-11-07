// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The note table (M3 Voice spec §4.2): identity by ID or pair, the pedal, expression state,
// the full-table rule and the per-group lists.

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "gg/core/NoteTable.h"

using gg::core::ControlEvent;
using gg::core::ControlKind;
using gg::core::NoteEvent;
using gg::core::NoteEventType;
using gg::core::NoteTable;
using State = gg::core::NoteTable::State;

namespace
{

NoteEvent on(int16_t ch, int16_t key, int32_t id = -1, float vel = 0.8f)
{
    NoteEvent n{0, NoteEventType::On, ch, key, vel};
    n.noteId = id;
    return n;
}

NoteEvent off(int16_t ch, int16_t key, int32_t id = -1)
{
    NoteEvent n{0, NoteEventType::Off, ch, key};
    n.noteId = id;
    return n;
}

ControlEvent ctl(ControlKind kind, int16_t ch, float value, int16_t key = -1, int32_t id = -1)
{
    ControlEvent c{0, kind, ch, value};
    c.key = key;
    c.noteId = id;
    return c;
}

// The slots holding a note that is not Free, in slot order.
std::vector<size_t> live(const NoteTable &t)
{
    std::vector<size_t> v;
    for (size_t i = 0; i < t.notes().size(); ++i)
    {
        if (t.notes()[i].state != State::Free)
        {
            v.push_back(i);
        }
    }
    return v;
}

std::vector<uint8_t> keys(const NoteTable &t)
{
    std::array<uint8_t, 128> out{};
    uint32_t n = 0;
    t.heldKeys(out, n);
    return {out.begin(), out.begin() + n};
}

} // namespace

TEST_CASE("note_table_off_by_id", "[core][notes]")
{
    NoteTable t;
    t.apply(on(0, 60, 7), 1);
    t.apply(on(0, 60, 8), 2);
    t.apply(off(-1, -1, 8), 3);
    REQUIRE(live(t).size() == 2);
    CHECK(t.notes()[0].state == State::Held);
    CHECK(t.notes()[1].state == State::Released);
    CHECK(keys(t) == std::vector<uint8_t>{60});
    // An ID that names nothing is ignored, whatever pair comes with it.
    t.apply(off(0, 60, 99), 4);
    CHECK(t.notes()[0].state == State::Held);
    t.apply(off(0, 60, 7), 5);
    CHECK(keys(t).empty());
}

TEST_CASE("note_table_off_by_pair_releases_oldest", "[core][notes]")
{
    // Row 184: two notes on one pair are two notes.
    NoteTable t;
    t.apply(on(0, 60), 1);
    t.apply(on(0, 60), 2);
    t.apply(on(1, 60), 3);
    NoteEvent o = off(0, 60);
    o.releaseVelocity = 0.25f;
    t.apply(o, 4);
    CHECK(t.notes()[0].state == State::Released);
    CHECK(t.notes()[0].releaseVelocity == Catch::Approx(0.25f));
    CHECK(t.notes()[1].state == State::Held);
    CHECK(keys(t) == std::vector<uint8_t>{60});
    t.apply(off(0, 60), 5);
    CHECK(t.notes()[1].state == State::Released);
    CHECK(t.notes()[2].state == State::Held); // channel 1's
    CHECK(keys(t) == std::vector<uint8_t>{60});
    t.apply(off(0, 60), 6); // none held on the pair: nothing
    CHECK(t.notes()[2].state == State::Held);
}

TEST_CASE("note_table_wildcard_and_cc123", "[core][notes]")
{
    NoteTable t;
    t.apply(on(0, 60), 1);
    t.apply(on(1, 60), 2);
    t.apply(on(1, 64), 3);
    t.apply(on(2, 67), 4);
    t.apply(off(-1, 60), 5); // every channel's key 60 (R15 style wildcard)
    CHECK(keys(t) == std::vector<uint8_t>{64, 67});
    t.apply(off(1, -1), 6); // every key on channel 1
    CHECK(keys(t) == std::vector<uint8_t>{67});
    t.apply(on(1, 70), 7);
    t.apply({0, NoteEventType::AllOff, 1, 0}, 8); // CC 123 on channel 1
    CHECK(keys(t) == std::vector<uint8_t>{67});
    t.apply({0, NoteEventType::AllOff, -1, 0}, 9);
    CHECK(keys(t).empty());
}

TEST_CASE("note_table_pedal_holds_and_releases", "[core][notes]")
{
    NoteTable t;
    t.applyControl(ctl(ControlKind::Sustain, 0, 1.0f), false);
    CHECK(t.pedalDown(0, false));
    CHECK_FALSE(t.pedalDown(1, false));
    t.apply(on(0, 60), 1);
    t.apply(on(1, 64), 2);
    t.beginGroup();
    t.apply(off(0, 60), 3);
    t.apply(off(1, 64), 4);
    CHECK(t.notes()[0].state == State::PedalHeld);
    CHECK(t.notes()[1].state == State::Released); // channel 1's pedal is up
    CHECK(keys(t) == std::vector<uint8_t>{60});
    CHECK(t.releasedThisGroup().size() == 1); // a pedal-hold is not a release
    t.beginGroup();
    t.applyControl(ctl(ControlKind::Sustain, 0, 0.4f), false);
    CHECK(t.notes()[0].state == State::Released);
    CHECK(keys(t).empty());
    REQUIRE(t.releasedThisGroup().size() == 1);
    CHECK(t.releasedThisGroup()[0] == 0);
    // A note held before the pedal went down is held by it too.
    t.applyControl(ctl(ControlKind::Sustain, 2, 0.5f), false);
    t.apply(on(2, 72), 5);
    t.apply(off(2, 72), 6);
    CHECK(keys(t) == std::vector<uint8_t>{72});
}

TEST_CASE("note_table_mpe_master_pedal", "[core][notes]")
{
    NoteTable t;
    t.applyControl(ctl(ControlKind::Sustain, 0, 1.0f), true); // master channel
    CHECK(t.pedalDown(5, true));
    CHECK_FALSE(t.pedalDown(5, false));
    t.apply(on(5, 60), 1);
    t.apply(off(5, 60), 2);
    CHECK(t.notes()[0].state == State::PedalHeld);
    t.applyControl(ctl(ControlKind::Sustain, 0, 0.0f), true);
    CHECK(t.notes()[0].state == State::Released);
}

TEST_CASE("note_table_bend_before_on", "[core][notes]")
{
    NoteTable t;
    t.applyControl(ctl(ControlKind::PitchBend, 3, 1.0f), false);
    t.applyControl(ctl(ControlKind::Aftertouch, 3, 0.5f), false);
    t.applyControl(ctl(ControlKind::Timbre, 3, 0.25f), false);
    CHECK(t.channelBend(3) == Catch::Approx(1.0f));
    t.apply(on(3, 60), 1);
    t.apply(on(4, 60), 2);
    CHECK(t.notes()[0].bend == Catch::Approx(1.0f));
    CHECK(t.notes()[0].pressure == Catch::Approx(0.5f));
    CHECK(t.notes()[0].timbre == Catch::Approx(0.25f));
    CHECK(t.notes()[1].bend == 0.0f);
    // A later change moves its channel's notes; a polyphonic pressure takes the note's own.
    t.applyControl(ctl(ControlKind::PitchBend, 3, 0.5f), false);
    CHECK(t.notes()[0].bend == 0.0f);
    t.applyControl(ctl(ControlKind::PolyPressure, 3, 0.9f, 60), false);
    t.applyControl(ctl(ControlKind::Aftertouch, 3, 0.1f), false);
    CHECK(t.notes()[0].pressure == Catch::Approx(0.9f));
    CHECK(t.channelPressure(3) == Catch::Approx(0.1f));
    // MPE On: the master channel's values are kept, not copied into notes.
    t.applyControl(ctl(ControlKind::PitchBend, 0, 1.0f), true);
    t.apply(on(0, 62), 3);
    CHECK(t.channelBend(0) == Catch::Approx(1.0f));
    CHECK(t.notes()[2].bend == 0.0f);
}

TEST_CASE("note_table_expression_unknown_id", "[core][notes]")
{
    NoteTable t;
    t.apply(on(0, 60, 5), 1);
    t.applyControl(ctl(ControlKind::NoteTuning, 0, 2.0f, -1, 6), false); // unknown ID
    t.applyControl(ctl(ControlKind::NoteVolume, 0, 3.0f, 60, 6), false);
    CHECK(t.notes()[0].tuning == 0.0f);
    CHECK(t.notes()[0].volume == 1.0f);
    t.applyControl(ctl(ControlKind::NoteTuning, -1, 200.0f, -1, 5), false); // clamped
    t.applyControl(ctl(ControlKind::NoteVolume, -1, 9.0f, -1, 5), false);
    t.applyControl(ctl(ControlKind::NoteBrightness, -1, 0.3f, -1, 5), false);
    t.applyControl(ctl(ControlKind::NotePressure, -1, 0.6f, -1, 5), false);
    CHECK(t.notes()[0].tuning == Catch::Approx(120.0f));
    CHECK(t.notes()[0].volume == Catch::Approx(4.0f));
    CHECK(t.notes()[0].timbre == Catch::Approx(0.3f));
    CHECK(t.notes()[0].pressure == Catch::Approx(0.6f));
    // By (channel, key) when no ID; non-finite ignored.
    t.applyControl(ctl(ControlKind::NoteTuning, 0, -3.0f, 60), false);
    CHECK(t.notes()[0].tuning == Catch::Approx(-3.0f));
    t.applyControl(ctl(ControlKind::NoteTuning, 0, std::numeric_limits<float>::quiet_NaN(), 60),
                   false);
    CHECK(t.notes()[0].tuning == Catch::Approx(-3.0f));
}

TEST_CASE("note_table_full_drops_oldest_released", "[core][notes]")
{
    NoteTable t;
    for (int i = 0; i < 128; ++i)
    {
        t.apply(on(0, static_cast<int16_t>(i), i), static_cast<uint64_t>(i + 1));
    }
    CHECK(live(t).size() == 128);
    // Released notes 5 and 9 (5 older): the 129th replaces 5, the 130th replaces 9.
    t.apply(off(0, 9, 9), 200);
    t.apply(off(0, 5, 5), 201);
    t.apply(on(1, 100, 1000), 300);
    CHECK(t.notes()[5].id == 1000);
    CHECK(t.notes()[9].id == 9);
    t.apply(on(1, 101, 1001), 301);
    CHECK(t.notes()[9].id == 1001);
    // No Released note: the oldest PedalHeld goes, before any Held one.
    t.applyControl(ctl(ControlKind::Sustain, 0, 1.0f), false);
    t.apply(off(0, 70, 70), 302);
    t.apply(off(0, 20, 20), 303); // ordinal 21: older than 71
    t.apply(on(1, 102, 1002), 304);
    CHECK(t.notes()[20].id == 1002);
    t.apply(on(1, 103, 1003), 305);
    CHECK(t.notes()[70].id == 1003);
    // All Held: the oldest, ordinal 1 (slot 0).
    t.apply(on(1, 104, 1004), 306);
    CHECK(t.notes()[0].id == 1004);
    CHECK(live(t).size() == 128);
}

TEST_CASE("note_table_full_reports_replaced", "[core][notes]")
{
    // apply() reports the note an On replaces in a full table, as it was, and nothing else.
    NoteTable t;
    NoteTable::Note replaced{};
    for (int i = 0; i < 128; ++i)
    {
        CHECK_FALSE(
            t.apply(on(3, static_cast<int16_t>(i), i), static_cast<uint64_t>(i + 1), &replaced));
    }
    CHECK_FALSE(t.apply(off(3, 7, 7), 200, &replaced)); // an Off replaces nothing
    REQUIRE(t.apply(on(1, 100, 1000), 300, &replaced));
    CHECK(replaced.id == 7); // the oldest Released
    CHECK(replaced.channel == 3);
    CHECK(replaced.key == 7);
    CHECK(replaced.state == State::Released);
    REQUIRE(t.apply(on(1, 101, 1001), 301)); // no out pointer: still reported
    CHECK(t.notes()[0].id == 1001);          // then the oldest Held
}

TEST_CASE("voice_grouping_short_tap", "[core][notes]")
{
    NoteTable t;
    t.beginGroup();
    t.apply(on(0, 60), 1);
    t.apply(off(0, 60), 2);
    REQUIRE(t.startedThisGroup().size() == 1);
    REQUIRE(t.releasedThisGroup().size() == 1);
    CHECK(t.startedThisGroup()[0] == t.releasedThisGroup()[0]);
    CHECK(keys(t).empty());
    t.beginGroup();
    CHECK(t.startedThisGroup().empty());
    CHECK(t.releasedThisGroup().empty());
}
