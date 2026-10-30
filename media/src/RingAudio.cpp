// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/RingAudio.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>

#include "gg/media/Convert.h"
#include "gg/media/Decode.h"
#include "gg/media/Sha256.h"
#include "gg/media/Utf8Path.h"
#include "gg/media/WavBytes.h"

namespace gg::media
{
std::shared_ptr<SampleBuffer> ringToBuffer(const float *slots, int64_t capacity, int64_t oldest,
                                           int64_t frames, double rate)
{
    if (slots == nullptr || capacity < 1 || oldest < 0 || frames < 0 || frames > capacity)
    {
        throw std::invalid_argument("ringToBuffer: a null ring, or a window outside it");
    }
    auto buffer = std::make_shared<SampleBuffer>(2, frames, rate);
    float *left = buffer->channel(0);
    float *right = buffer->channel(1);
    int64_t slot = oldest % capacity;
    for (int64_t k = 0; k < frames; ++k)
    {
        left[k] = slots[2 * slot];
        right[k] = slots[2 * slot + 1];
        slot = slot + 1 == capacity ? 0 : slot + 1;
    }
    return buffer;
}

EncodedRing encodeRing(const SampleBuffer &window)
{
    EncodedRing out;
    out.wavBytes = encodeFloat32Wav(window);
    out.sha256Hex = sha256Hex(std::as_bytes(std::span(out.wavBytes.data(), out.wavBytes.size())));
    out.size = out.wavBytes.size();
    return out;
}

namespace
{
RestoredRing failure(std::string reason)
{
    RestoredRing out;
    out.error = std::move(reason);
    return out;
}
} // namespace

RestoredRing restoreRing(const core::RingEntry &entry, std::string_view wavBytes, double rate,
                         int64_t ringFrames, uint32_t maxBlockSize, uint64_t maxDecodedBytes)
{
    if (!std::isfinite(rate) || rate <= 0.0 || ringFrames < 1)
    {
        return failure("the ring can't be restored: no rate or no length to restore it at");
    }
    try
    {
        DecodeResult decoded;
        if (!wavBytes.empty())
        {
            decoded = decodeAudioBytes(wavBytes, maxDecodedBytes);
        }
        else if (!entry.embed && !entry.absolutePath.empty())
        {
            const auto file = fromUtf8(entry.absolutePath);
            if (!file)
            {
                return failure("the ring's file path can't be read");
            }
            decoded = decodeAudioFile(*file, maxDecodedBytes);
        }
        else
        {
            return failure("the ring's audio is missing");
        }
        if (!decoded.audio)
        {
            return failure(decoded.error);
        }
        std::shared_ptr<SampleBuffer> audio = std::move(decoded.audio->buffer);
        if (audio->frames() != entry.frames)
        {
            return failure("the ring's audio holds " + std::to_string(audio->frames()) +
                           " frames, not the " + std::to_string(entry.frames) + " saved");
        }

        const double savedRate = audio->sampleRate();
        audio = convertRate(std::move(audio), rate);
        if (audio == nullptr)
        {
            return failure("the ring's rate conversion failed");
        }

        const int64_t frames = audio->frames();
        const int64_t kept = std::min(frames, ringFrames);
        const int64_t dropped = frames - kept;

        RestoredRing out;
        out.storage = std::make_unique<RingStorage>(ringFrames, maxBlockSize);
        float *slots = out.storage->slots();
        const float *left = audio->channel(0) + dropped;
        const float *right = audio->channel(audio->channels() > 1 ? 1 : 0) + dropped;
        for (int64_t k = 0; k < kept; ++k)
        {
            slots[2 * k] = left[k];
            slots[2 * k + 1] = right[k];
        }
        const int64_t capacity = out.storage->capacity();
        for (int64_t g = 0; g < RingStorage::kGuardFrames && g < capacity; ++g)
        {
            slots[2 * (capacity + g)] = slots[2 * g];
            slots[2 * (capacity + g) + 1] = slots[2 * g + 1];
        }
        out.frames = kept;

        const bool scaled = savedRate != rate;
        for (const int64_t mark : entry.splices)
        {
            const int64_t at = scaled ? std::llround(double(mark) * rate / savedRate) : mark;
            const int64_t moved = at - dropped;
            if (moved >= 0 && moved < kept && (out.splices.empty() || moved > out.splices.back()))
            {
                out.splices.push_back(moved);
            }
        }
        return out;
    }
    catch (const std::bad_alloc &)
    {
        return failure("not enough memory to restore the ring");
    }
    catch (const std::exception &e)
    {
        return failure(std::string("the ring can't be restored: ") + e.what());
    }
}
} // namespace gg::media
