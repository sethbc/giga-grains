// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_PLUGINTESTHOOKS_H
#define GG_PLUGIN_PLUGINTESTHOOKS_H

// Internal to the plugin core, and reached only by the tests (as gg-media's are, in
// media/src/TestHooks.h): a hook into the ring reader, so a test can make the head lap a copy at
// a point it chooses, where a running audio thread would do so only by chance. Unset (nullptr),
// as it is unless a test sets it, it costs one atomic load per try of a read, on the main thread;
// the audio thread never reaches it. Each test case runs in a process of its own, so a hook set by
// one never reaches another.

#include <cstdint>

namespace gg::plugin::detail
{
/// Called by the ring reader (readRing, a save's read and a resize's copy, MediaSession.h) on the
/// main thread after each try's copy of layer `layer`'s ring and before its closing reads (the
/// head R1 and the epoch), with the try's number from 0. A test renders the Processor forward
/// there, so the frames the head writes meanwhile lap the copy's front as an audio thread's
/// would.
using RingReadHook = void (*)(uint8_t layer, int attempt);

/// Sets the ring reader's hook (nullptr: none). Set only while no read runs.
void setRingReadHook(RingReadHook hook);
} // namespace gg::plugin::detail

#endif // GG_PLUGIN_PLUGINTESTHOOKS_H
