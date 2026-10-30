// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_PRESETDISCOVERY_H
#define GG_PLUGIN_PRESETDISCOVERY_H

#include <filesystem>
#include <vector>

#include <clap/factory/preset-discovery.h>

#include "gg/core/MachineSettings.h"

/// CLAP preset discovery (M2 sub-project 5's spec §6.3): the factory getFactory answers for
/// CLAP_PRESET_DISCOVERY_FACTORY_ID and its _COMPAT id, its one provider, and the preset folders
/// the provider declares, which the browser's catalog lists too.
namespace gg::plugin
{
/// One folder of preset files.
struct PresetLocation
{
    /// The folder, which may not exist yet (the user's is created on the first save).
    std::filesystem::path folder;
    /// Factory content (a content root's Presets), or the user's presets.
    bool factory;
    /// The content root a factory folder is in; empty for the user's.
    std::filesystem::path root;
};

/// The preset folders, in order: for each content root (gg::media::contentRoots(settings)) whose
/// Presets folder exists, a factory location; then the user presets folder
/// (gg::media::userPresetsFolder(settingsFile)), whether or not it exists, unless settingsFile is
/// empty. Reads the environment and the filesystem, and allocates: main thread or a worker.
std::vector<PresetLocation> presetLocations(const core::MachineSettings &settings,
                                            const std::filesystem::path &settingsFile);

/// The preset-discovery factory: one provider, `org.sethbc.giga-grains.presets`, "Giga Grains
/// presets", vendor `sethbc`, serving both products. Static storage, never null.
///
/// The provider's init (on the indexer's thread; it has no plugin instance) reads the settings
/// file (settingsFile(), so GG_SETTINGS_FILE is honoured) and declares the `ggpreset` filetype,
/// a FILE location per presetLocations entry whose folder exists (IS_FACTORY_CONTENT or
/// IS_USER_CONTENT; the user's is created on the first save, and an indexer may count a missing
/// location as an error, as clap-validator's crawl does), one
/// PLUGIN location, "Init", and the soundpack `org.sethbc.giga-grains.factory`, whose
/// description is the first line of the first factory location's root's content-version.txt
/// (the soundpack struct has no version field), empty when there is none. get_metadata reports a
/// preset file's metadata (gg::core::presetInfoOf), or for the PLUGIN location the INIT preset
/// (load key gg::core::kInitLoadKey), one preset naming both products' ids.
const clap_preset_discovery_factory *presetDiscoveryFactory() noexcept;
} // namespace gg::plugin

#endif // GG_PLUGIN_PRESETDISCOVERY_H
