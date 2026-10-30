// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "MutatePanel.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <sst/basic-blocks/params/ParamMetadata.h>
#include <sst/jucegui/components/Knob.h>
#include <sst/jucegui/data/Continuous.h>

#include "Theme.h"

namespace gg::ui
{
namespace
{
using gg::core::LayerParam;
using gg::core::ParamSlot;
namespace jcmp = sst::jucegui::components;
namespace jdata = sst::jucegui::data;

constexpr int kRowHeight = 24;
constexpr int kRowWidth = 150;
constexpr int kGap = 4;
constexpr int kHeadingHeight = 26;
constexpr int kBarHeight = 64;
constexpr float kDefaultAmount = 0.15f;

juce::String fromUtf8(const std::string &text) { return juce::String::fromUTF8(text.c_str()); }

void styleButton(juce::TextButton &button, const juce::String &id, const juce::String &text)
{
    button.setComponentID(id);
    button.setButtonText(text);
    button.setColour(juce::TextButton::buttonColourId, theme::kBackground);
    button.setColour(juce::TextButton::buttonOnColourId, theme::kAccent.withAlpha(0.35f));
    button.setColour(juce::TextButton::textColourOffId, theme::kDimText);
    button.setColour(juce::TextButton::textColourOnId, theme::kText);
    button.setColour(juce::ComboBox::outlineColourId, theme::kOutline);
}

/// The amount knob's source: a value of the panel's own, 0 to 1.
struct AmountSource final : jdata::Continuous
{
    std::string getLabel() const override { return "Amount"; }
    float getValue() const override { return value; }
    void setValueFromGUI(const float &f) override
    {
        value = std::clamp(f, 0.0f, 1.0f);
        if (changed)
        {
            changed();
        }
    }
    void setValueFromModel(const float &) override {}
    float getDefaultValue() const override { return kDefaultAmount; }
    float getMin() const override { return 0.0f; }
    float getMax() const override { return 1.0f; }
    std::string getValueAsStringFor(float f) const override
    {
        return std::to_string(static_cast<int>(f * 100.0f + 0.5f)) + "%";
    }

    float value{kDefaultAmount};
    std::function<void()> changed;
};

/// "A Size" -> "Size": a layer parameter's name less its layer's letter.
std::string withoutLayerLetter(const std::string &name, char letter)
{
    return name.size() > 2 && name[0] == letter && name[1] == ' ' ? name.substr(2) : name;
}

bool isSpace(ParamSlot slot) noexcept
{
    return slot >= ParamSlot::ReverbAmount && slot <= ParamSlot::DryToReverb;
}

bool isStep(LayerParam p) noexcept { return p >= LayerParam::Step1 && p <= LayerParam::Step16; }
} // namespace

struct MutatePanel::Impl
{
    /// One toggle: the slots it locks together (one, or a layer's 16 steps).
    struct Row
    {
        std::vector<ParamSlot> slots;
        juce::TextButton button;
    };
    struct Section
    {
        juce::Label heading;
        juce::TextButton lockAll, unlockAll; // a layer's only
        std::vector<std::unique_ptr<Row>> rows;
        bool isLayer{false};
        std::vector<ParamSlot> layerSlots; // all of a layer's, for lock all
    };

    /// The viewport's content: the sections stacked, each heading over a flow of toggles.
    struct Content final : juce::Component
    {
        explicit Content(Impl &o) : owner(o) { setComponentID("mutate.lockList"); }

        /// Places everything for `width`; the content's height follows.
        void layoutFor(int width)
        {
            int y = 4;
            for (auto &section : owner.sections)
            {
                auto head = juce::Rectangle<int>(4, y, width - 8, kHeadingHeight);
                if (section->isLayer)
                {
                    section->unlockAll.setBounds(head.removeFromRight(90));
                    head.removeFromRight(kGap);
                    section->lockAll.setBounds(head.removeFromRight(90));
                }
                section->heading.setBounds(head);
                y += kHeadingHeight + kGap;
                const int perLine = std::max(1, (width - 8 + kGap) / (kRowWidth + kGap));
                int i = 0;
                for (auto &row : section->rows)
                {
                    row->button.setBounds(4 + (i % perLine) * (kRowWidth + kGap),
                                          y + (i / perLine) * (kRowHeight + kGap), kRowWidth,
                                          kRowHeight);
                    ++i;
                }
                y += ((i + perLine - 1) / perLine) * (kRowHeight + kGap) + 8;
            }
            setSize(width, y);
        }

        Impl &owner;
    };

    Impl(MutatePanel &p, const EditorContext &c) : panel(p), context(c), content(*this) {}

    void addSection(const juce::String &title, const juce::String &idLetter, bool isLayer)
    {
        auto section = std::make_unique<Section>();
        section->isLayer = isLayer;
        section->heading.setText(title, juce::dontSendNotification);
        section->heading.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        section->heading.setColour(juce::Label::textColourId, theme::kText);
        content.addAndMakeVisible(section->heading);
        if (isLayer)
        {
            Section *raw = section.get();
            styleButton(section->lockAll, "mutate.lockall." + idLetter, "Lock all");
            styleButton(section->unlockAll, "mutate.unlockall." + idLetter, "Unlock all");
            section->lockAll.onClick = [this, raw] { setAll(*raw, true); };
            section->unlockAll.onClick = [this, raw] { setAll(*raw, false); };
            content.addAndMakeVisible(section->lockAll);
            content.addAndMakeVisible(section->unlockAll);
        }
        sections.push_back(std::move(section));
    }

    void addRow(const juce::String &label, std::vector<ParamSlot> slots)
    {
        Section &section = *sections.back();
        auto row = std::make_unique<Row>();
        row->slots = std::move(slots);
        styleButton(row->button,
                    "mutate.lock." + juce::String(static_cast<int>(row->slots.front())), label);
        row->button.setClickingTogglesState(true);
        row->button.setTooltip("Locked: Mutate leaves " + label + " where it is");
        Row *raw = row.get();
        row->button.onClick = [this, raw] {
            const bool locked = raw->button.getToggleState();
            for (const ParamSlot slot : raw->slots)
            {
                setLock(slot, locked);
            }
        };
        content.addAndMakeVisible(row->button);
        section.rows.push_back(std::move(row));
    }

    void setLock(ParamSlot slot, bool locked) const
    {
        if (context.setMutateLock)
        {
            context.setMutateLock(slot, locked);
        }
    }

    void setAll(Section &section, bool locked)
    {
        for (const ParamSlot slot : section.layerSlots)
        {
            setLock(slot, locked);
        }
        showLocks();
    }

    /// Every toggle on while all its slots are locked.
    void showLocks()
    {
        for (auto &section : sections)
        {
            for (auto &row : section->rows)
            {
                bool all = context.mutateLocked != nullptr;
                for (const ParamSlot slot : row->slots)
                {
                    all = all && context.mutateLocked(slot);
                }
                row->button.setToggleState(all, juce::dontSendNotification);
            }
        }
    }

    void build()
    {
        const auto mode = context.mode;
        const bool hasDry = mode == gg::core::Mode::Fx;
        // The sections in order; a slot goes to the first that claims it.
        addSection("Global", "", false);
        for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
        {
            const auto slot = static_cast<ParamSlot>(s);
            if (gg::core::layerOf(slot) || gg::core::isMacro(slot) || isSpace(slot) ||
                (slot == ParamSlot::Mix && !hasDry))
            {
                continue;
            }
            addRow(fromUtf8(gg::core::paramMeta(slot, mode).name), {slot});
        }
        addSection("Space", "", false);
        for (std::size_t s = static_cast<std::size_t>(ParamSlot::ReverbAmount);
             s <= static_cast<std::size_t>(ParamSlot::DryToReverb); ++s)
        {
            const auto slot = static_cast<ParamSlot>(s);
            if (slot == ParamSlot::DryToReverb && !hasDry)
            {
                continue;
            }
            addRow(fromUtf8(gg::core::paramMeta(slot, mode).name), {slot});
        }
        addSection("Macros", "", false);
        for (const ParamSlot slot : gg::core::kMacroSlots)
        {
            addRow(fromUtf8(gg::core::paramMeta(slot, mode).name), {slot});
        }
        for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
        {
            const char letter = static_cast<char>('A' + layer);
            addSection(juce::String("Layer ") + juce::String::charToString(letter),
                       juce::String::charToString(letter), true);
            Section &section = *sections.back();
            for (std::size_t p = 0; p < gg::core::kLayerParamCount; ++p)
            {
                const auto param = static_cast<LayerParam>(p);
                const ParamSlot slot = gg::core::layerSlot(param, layer);
                section.layerSlots.push_back(slot);
                if (isStep(param))
                {
                    // The 16 steps are one row, named for Step 1's slot.
                    if (param == LayerParam::Step1)
                    {
                        std::vector<ParamSlot> steps;
                        for (std::size_t i = 0; i < 16; ++i)
                        {
                            steps.push_back(gg::core::layerSlot(
                                static_cast<LayerParam>(static_cast<std::size_t>(param) + i),
                                layer));
                        }
                        addRow("Steps", std::move(steps));
                    }
                    continue;
                }
                addRow(fromUtf8(withoutLayerLetter(gg::core::paramMeta(slot, mode).name, letter)),
                       {slot});
            }
        }
    }

    MutatePanel &panel;
    const EditorContext &context;
    AmountSource amountSource;
    jcmp::Knob amountKnob;
    juce::Label amountText, legend;
    juce::TextButton go, reset, close;
    juce::Viewport viewport;
    Content content;
    std::vector<std::unique_ptr<Section>> sections;
    juce::Rectangle<int> barArea;
};

MutatePanel::MutatePanel(const EditorContext &context)
    : impl_(std::make_unique<Impl>(*this, context))
{
    Impl &m = *impl_;
    setComponentID("preset.mutatePanel");
    setTitle("Mutate");
    setWantsKeyboardFocus(true);
    setVisible(false);

    m.legend.setText("Highlighted parameters are locked: Mutate leaves them where they are.",
                     juce::dontSendNotification);
    m.legend.setFont(juce::FontOptions(12.0f));
    m.legend.setColour(juce::Label::textColourId, theme::kDimText);
    m.amountText.setComponentID("mutate.amountText");
    m.amountText.setFont(juce::FontOptions(13.0f));
    m.amountText.setColour(juce::Label::textColourId, theme::kText);
    auto showAmount = [&m] {
        m.amountText.setText("Amount " +
                                 fromUtf8(m.amountSource.getValueAsStringFor(m.amountSource.value)),
                             juce::dontSendNotification);
    };
    m.amountSource.changed = showAmount;
    showAmount();
    m.amountKnob.setSource(&m.amountSource);
    m.amountKnob.setComponentID("mutate.amount");

    styleButton(m.go, "mutate.go", "Mutate");
    styleButton(m.reset, "mutate.reset", "Reset locks");
    styleButton(m.close, "mutate.close", "Close");
    m.go.setColour(juce::TextButton::textColourOffId, theme::kText);
    m.go.setTooltip("Move every parameter that is not locked, by the amount");
    m.reset.setTooltip("Lock the structure (Mix, Level, the layers' on switches, the macros...)"
                       " and the layers that are off");
    m.go.onClick = [this] {
        if (impl_->context.mutate)
        {
            impl_->context.mutate(impl_->amountSource.value);
        }
    };
    m.reset.onClick = [this] {
        if (impl_->context.resetMutateLocks)
        {
            impl_->context.resetMutateLocks();
        }
        impl_->showLocks();
    };
    m.close.onClick = [this] { setVisible(false); };

    m.build();
    m.viewport.setViewedComponent(&m.content, false);
    m.viewport.setScrollBarsShown(true, false);
    for (juce::Component *c : std::initializer_list<juce::Component *>{
             &m.legend, &m.amountKnob, &m.amountText, &m.go, &m.reset, &m.close, &m.viewport})
    {
        addAndMakeVisible(*c);
    }
    m.showLocks();
}

MutatePanel::~MutatePanel() { impl_->viewport.setViewedComponent(nullptr, false); }

void MutatePanel::refresh()
{
    if (impl_->context.flushMutate)
    {
        impl_->context.flushMutate();
    }
    if (isVisible())
    {
        impl_->showLocks();
    }
}

void MutatePanel::paint(juce::Graphics &g)
{
    g.fillAll(theme::kBackground.withAlpha(0.97f));
    g.setColour(theme::kOutline);
    g.drawRect(getLocalBounds());
}

void MutatePanel::resized()
{
    Impl &m = *impl_;
    auto area = getLocalBounds().reduced(10, 8);
    auto bar = area.removeFromTop(kBarHeight);
    m.amountKnob.setBounds(bar.removeFromLeft(kBarHeight));
    bar.removeFromLeft(kGap);
    auto text = bar.removeFromLeft(110);
    m.amountText.setBounds(text.withSizeKeepingCentre(text.getWidth(), 22));
    m.go.setBounds(bar.removeFromLeft(90).withSizeKeepingCentre(90, 28));
    bar.removeFromLeft(kGap);
    m.reset.setBounds(bar.removeFromLeft(100).withSizeKeepingCentre(100, 28));
    bar.removeFromLeft(kGap);
    m.close.setBounds(bar.removeFromRight(70).withSizeKeepingCentre(70, 28));
    m.legend.setBounds(bar.withSizeKeepingCentre(bar.getWidth(), 36));
    area.removeFromTop(kGap);
    m.viewport.setBounds(area);
    m.content.layoutFor(std::max(kRowWidth + 8, m.viewport.getMaximumVisibleWidth()));
}

bool MutatePanel::keyPressed(const juce::KeyPress &key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        setVisible(false);
        return true;
    }
    return false;
}
} // namespace gg::ui
