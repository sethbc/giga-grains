// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_HELDVALUE_H
#define GG_UI_HELDVALUE_H

#include <chrono>

namespace gg::ui
{
/// Wall time in seconds, from any steady origin: the UI's clock for holds and extrapolation.
inline double wallSeconds() noexcept
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// A value the editor sent the plugin, shown in place of the parameter's base value until the
/// base value catches up. The plugin applies a gesture's values on the audio thread (spec §6.3),
/// so its base value lags the drag that set it; without the hold a control would jump back to
/// the old value for a block or two after it was let go.
///
/// While an edit is under way the sent value is shown whatever the base value does. After it
/// ends, the sent value is shown until the base value changes from what it was then (the audio
/// thread took ours, or the host moved it), equals the sent value, or kHoldSeconds pass.
struct HeldValue
{
    static constexpr double kHoldSeconds = 0.5;

    void begin() noexcept { editing_ = true; }
    void set(float value) noexcept
    {
        value_ = value;
        active_ = true;
    }
    void end(float base, double now) noexcept
    {
        editing_ = false;
        baseAtEnd_ = base;
        until_ = now + kHoldSeconds;
    }
    bool editing() const noexcept { return editing_; }

    /// What to show, given the parameter's base value now.
    float shown(float base, double now) noexcept
    {
        if (active_ && !editing_ && (base == value_ || base != baseAtEnd_ || !(now < until_)))
        {
            active_ = false;
        }
        return active_ ? value_ : base;
    }

  private:
    bool editing_{false};
    bool active_{false};
    float value_{0.0f};
    float baseAtEnd_{0.0f};
    double until_{0.0};
};
} // namespace gg::ui

#endif // GG_UI_HELDVALUE_H
