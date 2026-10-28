// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/Sha256.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <memory>

namespace gg::media
{
namespace
{
/// SHA-256 (FIPS 180-4 §6.2), fed in pieces of any length.
class Sha256
{
  public:
    void update(const uint8_t *data, size_t size)
    {
        length_ += size;
        while (size > 0)
        {
            const size_t take = std::min(size, block_.size() - used_);
            std::copy_n(data, take, block_.begin() + static_cast<std::ptrdiff_t>(used_));
            used_ += take;
            data += take;
            size -= take;
            if (used_ == block_.size())
            {
                compress();
                used_ = 0;
            }
        }
    }

    /// Pads the message (§5.1.1) and returns the digest as lowercase hex.
    std::string finishHex()
    {
        const uint64_t bits = length_ * 8;
        const uint8_t one = 0x80;
        update(&one, 1);
        const uint8_t zero = 0;
        while (used_ != 56)
        {
            update(&zero, 1);
        }
        std::array<uint8_t, 8> lengthBytes{};
        for (int i = 0; i < 8; ++i)
        {
            lengthBytes[size_t(i)] = static_cast<uint8_t>(bits >> (56 - 8 * i));
        }
        update(lengthBytes.data(), lengthBytes.size());

        static constexpr char kHex[] = "0123456789abcdef";
        std::string hex;
        hex.reserve(64);
        for (uint32_t word : state_)
        {
            for (int shift = 28; shift >= 0; shift -= 4)
            {
                hex.push_back(kHex[(word >> shift) & 0xfu]);
            }
        }
        return hex;
    }

  private:
    static constexpr std::array<uint32_t, 64> kRound{
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};

    static constexpr uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void compress()
    {
        std::array<uint32_t, 64> w{};
        for (size_t t = 0; t < 16; ++t)
        {
            w[t] = uint32_t(block_[4 * t]) << 24 | uint32_t(block_[4 * t + 1]) << 16 |
                   uint32_t(block_[4 * t + 2]) << 8 | uint32_t(block_[4 * t + 3]);
        }
        for (size_t t = 16; t < 64; ++t)
        {
            const uint32_t s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
            const uint32_t s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
            w[t] = w[t - 16] + s0 + w[t - 7] + s1;
        }

        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (size_t t = 0; t < 64; ++t)
        {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = h + s1 + ch + kRound[t] + w[t];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<uint32_t, 8> state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<uint8_t, 64> block_{};
    size_t used_{0};
    uint64_t length_{0};
};

constexpr size_t kReadBytes = 64 * 1024;
} // namespace

std::optional<FileHash> hashFile(const std::filesystem::path &path)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
    {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }
    Sha256 sha;
    uint64_t size = 0;
    const auto buffer = std::make_unique<char[]>(kReadBytes);
    while (in)
    {
        in.read(buffer.get(), static_cast<std::streamsize>(kReadBytes));
        const auto got = static_cast<size_t>(in.gcount());
        sha.update(reinterpret_cast<const uint8_t *>(buffer.get()), got);
        size += got;
    }
    // The loop ends at end-of-file (eof and fail set) or on a read error (bad set).
    if (in.bad() || !in.eof())
    {
        return std::nullopt;
    }
    return FileHash{sha.finishHex(), size};
}

std::string sha256Hex(std::span<const std::byte> bytes)
{
    Sha256 sha;
    sha.update(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
    return sha.finishHex();
}
} // namespace gg::media
