// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The editor's canvas: its lanes, drags, drops and stress seam.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "PluginHarness.h"
#include "EditorTestUtil.h"
#include "support/AllocTripwire.h"

#if GG_BUILD_EDITOR

using namespace gg::test::editor;

TEST_CASE("a canvas drag emits begin, values, end once", "[plugin]")
{
    // Review Focus 5: a drag that ends outside the window, or with the editor closing, ends its
    // gesture exactly once, and the last value sent is the last drag position.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    constexpr float kTol = gg::ui::GrainCanvas::kEdgeTolerancePx;

    SECTION("the instrument's window body sets Position, released outside the canvas")
    {
        RecordingContext recording(gg::core::Mode::Instrument);
        recording.values[static_cast<std::size_t>(ParamSlot::Position)] = 0.5f;
        recording.values[static_cast<std::size_t>(ParamSlot::Spray)] = 0.4f;
        giveLayerASample(recording.context);
        recording.context.sampleRate = 48000.0;
        auto editor = gg::ui::makeEditor(recording.context);
        auto *canvas = canvasOf(*editor);
        REQUIRE(canvas != nullptr);
        const auto lane = laneOf(*canvas, 0, false, 48000.0, 48000);
        const float off = static_cast<float>(canvas->laneBounds(0).getX());
        const auto rect = gg::ui::scanRect({0.5f, 0.4f, 0.0f, 0.0f}, lane);
        const float centre = 0.5f * (rect.x0 + rect.x1);
        const float y = rect.y0;
        REQUIRE(gg::ui::hitTestRect(centre, y, rect, kTol, lane) == gg::ui::DragTarget::Body);

        const juce::Point<float> down{off + centre, y};
        canvas->mouseDown(mouseAt(*canvas, off + centre, y, down, false));
        for (float dx : {10.0f, 20.0f, 30.0f})
        {
            canvas->mouseDrag(mouseAt(*canvas, off + centre - dx, y, down, true));
        }
        // Released well outside the canvas, to the left of the editor.
        canvas->mouseUp(mouseAt(*canvas, -200.0f, y, down, true));
        REQUIRE(recording.kinds() == "bvvve");
        CHECK(recording.calls.front().slot == ParamSlot::Position);
        CHECK(recording.calls.back().slot == ParamSlot::Position);
        double previous = 0.5;
        for (std::size_t i = 1; i <= 3; ++i)
        {
            const GestureCall &c = recording.calls[i];
            CHECK(c.slot == ParamSlot::Position);
            CHECK(c.value < previous); // moving left is earlier in the file
            previous = c.value;
        }
        CHECK(recording.calls[3].value ==
              gg::ui::positionValueAtX(centre - 30.0f, lane)); // the last drag position

        // Nothing more: another release or the editor closing ends nothing again.
        canvas->mouseUp(mouseAt(*canvas, -200.0f, y, down, true));
        editor.reset();
        CHECK(recording.kinds() == "bvvve");
    }
    SECTION("the fx window's older edge sets Spray")
    {
        RecordingContext recording(gg::core::Mode::Fx);
        recording.values[static_cast<std::size_t>(ParamSlot::Position)] = 0.5f;
        recording.values[static_cast<std::size_t>(ParamSlot::Spray)] = 0.8f;
        recording.context.sampleRate = 48000.0;
        const auto editor = gg::ui::makeEditor(recording.context);
        auto *canvas = canvasOf(*editor);
        REQUIRE(canvas != nullptr);
        const auto lane = laneOf(*canvas, 0, true, 48000.0, 0);
        const float off = static_cast<float>(canvas->laneBounds(0).getX());
        const auto rect = gg::ui::scanRect({0.5f, 0.8f, 0.0f, 0.0f}, lane);
        const float y = rect.y0;
        REQUIRE(gg::ui::hitTestRect(rect.x0, y, rect, kTol, lane) == gg::ui::DragTarget::LeftEdge);

        const juce::Point<float> down{off + rect.x0, y};
        canvas->mouseDown(mouseAt(*canvas, off + rect.x0, y, down, false));
        canvas->mouseDrag(mouseAt(*canvas, off + rect.x0 - 40.0f, y, down, true));
        canvas->mouseUp(mouseAt(*canvas, off + rect.x0 - 40.0f, y, down, true));
        REQUIRE(recording.kinds() == "bve");
        for (const GestureCall &c : recording.calls)
        {
            CHECK(c.slot == ParamSlot::Spray);
        }
        CHECK(recording.calls[1].value > 0.8); // wider
        CHECK(recording.calls[1].value ==
              gg::ui::sprayValueForExtent(rect.x1 - (rect.x0 - 40.0f), lane));
    }
    SECTION("the fx window's body moves Position further back as it moves left")
    {
        RecordingContext recording(gg::core::Mode::Fx);
        recording.values[static_cast<std::size_t>(ParamSlot::Position)] = 0.5f;
        recording.values[static_cast<std::size_t>(ParamSlot::Spray)] = 0.8f;
        const auto editor = gg::ui::makeEditor(recording.context); // inactive: no sample rate
        auto *canvas = canvasOf(*editor);
        REQUIRE(canvas != nullptr);
        const auto lane = laneOf(*canvas, 0, true, 48000.0, 0);
        const float off = static_cast<float>(canvas->laneBounds(0).getX());
        const auto rect = gg::ui::scanRect({0.5f, 0.8f, 0.0f, 0.0f}, lane);
        const float x = off + rect.x1 - 2.0f; // Position's own edge is the body (ruling PU-T8a)
        const float y = rect.y0;
        const juce::Point<float> down{x, y};
        canvas->mouseDown(mouseAt(*canvas, x, y, down, false));
        canvas->mouseDrag(mouseAt(*canvas, x - 50.0f, y, down, true));
        canvas->mouseUp(mouseAt(*canvas, x - 50.0f, y, down, true));
        REQUIRE(recording.kinds() == "bve");
        CHECK(recording.calls[1].slot == ParamSlot::Position);
        CHECK(recording.calls[1].value > 0.5);
        CHECK_THAT(
            recording.calls[1].value,
            Catch::Matchers::WithinAbs(gg::ui::positionValueAtX(rect.x1 - 50.0f, lane), 1e-6));
    }
    SECTION("a drag still live when the editor closes ends there")
    {
        RecordingContext recording(gg::core::Mode::Instrument);
        giveLayerASample(recording.context);
        recording.values[static_cast<std::size_t>(ParamSlot::Position)] = 0.5f;
        recording.values[static_cast<std::size_t>(ParamSlot::Spray)] = 0.4f;
        auto editor = gg::ui::makeEditor(recording.context);
        auto *canvas = canvasOf(*editor);
        REQUIRE(canvas != nullptr);
        const juce::Rectangle<int> bounds = canvas->laneBounds(0);
        const float x =
            static_cast<float>(bounds.getX()) + 0.5f * static_cast<float>(bounds.getWidth());
        const float y = static_cast<float>(bounds.getCentreY());
        const juce::Point<float> down{x, y};
        canvas->mouseDown(mouseAt(*canvas, x, y, down, false));
        canvas->mouseDrag(mouseAt(*canvas, x + 5.0f, y, down, true));
        editor.reset();
        CHECK(recording.kinds() == "bve");
    }
    SECTION("a press outside the window, or with no sample, grabs nothing")
    {
        RecordingContext recording(gg::core::Mode::Instrument);
        recording.values[static_cast<std::size_t>(ParamSlot::Position)] = 0.5f;
        const auto editor = gg::ui::makeEditor(recording.context); // no sample: no window
        auto *canvas = canvasOf(*editor);
        REQUIRE(canvas != nullptr);
        const juce::Rectangle<int> bounds = canvas->laneBounds(0);
        const float x =
            static_cast<float>(bounds.getX()) + 0.5f * static_cast<float>(bounds.getWidth());
        const float y = static_cast<float>(bounds.getCentreY());
        canvas->mouseDown(mouseAt(*canvas, x, y, {x, y}, false));
        canvas->mouseDrag(mouseAt(*canvas, x + 5.0f, y, {x, y}, true));
        canvas->mouseUp(mouseAt(*canvas, x + 5.0f, y, {x, y}, true));

        giveLayerASample(recording.context);
        const float edge = static_cast<float>(bounds.getX()) + 2.0f;
        canvas->mouseDown(mouseAt(*canvas, edge, y, {edge, y}, false));
        canvas->mouseUp(mouseAt(*canvas, edge, y, {edge, y}, false));
        // And in the header strip left of the lanes.
        canvas->mouseDown(mouseAt(*canvas, 10.0f, y, {10.0f, y}, false));
        canvas->mouseUp(mouseAt(*canvas, 10.0f, y, {10.0f, y}, false));
        CHECK(recording.calls.empty());
    }
    SECTION("a right-button press on the window grabs nothing")
    {
        RecordingContext recording(gg::core::Mode::Instrument);
        recording.values[static_cast<std::size_t>(ParamSlot::Position)] = 0.5f;
        recording.values[static_cast<std::size_t>(ParamSlot::Spray)] = 0.4f;
        giveLayerASample(recording.context);
        recording.context.sampleRate = 48000.0;
        const auto editor = gg::ui::makeEditor(recording.context);
        auto *canvas = canvasOf(*editor);
        REQUIRE(canvas != nullptr);
        int menus = 0;
        canvas->showMenu = [&menus](juce::PopupMenu) { ++menus; }; // the lane menu (VW-P2a)
        const auto lane = laneOf(*canvas, 0, false, 48000.0, 48000);
        const float off = static_cast<float>(canvas->laneBounds(0).getX());
        const auto rect = gg::ui::scanRect({0.5f, 0.4f, 0.0f, 0.0f}, lane);
        const float y = rect.y0;
        constexpr int kRight = juce::ModifierKeys::rightButtonModifier;
        // The body and an edge, each of which the left button grabs.
        for (const float x : {0.5f * (rect.x0 + rect.x1), rect.x0})
        {
            REQUIRE(gg::ui::hitTestRect(x, y, rect, kTol, lane) != gg::ui::DragTarget::None);
            const juce::Point<float> down{off + x, y};
            canvas->mouseDown(mouseAt(*canvas, off + x, y, down, false, kRight));
            canvas->mouseDrag(mouseAt(*canvas, off + x + 20.0f, y, down, true, kRight));
            canvas->mouseUp(mouseAt(*canvas, off + x + 20.0f, y, down, true, kRight));
        }
        CHECK(recording.calls.empty());
        CHECK(menus == 2); // each press opened the lane's menu instead
    }
}

TEST_CASE("the canvas draws the grains the queue holds", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    constexpr double kRate = 48000.0;
    constexpr int64_t kNow = 480000; // 10 s in
    gg::core::GrainQueue queue;
    gg::core::SnapshotBuffer snapshots;
    std::atomic<int64_t> clock{kNow};
    RecordingContext recording(gg::core::Mode::Fx);
    recording.context.grains = &queue;
    recording.context.snapshot = &snapshots;
    recording.context.clock = &clock;
    recording.context.sampleRate = kRate;

    const auto editor = gg::ui::makeEditor(recording.context);
    auto *canvas = canvasOf(*editor);
    REQUIRE(canvas != nullptr);
    const float width = static_cast<float>(canvas->laneBounds(0).getWidth());
    const float height = static_cast<float>(canvas->laneBounds(0).getHeight()); // lane A's top is 0

    // The lane's vertical axis is pitch (M2 Views' D1): rate 4, two octaves up, lies half the
    // reach above the centre, and rate 1/2, an octave down, a quarter of the reach below it.
    const auto grain = [](int64_t onset, double behindSeconds, float rate) {
        return gg::core::GrainVis{onset, static_cast<double>(kNow) - behindSeconds * kRate,
                                  rate,  static_cast<float>(0.1 * kRate),
                                  1.0f,  0.0f};
    };
    const float radius = 3.0f;
    const float reach = 0.5f * height - radius;
    const auto accum = [canvas](float x, float y) {
        return canvas->accumAt(static_cast<uint32_t>(std::lround(x)),
                               static_cast<uint32_t>(std::lround(y)));
    };
    // Each grain's capsule spans its 100 ms read, at its rate: from 4 s back to 3.6 s back (rate
    // 4), and from 8 s back to 7.95 s back (rate 1/2); x4 and x8 are the reads' middles. The
    // clock's extrapolation between the two frames scrolls both left: x4 stays inside its
    // capsule for 200 ms of it, but x8 only for 25 ms, so a frame later than that misses x8.
    const float x4 = width * (1.0f - 3.8f / 16.0f);
    const float x8 = width * (1.0f - 7.975f / 16.0f);

    // Two grains of 100 ms that started just now, 4 s and 8 s behind the head, one pitched up
    // and one down; one that has already ended. Queued after the canvas's first frame, which
    // clears the queue (the next test).
    gg::core::UiSnapshot snapshot;
    snapshot.clock = kNow;
    snapshot.writeHead = kNow;
    snapshot.lanes[0].writeHead = kNow;
    snapshot.lanes[0].builtKind = 1; // Live
    snapshots.push(snapshot);
    canvas->renderFrame();
    REQUIRE(queue.push(grain(kNow - 100, 4.0, 4.0f)));
    REQUIRE(queue.push(grain(kNow - 100, 8.0, 0.5f)));
    REQUIRE(queue.push(grain(kNow - 20000, 12.0, 1.0f))); // ended at kNow - 15200

    canvas->renderFrame();
    CHECK(canvas->liveGrainCount() == 2);
    CHECK(accum(x4, 0.5f * height - 0.5f * reach) > 0.0f);
    CHECK(accum(x8, 0.5f * height + 0.25f * reach) > 0.0f);
    // Nothing where no grain plays: the ended grain's place, and the lane's middle.
    CHECK(accum(width * (1.0f - 11.95f / 16.0f), 0.5f * height) == 0.0f);
    CHECK(accum(0.5f * width, 0.5f * height) == 0.0f);

    // Once both have ended (the clock moved on 100 ms), the queue empty, nothing is drawn.
    clock.store(kNow + 4800);
    canvas->renderFrame();
    CHECK(canvas->liveGrainCount() == 0);
    CHECK(accum(x4, 0.5f * height - 0.5f * reach) == 0.0f);

    // Reactivated between two frames, with a clock that starts again from 0: the grains of the
    // new activation draw at once (the extrapolation restarts rather than waiting for the old
    // clock to come round).
    gg::core::GrainQueue queue2;
    gg::core::SnapshotBuffer snapshots2;
    std::atomic<int64_t> clock2{64};
    recording.context.grains = &queue2;
    recording.context.snapshot = &snapshots2;
    recording.context.clock = &clock2;
    gg::core::UiSnapshot fresh;
    fresh.clock = 64;
    fresh.writeHead = 64;
    fresh.lanes[0].writeHead = 64;
    fresh.lanes[0].builtKind = 1;
    snapshots2.push(fresh);
    canvas->renderFrame(); // a new queue: cleared first
    REQUIRE(queue2.push(gg::core::GrainVis{0, 64.0 - 4.0 * kRate, 4.0f, 4800.0f, 1.0f, 0.0f}));
    canvas->renderFrame();
    CHECK(canvas->liveGrainCount() == 1);
    CHECK(accum(x4, 0.5f * height - 0.5f * reach) > 0.0f);

    // Deactivated (the plugin nulls the pointers): the canvas draws its static parts only.
    recording.context.grains = nullptr;
    recording.context.snapshot = nullptr;
    recording.context.clock = nullptr;
    canvas->renderFrame();
    CHECK(canvas->liveGrainCount() == 0);
    CHECK(accum(x4, 0.5f * height - 0.5f * reach) == 0.0f);
}

TEST_CASE("the canvas drops a stale backlog and waits for a snapshot", "[plugin]")
{
    // A queue's backlog when the canvas first reads it was queued while nothing drew it (an
    // editor reopening): the first frame clears it. And the FX lane places no grain before a
    // snapshot gives it the write head. The grains last 10 s, so the clock's extrapolation
    // between frames (at most 0.25 s) can't end them.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    constexpr double kRate = 48000.0;
    constexpr int64_t kNow = 480000;
    gg::core::GrainQueue queue;
    gg::core::SnapshotBuffer snapshots;
    std::atomic<int64_t> clock{kNow};
    RecordingContext recording(gg::core::Mode::Fx);
    recording.context.grains = &queue;
    recording.context.snapshot = &snapshots;
    recording.context.clock = &clock;
    recording.context.sampleRate = kRate;
    const auto editor = gg::ui::makeEditor(recording.context);
    auto *canvas = canvasOf(*editor);
    REQUIRE(canvas != nullptr);
    const float width = static_cast<float>(canvas->laneBounds(0).getWidth());
    const float height = static_cast<float>(canvas->laneBounds(0).getHeight());
    // A grain reading 4.5 s .. 4 s behind a write head at 0 (the head the lane would take
    // without a snapshot), slowly, for 10 s: so low that it lies on the lane's bottom edge (the
    // vertical axis is pitch, M2 Views' D1).
    const gg::core::GrainVis grain{
        kNow - 100, -4.5 * kRate, 0.05f, static_cast<float>(10.0 * kRate), 1.0f, 0.0f};
    const float y = gg::ui::pitchY(0.05f, laneOf(*canvas, 0, true, kRate, 0), 3.0f);
    REQUIRE(y == height - 3.0f);
    const auto lit = [&] {
        return canvas->accumAt(static_cast<uint32_t>(std::lround(width * (1.0f - 4.4f / 16.0f))),
                               static_cast<uint32_t>(std::lround(y))) > 0.0f;
    };

    REQUIRE(queue.push(grain));
    canvas->renderFrame();
    CHECK(canvas->liveGrainCount() == 0);
    gg::core::GrainVis leftover;
    CHECK_FALSE(queue.pop(leftover));

    // Held but not drawn without a snapshot...
    REQUIRE(queue.push(grain));
    canvas->renderFrame();
    CHECK(canvas->liveGrainCount() == 1);
    CHECK_FALSE(lit());
    // ... and drawn once one arrives, with the head at 0.
    gg::core::UiSnapshot snapshot;
    snapshot.clock = kNow;
    snapshot.writeHead = 0;
    snapshot.lanes[0].writeHead = 0;
    snapshot.lanes[0].builtKind = 1;
    snapshots.push(snapshot);
    canvas->renderFrame();
    CHECK(canvas->liveGrainCount() == 1);
    CHECK(lit());
}

TEST_CASE("the canvas draws each layer's grains in its lane", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    constexpr double kRate = 48000.0;
    constexpr int64_t kNow = 480000;
    gg::core::GrainQueue queue;
    gg::core::SnapshotBuffer snapshots;
    std::atomic<int64_t> clock{kNow};
    RecordingContext recording(gg::core::Mode::Fx);
    recording.context.grains = &queue;
    recording.context.snapshot = &snapshots;
    recording.context.clock = &clock;
    recording.context.sampleRate = kRate;
    const auto editor = gg::ui::makeEditor(recording.context);
    auto *canvas = canvasOf(*editor);
    REQUIRE(canvas != nullptr);

    gg::core::UiSnapshot snapshot;
    snapshot.clock = kNow;
    snapshot.writeHead = kNow;
    for (auto &lane : snapshot.lanes)
    {
        lane.writeHead = kNow;
        lane.builtKind = 1; // Live
    }
    snapshots.push(snapshot);
    canvas->renderFrame(); // clears the queue: what follows is the first to be read

    // One grain per layer, at rate 1 (the lane's centre), each 4 s back, the same x.
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        REQUIRE(
            queue.push(gg::core::GrainVis{kNow - 100, static_cast<double>(kNow) - 4.0 * kRate, 1.0f,
                                          static_cast<float>(0.1 * kRate), 1.0f, 0.0f, layer}));
    }
    canvas->renderFrame();
    CHECK(canvas->liveGrainCount() == 4);
    const auto x = static_cast<uint32_t>(
        std::lround(static_cast<float>(canvas->laneBounds(0).getWidth()) * (1.0f - 3.95f / 16.0f)));
    const auto centreOf = [canvas](uint8_t layer) {
        return static_cast<uint32_t>(canvas->laneBounds(layer).getCentreY());
    };
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        INFO("lane " << int(layer));
        CHECK(canvas->accumAt(x, centreOf(layer)) > 0.0f);
    }
    // One layer's grain lights its own lane alone: the clock moves on past all four, then layer
    // C plays one (4 s behind the head, which has moved on with the clock).
    constexpr int64_t kLater = kNow + 48000;
    clock.store(kLater);
    canvas->renderFrame();
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        CHECK(canvas->accumAt(x, centreOf(layer)) == 0.0f);
    }
    REQUIRE(queue.push(gg::core::GrainVis{kLater - 100, static_cast<double>(kLater) - 4.0 * kRate,
                                          1.0f, static_cast<float>(0.1 * kRate), 1.0f, 0.0f, 2}));
    canvas->renderFrame();
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        INFO("lane " << int(layer));
        CHECK((canvas->accumAt(x, centreOf(layer)) > 0.0f) == (layer == 2));
    }
}

TEST_CASE("a rectangle drag on lane B sets layer B's parameters", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    using gg::core::LayerParam;
    using gg::core::layerSlot;
    constexpr float kTol = gg::ui::GrainCanvas::kEdgeTolerancePx;
    RecordingContext recording(gg::core::Mode::Fx);
    const auto set = [&](LayerParam p, float v) {
        recording.values[static_cast<std::size_t>(layerSlot(p, 1))] = v;
    };
    set(LayerParam::Position, 0.5f);
    set(LayerParam::Spray, 0.8f);
    set(LayerParam::Pitch, 0.0f);
    set(LayerParam::PitchSpread, 12.0f);
    recording.context.sampleRate = 48000.0;
    const auto editor = gg::ui::makeEditor(recording.context);
    auto *canvas = canvasOf(*editor);
    REQUIRE(canvas != nullptr);
    const auto lane = laneOf(*canvas, 1, true, 48000.0, 0);
    const float off = static_cast<float>(canvas->laneBounds(1).getX());
    const auto rect = [&] {
        const auto v = [&](LayerParam p) {
            return recording.values[static_cast<std::size_t>(layerSlot(p, 1))];
        };
        return gg::ui::scanRect({v(LayerParam::Position), v(LayerParam::Spray),
                                 v(LayerParam::Pitch), v(LayerParam::PitchSpread)},
                                lane);
    };
    const auto drag = [&](float x, float y, float dx, float dy) {
        const juce::Point<float> down{off + x, y};
        canvas->mouseDown(mouseAt(*canvas, off + x, y, down, false));
        canvas->mouseDrag(mouseAt(*canvas, off + x + dx, y + dy, down, true));
        canvas->mouseUp(mouseAt(*canvas, off + x + dx, y + dy, down, true));
    };
    const auto lastSlot = [&] { return recording.calls.back().slot; };

    // The body, dragged along: Position.
    {
        const auto r = rect();
        const float x = 0.5f * (r.x0 + r.x1);
        const float y = 0.5f * (r.y0 + r.y1);
        REQUIRE(gg::ui::hitTestRect(x, y, r, kTol, lane) == gg::ui::DragTarget::Body);
        drag(x, y, -20.0f, 1.0f);
        REQUIRE(recording.kinds() == "bve");
        CHECK(recording.calls[0].slot == layerSlot(LayerParam::Position, 1));
        CHECK(recording.calls[1].slot == layerSlot(LayerParam::Position, 1));
        CHECK(lastSlot() == layerSlot(LayerParam::Position, 1));
        CHECK(recording.calls[1].value > 0.5); // left is further back on the FX lane
    }
    // The body, dragged up: Pitch, and Position stays where the last drag left it.
    {
        recording.calls.clear();
        const auto r = rect();
        const float x = 0.5f * (r.x0 + r.x1);
        const float y = 0.5f * (r.y0 + r.y1);
        drag(x, y, 1.0f, -20.0f);
        REQUIRE(recording.kinds() == "bve");
        CHECK(recording.calls[0].slot == layerSlot(LayerParam::Pitch, 1));
        CHECK(recording.calls[1].value == gg::ui::pitchValueAtY(y - 20.0f, lane));
        CHECK(recording.calls[1].value > 0.0); // higher on the lane is higher in pitch
    }
    // The top edge: Pitch Spread.
    {
        recording.calls.clear();
        const auto r = rect();
        const float x = 0.5f * (r.x0 + r.x1);
        const float y = r.y0 - 1.0f;
        REQUIRE(gg::ui::hitTestRect(x, y, r, kTol, lane) == gg::ui::DragTarget::TopEdge);
        drag(x, y, 0.0f, -10.0f);
        REQUIRE(recording.kinds() == "bve");
        CHECK(recording.calls[0].slot == layerSlot(LayerParam::PitchSpread, 1));
        CHECK(recording.calls[1].value > 12.0); // wider
    }
    // The left edge: Spray.
    {
        recording.calls.clear();
        const auto r = rect();
        const float y = 0.5f * (r.y0 + r.y1);
        REQUIRE(gg::ui::hitTestRect(r.x0, y, r, kTol, lane) == gg::ui::DragTarget::LeftEdge);
        drag(r.x0, y, -10.0f, 0.0f);
        REQUIRE(recording.kinds() == "bve");
        CHECK(recording.calls[0].slot == layerSlot(LayerParam::Spray, 1));
    }
    // Nothing in this test touched another layer's parameters.
    for (const GestureCall &c : recording.calls)
    {
        const auto ref = gg::core::layerOf(c.slot);
        REQUIRE(ref.has_value());
        CHECK(ref->layer == 1);
    }
}

TEST_CASE("a press on the row two lanes share goes to one lane", "[plugin]")
{
    // Lane A's bottom row is lane B's top row. Both rectangles reach it (A at Pitch -24 with a
    // spread of 24, B at Pitch +24 with a spread of 24), so both would grab a press there: the
    // lane below, whose half-open range [top, bottom) holds the row, takes it.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    using gg::core::LayerParam;
    using gg::core::layerSlot;
    RecordingContext recording(gg::core::Mode::Fx);
    const auto set = [&](uint8_t layer, LayerParam p, float v) {
        recording.values[static_cast<std::size_t>(layerSlot(p, layer))] = v;
    };
    set(0, LayerParam::Pitch, -24.0f);
    set(0, LayerParam::PitchSpread, 24.0f);
    set(1, LayerParam::Pitch, 24.0f);
    set(1, LayerParam::PitchSpread, 24.0f);
    recording.context.sampleRate = 48000.0;
    const auto editor = gg::ui::makeEditor(recording.context);
    auto *canvas = canvasOf(*editor);
    REQUIRE(canvas != nullptr);
    const juce::Rectangle<int> a = canvas->laneBounds(0);
    const juce::Rectangle<int> b = canvas->laneBounds(1);
    REQUIRE(a.getBottom() == b.getY()); // the lanes tile the canvas: they share the row
    const auto laneA = laneOf(*canvas, 0, true, 48000.0, 0);
    const auto laneB = laneOf(*canvas, 1, true, 48000.0, 0);
    const auto rectOf = [&](uint8_t layer, const gg::ui::LaneGeometry &g) {
        const auto v = [&](LayerParam p) {
            return recording.values[static_cast<std::size_t>(layerSlot(p, layer))];
        };
        return gg::ui::scanRect({v(LayerParam::Position), v(LayerParam::Spray),
                                 v(LayerParam::Pitch), v(LayerParam::PitchSpread)},
                                g);
    };
    const auto ra = rectOf(0, laneA);
    const auto rb = rectOf(1, laneB);
    const float x = std::max(ra.x0, rb.x0) + 4.0f;
    const auto row = static_cast<float>(b.getY());
    constexpr float kTol = gg::ui::GrainCanvas::kEdgeTolerancePx;
    REQUIRE(gg::ui::hitTestRect(x, row, ra, kTol, laneA) != gg::ui::DragTarget::None);
    REQUIRE(gg::ui::hitTestRect(x, row, rb, kTol, laneB) != gg::ui::DragTarget::None);

    const float off = static_cast<float>(b.getX());
    const juce::Point<float> down{off + x, row};
    canvas->mouseDown(mouseAt(*canvas, off + x, row, down, false));
    canvas->mouseDrag(mouseAt(*canvas, off + x, row - 8.0f, down, true));
    canvas->mouseUp(mouseAt(*canvas, off + x, row - 8.0f, down, true));
    REQUIRE(recording.kinds() == "bve");
    CHECK(recording.calls[0].slot == layerSlot(LayerParam::PitchSpread, 1));
}

TEST_CASE("a drag on a lane without a sample is no gesture", "[plugin]")
{
    // The instrument's layer B reads a Sample and has none (PR 1: only layer A can hold one):
    // nothing in its lane can be grabbed, but the press still selects the lane.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Instrument);
    giveLayerASample(recording.context);
    const auto editor = gg::ui::makeEditor(recording.context);
    auto *canvas = canvasOf(*editor);
    REQUIRE(canvas != nullptr);
    std::vector<int> selected;
    canvas->onSelectLayer = [&selected](uint8_t layer) { selected.push_back(layer); };
    const juce::Rectangle<int> b = canvas->laneBounds(1);
    const float y = static_cast<float>(b.getCentreY());
    for (const float f : {0.1f, 0.5f, 0.9f})
    {
        const float x = static_cast<float>(b.getX()) + f * static_cast<float>(b.getWidth());
        canvas->mouseDown(mouseAt(*canvas, x, y, {x, y}, false));
        canvas->mouseDrag(mouseAt(*canvas, x + 30.0f, y + 1.0f, {x, y}, true));
        canvas->mouseUp(mouseAt(*canvas, x + 30.0f, y + 1.0f, {x, y}, true));
        canvas->mouseDoubleClick(mouseAt(*canvas, x, y, {x, y}, false));
    }
    CHECK(recording.calls.empty());
    CHECK(selected == std::vector<int>{1, 1, 1});
}

TEST_CASE("double-click freezes one lane", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    using gg::core::LayerParam;
    using gg::core::layerSlot;
    RecordingContext recording(gg::core::Mode::Fx);
    recording.context.sampleRate = 48000.0;
    const auto editor = gg::ui::makeEditor(recording.context);
    auto *canvas = canvasOf(*editor);
    REQUIRE(canvas != nullptr);
    const auto lane = laneOf(*canvas, 2, true, 48000.0, 0);
    const float off = static_cast<float>(canvas->laneBounds(2).getX());
    const auto v = [&](LayerParam p) {
        return recording.values[static_cast<std::size_t>(layerSlot(p, 2))];
    };
    const auto r = gg::ui::scanRect({v(LayerParam::Position), v(LayerParam::Spray),
                                     v(LayerParam::Pitch), v(LayerParam::PitchSpread)},
                                    lane);
    const float x = 0.5f * (r.x0 + r.x1);
    const float y = 0.5f * (r.y0 + r.y1);
    REQUIRE(gg::ui::hitTestRect(x, y, r, gg::ui::GrainCanvas::kEdgeTolerancePx, lane) ==
            gg::ui::DragTarget::Body);
    const juce::Point<float> at{off + x, y};
    // JUCE's order for a double-click: down, up, down (click count 2), up, then the double-click
    // (Component::internalMouseUp calls mouseUp first).
    constexpr int kLeft = juce::ModifierKeys::leftButtonModifier;
    const auto doubleClick = [&] {
        canvas->mouseDown(mouseAt(*canvas, off + x, y, at, false));
        canvas->mouseUp(mouseAt(*canvas, off + x, y, at, false));
        canvas->mouseDown(mouseAt(*canvas, off + x, y, at, false, kLeft, 2));
        canvas->mouseUp(mouseAt(*canvas, off + x, y, at, false, kLeft, 2));
        canvas->mouseDoubleClick(mouseAt(*canvas, off + x, y, at, false, kLeft, 2));
    };
    // A body press that never moves is no gesture.
    canvas->mouseDown(mouseAt(*canvas, off + x, y, at, false));
    canvas->mouseUp(mouseAt(*canvas, off + x, y, at, false));
    CHECK(recording.calls.empty());
    doubleClick();
    REQUIRE(recording.kinds() == "bve");
    const ParamSlot freeze = layerSlot(LayerParam::Freeze, 2);
    CHECK(recording.calls[0].slot == freeze);
    CHECK(recording.calls[1].slot == freeze);
    CHECK(recording.calls[1].value == 1.0);
    CHECK(recording.calls[2].slot == freeze);
    // Again: off.
    doubleClick();
    REQUIRE(recording.kinds() == "bvebve");
    CHECK(recording.calls[4].value == 0.0);
    // Not on an edge, nor outside the rectangle.
    const juce::Point<float> edge{off + r.x0, y};
    canvas->mouseDoubleClick(mouseAt(*canvas, off + r.x0, y, edge, false, kLeft, 2));
    canvas->mouseDoubleClick(mouseAt(*canvas, off + 2.0f, y, {off + 2.0f, y}, false, kLeft, 2));
    CHECK(recording.kinds() == "bvebve");
}

TEST_CASE("a drop onto any lane loads its layer", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Instrument);
    giveLayerASample(recording.context);
    std::vector<std::pair<int, std::string>> loads;
    std::vector<std::string> notices;
    recording.context.loadSampleFor = [&loads](uint8_t layer, const std::string &path) {
        loads.emplace_back(layer, path);
        return layer == 0;
    };
    recording.context.notice = [&notices](const std::string &text) { notices.push_back(text); };
    const auto editor = gg::ui::makeEditor(recording.context);
    auto *canvas = canvasOf(*editor);
    REQUIRE(canvas != nullptr);
    // Platform-absolute paths: juce::File on Windows takes "/tmp/x" as relative, so its file
    // name would be the whole string. The files need not exist; the drop only parses the path.
    const juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const auto pathOf = [&dir](const char *name) {
        return dir.getChildFile(name).getFullPathName();
    };
    const juce::StringArray any{pathOf("anything.txt")};
    CHECK(canvas->isInterestedInFileDrag(any));

    const auto dropOn = [&](uint8_t layer, const juce::String &path) {
        const juce::Rectangle<int> b = canvas->laneBounds(layer);
        canvas->filesDropped(juce::StringArray{path}, b.getX() + 20, b.getCentreY());
    };
    dropOn(0, pathOf("a.wav"));
    REQUIRE(loads.size() == 1);
    CHECK(loads[0] == std::pair<int, std::string>{0, pathOf("a.wav").toStdString()});
    CHECK(notices.empty());
    // Every lane takes a sample (VW-P2c); the instrument's lane B is a Sample lane, so no notice.
    dropOn(1, pathOf("b.wav"));
    REQUIRE(loads.size() == 2);
    CHECK(loads[1] == std::pair<int, std::string>{1, pathOf("b.wav").toStdString()});
    CHECK(notices.empty());
    // A file the decoder can't read is refused as the loader strip refuses it, on any lane, and
    // never reaches the loader.
    dropOn(0, pathOf("notes.txt"));
    dropOn(1, pathOf("noextension"));
    CHECK(loads.size() == 2);
    REQUIRE(notices.size() == 2);
    CHECK(notices[0] == "can't load notes.txt: not a WAV, AIFF, FLAC or MP3 file");
    CHECK(notices[1] == "can't load noextension: not a WAV, AIFF, FLAC or MP3 file");
    // A drop left of the lanes is no lane's.
    canvas->filesDropped(juce::StringArray{pathOf("c.wav")}, 10,
                         canvas->laneBounds(1).getCentreY());
    CHECK(loads.size() == 2);
}

TEST_CASE("the stress seam draws four lanes", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    struct EnvGuard
    {
        EnvGuard()
        {
#if defined(_WIN32)
            _putenv_s("GG_STRESS_GRAINS", "512");
#else
            setenv("GG_STRESS_GRAINS", "512", 1);
#endif
        }
        ~EnvGuard()
        {
#if defined(_WIN32)
            _putenv_s("GG_STRESS_GRAINS", "");
#else
            unsetenv("GG_STRESS_GRAINS");
#endif
        }
    };
    RecordingContext recording(gg::core::Mode::Fx);
    {
        const EnvGuard env;
        const auto editor = gg::ui::makeEditor(recording.context);
        auto *canvas = canvasOf(*editor);
        REQUIRE(canvas != nullptr);
        CHECK(canvas->stressMode());
        canvas->renderFrame();
        for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
        {
            INFO("lane " << int(layer));
            const juce::Rectangle<int> b = canvas->laneBounds(layer);
            double energy = 0.0;
            for (int y = b.getY(); y < b.getBottom(); ++y)
            {
                for (int x = 0; x < b.getWidth(); ++x)
                {
                    energy += canvas->accumAt(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                }
            }
            CHECK(energy > 0.0);
        }
    }
    // Without the variable the canvas draws the queue, which an inactive plugin lacks.
    const auto editor = gg::ui::makeEditor(recording.context);
    auto *canvas = canvasOf(*editor);
    REQUIRE(canvas != nullptr);
    CHECK_FALSE(canvas->stressMode());
}

TEST_CASE("the canvas paints four lanes, and again after a resize", "[plugin]")
{
    // Paints into an image so drawLane, the peak strips' cache, the off lane's transparency layer
    // and the Sample lane's hint run.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    constexpr double kRate = 48000.0;
    constexpr int64_t kFrames = 16 * 48000;
    std::array<gg::core::LivePeaks, gg::core::kLayers> peaks;
    std::vector<float> noise(static_cast<std::size_t>(kFrames));
    for (std::size_t i = 0; i < noise.size(); ++i)
    {
        noise[i] = 0.5f * static_cast<float>(std::sin(0.01 * static_cast<double>(i)));
    }
    for (auto &p : peaks)
    {
        p.prepare(kFrames);
        p.write(noise.data(), noise.data(), 0, static_cast<uint32_t>(kFrames));
    }
    gg::core::GrainQueue queue;
    gg::core::SnapshotBuffer snapshots;
    std::atomic<int64_t> clock{kFrames};
    RecordingContext recording(gg::core::Mode::Fx);
    recording.context.grains = &queue;
    recording.context.snapshot = &snapshots;
    recording.context.clock = &clock;
    recording.context.sampleRate = kRate;
    recording.context.livePeaksFor = [&peaks](uint8_t layer) { return &peaks[layer]; };
    // Lane D is a Sample lane with a sample; lane C (index 2) has none and shows its hint.
    int waveformCalls = 0;
    recording.context.sampleWaveformFor = [&waveformCalls](uint8_t layer) {
        ++waveformCalls;
        return layer == 3 ? oneSecondSample() : gg::ui::SampleWaveform{};
    };
    gg::core::UiSnapshot snapshot;
    snapshot.clock = kFrames;
    snapshot.writeHead = kFrames;
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        auto &lane = snapshot.lanes[layer];
        lane.writeHead = kFrames;
        lane.builtKind = layer < 2 ? 1 : 0;
        lane.on = layer != 1; // lane B is off: drawn at 40%
        lane.feedback = layer == 0 ? 0.5f : 0.0f;
        lane.frozen = layer == 0;
        lane.effectivePosition = 0.3f; // a ghost
    }
    snapshots.push(snapshot);
    const auto editor = gg::ui::makeEditor(recording.context);
    auto *canvas = canvasOf(*editor);
    REQUIRE(canvas != nullptr);

    const auto paintInto = [&](int width, int height) {
        canvas->setBounds(0, 0, width, height);
        juce::Image image(juce::Image::ARGB, width, height, true);
        juce::Graphics g(image);
        canvas->paint(g);
        return image;
    };
    const auto litPixels = [&](const juce::Image &image, uint8_t layer) {
        const juce::Rectangle<int> b = canvas->laneBounds(layer);
        const juce::Colour ground = gg::ui::Palette::dark().lane;
        int lit = 0;
        for (int y = b.getY() + 2; y < b.getBottom() - 1; ++y)
        {
            for (int x = b.getX() + 2; x < b.getRight() - 4; ++x)
            {
                lit += image.getPixelAt(x, y) != ground ? 1 : 0;
            }
        }
        return lit;
    };
    juce::Image image = paintInto(900, 403);
    // Carry-over row 327: each lane's outline is fetched once a frame, not at each use.
    waveformCalls = 0;
    image = paintInto(900, 403);
    CHECK(waveformCalls == 2);      // lanes C and D, the two Sample lanes, once each
    CHECK(litPixels(image, 0) > 0); // peaks, rectangle, tint, marker
    CHECK(litPixels(image, 1) > 0); // off, but drawn
    CHECK(litPixels(image, 2) > 0); // the hint
    CHECK(litPixels(image, 3) > 0); // the sample's outline
    CHECK(image.getPixelAt(10, 10) ==
          gg::ui::Palette::dark().background); // the header column (chrome)
    image = paintInto(900, 403);               // the cached strips
    CHECK(litPixels(image, 0) > 0);
    image = paintInto(900, 404); // lane tops move, heights may not
    CHECK(litPixels(image, 2) > 0);
    CHECK(litPixels(image, 3) > 0);
    image = paintInto(700, 500);
    CHECK(litPixels(image, 3) > 0);
    CHECK(canvas->averageFrameMs() > 0.0);

    // A grain on lane D lights the accumulator inside its capsule's rectangle and nowhere else
    // (row 327: positions, not only lit pixels). It reads a quarter of the way into the 1 s
    // sample, 100 ms at rate 2, an octave up. Queued after a frame, which clears the queue.
    image = paintInto(900, 403);
    REQUIRE(queue.push(gg::core::GrainVis{kFrames - 100, 12000.0, 2.0f, 4800.0f, 1.0f, 0.0f, 3}));
    image = paintInto(900, 403);
    const auto lane = laneOf(*canvas, 3, false, kRate, 48000);
    const auto c = gg::ui::grainCapsule({kFrames - 100, 12000.0, 2.0f, 4800.0f, 1.0f, 0.0f},
                                        kFrames, kFrames, lane);
    REQUIRE(c.has_value());
    const juce::Rectangle<int> d = canvas->laneBounds(3);
    int inside = 0;
    int outside = 0;
    for (int y = d.getY(); y < d.getBottom(); ++y)
    {
        for (int x = 0; x < d.getWidth(); ++x)
        {
            if (canvas->accumAt(static_cast<uint32_t>(x), static_cast<uint32_t>(y)) <= 0.0f)
            {
                continue;
            }
            const bool in = static_cast<float>(x) >= c->x0 - c->radius - 1.0f &&
                            static_cast<float>(x) <= c->x1 + c->radius + 1.0f &&
                            std::abs(static_cast<float>(y) - c->y) <= c->radius + 1.0f;
            (in ? inside : outside) += 1;
        }
    }
    CHECK(inside > 0);
    CHECK(outside == 0);
    // And the painted image shows it there: lit pixels inside the capsule's rectangle.
    int litInside = 0;
    for (int y = static_cast<int>(c->y - c->radius); y <= static_cast<int>(c->y + c->radius); ++y)
    {
        for (int x = static_cast<int>(c->x0); x <= static_cast<int>(c->x1); ++x)
        {
            litInside += image.getPixelAt(d.getX() + x, y) != gg::ui::Palette::dark().lane ? 1 : 0;
        }
    }
    CHECK(litInside > 0);
}

TEST_CASE("the lanes' overlay colours are the same in both themes", "[plugin]")
{
    // D9: the lanes stay dark in the light theme, so everything drawn on them is theme-invariant.
    const gg::ui::Palette &dark = gg::ui::Palette::dark();
    const gg::ui::Palette &light = gg::ui::Palette::light();
    CHECK(light.lane == dark.lane);
    CHECK(light.laneRule == dark.laneRule);
    CHECK(light.laneText == dark.laneText);
    CHECK(light.peaks == dark.peaks);
    CHECK(light.window == dark.window);
    CHECK(light.ghost == dark.ghost);
    CHECK(light.freeze == dark.freeze);
    CHECK(light.feedback == dark.feedback);
    CHECK(light.layers == dark.layers);
}

namespace
{
/// An instrument canvas over a sample in a context with an undo stack that records what it applies.
struct UndoCanvas
{
    RecordingContext recording{gg::core::Mode::Instrument};
    std::vector<std::pair<ParamSlot, float>> undone;
    gg::ui::UndoStack undo{{.setValue = [this](ParamSlot s, float v) { undone.emplace_back(s, v); },
                            .setRoutes = [](const gg::core::ModRoutes &) {},
                            .setValues = {}}};
    std::unique_ptr<juce::Component> editor;
    gg::ui::GrainCanvas *canvas{nullptr};

    UndoCanvas()
    {
        recording.values[static_cast<std::size_t>(ParamSlot::Position)] = 0.5f;
        recording.values[static_cast<std::size_t>(ParamSlot::Spray)] = 0.4f;
        giveLayerASample(recording.context);
        recording.context.sampleRate = 48000.0;
        recording.context.undo = &undo;
        editor = gg::ui::makeEditor(recording.context);
        canvas = canvasOf(*editor);
    }
};
} // namespace

TEST_CASE("Escape mid canvas drag re-enables undo", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    constexpr float kTol = gg::ui::GrainCanvas::kEdgeTolerancePx;
    UndoCanvas f;
    REQUIRE(f.canvas != nullptr);
    const auto lane = laneOf(*f.canvas, 0, false, 48000.0, 48000);
    const float off = static_cast<float>(f.canvas->laneBounds(0).getX());
    const auto rect = gg::ui::scanRect({0.5f, 0.4f, 0.0f, 0.0f}, lane);
    const float centre = 0.5f * (rect.x0 + rect.x1);
    const float y = rect.y0;
    REQUIRE(gg::ui::hitTestRect(centre, y, rect, kTol, lane) == gg::ui::DragTarget::Body);

    const juce::Point<float> down{off + centre, y};
    f.canvas->mouseDown(mouseAt(*f.canvas, off + centre, y, down, false));
    f.canvas->mouseDrag(mouseAt(*f.canvas, off + centre - 20.0f, y, down, true));
    // Mid drag the stack waits for the gesture: undo does nothing.
    CHECK(f.undo.busy());
    f.undo.undo();
    CHECK(f.undone.empty());

    CHECK(f.canvas->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK_FALSE(f.undo.busy());
    REQUIRE(f.undo.size() == 1);
    f.undo.undo();
    REQUIRE(f.undone.size() == 1);
    CHECK(f.undone[0] == std::pair{ParamSlot::Position, 0.5f});
}

TEST_CASE("held arrow keys make one undo entry on the canvas lane", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    UndoCanvas f;
    REQUIRE(f.canvas != nullptr);
    for (int i = 0; i < 20; ++i)
    {
        REQUIRE(f.canvas->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    }
    CHECK(f.undo.size() == 1);
    CHECK(f.undo.undoLabel() == "A Position");
    // Another parameter's nudge is its own entry; so is a double-click's freeze.
    REQUIRE(f.canvas->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    CHECK(f.undo.size() == 2);
    f.undo.undo();
    f.undo.undo();
    REQUIRE(f.undone.size() == 2);
    CHECK(f.undone[0].first == ParamSlot::Pitch);
    CHECK(f.undone[1] == std::pair{ParamSlot::Position, 0.5f});
}

// The Live lanes (M2 sub-project 6's spec §6.3, PR 2): the ring's span, marks, loop and menu, and
// drops on every lane, over the real plugin.
namespace
{
constexpr const char *kFxPlugin = "org.sethbc.giga-grains-fx";
constexpr const char *kInstrumentPlugin = "org.sethbc.giga-grains";
constexpr uint32_t kBlock = 512;
constexpr double kPluginRate = 48000.0;

/// A plugin with its editor, whose canvas never shows a menu or a chooser: the menus it would
/// show are kept in `shown`.
struct PluginCanvas
{
    explicit PluginCanvas(const char *id) : plugin(id)
    {
        REQUIRE(plugin.valid());
        impl = dynamic_cast<gg::plugin::EditorPlugin *>(&plugin.impl());
        REQUIRE(impl != nullptr);
        editor = impl->createEditor();
        REQUIRE(editor != nullptr);
        canvas = canvasOf(*editor);
        REQUIRE(canvas != nullptr);
        canvas->showMenu = [this](juce::PopupMenu menu) { shown.push_back(std::move(menu)); };
        canvas->browse = [](const gg::ui::GrainCanvas::Browse &,
                            std::function<void(const juce::File &)>) {};
    }
    const gg::ui::EditorContext &context() const { return impl->editorContext(); }
    bool fx() const { return impl->mode() == gg::core::Mode::Fx; }

    /// `frames` of noise through process(), kBlock at a time, `events` in the first call.
    void process(uint32_t frames, const clap_input_events *events = nullptr)
    {
        for (uint32_t done = 0; done < frames; done += kBlock)
        {
            const uint32_t n = std::min(kBlock, frames - done);
            gg::test::Stereo in(n);
            gg::test::Stereo out(n);
            gg::test::fillNoise(in, 1 + done);
            REQUIRE(plugin.process(n, fx() ? in.data() : nullptr, out.data(),
                                   done == 0 ? events : nullptr) == CLAP_PROCESS_CONTINUE);
        }
    }
    /// One call carrying a parameter value.
    void processWith(ParamSlot slot, double value)
    {
        const auto event = gg::test::paramValue(gg::core::paramId(slot), value);
        gg::test::InputEvents events;
        events.add(&event.header);
        process(kBlock, events.list());
    }
    /// A parameter value while inactive, as a host's flush.
    void flush(ParamSlot slot, double value)
    {
        const auto event = gg::test::paramValue(gg::core::paramId(slot), value);
        gg::test::InputEvents events;
        events.add(&event.header);
        plugin.flush(events.list());
    }
    juce::Image paint()
    {
        juce::Image image(juce::Image::ARGB, canvas->getWidth(), canvas->getHeight(), true);
        juce::Graphics g(image);
        canvas->paint(g);
        return image;
    }
    /// Polls layer `layer`'s status until it is `state`.
    bool waitFor(uint8_t layer, gg::ui::SampleStatusView::State state)
    {
        for (int i = 0; i < 1000; ++i)
        {
            if (context().sampleStatusFor(layer).state == state)
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }
    juce::String notice() const { return labelText(*editor, "header.notice"); }
    bool noticeShown() const { return findDeep(*editor, "header.notice")->isVisible(); }
    /// A drop of `path` on lane `layer`.
    void dropOn(uint8_t layer, const std::filesystem::path &path)
    {
        const juce::Rectangle<int> b = canvas->laneBounds(layer);
        canvas->filesDropped(juce::StringArray{utf8String(path)}, b.getX() + 20, b.getCentreY());
    }

    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::test::PluginUnderTest plugin;
    gg::plugin::EditorPlugin *impl{nullptr};
    std::unique_ptr<juce::Component> editor;
    gg::ui::GrainCanvas *canvas{nullptr};
    std::vector<juce::PopupMenu> shown;
};

juce::String ellipsis(const char *text)
{
    return juce::String(text) + juce::String::charToString(0x2026);
}

/// The item of `menu`, or of a submenu of it, whose text is `text`; fails the test if none is.
juce::PopupMenu::Item itemOf(const juce::PopupMenu &menu, const juce::String &text)
{
    for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();)
    {
        if (it.getItem().text == text)
        {
            return it.getItem();
        }
    }
    FAIL("no menu item " << text.toStdString());
    return {};
}

/// Invokes `menu`'s item `text`, as choosing it does.
void choose(const juce::PopupMenu &menu, const juce::String &text)
{
    const juce::PopupMenu::Item item = itemOf(menu, text);
    REQUIRE(item.isEnabled);
    REQUIRE(item.action);
    item.action();
}

ParamSlot slotOf(gg::core::LayerParam p, uint8_t layer) { return gg::core::layerSlot(p, layer); }
} // namespace

TEST_CASE("a Live lane spans its ring", "[plugin]")
{
    gg::test::useMediaScratch("canvas-ring-span");
    PluginCanvas f(kFxPlugin);
    // Inactive: no ring, so the 16 s fallback.
    CHECK(f.canvas->laneGeometry(0).ringSeconds == 16.0);
    // Ring Length 60 s, sized at activation.
    f.flush(slotOf(gg::core::LayerParam::RingLength, 0), std::log2(60.0));
    REQUIRE(f.plugin.activate(kPluginRate, kBlock));
    f.process(kBlock);
    f.paint();
    CHECK_THAT(f.canvas->laneGeometry(0).ringSeconds, Catch::Matchers::WithinAbs(60.0, 0.1));
    CHECK_THAT(f.canvas->laneGeometry(1).ringSeconds, Catch::Matchers::WithinAbs(16.0, 0.1));
    // A resize while active: once the plugin has adopted the new ring, the lane follows it.
    f.processWith(slotOf(gg::core::LayerParam::RingLength, 1), std::log2(30.0));
    f.plugin.onMainThread(); // the watcher offers the ring
    f.process(kBlock);       // adopted
    f.plugin.onMainThread();
    REQUIRE(f.impl->processor()->ringRead(1).storage->frames() == 30 * 48000);
    f.paint();
    CHECK_THAT(f.canvas->laneGeometry(1).ringSeconds, Catch::Matchers::WithinAbs(30.0, 0.1));
    // Deactivated: 16 s again.
    f.plugin.deactivate();
    f.paint();
    CHECK(f.canvas->laneGeometry(0).ringSeconds == 16.0);
}

TEST_CASE("the lane draws the ring's marks", "[plugin]")
{
    gg::test::useMediaScratch("canvas-ring-marks");
    PluginCanvas f(kFxPlugin);
    REQUIRE(f.plugin.activate(kPluginRate, kBlock));
    f.process(2 * 48000);
    f.context().ringCommand(0, gg::ui::RingCommand::AddSplice, 1.0f);
    f.context().ringCommand(0, gg::ui::RingCommand::AddSplice, 0.5f);
    f.process(kBlock);
    const gg::ui::RingView ring = f.context().ringView(0);
    REQUIRE(ring.spliceCount == 2);
    f.paint();
    const std::vector<float> marks = f.canvas->marksDrawn(0);
    REQUIRE(marks.size() == 2);
    const gg::ui::LaneGeometry lane = f.canvas->laneGeometry(0);
    // Against the head the canvas drew with (the snapshot's, run on by the clock's extrapolation),
    // which is never behind the published one.
    const int64_t head = f.canvas->headDrawn(0);
    CHECK(head >= ring.head);
    for (std::size_t k = 0; k < 2; ++k)
    {
        INFO("mark " << k);
        const auto x = gg::ui::ringFrameX(lane, head, ring.splices[k], kPluginRate);
        REQUIRE(x.has_value());
        CHECK_THAT(marks[k], Catch::Matchers::WithinAbs(*x, 1.0));
    }
    // Lane B has no marks, nor does a lane once its marks are cleared.
    CHECK(f.canvas->marksDrawn(1).empty());
    f.context().ringCommand(0, gg::ui::RingCommand::ClearSplices, 0.0f);
    f.process(kBlock);
    f.paint();
    CHECK(f.canvas->marksDrawn(0).empty());
}

TEST_CASE("a right-click adds a splice mark where clicked", "[plugin]")
{
    gg::test::useMediaScratch("canvas-right-click");
    PluginCanvas f(kFxPlugin);
    REQUIRE(f.plugin.activate(kPluginRate, kBlock));
    f.process(10 * 48000); // the lane's middle, 8 s back, is in the ring
    f.paint();
    const gg::ui::LaneGeometry lane = f.canvas->laneGeometry(0);
    const juce::Rectangle<int> a = f.canvas->laneBounds(0);
    const float x = 0.5f * lane.width;
    const float off = static_cast<float>(a.getX());
    const float y = static_cast<float>(a.getCentreY());
    const int64_t head = f.context().ringView(0).head;
    constexpr int kRight = juce::ModifierKeys::rightButtonModifier;
    f.canvas->mouseDown(mouseAt(*f.canvas, off + x, y, {off + x, y}, false, kRight));
    f.canvas->mouseUp(mouseAt(*f.canvas, off + x, y, {off + x, y}, false, kRight));
    CHECK_FALSE(f.canvas->endDragIfAny()); // no drag began
    REQUIRE(f.shown.size() == 1);
    choose(f.shown.back(), "Add splice mark here");
    f.process(kBlock);
    const gg::ui::RingView ring = f.context().ringView(0);
    REQUIRE(ring.spliceCount == 1);
    const double column = lane.ringSeconds * kPluginRate / lane.width;
    const int64_t expected = gg::ui::ringFrameAt(lane, head, x, kPluginRate);
    CHECK(std::abs(static_cast<double>(ring.splices[0] - expected)) <= column);

    // A right-click on lane B selects it, and Shift+F10 opens the keyboard lane's menu, whose
    // mark goes at the anchor (the head as the command applies).
    const juce::Rectangle<int> b = f.canvas->laneBounds(1);
    const juce::Point<float> at{off + x, static_cast<float>(b.getCentreY())};
    f.canvas->mouseDown(mouseAt(*f.canvas, at.x, at.y, at, false, kRight));
    CHECK(f.canvas->selectedLayer() == 1);
    REQUIRE(f.shown.size() == 2);
    CHECK(f.canvas->keyPressed(
        juce::KeyPress(juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier, 0)));
    REQUIRE(f.shown.size() == 3);
    const int64_t headB = f.context().ringView(1).head;
    choose(f.shown.back(), "Add splice mark here");
    f.process(kBlock);
    const gg::ui::RingView ringB = f.context().ringView(1);
    REQUIRE(ringB.spliceCount == 1);
    CHECK(ringB.splices[0] == headB);
}

TEST_CASE("the lane menu's ring items are disabled while inactive", "[plugin]")
{
    // Review Focus 1: no ring item acts on a ring that isn't there.
    gg::test::useMediaScratch("canvas-menu-inactive");
    PluginCanvas f(kFxPlugin);
    const auto enabled = [&f](const char *text) {
        return itemOf(f.canvas->laneMenu(0), text).isEnabled;
    };
    const juce::String save = ellipsis("Save capture as");
    CHECK_FALSE(enabled("Add splice mark here"));
    CHECK_FALSE(enabled("Clear splice marks"));
    CHECK_FALSE(enabled("Capture to layer"));
    CHECK_FALSE(itemOf(f.canvas->laneMenu(0), save).isEnabled);
    CHECK(enabled("Loop")); // a parameter, set inactive as any other
    REQUIRE(f.plugin.activate(kPluginRate, kBlock));
    f.process(kBlock);
    CHECK(enabled("Add splice mark here"));
    CHECK_FALSE(enabled("Clear splice marks")); // no marks yet
    CHECK(enabled("Capture to layer"));
    CHECK(itemOf(f.canvas->laneMenu(0), save).isEnabled);
    f.context().ringCommand(0, gg::ui::RingCommand::AddSplice, 0.0f);
    f.process(kBlock);
    CHECK(enabled("Clear splice marks"));
    f.plugin.deactivate();
    CHECK_FALSE(enabled("Add splice mark here"));
    CHECK_FALSE(enabled("Clear splice marks"));
    CHECK_FALSE(enabled("Capture to layer"));
    CHECK_FALSE(itemOf(f.canvas->laneMenu(0), save).isEnabled);

    // A Sample lane's menu loads a sample, through the chooser, into its own layer.
    PluginCanvas inst(kInstrumentPlugin);
    const juce::PopupMenu menu = inst.canvas->laneMenu(2);
    CHECK(menu.getNumItems() == 1);
    juce::File chosen;
    gg::ui::GrainCanvas::Browse asked;
    const auto file =
        gg::test::writeSineWav(gg::test::sampleDir("canvas-menu-inactive") / "c.wav", 48000, 0.5);
    inst.canvas->browse = [&](const gg::ui::GrainCanvas::Browse &b,
                              std::function<void(const juce::File &)> done) {
        asked = b;
        done(juce::File(utf8String(file)));
    };
    choose(menu, ellipsis("Load sample"));
    CHECK_FALSE(asked.save);
    CHECK(asked.patterns.contains("*.wav"));
    CHECK(inst.waitFor(2, gg::ui::SampleStatusView::State::Loaded));
    CHECK(inst.context().sampleStatusFor(2).fileName == "c.wav");
}

TEST_CASE("the Loop item is one undo entry", "[plugin]")
{
    gg::test::useMediaScratch("canvas-loop-undo");
    PluginCanvas f(kFxPlugin);
    REQUIRE(f.plugin.activate(kPluginRate, kBlock));
    f.process(kBlock);
    gg::ui::UndoStack *undo = f.context().undo;
    REQUIRE(undo != nullptr);
    const ParamSlot loop = slotOf(gg::core::LayerParam::Loop, 0);
    CHECK_FALSE(itemOf(f.canvas->laneMenu(0), "Loop").isTicked);
    choose(f.canvas->laneMenu(0), "Loop");
    f.process(kBlock);
    CHECK(f.context().baseValue(loop) == 1.0f);
    CHECK(undo->size() == 1);
    CHECK(undo->undoLabel() == "A Loop");
    CHECK(itemOf(f.canvas->laneMenu(0), "Loop").isTicked);
    undo->undo();
    f.process(kBlock);
    f.plugin.onMainThread();
    f.process(kBlock);
    CHECK(f.context().baseValue(loop) == 0.0f);
    CHECK_FALSE(itemOf(f.canvas->laneMenu(0), "Loop").isTicked);
}

TEST_CASE("capture to layer B loads B", "[plugin]")
{
    const auto dir = gg::test::useMediaScratch("canvas-capture");
    PluginCanvas f(kFxPlugin);
    REQUIRE(f.plugin.activate(kPluginRate, kBlock));
    f.process(2 * 48000);
    CHECK(f.context().sampleStatusFor(1).state == gg::ui::SampleStatusView::State::None);
    choose(f.canvas->laneMenu(0), "B");
    CHECK(f.waitFor(1, gg::ui::SampleStatusView::State::Loaded));
    CHECK(f.context().sampleWaveformFor(1).frames > 0);
    CHECK_FALSE(f.noticeShown());

    // Save capture as…: the chooser opens in the captures folder, and a name without an
    // extension is saved as a WAV.
    gg::ui::GrainCanvas::Browse asked;
    const juce::File take = juce::File(utf8String(dir / "captures")).getChildFile("take");
    take.withFileExtension("wav").deleteFile();
    f.canvas->browse = [&](const gg::ui::GrainCanvas::Browse &b,
                           std::function<void(const juce::File &)> done) {
        asked = b;
        done(take);
    };
    choose(f.canvas->laneMenu(0), ellipsis("Save capture as"));
    CHECK(asked.save);
    CHECK(asked.initial.getFullPathName() ==
          juce::String::fromUTF8(f.context().capturesFolder().c_str()));
    CHECK(take.withFileExtension("wav").existsAsFile());

    // A refusal is posted: four 16 s rings at 192 kHz are past a 64 MiB cap.
    f.plugin.deactivate();
    REQUIRE(f.plugin.activate(192000.0, kBlock));
    f.process(kBlock);
    f.context().setMemoryCapMiB(64);
    choose(f.canvas->laneMenu(0), "C");
    CHECK(f.noticeShown());
    CHECK(f.notice().startsWith("over the memory cap"));
}

TEST_CASE("a Ring Length change mid-draw draws no stale mark", "[plugin]")
{
    // Review Focus 2: marks published for one ring length, drawn over the next, never land off
    // the lane; process() takes the resizes without allocating.
    using gg::test::AllocTripwire;
    gg::test::useMediaScratch("canvas-ring-stale");
    PluginCanvas f(kFxPlugin);
    REQUIRE(f.plugin.activate(kPluginRate, kBlock));
    f.process(14 * 48000);
    for (const float seconds : {12.0f, 6.0f, 1.0f})
    {
        f.context().ringCommand(0, gg::ui::RingCommand::AddSplice, seconds);
    }
    f.process(kBlock);
    REQUIRE(f.context().ringView(0).spliceCount == 3);
    const auto onLane = [&f] {
        f.paint();
        const float width = f.canvas->laneGeometry(0).width;
        for (const float x : f.canvas->marksDrawn(0))
        {
            INFO("mark at " << x << " on a lane " << width << " px wide");
            CHECK(x >= 0.0f);
            CHECK(x <= width);
        }
        return f.canvas->marksDrawn(0).size();
    };
    CHECK(onLane() == 3);
    const ParamSlot ringLength = slotOf(gg::core::LayerParam::RingLength, 0);
    std::vector<float> in(kBlock, 0.25f), out0(kBlock), out1(kBlock);
    float *inputs[2] = {in.data(), in.data()};
    float *outputs[2] = {out0.data(), out1.data()};
    // Grow to 60 s, then shrink to 4 s, drawing between every step.
    for (const double seconds : {60.0, 4.0})
    {
        INFO("Ring Length " << seconds << " s");
        f.processWith(ringLength, std::log2(seconds));
        onLane();
        bool adopted = false;
        for (int i = 0; i < 200 && !adopted; ++i)
        {
            f.plugin.onMainThread(); // the watcher offers the ring
            onLane();
            AllocTripwire::resetCount();
            clap_process_status status = CLAP_PROCESS_ERROR;
            {
                AllocTripwire::Scope armed;
                status = f.plugin.process(kBlock, inputs, outputs);
            }
            REQUIRE_NO_ALLOCATIONS(AllocTripwire::count());
            REQUIRE(status == CLAP_PROCESS_CONTINUE);
            adopted = f.impl->processor()->ringRead(0).storage->frames() ==
                      static_cast<int64_t>(seconds * kPluginRate);
            onLane();
        }
        CHECK(adopted);
        f.plugin.onMainThread();
    }
    // The 4 s ring keeps the mark 1 s back; the older ones have left it.
    CHECK(onLane() == 1);

    // A view caught between the two (marks of the 16 s ring, the 4 s ring's length, and a mark
    // past a head the lane hasn't caught up with) draws only the mark on the lane.
    RecordingContext recording(gg::core::Mode::Fx);
    recording.context.sampleRate = kPluginRate;
    static constexpr int64_t head = 10 * 48000;
    recording.context.ringView = [](uint8_t) {
        gg::ui::RingView ring;
        ring.valid = true;
        ring.head = head;
        ring.ringFrames = 4 * 48000;
        ring.windowFrames = ring.ringFrames;
        ring.spliceCount = 4;
        ring.splices = {head - 12 * 48000, head - 6 * 48000, head - 48000, head + 100};
        return ring;
    };
    const auto editor = gg::ui::makeEditor(recording.context);
    auto *canvas = canvasOf(*editor);
    REQUIRE(canvas != nullptr);
    juce::Image image(juce::Image::ARGB, canvas->getWidth(), canvas->getHeight(), true);
    juce::Graphics g(image);
    canvas->paint(g);
    const std::vector<float> drawn = canvas->marksDrawn(0);
    REQUIRE(drawn.size() == 1);
    CHECK_THAT(drawn[0], Catch::Matchers::WithinAbs(0.75 * canvas->laneGeometry(0).width, 1.0));
}

TEST_CASE("lane B takes a dropped file in both products", "[plugin]")
{
    const char *id = GENERATE(kInstrumentPlugin, kFxPlugin);
    CAPTURE(id);
    const auto dir = gg::test::useMediaScratch("canvas-drop-b");
    const auto file = gg::test::writeSineWav(dir / "b.wav", 48000, 0.75);
    PluginCanvas f(id);
    f.dropOn(1, file);
    REQUIRE(f.waitFor(1, gg::ui::SampleStatusView::State::Loaded));
    CHECK(f.context().sampleStatusFor(1).fileName == "b.wav");
    CHECK(f.context().sampleWaveformFor(1).frames == 36000);
    CHECK(f.context().sampleStatusFor(0).state == gg::ui::SampleStatusView::State::None);
}

TEST_CASE("a non-sample file on lane B is refused", "[plugin]")
{
    // The canvas's notice reaches the editor's header.
    const auto dir = gg::test::useMediaScratch("canvas-drop-refused");
    PluginCanvas f(kFxPlugin);
    CHECK_FALSE(f.noticeShown());
    f.dropOn(1, dir / "notes.txt");
    CHECK(f.noticeShown());
    CHECK(f.notice() == "can't load notes.txt: not a WAV, AIFF, FLAC or MP3 file");
    CHECK(f.context().sampleStatusFor(1).state == gg::ui::SampleStatusView::State::None);
}

TEST_CASE("a drop on a Live lane says how to hear it", "[plugin]")
{
    // Review Focus 4: the drop loads the sample and leaves the layer's Source alone (VW-P2c), and
    // a second drop during the first load wins.
    const auto dir = gg::test::useMediaScratch("canvas-drop-live");
    const auto first = gg::test::writeSineWav(dir / "long.wav", 48000, 3.0);
    const auto second = gg::test::writeSineWav(dir / "short.wav", 48000, 0.5);
    PluginCanvas f(kFxPlugin);
    const ParamSlot source = slotOf(gg::core::LayerParam::Source, 1);
    const float before = f.context().baseValue(source);
    f.dropOn(1, first);
    CHECK(f.noticeShown());
    CHECK(f.notice() == "B's sample is loading: set B Source to Sample to play it");
    f.dropOn(1, second);
    REQUIRE(f.waitFor(1, gg::ui::SampleStatusView::State::Loaded));
    CHECK(f.context().sampleStatusFor(1).fileName == "short.wav");
    CHECK(f.context().sampleWaveformFor(1).frames == 24000); // the second file's outline
    CHECK(f.context().baseValue(source) == before);          // still Live

    // The instrument's lane B is a Sample lane: its drop needs no word.
    PluginCanvas inst(kInstrumentPlugin);
    inst.dropOn(1, second);
    CHECK_FALSE(inst.noticeShown());
}

#endif
