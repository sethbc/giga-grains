// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_FXVIEW_H
#define GG_UI_FXVIEW_H

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/ui/Editor.h"

namespace gg::ui
{
/// The FX view (M2 sub-project 6's spec §6.7; component ID "view.fx"): its panels in the signal
/// chain's order (ruling VW-P2e), through the panel grid the Edit view shares (layoutPanelGrid),
/// in a vertical viewport: Delay (Amount, Time, Feedback, Damping, Ping-Pong), Reverb (Amount,
/// Decay, Size, Damping, Pre-delay, Modulation and, in the FX, Dry to Reverb), Limiter (Mode,
/// Ceiling, Release) and Output (Mix, in the FX, then Budget, in both products: ruling VW-T19a;
/// the instrument shows neither Mix nor Dry to Reverb, so its Output panel holds Budget alone).
/// All are global, so the layer doesn't matter. Reverb Size sets on release (D10, row 217): its
/// control shows the value as it is dragged and sends one value as the gesture ends, with a
/// tooltip that says so (ParamPanel).
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class FxView final : public juce::Component
{
  public:
    explicit FxView(const EditorContext &context);
    ~FxView() override;

    /// 30 Hz: the controls follow the base values, and their rings the routes (read once).
    void refresh();

    void resized() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gg::ui

#endif // GG_UI_FXVIEW_H
