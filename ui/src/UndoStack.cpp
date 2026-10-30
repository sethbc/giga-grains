// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/ui/UndoStack.h"

#include <algorithm>
#include <utility>

namespace gg::ui
{
namespace
{
template <class... F> struct Overloaded : F...
{
    using F::operator()...;
};
} // namespace

UndoStack::UndoStack(UndoCallbacks callbacks) : callbacks_(std::move(callbacks)) {}

void UndoStack::beginGesture(gg::core::ParamSlot slot, float before, std::string label)
{
    const bool open =
        std::ranges::any_of(open_, [slot](const OpenGesture &g) { return g.slot == slot; });
    if (!open)
    {
        open_.push_back({slot, before, std::move(label)});
    }
}

void UndoStack::endGesture(gg::core::ParamSlot slot, float after)
{
    const auto it =
        std::ranges::find_if(open_, [slot](const OpenGesture &g) { return g.slot == slot; });
    if (it == open_.end())
    {
        return;
    }
    OpenGesture gesture = std::move(*it);
    open_.erase(it);
    if (after == gesture.before)
    {
        return;
    }
    const ParamChange change{slot, gesture.before, after};
    if (groupDepth_ > 0)
    {
        if (group_.empty())
        {
            groupLabel_ = std::move(gesture.label);
        }
        group_.push_back(change);
        return;
    }
    push({std::move(gesture.label), change});
}

void UndoStack::beginGroup() { ++groupDepth_; }

void UndoStack::endGroup()
{
    if (groupDepth_ == 0 || --groupDepth_ > 0)
    {
        return;
    }
    if (!group_.empty())
    {
        push({std::move(groupLabel_), std::move(group_)});
    }
    group_.clear();
    groupLabel_.clear();
}

void UndoStack::pushRoutes(const gg::core::ModRoutes &before, const gg::core::ModRoutes &after,
                           std::string label)
{
    if (before != after)
    {
        push({std::move(label), RoutesChange{before, after}});
    }
}

void UndoStack::pushSnapshot(std::string label, const gg::core::ParamValues &vBefore,
                             const gg::core::ModRoutes &rBefore,
                             const gg::core::ParamValues &vAfter, const gg::core::ModRoutes &rAfter)
{
    Snapshot snapshot;
    for (std::size_t i = 0; i < gg::core::kParamCount; ++i)
    {
        if (vBefore[i] != vAfter[i])
        {
            snapshot.values.push_back({static_cast<gg::core::ParamSlot>(i), vBefore[i], vAfter[i]});
        }
    }
    if (rBefore != rAfter)
    {
        snapshot.routes = RoutesChange{rBefore, rAfter};
    }
    if (!snapshot.values.empty() || snapshot.routes)
    {
        push({std::move(label), std::move(snapshot)});
    }
}

std::string UndoStack::undoLabel() const
{
    return canUndo() ? entries_[cursor_ - 1].label : std::string{};
}

std::string UndoStack::redoLabel() const
{
    return canRedo() ? entries_[cursor_].label : std::string{};
}

void UndoStack::undo()
{
    if (!canUndo())
    {
        return;
    }
    --cursor_;
    const auto undoValues = [this](const ParamChanges &changes) {
        for (auto it = changes.rbegin(); it != changes.rend(); ++it)
        {
            callbacks_.setValue(it->slot, it->before);
        }
    };
    std::visit(Overloaded{[this](const ParamChange &c) { callbacks_.setValue(c.slot, c.before); },
                          undoValues,
                          [this](const RoutesChange &c) { callbacks_.setRoutes(c.before); },
                          [this, &undoValues](const Snapshot &s) {
                              if (s.routes)
                              {
                                  callbacks_.setRoutes(s.routes->before);
                              }
                              undoValues(s.values);
                          }},
               entries_[cursor_].change);
}

void UndoStack::redo()
{
    if (!canRedo())
    {
        return;
    }
    const auto redoValues = [this](const ParamChanges &changes) {
        for (const ParamChange &c : changes)
        {
            callbacks_.setValue(c.slot, c.after);
        }
    };
    std::visit(Overloaded{[this](const ParamChange &c) { callbacks_.setValue(c.slot, c.after); },
                          redoValues,
                          [this](const RoutesChange &c) { callbacks_.setRoutes(c.after); },
                          [this, &redoValues](const Snapshot &s) {
                              redoValues(s.values);
                              if (s.routes)
                              {
                                  callbacks_.setRoutes(s.routes->after);
                              }
                          }},
               entries_[cursor_].change);
    ++cursor_;
}

void UndoStack::push(Entry entry)
{
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_), entries_.end());
    entries_.push_back(std::move(entry));
    if (entries_.size() > kDepth)
    {
        entries_.pop_front();
    }
    cursor_ = entries_.size();
}
} // namespace gg::ui
