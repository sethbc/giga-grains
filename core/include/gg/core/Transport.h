// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_TRANSPORT_H
#define GG_CORE_TRANSPORT_H

namespace gg::core
{
/// The host's transport as its last transport event gave it. The transport sources
/// (`TransportSource`, ModSources.h) and the sync clock (`SyncClock`, SyncClock.h) both read it.
struct Transport
{
    bool hasBeats{false};
    double beatPosition{0};
    double tempoBpm{120};
    bool playing{false};
    int beatsPerBar{4};
    /// The time signature's denominator (CLAP's `tsig_denom`; 4 without it).
    int beatUnit{4};
    /// The beat the current bar started on (CLAP's `bar_start`; 0 without it).
    double barStart{0.0};
};
} // namespace gg::core

#endif // GG_CORE_TRANSPORT_H
