// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The editor's canvas: its lanes, drags, drops and stress seam.

#include <catch2/catch_test_macros.hpp>

#include "PluginHarness.h"
#include "EditorTestUtil.h"

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
    // 4), and from 8 s back to 7.95 s back (rate 1/2, wide enough that the clock's extrapolation
    // between frames can't scroll it off x8); x4 and x8 are the reads' middles.
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
    const auto doubleClick = [&] {
        canvas->mouseDown(mouseAt(*canvas, off + x, y, at, false));
        canvas->mouseUp(mouseAt(*canvas, off + x, y, at, false));
        canvas->mouseDown(mouseAt(*canvas, off + x, y, at, false));
        canvas->mouseUp(mouseAt(*canvas, off + x, y, at, false));
        canvas->mouseDoubleClick(mouseAt(*canvas, off + x, y, at, false));
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
    canvas->mouseDoubleClick(mouseAt(*canvas, off + r.x0, y, edge, false));
    canvas->mouseDoubleClick(mouseAt(*canvas, off + 2.0f, y, {off + 2.0f, y}, false));
    CHECK(recording.kinds() == "bvebve");
}

TEST_CASE("a drop onto lane A loads, onto lane B is refused", "[plugin]")
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
    dropOn(1, pathOf("b.wav"));
    REQUIRE(loads.size() == 2);
    CHECK(loads[1] == std::pair<int, std::string>{1, pathOf("b.wav").toStdString()});
    REQUIRE(notices.size() == 1);
    CHECK(notices[0] == "layer B takes a sample after the session format's next version");
    // A file the decoder can't read is refused as the loader strip refuses it, on any lane, and
    // never reaches the loader.
    dropOn(0, pathOf("notes.txt"));
    dropOn(1, pathOf("noextension"));
    CHECK(loads.size() == 2);
    REQUIRE(notices.size() == 3);
    CHECK(notices[1] == "can't load notes.txt: not a WAV, AIFF, FLAC or MP3 file");
    CHECK(notices[2] == "can't load noextension: not a WAV, AIFF, FLAC or MP3 file");
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
    recording.context.sampleWaveformFor = [](uint8_t layer) {
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
                            .setRoutes = [](const gg::core::ModRoutes &) {}}};
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

#endif
