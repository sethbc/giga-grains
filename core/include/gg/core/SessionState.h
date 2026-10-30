// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_SESSIONSTATE_H
#define GG_CORE_SESSIONSTATE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"

/// The session state both products save (INTENT §18, M1's spec §6.6, M2 sub-project 2's spec
/// §6.6): a versioned container of chunks, holding the patch (gg/core/PatchState.h), the media
/// manifest, the embedded audio and the per-instance settings. Like the patch, the manifest and
/// the settings are XML that gg-core writes and reads with tinyxml, private to gg-core's sources
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
/// encodeState writes three or four chunks, in this order:
/// - `PTCH`: the patch, toStateXml's bytes unchanged: the values, the seed and, since patch
///   version 2, the mod matrix's routes in its `<mod>` element (gg/core/PatchState.h);
/// - `MEDI`: the media manifest, version 2, on one line (broken here for reading), with at most
///   one <sample> and at most one <ring> per layer, the samples first, each in layer order:
///
///       <media version="2"><sample layer="A" sha256="e3b0…b855" size="1234"
///         path="/Samples/a.wav" relative="a.wav" format="wav" channels="2"
///         rate="40e7700000000000" frames="48000" embed="0" content="Drums/a.wav" audio="0" />
///         <ring layer="B" frames="96000" rate="40e7700000000000" splices="12000 24000"
///         sha256="5d41…" size="768068" embed="1" audio="1" /></media>
///
///   and `<media version="2" />` with none. The attributes are the entries' fields, in their
///   order, `layer` its letter, A to D. A <sample> has version 1's attributes (below), then
///   `content`, the content-relative path, and `audio`, its AUDI payload's id, written "0"
///   unless `embed` is 1. A <ring> has no `channels` (a ring is stereo) and no `head` or `loop`
///   (the layer's parameters carry them); `splices` is its marks in decimal, one space between
///   each, "" with none; embedded (`embed="1"`) it has `audio` and no `path`, filed
///   (`embed="0"`) it has `path` and no `audio`.
///   Version 1, which M1 and M2 sub-project 1 wrote, held zero or one <sample>, layer A's:
///
///       <media version="1"><sample sha256="e3b0…b855" size="1234" path="/Samples/a.wav"
///         relative="a.wav" format="wav" channels="2" rate="40e7700000000000" frames="48000"
///         embed="0" /></media>
///
///   `size`, `channels`, `frames` and `audio` in decimal; `rate` the double's bits as 16 hex
///   digits, so it comes back bit for bit (as `bits` does for PTCH's floats); `embed` 0 or 1
///   (always 0 in version 1); `path`, `relative`, `content` and `format` as their bytes, every
///   `&` written `&#x26;` (tinyxml would otherwise read a literal `&#x41;` back as `A`). They are
///   meant to be UTF-8, but any bytes but NUL come back unchanged, so a path that isn't valid
///   UTF-8 is still kept.
/// - `AUDI`, only when `audio` holds a payload: the embedded audio, binary:
///
///       u32  count
///       then `count` payloads, each:
///         u32  id                 above 0, unique in the chunk
///         u64  length
///         the bytes               a complete float32 WAV file (gg/media/WavBytes.h)
///
///   An embedded <sample> or <ring> names its payload by `audio`.
/// - `INST`: the per-instance settings, `<instance version="1" memoryCapMiB="1024" />`.
///
/// An older build skips AUDI and INST as unknown chunks, and reads a version-2 MEDI as a newer
/// manifest (no sample, its bytes kept).
namespace gg::core
{
/// The container's version: the u32 after the magic.
inline constexpr uint32_t kStateContainerVersion = 1;

/// The most a state may hold, in bytes: the plugins read no further (carry-over T7 218). Above
/// the 32 MB embed limit, kEmbedCapBytes (INTENT §18).
inline constexpr std::size_t kMaxStateBytes = std::size_t{64} << 20;

/// The most embedded audio a session holds, in bytes: AUDI's payloads together (INTENT §18's
/// 32 MB; spec §6.6's embed rule). The saver keeps to it; decodeState doesn't check it.
inline constexpr std::size_t kEmbedCapBytes = std::size_t{32} << 20;

/// The per-instance memory cap (INST's `memoryCapMiB`), in MiB: its default, floor and ceiling.
inline constexpr uint32_t kMemoryCapDefaultMiB = 1024;
inline constexpr uint32_t kMemoryCapMinMiB = 64;
inline constexpr uint32_t kMemoryCapMaxMiB = 16384;

/// One sample in the media manifest: the file the user loaded, as it was when loaded, or a
/// capture, embedded in AUDI or filed.
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
    bool embed{false};        // its audio is AUDI's payload `audioId`
    uint8_t layer{0};         // 0 to 3; a version-1 entry is 0's
    std::string contentPath;  // UTF-8, inside a content root, `/` separators; "" when none
    uint32_t audioId{0};      // the AUDI payload when `embed`; 0 otherwise
    friend bool operator==(const MediaEntry &, const MediaEntry &) = default;
};

/// One saved ring in the media manifest: the window, oldest frame first, as float32 WAV bytes
/// (gg/media/WavBytes.h), embedded in AUDI or filed in a captures folder.
struct RingEntry
{
    uint8_t layer{0};             // 0 to 3
    int64_t frames{0};            // the window's
    double sampleRate{0.0};       // the host rate it was written at
    uint32_t channels{2};         // a ring is stereo
    std::vector<int64_t> splices; // frames from the window's start, ascending, below `frames`
    std::string sha256Hex;        // 64 lowercase hex digits, of the WAV bytes
    uint64_t fileSize{0};         // the WAV bytes' length
    bool embed{false};            // in AUDI, as `audioId`; else filed at `absolutePath`
    uint32_t audioId{0};          // the AUDI payload when `embed`; 0 otherwise
    std::string absolutePath;     // UTF-8, the file in a captures folder; "" when embedded
    friend bool operator==(const RingEntry &, const RingEntry &) = default;
};

/// One embedded payload (AUDI): a complete float32 WAV file's bytes, named by `id`.
struct AudioPayload
{
    uint32_t id{0}; // above 0
    std::string bytes;
    friend bool operator==(const AudioPayload &, const AudioPayload &) = default;
};

/// What a session state holds: the parameter values, the engine seed and the mod matrix's routes
/// (PTCH), the samples and rings the manifest lists, by layer (MEDI), the payloads they embed
/// (AUDI) and the per-instance settings (INST).
struct SessionState
{
    ParamValues values{};
    uint64_t seed{0};
    /// The manifest's samples, by layer: samples[k], if any, is layer k's, its `layer` k.
    std::array<std::optional<MediaEntry>, kLayers> samples{};
    /// The manifest's rings, by layer: rings[k], if any, is layer k's, its `layer` k.
    std::array<std::optional<RingEntry>, kLayers> rings{};
    /// The embedded payloads, ids unique and above 0. decodeState keeps those an entry names, in
    /// AUDI's order.
    std::vector<AudioPayload> audio;
    /// The memory cap, kMemoryCapMinMiB to kMemoryCapMaxMiB (INST).
    uint32_t memoryCapMiB{kMemoryCapDefaultMiB};
    /// A manifest of a newer version than this build reads (its `version` above 2): the MEDI
    /// payload byte for byte, kept so that saving the session writes it back unchanged. The
    /// build can't read its entries, so `samples` and `rings` are then empty (INTENT §18).
    std::optional<std::string> newerMedia;
    /// The mod matrix's routes (gg/core/ModMatrix.h), every slot empty by default. A state of
    /// patch version 1 (M1's) decodes with factoryRoutes(mode) (gg/core/PatchState.h).
    ModRoutes routes{};
};

/// Whether `entry` is a sample encodeState can write: `sha256Hex` is 64 lowercase hex digits, no
/// string holds a NUL byte, `frames` isn't negative, `layer` is below kLayers, and `audioId` is
/// above 0 when `embed` and 0 otherwise. encodeState also needs an embedded entry's payload in
/// `audio` and the entry in its own layer's slot. Every such entry decodes back equal to itself
/// (but for a NaN sample rate, which comes back with the same bits).
bool isValidMediaEntry(const MediaEntry &entry) noexcept;

/// Whether `entry` is a ring encodeState can write: `layer` below kLayers, `frames` not
/// negative, `channels` 2, `splices` strictly ascending from 0 and below `frames`, `sha256Hex` 64
/// lowercase hex digits, and either `embed` with `audioId` above 0 and no path, or not `embed`
/// with `audioId` 0 and a path holding no NUL byte. encodeState also needs an embedded entry's
/// payload in `audio` and the entry in its own layer's slot. Every such entry decodes back equal
/// to itself (the rate by its bits).
bool isValidRingEntry(const RingEntry &entry) noexcept;

/// The container of `state`: PTCH, MEDI (version 2), then AUDI when `state.audio` is non-empty,
/// then INST. decodeState loads whatever it writes.
/// - Each entry in `state.samples` and `state.rings` must be isValidMediaEntry or
///   isValidRingEntry, sit in its own layer's slot and, when embedded, name a payload in
///   `state.audio`; each payload's id must be above 0 and unique. Debug builds assert all of it.
/// - In release builds, an entry or payload that isn't is left out, and the rest is written: a
///   state that fails to load would lose the whole session, where this loses one reference
///   (ruling R9). A payload no entry names is still written.
/// - `state.memoryCapMiB` is written clamped to kMemoryCapMinMiB to kMemoryCapMaxMiB.
/// - With no sample and no ring, `state.newerMedia`, if any, is MEDI, byte for byte: the newer
///   manifest decodeState kept. Only one decodeState would keep again is written; any other is
///   left out, and MEDI is written empty. An entry takes its place: loading one replaces the
///   manifest.
std::string encodeState(const SessionState &state);

/// Loads a session state into `mode`'s product. All or nothing; on false `out` is untouched.
/// - Bytes that start with `<` are a pre-container state, saved by sub-projects 1 and 2: their
///   whole content is PTCH, there is no sample, and they load exactly as fromStateXml loads
///   them.
/// - Otherwise they must be a container: the magic, version 1, exactly one PTCH and one MEDI and
///   at most one AUDI and one INST among its chunks, in any order. Chunks with other IDs are
///   skipped, so a newer build's chunks don't break this one.
/// - Returns false for a bad magic, any other container version, a chunk header or payload
///   running past the end, a missing PTCH or MEDI, a repeated PTCH, MEDI, AUDI or INST, or one of
///   them that fails to parse:
///   - PTCH loads as fromStateXml loads it (gg/core/PatchState.h): its version migrates (a
///     version-1 state takes the factory routes), a param it doesn't mention takes `mode`'s
///     default, values are clamped, and the routes are its `<mod>`'s;
///   - MEDI must be well-formed XML whose only top-level node is a <media> root, with an
///     integer `version` from 1. Other elements and attributes inside <media> are ignored.
///     - Version 1 has at most one <sample>, layer A's, with every attribute of version 1
///       present and well-formed (`sha256` 64 lowercase hex digits, `rate` 16 hex digits,
///       `frames` not negative, `embed` 0).
///     - Version 2 has at most one <sample> and one <ring> per layer, in any order, each with a
///       `layer` of A to D. A <sample> has version 1's attributes, `embed` 0 or 1, then
///       `content` and `audio` (0 unless `embed` is 1, and then above 0). A <ring> has `frames`
///       (not negative), `rate`, `splices` (as written: strictly ascending, below `frames`),
///       `sha256`, `size`, `embed` 0 or 1, and `audio` above 0 when embedded or `path` when not.
///     - An embedded entry whose `audio` isn't among AUDI's payloads fails the state.
///     - A payload holding a NUL byte fails, PTCH's and INST's too.
///   - AUDI must be exactly its count's payloads, each id above 0 and unique; a payload no entry
///     names is ignored (not kept in `out.audio`).
///   - INST must be well-formed XML whose only top-level node is an <instance> root with an
///     integer `version` from 1; a newer version is read the same way. Its `memoryCapMiB`, an
///     integer, is clamped to kMemoryCapMinMiB to kMemoryCapMaxMiB; absent or not an integer it
///     is kMemoryCapDefaultMiB, as it is with no INST.
///   - PTCH, MEDI and INST each fail when their XML nests elements more than 64 deep (as tinyxml
///     reads it), or can't be read safely, before tinyxml parses it.
///   - A MEDI of a newer version, well-formed with its <media> root alone at the top level and
///     an integer `version` above this build's, loads with no sample and no ring: `out.newerMedia`
///     keeps its bytes, so a save writes them back unchanged, and nothing inside <media> is read
///     (the director's decision of 2026-10-28, INTENT §18). Where a newer PTCH migrates, a newer
///     MEDI can't: this build doesn't know what its sample needs. A future MEDI must still
///     pass the pre-scan above (at most 64 deep, no <? after the first node, quoted
///     attributes, well-formed &#...; references), or this build fails the whole state.
/// - `bytes` is whatever the host handed over: the caller caps its length (the plugin reads at
///   most kMaxStateBytes), and decodeState works on all of it.
/// - `migratedFromVersion`, if given, is written on success only, as fromStateXml writes it.
bool decodeState(std::string_view bytes, Mode mode, SessionState &out,
                 uint32_t *migratedFromVersion = nullptr);
} // namespace gg::core

#endif // GG_CORE_SESSIONSTATE_H
