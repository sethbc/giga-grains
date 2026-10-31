// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_SOURCESTRIP_H
#define GG_UI_SOURCESTRIP_H

#include <array>
#include <cstdint>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/core/ModMatrix.h"

namespace gg::ui
{
/// One mod source's chip (M2 sub-project 6's spec §6.6; component ID "source.<ModSource index>"),
/// coloured by its family (the palette's sourceFamilies). A drag from it carries the description
/// "modsource:<ModSource index>" for a ParamControl to take.
class SourceChip final : public juce::TextButton
{
  public:
    explicit SourceChip(gg::core::ModSource source);

    gg::core::ModSource source() const noexcept { return source_; }
    /// "modsource:<index>".
    juce::String description() const;
    /// A per-layer source shows the selected layer's letter ("LFO 1 B").
    void setLayer(uint8_t layer);

    void paintButton(juce::Graphics &g, bool highlighted, bool down) override;
    void mouseDrag(const juce::MouseEvent &e) override;

  private:
    const gg::core::ModSource source_;
    juce::String baseText_;
};

/// The 24 chips in five groups (macros · LFO 1 to Step · Wheel to Key · Follower · Beat to
/// Playing), wrapped in the strip's width (component ID "sources").
class SourceStrip final : public juce::Component
{
  public:
    SourceStrip();
    ~SourceStrip() override;

    /// The layer whose letter the per-layer chips show.
    void setLayer(uint8_t layer);
    /// The height that holds the chips in `width`.
    int heightFor(int width);

    void resized() override;

  private:
    int layout(int width, bool place);

    std::array<std::unique_ptr<SourceChip>,
               static_cast<std::size_t>(gg::core::ModSource::Count) - 1>
        chips_;
};
} // namespace gg::ui

#endif // GG_UI_SOURCESTRIP_H
