// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The note and control events a Processor takes (spec §6.1, M2 sub-project 1's spec §6.8, M3
// Voice spec §4.2). Split from Processor.h so the NoteTable can read them; Processor.h includes
// this header.

#pragma once

#include <cstdint>

namespace gg::core
{

enum class NoteEventType : uint8_t
{
    On,     ///< Holds (channel, key).
    Off,    ///< Releases the held pairs that channel and key match; −1 matches any.
    AllOff, ///< Releases every key held on its channel, as MIDI CC 123 does (ruling R14); −1
            ///< releases everything. The key is ignored.
};

/// A note event inside one process() call (spec §6.1). The grains ignore velocity (D6); an On's
/// velocity is the mod matrix's Velocity source (M2 sub-project 1's spec §6.8).
struct NoteEvent
{
    uint32_t offset; ///< Frames from the start of the call. A call's notes are sorted by it.
    NoteEventType type;
    int16_t channel; ///< 0..15; −1 = any (Off and AllOff only). Other values make it ignored.
    int16_t key;     ///< 0..127; −1 = any (Off only; AllOff ignores it). Other values make the
                     ///< event ignored.
    /// On only: 0..1. Clamped to it; NaN counts as 0.
    float velocity{1.0f};
    /// CLAP's note ID, or −1 for none (M3 Voice spec §4.2). An On's ID names the note; an Off
    /// with an ID >= 0 releases the note with that ID (its channel and key are not read), and
    /// is ignored when none is held. An Off without one releases by (channel, key).
    int32_t noteId{-1};
    /// Off only: 0..1. Clamped to it; NaN counts as 0.
    float releaseVelocity{0.0f};
};

/// Which MIDI control a ControlEvent carries (M2 sub-project 1's spec §6.8): CC 1, channel
/// pressure or pitch bend.
enum class ControlKind : uint8_t
{
    ModWheel,
    Aftertouch,
    PitchBend,
    // The kinds below feed the NoteTable alone (M3 Voice spec §4.2, §4.5); the sources above
    // keep their global behavior. Value ranges are in NoteTable.h's notes.
    Sustain,      ///< CC 64: 0..1, >= 0.5 is down. Per channel (MPE On: master channel 0 for all).
    Timbre,       ///< CC 74 / 127, 0..1, its channel's.
    PolyPressure, ///< Polyphonic aftertouch, 0..1, for the notes on (channel, key).
    NoteTuning,   ///< CLAP note expression: semitones, −120..120.
    NotePressure, ///< CLAP note expression: 0..1.
    NoteBrightness, ///< CLAP note expression: 0..1 (the note's Timbre).
    NoteVolume,     ///< CLAP note expression: a gain, 0..4.
};

/// A MIDI control inside one process() call: the mod matrix's Mod Wheel, Aftertouch and Pitch
/// Bend sources (spec §6.8). It joins the control grid as a note event does.
struct ControlEvent
{
    uint32_t offset;  ///< Frames from the start of the call. A call's controls are sorted by it.
    ControlKind kind; ///< Out of range: the event is ignored.
    int16_t channel;  ///< 0..15 or −1 (any); other values make it ignored. Not otherwise read yet.
    float value;      ///< 0..1, clamped to it (pitch bend's center is 0.5); NaN or ±∞ ignored.
                      ///< The kinds from Sustain on have their own ranges (clamped to them).
    /// PolyPressure: the key (0..127); the Note kinds without an ID: the key. Else not read.
    /// −1 or out of range for those: the event is ignored.
    int16_t key{-1};
    /// The Note kinds: the CLAP note ID, matched first; −1 matches by (channel, key). An ID that
    /// names no note ignores the event.
    int32_t noteId{-1};
};

} // namespace gg::core
