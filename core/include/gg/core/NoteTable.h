// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The note table (M3 Voice spec §4.2, §4.5): the Processor's instance-wide record of its notes,
// with their identity, the sustain pedal and each note's expression. Fixed size, no allocation,
// no unbounded loop: every method is bounded by kMaxNotes or the 16 channels, so the audio
// thread may call it.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "gg/core/NoteEvents.h"

namespace gg::core
{

/// What the table holds, and the rules it keeps:
///
/// - **Identity.** A note is an On. Two Ons on one (channel, key) are two notes. An Off with a
///   note ID >= 0 releases the held note with that ID (none held: ignored). An Off with a
///   channel and key both given releases the OLDEST held note on that pair (row 184); with
///   either −1 it releases every held note that matches, as the M2 held set did (ruling R15).
///   AllOff (CC 123, ruling R14) releases every note on its channel, or every note for −1,
///   pedal-held ones too (a panic).
/// - **The pedal.** Sustain >= 0.5 is down, per channel. While a channel's pedal is down, an
///   Off moves a Held note to PedalHeld; pedal up moves the channel's PedalHeld notes to
///   Released. With MPE On the master channel (0) pedal acts on every channel: pedalDown(ch,
///   true) is the OR of the channel's and the master's. The mode comes from applyControl's
///   `mpe` (the last call's; setMpe sets it alone).
/// - **State.** Held and PedalHeld notes are "held" (heldKeys); Released ones stay in their slot
///   (a voice's tail may still read them) until free() or a replacement.
/// - **A full table** (128 notes) gives a new note a Free slot, else replaces the oldest
///   Released note, else the oldest PedalHeld, else the oldest Held. "Oldest" is the smallest
///   `ordinal`; the caller passes a strictly increasing one with each On. apply() reports the
///   note it replaces, for the caller to end (the Processor's NOTE_END).
/// - **Groups.** apply() folds events as they arrive, as the Processor's held set always did;
///   startedThisGroup() and releasedThisGroup() list, in event order, the slots of the Ons and of
///   the Held/PedalHeld -> Released moves since beginGroup(), so a note that starts and ends in
///   one group is in both. (A Held -> PedalHeld move is not a release.) Each list holds at most
///   kMaxNotes entries; later ones are not listed.
/// - **Expression.** Per channel: bend (the raw 0..1 value of PitchBend mapped to −1..1 as
///   (v − 0.5) × 2, which is not exact at center for a 14-bit value: a later task derives the
///   exact one, spec §3a B7), pressure (ControlKind::Aftertouch), timbre (Timbre, CC 74 / 127);
///   all clamped to 0..1 but the bend. A channel's value applies to its notes now and to a note
///   that starts later (note_table_bend_before_on); with MPE On the master channel (0) values
///   are kept for channelBend/channelPressure/channelTimbre but not copied into notes. A note's
///   pressure follows its channel's until it has had a PolyPressure (then hadPolyPressure and
///   only polyphonic pressure moves it). Per note, by ID, else by (channel, key) among held
///   notes: tuning (−120..120 semitones), pressure (0..1), timbre (0..1), volume (0..4).
///   Out-of-range values are clamped; non-finite ones are ignored.
class NoteTable
{
  public:
    static constexpr size_t kMaxNotes = 128;
    static constexpr size_t kChannels = 16;

    enum class State : uint8_t
    {
        Free,
        Held,
        PedalHeld,
        Released,
    };

    struct Note
    {
        int32_t id{-1};
        int16_t channel{0};
        int16_t key{0};
        float velocity{0.0f};
        float releaseVelocity{0.0f};
        State state{State::Free};
        float bend{0.0f};     ///< −1..1, the channel's at the note's start and since.
        float pressure{0.0f}; ///< 0..1.
        float timbre{0.0f};   ///< 0..1.
        float tuning{0.0f};   ///< Semitones, −120..120.
        float volume{1.0f};   ///< 0..4.
        bool hadPolyPressure{false};
        uint64_t ordinal{0};
    };

    /// Applies a note event whose fields are in range (the Processor validates). `ordinalClock`
    /// becomes an On's ordinal. Returns true when an On took a slot that held a note (a full
    /// table: the class comment's rule), and copies that note, as it was, into `*replaced` when
    /// given, so the caller can end it (the Processor's NOTE_END); false otherwise.
    bool apply(const NoteEvent &n, uint64_t ordinalClock, Note *replaced = nullptr) noexcept;
    /// Applies a control event whose channel is -1..15 and whose value is finite (or not: a
    /// non-finite value is ignored here). ModWheel is not the table's. Remembers `mpe`.
    void applyControl(const ControlEvent &c, bool mpe) noexcept;
    void setMpe(bool mpe) noexcept { mpe_ = mpe; }

    /// The distinct keys of the Held and PedalHeld notes, ascending, into `out` (at most
    /// out.size() of them; 128 hold any set); `count` is how many.
    void heldKeys(std::span<uint8_t> out, uint32_t &count) const noexcept;

    std::span<const Note> notes() const noexcept { return {notes_.data(), notes_.size()}; }
    float channelBend(int ch) const noexcept;
    float channelPressure(int ch) const noexcept;
    float channelTimbre(int ch) const noexcept;
    bool pedalDown(int ch, bool mpe) const noexcept;

    std::span<const uint8_t> startedThisGroup() const noexcept
    {
        return {started_.data(), startedCount_};
    }
    std::span<const uint8_t> releasedThisGroup() const noexcept
    {
        return {released_.data(), releasedCount_};
    }
    /// Empties both lists: a new group begins.
    void beginGroup() noexcept
    {
        startedCount_ = 0;
        releasedCount_ = 0;
    }

    /// Frees a slot (index into notes()); out of range is ignored. For the voices that are done
    /// with a Released note.
    void free(size_t index) noexcept;
    /// Every note Free, every channel at rest (bend 0, pressure 0, timbre 0, pedal up), the
    /// lists empty, MPE off.
    void reset() noexcept;

  private:
    size_t slotForNew() const noexcept;
    void release(size_t i, float releaseVelocity) noexcept;
    void releaseNow(size_t i, float releaseVelocity) noexcept;
    void setChannelBend(int ch, float bend) noexcept;
    void setChannelPressure(int ch, float v) noexcept;
    void setChannelTimbre(int ch, float v) noexcept;
    void setPedal(int ch, bool down) noexcept;
    void unpedal() noexcept;
    bool channelValuesCopied(int ch) const noexcept { return !(mpe_ && ch == 0); }

    std::array<Note, kMaxNotes> notes_{};
    std::array<float, kChannels> bend_{};
    std::array<float, kChannels> pressure_{};
    std::array<float, kChannels> timbre_{};
    std::array<bool, kChannels> pedal_{};
    std::array<uint8_t, kMaxNotes> started_{};
    std::array<uint8_t, kMaxNotes> released_{};
    size_t startedCount_{0};
    size_t releasedCount_{0};
    bool mpe_{false};
};

} // namespace gg::core
