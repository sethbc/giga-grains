// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_MEDIAVIEWS_H
#define GG_UI_MEDIAVIEWS_H

#include <array>
#include <cstdint>

#include "gg/core/ParamIds.h" // kMaxSplices

namespace gg::ui
{
/// A Live layer's ring, as the audio thread last published it (Processor::ringMeta): the
/// editor's view of it, with no JUCE in the header so the lane maths can include it.
struct RingView
{
    bool valid{false}; ///< false while the plugin is inactive
    int64_t head{0}, windowFrames{0};
    uint32_t spliceCount{0};
    std::array<int64_t, gg::core::kMaxSplices> splices{}; ///< ascending ring frames
    bool frozen{false};
};

/// What the instance holds against its memory cap (MediaSession::memoryUsedBytes), and the cap.
struct MemoryView
{
    uint64_t usedBytes{0};
    uint32_t capMiB{0};
};

/// A ring command of the editor's (the Processor's CommandKind AddSplice and ClearSplices).
enum class RingCommand : uint8_t
{
    AddSplice,
    ClearSplices
};
} // namespace gg::ui

#endif // GG_UI_MEDIAVIEWS_H
