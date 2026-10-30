// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_EDITOR_UISETTINGS_H
#define GG_PLUGIN_EDITOR_UISETTINGS_H

#include <cstdint>

/// The editor's per-machine settings (M2 sub-project 6's spec §6.9): the theme, kept in the
/// settings file beside the loader's folders (MachineSettings::theme). Both products
/// read it; the FX reads the file for this alone. The file is $GG_SETTINGS_FILE when that is set
/// and not empty (the tests' way to keep away from the user's file), else
/// gg::media::defaultSettingsPath(), as GigaGrainsPlugin finds it, but looked up on every call.
/// Main thread only (ruling R16); never the audio thread.
namespace gg::plugin
{
/// The editor's two themes. The lanes stay dark in both (spec §6.9, D9).
enum class ThemeName : uint8_t
{
    Dark,
    Light
};

/// The theme the settings file holds: Light for "light", Dark for anything else, a missing or
/// unreadable file included.
ThemeName readTheme();

/// Writes `theme` ("dark" or "light") into the settings file: a read-modify-write of the whole
/// file, as the sample loader saves its folders, so the folders survive. False, never throwing,
/// if the file couldn't be saved (gg::media::saveSettings), or no settings file can be found.
bool writeTheme(ThemeName theme);
} // namespace gg::plugin

#endif // GG_PLUGIN_EDITOR_UISETTINGS_H
