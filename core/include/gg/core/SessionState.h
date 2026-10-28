// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_SESSIONSTATE_H
#define GG_CORE_SESSIONSTATE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "gg/core/ParamIds.h"

/// The session state both products save (INTENT §18, spec §6.6): a versioned container of
/// chunks, holding the patch (gg/core/PatchState.h) and the media manifest. Like the patch, the
/// manifest is XML that gg-core writes and reads with tinyxml, private to gg-core's sources
/// (ruling M1-l). Main thread only: both functions allocate.
///
/// The layout, every integer little-endian:
///
///     "GGST"                      the magic, 4 bytes
///     u32  version                the container version, kStateContainerVersion
///     then chunks, each:
///       4 bytes  id               four characters
///       u64      length           the payload's length in bytes
///       payload
///
/// encodeState writes two chunks, in this order:
/// - `PTCH`: the patch, toStateXml's bytes unchanged;
/// - `MEDI`: the media manifest, on one line (broken here for reading), with zero or one
///   <sample> (one per loaded sample; M1 loads at most one):
///
///       <media version="1"><sample sha256="e3b0…b855" size="1234" path="/Samples/a.wav"
///         relative="a.wav" format="wav" channels="2" rate="40e7700000000000" frames="48000"
///         embed="0" /></media>
///
///   and `<media version="1" />` with none. The attributes are MediaEntry's fields, in its order:
///   `size`, `channels` and `frames` in decimal; `rate` the double's bits as 16 hex digits, so it
///   comes back bit for bit (as `bits` does for PTCH's floats); `embed` 0 or 1; `path`,
///   `relative` and `format` as UTF-8 text, every `&` in them written `&#x26;` (tinyxml would
///   otherwise read a literal `&#x41;` back as `A`).
///
/// `AUDI` (embedded audio) is reserved for M2 and not written.
namespace gg::core
{
/// The container's version: the u32 after the magic.
inline constexpr uint32_t kStateContainerVersion = 1;

/// The most a state may hold, in bytes: the plugins read no further (carry-over T7 218). Above
/// M2's 32 MB embed limit (INTENT §18).
inline constexpr std::size_t kMaxStateBytes = std::size_t{64} << 20;

/// One sample in the media manifest: the file the user loaded, as it was when loaded.
struct MediaEntry
{
    std::string sha256Hex; // 64 lowercase hex digits
    uint64_t fileSize{0};
    std::string absolutePath; // UTF-8
    std::string relativePath; // UTF-8, to the sample folder; empty when outside it
    std::string format;       // "wav", "aiff", "flac", "mp3"
    uint32_t channels{0};     // as stored in the file
    double sampleRate{0.0};   // the file's
    int64_t frames{0};        // the file's
    bool embed{false};        // always false until M2
    friend bool operator==(const MediaEntry &, const MediaEntry &) = default;
};

/// What a session state holds: the parameter values and the engine seed (PTCH), and the sample
/// the manifest lists, if any (MEDI).
struct SessionState
{
    ParamValues values{};
    uint64_t seed{0};
    std::optional<MediaEntry> sample;
};

/// The container of `state`: PTCH, then MEDI.
std::string encodeState(const SessionState &state);

/// Loads a session state into `mode`'s product. All or nothing; on false `out` is untouched.
/// - Bytes that start with `<` are a pre-container state, saved by sub-projects 1 and 2: their
///   whole content is PTCH, and there is no sample.
/// - Otherwise they must be a container: the magic, version 1, and exactly one PTCH and one MEDI
///   among its chunks, in any order. Chunks with other IDs are skipped, so a newer build's
///   chunks don't break this one.
/// - Returns false for a bad magic, any other container version, a chunk header or payload
///   running past the end, a missing or repeated PTCH or MEDI, or a PTCH or MEDI that fails to
///   parse:
///   - PTCH loads as fromStateXml loads it (gg/core/PatchState.h): its version migrates, a
///     param it doesn't mention takes `mode`'s default, and values are clamped;
///   - MEDI must be well-formed XML with a <media> root carrying an integer `version`, and at
///     most one <sample>, with every attribute above present and well-formed (`sha256` 64
///     lowercase hex digits, `rate` 16 hex digits, `embed` 0 or 1). Other elements and
///     attributes are ignored. A payload holding a NUL byte fails, PTCH's too.
/// - `migratedFromVersion`, if given, is written on success only, as fromStateXml writes it.
bool decodeState(std::string_view bytes, Mode mode, SessionState &out,
                 uint32_t *migratedFromVersion = nullptr);
} // namespace gg::core

#endif // GG_CORE_SESSIONSTATE_H
