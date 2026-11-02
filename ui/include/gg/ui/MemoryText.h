// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_MEMORYTEXT_H
#define GG_UI_MEMORYTEXT_H

#include <cstdint>
#include <span>
#include <string>

namespace gg::ui
{
/// The sample memory readout, "312 MB of 1 GB" (M2 sub-project 6's spec §6.3).
///
/// The units are those of gg::media::MemoryBudget::refusal ("needs N MB, F MB free of C MB"): a
/// MB is a MiB (1,048,576 bytes), and a byte count is rounded up to whole MB, as the refusal
/// rounds what it needs and the cap. From 1,024 MB the figure is in GB (1,024 MB each) with one
/// decimal, left off when it is whole ("1 GB", "1.5 GB").
std::string formatMemory(uint64_t usedBytes, uint32_t capMiB);

/// One size alone, "512 MB" or "1.5 GB", as the readout writes each half (the cap menu's items).
std::string formatMemorySize(uint64_t mib);

/// The cap menu's choices, ascending, in MiB: 64, 128, 256, 512, then 1, 2, 4, 8 and 16 GB.
std::span<const uint32_t> memoryCapChoicesMiB() noexcept;
} // namespace gg::ui

#endif // GG_UI_MEMORYTEXT_H
