// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "Render.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <system_error>

#include <fmt/format.h>

#include "Signals.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/MachineSettings.h"
#include "gg/core/ModSources.h"
#include "gg/core/Processor.h"
#include "gg/core/SessionState.h"
#include "gg/engine/Engine.h"
#include "gg/media/Decode.h"
#include "gg/media/MemoryBudget.h"
#include "gg/media/RingAudio.h"
#include "gg/media/Sample.h"
#include "gg/media/Search.h"
#include "gg/metrics/WavIO.h"
#include "gg/source/RingStorage.h"
#include "gg/source/SampleBuffer.h"

namespace gg::render
{
namespace
{
/// Collects the grains spawned during one process() call, and the latest grainEndSample of all
/// grains so far. The engine calls the sink from inside process(), which is annotated
/// non-blocking, so the sink only copies into storage reserved up front and never allocates;
/// that keeps RTSan builds of the tests clean. The lines are formatted between process() calls.
struct GrainCollector
{
    /// A grain, with the layer that spawned it.
    struct Event
    {
        uint32_t layer;
        GrainSpec grain;
    };

    explicit GrainCollector(std::size_t capacity) { pending.reserve(capacity); }

    static void sink(void *ctx, uint32_t layer, const GrainSpec &g)
    {
        auto &self = *static_cast<GrainCollector *>(ctx);
        self.latestEnd = std::max(self.latestEnd, grainEndSample(g));
        if (self.pending.size() < self.pending.capacity())
        {
            self.pending.push_back({layer, g});
        }
        else
        {
            self.overflowed = true;
        }
    }

    std::vector<Event> pending;
    int64_t latestEnd{-1}; ///< -1 until a grain spawns
    bool overflowed{false};
};

std::shared_ptr<const SampleBuffer> toSampleBuffer(const gg::metrics::AudioData &audio)
{
    const auto channels = static_cast<uint32_t>(audio.channels.size());
    std::size_t frames = channels > 0 ? audio.channels[0].size() : 0;
    for (const auto &c : audio.channels)
    {
        frames = std::min(frames, c.size());
    }
    auto buffer =
        std::make_shared<SampleBuffer>(channels, static_cast<int64_t>(frames), audio.sampleRate);
    for (uint32_t c = 0; c < channels; ++c)
    {
        std::copy_n(audio.channels[c].begin(), frames, buffer->channel(c));
    }
    return buffer;
}

CloudParams toCloudParams(const StreamSpec &s)
{
    return {
        .densityHz = s.grainRateHz,
        .distribution = s.distribution,
        .grainDurationSec = s.grainDurationMs / 1000.0,
        .position = s.position,
        .spray = s.positionJitter,
        .pitchSemitones = s.pitchSemitones,
        .stereo = 0.0,
        .gain = static_cast<float>(s.gain),
        .quality = s.quality,
    };
}

/// The most grains one process() call of `frames` frames can spawn. The scheduler's own onsets
/// are at least CloudScheduler::kMinIntervalSamples (1/16) apart, less the rounding of t + Δ,
/// which is at most 2^-22 samples while t is below 2^32 (every render gg-render accepts is
/// shorter): fewer than frames / (1/16 − 2^-22) + 1 grains, at most 16 × frames + 5. A note-on
/// into an empty held set forces an onset at its grid boundary, however close to the one before
/// (spec §6.2), so the minimum interval doesn't hold there: such forced onsets add at most one
/// per boundary, frames / kControlGrid + 2 in a call. The code reserves
/// 16 × frames + frames / kControlGrid + 16: the extra slack over the derived + 7 is deliberate,
/// so the bound doesn't rest on that rounding analysis alone. The bound holds for a Processor
/// call too, which renders in pieces split at grid boundaries: the intervals are between
/// consecutive onsets, whichever piece they fall in.
std::size_t maxGrainsPerCall(uint32_t frames)
{
    const auto perFrame = static_cast<std::size_t>(1.0 / CloudScheduler::kMinIntervalSamples);
    return perFrame * frames + frames / gg::core::Processor::kControlGrid + 16;
}

/// Moves the grains collected during one process() call into eventLines, if it isn't null.
void takeEventLines(GrainCollector &grains, std::vector<std::string> *eventLines)
{
    if (eventLines != nullptr)
    {
        for (const GrainCollector::Event &e : grains.pending)
        {
            eventLines->push_back(formatEventLine(e.layer, e.grain));
        }
    }
    grains.pending.clear();
}

gg::metrics::AudioData renderSample(const RenderSpec &spec, std::vector<std::string> *eventLines)
{
    const auto source = toSampleBuffer(makeSignal(spec.source, spec.sampleRate, spec.seed));

    EngineConfig config;
    config.sampleRate = spec.sampleRate;
    config.maxBlockSize = spec.blockSize;
    config.seed = spec.seed;
    Engine engine(config);
    engine.setSource(source);
    engine.setCloudParams(toCloudParams(spec.stream));

    GrainCollector grains(maxGrainsPerCall(spec.blockSize));
    engine.setEventSink(&GrainCollector::sink, &grains);
    if (eventLines != nullptr)
    {
        eventLines->clear();
    }

    gg::metrics::AudioData out{spec.sampleRate, {{}, {}}};
    // Renders frames [from, to) into `out`, which must already hold `to` frames.
    const auto renderFrames = [&](std::size_t from, std::size_t to) {
        for (std::size_t done = from; done < to;)
        {
            const auto n = static_cast<uint32_t>(std::min<std::size_t>(spec.blockSize, to - done));
            float *const block[2] = {out.channels[0].data() + done, out.channels[1].data() + done};
            engine.process(block, 2, n);
            done += n;
            takeEventLines(grains, eventLines);
        }
    };

    // durationSeconds of stream activity.
    const int64_t duration = framesFor(spec.durationSeconds, spec.sampleRate);
    for (auto &channel : out.channels)
    {
        channel.resize(static_cast<std::size_t>(duration));
    }
    renderFrames(0, static_cast<std::size_t>(duration));

    // The release tail: with the stream off, render until every grain has finished, plus one
    // silent frame. The end comes from the grains, not from polling activeGrains() once per
    // block, so the length doesn't depend on blockSize.
    CloudParams off = toCloudParams(spec.stream);
    off.densityHz = 0.0; // off (ruling M1-a)
    engine.setCloudParams(off);
    const int64_t end = std::max(duration, grains.latestEnd + 1);
    // Defensive bound. No grain is longer than the stream's grain duration (at least 16 frames,
    // as the engine clamps it), and one that started on the last frame of the duration ends
    // floor(length) + 2 frames later (grainEndSample), so the tail never needs more. Hitting the
    // bound means the engine broke that promise, an internal error: gg-render then exits with
    // status 3.
    const double longestGrain =
        std::max(16.0, spec.stream.grainDurationMs / 1000.0 * spec.sampleRate);
    const int64_t tailBound = static_cast<int64_t>(longestGrain) + 2 + spec.blockSize;
    if (end - duration > tailBound)
    {
        throw std::logic_error(
            fmt::format("the release tail would last {} frames, more than the {}-frame bound",
                        end - duration, tailBound));
    }
    for (auto &channel : out.channels)
    {
        channel.resize(static_cast<std::size_t>(end));
    }
    renderFrames(static_cast<std::size_t>(duration), static_cast<std::size_t>(end));

    if (engine.activeGrains() != 0)
    {
        throw std::logic_error("grains are still sounding after the release tail");
    }
    if (grains.overflowed)
    {
        throw std::logic_error("more grains spawned in one block than the scheduler allows");
    }
    return out;
}

gg::core::ParamSlot slotNamed(const std::string &name)
{
    const auto slot = paramSlotNamed(name);
    if (!slot)
    {
        throw std::invalid_argument("no parameter is named " + name);
    }
    return *slot;
}

/// The notes as note events at absolute frames, in frame order: an On at each note's start and
/// an Off at its end, unless that is at or past `frames`. On one frame the Offs go first, so a
/// key that ends where it starts again stays held; otherwise the notes' order is kept.
struct TimedNote
{
    std::size_t frame;
    gg::core::NoteEvent event; ///< Its offset is set per call.
};

std::vector<TimedNote> timedNotes(const std::vector<NoteSpec> &notes, double sampleRate,
                                  std::size_t frames)
{
    using gg::core::NoteEventType;
    std::vector<TimedNote> timed;
    timed.reserve(2 * notes.size());
    for (const NoteSpec &n : notes)
    {
        const auto channel = static_cast<int16_t>(n.channel);
        const auto key = static_cast<int16_t>(n.key);
        const float velocity = static_cast<float>(n.velocity) / 127.0f;
        timed.push_back({static_cast<std::size_t>(framesFor(n.timeSeconds, sampleRate)),
                         {0, NoteEventType::On, channel, key, velocity}});
        const auto off =
            static_cast<std::size_t>(framesFor(n.timeSeconds + n.durationSeconds, sampleRate));
        if (off < frames)
        {
            timed.push_back({off, {0, NoteEventType::Off, channel, key, velocity}});
        }
    }
    std::stable_sort(timed.begin(), timed.end(), [](const TimedNote &a, const TimedNote &b) {
        const bool aOn = a.event.type == NoteEventType::On;
        const bool bOn = b.event.type == NoteEventType::On;
        return a.frame < b.frame || (a.frame == b.frame && !aOn && bOn);
    });
    return timed;
}

/// The layer (0 to 3) a letter "A" to "D" names, or `otherwise` for any other text (the parser has
/// already checked it: "same" or "all").
uint8_t layerOrElse(const std::string &letter, uint8_t otherwise)
{
    return letter.size() == 1 && letter[0] >= 'A' && letter[0] < 'A' + gg::core::kLayers
               ? static_cast<uint8_t>(letter[0] - 'A')
               : otherwise;
}

/// The routes in effect for the render: `routes` (the factory routes) with each of the spec's
/// routes set in its slot.
gg::core::ModRoutes routesWith(gg::core::ModRoutes routes, const std::vector<RouteSpec> &mod)
{
    for (const RouteSpec &r : mod)
    {
        gg::core::ModRoute route; // source None: an empty slot
        const auto source = gg::core::modSourceNamed(r.source);
        const auto curve = gg::core::modCurveNamed(r.curve);
        if (!source || !curve)
        {
            throw std::invalid_argument("no source or curve is named " + r.source + ", " + r.curve);
        }
        if (*source != gg::core::ModSource::None)
        {
            route = {
                .source = *source,
                .sourceLayer = layerOrElse(r.sourceLayer, gg::core::kSameLayer),
                .target = slotNamed(r.target),
                .targetLayer = layerOrElse(r.targetLayer, gg::core::kAllLayers),
                .depth = static_cast<float>(r.depth),
                .curve = *curve,
            };
        }
        routes.at(r.slot - 1u) = route;
    }
    return routes;
}

/// The controls as control events at absolute frames, in the spec's (time) order.
struct TimedControl
{
    std::size_t frame;
    gg::core::ControlEvent event; ///< Its offset is set per call.
};

std::vector<TimedControl> timedControls(const std::vector<ControlSpec> &controls, double sampleRate)
{
    using gg::core::ControlKind;
    std::vector<TimedControl> timed;
    timed.reserve(controls.size());
    for (const ControlSpec &c : controls)
    {
        const ControlKind kind = c.kind == "modWheel"     ? ControlKind::ModWheel
                                 : c.kind == "aftertouch" ? ControlKind::Aftertouch
                                                          : ControlKind::PitchBend;
        timed.push_back({static_cast<std::size_t>(framesFor(c.timeSeconds, sampleRate)),
                         {0, kind, static_cast<int16_t>(c.channel), static_cast<float>(c.value)}});
    }
    return timed;
}

/// The ring operations as events at absolute frames: the commands in the spec's (time) order, and
/// the captures apart, which cut the call loop.
struct TimedCommand
{
    std::size_t frame;
    gg::core::CommandEvent event; ///< Its offset is set per call.
};

struct TimedCapture
{
    std::size_t frame;
    const OperationSpec *op;
};

char letterOf(uint8_t layer) { return static_cast<char>('A' + layer); }

/// The bytes of the file at `path` (UTF-8), or BadInput.
std::string readWholeFile(const std::string &path)
{
    std::ifstream in(pathFromUtf8(path), std::ios::binary);
    if (!in)
    {
        throw BadInput(fmt::format("can't read {}", path));
    }
    std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (in.bad())
    {
        throw BadInput(fmt::format("can't read {}", path));
    }
    return bytes;
}

/// Writes `bytes` to `path` (UTF-8), making its folder; false on failure.
bool writeWholeFile(const std::filesystem::path &path, const std::string &bytes)
{
    std::error_code ignored;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), ignored);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    return !out.fail();
}

/// The window a capture or a save reads from a layer's ring.
struct RingWindow
{
    int64_t start{0};           ///< The oldest frame, in ring time.
    int64_t frames{0};          ///< 0 when the ring is empty.
    std::vector<int64_t> marks; ///< The marks inside the loop window, ascending, in ring time.
};

/// Layer `layer`'s ring as the main thread reads it between process() calls: the loop window
/// (the newest min(L, N) frames while Loop is on, else min(head, N)), or, for `splice` above 0
/// with a mark inside that window, the span of that splice (the marks inside it, oldest first;
/// a splice past the count is the last, whose span runs to the head). A save passes splice 0.
RingWindow ringWindow(const gg::core::Processor &processor, uint8_t layer, uint32_t splice,
                      double sampleRate)
{
    const gg::core::RingMeta meta = processor.ringMeta(layer);
    const gg::core::RingRead ring = processor.ringRead(layer);
    RingWindow w;
    const int64_t n = ring.storage != nullptr ? ring.storage->frames() : 0;
    const int64_t head = meta.head;
    // The window: the newest min(L, N) frames while Loop is on (L the Loop Length base in frames,
    // as the Processor hands it to LiveSource), else min(head, N). meta.windowFrames is not used:
    // it follows the Splice selection too, which neither a save nor splice 0 does.
    gg::core::ParamValues bases{};
    for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        bases[s] = processor.base(static_cast<gg::core::ParamSlot>(s));
    }
    const gg::core::RingParams ringParams = gg::core::toRingParams(bases, layer);
    int64_t window = n;
    if (ringParams.loop)
    {
        window = std::min(n, std::max<int64_t>(1, static_cast<int64_t>(std::llround(
                                                      ringParams.loopSeconds * sampleRate))));
    }
    w.frames = std::min(head, window);
    w.start = head - w.frames;
    // The marks inside the window, ascending; splices count from the oldest of them.
    for (uint32_t k = 0; k < meta.spliceCount; ++k)
    {
        if (meta.splices[k] >= w.start && meta.splices[k] <= head)
        {
            w.marks.push_back(meta.splices[k]);
        }
    }
    if (splice > 0 && !w.marks.empty())
    {
        const std::size_t kept = w.marks.size();
        const std::size_t k = std::min<std::size_t>(splice, kept);
        w.start = w.marks[k - 1];
        w.frames = (k < kept ? w.marks[k] : head) - w.start;
    }
    return w;
}

/// The ring's window as a stereo buffer at `sampleRate`.
std::shared_ptr<gg::SampleBuffer> ringBuffer(const gg::core::Processor &processor, uint8_t layer,
                                             const RingWindow &w, double sampleRate)
{
    const gg::core::RingRead ring = processor.ringRead(layer);
    return gg::media::ringToBuffer(ring.storage->slots(), ring.storage->capacity(), w.start,
                                   w.frames, sampleRate);
}

/// The levels a ring's storage holds, for the memory cap's accounting: level 0 alone until the
/// ring's pyramid (M2 sub-project 2's spec §6.9).
constexpr uint32_t kRingLevels = 1;

/// Writes the session at the render's end (RenderOptions::saveStatePath; the spec's D1 rule: a
/// layer's ring is kept when its Freeze or Loop base is on).
void saveSession(const gg::core::Processor &processor, const RenderSpec &spec, uint32_t capMiB,
                 const std::array<std::optional<gg::core::MediaEntry>, gg::core::kLayers> &entries,
                 const std::array<std::shared_ptr<gg::SampleBuffer>, gg::core::kLayers> &captured,
                 const RenderOptions &options)
{
    namespace fs = std::filesystem;
    gg::core::SessionState state;
    for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        state.values[s] = processor.base(static_cast<gg::core::ParamSlot>(s));
    }
    state.seed = spec.seed;
    state.routes = processor.routes();
    state.memoryCapMiB = capMiB;

    // The embed rule: layer by layer, a ring before a capture, embedded while the audio together
    // stays within the cap, else filed in the captures folder under its fingerprint.
    uint64_t embedded = 0;
    uint32_t nextId = 1;
    struct Placed
    {
        bool embed;
        uint32_t audioId;
        std::string path;
    };
    const auto place = [&](const gg::media::EncodedRing &encoded) -> Placed {
        if (embedded + encoded.size <= gg::core::kEmbedCapBytes)
        {
            embedded += encoded.size;
            const uint32_t id = nextId++;
            state.audio.push_back({id, encoded.wavBytes});
            return {true, id, {}};
        }
        if (options.capturesDir.empty())
        {
            throw BadInput(fmt::format("the session's audio passes the {} MiB embed limit: give "
                                       "--captures-dir for the rest",
                                       gg::core::kEmbedCapBytes >> 20));
        }
        const fs::path file =
            (fs::absolute(pathFromUtf8(options.capturesDir)) / (encoded.sha256Hex + ".wav"))
                .lexically_normal();
        std::error_code ec;
        if (!(fs::is_regular_file(file, ec) && fs::file_size(file, ec) == encoded.size) &&
            !writeWholeFile(file, encoded.wavBytes))
        {
            throw BadInput(fmt::format("can't write {}", utf8FromPath(file)));
        }
        return {false, 0, utf8FromPath(file)};
    };

    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        const auto base = [&](gg::core::LayerParam p) {
            return processor.base(gg::core::layerSlot(p, layer));
        };
        if (base(gg::core::LayerParam::Freeze) >= 0.5f || base(gg::core::LayerParam::Loop) >= 0.5f)
        {
            const RingWindow w = ringWindow(processor, layer, 0, spec.sampleRate);
            if (w.frames >= 1)
            {
                const auto buffer = ringBuffer(processor, layer, w, spec.sampleRate);
                const gg::media::EncodedRing encoded = gg::media::encodeRing(*buffer);
                const Placed placed = place(encoded);
                gg::core::RingEntry ring;
                ring.layer = layer;
                ring.frames = w.frames;
                ring.sampleRate = spec.sampleRate;
                for (const int64_t mark : w.marks)
                {
                    if (mark >= w.start && mark - w.start < w.frames)
                    {
                        ring.splices.push_back(mark - w.start);
                    }
                }
                ring.sha256Hex = encoded.sha256Hex;
                ring.fileSize = encoded.size;
                ring.embed = placed.embed;
                ring.audioId = placed.audioId;
                ring.absolutePath = placed.path;
                state.rings[layer] = std::move(ring);
            }
        }
        if (captured[layer])
        {
            const gg::media::EncodedRing encoded = gg::media::encodeRing(*captured[layer]);
            const Placed placed = place(encoded);
            gg::core::MediaEntry sample;
            sample.sha256Hex = encoded.sha256Hex;
            sample.fileSize = encoded.size;
            sample.absolutePath = placed.path;
            sample.format = "wav";
            sample.channels = captured[layer]->channels();
            sample.sampleRate = captured[layer]->sampleRate();
            sample.frames = captured[layer]->frames();
            sample.embed = placed.embed;
            sample.layer = layer;
            sample.audioId = placed.audioId;
            state.samples[layer] = std::move(sample);
        }
        else if (entries[layer])
        {
            state.samples[layer] = *entries[layer];
            state.samples[layer]->layer = layer;
        }
    }
    if (!writeWholeFile(pathFromUtf8(options.saveStatePath), gg::core::encodeState(state)))
    {
        throw BadInput(fmt::format("can't write {}", options.saveStatePath));
    }
}

/// Instrument and FX mode: the plugin's Processor, fed as a host feeds it.
gg::metrics::AudioData renderProcessor(const RenderSpec &spec, std::vector<std::string> *eventLines,
                                       const RenderOptions &options)
{
    const bool fx = spec.mode == RenderMode::Fx;
    const gg::core::Mode mode = fx ? gg::core::Mode::Fx : gg::core::Mode::Instrument;
    const auto &params = fx ? spec.fx.params : spec.instrument.params;
    const auto &automationPoints = fx ? spec.fx.automation : spec.instrument.automation;
    const auto &layerSamples = fx ? spec.fx.layerSamples : spec.instrument.layerSamples;
    const auto &mod = fx ? spec.fx.mod : spec.instrument.mod;
    const auto &transport = fx ? spec.fx.transport : spec.instrument.transport;
    const auto &statePath = fx ? spec.fx.statePath : spec.instrument.statePath;
    const auto &operations = fx ? spec.fx.operations : spec.instrument.operations;
    const auto &memoryCapMiB = fx ? spec.fx.memoryCapMiB : spec.instrument.memoryCapMiB;
    const auto frames = static_cast<std::size_t>(framesFor(spec.durationSeconds, spec.sampleRate));
    namespace fs = std::filesystem;

    // The FX's input, silent after its own length (and cut at the render's end); the instrument
    // has none.
    gg::metrics::AudioData input;
    if (fx)
    {
        input = makeInput(spec.fx.input, spec.fx.channels, spec.sampleRate, spec.seed);
        for (auto &channel : input.channels)
        {
            channel.resize(frames, 0.0f);
        }
    }
    const auto inputChannels = static_cast<uint32_t>(input.channels.size());

    // The session to restore, and the memory cap every sample and ring is checked against.
    std::optional<gg::core::SessionState> session;
    if (!statePath.empty())
    {
        const std::string bytes = readWholeFile(statePath);
        gg::core::SessionState decoded;
        if (bytes.size() > gg::core::kMaxStateBytes || !gg::core::decodeState(bytes, mode, decoded))
        {
            throw BadInput(fmt::format("{} isn't a session state this version reads", statePath));
        }
        session = std::move(decoded);
    }
    const uint32_t capMiB = memoryCapMiB ? *memoryCapMiB
                            : session    ? session->memoryCapMiB
                                         : gg::core::kMemoryCapDefaultMiB;
    gg::media::MemoryBudget budget{uint64_t{capMiB} << 20};
    const auto room = [&] {
        return budget.capBytes > budget.usedBytes ? budget.capBytes - budget.usedBytes : 0;
    };
    const auto charge = [&](uint64_t bytes) {
        if (!budget.fits(bytes))
        {
            throw BadInput(budget.refusal(bytes));
        }
        budget.usedBytes += bytes;
    };
    // The folders a restored state's media are found in: never searched by name.
    std::vector<fs::path> contentRoots;
    for (const std::string *dir : {&options.contentDir, &options.mediaDir})
    {
        if (!dir->empty())
        {
            contentRoots.push_back(fs::absolute(pathFromUtf8(*dir)).lexically_normal());
        }
    }
    gg::core::MachineSettings settings;
    settings.capturesFolder = !options.capturesDir.empty() ? options.capturesDir : options.mediaDir;

    gg::core::Processor processor({spec.sampleRate, spec.blockSize, spec.seed, mode});

    // What each layer's source costs the cap, and what a save needs to write it.
    std::array<uint64_t, gg::core::kLayers> sourceCost{};
    std::array<std::optional<gg::core::MediaEntry>, gg::core::kLayers> sampleEntries;
    std::array<std::shared_ptr<gg::SampleBuffer>, gg::core::kLayers> captured;
    const auto setSource = [&](uint8_t layer, std::shared_ptr<const PreparedSource> source) {
        const uint64_t cost = gg::media::sourceBytes(*source);
        budget.usedBytes -= sourceCost[layer];
        sourceCost[layer] = 0;
        charge(cost);
        sourceCost[layer] = cost;
        processor.setSourceNow(layer, std::move(source));
    };
    const bool saving = !options.saveStatePath.empty();
    // A sample file's manifest entry, for a save: by content path when it lies in a content root
    // or the media directory, else by its absolute path.
    // (A second decode of a file makeSample already loaded: makeSample keeps no manifest entry.)
    const auto fileEntry = [&](const std::string &path) {
        auto loaded = gg::media::loadSample(pathFromUtf8(path), settings, room());
        if (!loaded.decoded)
        {
            throw BadInput(loaded.error);
        }
        gg::core::MediaEntry entry = std::move(loaded.entry);
        const fs::path file = pathFromUtf8(entry.absolutePath);
        for (const fs::path &root : contentRoots)
        {
            const fs::path relative = file.lexically_relative(root);
            if (!relative.empty() && *relative.begin() != "..")
            {
                entry.contentPath = utf8FromPath(relative.generic_string());
                entry.absolutePath.clear();
                entry.relativePath.clear();
                break;
            }
        }
        return entry;
    };

    // 1. The state: its values and routes, then its samples.
    if (session)
    {
        processor.setBases(session->values);
        processor.setRoutes(session->routes);
        for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
        {
            if (!session->samples[layer])
            {
                continue;
            }
            const gg::core::MediaEntry &entry = *session->samples[layer];
            const std::string name = !entry.contentPath.empty() ? entry.contentPath
                                     : !entry.absolutePath.empty()
                                         ? utf8FromPath(pathFromUtf8(entry.absolutePath).filename())
                                         : "(embedded)";
            std::shared_ptr<gg::SampleBuffer> decoded;
            if (entry.embed)
            {
                const auto payload = std::find_if(
                    session->audio.begin(), session->audio.end(),
                    [&](const gg::core::AudioPayload &p) { return p.id == entry.audioId; });
                if (session->sampleAudioMissing[layer] || payload == session->audio.end())
                {
                    throw BadInput(fmt::format("layer {}'s embedded sample is missing from {}",
                                               letterOf(layer), statePath));
                }
                auto result = gg::media::decodeAudioBytes(payload->bytes, room());
                if (!result.audio)
                {
                    throw BadInput(fmt::format("layer {}'s embedded sample: {}", letterOf(layer),
                                               result.error));
                }
                decoded = result.audio->buffer;
            }
            else
            {
                const gg::media::SearchResult found =
                    gg::media::findSample(entry, settings, contentRoots);
                if (found.outcome != gg::media::SearchOutcome::Found)
                {
                    throw BadInput(fmt::format(
                        "layer {}'s sample {} {} (looked through --content-dir and the spec's "
                        "directory or --media-dir)",
                        letterOf(layer), name,
                        found.outcome == gg::media::SearchOutcome::Changed ? "has changed"
                                                                           : "was not found"));
                }
                auto loaded = gg::media::loadSample(found.path, settings, room(), std::nullopt);
                if (!loaded.decoded)
                {
                    throw BadInput(loaded.error);
                }
                decoded = std::move(loaded.decoded);
            }
            setSource(layer, gg::media::prepare(decoded, spec.sampleRate));
            if (entry.embed)
            {
                captured[layer] = decoded; // saved again, embedded or filed afresh
            }
            else
            {
                sampleEntries[layer] = entry;
            }
        }
    }

    // 2. The spec's samples, params, routes and transport. A layer's entry for a save is its
    // file's, or none for a built-in signal.
    const auto specSample = [&](uint8_t layer, const SourceSpec &sample) {
        setSource(layer, makeSample(sample, spec.sampleRate, spec.seed));
        captured[layer] = nullptr;
        sampleEntries[layer].reset();
        if (saving && sample.kind == SourceKind::File)
        {
            sampleEntries[layer] = fileEntry(sample.path);
        }
    };
    if (!fx && !spec.instrument.noSample)
    {
        specSample(0, spec.instrument.sample);
    }
    for (const auto &[layer, sample] : layerSamples)
    {
        specSample(layer, sample);
    }
    for (const auto &[name, value] : params)
    {
        processor.setBase(slotNamed(name), static_cast<float>(value));
    }
    if (!mod.empty())
    {
        processor.setRoutes(routesWith(processor.routes(), mod));
    }
    if (transport)
    {
        processor.setTransport({.hasBeats = true,
                                .beatPosition = transport->startBeat,
                                .tempoBpm = transport->tempoBpm,
                                .playing = transport->playing,
                                .beatsPerBar = 4});
    }

    // 3. The rings: restored, or sized by Ring Length, before the first call.
    {
        gg::core::ParamValues bases{};
        for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
        {
            bases[s] = processor.base(static_cast<gg::core::ParamSlot>(s));
        }
        for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
        {
            // N = ceil(seconds × sampleRate), as the engine sizes its ring.
            const auto ringFrames = static_cast<int64_t>(
                std::ceil(gg::core::toRingParams(bases, layer).ringSeconds * spec.sampleRate));
            charge(gg::media::ringBytes(ringFrames, spec.blockSize, kRingLevels));
            const bool saved = session && session->rings[layer].has_value();
            if (saved)
            {
                gg::core::RingEntry entry = *session->rings[layer];
                std::string wav;
                if (entry.embed)
                {
                    const auto payload = std::find_if(
                        session->audio.begin(), session->audio.end(),
                        [&](const gg::core::AudioPayload &p) { return p.id == entry.audioId; });
                    if (session->ringAudioMissing[layer] || payload == session->audio.end())
                    {
                        throw BadInput(fmt::format("layer {}'s embedded ring is missing from {}",
                                                   letterOf(layer), statePath));
                    }
                    wav = payload->bytes;
                }
                else
                {
                    const gg::media::SearchResult found = gg::media::findFiledAudio(
                        entry.sha256Hex, entry.fileSize, entry.absolutePath, settings);
                    if (found.outcome != gg::media::SearchOutcome::Found)
                    {
                        throw BadInput(fmt::format(
                            "layer {}'s ring file {} was not found (looked in --captures-dir, "
                            "or the spec's directory or --media-dir)",
                            letterOf(layer), entry.absolutePath));
                    }
                    entry.absolutePath = utf8FromPath(found.path);
                }
                auto restored = gg::media::restoreRing(entry, wav, spec.sampleRate, ringFrames,
                                                       spec.blockSize, room());
                if (!restored.storage)
                {
                    throw BadInput(
                        fmt::format("layer {}'s saved ring: {}", letterOf(layer), restored.error));
                }
                gg::core::RingOffer offer;
                offer.restore = true;
                offer.frames = restored.frames;
                offer.frozen = bases[static_cast<std::size_t>(gg::core::layerSlot(
                                   gg::core::LayerParam::Freeze, layer))] >= 0.5f;
                offer.spliceCount = static_cast<uint32_t>(restored.splices.size());
                std::copy(restored.splices.begin(), restored.splices.end(), offer.splices.begin());
                if (!processor.setRingNow(layer, std::move(restored.storage), offer))
                {
                    throw BadInput(fmt::format("layer {} refused its saved ring", letterOf(layer)));
                }
            }
            else
            {
                const gg::core::RingRead current = processor.ringRead(layer);
                if (current.storage == nullptr || current.storage->frames() != ringFrames)
                {
                    gg::core::RingOffer offer; // a resize from nothing: copiedTo 0
                    if (!processor.setRingNow(
                            layer, std::make_unique<gg::RingStorage>(ringFrames, spec.blockSize),
                            offer))
                    {
                        throw std::logic_error("a fresh ring was refused");
                    }
                }
            }
        }
    }

    // The automation as events at absolute frames, in time order.
    struct TimedEvent
    {
        std::size_t frame;
        gg::core::ParamSlot slot;
        double value;
    };
    std::vector<TimedEvent> automation;
    automation.reserve(automationPoints.size());
    for (const AutomationPoint &p : automationPoints)
    {
        automation.push_back({static_cast<std::size_t>(framesFor(p.timeSeconds, spec.sampleRate)),
                              slotNamed(p.param), p.value});
    }
    const std::vector<TimedNote> notes =
        timedNotes(fx ? spec.fx.notes : spec.instrument.notes, spec.sampleRate, frames);
    const std::vector<TimedControl> controls =
        timedControls(fx ? spec.fx.controls : spec.instrument.controls, spec.sampleRate);
    std::vector<TimedCommand> commands;
    std::vector<TimedCapture> captures;
    for (const OperationSpec &op : operations)
    {
        const auto frame = static_cast<std::size_t>(framesFor(op.timeSeconds, spec.sampleRate));
        if (op.op == "capture")
        {
            captures.push_back({frame, &op});
        }
        else
        {
            commands.push_back({frame,
                                {0,
                                 op.op == "splice" ? gg::core::CommandKind::AddSplice
                                                   : gg::core::CommandKind::ClearSplices,
                                 op.layer, static_cast<float>(op.behind)}});
        }
    }

    // Each layer's engine spawns within the bound on its own (M2 sub-project 1's spec §6.2).
    GrainCollector grains(gg::core::Processor::kLayers * maxGrainsPerCall(spec.blockSize));
    processor.setEventSink(&GrainCollector::sink, &grains);
    if (eventLines != nullptr)
    {
        eventLines->clear();
    }

    // A capture, between calls: the thread is the audio thread, so the ring can be read in place.
    const auto capture = [&](const OperationSpec &op) {
        const RingWindow w = ringWindow(processor, op.layer, op.splice, spec.sampleRate);
        if (w.frames < 1)
        {
            throw BadInput(fmt::format("nothing to capture at {} s: layer {}'s ring is empty",
                                       op.timeSeconds, letterOf(op.layer)));
        }
        const auto buffer = ringBuffer(processor, op.layer, w, spec.sampleRate);
        if (!op.file.empty())
        {
            gg::metrics::AudioData audio{spec.sampleRate, {{}, {}}};
            for (uint32_t c = 0; c < 2; ++c)
            {
                audio.channels[c].assign(buffer->channel(c), buffer->channel(c) + w.frames);
            }
            const fs::path file = pathFromUtf8(op.file);
            std::error_code ignored;
            if (file.has_parent_path())
            {
                fs::create_directories(file.parent_path(), ignored);
            }
            if (!gg::metrics::writeWavFloat32(op.file, audio))
            {
                throw BadInput(fmt::format("can't write {}", op.file));
            }
        }
        if (op.into)
        {
            setSource(*op.into, gg::media::prepare(buffer, spec.sampleRate));
            captured[*op.into] = buffer;
            sampleEntries[*op.into].reset();
        }
    };

    gg::metrics::AudioData out{spec.sampleRate,
                               {std::vector<float>(frames), std::vector<float>(frames)}};
    std::vector<gg::core::ParamEvent> events;
    events.reserve(automation.size());
    std::vector<gg::core::NoteEvent> noteEvents;
    noteEvents.reserve(notes.size());
    std::vector<gg::core::ControlEvent> controlEvents;
    controlEvents.reserve(controls.size());
    std::vector<gg::core::CommandEvent> commandEvents;
    commandEvents.reserve(commands.size());
    std::size_t next = 0;        // the first automation event not yet handed to the processor
    std::size_t nextNote = 0;    // the first note event not yet handed to it
    std::size_t nextControl = 0; // the first control event not yet handed to it
    std::size_t nextCommand = 0; // the first command not yet handed to it
    std::size_t nextCapture = 0; // the first capture not yet made
    for (std::size_t done = 0; done < frames;)
    {
        // The captures at this frame, between the calls that end and begin here.
        for (; nextCapture < captures.size() && captures[nextCapture].frame <= done; ++nextCapture)
        {
            capture(*captures[nextCapture].op);
        }
        auto n = static_cast<uint32_t>(std::min<std::size_t>(spec.blockSize, frames - done));
        if (nextCapture < captures.size())
        {
            n = std::min(n, static_cast<uint32_t>(captures[nextCapture].frame - done));
        }
        // This call's events, at their offsets from its first frame. The Processor merges the
        // kinds by offset.
        events.clear();
        for (; next < automation.size() && automation[next].frame < done + n; ++next)
        {
            const TimedEvent &e = automation[next];
            events.push_back({static_cast<uint32_t>(e.frame - done), e.slot, false, e.value});
        }
        noteEvents.clear();
        for (; nextNote < notes.size() && notes[nextNote].frame < done + n; ++nextNote)
        {
            gg::core::NoteEvent e = notes[nextNote].event;
            e.offset = static_cast<uint32_t>(notes[nextNote].frame - done);
            noteEvents.push_back(e);
        }
        controlEvents.clear();
        for (; nextControl < controls.size() && controls[nextControl].frame < done + n;
             ++nextControl)
        {
            gg::core::ControlEvent e = controls[nextControl].event;
            e.offset = static_cast<uint32_t>(controls[nextControl].frame - done);
            controlEvents.push_back(e);
        }
        commandEvents.clear();
        for (; nextCommand < commands.size() && commands[nextCommand].frame < done + n;
             ++nextCommand)
        {
            gg::core::CommandEvent e = commands[nextCommand].event;
            e.offset = static_cast<uint32_t>(commands[nextCommand].frame - done);
            commandEvents.push_back(e);
        }
        const float *const in[2] = {inputChannels > 0 ? input.channels[0].data() + done : nullptr,
                                    inputChannels > 1 ? input.channels[1].data() + done : nullptr};
        float *const block[2] = {out.channels[0].data() + done, out.channels[1].data() + done};
        processor.process(in, inputChannels, block, 2, n, events, noteEvents, controlEvents,
                          commandEvents);
        done += n;
        takeEventLines(grains, eventLines);
    }

    if (grains.overflowed)
    {
        throw std::logic_error("more grains spawned in one block than the scheduler allows");
    }
    if (saving)
    {
        saveSession(processor, spec, capMiB, sampleEntries, captured, options);
    }
    return out;
}
} // namespace

std::string formatEventLine(uint32_t layer, const GrainSpec &g)
{
    return fmt::format(
        R"({{"i":{},"layer":{},"onset":{},"frac":{},"pos":{},"rate":{},"dur":{},"tex":{},"gain":{},"pan":{}}})",
        g.index, layer, g.onsetSample, g.onsetFrac, g.startPos, g.rate, g.durationSamples,
        g.texture, g.gain, g.pan);
}

gg::metrics::AudioData render(const RenderSpec &spec, std::vector<std::string> *eventLines,
                              const RenderOptions &options)
{
    if (spec.mode == RenderMode::Sample)
    {
        if (!options.saveStatePath.empty())
        {
            throw BadInput("--save-state needs an instrument or FX spec: a sample-mode render has "
                           "no session");
        }
        return renderSample(spec, eventLines);
    }
    return renderProcessor(spec, eventLines, options);
}
} // namespace gg::render
