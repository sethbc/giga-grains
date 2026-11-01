// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_STATECHUNKS_H
#define GG_CORE_STATECHUNKS_H

// Internal to gg-core: the walk over a session state's container (gg/core/SessionState.h) that
// decodeState and presetInfoOf share, so both find PTCH and MEDI the same way.

#include <optional>
#include <string_view>

namespace gg::core
{
/// Finds a container's PTCH and MEDI payloads (views into `bytes`) and, where `audio` and
/// `instance` aren't null, its AUDI and INST payloads, empty when absent: false, and all of them
/// untouched, for a bad magic, any container version but kStateContainerVersion, a chunk header
/// or payload running past the end, a missing PTCH or MEDI, or a repeated PTCH, MEDI, AUDI or
/// INST. Chunks with other IDs are skipped. No payload is parsed.
bool findStateChunks(std::string_view bytes, std::string_view &patch, std::string_view &media,
                     std::optional<std::string_view> *audio = nullptr,
                     std::optional<std::string_view> *instance = nullptr);
} // namespace gg::core

#endif // GG_CORE_STATECHUNKS_H
