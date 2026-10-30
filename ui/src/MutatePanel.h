// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_MUTATEPANEL_H
#define GG_UI_MUTATEPANEL_H

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/ui/Editor.h"

namespace gg::ui
{
/// The Mutate panel (M2 sub-project 5's spec §6.9; component ID "preset.mutatePanel"), an overlay
/// the editor lays over the canvas and the macros while it is visible, as the preset browser is:
/// an Amount knob ("mutate.amount": 0 to 100%, 15% at the start), "Mutate" ("mutate.go"), "Reset
/// locks" ("mutate.reset"), "Close" ("mutate.close"), and the lock list below, in a scrolling
/// view: the sections Global, Space, Macros and Layer A to D, each parameter a toggle
/// ("mutate.lock.<slot>", <slot> the parameter's ParamSlot number) that is on while Mutate leaves
/// it alone. Each layer's 16 Step values are one toggle, named for its Step 1 slot, locking and
/// unlocking all 16 together. Each layer's heading has "Lock all" ("mutate.lockall.<letter>")
/// and "Unlock all" ("mutate.unlockall.<letter>").
///
/// The locks are the plugin's (EditorContext::mutateLocked); the toggles follow them at each
/// refresh. Escape closes the panel. The instrument's hidden Mix and Dry to Reverb have no row.
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class MutatePanel final : public juce::Component
{
  public:
    explicit MutatePanel(const EditorContext &context);
    ~MutatePanel() override;

    /// The editor's 30 Hz timer: sends the next batch of a press (context.flushMutate), and while
    /// the panel is visible shows the locks as they are now.
    void refresh();

    void paint(juce::Graphics &g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress &key) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gg::ui

#endif // GG_UI_MUTATEPANEL_H
