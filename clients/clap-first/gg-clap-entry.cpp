// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The CLAP entry point. make_clapfirst_plugins compiles this file into every format (the
// .clap exports it; the VST3, AUv2 and standalone wrappers link it statically), and the
// functions it names live in gg-plugin-impl (plugin/src/Factory.h).

#include <cstdio>

#include <clap/clap.h>

namespace gg::plugin
{
extern const void *getFactory(const char *factoryId);
extern bool clapInit(const char *pluginPath);
extern void clapDeinit();
} // namespace gg::plugin

extern "C"
{
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes" // CLAP_EXPORT on a const object
#endif

    // clang-format off
    const CLAP_EXPORT struct clap_plugin_entry clap_entry = {
        CLAP_VERSION,
        gg::plugin::clapInit,
        gg::plugin::clapDeinit,
        gg::plugin::getFactory
    };
    // clang-format on

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}

// Under the RealtimeSanitizer, report real-time violations and keep running rather than
// aborting the host.
#if defined(__has_feature)
#if __has_feature(realtime_sanitizer)
#define GG_RTSAN_EXPORT __attribute__((visibility("default"), used))
extern "C" GG_RTSAN_EXPORT const char *__rtsan_default_options() { return "halt_on_error=false"; }
extern "C" GG_RTSAN_EXPORT void __sanitizer_report_error_summary(const char *error_summary)
{
    std::fprintf(stderr, "%s\n", error_summary);
}
#endif
#endif
