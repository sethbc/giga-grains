// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "HelpersConfig.h"

#include <type_traits>

#include <clap/helpers/host-proxy.hxx>
#include <clap/helpers/plugin.hxx>

// The one explicit instantiation of the clap-helpers templates for this configuration.
template class clap::helpers::Plugin<gg::plugin::misLevel, gg::plugin::checkLevel>;
template class clap::helpers::HostProxy<gg::plugin::misLevel, gg::plugin::checkLevel>;

static_assert(std::is_same_v<gg::plugin::plugHelper_t,
                             clap::helpers::Plugin<gg::plugin::misLevel, gg::plugin::checkLevel>>);
