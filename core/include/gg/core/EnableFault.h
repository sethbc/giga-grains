// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_ENABLEFAULT_H
#define GG_CORE_ENABLEFAULT_H

#include <cstdint>
#include <string>

namespace gg::core
{
/// Carry-over row 234's diagnostic: the first layer enable flip Processor::enableFaults()
/// counted, its layer, its piece's first frame and which invariant it broke (disagrees: the flip
/// disagreed with its on ramp re-evaluated; offGrid: it fell off the control grid other than at a
/// ramp off's end or after a jump).
struct EnableFault
{
    uint8_t layer{0};
    int64_t frame{0};
    bool disagrees{false};
    bool offGrid{false};
};

/// What row 234's counters said after a render: Processor::enableFaults(), firstEnableFault()
/// and engineGateFaults(). All zero in every correct render.
struct FaultCounts
{
    uint32_t enableFaults{0};
    EnableFault firstEnableFault{};
    uint32_t gateFaults{0};
};

/// The counts as one line, for a test's INFO and gg-render's WARNING: "no enable or gate faults",
/// or each count with the first enable fault's layer, frame and kind.
inline std::string describeFaults(const FaultCounts &c)
{
    if (c.enableFaults == 0 && c.gateFaults == 0)
    {
        return "no enable or gate faults";
    }
    std::string s = std::to_string(c.enableFaults) + " layer enable faults";
    if (c.enableFaults > 0)
    {
        const EnableFault &f = c.firstEnableFault;
        s += " (the first on layer " + std::to_string(f.layer) + " at frame " +
             std::to_string(f.frame) + (f.disagrees ? ", disagreeing with its on ramp" : "") +
             (f.offGrid ? ", off the control grid" : "") + ")";
    }
    return s + ", " + std::to_string(c.gateFaults) + " grains spawned with an engine's gate off";
}
} // namespace gg::core

#endif // GG_CORE_ENABLEFAULT_H
