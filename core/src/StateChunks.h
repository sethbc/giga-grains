// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_STATECHUNKS_H
#define GG_CORE_STATECHUNKS_H

// Internal to gg-core: the walk over a session state's container (gg/core/SessionState.h) that
// decodeState and presetInfoOf share, so both find PTCH and MEDI the same way.

#include <string_view>

namespace gg::core
{
/// Finds a container's PTCH and MEDI payloads (views into `bytes`): false, and both untouched,
/// for a bad magic, any container version but kStateContainerVersion, a chunk header or payload
/// running past the end, or a missing or repeated PTCH or MEDI. Chunks with other IDs are
/// skipped. Neither payload is parsed.
bool findStateChunks(std::string_view bytes, std::string_view &patch, std::string_view &media);
} // namespace gg::core

#endif // GG_CORE_STATECHUNKS_H
