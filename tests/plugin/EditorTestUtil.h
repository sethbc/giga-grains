// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#pragma once

// Helpers the editor's GUI tests share (factory_tests.cpp, canvas_tests.cpp, panels_tests.cpp,
// mod_view_tests.cpp). They are inline in gg::test::editor, which each of those files opens with a
// using-directive, so no file holds a copy.

#include <catch2/catch_test_macros.hpp>

#include "PluginHarness.h"
#if GG_BUILD_EDITOR
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <sst/jucegui/components/Knob.h>
#include <sst/jucegui/components/MenuButton.h>
#include <sst/jucegui/components/ToggleButton.h>
#include <sst/jucegui/components/VSlider.h>
#include <sst/jucegui/data/Continuous.h>
#include <sst/jucegui/style/StyleSheet.h>

#include "AboutBox.h"
#include "GrainCanvas.h" // ui/src: the editor's own components
#include "LayerStrip.h"
#include "LoaderStrip.h"
#include "MacroRow.h"
#include "MorphPad.h"
#include "Palette.h"
#include "ParamControl.h"
#include "views/EditView.h"
#include "views/FxView.h"
#include "views/ModView.h"
#include "views/RouteList.h"
#include "views/SourceStrip.h"
#include "editor/EditorPlugin.h"
#include "editor/UiSettings.h"
#include "gg/core/GrainQueue.h"
#include "gg/core/ParamIds.h"
#include "gg/core/UiSnapshot.h"
#include "gg/media/SettingsFile.h"
#include "gg/ui/Editor.h"
#include "gg/ui/LaneModel.h"
#include "gg/ui/UndoStack.h"
#include "gg/ui/Notices.h"
#include "gg/core/Version.h"
#include "support/Utf8.h"

namespace gg::test::editor
{

// The editor's sample loader strip (spec §6.5). These create the editor itself, so they start
// JUCE's GUI as the shim's guiCreate does, but open no window: the strip's widgets are found by
// their component IDs, and a drop is simulated by calling the editor's filesDropped.
/// The component with this ID anywhere in the editor (the strip's widgets are the strip's
/// children), or null.
inline const juce::Component *stripChild(const juce::Component &editor, const char *id)
{
    for (const juce::Component *child : editor.getChildren())
    {
        if (child->getComponentID() == id)
        {
            return child;
        }
        if (const juce::Component *found = stripChild(*child, id))
        {
            return found;
        }
    }
    return nullptr;
}

inline juce::String labelText(const juce::Component &editor, const char *id)
{
    const auto *label = dynamic_cast<const juce::Label *>(stripChild(editor, id));
    return label != nullptr ? label->getText() : juce::String("<no label>");
}

/// Whether the strip shows "Relink…"; fails the test if the strip has no such button.
inline bool relinkShown(const juce::Component &editor)
{
    const auto *relink = stripChild(editor, "sampleRelink");
    REQUIRE(relink != nullptr);
    return relink->isVisible();
}

inline juce::String utf8String(const std::filesystem::path &path)
{
    return juce::String::fromUTF8(gg::test::utf8(path).c_str());
}

// The Perform view (sub-project 5's spec §6): the header, the canvas, the macro row and the
// instrument's loader strip, built headless as above. A context whose callbacks record what the
// editor asks of the plugin stands in for the plugin where the test needs to see the calls.
using gg::core::ParamSlot;

/// What the editor asked of the plugin: 'b'egin, 'v'alue and 'e'nd, in order.
struct GestureCall
{
    char kind;
    ParamSlot slot;
    double value;
};

/// An EditorContext whose base values start at the product's defaults and follow setValue at
/// once (as the plugin's do once the audio thread takes them), and which records every gesture.
struct RecordingContext
{
    gg::ui::EditorContext context;
    std::vector<GestureCall> calls;
    std::array<float, gg::core::kParamCount> values{};

    explicit RecordingContext(gg::core::Mode mode)
    {
        context.productName = mode == gg::core::Mode::Fx ? "Giga Grains FX" : "Giga Grains";
        context.mode = mode;
        for (std::size_t i = 0; i < gg::core::kParamCount; ++i)
        {
            values[i] = gg::core::paramMeta(static_cast<ParamSlot>(i), mode).defaultVal;
        }
        context.baseValue = [this](ParamSlot s) { return values[static_cast<std::size_t>(s)]; };
        context.beginGesture = [this](ParamSlot s) { calls.push_back({'b', s, 0.0}); };
        context.setValue = [this](ParamSlot s, double v) {
            calls.push_back({'v', s, v});
            values[static_cast<std::size_t>(s)] = static_cast<float>(v);
        };
        context.endGesture = [this](ParamSlot s) { calls.push_back({'e', s, 0.0}); };
    }

    /// The kinds of the calls so far, as a string ("bvve").
    std::string kinds() const
    {
        std::string out;
        for (const GestureCall &c : calls)
        {
            out += c.kind;
        }
        return out;
    }
};

/// The component with this ID anywhere below `root`, or null.
inline juce::Component *findDeep(const juce::Component &root, const char *id)
{
    for (juce::Component *child : root.getChildren())
    {
        if (child->getComponentID() == id)
        {
            return child;
        }
        if (juce::Component *found = findDeep(*child, id))
        {
            return found;
        }
    }
    return nullptr;
}

/// A mouse event at (x, y) in `c`'s coordinates, the left button (or `button`) down since `down`.
inline juce::MouseEvent mouseAt(juce::Component &c, float x, float y, juce::Point<float> down,
                                bool dragged, int button = juce::ModifierKeys::leftButtonModifier)
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(
        juce::Desktop::getInstance().getMainMouseSource(), {x, y}, juce::ModifierKeys(button),
        juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
        juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
        juce::MouseInputSource::defaultTiltY, &c, &c, now, down, now, 1, dragged);
}

/// Layer `layer`'s lane geometry as the canvas lays it out before its first paint (one physical
/// pixel per logical one). x runs across the lane, from the canvas's x laneBounds().getX(); y
/// is the canvas's own, since the lane's `top` is its place in the stacked canvas.
inline gg::ui::LaneGeometry laneOf(const gg::ui::GrainCanvas &canvas, uint8_t layer, bool fx,
                                   double sampleRate, int64_t sourceFrames)
{
    const juce::Rectangle<int> b = canvas.laneBounds(layer);
    return {.width = static_cast<float>(b.getWidth()),
            .height = static_cast<float>(b.getHeight()),
            .pixelScale = 1.0f,
            .fxLane = fx,
            .sampleRate = sampleRate,
            .ringSeconds = 16.0,
            .sourceFrames = sourceFrames,
            .top = static_cast<float>(b.getY())};
}

/// The editor's canvas.
inline gg::ui::GrainCanvas *canvasOf(const juce::Component &editor)
{
    return dynamic_cast<gg::ui::GrainCanvas *>(findDeep(editor, "canvas"));
}

/// One second of a 48 kHz instrument sample, for the instrument's lane.
inline gg::ui::SampleWaveform oneSecondSample()
{
    return {std::make_shared<const std::vector<float>>(94, 0.5f), 48000, 1.0};
}

/// Gives layer A the one-second sample (PR 1: only layer A can hold one; the other lanes have
/// none).
inline void giveLayerASample(gg::ui::EditorContext &context)
{
    context.sampleWaveformFor = [](uint8_t layer) {
        return layer == 0 ? oneSecondSample() : gg::ui::SampleWaveform{};
    };
}

/// One sample status, every field given.
inline gg::ui::SampleStatusView statusView(gg::ui::SampleStatusView::State state,
                                           std::string fileName = {}, std::string message = {},
                                           double seconds = 0.0, uint32_t channels = 0,
                                           double fileRate = 0.0, bool relinkable = false)
{
    return {state,    std::move(fileName), std::move(message), seconds, channels,
            fileRate, relinkable};
}

// The parameter control (M2 sub-project 6's spec §6.5): M1's macro adaptor generalized, which
// every view builds its controls from. Built headless, as the Perform view's tests are.
/// The first child of `parent` that is a T, or null.
template <typename T> T *childOfType(const juce::Component &parent)
{
    for (juce::Component *child : parent.getChildren())
    {
        if (auto *t = dynamic_cast<T *>(child))
        {
            return t;
        }
    }
    return nullptr;
}

/// The components below `root` of type T, in tree order.
template <typename T> void collect(juce::Component &root, std::vector<T *> &out)
{
    for (juce::Component *child : root.getChildren())
    {
        if (auto *t = dynamic_cast<T *>(child))
        {
            out.push_back(t);
        }
        collect(*child, out);
    }
}

inline juce::String controlId(ParamSlot slot)
{
    return "param." + juce::String(gg::core::paramId(slot));
}

/// The Edit view's placed slots for `layer`: slotsIn over its fourteen panels (Source to Step,
/// and Ring), less what the product hides.
inline std::vector<ParamSlot> editSlots(uint8_t layer, gg::core::Mode mode)
{
    std::vector<gg::ui::Panel> panels;
    for (uint8_t p = 0; p <= static_cast<uint8_t>(gg::ui::Panel::Step); ++p)
    {
        panels.push_back(static_cast<gg::ui::Panel>(p));
    }
    panels.push_back(gg::ui::Panel::Ring);
    std::vector<ParamSlot> out;
    for (const gg::ui::Panel panel : panels)
    {
        for (const ParamSlot slot : gg::ui::slotsIn(panel, layer))
        {
            if (!gg::ui::hiddenIn(slot, mode))
            {
                out.push_back(slot);
            }
        }
    }
    return out;
}

/// A Mod view's context: the routes kept here, an undo stack that replays into them, and the
/// notices shown.
struct ModHarness
{
    RecordingContext recording;
    gg::core::ModRoutes routes;
    std::vector<std::string> notices;
    gg::ui::UndoStack undo;

    explicit ModHarness(gg::core::Mode mode)
        : recording(mode), routes(gg::core::factoryRoutes(mode)),
          undo({[](ParamSlot, float) {}, [this](const gg::core::ModRoutes &r) { routes = r; }})
    {
        recording.context.routes = [this] { return routes; };
        recording.context.setRoutes = [this](const gg::core::ModRoutes &r) { routes = r; };
        recording.context.undo = &undo;
        recording.context.notice = [this](const std::string &text) { notices.push_back(text); };
    }
};

/// Drops chip `source` (a ModSource index) on `target`.
inline void dropChip(juce::Component *target, int source)
{
    auto *t = dynamic_cast<juce::DragAndDropTarget *>(target);
    REQUIRE(t != nullptr);
    const juce::DragAndDropTarget::SourceDetails details(
        juce::var("modsource:" + juce::String(source)), nullptr, juce::Point<int>(5, 5));
    REQUIRE(t->isInterestedInDragSource(details));
    t->itemDropped(details);
}

} // namespace gg::test::editor
#endif
