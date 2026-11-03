// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "RingReader.h"

#include <cstring>

#include "gg/media/RingAudio.h"

namespace gg::plugin
{
namespace
{
/// The ring reader's hook (PluginTestHooks.h); nullptr unless a test sets one.
std::atomic<detail::RingReadHook> ringReadHook{nullptr};
} // namespace

namespace detail
{
RingReadHook loadRingReadHook() noexcept { return ringReadHook.load(std::memory_order_relaxed); }

void setRingReadHook(RingReadHook hook) { ringReadHook.store(hook); }

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
} // namespace detail

std::optional<RingCopy> readRing(const core::Processor &processor, uint8_t layer,
                                 const RingSpanRequest &request, double rate)
{
    std::shared_ptr<SampleBuffer> buffer;
    const auto done = detail::readWith(
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
} // namespace gg::plugin
