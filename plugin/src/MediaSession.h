// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_MEDIASESSION_H
#define GG_PLUGIN_MEDIASESSION_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "SampleLoader.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"
#include "gg/core/SessionState.h"
#include "gg/core/SourceExchange.h"
#include "gg/media/Collect.h"
#include "gg/media/MemoryBudget.h"
#include "gg/media/RingAudio.h"
#include "gg/source/RingStorage.h"
#include "gg/source/SampleBuffer.h"

namespace gg::plugin
{
/// A copy of part of a layer's ring, read on the main thread without stopping the audio thread
/// (readRing).
struct RingCopy
{
    std::shared_ptr<SampleBuffer> window; ///< stereo, at the rate given, oldest frame first
    int64_t oldest{0};                    ///< the ring time of window's first frame
    int64_t head{0};                      ///< R0: the head the copy is the window as of
    std::vector<int64_t> marks;           ///< the splice marks inside the copy, as ring times
};

/// The span of a layer's ring a read takes (readRing), by ring time, as of the head R0 with N the
/// storage's window:
/// - the window: [max(0, R0 − N), R0), and with Loop on its newest min(L, N) frames, L the Loop
///   Length in frames (llround(seconds × rate), at least 1, as the Processor sets it), the Splice
///   selection aside (Task 8's ruling: a saved ring is this window, Splice reselects on restore);
/// - `splice` k >= 1 (a capture's): the span from the k-th mark inside the window, oldest first
///   (D6), to the next mark or R0; k past the count is the last; with no mark inside, the window.
struct RingSpanRequest
{
    bool loop{false};
    int64_t loopFrames{0};
    uint32_t splice{0};
};

/// Reads layer `layer`'s span `request` (RingSpanRequest) through core::RingRead's protocol (spec
/// §6.4, as Processor.h documents it): the epoch, the storage, the head R0 with acquire order, the
/// copy, an acquire fence, then the head R1 and the epoch again. A frame f of the copy is the one
/// written for it when the epoch held and R1 − f <= N (the documented check, frame by frame): so a
/// copy whose oldest frames the head lapped while it copied is still good from max(from, R1 − N)
/// on. A read is tried up to 8 times for the whole span (a frozen ring passes at once, a running
/// one whose head moved during the copy fails the whole-span check), and the last try, if its
/// epoch held, is kept with its lapped front trimmed. nullopt with no ring, an empty span, or the
/// storage replaced under every try. Main thread only: the storage may be read only on the
/// thread that frees it (Processor::ringRead). Consumes the layer's RingMeta (Processor::ringMeta,
/// which has one consumer), for the marks.
std::optional<RingCopy> readRing(const core::Processor &processor, uint8_t layer,
                                 const RingSpanRequest &request, double rate);

/// An instance's media (M2 sub-project 2's spec §6.3–§6.8), in both products: four
/// SampleLoaders, one per layer, sharing the instance's memory budget; the rings the session
/// saves and restores (D1, D8); the Ring Length watcher, which resizes a ring through its layer's
/// SourceExchange while the plugin runs; capture; and collect and save. Main thread only, like
/// the loaders it owns; the audio thread sees only what the exchanges carry.
///
/// **The memory budget** (spec §6.5) is refreshed on the main thread after every change this
/// class makes or applies: the bytes every loader holds (SampleLoader::heldBytes), every layer's
/// ring of record (gg::media::ringBytes of the window the layer was last given, while active: the
/// Processor's own 16 s ring, a resize offered or set, a restore's) and the WAV bytes of a ring
/// held for the next activation. It counts what the instance references, never what the audio
/// thread has yet to hand back, so a refusal never depends on timing. A ring of a new length is
/// checked with the ring it replaces still counted (both live until the swap); so is a restored
/// ring.
///
/// **Rings saved** (D1): a layer's ring is saved when its Freeze or Loop base value is on, as its
/// window (RingSpanRequest: the loop's when Loop is on), read with readRing (the save retries,
/// then trims, rather than waiting for a gap between process() calls, which the main thread can't
/// see), its marks inside it. Rings and embedded captures then take the embed rule (spec §6.6) in
/// layer order, rings before captures: embedded in AUDI while the payloads stay within
/// core::kEmbedCapBytes, else filed in the captures folder as `<sha256>.wav`.
///
/// **Status texts** (status()): "resizing" while a resize is in flight; "over the memory cap:
/// needs N MB, F MB free of C MB" (gg::media::MemoryBudget::refusal) for a ring the cap refused;
/// "not saved: captures folder unwritable" and "not saved: the ring was being replaced" for a
/// save; "ring not found", "embedded audio missing" and "ring not restored: <why>" for a restore;
/// "no ring to capture"; else the loader's status message.
class MediaSession
{
  public:
    /// The callbacks of layer k's loader, made once per layer at construction.
    using CallbacksFor = std::function<SampleLoader::Callbacks(uint8_t layer)>;
    /// The base value of a parameter as the main thread sees it (the plugin's values_), for what
    /// a capture's window reads (Loop and Loop Length). Without one, Loop reads as off.
    using BaseValue = std::function<float(core::ParamSlot)>;

    /// `settingsFile` is the per-machine settings file the loaders read (and the captures folder
    /// comes from). The budget starts at core::kMemoryCapDefaultMiB. Both products' media are
    /// alike, so `mode` changes nothing yet.
    MediaSession(core::Mode mode, std::filesystem::path settingsFile, CallbacksFor perLayer,
                 BaseValue baseValue = {});
    MediaSession(const MediaSession &) = delete;
    MediaSession &operator=(const MediaSession &) = delete;
    ~MediaSession();

    /// Layer `layer`'s loader (`layer` below core::kLayers).
    SampleLoader &loader(uint8_t layer) noexcept { return *loaders_[layer]; }
    const SampleLoader &loader(uint8_t layer) const noexcept { return *loaders_[layer]; }

    /// The memory cap, in MiB (INST's memoryCapMiB): set clamped to kMemoryCapMinMiB to
    /// kMemoryCapMaxMiB. Lowering it evicts nothing; it refuses what comes next.
    uint32_t memoryCapMiB() const noexcept { return capMiB_; }
    void setMemoryCapMiB(uint32_t mib);
    /// What the instance holds against the cap (the class comment), in bytes; sub-project 6's
    /// readout.
    uint64_t memoryUsedBytes() const noexcept;

    /// The session to save: the values, seed and routes given, every loader's entry, the rings
    /// of D1 read from `active` (or, inactive, those held for the next activation), the embed rule
    /// (the class comment), the cap. Filed audio is written to the captures folder unless a file
    /// of its name and size is there. A ring's WAV bytes and hash are cached by (layer, storage
    /// epoch, head, window start), so a host saving every few seconds hashes an unchanged frozen
    /// ring once. Never throws: a write that fails leaves the entry out and sets that layer's
    /// status message; the rest saves.
    core::SessionState save(const core::ParamValues &values, uint64_t seed,
                            const core::ModRoutes &routes, const core::Processor *active) noexcept;

    /// What restore() leaves the plugin to offer per layer, with the values on layer A's exchange.
    struct Restored
    {
        std::array<SampleLoader::Restored, core::kLayers> samples{};
        std::array<std::optional<core::RingOffer>, core::kLayers> rings{};
        std::array<std::unique_ptr<RingStorage>, core::kLayers> storages{};
    };
    /// The restore (D8), inside stateLoad: the cap from INST first, then the loaders in layer
    /// order (an embedded sample from its AUDI payload), then each ring: its payload, or its file
    /// found by gg::media::findFiledAudio, restored (gg::media::restoreRing) into a storage of the
    /// layer's loaded Ring Length at `activeRate`, after a check against the budget, as an offer
    /// held from the first sample when the layer's loaded Freeze is on. Inactive (`activeRate`
    /// 0), the rings wait for activate(). A ring that can't be found, whose payload is missing,
    /// that the cap refuses or that won't decode is left out, with its status. Never throws.
    Restored restore(const core::SessionState &state, double activeRate) noexcept;

    /// The plugin's activate(), with the new Processor before its first call: every loader's
    /// source (Processor::setSourceNow) and every layer's ring at its Ring Length
    /// (Processor::setRingNow): a ring held for this activation restored, or a new storage when
    /// the length differs from the Processor's own (spec Review Focus 2). Throws what the
    /// Processor's setters throw (std::bad_alloc); the plugin then deactivates.
    void activate(core::Processor &processor, double sampleRate, uint32_t maxBlockSize);
    /// The plugin's deactivate(), with the audio thread stopped: the rings of D1 in `processor`
    /// (if any) are kept for the next activation, converted there if its rate differs, as a
    /// restore's are, and the loaders deactivate. Never throws: a ring that can't be kept is lost
    /// with its Processor, as before.
    void deactivate(const core::Processor *processor, const core::ParamValues &values) noexcept;
    /// clap_plugin.on_main_thread, after the plugin has collected its exchanges: the loaders'
    /// finished work, then the Ring Length watcher while active.
    void onMainThread(core::Processor *active) noexcept;

    /// The Ring Length watcher (spec §6.5), on the main thread while active: for each layer whose
    /// requested ring (Processor::ringSecondsRequested) differs from its ring of record and has no
    /// offer in flight, a new storage (the cap permitting), the stable part of the current window
    /// copied into it (readRing's protocol), offered with that head (SourceExchange::offerRing);
    /// a declined offer is made again from the current head. "resizing" meanwhile; "over the
    /// memory cap: …" when refused, until the request changes. Never throws.
    void pollRingLengths(core::Processor &processor) noexcept;

    /// The layer's ring window, or splice `splice` of it (RingSpanRequest, with the layer's Loop
    /// and Loop Length bases), at the active rate (spec §6.4); nullptr, with the status "no ring
    /// to capture", when the layer has no ring or it is empty.
    std::shared_ptr<const SampleBuffer> captureRing(uint8_t layer, uint32_t splice,
                                                    const core::Processor &processor);
    /// `buffer` becomes layer `targetLayer`'s sample through its loader as an in-memory load
    /// (SampleLoader::loadInMemory): its entry embedded, `wav`, with its WAV bytes' fingerprint
    /// and a payload id; the session saves it by the embed rule. False when the cap refuses it.
    bool loadCapture(uint8_t targetLayer, std::shared_ptr<const SampleBuffer> buffer);
    /// Writes `buffer` to `path` as a float32 WAV (gg::media::encodeFloat32Wav), creating its
    /// folder. False if it can't be written.
    bool saveCapture(const SampleBuffer &buffer, const std::filesystem::path &path);
    /// Collect and save (spec §6.7): every loader's file and every filed ring held for the next
    /// activation, copied into `folder` by gg::media::collectMedia, the entries then pointing at
    /// the copies, and the state marked dirty.
    media::CollectResult collectAndSave(const std::filesystem::path &folder);

    /// Layer `layer`'s status message (the class comment).
    std::string status(uint8_t layer) const;

    /// The rings encoded (WAV bytes and hash) by save() and deactivate() so far: for tests.
    uint32_t ringsEncoded() const noexcept { return ringsEncoded_; }

  private:
    struct RingSlot
    {
        int64_t frames{0}; // the ring of record's window, while active
        bool offerPending{false};
        bool offeredRestore{false};
        uint32_t declinesAtOffer{0};
        int64_t refusedFrames{-1}; // a length the cap refused, not tried again until it changes
        std::string resizeStatus;
        std::string persistStatus;
        std::string captureStatus;
        // Held for the next activation: a ring restored while inactive, or kept at deactivate.
        std::optional<core::RingEntry> pendingEntry;
        std::shared_ptr<const std::string> pendingBytes; // null: filed, read from its path
        // The save's cache (the class comment).
        bool cached{false};
        uint32_t cacheEpoch{0};
        int64_t cacheHead{0};
        int64_t cacheOldest{0};
        std::shared_ptr<const media::EncodedRing> encoded;
    };

    /// A layer's ring as a RingEntry (not yet embedded or filed) and its WAV bytes, read from
    /// `processor` for D1's save (the cache), or nullopt (no ring, an empty window, or a read that
    /// failed: `failed` then says so).
    struct SavedRing
    {
        core::RingEntry entry;
        std::shared_ptr<const std::string> bytes;
    };
    std::optional<SavedRing> readForSave(const core::Processor &processor, uint8_t layer,
                                         const core::ParamValues &values, bool &failed);
    /// The window request of layer `layer` with `values` (Loop, Loop Length) at rate_.
    RingSpanRequest windowOf(const core::ParamValues &values, uint8_t layer, uint32_t splice) const;
    void refreshBudget() noexcept;
    /// The captures folder: the settings', or gg::media::defaultCapturesFolder().
    std::filesystem::path capturesFolder() const;
    /// Restores the held ring of `layer` into `processor` (activate()).
    void restoreHeld(core::Processor &processor, uint8_t layer);

    const std::filesystem::path settingsFile_;
    BaseValue baseValue_;
    std::function<void()> markDirty_;
    media::MemoryBudget budget_;
    uint32_t capMiB_{core::kMemoryCapDefaultMiB};
    std::array<std::unique_ptr<SampleLoader>, core::kLayers> loaders_;
    std::array<RingSlot, core::kLayers> rings_;
    std::optional<std::string> newerAudio_; // a newer manifest's AUDI, kept for the save
    core::Processor *active_{nullptr};
    double rate_{0.0};
    uint32_t maxBlock_{0};
    uint32_t ringsEncoded_{0};
};
} // namespace gg::plugin

#endif // GG_PLUGIN_MEDIASESSION_H
