// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_IDENTITY_H
#define GG_PLUGIN_IDENTITY_H

/// The plugin identities (INTENT §18). Hosts store these in every saved session, so they are
/// frozen at the first public release: changing any value orphans users' sessions.
/// clients/clap-first/CMakeLists.txt repeats the CLAP ids as bundle ids, and the standalone apps'
/// bundle ids are "<CLAP id>.standalone". INTENT §18 also lists the identities derived from
/// these: the VST3 class UIDs and the standalone bundle ids.
namespace gg::plugin::identity
{
inline constexpr char kVendor[] = "sethbc";
inline constexpr char kUrl[] = "https://github.com/sethbc/giga-grains";

inline constexpr char kInstrumentId[] = "org.sethbc.giga-grains";
inline constexpr char kInstrumentName[] = "Giga Grains";
inline constexpr char kFxId[] = "org.sethbc.giga-grains-fx";
inline constexpr char kFxName[] = "Giga Grains FX";

/// AudioUnit identities are (type, subtype, manufacturer) triples of four-character codes.
inline constexpr char kAuManufacturer[] = "Sbcx";
inline constexpr char kAuManufacturerName[] = "sethbc";
inline constexpr char kAuInstrumentType[] = "aumu";
inline constexpr char kAuInstrumentSubtype[] = "GGin";
/// The FX registers twice: as an audio effect for ordinary inserts, and as a MIDI-controlled
/// effect (an extra identity through clap-wrapper's legacy-identity factory). Logic offers an
/// `aumf` only in instrument slots, with the audio arriving through the side-chain.
inline constexpr char kAuFxType[] = "aufx";
inline constexpr char kAuFxExtraType[] = "aumf";
inline constexpr char kAuFxSubtype[] = "GGfx";

static_assert(sizeof(kAuManufacturer) == 5 && sizeof(kAuInstrumentType) == 5 &&
                  sizeof(kAuInstrumentSubtype) == 5 && sizeof(kAuFxType) == 5 &&
                  sizeof(kAuFxExtraType) == 5 && sizeof(kAuFxSubtype) == 5,
              "AudioUnit codes are exactly four characters");
} // namespace gg::plugin::identity

#endif // GG_PLUGIN_IDENTITY_H
