// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/ui/UndoStack.h"

#include <algorithm>
#include <span>
#include <utility>

namespace gg::ui
{
namespace
{
template <class... F> struct Overloaded : F...
{
    using F::operator()...;
};

/// Sets a flag for its lifetime: the stack is applying an entry.
struct Applying
{
    explicit Applying(bool &flag) noexcept : flag_(flag) { flag_ = true; }
    ~Applying() { flag_ = false; }
    Applying(const Applying &) = delete;
    Applying &operator=(const Applying &) = delete;

  private:
    bool &flag_;
};
} // namespace

UndoStack::UndoStack(UndoCallbacks callbacks) : callbacks_(std::move(callbacks)) {}

void UndoStack::beginGesture(gg::core::ParamSlot slot, float before, std::string label)
{
    beginGesture(slot, before, std::move(label), GestureOptions{});
}

void UndoStack::beginGesture(gg::core::ParamSlot slot, float before, std::string label,
                             GestureOptions options)
{
    if (applying_)
    {
        return;
    }
    const bool open =
        std::ranges::any_of(open_, [slot](const OpenGesture &g) { return g.slot == slot; });
    if (!open)
    {
        open_.push_back({slot, before, std::move(label), options});
    }
}

void UndoStack::endGesture(gg::core::ParamSlot slot, float after)
{
    if (applying_)
    {
        return;
    }
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
        groupNudge_ = groupNudge_ && gesture.options.nudge;
        groupSeconds_ = std::max(groupSeconds_, gesture.options.timeSeconds);
        return;
    }
    pushOrMerge(
        {std::move(gesture.label), change, gesture.options.nudge, gesture.options.timeSeconds},
        gesture.options.timeSeconds);
}

void UndoStack::beginGroup()
{
    if (!applying_)
    {
        ++groupDepth_;
    }
}

void UndoStack::endGroup()
{
    if (applying_ || groupDepth_ == 0 || --groupDepth_ > 0)
    {
        return;
    }
    if (!group_.empty())
    {
        pushOrMerge({std::move(groupLabel_), std::move(group_), groupNudge_, groupSeconds_},
                    groupSeconds_);
    }
    group_.clear();
    groupLabel_.clear();
    groupNudge_ = true;
    groupSeconds_ = 0.0;
}

void UndoStack::pushRoutes(const gg::core::ModRoutes &before, const gg::core::ModRoutes &after,
                           std::string label)
{
    if (!applying_ && before != after)
    {
        push({std::move(label), RoutesChange{before, after}});
    }
}

void UndoStack::pushSnapshot(std::string label, const gg::core::ParamValues &vBefore,
                             const gg::core::ModRoutes &rBefore,
                             const gg::core::ParamValues &vAfter, const gg::core::ModRoutes &rAfter)
{
    if (applying_)
    {
        return;
    }
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
    if (applying_ || busy() || !canUndo())
    {
        return;
    }
    const Applying applying(applying_);
    --cursor_;
    const auto undoValues = [this](const ParamChanges &changes) {
        applyValues(changes, true, false);
    };
    std::visit(Overloaded{[this](const ParamChange &c) { callbacks_.setValue(c.slot, c.before); },
                          undoValues,
                          [this](const RoutesChange &c) { callbacks_.setRoutes(c.before); },
                          [this](const Snapshot &s) {
                              if (s.routes)
                              {
                                  callbacks_.setRoutes(s.routes->before);
                              }
                              applyValues(s.values, true, true);
                          }},
               entries_[cursor_].change);
}

void UndoStack::redo()
{
    if (applying_ || busy() || !canRedo())
    {
        return;
    }
    const Applying applying(applying_);
    const auto redoValues = [this](const ParamChanges &changes) {
        applyValues(changes, false, false);
    };
    std::visit(Overloaded{[this](const ParamChange &c) { callbacks_.setValue(c.slot, c.after); },
                          redoValues,
                          [this](const RoutesChange &c) { callbacks_.setRoutes(c.after); },
                          [this](const Snapshot &s) {
                              applyValues(s.values, false, true);
                              if (s.routes)
                              {
                                  callbacks_.setRoutes(s.routes->after);
                              }
                          }},
               entries_[cursor_].change);
    ++cursor_;
}

void UndoStack::applyValues(const ParamChanges &changes, bool undoing, bool batch)
{
    // A group's gestures go back in reverse order and forward in order. A snapshot's go through
    // setValues as one call, so the owner can batch them (a snapshot is up to every parameter).
    if (batch && callbacks_.setValues)
    {
        std::vector<std::pair<gg::core::ParamSlot, double>> values;
        values.reserve(changes.size());
        const auto add = [&values, undoing](const ParamChange &c) {
            values.emplace_back(c.slot, static_cast<double>(undoing ? c.before : c.after));
        };
        if (undoing)
        {
            std::ranges::for_each(changes.rbegin(), changes.rend(), add);
        }
        else
        {
            std::ranges::for_each(changes, add);
        }
        if (!values.empty())
        {
            callbacks_.setValues(values);
        }
        return;
    }
    const auto set = [this, undoing](const ParamChange &c) {
        callbacks_.setValue(c.slot, undoing ? c.before : c.after);
    };
    if (undoing)
    {
        std::ranges::for_each(changes.rbegin(), changes.rend(), set);
    }
    else
    {
        std::ranges::for_each(changes, set);
    }
}

void UndoStack::pushOrMerge(Entry entry, double beginSeconds)
{
    if (entry.nudge && cursor_ == entries_.size() && !entries_.empty())
    {
        Entry &top = entries_.back();
        if (top.nudge && beginSeconds - top.endSeconds <= kNudgeMergeSeconds &&
            beginSeconds >= top.endSeconds)
        {
            // The same parameters, in the same order, on both sides.
            const auto same = [](const ParamChange &a, const ParamChange &b) {
                return a.slot == b.slot;
            };
            const auto asChanges = [](Entry &e) -> std::span<ParamChange> {
                if (auto *one = std::get_if<ParamChange>(&e.change))
                {
                    return {one, 1};
                }
                if (auto *many = std::get_if<ParamChanges>(&e.change))
                {
                    return {many->data(), many->size()};
                }
                return {};
            };
            const std::span<ParamChange> older = asChanges(top);
            const std::span<ParamChange> newer = asChanges(entry);
            if (!older.empty() && std::ranges::equal(older, newer, same))
            {
                bool changed = false;
                for (std::size_t i = 0; i < older.size(); ++i)
                {
                    older[i].after = newer[i].after;
                    changed = changed || older[i].after != older[i].before;
                }
                top.endSeconds = entry.endSeconds;
                if (!changed)
                {
                    entries_.pop_back();
                    cursor_ = entries_.size();
                }
                return;
            }
        }
    }
    push(std::move(entry));
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
