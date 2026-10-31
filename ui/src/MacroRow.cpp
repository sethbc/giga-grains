// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "MacroRow.h"

#include <algorithm>
#include <functional>
#include <cstddef>
#include <string>
#include <string_view>

#include <sst/jucegui/components/Knob.h>
#include <sst/jucegui/components/ToggleButton.h>
#include <sst/jucegui/data/Continuous.h>
#include <sst/jucegui/data/Discrete.h>

#include "ParamControl.h"
#include "ParamValue.h"

namespace gg::ui
{
namespace
{
using gg::core::ParamSlot;
namespace jcmp = sst::jucegui::components;
namespace jdata = sst::jucegui::data;

/// The macro's name as the row shows it: the parameter's, less "Macro ".
std::string shortName(const std::string &name)
{
    constexpr std::string_view kPrefix = "Macro ";
    return name.starts_with(kPrefix) ? name.substr(kPrefix.size()) : name;
}

/// A macro's value, labelled with its short name.
ParamValue macroValue(const EditorContext &context, ParamSlot slot)
{
    return ParamValue(context, slot, shortName(gg::core::paramMeta(slot, context.mode).name));
}

/// A knob's source: a continuous macro.
struct KnobSource final : jdata::Continuous
{
    KnobSource(const EditorContext &context, ParamSlot slot) : macro(macroValue(context, slot)) {}

    std::string getLabel() const override { return macro.label(); }
    float getValue() const override { return macro.value(); }
    void setValueFromGUI(const float &f) override { macro.set(f); }
    void setValueFromModel(const float &) override {} // the model is the plugin's base value
    float getDefaultValue() const override { return macro.meta().defaultVal; }
    float getMin() const override { return macro.meta().minVal; }
    float getMax() const override { return macro.meta().maxVal; }
    std::string getValueAsStringFor(float f) const override
    {
        return macro.meta().valueToString(f).value_or(std::string());
    }

    ParamValue macro;
};

/// The Freeze toggle's source: a stepped macro, off (0) or on (1).
struct ToggleSource final : jdata::Discrete
{
    ToggleSource(const EditorContext &context, ParamSlot slot) : macro(macroValue(context, slot)) {}

    std::string getLabel() const override { return macro.label(); }
    int getValue() const override { return macro.value() > 0.5f ? 1 : 0; }
    int getDefaultValue() const override { return macro.meta().defaultVal > 0.5f ? 1 : 0; }
    void setValueFromGUI(const int &i) override { macro.set(i != 0 ? 1.0f : 0.0f); }
    void setValueFromModel(const int &) override {}
    int getMin() const override { return 0; }
    int getMax() const override { return 1; }
    std::string getValueAsStringFor(int i) const override { return i != 0 ? "on" : "off"; }

    ParamValue macro;
};

/// A drop target for a Mod view source chip (spec §6.6): with a handler, a chip dropped on the
/// macro calls it with the macro's slot, which refuses it (a macro can't be a target).
struct ChipTarget : juce::DragAndDropTarget
{
    ParamSlot slot{ParamSlot::Count};
    std::function<void(ParamSlot, gg::core::ModSource)> onDrop;

    bool isInterestedInDragSource(const SourceDetails &details) override
    {
        return onDrop && ParamControl::sourceOfDrag(details.description).has_value();
    }
    void itemDropped(const SourceDetails &details) override
    {
        if (const auto source = ParamControl::sourceOfDrag(details.description); source && onDrop)
        {
            onDrop(slot, *source);
        }
    }
};

/// A knob whose arrow keys step it by 1/100 of its range (1/1000 with Shift), where
/// sst-jucegui's step by 1/40 and know no Left or Right.
struct MacroKnob final : jcmp::Knob, ChipTarget
{
    bool keyPressed(const juce::KeyPress &key) override
    {
        return stepByArrowKey(*this, key) || jcmp::Knob::keyPressed(key);
    }
};

/// The Freeze macro's toggle.
struct MacroToggle final : jcmp::ToggleButton, ChipTarget
{
};
} // namespace

struct MacroRow::Impl
{
    // The sources before the controls, so the controls go first.
    std::array<std::unique_ptr<KnobSource>, 8> knobSources;
    std::unique_ptr<ToggleSource> toggleSource;
    std::array<std::unique_ptr<MacroKnob>, 8> knobs; // index 5 (Freeze) unused
    std::unique_ptr<MacroToggle> toggle;
};

namespace
{
constexpr std::size_t kFreezeIndex = 5; // Macro 6: stepped
static_assert(gg::core::kMacroSlots[kFreezeIndex] == gg::core::ParamSlot::Macro6);
} // namespace

MacroRow::MacroRow(const EditorContext &context) : impl_(std::make_unique<Impl>())
{
    setComponentID("macros");
    setTitle("Macros");
    for (std::size_t i = 0; i < gg::core::kMacroSlots.size(); ++i)
    {
        const ParamSlot slot = gg::core::kMacroSlots[i];
        const juce::String id = "macro." + juce::String(static_cast<int>(i) + 1);
        if (i == kFreezeIndex)
        {
            impl_->toggleSource = std::make_unique<ToggleSource>(context, slot);
            auto toggle = std::make_unique<MacroToggle>();
            toggle->slot = slot;
            ToggleSource *source = impl_->toggleSource.get();
            toggle->setSource(source);
            toggle->setLabel(source->getLabel());
            toggle->setDrawMode(jcmp::ToggleButton::DrawMode::LABELED);
            toggle->onBeginEdit = [source] { source->macro.begin(); };
            toggle->onEndEdit = [source] { source->macro.end(); };
            toggle->setComponentID(id);
            addAndMakeVisible(*toggle);
            impl_->toggle = std::move(toggle);
            continue;
        }
        impl_->knobSources[i] = std::make_unique<KnobSource>(context, slot);
        auto knob = std::make_unique<MacroKnob>();
        knob->slot = slot;
        KnobSource *source = impl_->knobSources[i].get();
        knob->setSource(source);
        knob->onBeginEdit = [source] { source->macro.begin(); };
        knob->onEndEdit = [source] { source->macro.end(); };
        knob->setComponentID(id);
        addAndMakeVisible(*knob);
        impl_->knobs[i] = std::move(knob);
    }
}

MacroRow::~MacroRow()
{
    // An edit the controls never ended (the editor closed under the mouse) ends here.
    for (std::size_t i = 0; i < impl_->knobSources.size(); ++i)
    {
        if (impl_->knobSources[i])
        {
            impl_->knobSources[i]->macro.end();
        }
    }
    impl_->toggleSource->macro.end();
    impl_->knobs = {};
    impl_->toggle.reset();
}

void MacroRow::setSourceDropHandler(
    std::function<void(gg::core::ParamSlot, gg::core::ModSource)> handler)
{
    for (const auto &knob : impl_->knobs)
    {
        if (knob)
        {
            knob->onDrop = handler;
        }
    }
    impl_->toggle->onDrop = std::move(handler);
}

void MacroRow::refresh()
{
    for (std::size_t i = 0; i < impl_->knobs.size(); ++i)
    {
        if (impl_->knobSources[i] && impl_->knobSources[i]->macro.refresh())
        {
            impl_->knobs[i]->repaint();
        }
    }
    if (impl_->toggleSource->macro.refresh())
    {
        impl_->toggle->repaint();
    }
}

void MacroRow::resized()
{
    auto area = getLocalBounds();
    const int cell = area.getWidth() / static_cast<int>(gg::core::kMacroSlots.size());
    for (std::size_t i = 0; i < gg::core::kMacroSlots.size(); ++i)
    {
        auto slot = i + 1 == gg::core::kMacroSlots.size() ? area : area.removeFromLeft(cell);
        slot = slot.reduced(6, 2);
        if (i == kFreezeIndex)
        {
            // The large toggle: the cell's width, a little more than half its height, centred.
            const int h = std::min(slot.getHeight(), std::max(28, slot.getHeight() * 3 / 5));
            impl_->toggle->setBounds(slot.withSizeKeepingCentre(slot.getWidth(), h));
            continue;
        }
        // The knob draws a circle as wide as its bounds at their top and its label in what is
        // left below, so its bounds are narrower than they are tall by the label's height.
        constexpr int kLabelHeight = 20;
        const int side = std::max(0, std::min(slot.getWidth(), slot.getHeight() - kLabelHeight));
        impl_->knobs[i]->setBounds(slot.withSizeKeepingCentre(side, slot.getHeight()));
    }
}
} // namespace gg::ui
