// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_FACTORY_H
#define GG_PLUGIN_FACTORY_H

namespace gg::plugin
{
/// The three functions behind the CLAP entry point (clients/clap-first/gg-clap-entry.cpp,
/// which declares them itself). getFactory answers CLAP_PLUGIN_FACTORY_ID (both plugins),
/// CLAP_PLUGIN_FACTORY_INFO_AUV2 (their AudioUnit identities) and
/// CLAP_PLUGIN_FACTORY_INFO_AUV2_LEGACY (the FX's extra `aumf` identity), and nullptr for
/// anything else.
const void *getFactory(const char *factoryId);
bool clapInit(const char *pluginPath);
void clapDeinit();
} // namespace gg::plugin

#endif // GG_PLUGIN_FACTORY_H
