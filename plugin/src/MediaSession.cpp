// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "MediaSession.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <span>
#include <system_error>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "gg/core/ParamMap.h"
#include "gg/media/Decode.h"
#include "gg/media/Search.h"
#include "gg/media/SettingsFile.h"
#include "gg/media/Utf8Path.h"
#include "gg/media/WavBytes.h"
#include "PluginTestHooks.h"

namespace gg::plugin
{
namespace
{
namespace fs = std::filesystem;
using core::kLayers;

constexpr int kRingReadTries = 8;

/// The ring reader's hook (PluginTestHooks.h); nullptr unless a test sets one.
std::atomic<detail::RingReadHook> ringReadHook{nullptr};

constexpr const char *kResizing = "resizing";
constexpr const char *kCapturesUnwritable = "not saved: captures folder unwritable";
constexpr const char *kRingReplaced = "not saved: the ring was being replaced";
constexpr const char *kRingNotFound = "ring not found";
constexpr const char *kEmbeddedMissing = "embedded audio missing";
constexpr const char *kNoRing = "no ring to capture";

/// A ring of `seconds` at `rate`, in frames, as LiveSource::prepare sizes it.
int64_t framesFor(double seconds, double rate) noexcept
{
    return core::Processor::ringFramesFor(seconds, rate); // the engines' ring limit's frames
}

/// The bytes a ring of `frames` frames holds (RingStorage::bytes), with its levels (spec §6.9).
uint64_t ringBytesOf(int64_t frames, uint32_t maxBlock) noexcept
{
    return media::ringBytes(frames, maxBlock, RingStorage::levelCountFor(frames));
}

uint64_t decodeBound(const media::MemoryBudget &b, uint64_t also) noexcept
{
    const uint64_t used = b.usedBytes + also;
    return std::min(b.capBytes > used ? b.capBytes - used : 0, media::kDefaultDecodeBound);
}

std::string utf8(const fs::path &path) { return media::toUtf8(path).value_or(std::string()); }

/// The process's id, for a temporary file's name.
unsigned long processId() noexcept
{
#if defined(_WIN32)
    return static_cast<unsigned long>(::_getpid());
#else
    return static_cast<unsigned long>(::getpid());
#endif
}

/// Writes `bytes` to `path` through a temporary file beside it, so a file of that name is always
/// whole. False if it can't.
bool writeWhole(const fs::path &path, const std::string &bytes)
{
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    fs::path temporary = path;
    // Unique to this process and this write, so two instances (or hosts) filing the same ring at
    // once never write one temporary file.
    static std::atomic<uint32_t> writes{0};
    temporary += "." + std::to_string(processId()) + "-" +
                 std::to_string(writes.fetch_add(1, std::memory_order_relaxed)) + ".partial";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return false;
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out)
        {
            fs::remove(temporary, error);
            return false;
        }
    }
    fs::rename(temporary, path, error);
    if (error)
    {
        fs::remove(temporary, error);
        return false;
    }
    return true;
}

/// The span `request` names as of the head `r0` over a window of `n` frames, and the marks inside
/// the window (`meta`'s, by ring time).
struct Span
{
    int64_t from{0};
    int64_t to{0};
};

Span spanOf(const RingSpanRequest &request, const core::RingMeta &meta, int64_t r0, int64_t n,
            std::vector<int64_t> &marks)
{
    int64_t lo = std::max<int64_t>(0, r0 - n);
    if (request.loop)
    {
        lo = std::max(lo, r0 - std::max<int64_t>(1, request.loopFrames));
    }
    marks.clear();
    for (uint32_t k = 0; k < std::min<uint32_t>(meta.spliceCount, core::kMaxSplices); ++k)
    {
        if (meta.splices[k] >= lo && meta.splices[k] < r0)
        {
            marks.push_back(meta.splices[k]);
        }
    }
    if (request.splice == 0 || marks.empty())
    {
        return {lo, r0};
    }
    // D6: from the oldest mark inside the window; past the count, the last.
    const std::size_t k = std::min<std::size_t>(request.splice, marks.size()) - 1;
    const int64_t to = k + 1 < marks.size() ? marks[k + 1] : r0;
    return {marks[k], to};
}

/// The one ring reader (core::RingRead's protocol; readRing's comment). `copy(storage, from, to)`
/// copies frames [from, to) wherever the caller wants them, each try. On success, `from`, `valid`
/// (the first frame of the copy known good) and `to`, the head R0 and the marks inside the window.
struct ReadDone
{
    int64_t from{0};
    int64_t valid{0};
    int64_t to{0};
    int64_t head{0};
    uint32_t epoch{0};  // e0, the storage epoch the copy is of
    bool frozen{false}; // the meta's: the engine was given its freeze
    bool cached{false}; // the probe said the caller holds this copy: nothing was copied
    std::vector<int64_t> marks;
};

/// `probe(meta, e0, r0, span)`, before the copy, may say the caller already holds that copy
/// (the save's cache of a frozen ring): the read then ends there, `cached`, without copying.
/// `replaced`, when given, is set when the read failed with a ring there (the storage replaced
/// under every try, or the whole copy lapped), as against no ring or an empty span.
template <class Probe, class Copy>
std::optional<ReadDone> readWith(const core::Processor &processor, uint8_t layer,
                                 const RingSpanRequest &request, int64_t maxFrames, Probe &&probe,
                                 Copy &&copy, bool *replaced = nullptr)
{
    if (replaced != nullptr)
    {
        *replaced = false;
    }
    ReadDone done;
    for (int attempt = 0; attempt < kRingReadTries; ++attempt)
    {
        const bool last = attempt + 1 == kRingReadTries;
        // 1. The marks (and, for a splice, the span) from the latest meta.
        const core::RingMeta meta = processor.ringMeta(layer);
        // 2. The epoch: odd while an adoption swaps the storage.
        const std::atomic<uint32_t> *epoch = processor.ringRead(layer).storageEpoch;
        const uint32_t e0 = epoch->load(std::memory_order_acquire);
        if ((e0 & 1u) != 0)
        {
            std::this_thread::yield();
            continue;
        }
        // 3. The storage as of e0 or later.
        const core::RingRead read = processor.ringRead(layer);
        if (read.storage == nullptr)
        {
            return std::nullopt;
        }
        const int64_t n = read.storage->frames();
        // 4. The head R0.
        const int64_t r0 = read.head->load(std::memory_order_acquire);
        Span span = spanOf(request, meta, r0, n, done.marks);
        if (maxFrames > 0)
        {
            span.from = std::max(span.from, span.to - maxFrames);
        }
        if (span.to <= span.from)
        {
            return std::nullopt;
        }
        done.frozen = meta.frozen;
        if (probe(meta, e0, r0, span))
        {
            done.from = span.from;
            done.valid = span.from;
            done.to = span.to;
            done.head = r0;
            done.epoch = e0;
            done.cached = true;
            return done;
        }
        // 5. The copy.
        copy(*read.storage, span.from, span.to);
        if (const auto hook = ringReadHook.load(std::memory_order_relaxed))
        {
            hook(layer, attempt);
        }
        // 6. No frame load completes after what follows.
        std::atomic_thread_fence(std::memory_order_acquire);
        // 7. R1 and the epoch again.
        const int64_t r1 = read.head->load(std::memory_order_relaxed);
        const uint32_t e1 = read.storageEpoch->load(std::memory_order_relaxed);
        if (e1 != e0)
        {
            continue;
        }
        // Frame f is good when R1 − f <= N: none written meanwhile shares its slot.
        const int64_t valid = std::max(span.from, r1 - n);
        if (valid == span.from || last)
        {
            if (valid >= span.to)
            {
                break; // the head lapped the whole copy
            }
            done.from = span.from;
            done.valid = valid;
            done.to = span.to;
            done.head = r0;
            done.epoch = e0;
            return done;
        }
    }
    if (replaced != nullptr)
    {
        *replaced = true;
    }
    return std::nullopt;
}

/// The RingEntry of a D1 window `done` read of layer `layer` at `rate`, with the marks inside
/// it as offsets from its first frame; no fingerprint yet.
core::RingEntry windowEntry(const ReadDone &done, uint8_t layer, double rate)
{
    core::RingEntry entry;
    entry.layer = layer;
    entry.frames = done.to - done.from;
    entry.sampleRate = rate;
    entry.channels = 2;
    for (const int64_t m : done.marks)
    {
        if (m >= done.from && m < done.to)
        {
            entry.splices.push_back(m - done.from);
        }
    }
    return entry;
}

/// readWith with no cache to probe.
template <class Copy>
std::optional<ReadDone> readWith(const core::Processor &processor, uint8_t layer,
                                 const RingSpanRequest &request, int64_t maxFrames, Copy &&copy)
{
    return readWith(
        processor, layer, request, maxFrames,
        [](const core::RingMeta &, uint32_t, int64_t, const Span &) { return false; },
        std::forward<Copy>(copy));
}
} // namespace

std::optional<RingCopy> readRing(const core::Processor &processor, uint8_t layer,
                                 const RingSpanRequest &request, double rate)
{
    std::shared_ptr<SampleBuffer> buffer;
    const auto done = readWith(
        processor, layer, request, 0, [&](const RingStorage &storage, int64_t from, int64_t to) {
            buffer =
                media::ringToBuffer(storage.slots(), storage.capacity(), from, to - from, rate);
        });
    if (!done.has_value())
    {
        return std::nullopt;
    }
    RingCopy out;
    out.oldest = done->valid;
    out.head = done->head;
    for (const int64_t m : done->marks)
    {
        if (m >= done->valid && m < done->to)
        {
            out.marks.push_back(m);
        }
    }
    if (done->valid == done->from)
    {
        out.window = std::move(buffer);
        return out;
    }
    // The lapped front dropped: the newest frames alone.
    const int64_t skip = done->valid - done->from;
    const int64_t frames = done->to - done->valid;
    auto trimmed = std::make_shared<SampleBuffer>(2, frames, rate);
    for (uint32_t c = 0; c < 2; ++c)
    {
        std::memcpy(trimmed->channel(c), buffer->channel(c) + skip,
                    static_cast<std::size_t>(frames) * sizeof(float));
    }
    out.window = std::move(trimmed);
    return out;
}

MediaSession::MediaSession(core::Mode mode, std::filesystem::path settingsFile,
                           CallbacksFor perLayer, BaseValue baseValue)
    : mode_(mode), settingsFile_(std::move(settingsFile)), baseValue_(std::move(baseValue)),
      budget_{uint64_t{core::kMemoryCapDefaultMiB} << 20}
{
    for (uint8_t l = 0; l < kLayers; ++l)
    {
        SampleLoader::Callbacks callbacks = perLayer ? perLayer(l) : SampleLoader::Callbacks{};
        if (l == 0)
        {
            markDirty_ = callbacks.markDirty;
        }
        loaders_[l] =
            std::make_unique<SampleLoader>(settingsFile_, std::move(callbacks), l, &budget_);
    }
}

MediaSession::~MediaSession() = default;

void MediaSession::setMemoryCapMiB(uint32_t mib)
{
    capMiB_ = std::clamp(mib, core::kMemoryCapMinMiB, core::kMemoryCapMaxMiB);
    budget_.capBytes = uint64_t{capMiB_} << 20;
}

uint64_t MediaSession::memoryUsedBytes() const noexcept
{
    uint64_t used = 0;
    for (uint8_t l = 0; l < kLayers; ++l)
    {
        used += loaders_[l]->heldBytes();
        const RingSlot &slot = rings_[l];
        if (active_ != nullptr)
        {
            used += ringBytesOf(slot.frames, maxBlock_);
        }
        if (slot.pendingBytes != nullptr)
        {
            used += slot.pendingBytes->size();
        }
        if (slot.pendingWindow != nullptr)
        {
            used += static_cast<uint64_t>(slot.pendingWindow->frames()) *
                    slot.pendingWindow->channels() * sizeof(float);
        }
        if (slot.offeredBytes != nullptr)
        {
            used += slot.offeredBytes->size();
        }
    }
    return used;
}

bool MediaSession::restoreInFlight(const core::Processor &processor, uint8_t layer) const noexcept
{
    const RingSlot &slot = rings_[layer];
    return slot.offeredEntry.has_value() && slot.offerPending && slot.offeredRestore &&
           processor.sources(layer).ringAdoptionPending() &&
           processor.ringAdoptionsDeclined(layer) == slot.declinesAtOffer;
}

void MediaSession::refreshBudget() noexcept { budget_.usedBytes = memoryUsedBytes(); }

fs::path MediaSession::capturesFolder() const
{
    const core::MachineSettings settings =
        settingsFile_.empty() ? core::MachineSettings{} : media::loadSettings(settingsFile_);
    if (!settings.capturesFolder.empty())
    {
        if (auto folder = media::fromUtf8(settings.capturesFolder))
        {
            return *folder;
        }
    }
    return media::defaultCapturesFolder();
}

RingSpanRequest MediaSession::windowOf(const core::ParamValues &values, uint8_t layer,
                                       uint32_t splice) const
{
    const core::RingParams ring = core::toRingParams(values, layer);
    RingSpanRequest request;
    request.loop = ring.loop;
    // As the Processor gives the ring its Loop Length.
    const double loopFrames = std::min(ring.loopSeconds * rate_, 0x1.0p40);
    request.loopFrames = std::max<int64_t>(1, std::llround(loopFrames));
    request.splice = splice;
    return request;
}

std::optional<MediaSession::SavedRing> MediaSession::readForSave(const core::Processor &processor,
                                                                 uint8_t layer,
                                                                 const core::ParamValues &values,
                                                                 bool &failed)
{
    failed = false;
    RingSlot &slot = rings_[layer];
    const RingSpanRequest request = windowOf(values, layer, 0);
    // The cache (spec §6.6), probed through the one reader: a frozen ring whose storage epoch,
    // head and window start are the ones encoded last isn't copied or hashed again. Only a frozen
    // one: nothing writes its frames while the head stands, where a live ring's every write moves
    // the head (so its key would change anyway), and a frozen ring is the spec's case.
    std::shared_ptr<SampleBuffer> buffer;
    const auto probe = [&slot](const core::RingMeta &meta, uint32_t e0, int64_t r0,
                               const Span &span) {
        return meta.frozen && slot.cached && slot.encoded != nullptr && slot.cacheEpoch == e0 &&
               slot.cacheHead == r0 && slot.cacheOldest == span.from;
    };
    const auto done = readWith(
        processor, layer, request, 0, probe,
        [&](const RingStorage &storage, int64_t from, int64_t to) {
            buffer =
                media::ringToBuffer(storage.slots(), storage.capacity(), from, to - from, rate_);
        },
        &failed);
    if (!done.has_value())
    {
        return std::nullopt; // no ring, an empty one, or one replaced under every try (failed)
    }
    if (!done->cached)
    {
        // The window keeps its length W: a front the head lapped during the copy is saved as
        // silence, as the resize's stable part is, so the loop's period and the marks' offsets
        // survive.
        const auto lapped = static_cast<std::size_t>(done->valid - done->from);
        for (uint32_t c = 0; c < 2; ++c)
        {
            std::fill_n(buffer->channel(c), lapped, 0.0f);
        }
        slot.encoded = std::make_shared<media::EncodedRing>(media::encodeRing(*buffer));
        ++ringsEncoded_;
        slot.cached = done->frozen;
        slot.cacheEpoch = done->epoch;
        slot.cacheHead = done->head;
        slot.cacheOldest = done->from;
    }
    SavedRing out;
    out.entry = windowEntry(*done, layer, rate_);
    out.entry.sha256Hex = slot.encoded->sha256Hex;
    out.entry.fileSize = slot.encoded->size;
    // The bytes are shared with the cache: aliasing the encoded ring's string.
    out.bytes = std::shared_ptr<const std::string>(slot.encoded, &slot.encoded->wavBytes);
    return out;
}

core::SessionState MediaSession::save(const core::ParamValues &values, uint64_t seed,
                                      const core::ModRoutes &routes,
                                      const core::Processor *active) noexcept
{
    core::SessionState state;
    state.values = values;
    state.seed = seed;
    state.routes = routes;
    state.memoryCapMiB = capMiB_;
    try
    {
        state.newerMedia = loaders_[0]->newerMedia();
        if (state.newerMedia.has_value())
        {
            state.newerAudio = newerAudio_;
        }
        uint64_t embedded = 0;
        uint32_t nextId = 1;
        // The embed rule (spec §6.6): in layer order, rings before captures; embedded while the
        // payloads stay within the cap, else filed as <sha256>.wav in the captures folder.
        // Returns the payload id (embedded), 0 with `path` set (filed), or kUnwritable. (A
        // plain value, not an optional: GCC 13's -Wmaybe-uninitialized.)
        constexpr int64_t kUnwritable = -1;
        bool haveFolder = false;
        fs::path folder; // the captures folder, read from the settings once per save
        const auto place = [&](const std::shared_ptr<const std::string> &bytes,
                               const std::string &sha256Hex, std::string &path) -> int64_t {
            if (embedded + bytes->size() <= core::kEmbedCapBytes)
            {
                embedded += bytes->size();
                state.audio.push_back({nextId, *bytes});
                return nextId++;
            }
            std::error_code error;
            // A copy collect and save made (collectAndSave) is where the project keeps it.
            for (const auto &[sha, copy] : collected_)
            {
                if (sha == sha256Hex && fs::is_regular_file(copy, error) &&
                    fs::file_size(copy, error) == bytes->size() && !error)
                {
                    path = utf8(copy);
                    return path.empty() ? kUnwritable : int64_t{0};
                }
            }
            if (!haveFolder)
            {
                folder = capturesFolder();
                haveFolder = true;
            }
            if (folder.empty())
            {
                return kUnwritable;
            }
            const fs::path file = folder / (sha256Hex + ".wav");
            const bool present = fs::is_regular_file(file, error) &&
                                 fs::file_size(file, error) == bytes->size() && !error;
            if (!present && !writeWhole(file, *bytes))
            {
                return kUnwritable;
            }
            path = utf8(file);
            return path.empty() ? kUnwritable : int64_t{0};
        };
        for (uint8_t l = 0; l < kLayers; ++l)
        {
            RingSlot &slot = rings_[l];
            // The ring (D1). A layer no longer held has nothing to say about its ring.
            if (!core::ringSaved(values, mode_, l))
            {
                slot.persistStatus.clear();
            }
            else
            {
                std::optional<SavedRing> ring;
                bool failed = false;
                if (active != nullptr && restoreInFlight(*active, l))
                {
                    // Restored, offered and not yet adopted: the Processor still plays the ring
                    // before it, which the restored values no longer describe.
                    ring = SavedRing{*slot.offeredEntry, slot.offeredBytes};
                }
                else if (active != nullptr)
                {
                    ring = readForSave(*active, l, values, failed);
                }
                else if (slot.pendingEntry.has_value())
                {
                    if (slot.pendingWindow != nullptr && slot.pendingBytes == nullptr)
                    {
                        // Kept at deactivate as frames: encoded and hashed once, here.
                        auto encoded = std::make_shared<media::EncodedRing>(
                            media::encodeRing(*slot.pendingWindow));
                        ++ringsEncoded_;
                        slot.pendingEntry->sha256Hex = encoded->sha256Hex;
                        slot.pendingEntry->fileSize = encoded->size;
                        slot.pendingBytes =
                            std::shared_ptr<const std::string>(encoded, &encoded->wavBytes);
                    }
                    ring = SavedRing{*slot.pendingEntry, slot.pendingBytes};
                }
                if (failed)
                {
                    slot.persistStatus = kRingReplaced;
                }
                else if (ring.has_value() && ring->bytes == nullptr)
                {
                    // Held for activation and filed: saved as it was.
                    state.rings[l] = ring->entry;
                }
                else if (ring.has_value())
                {
                    core::RingEntry entry = ring->entry;
                    entry.layer = l;
                    std::string path;
                    const int64_t id = place(ring->bytes, entry.sha256Hex, path);
                    if (id == kUnwritable)
                    {
                        slot.persistStatus = kCapturesUnwritable;
                    }
                    else
                    {
                        entry.embed = id != 0;
                        entry.audioId = static_cast<uint32_t>(id);
                        entry.absolutePath = path;
                        state.rings[l] = std::move(entry);
                        if (slot.persistStatus == kCapturesUnwritable ||
                            slot.persistStatus == kRingReplaced)
                        {
                            slot.persistStatus.clear();
                        }
                    }
                }
            }
            // The sample: a file's entry as it is, a capture's by the embed rule.
            const auto &entry = loaders_[l]->entry();
            if (!entry.has_value())
            {
                continue;
            }
            core::MediaEntry sample = *entry;
            sample.layer = l;
            if (sample.embed)
            {
                const auto &bytes = loaders_[l]->embeddedBytes();
                if (bytes == nullptr)
                {
                    continue; // its payload was missing at the restore: the reference is lost
                }
                std::string path;
                const int64_t id = place(bytes, sample.sha256Hex, path);
                if (id == kUnwritable)
                {
                    slot.persistStatus = kCapturesUnwritable;
                    continue;
                }
                sample.embed = id != 0;
                sample.audioId = static_cast<uint32_t>(id);
                sample.absolutePath = path;
                sample.relativePath.clear();
            }
            state.samples[l] = std::move(sample);
        }
    }
    catch (...)
    {
        // Memory: the session saves without its media rather than not at all (ruling R9).
        state.samples = {};
        state.rings = {};
        state.audio.clear();
    }
    return state;
}

MediaSession::Restored MediaSession::restore(core::SessionState state, double activeRate) noexcept
{
    Restored out;
    setMemoryCapMiB(state.memoryCapMiB);
    for (RingSlot &slot : rings_)
    {
        slot.persistStatus.clear();
        slot.captureStatus.clear();
        slot.pendingEntry.reset();
        slot.pendingBytes.reset();
        slot.pendingWindow.reset();
        slot.offeredEntry.reset();
        slot.offeredBytes.reset();
        slot.cached = false;
        slot.encoded.reset();
    }
    try
    {
        newerAudio_ = std::move(state.newerAudio);
        // The payloads, moved out of the state once and shared: each sample or ring that keeps
        // one aliases its string, so no payload is copied.
        const auto payloads =
            std::make_shared<const std::vector<core::AudioPayload>>(std::move(state.audio));
        const auto payloadOf = [&](uint32_t id) -> std::shared_ptr<const std::string> {
            for (const auto &p : *payloads)
            {
                if (p.id == id)
                {
                    return std::shared_ptr<const std::string>(payloads, &p.bytes);
                }
            }
            return nullptr;
        };
        // The loaders, in layer order, each checked against what those before it took.
        for (uint8_t l = 0; l < kLayers; ++l)
        {
            refreshBudget();
            const auto &entry = state.samples[l];
            std::shared_ptr<const std::string> payload;
            if (entry.has_value() && entry->embed)
            {
                payload = payloadOf(entry->audioId);
            }
            out.samples[l] = loaders_[l]->restore(entry, l == 0 ? state.newerMedia : std::nullopt,
                                                  std::move(payload));
        }
        // Then the rings.
        const core::MachineSettings settings =
            settingsFile_.empty() ? core::MachineSettings{} : media::loadSettings(settingsFile_);
        for (uint8_t l = 0; l < kLayers; ++l)
        {
            RingSlot &slot = rings_[l];
            if (!state.rings[l].has_value())
            {
                continue;
            }
            core::RingEntry entry = *state.rings[l];
            std::shared_ptr<const std::string> bytes;
            if (entry.embed)
            {
                bytes = state.ringAudioMissing[l] ? nullptr : payloadOf(entry.audioId);
                if (bytes == nullptr)
                {
                    slot.persistStatus = kEmbeddedMissing;
                    continue;
                }
            }
            else
            {
                const media::SearchResult found = media::findFiledAudio(
                    entry.sha256Hex, entry.fileSize, entry.absolutePath, settings);
                if (found.outcome != media::SearchOutcome::Found)
                {
                    slot.persistStatus = kRingNotFound; // the layer stays live (FR-8)
                    continue;
                }
                entry.absolutePath = utf8(found.path);
            }
            if (activeRate <= 0.0)
            {
                // Inactive: it waits for activate(), as a decoded sample does.
                slot.pendingEntry = std::move(entry);
                slot.pendingBytes = std::move(bytes);
                continue;
            }
            refreshBudget();
            const int64_t frames =
                framesFor(core::toRingParams(state.values, l).ringSeconds, activeRate);
            const uint64_t need = ringBytesOf(frames, maxBlock_);
            if (!budget_.fits(need))
            {
                slot.persistStatus = budget_.refusal(need);
                continue;
            }
            media::RestoredRing ring =
                media::restoreRing(entry, bytes != nullptr ? std::string_view(*bytes) : "",
                                   activeRate, frames, maxBlock_, decodeBound(budget_, need));
            if (ring.storage == nullptr)
            {
                slot.persistStatus = "ring not restored: " + ring.error;
                continue;
            }
            core::RingOffer how;
            how.restore = true;
            how.frames = ring.frames;
            how.frozen = core::freezeOn(state.values, l);
            how.spliceCount = static_cast<uint32_t>(
                std::min<std::size_t>(ring.splices.size(), core::kMaxSplices));
            std::copy_n(ring.splices.begin(), how.spliceCount, how.splices.begin());
            out.storages[l] = std::move(ring.storage);
            out.rings[l] = how;
            // The plugin offers it now: in flight until the audio thread adopts it, and saved as
            // restored meanwhile.
            slot.offeredEntry = std::move(entry);
            slot.offeredBytes = std::move(bytes);
            slot.frames = frames;
            slot.offerPending = true;
            slot.offeredRestore = true;
            slot.declinesAtOffer = active_ != nullptr ? active_->ringAdoptionsDeclined(l) : 0;
            slot.resizeStatus.clear();
            slot.refusedFrames = -1;
        }
    }
    catch (...)
    {
        // Memory: what didn't restore is left out; the loaders' statuses say what they did.
        for (uint8_t l = 0; l < kLayers; ++l)
        {
            if (out.storages[l] == nullptr)
            {
                out.rings[l].reset();
            }
        }
    }
    refreshBudget();
    return out;
}

void MediaSession::activate(core::Processor &processor, double sampleRate, uint32_t maxBlockSize)
{
    active_ = &processor;
    rate_ = sampleRate;
    maxBlock_ = maxBlockSize;
    for (uint8_t l = 0; l < kLayers; ++l)
    {
        RingSlot &slot = rings_[l];
        // A save's status was about the last Processor's ring; a restore's stands until the next
        // restore or the layer is no longer held.
        if (slot.persistStatus == kCapturesUnwritable || slot.persistStatus == kRingReplaced)
        {
            slot.persistStatus.clear();
        }
        const RingStorage *storage = processor.ringRead(l).storage;
        slot.frames = storage != nullptr ? storage->frames() : 0;
        slot.offerPending = false;
        slot.offeredRestore = false;
        slot.offeredEntry.reset();
        slot.offeredBytes.reset();
        slot.refusedFrames = -1;
        slot.resizeStatus.clear();
        slot.cached = false;
        slot.encoded.reset();
    }
    // The samples first, in layer order, then the rings, as a restore takes them.
    for (uint8_t l = 0; l < kLayers; ++l)
    {
        refreshBudget();
        processor.setSourceNow(l, loaders_[l]->activate(sampleRate));
    }
    for (uint8_t l = 0; l < kLayers; ++l)
    {
        RingSlot &slot = rings_[l];
        refreshBudget();
        if (slot.pendingEntry.has_value())
        {
            restoreHeld(processor, l);
            continue;
        }
        // Spec Review Focus 2: a Ring Length set while inactive applies before the first call.
        const int64_t target = framesFor(processor.ringSecondsRequested(l), sampleRate);
        if (target == slot.frames)
        {
            continue;
        }
        const uint64_t need = ringBytesOf(target, maxBlockSize);
        if (!budget_.fits(need))
        {
            slot.resizeStatus = budget_.refusal(need);
            slot.refusedFrames = target;
            continue;
        }
        // A fresh ring: no frame to copy (the head is at 0).
        if (processor.setRingNow(l, std::make_unique<RingStorage>(target, maxBlockSize),
                                 core::RingOffer{}))
        {
            slot.frames = target;
        }
    }
    refreshBudget();
}

void MediaSession::restoreHeld(core::Processor &processor, uint8_t layer)
{
    RingSlot &slot = rings_[layer];
    const core::RingEntry entry = std::move(*slot.pendingEntry);
    const std::shared_ptr<const std::string> bytes = std::move(slot.pendingBytes);
    std::shared_ptr<SampleBuffer> window = std::move(slot.pendingWindow);
    slot.pendingEntry.reset();
    slot.pendingBytes.reset();
    slot.pendingWindow.reset();
    refreshBudget();
    const int64_t frames = framesFor(processor.ringSecondsRequested(layer), rate_);
    const uint64_t need = ringBytesOf(frames, maxBlock_);
    if (!budget_.fits(need))
    {
        slot.persistStatus = budget_.refusal(need);
        return;
    }
    // A ring kept at deactivate is restored from its frames; one restored while inactive from
    // its WAV bytes or its file.
    media::RestoredRing ring =
        window != nullptr
            ? media::restoreRingFrom(entry, std::move(window), rate_, frames, maxBlock_)
            : media::restoreRing(entry, bytes != nullptr ? std::string_view(*bytes) : "", rate_,
                                 frames, maxBlock_, decodeBound(budget_, need));
    if (ring.storage == nullptr)
    {
        slot.persistStatus = "ring not restored: " + ring.error;
        return;
    }
    core::RingOffer how;
    how.restore = true;
    how.frames = ring.frames;
    how.frozen = processor.base(core::layerSlot(core::LayerParam::Freeze, layer)) > 0.5f;
    how.spliceCount =
        static_cast<uint32_t>(std::min<std::size_t>(ring.splices.size(), core::kMaxSplices));
    std::copy_n(ring.splices.begin(), how.spliceCount, how.splices.begin());
    if (processor.setRingNow(layer, std::move(ring.storage), how))
    {
        slot.frames = frames;
    }
}

void MediaSession::deactivate(const core::Processor *processor,
                              const core::ParamValues &values) noexcept
{
    for (uint8_t l = 0; l < kLayers; ++l)
    {
        RingSlot &slot = rings_[l];
        if (processor == nullptr || !core::ringSaved(values, mode_, l))
        {
            continue;
        }
        try
        {
            // The audio thread is stopped: the read passes at once. The frames are copied, not
            // encoded: the host's main thread waits for this, and four 300 s rings took seconds
            // to encode and hash. The next save() does that, or the next activation restores the
            // frames as they are.
            std::shared_ptr<SampleBuffer> window;
            const auto done =
                readWith(*processor, l, windowOf(values, l, 0), 0,
                         [&](const RingStorage &storage, int64_t from, int64_t to) {
                             window = media::ringToBuffer(storage.slots(), storage.capacity(), from,
                                                          to - from, rate_);
                         });
            if (done.has_value())
            {
                const auto lapped = static_cast<std::size_t>(done->valid - done->from);
                for (uint32_t c = 0; c < 2; ++c)
                {
                    std::fill_n(window->channel(c), lapped, 0.0f); // none: nothing writes
                }
                core::RingEntry entry = windowEntry(*done, l, rate_);
                std::shared_ptr<const std::string> bytes;
                if (done->frozen && slot.cached && slot.encoded != nullptr &&
                    slot.cacheEpoch == done->epoch && slot.cacheHead == done->head &&
                    slot.cacheOldest == done->from)
                {
                    // A save encoded this very window: its bytes are kept with it.
                    entry.sha256Hex = slot.encoded->sha256Hex;
                    entry.fileSize = slot.encoded->size;
                    bytes =
                        std::shared_ptr<const std::string>(slot.encoded, &slot.encoded->wavBytes);
                }
                slot.pendingEntry = std::move(entry);
                slot.pendingBytes = std::move(bytes);
                slot.pendingWindow = std::move(window);
            }
        }
        catch (...)
        {
            // Memory: the ring goes with its Processor, as before this sub-project.
        }
    }
    for (const auto &loader : loaders_)
    {
        loader->deactivate();
    }
    active_ = nullptr;
    rate_ = 0.0;
    for (RingSlot &slot : rings_)
    {
        slot.frames = 0;
        slot.offerPending = false;
        slot.offeredRestore = false;
        slot.offeredEntry.reset();
        slot.offeredBytes.reset();
        slot.refusedFrames = -1;
        slot.resizeStatus.clear();
        slot.cached = false;
        slot.encoded.reset();
    }
    refreshBudget();
}

void MediaSession::onMainThread(core::Processor *active) noexcept
{
    for (const auto &loader : loaders_)
    {
        loader->onMainThread();
    }
    if (active != nullptr)
    {
        pollRingLengths(*active);
    }
    refreshBudget();
}

void MediaSession::pollRingLengths(core::Processor &processor) noexcept
{
    for (uint8_t l = 0; l < kLayers; ++l)
    {
        RingSlot &slot = rings_[l];
        try
        {
            if (slot.offerPending)
            {
                if (processor.sources(l).ringAdoptionPending())
                {
                    if (processor.ringAdoptionsDeclined(l) == slot.declinesAtOffer)
                    {
                        continue; // in flight
                    }
                    // Declined (the copy's gap was past kAdoptCopyFrames): made again below.
                    const RingStorage *storage = processor.ringRead(l).storage;
                    slot.frames = storage != nullptr ? storage->frames() : 0;
                    if (slot.offeredRestore)
                    {
                        slot.persistStatus = "ring not restored: declined";
                    }
                }
                slot.offerPending = false;
                slot.offeredRestore = false;
                slot.offeredEntry.reset();
                slot.offeredBytes.reset();
            }
            const int64_t target = framesFor(processor.ringSecondsRequested(l), rate_);
            if (target == slot.frames)
            {
                slot.resizeStatus.clear();
                slot.refusedFrames = -1;
                continue;
            }
            if (target == slot.refusedFrames)
            {
                continue; // refused: not tried again until the request changes
            }
            if (target < slot.frames && processor.ringMeta(l).framesInUse > target)
            {
                // Ruling SP-T12ad: a shrink waits until the ring needs no more than `target` frames
                // (the audio thread narrows its reads once the grains allow, and the overdub's
                // period over 10 ms after).
                slot.resizeStatus = kResizing;
                continue;
            }
            refreshBudget();
            const uint64_t need = ringBytesOf(target, maxBlock_);
            if (!budget_.fits(need))
            {
                slot.resizeStatus = budget_.refusal(need);
                slot.refusedFrames = target;
                continue;
            }
            auto next = std::make_unique<RingStorage>(target, maxBlock_);
            // The stable part: the window's frames older than R0, at most the new window long,
            // frame f at f mod the new capacity (LiveSource::adoptStorage). A front the head
            // lapped during the copy is silence, as a young ring's is.
            RingStorage &into = *next;
            const auto done =
                readWith(processor, l, RingSpanRequest{}, target,
                         [&into](const RingStorage &from, int64_t first, int64_t end) {
                             // In runs, and the levels copied as the frames are, not computed
                             // (spec §6.9): the copy must end before the audio thread writes
                             // kAdoptCopyFrames more frames, or the offer is declined.
                             copyRingFrames(from.slots(), from.capacity(), into.slots(),
                                            into.capacity(), first, end);
                             // The level frames complete before the head R0 (end); the audio
                             // thread copies those completing from copiedTo = R0 on.
                             copyLevels(from, into, first, end);
                         });
            core::RingOffer how;
            if (done.has_value())
            {
                for (int64_t f = done->from; f < done->valid; ++f)
                {
                    const int64_t d = f % into.capacity();
                    into.slots()[2 * d] = 0.0f;
                    into.slots()[2 * d + 1] = 0.0f;
                }
                // The lapped front's level frames, which may hold newer frames' values, are
                // made again from what is there (spec §6.9), as a restore's are: a few frames.
                fillLevels(into, done->from, done->valid);
                how.copiedTo = done->head;
            }
            else
            {
                // An empty ring (or one replaced under every try): nothing older to keep; the
                // audio thread copies what it can.
                how.copiedTo = processor.ringRead(l).head->load(std::memory_order_acquire);
            }
            slot.declinesAtOffer = processor.ringAdoptionsDeclined(l);
            processor.sources(l).offerRing(std::move(next), how);
            slot.offerPending = true;
            slot.offeredRestore = false;
            slot.offeredEntry.reset();
            slot.offeredBytes.reset();
            slot.frames = target;
            slot.resizeStatus.clear();
            slot.refusedFrames = -1;
            slot.cached = false;
        }
        catch (...)
        {
            // Short enough for any std::string's own buffer: assigning it never allocates.
            slot.resizeStatus = "out of memory";
        }
    }
    refreshBudget();
}

std::shared_ptr<const SampleBuffer> MediaSession::captureRing(uint8_t layer, uint32_t splice,
                                                              const core::Processor &processor)
{
    RingSlot &slot = rings_[layer];
    core::ParamValues values{};
    if (baseValue_)
    {
        for (const auto p :
             {core::LayerParam::Loop, core::LayerParam::LoopLength, core::LayerParam::RingLength})
        {
            const auto s = core::layerSlot(p, layer);
            values[static_cast<std::size_t>(s)] = baseValue_(s);
        }
    }
    std::optional<RingCopy> copy;
    if (rate_ > 0.0)
    {
        copy = readRing(processor, layer, windowOf(values, layer, splice), rate_);
    }
    if (!copy.has_value())
    {
        slot.captureStatus = kNoRing;
        return nullptr;
    }
    slot.captureStatus.clear();
    return copy->window;
}

bool MediaSession::loadCapture(uint8_t targetLayer, std::shared_ptr<const SampleBuffer> buffer)
{
    if (targetLayer >= kLayers || buffer == nullptr)
    {
        return false;
    }
    media::EncodedRing encoded = media::encodeRing(*buffer);
    core::MediaEntry entry;
    entry.sha256Hex = encoded.sha256Hex;
    entry.fileSize = encoded.size;
    entry.format = "wav";
    entry.channels = buffer->channels();
    entry.sampleRate = buffer->sampleRate();
    entry.frames = buffer->frames();
    entry.embed = true;
    entry.layer = targetLayer;
    entry.audioId = 1; // renumbered at each save
    auto bytes = std::make_shared<const std::string>(std::move(encoded.wavBytes));
    refreshBudget();
    // prepare() never writes the buffer it is given: it shares it, or converts it into another.
    const bool loaded = loaders_[targetLayer]->loadInMemory(
        std::const_pointer_cast<SampleBuffer>(std::move(buffer)), std::move(entry),
        std::move(bytes));
    refreshBudget();
    return loaded;
}

bool MediaSession::saveCapture(const SampleBuffer &buffer, const std::filesystem::path &path)
{
    try
    {
        return writeWhole(path, media::encodeFloat32Wav(buffer));
    }
    catch (...)
    {
        return false;
    }
}

media::CollectResult MediaSession::collectAndSave(const std::filesystem::path &folder)
{
    // The save first (the controller's ruling): an active instance's filed rings and captures
    // are written to the captures folder as a save writes them, then travel with the project.
    core::ParamValues values{};
    if (baseValue_)
    {
        for (std::size_t s = 0; s < core::kParamCount; ++s)
        {
            values[s] = baseValue_(static_cast<core::ParamSlot>(s));
        }
    }
    core::SessionState state = save(values, 0, {}, active_);
    state.audio.clear(); // embedded audio needs no collecting
    const core::MachineSettings settings =
        settingsFile_.empty() ? core::MachineSettings{} : media::loadSettings(settingsFile_);
    // Sub-project 5 names the content roots (ContentPaths.h); none until then.
    media::CollectResult result = media::collectMedia(state, folder, settings, {});
    const auto remember = [this](const std::string &sha256Hex, const std::string &path) {
        if (const auto copy = media::fromUtf8(path))
        {
            std::erase_if(collected_, [&](const auto &c) { return c.first == sha256Hex; });
            collected_.emplace_back(sha256Hex, *copy);
        }
    };
    for (uint8_t l = 0; l < kLayers; ++l)
    {
        const auto &before = state.samples[l];
        const auto &after = result.state.samples[l];
        if (before.has_value() && after.has_value() && !before->embed &&
            after->absolutePath != before->absolutePath)
        {
            const auto &entry = loaders_[l]->entry();
            if (entry.has_value() && entry->embed)
            {
                remember(after->sha256Hex, after->absolutePath); // a capture, filed by the save
            }
            else
            {
                loaders_[l]->relocate(*after);
            }
        }
        const auto &ringBefore = state.rings[l];
        const auto &ringAfter = result.state.rings[l];
        if (ringBefore.has_value() && ringAfter.has_value() && !ringBefore->embed &&
            ringAfter->absolutePath != ringBefore->absolutePath)
        {
            remember(ringAfter->sha256Hex, ringAfter->absolutePath);
            RingSlot &slot = rings_[l];
            if (slot.pendingEntry.has_value() &&
                slot.pendingEntry->sha256Hex == ringAfter->sha256Hex)
            {
                slot.pendingEntry->absolutePath = ringAfter->absolutePath;
            }
        }
    }
    if (markDirty_)
    {
        markDirty_();
    }
    return result;
}

std::string MediaSession::status(uint8_t layer) const
{
    const RingSlot &slot = rings_[layer];
    if (active_ != nullptr && slot.offerPending && !slot.offeredRestore &&
        active_->sources(layer).ringAdoptionPending())
    {
        return kResizing;
    }
    for (const std::string *message :
         {&slot.resizeStatus, &slot.persistStatus, &slot.captureStatus})
    {
        if (!message->empty())
        {
            return *message;
        }
    }
    return loaders_[layer]->status().message;
}

namespace detail
{
void setRingReadHook(RingReadHook hook) { ringReadHook.store(hook); }
} // namespace detail
} // namespace gg::plugin
