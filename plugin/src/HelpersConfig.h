// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_HELPERSCONFIG_H
#define GG_PLUGIN_HELPERSCONFIG_H

#include <clap/helpers/plugin.hh>

namespace gg::plugin
{
// Log host misbehaviour instead of terminating: a plugin must not take the host down with it.
inline constexpr clap::helpers::MisbehaviourHandler misLevel =
    clap::helpers::MisbehaviourHandler::Ignore;

// Debug builds check everything the host does (Maximal). Release-type builds, meaning the
// shipped products and the rtsan preset, check at Minimal (review finding I1, ruling T8-e).
// Maximal reports each non-parameter event in a params flush by building a std::ostringstream
// and handing it to the host's log. clap-wrapper's VST3 puts notes and MIDI in flushes, and
// its host log allocates a std::string and writes stdio (fprintf, OutputDebugStringA), all on
// the audio thread. BUILD_IS_DEBUG / BUILD_IS_RELEASE come from gg::compiler-options. Every
// file that includes this header must see one of them, so plugHelper_t is the same type
// everywhere. The template is instantiated once, in HelpersConfig.cpp.
//
// Minimal, not None, in release-type builds (decided in M1, carry-over [236]). At Minimal,
// clap-helpers still reports real host misbehaviour on the audio thread (process() while
// inactive or not processing, a call on the wrong thread) through the host's log, or stderr
// when the host has none; the wrong-thread report also builds a std::ostringstream. So it can
// allocate and do I/O there, but only once the host has already broken the CLAP contract, and
// the report is how that gets diagnosed, so it stays. What a well-behaved host drives is
// covered: the rtsan preset is RelWithDebInfo, so it checks at Minimal, and it builds the plugin
// core and runs the [plugin][rt] tests (process and params.flush, with notes) under
// RealtimeSanitizer and the allocation tripwire, through the clap_plugin vtables.
#if defined(BUILD_IS_DEBUG)
inline constexpr clap::helpers::CheckingLevel checkLevel = clap::helpers::CheckingLevel::Maximal;
#elif defined(BUILD_IS_RELEASE)
inline constexpr clap::helpers::CheckingLevel checkLevel = clap::helpers::CheckingLevel::Minimal;
#else
#error "HelpersConfig.h needs BUILD_IS_DEBUG or BUILD_IS_RELEASE: link gg::compiler-options"
#endif

using plugHelper_t = clap::helpers::Plugin<misLevel, checkLevel>;
} // namespace gg::plugin

#endif // GG_PLUGIN_HELPERSCONFIG_H
