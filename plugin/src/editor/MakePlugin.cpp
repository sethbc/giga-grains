// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The factory's plugin instances when the editor is built (GG_BUILD_EDITOR=ON): the core with
// its JUCE editor attached.

#include "EditorPlugin.h"

namespace gg::plugin
{
const clap_plugin *makePlugin(const clap_host *host, Product product)
{
    return (new EditorPlugin(host, product))->clapPlugin();
}
} // namespace gg::plugin
