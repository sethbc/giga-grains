// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_VIEWSTACK_H
#define GG_UI_VIEWSTACK_H

#include <array>
#include <cstddef>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/ui/Editor.h"

namespace gg::ui
{
/// The editor's four views, one shown at a time (M2 sub-project 6's spec §6.2; component ID
/// "views"): each fills the stack, and switching is a show and a hide, so a hidden view keeps
/// what it holds (the canvas its grains). The views are the owner's; the stack only places them.
/// Perform is shown at first. Message thread only.
class ViewStack final : public juce::Component
{
  public:
    ViewStack(juce::Component &perform, juce::Component &edit, juce::Component &mod,
              juce::Component &fx)
        : views_{&perform, &edit, &mod, &fx}
    {
        setComponentID("views");
        for (juce::Component *view : views_)
        {
            addChildComponent(*view);
        }
        show(View::Perform);
    }

    void show(View view)
    {
        shown_ = view;
        for (std::size_t i = 0; i < views_.size(); ++i)
        {
            views_[i]->setVisible(i == static_cast<std::size_t>(view));
        }
    }

    View shown() const noexcept { return shown_; }

    juce::Component &view(View view) const noexcept
    {
        return *views_[static_cast<std::size_t>(view)];
    }

    void resized() override
    {
        for (juce::Component *view : views_)
        {
            view->setBounds(getLocalBounds());
        }
    }

  private:
    std::array<juce::Component *, 4> views_;
    View shown_{View::Perform};
};
} // namespace gg::ui

#endif // GG_UI_VIEWSTACK_H
