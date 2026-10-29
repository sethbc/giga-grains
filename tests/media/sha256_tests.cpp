// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/media/Sha256.h"
#include "media/AudioFixtures.h"

namespace
{
std::string hashOf(std::string_view text)
{
    return gg::media::sha256Hex(std::as_bytes(std::span(text.data(), text.size())));
}
} // namespace

TEST_CASE("sha256 matches the FIPS 180-4 test vectors", "[media]")
{
    REQUIRE(hashOf("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(hashOf("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(hashOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    REQUIRE(hashOf("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnop"
                   "qklmnopqrlmnopqrsmnopqrstnopqrstu") ==
            "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
    REQUIRE(hashOf(std::string(1000000, 'a')) ==
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("hashFile hashes a file's bytes and reports its size", "[media]")
{
    const auto dir = gg::test::scratchDir("media-sha256");

    // A million 'a's: the known vector again, read in 64 KiB pieces with a partial last one.
    const std::vector<uint8_t> million(1000000, uint8_t('a'));
    const auto millionFile = dir / "million-a.bin";
    gg::test::writeFile(millionFile, million);
    const auto millionHash = gg::media::hashFile(millionFile);
    REQUIRE(millionHash.has_value());
    REQUIRE(millionHash->size == 1000000);
    REQUIRE(millionHash->sha256Hex ==
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    // Exactly two read pieces, with bytes that differ, against the in-memory hash.
    std::vector<uint8_t> bytes(2 * 65536);
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        bytes[i] = static_cast<uint8_t>((i * 131u) ^ (i >> 8));
    }
    const auto file = dir / "pattern.bin";
    gg::test::writeFile(file, bytes);
    const auto hash = gg::media::hashFile(file);
    REQUIRE(hash.has_value());
    REQUIRE(hash->size == bytes.size());
    REQUIRE(hash->sha256Hex == gg::media::sha256Hex(std::as_bytes(std::span(bytes))));

    const auto empty = dir / "empty.bin";
    gg::test::writeFile(empty, {});
    const auto emptyHash = gg::media::hashFile(empty);
    REQUIRE(emptyHash.has_value());
    REQUIRE(emptyHash->size == 0);
    REQUIRE(emptyHash->sha256Hex ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("hashFile of a missing file is nullopt", "[media]")
{
    const auto file = gg::test::scratchDir("media-sha256") / "no-such-file.bin";
    std::filesystem::remove(file);
    REQUIRE_FALSE(gg::media::hashFile(file).has_value());
}

TEST_CASE("hashFile stops once its cancel flag is set", "[media]")
{
    // The sample loader's prompt stop (carry-over [194]). A flag never set changes nothing.
    const auto file = gg::test::scratchDir("media-sha256") / "cancel.bin";
    gg::test::writeFile(file, std::vector<uint8_t>(3 * 65536, uint8_t('a')));
    std::atomic<bool> cancel{false};
    const auto hash = gg::media::hashFile(file, &cancel);
    const auto plain = gg::media::hashFile(file);
    REQUIRE(hash.has_value());
    REQUIRE(plain.has_value());
    CHECK(hash->sha256Hex == plain->sha256Hex);
    CHECK(hash->size == plain->size);
    cancel = true;
    CHECK_FALSE(gg::media::hashFile(file, &cancel).has_value());
}
