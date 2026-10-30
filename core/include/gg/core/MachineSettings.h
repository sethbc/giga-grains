// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_MACHINESETTINGS_H
#define GG_CORE_MACHINESETTINGS_H

#include <string>
#include <string_view>

/// The per-machine settings (spec §6.7): the folders a missing sample is searched in, and the
/// captures folder (M2 sub-project 2's spec §6.6), where rings and captures too large to embed in
/// a session are filed. They live
/// in a small XML file in the user's configuration folder, which gg-media reads and writes
/// (gg/media/SettingsFile.h); gg-core only encodes them, with tinyxml, private to gg-core's
/// sources (ruling M1-l). The codec is pure, with no state, and may be called from any thread; it
/// allocates, so never from the audio thread. The settings file itself is main thread only
/// (ruling R16).
///
/// The XML, on one line:
///
///     <settings version="1" sampleFolder="/Users/me/Samples" lastLoadFolder="/Users/me/Kicks"
///               capturesFolder="" />
///
/// Each attribute holds its string's bytes, every `&` written `&#x26;`, as MEDI's paths are
/// (gg/core/SessionState.h): any bytes but NUL come back unchanged.
namespace gg::core
{
/// The settings' version: the `version` attribute toSettingsXml writes.
inline constexpr int kMachineSettingsVersion = 1;

/// The settings. All three are UTF-8 folder paths, empty when unset.
struct MachineSettings
{
    std::string sampleFolder;   // set once by the user: the root of their sample library
    std::string lastLoadFolder; // the folder of the last file loaded
    // Where captures and rings are filed; empty means gg::media::defaultCapturesFolder() (the
    // plugin substitutes it). A file from before it reads empty, so the version stays 1. Its
    // default member initializer keeps `MachineSettings{sample, last}` free of
    // -Wmissing-field-initializers (Clang and GCC alike).
    std::string capturesFolder{};
    friend bool operator==(const MachineSettings &, const MachineSettings &) = default;
};

/// The settings as XML. No string may hold a NUL byte (debug builds assert it); in release
/// builds a string is written up to its first NUL.
std::string toSettingsXml(const MachineSettings &settings);

/// Reads XML that toSettingsXml wrote into `out`; false, with `out` untouched, if it isn't
/// settings: XML that isn't well-formed, holds a NUL byte, or whose top level holds anything but
/// one <settings> element, with an integer `version` of 1 or more, and any XML declarations and
/// comments (so a file edited by hand still loads; a UTF-8 byte order mark is skipped). A missing
/// attribute reads as empty, and other attributes and elements are ignored, so a newer build's
/// file still gives this one its folders.
bool fromSettingsXml(std::string_view xml, MachineSettings &out);
} // namespace gg::core

#endif // GG_CORE_MACHINESETTINGS_H
