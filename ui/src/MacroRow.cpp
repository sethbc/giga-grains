// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "MacroRow.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

#include <sst/basic-blocks/params/ParamMetadata.h>
#include <sst/jucegui/components/Knob.h>
#include <sst/jucegui/components/ToggleButton.h>
#include <sst/jucegui/data/Continuous.h>
#include <sst/jucegui/data/Discrete.h>

#include "HeldValue.h"

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

/// The gesture plumbing both adaptors share: one parameter's base value through the context,
/// with the value sent held until the base value catches up. An edit that arrives outside a
/// begin/end pair (a path of the control that sets a value without one) is made a gesture of
/// its own, so the host never sees a value without one.
class MacroValue
{
  public:
    MacroValue(const EditorContext &context, ParamSlot slot)
        : context_(context), slot_(slot), meta_(gg::core::paramMeta(slot, context.mode)),
          label_(shortName(meta_.name)), value_(base())
    {
    }

    const sst::basic_blocks::params::ParamMetaData &meta() const noexcept { return meta_; }
    const std::string &label() const noexcept { return label_; }
    float value() const noexcept { return value_; }

    void begin()
    {
        if (editing_)
        {
            return;
        }
        editing_ = true;
        held_.begin();
        if (context_.beginGesture)
        {
            context_.beginGesture(slot_);
        }
    }

    void set(float v)
    {
        if (std::isnan(v))
        {
            return; // as uiSetValue ignores one: nothing is stored, held or sent
        }
        v = std::clamp(v, meta_.minVal, meta_.maxVal);
        const bool own = !editing_;
        if (own)
        {
            begin();
        }
        value_ = v;
        held_.set(v);
        if (context_.setValue)
        {
            context_.setValue(slot_, static_cast<double>(v));
        }
        if (own)
        {
            end();
        }
    }

    void end()
    {
        if (!editing_)
        {
            return;
        }
        editing_ = false;
        held_.end(base(), wallSeconds());
        if (context_.endGesture)
        {
            context_.endGesture(slot_);
        }
    }

    /// Follows the base value; true if what the control shows changed.
    bool refresh()
    {
        const float shown = held_.shown(base(), wallSeconds());
        if (shown == value_)
        {
            return false;
        }
        value_ = shown;
        return true;
    }

  private:
    float base() const { return context_.baseValue ? context_.baseValue(slot_) : meta_.defaultVal; }

    const EditorContext &context_;
    const ParamSlot slot_;
    const sst::basic_blocks::params::ParamMetaData &meta_;
    const std::string label_;
    float value_;
    bool editing_{false};
    HeldValue held_;
};

/// A knob's source: a continuous macro.
struct KnobSource final : jdata::Continuous
{
    KnobSource(const EditorContext &context, ParamSlot slot) : macro(context, slot) {}

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

    MacroValue macro;
};

/// The Freeze toggle's source: a stepped macro, off (0) or on (1).
struct ToggleSource final : jdata::Discrete
{
    ToggleSource(const EditorContext &context, ParamSlot slot) : macro(context, slot) {}

    std::string getLabel() const override { return macro.label(); }
    int getValue() const override { return macro.value() > 0.5f ? 1 : 0; }
    int getDefaultValue() const override { return macro.meta().defaultVal > 0.5f ? 1 : 0; }
    void setValueFromGUI(const int &i) override { macro.set(i != 0 ? 1.0f : 0.0f); }
    void setValueFromModel(const int &) override {}
    int getMin() const override { return 0; }
    int getMax() const override { return 1; }
    std::string getValueAsStringFor(int i) const override { return i != 0 ? "on" : "off"; }

    MacroValue macro;
};

/// A knob whose arrow keys step it by 1/100 of its range (1/1000 with Shift), where
/// sst-jucegui's step by 1/40 and know no Left or Right.
struct MacroKnob final : jcmp::Knob
{
    bool keyPressed(const juce::KeyPress &key) override
    {
        const int code = key.getKeyCode();
        const bool up = code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey;
        const bool down = code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey;
        jdata::Continuous *source = continuous();
        if ((!up && !down) || source == nullptr || key.getModifiers().isCommandDown())
        {
            return jcmp::Knob::keyPressed(key);
        }
        const float range = source->getMax() - source->getMin();
        const float step = range * (key.getModifiers().isShiftDown() ? 0.001f : 0.01f);
        const float v = std::clamp(source->getValue() + (up ? step : -step), source->getMin(),
                                   source->getMax());
        onBeginEdit();
        source->setValueFromGUI(v);
        onEndEdit();
        notifyAccessibleChange();
        repaint();
        return true;
    }
};
} // namespace

struct MacroRow::Impl
{
    // The sources before the controls, so the controls go first.
    std::array<std::unique_ptr<KnobSource>, 8> knobSources;
    std::unique_ptr<ToggleSource> toggleSource;
    std::array<std::unique_ptr<MacroKnob>, 8> knobs; // index 5 (Freeze) unused
    std::unique_ptr<jcmp::ToggleButton> toggle;
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
            auto toggle = std::make_unique<jcmp::ToggleButton>();
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
