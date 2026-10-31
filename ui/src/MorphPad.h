// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_MORPHPAD_H
#define GG_UI_MORPHPAD_H

#include <memory>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/ui/Editor.h"
#include "gg/ui/PadModel.h"

namespace gg::ui
{
/// The XY morph pad (M2 sub-project 6's spec §6.3), component ID "pad": Morph X across, Morph Y
/// up, in the largest square the component holds, centred. The four quadrants are shaded in their
/// layers' colours by morphWeights at the pad's position (with the layers' On switches), and the
/// corners carry the layers' letters, A bottom-left, B bottom-right, C top-left, D top-right.
///
/// **Edits.** A drag is one gesture on both parameters: beginGesture(MorphX), beginGesture
/// (MorphY), the pair of values at each move, endGesture of both, and one undo entry ("Morph")
/// when the editor has an undo stack. A double-click returns the pad to the centre, (0.5, 0.5),
/// and the arrow keys move it by 1/100 (1/1000 with Shift), each press a gesture of its own.
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class MorphPad final : public juce::Component
{
  public:
    explicit MorphPad(const EditorContext &context);
    /// Ends a gesture the pad never ended (the editor closed under the mouse).
    ~MorphPad() override;

    /// Shows the base values as they are now.
    void refresh();

    /// Where the effective values have the pad (a snapshot's), drawn as a ghost; nothing for none.
    void setGhost(std::optional<PadPoint> ghost);

    /// The values the pad shows: those it sent while an edit holds them, else the base values.
    PadPoint value() const noexcept;

    void paint(juce::Graphics &g) override;
    void mouseDown(const juce::MouseEvent &e) override;
    void mouseDrag(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;
    void mouseDoubleClick(const juce::MouseEvent &e) override;
    bool keyPressed(const juce::KeyPress &key) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gg::ui

#endif // GG_UI_MORPHPAD_H
