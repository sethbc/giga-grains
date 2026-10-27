// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/ui/StressGrains.h"

#include <cmath>

#include "gg/core/Rng.h"

namespace gg::ui
{
namespace
{
constexpr uint32_t kLanes = 4;
constexpr uint64_t kMinLife = 20;   // frames
constexpr uint64_t kLifeRange = 40; // so lives are 20..59 frames
// Keeps the per-slot draws apart from the per-grain ones, which start from small integers.
constexpr uint64_t kSlotSalt = 0x67672d7374726573ull; // "gg-stres"

/// The top 24 bits of h as a float in [0, 1).
float unit(uint64_t h) noexcept { return static_cast<float>(h >> 40) * 0x1.0p-24f; }
} // namespace

void stressGrains(uint64_t frame, uint32_t count, uint32_t width, uint32_t height, float pixelScale,
                  Splat *out) noexcept
{
    if (out == nullptr)
    {
        return;
    }
    if (!std::isfinite(pixelScale) || !(pixelScale > 0.0f))
    {
        pixelScale = 1.0f;
    }
    const auto canvasWidth = static_cast<float>(width);
    const float laneHeight = static_cast<float>(height) / kLanes;

    for (uint32_t i = 0; i < count; ++i)
    {
        // The slot's lifetime and phase stay fixed, so the slots respawn out of step.
        const uint64_t slot = rng::splitmix64(kSlotSalt ^ i);
        const uint64_t life = kMinLife + slot % kLifeRange;
        const uint64_t age = frame + (slot >> 32) % life;
        const uint64_t generation = age / life;
        const float progress = static_cast<float>(age % life) / static_cast<float>(life);

        // This generation's grain.
        uint64_t h = rng::splitmix64(generation * count + i);
        const float u0 = unit(h);
        h = rng::splitmix64(h);
        const float u1 = unit(h);
        h = rng::splitmix64(h);
        const float u2 = unit(h);
        h = rng::splitmix64(h);
        const float u3 = unit(h);
        h = rng::splitmix64(h);
        const float u4 = unit(h);
        const auto lane = static_cast<uint32_t>(h % kLanes);

        const float drift = (u2 - 0.5f) * 40.0f * pixelScale; // over the grain's life
        const float envelope = 4.0f * progress * (1.0f - progress);
        out[i] = {u0 * canvasWidth + progress * drift,
                  laneHeight * (static_cast<float>(lane) + 0.5f + 0.4f * (2.0f * u1 - 1.0f)),
                  (2.0f + 6.0f * u3) * pixelScale, (0.15f + 0.45f * u4) * envelope};
    }
}
} // namespace gg::ui
