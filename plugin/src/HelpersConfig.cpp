// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "HelpersConfig.h"

#include <type_traits>

#include <clap/helpers/host-proxy.hxx>
#include <clap/helpers/plugin.hxx>

// The one explicit instantiation of the clap-helpers templates for this configuration, on the
// same switch as HelpersConfig.h: Maximal checking in Debug builds, Minimal in release-type ones.
#if defined(BUILD_IS_DEBUG)
template class clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                     clap::helpers::CheckingLevel::Maximal>;
template class clap::helpers::HostProxy<clap::helpers::MisbehaviourHandler::Ignore,
                                        clap::helpers::CheckingLevel::Maximal>;
static_assert(std::is_same_v<gg::plugin::plugHelper_t,
                             clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                                   clap::helpers::CheckingLevel::Maximal>>);
#else
template class clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                     clap::helpers::CheckingLevel::Minimal>;
template class clap::helpers::HostProxy<clap::helpers::MisbehaviourHandler::Ignore,
                                        clap::helpers::CheckingLevel::Minimal>;
static_assert(std::is_same_v<gg::plugin::plugHelper_t,
                             clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                                   clap::helpers::CheckingLevel::Minimal>>);
#endif
