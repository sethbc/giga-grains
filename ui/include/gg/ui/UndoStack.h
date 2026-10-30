// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_UNDOSTACK_H
#define GG_UI_UNDOSTACK_H

#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"

namespace gg::ui
{
/// How an undo or redo reaches the plugin (spec §6.8). Both must be set.
struct UndoCallbacks
{
    /// Sets one parameter. The owner makes each call one gesture (begin, set, end), so hosts
    /// record it as automation.
    std::function<void(gg::core::ParamSlot, float)> setValue;
    /// Replaces the mod matrix's 48 routes.
    std::function<void(const gg::core::ModRoutes &)> setRoutes;
};

/// The editor's undo and redo history (spec §6.8, D7): framework-free, knowing nothing of JUCE or
/// the plugin. It records the editor's own edits and replays them through UndoCallbacks; host
/// automation, state loads and the audio thread push nothing. UI thread only.
///
/// - **Entries:** a parameter gesture (its value at beginGesture and at endGesture), a group of
///   gestures (the XY pad's two parameters as one step), a routes edit and a whole-state snapshot.
/// - **Undo applies the entry's before, redo its after,** whatever the values are now.
/// - **At most kDepth entries,** the oldest dropped. A new entry clears what could be redone.
class UndoStack
{
  public:
    static constexpr std::size_t kDepth = 200;

    explicit UndoStack(UndoCallbacks callbacks);

    /// Opens a gesture on `slot` at value `before`, labelled `label` ("A Size"). A second begin of
    /// a slot whose gesture is still open is ignored (its first before and label are kept);
    /// gestures on different slots may be open at once.
    void beginGesture(gg::core::ParamSlot slot, float before, std::string label);
    /// Closes the gesture on `slot` at value `after`: one entry, or one gesture in the open group.
    /// No entry when `after` equals the before exactly, or when no gesture on `slot` is open.
    void endGesture(gg::core::ParamSlot slot, float after);

    /// Opens a group: the gestures that end before endGroup make one entry, labelled with the
    /// first's label. Groups nest; the outermost endGroup closes it.
    void beginGroup();
    /// Closes the group, pushing its entry if any of its gestures changed a value. Without an open
    /// group, does nothing.
    void endGroup();

    /// Pushes a routes edit (a route's source, target, depth or curve changed). No entry when the
    /// two are equal.
    void pushRoutes(const gg::core::ModRoutes &before, const gg::core::ModRoutes &after,
                    std::string label);
    /// Pushes a whole-state entry (sub-project 5's Mutate and preset recall). Only the values and
    /// the routes that differ are kept and applied; no entry when nothing differs.
    void pushSnapshot(std::string label, const gg::core::ParamValues &vBefore,
                      const gg::core::ModRoutes &rBefore, const gg::core::ParamValues &vAfter,
                      const gg::core::ModRoutes &rAfter);

    bool canUndo() const noexcept { return cursor_ > 0; }
    bool canRedo() const noexcept { return cursor_ < entries_.size(); }
    /// The label of the entry undo() would apply; "" when none.
    std::string undoLabel() const;
    /// The label of the entry redo() would apply; "" when none.
    std::string redoLabel() const;

    /// Applies the last entry's before through the callbacks and moves the cursor back. A group's
    /// gestures are undone in reverse order. Does nothing, calling no callback, when !canUndo().
    void undo();
    /// Applies the next entry's after through the callbacks and moves the cursor on. A group's
    /// gestures are redone in order. Does nothing, calling no callback, when !canRedo().
    void redo();

    /// The entries kept, undoable and redoable together; at most kDepth.
    std::size_t size() const noexcept { return entries_.size(); }

  private:
    struct ParamChange
    {
        gg::core::ParamSlot slot;
        float before;
        float after;
    };
    struct RoutesChange
    {
        gg::core::ModRoutes before;
        gg::core::ModRoutes after;
    };
    /// A group's gestures, or a snapshot's changed values, in the order they are redone.
    using ParamChanges = std::vector<ParamChange>;
    struct Snapshot
    {
        ParamChanges values;
        std::optional<RoutesChange> routes;
    };
    struct Entry
    {
        std::string label;
        std::variant<ParamChange, ParamChanges, RoutesChange, Snapshot> change;
    };
    struct OpenGesture
    {
        gg::core::ParamSlot slot;
        float before;
        std::string label;
    };

    void push(Entry entry);

    UndoCallbacks callbacks_;
    std::deque<Entry> entries_;
    std::size_t cursor_{0}; // the entries applied: undo() applies entries_[cursor_ - 1]
    std::vector<OpenGesture> open_;
    int groupDepth_{0};
    std::string groupLabel_;
    ParamChanges group_;
};
} // namespace gg::ui

#endif // GG_UI_UNDOSTACK_H
