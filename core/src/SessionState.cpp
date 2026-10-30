// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/SessionState.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <charconv>
#include <cstring>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

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
constexpr std::string_view kAudioChunk = "AUDI";
constexpr std::string_view kInstanceChunk = "INST";
constexpr std::size_t kHeaderBytes = 8;         // the magic and the version
constexpr std::size_t kChunkHeaderBytes = 12;   // the ID and the length
constexpr std::size_t kPayloadHeaderBytes = 12; // AUDI's: a payload's id and length
constexpr int kMediaVersion = 2;
constexpr int kInstanceVersion = 1;

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

bool noNul(const std::string &s) { return s.find('\0') == std::string::npos; }

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

// A layer's letter in MEDI: A for layer 0 to D for layer 3.
std::string layerLetter(uint8_t layer) { return std::string(1, static_cast<char>('A' + layer)); }

// The marks as written: decimal, one space between each.
std::string splicesText(const std::vector<int64_t> &splices)
{
    std::string out;
    for (const int64_t s : splices)
    {
        if (!out.empty())
        {
            out += ' ';
        }
        out += decimal(s);
    }
    return out;
}

bool hasId(const std::vector<uint32_t> &sortedIds, uint32_t id)
{
    return std::binary_search(sortedIds.begin(), sortedIds.end(), id);
}

// A <sample> of version 2 (isValidMediaEntry): version 1's attributes in its order, after the
// layer, then the content path and the payload's id.
TiXmlElement sampleXml(const MediaEntry &entry)
{
    TiXmlElement e("sample");
    e.SetAttribute("layer", layerLetter(entry.layer));
    e.SetAttribute("sha256", entry.sha256Hex);
    e.SetAttribute("size", decimal(entry.fileSize));
    e.SetAttribute("path", escapeAmpersands(entry.absolutePath));
    e.SetAttribute("relative", escapeAmpersands(entry.relativePath));
    e.SetAttribute("format", escapeAmpersands(entry.format));
    e.SetAttribute("channels", decimal(entry.channels));
    e.SetAttribute("rate", bitsToHex(entry.sampleRate));
    e.SetAttribute("frames", decimal(entry.frames));
    e.SetAttribute("embed", entry.embed ? "1" : "0");
    e.SetAttribute("content", escapeAmpersands(entry.contentPath));
    e.SetAttribute("audio", decimal(entry.audioId)); // 0 unless embedded
    return e;
}

// A <ring> (isValidRingEntry): its payload's id when embedded, its path when filed.
TiXmlElement ringXml(const RingEntry &entry)
{
    TiXmlElement e("ring");
    e.SetAttribute("layer", layerLetter(entry.layer));
    e.SetAttribute("frames", decimal(entry.frames));
    e.SetAttribute("rate", bitsToHex(entry.sampleRate));
    e.SetAttribute("splices", splicesText(entry.splices));
    e.SetAttribute("sha256", entry.sha256Hex);
    e.SetAttribute("size", decimal(entry.fileSize));
    e.SetAttribute("embed", entry.embed ? "1" : "0");
    if (entry.embed)
    {
        e.SetAttribute("audio", decimal(entry.audioId));
    }
    else
    {
        e.SetAttribute("path", escapeAmpersands(entry.absolutePath));
    }
    return e;
}

// MEDI version 2: the samples, then the rings, each in layer order (null for none).
std::string mediaXml(const std::array<const MediaEntry *, kLayers> &samples,
                     const std::array<const RingEntry *, kLayers> &rings)
{
    TiXmlDocument doc;
    TiXmlElement media("media");
    media.SetAttribute("version", kMediaVersion);
    for (const MediaEntry *sample : samples)
    {
        if (sample != nullptr)
        {
            media.InsertEndChild(sampleXml(*sample));
        }
    }
    for (const RingEntry *ring : rings)
    {
        if (ring != nullptr)
        {
            media.InsertEndChild(ringXml(*ring));
        }
    }
    doc.InsertEndChild(media);
    std::ostringstream oss;
    oss << doc;
    return oss.str();
}

// AUDI: the count, then each payload's id, length and bytes.
std::string audioChunk(const std::vector<const AudioPayload *> &payloads)
{
    std::size_t total = 4;
    for (const AudioPayload *p : payloads)
    {
        total += kPayloadHeaderBytes + p->bytes.size();
    }
    std::string out;
    out.reserve(total);
    appendLittleEndian(out, payloads.size(), 4);
    for (const AudioPayload *p : payloads)
    {
        appendLittleEndian(out, p->id, 4);
        appendLittleEndian(out, p->bytes.size(), 8);
        out += p->bytes;
    }
    return out;
}

// INST: the per-instance settings.
std::string instanceXml(uint32_t memoryCapMiB)
{
    TiXmlDocument doc;
    TiXmlElement instance("instance");
    instance.SetAttribute("version", kInstanceVersion);
    instance.SetAttribute("memoryCapMiB", decimal(memoryCapMiB));
    doc.InsertEndChild(instance);
    std::ostringstream oss;
    oss << doc;
    return oss.str();
}

// The whole of `s` as a decimal T, or nothing.
template <typename T> std::optional<T> parseDecimal(std::string_view s)
{
    T v{};
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v, 10);
    if (s.empty() || ec != std::errc{} || end != s.data() + s.size())
    {
        return std::nullopt;
    }
    return v;
}

template <typename T> std::optional<T> parseDecimal(const char *s)
{
    if (s == nullptr)
    {
        return std::nullopt;
    }
    return parseDecimal<T>(std::string_view(s));
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

// `embed` as written: "0" or "1".
std::optional<bool> parseFlag(const char *s)
{
    if (s == nullptr || (std::strcmp(s, "0") != 0 && std::strcmp(s, "1") != 0))
    {
        return std::nullopt;
    }
    return s[0] == '1';
}

// A layer's letter, A to D.
std::optional<uint8_t> parseLayer(const char *s)
{
    if (s == nullptr || s[0] < 'A' || s[0] >= 'A' + kLayers || s[1] != '\0')
    {
        return std::nullopt;
    }
    return static_cast<uint8_t>(s[0] - 'A');
}

// The marks as splicesText writes them, at most kMaxSplices, strictly ascending and below
// `frames`, or nothing.
std::optional<std::vector<int64_t>> parseSplices(const char *s, int64_t frames)
{
    if (s == nullptr)
    {
        return std::nullopt;
    }
    std::vector<int64_t> out;
    std::string_view rest(s);
    while (!rest.empty())
    {
        const auto space = rest.find(' ');
        const auto mark = parseDecimal<int64_t>(rest.substr(0, space));
        if (!mark || *mark < 0 || *mark >= frames || (!out.empty() && *mark <= out.back()) ||
            out.size() == kMaxSplices)
        {
            return std::nullopt;
        }
        out.push_back(*mark);
        if (space == std::string_view::npos)
        {
            break;
        }
        rest.remove_prefix(space + 1);
        if (rest.empty())
        {
            return std::nullopt; // a trailing space
        }
    }
    return out;
}

// A <sample>: version 1's attributes, and in version 2 its layer, content path and payload.
std::optional<MediaEntry> parseSample(const TiXmlElement &e, int version)
{
    const char *sha = e.Attribute("sha256");
    const auto size = parseDecimal<uint64_t>(e.Attribute("size"));
    const char *path = e.Attribute("path");
    const char *relative = e.Attribute("relative");
    const char *format = e.Attribute("format");
    const auto channels = parseDecimal<uint32_t>(e.Attribute("channels"));
    const auto rate = parseRate(e.Attribute("rate"));
    const auto frames = parseDecimal<int64_t>(e.Attribute("frames"));
    const auto embed = parseFlag(e.Attribute("embed"));
    if (sha == nullptr || !isSha256Hex(sha) || !size || path == nullptr || relative == nullptr ||
        format == nullptr || !channels || !rate || !frames || *frames < 0 || !embed)
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
    if (version == 1)
    {
        // Layer A's, and never embedded: embed="1" needs AUDI, which version 1 doesn't have.
        if (*embed)
        {
            return std::nullopt;
        }
        return entry;
    }
    const auto layer = parseLayer(e.Attribute("layer"));
    const char *content = e.Attribute("content");
    const auto audio = parseDecimal<uint32_t>(e.Attribute("audio"));
    if (!layer || content == nullptr || !audio || (*embed != (*audio != 0)))
    {
        return std::nullopt;
    }
    entry.embed = *embed;
    entry.layer = *layer;
    entry.contentPath = content;
    entry.audioId = *audio;
    return entry;
}

// A <ring> (version 2): an embedded one's payload id, a filed one's path.
std::optional<RingEntry> parseRing(const TiXmlElement &e)
{
    const auto layer = parseLayer(e.Attribute("layer"));
    const auto frames = parseDecimal<int64_t>(e.Attribute("frames"));
    const auto rate = parseRate(e.Attribute("rate"));
    const char *sha = e.Attribute("sha256");
    const auto size = parseDecimal<uint64_t>(e.Attribute("size"));
    const auto embed = parseFlag(e.Attribute("embed"));
    if (!layer || !frames || *frames < 0 || !rate || sha == nullptr || !isSha256Hex(sha) || !size ||
        !embed)
    {
        return std::nullopt;
    }
    auto splices = parseSplices(e.Attribute("splices"), *frames);
    if (!splices)
    {
        return std::nullopt;
    }
    RingEntry entry;
    entry.layer = *layer;
    entry.frames = *frames;
    entry.sampleRate = *rate;
    entry.splices = std::move(*splices);
    entry.sha256Hex = sha;
    entry.fileSize = *size;
    entry.embed = *embed;
    if (entry.embed)
    {
        const auto audio = parseDecimal<uint32_t>(e.Attribute("audio"));
        if (!audio || *audio == 0)
        {
            return std::nullopt;
        }
        entry.audioId = *audio;
    }
    else
    {
        const char *path = e.Attribute("path");
        if (path == nullptr)
        {
            return std::nullopt;
        }
        entry.absolutePath = path;
    }
    return entry;
}

// Parses `payload` into `doc` as one of gg-core's XML chunks: no NUL, nested at most 64 deep,
// well-formed, its only top-level node a `root` element with an integer `version` from 1 (the
// whole attribute, in decimal: "1x" isn't 1, as tinyxml's QueryIntAttribute would read it).
// Returns the root and sets `version`, or null.
const TiXmlElement *parseRoot(std::string_view payload, const char *root, TiXmlDocument &doc,
                              int &version)
{
    if (payload.find('\0') != std::string_view::npos)
    {
        return nullptr;
    }
    // Byte by byte, as PatchBase reads PTCH: tinyxml's UTF-8 mode would misread a path that
    // isn't valid UTF-8 (a lone lead byte swallows what follows it), and every byte but NUL comes
    // back unchanged this way.
    //
    // tinyxml stops quietly at top-level text it can't read (after the root, say), and returns
    // where it stopped; having read everything, it returns null.
    if (!xmlNestingWithin(payload))
    {
        return nullptr; // tinyxml's recursion would overflow the stack
    }
    const std::string text(payload);
    const char *stopped = doc.Parse(text.c_str(), nullptr, TIXML_ENCODING_UNKNOWN);
    if (doc.Error() || (stopped != nullptr && *stopped != '\0'))
    {
        return nullptr;
    }
    const TiXmlElement *element = doc.RootElement();
    if (element == nullptr || std::strcmp(element->Value(), root) != 0 ||
        doc.FirstChild() != element || element->NextSibling() != nullptr)
    {
        return nullptr;
    }
    const auto v = parseDecimal<int>(element->Attribute("version"));
    if (!v || *v < 1)
    {
        return nullptr;
    }
    version = *v;
    return element;
}

// What a MEDI payload lists.
struct Manifest
{
    std::array<std::optional<MediaEntry>, kLayers> samples{};
    std::array<std::optional<RingEntry>, kLayers> rings{};
    bool newer{false}; // a version above this build's: nothing else is read
};

// Parses a MEDI payload into `out`: false if it isn't a manifest (decodeState lists the rules),
// and then `out` is untouched. A manifest of a newer version than this build reads sets
// `out.newer` and lists nothing; its content isn't read past the root's version. Whether an
// embedded entry's payload exists is decodeState's to check, with AUDI.
bool parseMedia(std::string_view payload, Manifest &out)
{
    TiXmlDocument doc;
    int version{0};
    const TiXmlElement *media = parseRoot(payload, "media", doc, version);
    if (media == nullptr)
    {
        return false;
    }
    Manifest parsed;
    if (version > kMediaVersion)
    {
        parsed.newer = true;
    }
    else if (version == 1)
    {
        // At most one <sample>, layer A's.
        const TiXmlElement *e = media->FirstChildElement("sample");
        if (e != nullptr)
        {
            if (e->NextSiblingElement("sample") != nullptr)
            {
                return false;
            }
            parsed.samples[0] = parseSample(*e, version);
            if (!parsed.samples[0])
            {
                return false;
            }
        }
    }
    else
    {
        // At most one <sample> and one <ring> per layer, in any order; other elements ignored.
        for (const TiXmlElement *e = media->FirstChildElement(); e != nullptr;
             e = e->NextSiblingElement())
        {
            if (std::strcmp(e->Value(), "sample") == 0)
            {
                auto entry = parseSample(*e, version);
                if (!entry || parsed.samples[entry->layer])
                {
                    return false;
                }
                parsed.samples[entry->layer] = std::move(entry);
            }
            else if (std::strcmp(e->Value(), "ring") == 0)
            {
                auto entry = parseRing(*e);
                if (!entry || parsed.rings[entry->layer])
                {
                    return false;
                }
                parsed.rings[entry->layer] = std::move(entry);
            }
        }
    }
    out = std::move(parsed);
    return true;
}

// Whether `payload` is a manifest of a newer version than this build reads, as decodeState
// would keep it.
bool isNewerMedia(std::string_view payload)
{
    Manifest m;
    return parseMedia(payload, m) && m.newer;
}

// Parses an AUDI payload into `out`, (id, bytes) in its order: false unless it is exactly its
// count's payloads, each id above 0 and unique, and then `out` is untouched. The bytes are views
// into `payload`.
bool parseAudio(std::string_view payload, std::vector<std::pair<uint32_t, std::string_view>> &out)
{
    if (payload.size() < 4)
    {
        return false;
    }
    const uint64_t count = readLittleEndian(payload, 0, 4);
    std::vector<std::pair<uint32_t, std::string_view>> parsed;
    std::vector<uint32_t> ids;
    std::size_t at = 4;
    for (uint64_t i = 0; i < count; ++i)
    {
        // Each payload takes at least its header, so a count the bytes can't hold fails here
        // before anything grows with it.
        if (payload.size() - at < kPayloadHeaderBytes)
        {
            return false;
        }
        const auto id = static_cast<uint32_t>(readLittleEndian(payload, at, 4));
        const uint64_t length = readLittleEndian(payload, at + 4, 8);
        at += kPayloadHeaderBytes;
        if (id == 0 || length > payload.size() - at)
        {
            return false;
        }
        parsed.emplace_back(id, payload.substr(at, static_cast<std::size_t>(length)));
        ids.push_back(id);
        at += static_cast<std::size_t>(length);
    }
    if (at != payload.size())
    {
        return false; // bytes after the last payload
    }
    std::sort(ids.begin(), ids.end());
    if (std::adjacent_find(ids.begin(), ids.end()) != ids.end())
    {
        return false; // an id used twice
    }
    out = std::move(parsed);
    return true;
}

// INST's `memoryCapMiB`: an integer clamped to the floor and ceiling (one too large for int64
// clamped by its sign); absent, or not an integer, the default.
uint32_t parseMemoryCap(const char *s)
{
    if (s == nullptr)
    {
        return kMemoryCapDefaultMiB;
    }
    const std::string_view text(s);
    const std::string_view digits = text.starts_with('-') ? text.substr(1) : text;
    if (digits.empty() || !std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; }))
    {
        return kMemoryCapDefaultMiB;
    }
    const bool negative = digits.size() != text.size();
    int64_t v{0};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v, 10);
    (void)end; // every byte is a digit or the sign: from_chars reads them all
    if (ec == std::errc::result_out_of_range)
    {
        return negative ? kMemoryCapMinMiB : kMemoryCapMaxMiB;
    }
    return static_cast<uint32_t>(std::clamp<int64_t>(v, kMemoryCapMinMiB, kMemoryCapMaxMiB));
}

// Parses an INST payload's memory cap into `memoryCapMiB`: false if it isn't the settings
// (decodeState lists the rules), and then `memoryCapMiB` is untouched. A newer version's known
// attributes are read the same way.
bool parseInstance(std::string_view payload, uint32_t &memoryCapMiB)
{
    TiXmlDocument doc;
    int version{0};
    const TiXmlElement *instance = parseRoot(payload, "instance", doc, version);
    if (instance == nullptr)
    {
        return false;
    }
    memoryCapMiB = parseMemoryCap(instance->Attribute("memoryCapMiB"));
    return true;
}
} // namespace

bool isValidMediaEntry(const MediaEntry &entry) noexcept
{
    return isSha256Hex(entry.sha256Hex) && noNul(entry.absolutePath) && noNul(entry.relativePath) &&
           noNul(entry.format) && noNul(entry.contentPath) && entry.frames >= 0 &&
           entry.layer < kLayers && entry.embed == (entry.audioId != 0);
}

bool isValidRingEntry(const RingEntry &entry) noexcept
{
    if (entry.layer >= kLayers || entry.frames < 0 || entry.channels != 2 ||
        entry.splices.size() > kMaxSplices || !isSha256Hex(entry.sha256Hex))
    {
        return false;
    }
    for (std::size_t i = 0; i < entry.splices.size(); ++i)
    {
        const int64_t s = entry.splices[i];
        if (s < 0 || s >= entry.frames || (i > 0 && s <= entry.splices[i - 1]))
        {
            return false;
        }
    }
    // Embedded: the payload's id, and no path, which isn't written. Filed: the path.
    return entry.embed ? entry.audioId != 0 && entry.absolutePath.empty()
                       : entry.audioId == 0 && noNul(entry.absolutePath);
}

std::string encodeState(const SessionState &state)
{
    // An entry or payload that wouldn't decode isn't written: losing one reference is better
    // than a state that fails to load and loses the whole session (ruling R9).
    std::vector<const AudioPayload *> payloads;
    std::vector<uint32_t> ids; // sorted
    for (const AudioPayload &p : state.audio)
    {
        const auto at = std::lower_bound(ids.begin(), ids.end(), p.id);
        const bool writable = p.id != 0 && (at == ids.end() || *at != p.id);
        assert(writable);
        if (writable)
        {
            payloads.push_back(&p);
            ids.insert(at, p.id);
        }
    }
    bool anyEntry{false};
    std::array<const MediaEntry *, kLayers> samples{};
    std::array<const RingEntry *, kLayers> rings{};
    for (uint8_t k = 0; k < kLayers; ++k)
    {
        if (const auto &e = state.samples[k])
        {
            anyEntry = true;
            const bool writable =
                isValidMediaEntry(*e) && e->layer == k && (!e->embed || hasId(ids, e->audioId));
            // One decodeState loaded without its payload is left out in every build.
            assert(writable || state.sampleAudioMissing[k]);
            samples[k] = writable ? &*e : nullptr;
        }
        if (const auto &r = state.rings[k])
        {
            anyEntry = true;
            const bool writable =
                isValidRingEntry(*r) && r->layer == k && (!r->embed || hasId(ids, r->audioId));
            assert(writable || state.ringAudioMissing[k]);
            rings[k] = writable ? &*r : nullptr;
        }
    }
    const std::string patch = toStateXml(state.values, state.seed, state.routes);
    // With no entry, a newer build's manifest goes back as it came (INTENT §18), and the AUDI
    // beside it, which it may embed audio from.
    const bool writeNewer = !anyEntry && state.newerMedia && isNewerMedia(*state.newerMedia);
    const std::string media = writeNewer ? *state.newerMedia : mediaXml(samples, rings);
    const bool newerAudio = writeNewer && state.newerAudio.has_value();
    const bool writeAudio = newerAudio || !payloads.empty();
    const std::string audio = newerAudio   ? *state.newerAudio
                              : writeAudio ? audioChunk(payloads)
                                           : std::string{};
    const std::string instance =
        instanceXml(std::clamp(state.memoryCapMiB, kMemoryCapMinMiB, kMemoryCapMaxMiB));

    std::string out;
    out.reserve(kHeaderBytes + 4 * kChunkHeaderBytes + patch.size() + media.size() + audio.size() +
                instance.size());
    out += kMagic;
    appendLittleEndian(out, kStateContainerVersion, 4);
    appendChunk(out, kPatchChunk, patch);
    appendChunk(out, kMediaChunk, media);
    if (writeAudio)
    {
        appendChunk(out, kAudioChunk, audio);
    }
    appendChunk(out, kInstanceChunk, instance);
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
        if (!fromStateXml(bytes, loaded.values, loaded.seed, loaded.routes, mode, &migrated))
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
        std::optional<std::string_view> audio;
        std::optional<std::string_view> instance;
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
            auto *slot = id == kPatchChunk      ? &patch
                         : id == kMediaChunk    ? &media
                         : id == kAudioChunk    ? &audio
                         : id == kInstanceChunk ? &instance
                                                : nullptr;
            if (slot != nullptr)
            {
                if (slot->has_value())
                {
                    return false; // a repeated PTCH, MEDI, AUDI or INST
                }
                *slot = payload;
            }
        }
        Manifest manifest;
        if (!patch || !media || patch->find('\0') != std::string_view::npos ||
            (instance && !parseInstance(*instance, loaded.memoryCapMiB)) ||
            !parseMedia(*media, manifest))
        {
            return false;
        }
        // Beside a newer manifest AUDI is kept unread: it carries no version, and a newer build's
        // may be laid out otherwise.
        std::vector<std::pair<uint32_t, std::string_view>> payloads;
        if (audio && !manifest.newer && !parseAudio(*audio, payloads))
        {
            return false;
        }
        // An embedded entry whose payload isn't there loads, marked missing; those named and
        // found are kept, in AUDI's order.
        std::vector<uint32_t> named;
        for (uint8_t k = 0; k < kLayers; ++k)
        {
            const auto &s = manifest.samples[k];
            const auto &r = manifest.rings[k];
            const auto found = [&](uint32_t id) {
                return std::ranges::any_of(payloads, [&](const auto &p) { return p.first == id; });
            };
            if (s && s->embed)
            {
                loaded.sampleAudioMissing[k] = !found(s->audioId);
                named.push_back(s->audioId);
            }
            if (r && r->embed)
            {
                loaded.ringAudioMissing[k] = !found(r->audioId);
                named.push_back(r->audioId);
            }
        }
        if (!fromStateXml(*patch, loaded.values, loaded.seed, loaded.routes, mode, &migrated))
        {
            return false;
        }
        for (const auto &[id, payload] : payloads)
        {
            if (std::ranges::find(named, id) != named.end())
            {
                loaded.audio.push_back({id, std::string(payload)});
            }
        }
        loaded.samples = std::move(manifest.samples);
        loaded.rings = std::move(manifest.rings);
        if (manifest.newer)
        {
            loaded.newerMedia.emplace(*media);
            if (audio)
            {
                loaded.newerAudio.emplace(*audio);
            }
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
