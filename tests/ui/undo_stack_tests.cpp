// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// UndoStack: the editor's framework-free undo and redo history (spec §6.8). Each test drives the
// stack against a model of the plugin's values and routes, which the callbacks write and log, so
// these need no editor or JUCE and also run in the sanitizer builds.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/ui/UndoStack.h"

using gg::core::ModRoutes;
using gg::core::ParamSlot;
using gg::core::ParamValues;
using gg::ui::UndoStack;

namespace
{
/// The plugin as the stack sees it: the values and routes the callbacks set, and a log of every
/// call in order.
struct Model
{
    ParamValues values{};
    ModRoutes routes{};
    std::vector<std::pair<ParamSlot, float>> valueCalls;
    std::vector<ModRoutes> routeCalls;

    gg::ui::UndoCallbacks callbacks()
    {
        return {.setValue =
                    [this](ParamSlot slot, float v) {
                        values[static_cast<std::size_t>(slot)] = v;
                        valueCalls.emplace_back(slot, v);
                    },
                .setRoutes =
                    [this](const ModRoutes &r) {
                        routes = r;
                        routeCalls.push_back(r);
                    }};
    }
    void clearCalls()
    {
        valueCalls.clear();
        routeCalls.clear();
    }
};

/// Routes with slot `slot` sending LFO 1 to Size at `depth`.
ModRoutes routesWith(std::size_t slot, float depth)
{
    ModRoutes r{};
    r[slot].source = gg::core::ModSource::Lfo1;
    r[slot].target = ParamSlot::Size;
    r[slot].depth = depth;
    return r;
}
} // namespace

TEST_CASE("a gesture makes one entry", "[ui]")
{
    Model m;
    UndoStack s{m.callbacks()};
    s.beginGesture(ParamSlot::Size, 0.2f, "A Size");
    s.endGesture(ParamSlot::Size, 0.7f);
    CHECK(s.size() == 1);
    CHECK(s.canUndo());
    CHECK_FALSE(s.canRedo());
    CHECK(s.undoLabel() == "A Size");
    CHECK(s.redoLabel().empty());

    s.undo();
    REQUIRE(m.valueCalls.size() == 1);
    CHECK(m.valueCalls[0] == std::pair{ParamSlot::Size, 0.2f});
    CHECK(m.routeCalls.empty());
    CHECK_FALSE(s.canUndo());
    CHECK(s.canRedo());
    CHECK(s.redoLabel() == "A Size");
    CHECK(s.undoLabel().empty());

    m.clearCalls();
    s.redo();
    REQUIRE(m.valueCalls.size() == 1);
    CHECK(m.valueCalls[0] == std::pair{ParamSlot::Size, 0.7f});
    CHECK(s.canUndo());
    CHECK_FALSE(s.canRedo());
    CHECK(s.size() == 1);
}

TEST_CASE("an unchanged gesture makes none", "[ui]")
{
    Model m;
    UndoStack s{m.callbacks()};
    s.beginGesture(ParamSlot::Size, 0.4f, "A Size");
    s.endGesture(ParamSlot::Size, 0.4f);
    CHECK(s.size() == 0);
    CHECK_FALSE(s.canUndo());

    SECTION("an end with no begin makes none")
    {
        s.endGesture(ParamSlot::Pitch, 0.9f);
        CHECK(s.size() == 0);
    }
    SECTION("a nested begin of the same slot keeps the first before")
    {
        s.beginGesture(ParamSlot::Pitch, 0.1f, "A Pitch");
        s.beginGesture(ParamSlot::Pitch, 0.5f, "ignored");
        s.endGesture(ParamSlot::Pitch, 0.5f);
        REQUIRE(s.size() == 1);
        CHECK(s.undoLabel() == "A Pitch");
        s.undo();
        REQUIRE(m.valueCalls.size() == 1);
        CHECK(m.valueCalls[0] == std::pair{ParamSlot::Pitch, 0.1f});
    }
}

TEST_CASE("a group makes one entry of two gestures", "[ui]")
{
    Model m;
    UndoStack s{m.callbacks()};
    s.beginGroup();
    s.beginGesture(ParamSlot::MorphX, 0.5f, "Morph X");
    s.beginGesture(ParamSlot::MorphY, 0.5f, "Morph Y");
    s.endGesture(ParamSlot::MorphX, 0.1f);
    s.endGesture(ParamSlot::MorphY, 0.9f);
    s.endGroup();
    REQUIRE(s.size() == 1);
    CHECK(s.undoLabel() == "Morph X");

    s.undo();
    REQUIRE(m.valueCalls.size() == 2);
    CHECK(m.valueCalls[0] == std::pair{ParamSlot::MorphY, 0.5f});
    CHECK(m.valueCalls[1] == std::pair{ParamSlot::MorphX, 0.5f});
    CHECK_FALSE(s.canUndo());

    m.clearCalls();
    s.redo();
    REQUIRE(m.valueCalls.size() == 2);
    CHECK(m.valueCalls[0] == std::pair{ParamSlot::MorphX, 0.1f});
    CHECK(m.valueCalls[1] == std::pair{ParamSlot::MorphY, 0.9f});

    SECTION("a group whose gestures changed nothing makes none")
    {
        s.beginGroup();
        s.beginGesture(ParamSlot::MorphX, 0.1f, "Morph X");
        s.endGesture(ParamSlot::MorphX, 0.1f);
        s.endGroup();
        CHECK(s.size() == 1);
    }
}

TEST_CASE("a new entry clears redo", "[ui]")
{
    Model m;
    UndoStack s{m.callbacks()};
    s.beginGesture(ParamSlot::Size, 0.2f, "A Size");
    s.endGesture(ParamSlot::Size, 0.7f);
    s.beginGesture(ParamSlot::Pitch, 0.5f, "A Pitch");
    s.endGesture(ParamSlot::Pitch, 0.6f);
    s.undo();
    REQUIRE(s.canRedo());
    CHECK(s.redoLabel() == "A Pitch");

    s.beginGesture(ParamSlot::Density, 0.3f, "A Density");
    s.endGesture(ParamSlot::Density, 0.8f);
    CHECK_FALSE(s.canRedo());
    CHECK(s.redoLabel().empty());
    CHECK(s.size() == 2);
    CHECK(s.undoLabel() == "A Density");

    m.clearCalls();
    s.redo();
    CHECK(m.valueCalls.empty());
    s.undo();
    s.undo();
    REQUIRE(m.valueCalls.size() == 2);
    CHECK(m.valueCalls[0] == std::pair{ParamSlot::Density, 0.3f});
    CHECK(m.valueCalls[1] == std::pair{ParamSlot::Size, 0.2f});
}

TEST_CASE("the depth is 200", "[ui]")
{
    STATIC_REQUIRE(UndoStack::kDepth == 200);
    Model m;
    UndoStack s{m.callbacks()};
    for (int i = 0; i < 201; ++i)
    {
        s.beginGesture(ParamSlot::Size, static_cast<float>(i), "edit " + std::to_string(i));
        s.endGesture(ParamSlot::Size, static_cast<float>(i + 1));
    }
    CHECK(s.size() == 200);
    CHECK(s.undoLabel() == "edit 200");

    std::size_t undone = 0;
    while (s.canUndo())
    {
        s.undo();
        ++undone;
    }
    CHECK(undone == 200);
    // The oldest, "edit 0" (0 to 1), is gone: the last undo is "edit 1"'s.
    REQUIRE(m.valueCalls.size() == 200);
    CHECK(m.valueCalls.back() == std::pair{ParamSlot::Size, 1.0f});
    CHECK(s.redoLabel() == "edit 1");
}

TEST_CASE("undo on an empty stack does nothing", "[ui]")
{
    Model m;
    UndoStack s{m.callbacks()};
    CHECK_FALSE(s.canUndo());
    CHECK_FALSE(s.canRedo());
    CHECK(s.undoLabel().empty());
    CHECK(s.redoLabel().empty());
    s.undo();
    s.redo();
    CHECK(m.valueCalls.empty());
    CHECK(m.routeCalls.empty());
    CHECK(s.size() == 0);
}

TEST_CASE("undo applies its before whatever the value is now", "[ui]")
{
    Model m;
    UndoStack s{m.callbacks()};
    s.beginGesture(ParamSlot::Size, 0.2f, "A Size");
    s.endGesture(ParamSlot::Size, 0.7f);
    // Host automation moves the value; it pushes nothing.
    m.values[static_cast<std::size_t>(ParamSlot::Size)] = 0.95f;

    s.undo();
    REQUIRE(m.valueCalls.size() == 1);
    CHECK(m.valueCalls[0] == std::pair{ParamSlot::Size, 0.2f});
    CHECK(m.values[static_cast<std::size_t>(ParamSlot::Size)] == 0.2f);
}

TEST_CASE("a routes entry round-trips", "[ui]")
{
    Model m;
    UndoStack s{m.callbacks()};
    const ModRoutes before = routesWith(12, 0.25f);
    const ModRoutes after = routesWith(12, 0.75f);
    s.pushRoutes(before, after, "route 13");
    REQUIRE(s.size() == 1);
    CHECK(s.undoLabel() == "route 13");

    s.undo();
    REQUIRE(m.routeCalls.size() == 1);
    CHECK(m.routeCalls[0] == before);
    CHECK(m.valueCalls.empty());

    s.redo();
    REQUIRE(m.routeCalls.size() == 2);
    CHECK(m.routeCalls[1] == after);

    SECTION("equal routes make no entry")
    {
        s.pushRoutes(after, after, "route 13");
        CHECK(s.size() == 1);
    }
}

TEST_CASE("a snapshot entry applies only what differs", "[ui]")
{
    Model m;
    UndoStack s{m.callbacks()};
    ParamValues vBefore{};
    vBefore.fill(0.5f);
    ParamValues vAfter = vBefore;
    vAfter[static_cast<std::size_t>(ParamSlot::Texture)] = 0.9f;
    const ModRoutes rBefore{};
    const ModRoutes rAfter = routesWith(3, -0.5f);
    s.pushSnapshot("Mutate", vBefore, rBefore, vAfter, rAfter);
    REQUIRE(s.size() == 1);
    CHECK(s.undoLabel() == "Mutate");

    s.undo();
    REQUIRE(m.valueCalls.size() == 1);
    CHECK(m.valueCalls[0] == std::pair{ParamSlot::Texture, 0.5f});
    REQUIRE(m.routeCalls.size() == 1);
    CHECK(m.routeCalls[0] == rBefore);

    m.clearCalls();
    s.redo();
    REQUIRE(m.valueCalls.size() == 1);
    CHECK(m.valueCalls[0] == std::pair{ParamSlot::Texture, 0.9f});
    REQUIRE(m.routeCalls.size() == 1);
    CHECK(m.routeCalls[0] == rAfter);

    SECTION("unchanged routes are not set")
    {
        m.clearCalls();
        s.pushSnapshot("Recall", vAfter, rAfter, vBefore, rAfter);
        s.undo();
        CHECK(m.valueCalls.size() == 1);
        CHECK(m.routeCalls.empty());
    }
}
