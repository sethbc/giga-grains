// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The factory's plugin instances when the editor isn't built (GG_BUILD_EDITOR=OFF): the
// JUCE-free core alone, which offers no gui extension.

#include "GigaGrainsPlugin.h"

namespace gg::plugin
{
const clap_plugin *makePlugin(const clap_host *host, Product product)
{
    return (new GigaGrainsPlugin(host, product))->clapPlugin();
}
} // namespace gg::plugin
