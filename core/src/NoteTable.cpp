// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/NoteTable.h"

#include <algorithm>
#include <cmath>

namespace gg::core
{

namespace
{

float clampTo(float v, float lo, float hi) noexcept { return v >= lo ? (v <= hi ? v : hi) : lo; }

/// A value clamped to [lo, hi], NaN as lo (an On's velocity: NaN counts as 0).
float clampNaNLow(float v, float lo, float hi) noexcept
{
    return std::isnan(v) ? lo : clampTo(v, lo, hi);
}

bool isHeld(NoteTable::State s) noexcept
{
    return s == NoteTable::State::Held || s == NoteTable::State::PedalHeld;
}

} // namespace

size_t NoteTable::slotForNew() const noexcept
{
    // A Free slot first; else the oldest of the weakest state.
    for (size_t i = 0; i < kMaxNotes; ++i)
    {
        if (notes_[i].state == State::Free)
        {
            return i;
        }
    }
    for (const State s : {State::Released, State::PedalHeld, State::Held})
    {
        size_t best = kMaxNotes;
        for (size_t i = 0; i < kMaxNotes; ++i)
        {
            if (notes_[i].state == s &&
                (best == kMaxNotes || notes_[i].ordinal < notes_[best].ordinal))
            {
                best = i;
            }
        }
        if (best != kMaxNotes)
        {
            return best;
        }
    }
    return 0; // unreachable: a full table has a note in one of those states
}

void NoteTable::releaseNow(size_t i, float releaseVelocity) noexcept
{
    Note &n = notes_[i];
    n.state = State::Released;
    n.releaseVelocity = releaseVelocity;
    if (releasedCount_ < kMaxNotes)
    {
        released_[releasedCount_++] = static_cast<uint8_t>(i);
    }
}

void NoteTable::release(size_t i, float releaseVelocity) noexcept
{
    Note &n = notes_[i];
    if (pedalDown(n.channel, mpe_))
    {
        n.state = State::PedalHeld;
        n.releaseVelocity = releaseVelocity;
    }
    else
    {
        releaseNow(i, releaseVelocity);
    }
}

void NoteTable::apply(const NoteEvent &e, uint64_t ordinalClock) noexcept
{
    const float rv = clampNaNLow(e.releaseVelocity, 0.0f, 1.0f);
    switch (e.type)
    {
    case NoteEventType::On:
    {
        const size_t i = slotForNew();
        Note &n = notes_[i];
        n = Note{};
        n.id = e.noteId;
        n.channel = e.channel;
        n.key = e.key;
        n.velocity = clampNaNLow(e.velocity, 0.0f, 1.0f);
        n.state = State::Held;
        n.ordinal = ordinalClock;
        if (channelValuesCopied(e.channel))
        {
            const auto ch = static_cast<size_t>(e.channel);
            n.bend = bend_[ch];
            n.pressure = pressure_[ch];
            n.timbre = timbre_[ch];
        }
        if (startedCount_ < kMaxNotes)
        {
            started_[startedCount_++] = static_cast<uint8_t>(i);
        }
        break;
    }
    case NoteEventType::Off:
    {
        if (e.noteId >= 0)
        {
            size_t best = kMaxNotes;
            for (size_t i = 0; i < kMaxNotes; ++i)
            {
                if (notes_[i].state == State::Held && notes_[i].id == e.noteId &&
                    (best == kMaxNotes || notes_[i].ordinal < notes_[best].ordinal))
                {
                    best = i;
                }
            }
            if (best != kMaxNotes)
            {
                release(best, rv);
            }
            break;
        }
        const bool exact = e.channel >= 0 && e.key >= 0;
        size_t best = kMaxNotes;
        for (size_t i = 0; i < kMaxNotes; ++i)
        {
            const Note &n = notes_[i];
            if (n.state != State::Held || (e.channel >= 0 && n.channel != e.channel) ||
                (e.key >= 0 && n.key != e.key))
            {
                continue;
            }
            if (!exact)
            {
                release(i, rv);
            }
            else if (best == kMaxNotes || n.ordinal < notes_[best].ordinal)
            {
                best = i;
            }
        }
        if (exact && best != kMaxNotes)
        {
            release(best, rv);
        }
        break;
    }
    case NoteEventType::AllOff:
        for (size_t i = 0; i < kMaxNotes; ++i)
        {
            const Note &n = notes_[i];
            if (isHeld(n.state) && (e.channel < 0 || n.channel == e.channel))
            {
                releaseNow(i, rv);
            }
        }
        break;
    }
}

void NoteTable::setChannelBend(int ch, float bend) noexcept
{
    bend_[static_cast<size_t>(ch)] = bend;
    if (channelValuesCopied(ch))
    {
        for (Note &n : notes_)
        {
            if (n.state != State::Free && n.channel == ch)
            {
                n.bend = bend;
            }
        }
    }
}

void NoteTable::setChannelPressure(int ch, float v) noexcept
{
    pressure_[static_cast<size_t>(ch)] = v;
    if (channelValuesCopied(ch))
    {
        for (Note &n : notes_)
        {
            if (n.state != State::Free && n.channel == ch && !n.hadPolyPressure)
            {
                n.pressure = v;
            }
        }
    }
}

void NoteTable::setChannelTimbre(int ch, float v) noexcept
{
    timbre_[static_cast<size_t>(ch)] = v;
    if (channelValuesCopied(ch))
    {
        for (Note &n : notes_)
        {
            if (n.state != State::Free && n.channel == ch)
            {
                n.timbre = v;
            }
        }
    }
}

void NoteTable::unpedal() noexcept
{
    for (size_t i = 0; i < kMaxNotes; ++i)
    {
        if (notes_[i].state == State::PedalHeld && !pedalDown(notes_[i].channel, mpe_))
        {
            releaseNow(i, notes_[i].releaseVelocity);
        }
    }
}

void NoteTable::setPedal(int ch, bool down) noexcept
{
    pedal_[static_cast<size_t>(ch)] = down;
    if (!down)
    {
        unpedal();
    }
}

void NoteTable::applyControl(const ControlEvent &c, bool mpe) noexcept
{
    mpe_ = mpe;
    if (!std::isfinite(c.value) || c.channel < -1 || c.channel > 15)
    {
        return;
    }
    // A channel -1 control is for every channel.
    const int lo = c.channel < 0 ? 0 : c.channel;
    const int hi = c.channel < 0 ? static_cast<int>(kChannels) - 1 : c.channel;
    switch (c.kind)
    {
    case ControlKind::ModWheel:
        break;
    case ControlKind::Aftertouch:
        for (int ch = lo; ch <= hi; ++ch)
        {
            setChannelPressure(ch, clampTo(c.value, 0.0f, 1.0f));
        }
        break;
    case ControlKind::PitchBend:
        for (int ch = lo; ch <= hi; ++ch)
        {
            setChannelBend(ch, (clampTo(c.value, 0.0f, 1.0f) - 0.5f) * 2.0f);
        }
        break;
    case ControlKind::Timbre:
        for (int ch = lo; ch <= hi; ++ch)
        {
            setChannelTimbre(ch, clampTo(c.value, 0.0f, 1.0f));
        }
        break;
    case ControlKind::Sustain:
        for (int ch = lo; ch <= hi; ++ch)
        {
            setPedal(ch, c.value >= 0.5f);
        }
        break;
    case ControlKind::PolyPressure:
    {
        if (c.key < 0 || c.key > 127 || c.channel < 0)
        {
            return;
        }
        const float v = clampTo(c.value, 0.0f, 1.0f);
        for (Note &n : notes_)
        {
            if (isHeld(n.state) && n.channel == c.channel && n.key == c.key)
            {
                n.pressure = v;
                n.hadPolyPressure = true;
            }
        }
        break;
    }
    case ControlKind::NoteTuning:
    case ControlKind::NotePressure:
    case ControlKind::NoteBrightness:
    case ControlKind::NoteVolume:
    {
        const bool byId = c.noteId >= 0;
        if (!byId && (c.key < 0 || c.key > 127 || c.channel < 0))
        {
            return;
        }
        for (Note &n : notes_)
        {
            const bool match = byId ? (n.state != State::Free && n.id == c.noteId)
                                    : (isHeld(n.state) && n.channel == c.channel && n.key == c.key);
            if (!match)
            {
                continue;
            }
            switch (c.kind)
            {
            case ControlKind::NoteTuning:
                n.tuning = clampTo(c.value, -120.0f, 120.0f);
                break;
            case ControlKind::NotePressure:
                n.pressure = clampTo(c.value, 0.0f, 1.0f);
                n.hadPolyPressure = true;
                break;
            case ControlKind::NoteBrightness:
                n.timbre = clampTo(c.value, 0.0f, 1.0f);
                break;
            default:
                n.volume = clampTo(c.value, 0.0f, 4.0f);
                break;
            }
        }
        break;
    }
    }
}

void NoteTable::heldKeys(std::span<uint8_t> out, uint32_t &count) const noexcept
{
    std::array<uint64_t, 2> bits{};
    for (const Note &n : notes_)
    {
        if (isHeld(n.state))
        {
            const auto k = static_cast<size_t>(std::clamp<int16_t>(n.key, 0, 127));
            bits[k >> 6] |= uint64_t{1} << (k & 63);
        }
    }
    count = 0;
    for (size_t k = 0; k < 128 && count < out.size(); ++k)
    {
        if ((bits[k >> 6] >> (k & 63)) & 1u)
        {
            out[count++] = static_cast<uint8_t>(k);
        }
    }
}

float NoteTable::channelBend(int ch) const noexcept
{
    return ch >= 0 && ch < 16 ? bend_[static_cast<size_t>(ch)] : 0.0f;
}

float NoteTable::channelPressure(int ch) const noexcept
{
    return ch >= 0 && ch < 16 ? pressure_[static_cast<size_t>(ch)] : 0.0f;
}

float NoteTable::channelTimbre(int ch) const noexcept
{
    return ch >= 0 && ch < 16 ? timbre_[static_cast<size_t>(ch)] : 0.0f;
}

bool NoteTable::pedalDown(int ch, bool mpe) const noexcept
{
    if (ch < 0 || ch > 15)
    {
        return false;
    }
    return pedal_[static_cast<size_t>(ch)] || (mpe && pedal_[0]);
}

void NoteTable::free(size_t index) noexcept
{
    if (index < kMaxNotes)
    {
        notes_[index] = Note{};
    }
}

void NoteTable::reset() noexcept
{
    notes_.fill(Note{});
    bend_.fill(0.0f);
    pressure_.fill(0.0f);
    timbre_.fill(0.0f);
    pedal_.fill(false);
    startedCount_ = 0;
    releasedCount_ = 0;
    mpe_ = false;
}

} // namespace gg::core
