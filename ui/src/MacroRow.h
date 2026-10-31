// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_MACROROW_H
#define GG_UI_MACROROW_H

#include <array>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/ui/Editor.h"

namespace gg::ui
{
/// The Perform view's eight macros (sub-project 5's spec §6.1), in remote-control order:
/// sst-jucegui knobs, and a large toggle for Freeze (Macro 6, stepped). Their component IDs are
/// "macro.1" .. "macro.8", and each is labelled with its parameter's name less "Macro ".
///
/// Each control's data source is an adaptor over the context's base value: an edit (a drag, a
/// click, the wheel or a key) is a gesture, beginGesture, setValue for each change and
/// endGesture, and the control shows the value it sent until the base value catches up
/// (HeldValue). refresh(), which the editor's 30 Hz timer calls, follows the base values the host
/// or the audio thread changed. Each control takes keyboard focus: Up and Right raise it by
/// 1/100 of its range, Down and Left lower it, by 1/1000 with Shift.
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class MacroRow final : public juce::Component
{
  public:
    explicit MacroRow(const EditorContext &context);
    ~MacroRow() override;

    /// Shows the base values as they are now.
    void refresh();

    /// Makes the macros drop targets for the Mod view's source chips (spec §6.6): a chip dropped
    /// on one calls `handler` with its slot, which refuses it with a notice. Null (the default)
    /// takes no drops.
    void
    setSourceDropHandler(std::function<void(gg::core::ParamSlot, gg::core::ModSource)> handler);

    void resized() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gg::ui

#endif // GG_UI_MACROROW_H
