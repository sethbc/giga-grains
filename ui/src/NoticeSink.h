// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_NOTICESINK_H
#define GG_UI_NOTICESINK_H

#include <string>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/ui/Editor.h"

namespace gg::ui
{
/// What shows the editor's transient messages (M2 sub-project 6's spec §6.2): the Editor, whose
/// header shows each for 4 s after passing it to the context's notice.
struct NoticeSink
{
    virtual ~NoticeSink() = default;
    virtual void showNotice(const std::string &utf8) = 0;
};

/// Tells the user `utf8` (a refused drop): the nearest NoticeSink at or above `from` shows it,
/// or, with none (a component built alone by a test), the context's notice is called with it,
/// if set. Message thread.
inline void postNotice(juce::Component &from, const EditorContext &context, const std::string &utf8)
{
    for (juce::Component *at = &from; at != nullptr; at = at->getParentComponent())
    {
        if (auto *sink = dynamic_cast<NoticeSink *>(at))
        {
            sink->showNotice(utf8);
            return;
        }
    }
    if (context.notice)
    {
        context.notice(utf8);
    }
}
} // namespace gg::ui

#endif // GG_UI_NOTICESINK_H
