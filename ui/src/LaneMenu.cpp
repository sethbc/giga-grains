// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The canvas's lane menu (ruling VW-P2a) and its choosers: GrainCanvas members, apart from the
// lanes' drawing and drags in GrainCanvas.cpp.

#include "GrainCanvas.h"

#include "SampleFiles.h"

#include <optional>
#include <string>

#include "NoticeSink.h"
#include "gg/core/ParamMap.h"
#include "gg/ui/UndoStack.h"

namespace gg::ui
{
namespace
{
using gg::core::LayerParam;
using gg::core::ParamSlot;

juce::String withEllipsis(const char *text)
{
    return juce::String(text) + juce::String::charToString(0x2026);
}
} // namespace

void GrainCanvas::openLaneMenu(uint8_t layer, std::optional<int64_t> frame)
{
    menuLayer_ = layer;
    menuFrame_ = frame;
    if (showMenu)
    {
        showMenu(laneMenu(layer));
    }
}

juce::PopupMenu GrainCanvas::laneMenu(uint8_t layer)
{
    juce::PopupMenu menu;
    if (layer >= kLaneCount)
    {
        return menu;
    }
    forgetFrame();
    const juce::Component::SafePointer<GrainCanvas> safe(this);
    if (!liveLane(layer))
    {
        menu.addItem(withEllipsis("Load sample"), [safe, layer] {
            if (safe != nullptr)
            {
                safe->chooseSampleFor(layer);
            }
        });
        return menu;
    }
    const std::optional<int64_t> frame = menuLayer_ == layer ? menuFrame_ : std::nullopt;
    const RingView ring = ringOf(layer);
    const gg::core::RingParams params = ringParamsOf(layer);
    const uint32_t splice = params.splice; // the selected one, or 0 for the window
    // Commands, not parameters (VW-P2b): no undo entry. The mark is the ring frame clicked
    // (AddSpliceAt), so it lands there however far the head runs before the command applies.
    menu.addItem(juce::PopupMenu::Item("Add splice mark here")
                     .setEnabled(ring.valid)
                     .setAction([safe, layer, frame] {
                         if (safe != nullptr && safe->context_.ringCommand)
                         {
                             // The frame clicked itself (carry-over row 388 (m)): wherever the
                             // head is when the command applies. From the keyboard, at the
                             // anchor as it applies.
                             if (frame)
                             {
                                 safe->context_.ringCommand(layer, RingCommand::AddSpliceAt,
                                                            static_cast<double>(*frame));
                             }
                             else
                             {
                                 safe->context_.ringCommand(layer, RingCommand::AddSplice, 0.0);
                             }
                             safe->repaint();
                         }
                     }));
    menu.addItem(juce::PopupMenu::Item("Clear splice marks")
                     .setEnabled(ring.valid && ring.spliceCount > 0)
                     .setAction([safe, layer] {
                         if (safe != nullptr && safe->context_.ringCommand)
                         {
                             safe->context_.ringCommand(layer, RingCommand::ClearSplices, 0.0f);
                             safe->repaint();
                         }
                     }));
    // Loop is a parameter: one gesture, so one undo entry (VW-P2b), and it can be set inactive.
    menu.addItem(juce::PopupMenu::Item("Loop").setTicked(params.loop).setAction([safe, layer] {
        if (safe == nullptr)
        {
            return;
        }
        GrainCanvas &c = *safe;
        const ParamSlot slot = gg::core::layerSlot(LayerParam::Loop, layer);
        const float before = c.context_.baseValue ? c.context_.baseValue(slot) : 0.0f;
        const float after = before > 0.5f ? 0.0f : 1.0f;
        c.recordBegin(slot, before);
        if (c.context_.beginGesture)
        {
            c.context_.beginGesture(slot);
        }
        if (c.context_.setValue)
        {
            c.context_.setValue(slot, static_cast<double>(after));
        }
        if (c.context_.endGesture)
        {
            c.context_.endGesture(slot);
        }
        c.recordEnd(slot, after);
        c.repaint();
    }));
    juce::PopupMenu targets;
    for (uint8_t target = 0; target < kLaneCount; ++target)
    {
        targets.addItem(
            juce::String::charToString(static_cast<juce::juce_wchar>(kLayerLetters[target])),
            [safe, layer, splice, target] {
                if (safe == nullptr || !safe->context_.captureTo)
                {
                    return;
                }
                const bool live = safe->liveLane(target); // the kind built before the load
                const std::string message = safe->context_.captureTo(layer, splice, target);
                if (!message.empty())
                {
                    postNotice(*safe, safe->context_, message);
                    return;
                }
                // A Live target keeps reading its ring: the capture doesn't switch its Source,
                // so say how to hear it, as a drop does. A Sample target says it took it.
                const std::string letter(1, kLayerLetters[target]);
                postNotice(*safe, safe->context_,
                           live ? letter + "'s sample is loading: set " + letter +
                                      " Source to Sample to play it"
                                : "Captured into " + letter);
            });
    }
    menu.addSubMenu("Capture to layer", targets, ring.valid);
    menu.addItem(juce::PopupMenu::Item(withEllipsis("Save capture as"))
                     .setEnabled(ring.valid)
                     .setAction([safe, layer, splice] {
                         if (safe != nullptr)
                         {
                             safe->chooseCaptureFileFor(layer, splice);
                         }
                     }));
    return menu;
}

void GrainCanvas::chooseSampleFor(uint8_t layer)
{
    const juce::String last =
        context_.lastLoadFolder ? juce::String::fromUTF8(context_.lastLoadFolder().c_str()) : "";
    const juce::File initial = juce::File::isAbsolutePath(last) ? juce::File(last) : juce::File();
    const juce::Component::SafePointer<GrainCanvas> safe(this);
    browse({"Load a sample", initial, false, kSampleFileChooserPatterns},
           [safe, layer](const juce::File &file) {
               if (safe != nullptr && file != juce::File() && safe->context_.loadSampleFor)
               {
                   safe->context_.loadSampleFor(layer, file.getFullPathName().toStdString());
               }
           });
}

void GrainCanvas::chooseCaptureFileFor(uint8_t layer, uint32_t splice)
{
    const juce::String folder =
        context_.capturesFolder ? juce::String::fromUTF8(context_.capturesFolder().c_str()) : "";
    const juce::File initial =
        juce::File::isAbsolutePath(folder) ? juce::File(folder) : juce::File();
    const juce::Component::SafePointer<GrainCanvas> safe(this);
    browse({"Save the capture as", initial, true, "*.wav"}, [safe, layer,
                                                             splice](const juce::File &chosen) {
        if (safe == nullptr || chosen == juce::File())
        {
            return;
        }
        const juce::File file =
            chosen.hasFileExtension("wav") ? chosen : chosen.withFileExtension("wav");
        const std::string message =
            safe->context_.saveCaptureTo
                ? safe->context_.saveCaptureTo(layer, splice, file.getFullPathName().toStdString())
                : std::string("can't save the capture");
        if (!message.empty())
        {
            postNotice(*safe, safe->context_, message);
        }
    });
}
} // namespace gg::ui
