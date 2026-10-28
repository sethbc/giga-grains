// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/SessionState.h"

#include <array>
#include <bit>
#include <cassert>
#include <charconv>
#include <cstring>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

#include "XmlText.h"
#include "gg/core/PatchState.h"
#include "tinyxml/tinyxml.h"

namespace gg::core
{
namespace
{
constexpr std::string_view kMagic = "GGST";
constexpr std::string_view kPatchChunk = "PTCH";
constexpr std::string_view kMediaChunk = "MEDI";
constexpr std::size_t kHeaderBytes = 8;       // the magic and the version
constexpr std::size_t kChunkHeaderBytes = 12; // the ID and the length
constexpr int kMediaVersion = 1;

bool isSha256Hex(std::string_view s)
{
    if (s.size() != 64)
    {
        return false;
    }
    for (const char c : s)
    {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        {
            return false;
        }
    }
    return true;
}

void appendLittleEndian(std::string &out, uint64_t v, std::size_t bytes)
{
    for (std::size_t i = 0; i < bytes; ++i)
    {
        out += static_cast<char>((v >> (8 * i)) & 0xffu);
    }
}

uint64_t readLittleEndian(std::string_view bytes, std::size_t at, std::size_t count)
{
    uint64_t v{0};
    for (std::size_t i = 0; i < count; ++i)
    {
        v |= uint64_t{static_cast<unsigned char>(bytes[at + i])} << (8 * i);
    }
    return v;
}

void appendChunk(std::string &out, std::string_view id, std::string_view payload)
{
    out += id;
    appendLittleEndian(out, payload.size(), 8);
    out += payload;
}

// An unsigned or signed integer in decimal, never locale-dependent.
template <typename T> std::string decimal(T v)
{
    std::array<char, 24> text{};
    const auto [end, ec] = std::to_chars(text.data(), text.data() + text.size(), v);
    (void)ec; // 20 characters at most: it always fits
    return std::string(text.data(), end);
}

// A double's bits as 16 lowercase hex digits.
std::string bitsToHex(double v)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    auto u = std::bit_cast<uint64_t>(v);
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i)
    {
        out[static_cast<std::size_t>(i)] = kDigits[u & 0xfu];
        u >>= 4;
    }
    return out;
}

std::string mediaXml(const std::optional<MediaEntry> &sample)
{
    TiXmlDocument doc;
    TiXmlElement media("media");
    media.SetAttribute("version", kMediaVersion);
    if (sample)
    {
        TiXmlElement e("sample");
        e.SetAttribute("sha256", sample->sha256Hex);
        e.SetAttribute("size", decimal(sample->fileSize));
        e.SetAttribute("path", escapeAmpersands(sample->absolutePath));
        e.SetAttribute("relative", escapeAmpersands(sample->relativePath));
        e.SetAttribute("format", escapeAmpersands(sample->format));
        e.SetAttribute("channels", decimal(sample->channels));
        e.SetAttribute("rate", bitsToHex(sample->sampleRate));
        e.SetAttribute("frames", decimal(sample->frames));
        e.SetAttribute("embed", "0"); // an entry that isValidMediaEntry, so never embedded
        media.InsertEndChild(e);
    }
    doc.InsertEndChild(media);
    std::ostringstream oss;
    oss << doc;
    return oss.str();
}

// The whole of `s` as a decimal T, or nothing.
template <typename T> std::optional<T> parseDecimal(const char *s)
{
    if (s == nullptr)
    {
        return std::nullopt;
    }
    const auto len = std::strlen(s);
    T v{};
    const auto [end, ec] = std::from_chars(s, s + len, v, 10);
    if (len == 0 || ec != std::errc{} || end != s + len)
    {
        return std::nullopt;
    }
    return v;
}

// The double written by bitsToHex: exactly 16 hex digits, or nothing.
std::optional<double> parseRate(const char *s)
{
    if (s == nullptr || std::strlen(s) != 16)
    {
        return std::nullopt;
    }
    uint64_t u{0};
    const auto [end, ec] = std::from_chars(s, s + 16, u, 16);
    if (ec != std::errc{} || end != s + 16)
    {
        return std::nullopt;
    }
    return std::bit_cast<double>(u);
}

std::optional<MediaEntry> parseSample(const TiXmlElement &e)
{
    const char *sha = e.Attribute("sha256");
    const auto size = parseDecimal<uint64_t>(e.Attribute("size"));
    const char *path = e.Attribute("path");
    const char *relative = e.Attribute("relative");
    const char *format = e.Attribute("format");
    const auto channels = parseDecimal<uint32_t>(e.Attribute("channels"));
    const auto rate = parseRate(e.Attribute("rate"));
    const auto frames = parseDecimal<int64_t>(e.Attribute("frames"));
    const char *embed = e.Attribute("embed");
    // embed="1" needs the AUDI chunk, which this version doesn't have.
    if (sha == nullptr || !isSha256Hex(sha) || !size || path == nullptr || relative == nullptr ||
        format == nullptr || !channels || !rate || !frames || *frames < 0 || embed == nullptr ||
        std::strcmp(embed, "0") != 0)
    {
        return std::nullopt;
    }
    MediaEntry entry;
    entry.sha256Hex = sha;
    entry.fileSize = *size;
    entry.absolutePath = path;
    entry.relativePath = relative;
    entry.format = format;
    entry.channels = *channels;
    entry.sampleRate = *rate;
    entry.frames = *frames;
    entry.embed = false;
    return entry;
}

// Parses a MEDI payload into `sample`: false if it isn't a manifest (decodeState lists the
// rules), and then `sample` is untouched.
bool parseMedia(std::string_view payload, std::optional<MediaEntry> &sample)
{
    if (payload.find('\0') != std::string_view::npos)
    {
        return false;
    }
    // Byte by byte, as PatchBase reads PTCH: tinyxml's UTF-8 mode would misread a path that
    // isn't valid UTF-8 (a lone lead byte swallows what follows it), and every byte but NUL comes
    // back unchanged this way.
    //
    // tinyxml stops quietly at top-level text it can't read (after the root, say), and returns
    // where it stopped; having read everything, it returns null.
    if (!xmlNestingWithin(payload))
    {
        return false; // tinyxml's recursion would overflow the stack
    }
    const std::string text(payload);
    TiXmlDocument doc;
    const char *stopped = doc.Parse(text.c_str(), nullptr, TIXML_ENCODING_UNKNOWN);
    if (doc.Error() || (stopped != nullptr && *stopped != '\0'))
    {
        return false;
    }
    // The <media> root, alone at the top level, at a version this build reads.
    const TiXmlElement *media = doc.RootElement();
    int version{0};
    if (media == nullptr || std::strcmp(media->Value(), "media") != 0 ||
        doc.FirstChild() != media || media->NextSibling() != nullptr ||
        media->QueryIntAttribute("version", &version) != TIXML_SUCCESS || version < 1 ||
        version > kMediaVersion)
    {
        return false;
    }
    const TiXmlElement *e = media->FirstChildElement("sample");
    if (e == nullptr)
    {
        sample.reset();
        return true;
    }
    if (e->NextSiblingElement("sample") != nullptr)
    {
        return false;
    }
    auto entry = parseSample(*e);
    if (!entry)
    {
        return false;
    }
    sample = std::move(entry);
    return true;
}
} // namespace

bool isValidMediaEntry(const MediaEntry &entry) noexcept
{
    const auto noNul = [](const std::string &s) { return s.find('\0') == std::string::npos; };
    return isSha256Hex(entry.sha256Hex) && noNul(entry.absolutePath) && noNul(entry.relativePath) &&
           noNul(entry.format) && entry.frames >= 0 && !entry.embed;
}

std::string encodeState(const SessionState &state)
{
    // An entry that wouldn't decode isn't written: losing the sample reference is better than
    // a state that fails to load and loses the whole session (ruling R9).
    assert(!state.sample || isValidMediaEntry(*state.sample));
    const bool writeSample = state.sample && isValidMediaEntry(*state.sample);
    const std::string patch = toStateXml(state.values, state.seed);
    const std::string media = mediaXml(writeSample ? state.sample : std::nullopt);

    std::string out;
    out.reserve(kHeaderBytes + 2 * kChunkHeaderBytes + patch.size() + media.size());
    out += kMagic;
    appendLittleEndian(out, kStateContainerVersion, 4);
    appendChunk(out, kPatchChunk, patch);
    appendChunk(out, kMediaChunk, media);
    return out;
}

bool decodeState(std::string_view bytes, Mode mode, SessionState &out,
                 uint32_t *migratedFromVersion)
{
    // Everything is decoded into these, and written to `out` only once all of it has loaded.
    SessionState loaded;
    uint32_t migrated{0};

    if (bytes.starts_with('<'))
    {
        // A pre-container state: PTCH alone, read exactly as before the container, so every
        // state that loaded then loads now (XML followed by a NUL included).
        if (!fromStateXml(bytes, loaded.values, loaded.seed, mode, &migrated))
        {
            return false;
        }
    }
    else
    {
        if (bytes.size() < kHeaderBytes || bytes.substr(0, kMagic.size()) != kMagic ||
            readLittleEndian(bytes, kMagic.size(), 4) != kStateContainerVersion)
        {
            return false;
        }
        std::optional<std::string_view> patch;
        std::optional<std::string_view> media;
        std::size_t at = kHeaderBytes;
        while (at < bytes.size())
        {
            if (bytes.size() - at < kChunkHeaderBytes)
            {
                return false;
            }
            const auto id = bytes.substr(at, 4);
            const uint64_t length = readLittleEndian(bytes, at + 4, 8);
            at += kChunkHeaderBytes;
            if (length > bytes.size() - at)
            {
                return false;
            }
            const auto payload = bytes.substr(at, static_cast<std::size_t>(length));
            at += static_cast<std::size_t>(length);

            // Chunks with other IDs are skipped.
            auto *slot = id == kPatchChunk ? &patch : id == kMediaChunk ? &media : nullptr;
            if (slot != nullptr)
            {
                if (slot->has_value())
                {
                    return false; // a repeated PTCH or MEDI
                }
                *slot = payload;
            }
        }
        if (!patch || !media || patch->find('\0') != std::string_view::npos ||
            !parseMedia(*media, loaded.sample) ||
            !fromStateXml(*patch, loaded.values, loaded.seed, mode, &migrated))
        {
            return false;
        }
    }

    out = std::move(loaded);
    if (migratedFromVersion != nullptr)
    {
        *migratedFromVersion = migrated;
    }
    return true;
}
} // namespace gg::core
