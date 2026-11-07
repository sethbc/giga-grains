// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// One layer's voices (M3 Voice spec §4.3, V4): which of the engine's kMaxVoices voice slots holds
// which note, each voice's ordinal and its own Env 1 and Env 2. Fixed size, no allocation, every
// method bounded by kMaxVoices, so the audio thread may call it.

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "gg/core/ModSources.h"
#include "gg/engine/Engine.h"

namespace gg::core
{

/// The voice slots of one layer, mirroring its Engine's (startVoice, finishVoice): the Processor
/// starts a voice here, then tells the engine with the slot and ordinal it got.
///
/// - **Ordinals** count the layer's voices from 0 since construction or reset(): a voice's
///   stream is 1 + its ordinal (the engine's RNG key), so the n-th voice of a render draws the
///   same grains however the host splits blocks.
/// - **A slot** is free once its voice finished (or never started). start() takes the lowest
///   free slot; with none free it steals (V4): the oldest voice (smallest ordinal) whose note is
///   released or pedal-held (weak), else the oldest held one. The stolen voice stops spawning at
///   once and its grains play out their windows, which is the fade: no steal fade of its own (B6).
/// - **The gate** of a voice is its note held or pedal-held: release() closes it. A voice whose
///   note starts and ends within one control group (B5) closes its gate one cell later
///   (releaseLater()), so its Env 1 attacks and it plays its release.
class VoiceAllocator
{
  public:
    static constexpr uint32_t kVoices = gg::kMaxVoices;

    struct Voice
    {
        bool spawning{false};
        uint8_t note{0};          ///< The note's index into NoteTable::notes().
        uint32_t ordinal{0};      ///< 0, 1, 2, … per layer since construction or reset().
        Adsr env[2];              ///< The voice's own Env 1 (its amplitude, V1) and Env 2.
        uint64_t noteOrdinal{0};  ///< The note's NoteTable::Note::ordinal: the slot still holds it.
        bool released{false};     ///< The gate is closed: the note is off and no pedal holds it.
        bool weak{false};         ///< The note is released or pedal-held: stolen first.
        bool releaseLater{false}; ///< B5: the gate closes at the first boundary after startedAt.
        bool told{false};         ///< The engine has the voice (startVoice); false after start().
        int64_t startedAt{0};     ///< The boundary the voice started at.
        float envValue[2]{};      ///< Env 1's and Env 2's values for the last stepped cell.
    };

    /// Starts a voice for note `noteIndex` (whose NoteTable ordinal is `noteOrdinal`) at the
    /// boundary `at`, gate open, envelopes reset. Returns its slot; `ordinalOut` is its ordinal,
    /// `stolenSlotOut` the slot stolen (the same slot) or −1 when a free one was taken.
    int start(uint8_t noteIndex, uint64_t noteOrdinal, int64_t at, uint32_t &ordinalOut,
              int &stolenSlotOut) noexcept;
    /// The brief's form: start(noteIndex, 0, 0, …).
    int start(uint8_t noteIndex, uint32_t &ordinalOut, int &stolenSlotOut) noexcept
    {
        return start(noteIndex, 0, 0, ordinalOut, stolenSlotOut);
    }
    /// Closes the gate of every spawning voice of note `noteIndex` (and ranks them weak).
    void release(uint8_t noteIndex) noexcept;
    /// Stops the voice in `slot` spawning: its slot is free. Out of range is ignored.
    void finish(int slot) noexcept;
    /// Finishes every voice; the ordinals go on.
    void clear() noexcept;
    /// Finishes every voice and restarts the ordinals at 0.
    void reset() noexcept;

    bool anySpawning() const noexcept;
    std::span<Voice> voices() noexcept { return {voices_.data(), voices_.size()}; }
    std::span<const Voice> voices() const noexcept { return {voices_.data(), voices_.size()}; }

  private:
    std::array<Voice, kVoices> voices_{};
    uint32_t nextOrdinal_{0};
};

} // namespace gg::core
