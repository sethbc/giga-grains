// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_HELPERSCONFIG_H
#define GG_PLUGIN_HELPERSCONFIG_H

#include <clap/helpers/plugin.hh>

namespace gg::plugin
{
// Check everything the host does, but log misbehaviour instead of terminating: a plugin must
// not take the host down with it. The template is instantiated once, in HelpersConfig.cpp.
inline constexpr clap::helpers::MisbehaviourHandler misLevel =
    clap::helpers::MisbehaviourHandler::Ignore;
inline constexpr clap::helpers::CheckingLevel checkLevel = clap::helpers::CheckingLevel::Maximal;

using plugHelper_t = clap::helpers::Plugin<misLevel, checkLevel>;
} // namespace gg::plugin

#endif // GG_PLUGIN_HELPERSCONFIG_H
