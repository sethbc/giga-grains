// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "ParamControl.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <type_traits>

#include <clap/ext/params.h>
#include <sst/basic-blocks/params/ParamMetadata.h>
#include <sst/jucegui/components/HSlider.h>
#include <sst/jucegui/components/Knob.h>
#include <sst/jucegui/components/MenuButton.h>
#include <sst/jucegui/components/ToggleButton.h>
#include <sst/jucegui/components/VSlider.h>
#include <sst/jucegui/data/Continuous.h>
#include <sst/jucegui/data/Discrete.h>

#include "Palette.h"
#include "ParamValue.h"
#include "gg/ui/LayerColours.h"

namespace gg::ui
{
namespace
{
using gg::core::ParamSlot;
using sst::basic_blocks::params::ParamMetaData;
namespace jcmp = sst::jucegui::components;
namespace jdata = sst::jucegui::data;

/// The control's label: the parameter's name, less "B " for a layer parameter.
std::string labelFor(ParamSlot slot, gg::core::Mode mode)
{
    const std::string &name = gg::core::paramMeta(slot, mode).name;
    if (const auto ref = gg::core::layerOf(slot))
    {
        const std::string prefix{kLayerLetters[ref->layer], ' '};
        if (name.starts_with(prefix))
        {
            return name.substr(prefix.size());
        }
    }
    return name;
}

/// A continuous control's source: the parameter's value.
struct ContinuousSource final : jdata::Continuous
{
    ContinuousSource(ParamValue &v, const bool &inertFlag) : value(v), inert(inertFlag) {}

    std::string getLabel() const override { return value.label(); }
    float getValue() const override { return value.value(); }
    void setValueFromGUI(const float &f) override
    {
        if (!inert)
        {
            value.set(f);
        }
    }
    void setValueFromModel(const float &) override {} // the model is the plugin's base value
    float getDefaultValue() const override { return value.meta().defaultVal; }
    float getMin() const override { return value.meta().minVal; }
    float getMax() const override { return value.meta().maxVal; }
    std::string getValueAsStringFor(float f) const override
    {
        return value.meta().valueToString(f).value_or(std::string());
    }

    ParamValue &value;
    const bool &inert; ///< mod mode: every path that sets the value, the accessibility one too
};

/// A stepped control's source: the parameter's value as its step, an on/off one's by > 0.5.
struct DiscreteSource final : jdata::Discrete
{
    DiscreteSource(ParamValue &v, const bool &inertFlag) : value(v), inert(inertFlag) {}

    std::string getLabel() const override { return value.label(); }
    int getValue() const override { return step(value.value()); }
    int getDefaultValue() const override { return step(value.meta().defaultVal); }
    void setValueFromGUI(const int &i) override
    {
        if (!inert)
        {
            value.set(static_cast<float>(i));
        }
    }
    void setValueFromModel(const int &) override {}
    int getMin() const override { return step(value.meta().minVal); }
    int getMax() const override { return step(value.meta().maxVal); }
    std::string getValueAsStringFor(int i) const override
    {
        return value.meta().valueToString(static_cast<float>(i)).value_or(std::to_string(i));
    }

    int step(float v) const
    {
        if (value.meta().type == ParamMetaData::BOOL)
        {
            return v > 0.5f ? 1 : 0;
        }
        return static_cast<int>(std::lround(v));
    }

    ParamValue &value;
    const bool &inert;
};

/// Whether the control takes edits: off in mod mode.
struct Inertia : juce::SettableTooltipClient
{
    bool inert{false};
};

/// An sst-jucegui control that ignores keys while inert, and steps a continuous one by the
/// arrow keys (stepByArrowKey).
template <typename Editor> struct Control final : Editor, Inertia
{
    bool keyPressed(const juce::KeyPress &key) override
    {
        if (inert)
        {
            return false;
        }
        if constexpr (std::is_base_of_v<jcmp::ContinuousParamEditor, Editor>)
        {
            if (stepByArrowKey(*this, key))
            {
                return true;
            }
        }
        return Editor::keyPressed(key);
    }
};

// The knob's geometry: rings and the band draw in a margin around its circle, which sits at the
// top of its bounds, its label below it.
constexpr int kRingMargin = 8;
constexpr int kKnobLabelHeight = 20;
constexpr float kRingGap = 2.0f;     ///< between the knob's circle and the first ring
constexpr float kRingSpacing = 3.0f; ///< between rings, at most
constexpr float kRingStroke = 2.0f;
constexpr float kRingDragPx = 150.0f; ///< a full depth's vertical drag, as sst-jucegui's knobs
constexpr float kArcSweep = 0.8f;     ///< sst-jucegui's knob arc: ±0.8π about the top

/// An arc's angle for a value in 0..1, as sst-jucegui's knob draws its own.
float angleOf(float v01)
{
    return kArcSweep * juce::MathConstants<float>::pi * (2.0f * std::clamp(v01, 0.0f, 1.0f) - 1.0f);
}
} // namespace

struct ParamControl::Impl
{
    Impl(const EditorContext &context, ParamSlot s, Kind k)
        : kind(k), value(context, s, labelFor(s, context.mode))
    {
    }

    /// The knob's circle in the control's coordinates; empty for another kind.
    juce::Rectangle<float> knobCircle() const
    {
        if (kind != Kind::Knob || control == nullptr)
        {
            return {};
        }
        const auto b = control->getBounds().toFloat();
        return b.withHeight(b.getWidth());
    }

    /// Ring i's radius, its spacing shrinking as rings are added so that they stay in the margin.
    float ringRadius(std::size_t i, float knobRadius) const
    {
        return knobRadius + kRingGap + (static_cast<float>(i) + 0.5f) * ringSpacing();
    }
    float ringSpacing() const
    {
        return rings.empty() ? kRingSpacing
                             : std::min(kRingSpacing, (kRingMargin - kRingGap) /
                                                          static_cast<float>(rings.size()));
    }

    /// The value shown, in 0..1 of its range.
    float shown01() const
    {
        const auto &meta = value.meta();
        const float range = meta.maxVal - meta.minVal;
        return range > 0.0f ? (value.value() - meta.minVal) / range : 0.0f;
    }

    const Kind kind;
    // The value and the sources before the control, so the control goes first.
    ParamValue value;
    std::unique_ptr<ContinuousSource> continuous;
    std::unique_ptr<DiscreteSource> discrete;
    std::unique_ptr<juce::Component> control;
    Inertia *inertia{nullptr};

    bool modMode{false};
    std::vector<Ring> rings;
    std::optional<std::pair<float, float>> band;
    std::optional<std::size_t> dragRing; ///< the ring a mod-mode drag holds
    float dragDepth0{0.0f};
};

namespace
{
/// Makes `editor` the control: its edits are the value's gestures.
template <typename Editor, typename Source>
std::unique_ptr<juce::Component> wire(std::unique_ptr<Control<Editor>> editor, Source *source,
                                      ParamValue &value, Inertia *&inertia, const bool &modMode)
{
    editor->setSource(source);
    // No gesture opens in mod mode, whichever path (the accessibility interface's) asks.
    editor->onBeginEdit = [&value, &modMode] {
        if (!modMode)
        {
            value.begin();
        }
    };
    editor->onEndEdit = [&value] { value.end(); };
    inertia = editor.get();
    return editor;
}
} // namespace

ParamControl::Kind ParamControl::kindFor(const ParamMetaData &meta) noexcept
{
    const bool stepped =
        (meta.flags & CLAP_PARAM_IS_STEPPED) != 0 || meta.type != ParamMetaData::FLOAT;
    if (!stepped)
    {
        return Kind::Knob;
    }
    const long values =
        meta.type == ParamMetaData::BOOL ? 2 : std::lround(meta.maxVal - meta.minVal) + 1;
    return values > 2 ? Kind::Menu : values == 2 ? Kind::Toggle : Kind::Knob;
}

ParamControl::ParamControl(const EditorContext &context, ParamSlot slot, Kind kind)
    : impl_(std::make_unique<Impl>(context, slot, kind))
{
    setComponentID("param." + juce::String(gg::core::paramId(slot)));
    setTitle(impl_->value.label());
    Impl &m = *impl_;
    switch (kind)
    {
    case Kind::Knob:
    case Kind::HSlider:
    case Kind::VSlider:
        m.continuous = std::make_unique<ContinuousSource>(m.value, m.modMode);
        if (kind == Kind::Knob)
        {
            m.control = wire(std::make_unique<Control<jcmp::Knob>>(), m.continuous.get(), m.value,
                             m.inertia, m.modMode);
        }
        else if (kind == Kind::HSlider)
        {
            m.control = wire(std::make_unique<Control<jcmp::HSlider>>(), m.continuous.get(),
                             m.value, m.inertia, m.modMode);
        }
        else
        {
            m.control = wire(std::make_unique<Control<jcmp::VSlider>>(), m.continuous.get(),
                             m.value, m.inertia, m.modMode);
        }
        break;
    case Kind::Toggle:
    {
        m.discrete = std::make_unique<DiscreteSource>(m.value, m.modMode);
        auto toggle = std::make_unique<Control<jcmp::ToggleButton>>();
        toggle->setLabel(m.value.label());
        toggle->setDrawMode(jcmp::ToggleButton::DrawMode::LABELED);
        m.control = wire(std::move(toggle), m.discrete.get(), m.value, m.inertia, m.modMode);
        break;
    }
    case Kind::Menu:
        m.discrete = std::make_unique<DiscreteSource>(m.value, m.modMode);
        m.control = wire(std::make_unique<Control<jcmp::MenuButtonDiscreteEditor>>(),
                         m.discrete.get(), m.value, m.inertia, m.modMode);
        break;
    }
    addAndMakeVisible(*m.control);
}

ParamControl::~ParamControl()
{
    impl_->value.end();
    impl_->control.reset();
}

void ParamControl::refresh()
{
    if (impl_->value.refresh())
    {
        repaint();
    }
}

void ParamControl::setCommitOnRelease(bool on) { impl_->value.setCommitOnRelease(on); }

void ParamControl::setTooltip(const juce::String &text)
{
    if (impl_->inertia != nullptr)
    {
        impl_->inertia->setTooltip(text);
    }
}

juce::String ParamControl::tooltip() const
{
    return impl_->inertia != nullptr ? impl_->inertia->getTooltip() : juce::String();
}

juce::String ParamControl::shownText() const
{
    const auto text = impl_->value.meta().valueToString(impl_->value.value());
    return juce::String::fromUTF8(text ? text->c_str() : "");
}

void ParamControl::setModMode(bool on)
{
    impl_->modMode = on;
    impl_->inertia->inert = on;
    impl_->control->setInterceptsMouseClicks(!on, !on);
    if (!on)
    {
        impl_->dragRing.reset();
    }
    repaint();
}

void ParamControl::setRings(std::vector<Ring> rings)
{
    impl_->rings = std::move(rings);
    if (impl_->dragRing && *impl_->dragRing >= impl_->rings.size())
    {
        impl_->dragRing.reset();
    }
    repaint();
}

const std::vector<ParamControl::Ring> &ParamControl::rings() const noexcept { return impl_->rings; }

void ParamControl::setSpreadBand(std::optional<std::pair<float, float>> lowHigh01)
{
    impl_->band = lowHigh01;
    repaint();
}

ParamSlot ParamControl::slot() const noexcept { return impl_->value.slot(); }

float ParamControl::shownValue() const noexcept { return impl_->value.value(); }

void ParamControl::resized()
{
    auto area = getLocalBounds();
    if (impl_->kind != Kind::Knob)
    {
        impl_->control->setBounds(area);
        return;
    }
    // The knob's circle is as wide as its bounds, at their top; the label takes what is left
    // below it. The margin around the circle is the rings'.
    area.removeFromTop(kRingMargin);
    const int side = std::max(
        0, std::min(area.getWidth() - 2 * kRingMargin, area.getHeight() - kKnobLabelHeight));
    impl_->control->setBounds(
        area.withSizeKeepingCentre(side, side + kKnobLabelHeight).withY(area.getY()));
}

void ParamControl::paintOverChildren(juce::Graphics &g)
{
    const Impl &m = *impl_;
    const auto circle = m.knobCircle();
    if (circle.isEmpty())
    {
        return;
    }
    const auto centre = circle.getCentre();
    const float radius = circle.getWidth() * 0.5f;
    const auto arc = [&g, centre](float r, float from01, float to01, float stroke) {
        juce::Path p;
        p.addCentredArc(centre.x, centre.y, r, r, 0.0f, angleOf(std::min(from01, to01)),
                        angleOf(std::max(from01, to01)), true);
        g.strokePath(p, juce::PathStrokeType(stroke));
    };
    const Palette &palette = paletteOf(*this);
    if (m.band)
    {
        g.setColour(palette.accent.withAlpha(0.3f));
        arc(radius - 2.0f, m.band->first, m.band->second, 3.0f);
    }
    const float v0 = m.shown01();
    const float stroke = std::min(kRingStroke, m.ringSpacing());
    for (std::size_t i = 0; i < m.rings.size(); ++i)
    {
        const Ring &ring = m.rings[i];
        const float d = ring.depth01;
        g.setColour(ring.colour.withAlpha(m.dragRing == i ? 1.0f : 0.85f));
        if (ring.bipolar)
        {
            arc(m.ringRadius(i, radius), v0 - std::abs(d), v0 + std::abs(d), stroke);
        }
        else
        {
            arc(m.ringRadius(i, radius), v0, v0 + d, stroke);
        }
    }
}

void ParamControl::mouseDown(const juce::MouseEvent &e)
{
    Impl &m = *impl_;
    m.dragRing.reset();
    const auto circle = m.knobCircle();
    if (!m.modMode || circle.isEmpty() || m.rings.empty())
    {
        return;
    }
    // The ring whose radius the press is nearest, within half the spacing and a pixel.
    const float radius = circle.getWidth() * 0.5f;
    const float distance = circle.getCentre().getDistanceFrom(e.position);
    const float spacing = m.ringSpacing();
    const float nearest = std::clamp(std::round((distance - m.ringRadius(0, radius)) / spacing),
                                     0.0f, static_cast<float>(m.rings.size() - 1));
    const auto i = static_cast<std::size_t>(nearest);
    if (std::abs(distance - m.ringRadius(i, radius)) <= spacing * 0.5f + 1.0f)
    {
        m.dragRing = i;
        m.dragDepth0 = m.rings[i].depth01;
        repaint();
    }
}

void ParamControl::mouseDrag(const juce::MouseEvent &e)
{
    Impl &m = *impl_;
    if (!m.modMode || !m.dragRing)
    {
        return;
    }
    Ring &ring = m.rings[*m.dragRing];
    const float depth =
        std::clamp(m.dragDepth0 - static_cast<float>(e.getDistanceFromDragStartY()) / kRingDragPx,
                   -1.0f, 1.0f);
    if (depth == ring.depth01)
    {
        return;
    }
    ring.depth01 = depth;
    if (onRingDrag)
    {
        onRingDrag(ring.slot, depth);
    }
    repaint();
}

void ParamControl::mouseUp(const juce::MouseEvent &)
{
    if (impl_->dragRing)
    {
        impl_->dragRing.reset();
        repaint();
    }
}
} // namespace gg::ui
