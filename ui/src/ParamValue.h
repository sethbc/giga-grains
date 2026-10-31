// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_PARAMVALUE_H
#define GG_UI_PARAMVALUE_H

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include <juce_gui_basics/juce_gui_basics.h>
#include <sst/basic-blocks/params/ParamMetadata.h>
#include <sst/jucegui/components/ContinuousParamEditor.h>
#include <sst/jucegui/data/Continuous.h>

#include "HeldValue.h"
#include "gg/core/ParamIds.h"
#include "gg/ui/Editor.h"
#include "gg/ui/UndoStack.h"

namespace gg::ui
{
/// The gesture plumbing every control shares (sub-project 5's macros, M2 sub-project 6's
/// ParamControl): one parameter's base value through the context, with the value sent held until
/// the base value catches up (HeldValue). An edit that arrives outside a begin/end pair (a path
/// of the control that sets a value without one) is made a gesture of its own, so the host never
/// sees a value without one. A NaN is ignored, and every value is clamped to the parameter's
/// range.
///
/// With commit on release (M2 sub-project 6's spec §6.7, D10) a gesture's values are shown but
/// not sent: one setValue, the gesture's last value, goes just before its endGesture, and a
/// gesture that set nothing sends none.
///
/// **Undo** (spec §6.8): with the context's undo stack, each gesture is recorded in it, labelled
/// with the parameter's name ("A Size"): begin opens it at the value shown then, end closes it at
/// the value shown, so a gesture that ends where it began makes no entry.
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class ParamValue
{
  public:
    /// `label` is what the control shows as its name.
    ParamValue(const EditorContext &context, gg::core::ParamSlot slot, std::string label)
        : context_(context), slot_(slot), meta_(gg::core::paramMeta(slot, context.mode)),
          label_(std::move(label)), value_(base())
    {
    }

    gg::core::ParamSlot slot() const noexcept { return slot_; }
    const sst::basic_blocks::params::ParamMetaData &meta() const noexcept { return meta_; }
    const std::string &label() const noexcept { return label_; }
    float value() const noexcept { return value_; }

    void setCommitOnRelease(bool on) noexcept { commitOnRelease_ = on; }

    void begin()
    {
        if (editing_)
        {
            return;
        }
        editing_ = true;
        if (context_.undo != nullptr)
        {
            context_.undo->beginGesture(slot_, value_, meta_.name);
        }
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
        if (commitOnRelease_)
        {
            pending_ = v;
        }
        else
        {
            send(v);
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
        if (pending_)
        {
            send(*pending_);
            pending_.reset();
        }
        editing_ = false;
        held_.end(base(), wallSeconds());
        if (context_.endGesture)
        {
            context_.endGesture(slot_);
        }
        if (context_.undo != nullptr)
        {
            context_.undo->endGesture(slot_, value_);
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

    void send(float v) const
    {
        if (context_.setValue)
        {
            context_.setValue(slot_, static_cast<double>(v));
        }
    }

    const EditorContext &context_;
    const gg::core::ParamSlot slot_;
    const sst::basic_blocks::params::ParamMetaData &meta_;
    const std::string label_;
    float value_;
    bool editing_{false};
    bool commitOnRelease_{false};
    std::optional<float> pending_;
    HeldValue held_;
};

/// The arrow keys' edit of a continuous control: Up and Right raise it by 1/100 of its range,
/// Down and Left lower it, by 1/1000 with Shift, each press one gesture, where sst-jucegui's
/// controls step by 1/40 and know no Left or Right. False, changing nothing, for any other key,
/// with Command held, or with no source: the control's own handling takes those.
inline bool stepByArrowKey(sst::jucegui::components::ContinuousParamEditor &editor,
                           const juce::KeyPress &key)
{
    const int code = key.getKeyCode();
    const bool up = code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey;
    const bool down = code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey;
    sst::jucegui::data::Continuous *source = editor.continuous();
    if ((!up && !down) || source == nullptr || key.getModifiers().isCommandDown())
    {
        return false;
    }
    const float range = source->getMax() - source->getMin();
    const float step = range * (key.getModifiers().isShiftDown() ? 0.001f : 0.01f);
    const float v =
        std::clamp(source->getValue() + (up ? step : -step), source->getMin(), source->getMax());
    editor.onBeginEdit();
    source->setValueFromGUI(v);
    editor.onEndEdit();
    editor.notifyAccessibleChange();
    editor.repaint();
    return true;
}
} // namespace gg::ui

#endif // GG_UI_PARAMVALUE_H
