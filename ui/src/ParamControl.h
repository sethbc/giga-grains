// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_PARAMCONTROL_H
#define GG_UI_PARAMCONTROL_H

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/core/ParamIds.h"
#include "gg/ui/Editor.h"

namespace gg::ui
{
/// One parameter's control (M2 sub-project 6's spec §6.5), which every view builds its controls
/// from: an sst-jucegui knob, slider, toggle or menu button by Kind, over a data source that is
/// the parameter's ParamValue (M1's macro adaptor, generalized). Its component ID is
/// "param.<CLAP ID in decimal>"; its label is the parameter's name, less the layer letter for a
/// layer parameter ("B Size" shows "Size"), and it shows the base value as the host's text.
///
/// Each edit (a drag, a click, the wheel, a key, a menu choice) is a gesture: beginGesture,
/// setValue for each change and endGesture, with the value sent shown until the base value
/// catches up (HeldValue). Up and Right raise a continuous control by 1/100 of its range, Down
/// and Left lower it, by 1/1000 with Shift; a double-click returns it to its default (one
/// gesture). refresh(), which the editor's 30 Hz timer calls, follows the base value the host or
/// the audio thread changed. With commit on release (D10) a gesture sends one value, its last,
/// as it ends.
///
/// **Mod mode** (spec §6.6) leaves the control's own value inert: it takes no mouse or key
/// edit. The rings, one arc per route targeting the parameter, drawn around a knob in slot order
/// (a bipolar one centred on the base value), are draggable then: a vertical drag on a ring calls
/// onRingDrag with its new depth. Nothing changes the value then, the accessibility interface's
/// setValue and setValueAsString included. A knob also draws a spread pair's band (setSpreadBand).
/// Only a Knob draws rings and the band; a toggle or a menu, which have no arc to draw, shows one
/// small dot in the source family's colour for each route targeting it, at its top right.
///
/// **A drop target for a Mod view source chip** (setSourceDropEnabled): a dragged
/// "modsource:<ModSource index>" description is accepted and onSourceDrop gets the source, whatever
/// the control's Kind; whether it can be a target is the owner's to judge.
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class ParamControl final : public juce::Component, public juce::DragAndDropTarget
{
  public:
    enum class Kind
    {
        Knob,
        HSlider,
        VSlider,
        Toggle,
        Menu
    };

    /// The kind for a parameter's metadata: a stepped one with more than two values is a Menu,
    /// with two a Toggle; anything else is a Knob.
    static Kind kindFor(const sst::basic_blocks::params::ParamMetaData &meta) noexcept;

    ParamControl(const EditorContext &context, gg::core::ParamSlot slot, Kind kind);
    /// Ends a gesture the control never ended (the editor closed under the mouse).
    ~ParamControl() override;

    /// Shows the base value as it is now.
    void refresh();

    /// D10: a gesture's values are shown but only its last is sent, as it ends.
    void setCommitOnRelease(bool on);

    /// The hover text, shown over the control itself.
    void setTooltip(const juce::String &text);
    juce::String tooltip() const;
    /// The shown value as the host's text ("∞" at Reverb Decay's top).
    juce::String shownText() const;

    /// The control's own value is inert; its rings are draggable.
    void setModMode(bool on);

    /// A route's ring: its matrix slot (1 to 48), its depth as a fraction of the parameter's
    /// range (negative lowers), whether it swings both ways, and its source family's colour.
    struct Ring
    {
        uint8_t slot;
        float depth01;
        bool bipolar;
        juce::Colour colour;
    };
    /// The rings drawn around the knob, inside out in the order given (slot order).
    void setRings(std::vector<Ring> rings);
    const std::vector<Ring> &rings() const noexcept;
    /// Mod mode: a vertical drag on a ring, with the ring's matrix slot and its new depth.
    std::function<void(uint8_t slot, float depth01)> onRingDrag;
    /// Mod mode: the mouse went up after a ring drag (or any press): the drag's gesture is over.
    std::function<void()> onRingRelease;
    /// A chip is dragged over the control.
    std::function<void()> onDropHover;
    /// A chip was dropped on the control.
    std::function<void(gg::core::ModSource)> onSourceDrop;

    /// Whether a source chip may be dropped on the control: off by default.
    void setSourceDropEnabled(bool on);
    bool isInterestedInDragSource(const SourceDetails &details) override;
    void itemDragEnter(const SourceDetails &details) override;
    void itemDragExit(const SourceDetails &details) override;
    void itemDropped(const SourceDetails &details) override;

    /// The spread pair's faint band on the base knob, from low to high in 0..1 of its range;
    /// nothing for none.
    void setSpreadBand(std::optional<std::pair<float, float>> lowHigh01);

    gg::core::ParamSlot slot() const noexcept;
    /// What the control displays: the value sent while an edit holds it, else the base value.
    float shownValue() const noexcept;

    void resized() override;
    void paintOverChildren(juce::Graphics &g) override;
    void mouseDown(const juce::MouseEvent &e) override;
    void mouseDrag(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gg::ui

#endif // GG_UI_PARAMCONTROL_H
