// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_TESTS_SUPPORT_STATECONTAINER_H
#define GG_TESTS_SUPPORT_STATECONTAINER_H

// Builds and takes apart the session-state container (gg/core/SessionState.h) byte by byte, so
// the tests can write states encodeState never would: unknown chunks, missing ones, other
// versions and magics, edited payloads, AUDI's binary payload lists and INST's settings.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace gg::test
{
/// `v`'s low `bytes` bytes, little-endian.
inline std::string littleEndian(uint64_t v, std::size_t bytes)
{
    std::string out;
    for (std::size_t i = 0; i < bytes; ++i)
    {
        out += static_cast<char>((v >> (8 * i)) & 0xffu);
    }
    return out;
}

/// One chunk: a four-character ID, the payload's length as a little-endian u64, the payload.
inline std::string stateChunk(std::string_view id, std::string_view payload)
{
    std::string out(id);
    out += littleEndian(payload.size(), 8);
    out += payload;
    return out;
}

/// A container: `magic`, the little-endian u32 `version`, then `chunks` as they are.
inline std::string stateContainer(std::initializer_list<std::string> chunks, uint32_t version = 1,
                                  std::string_view magic = "GGST")
{
    std::string out(magic);
    out += littleEndian(version, 4);
    for (const auto &c : chunks)
    {
        out += c;
    }
    return out;
}

/// An AUDI payload: the little-endian u32 `count` (the payloads' number unless given), then each
/// payload's little-endian u32 id, u64 length and bytes.
inline std::string
audioChunkPayload(std::initializer_list<std::pair<uint32_t, std::string>> payloads,
                  std::optional<uint32_t> count = std::nullopt)
{
    std::string out = littleEndian(count.value_or(static_cast<uint32_t>(payloads.size())), 4);
    for (const auto &[id, bytes] : payloads)
    {
        out += littleEndian(id, 4);
        out += littleEndian(bytes.size(), 8);
        out += bytes;
    }
    return out;
}

/// An INST payload as encodeState writes it, with `memoryCapMiB` as its attribute's text.
inline std::string instanceChunkPayload(std::string_view memoryCapMiB = "1024")
{
    return R"(<instance version="1" memoryCapMiB=")" + std::string(memoryCapMiB) + R"(" />)";
}

/// The payload of the first chunk `id` in a well-formed container, or nothing.
inline std::optional<std::string> stateChunkPayload(std::string_view container, std::string_view id)
{
    std::size_t at = 8;
    while (at + 12 <= container.size())
    {
        uint64_t length = 0;
        for (std::size_t i = 0; i < 8; ++i)
        {
            length |= uint64_t{static_cast<unsigned char>(container[at + 4 + i])} << (8 * i);
        }
        if (length > container.size() - at - 12)
        {
            return std::nullopt;
        }
        if (container.substr(at, 4) == id)
        {
            return std::string(container.substr(at + 12, static_cast<std::size_t>(length)));
        }
        at += 12 + static_cast<std::size_t>(length);
    }
    return std::nullopt;
}
} // namespace gg::test

#endif // GG_TESTS_SUPPORT_STATECONTAINER_H
