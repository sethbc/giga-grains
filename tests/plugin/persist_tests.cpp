// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The plugin's media session (M2 sub-project 2's spec §6.3–§6.8), through the plugin as a host
// drives it: rings saved and restored with the state (D1, D8), the embed rule and the captures
// folder, the Ring Length watcher, the memory cap, capture, collect and save, and the ring
// reader under a running audio thread. Each test's files go under
// GG_TEST_OUTPUT_DIR/plugin-sample-<name>/, with a settings file and a captures folder of its own.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <clap/clap.h>

#include "MediaSession.h"
#include "PluginHarness.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"
#include "gg/core/SessionState.h"
#include "gg/media/Decode.h"
#include "gg/media/RingAudio.h"
#include "gg/media/SamplePool.h"
#include "gg/source/RingStorage.h"
#include "support/StateContainer.h"

using gg::core::LayerParam;
using gg::core::layerSlot;
using gg::core::paramId;
using gg::core::ParamSlot;
using gg::plugin::SampleState;
using gg::test::PluginUnderTest;
using gg::test::Stereo;

namespace
{
constexpr const char *kInstrumentId = "org.sethbc.giga-grains";
constexpr const char *kFxId = "org.sethbc.giga-grains-fx";
constexpr double kRate = 48000.0;
constexpr uint32_t kBlock = 512;
namespace fs = std::filesystem;

clap_id idOf(LayerParam p, uint8_t layer) { return paramId(layerSlot(p, layer)); }

// Renders `blocks` blocks of kBlock frames, with noise input (seeded per block from `seed`) or
// silence, into `out` (appended) when given.
void render(PluginUnderTest &plugin, int blocks, uint64_t seed, std::vector<float> *out = nullptr,
            bool silent = false)
{
    Stereo in(kBlock);
    Stereo block(kBlock);
    for (int i = 0; i < blocks; ++i)
    {
        if (!silent)
        {
            gg::test::fillNoise(in, seed + static_cast<uint64_t>(i) * 4 * kBlock);
        }
        REQUIRE(plugin.process(kBlock, in.data(), block.data()) == CLAP_PROCESS_CONTINUE);
        if (out != nullptr)
        {
            out->insert(out->end(), block.left.begin(), block.left.end());
            out->insert(out->end(), block.right.begin(), block.right.end());
        }
    }
}

// Sets parameter `id` to `value` through params.flush.
void setParam(PluginUnderTest &plugin, clap_id id, double value)
{
    const auto event = gg::test::paramValue(id, value);
    gg::test::InputEvents events;
    events.add(&event.header);
    plugin.flush(events.list());
}

float peakOf(const std::vector<float> &v)
{
    float p = 0.0f;
    for (const float x : v)
    {
        p = std::max(p, std::abs(x));
    }
    return p;
}

// An FX instance of seed 3, active, whose layer A ring holds 1 s of noise and is then frozen
// (Freeze on, 0.5 s more rendered, past the 20 ms ramp).
void frozenFx(PluginUnderTest &fx)
{
    REQUIRE(fx.valid());
    REQUIRE(fx.loadDefaultsWithSeed(3));
    REQUIRE(fx.activate(kRate, kBlock));
    render(fx, 94, 11);
    setParam(fx, paramId(ParamSlot::Freeze), 1.0);
    render(fx, 47, 12);
}

gg::core::SessionState decoded(const std::string &state, gg::core::Mode mode)
{
    gg::core::SessionState s;
    REQUIRE(gg::core::decodeState(state, mode, s));
    return s;
}

// The FX defaults, seed 0, as a SessionState to build a state from.
gg::core::SessionState fxDefaults()
{
    gg::core::SessionState s;
    for (std::size_t i = 0; i < gg::core::kParamCount; ++i)
    {
        s.values[i] = gg::core::paramMeta(static_cast<ParamSlot>(i), gg::core::Mode::Fx).defaultVal;
    }
    s.routes = gg::core::factoryRoutes(gg::core::Mode::Fx);
    return s;
}

std::size_t slotIndex(LayerParam p, uint8_t layer)
{
    return static_cast<std::size_t>(layerSlot(p, layer));
}

// A stereo ramp of `frames` frames at kRate: frame f is (f mod 9973) / 9973, the right channel
// negated, so any frame's place can be told from its value.
std::shared_ptr<gg::SampleBuffer> ramp(int64_t frames)
{
    auto b = std::make_shared<gg::SampleBuffer>(2, frames, kRate);
    for (int64_t f = 0; f < frames; ++f)
    {
        const float v = static_cast<float>(f % 9973) / 9973.0f;
        b->channel(0)[f] = v;
        b->channel(1)[f] = -v;
    }
    return b;
}

// `buffer`'s frames interleaved, as ringWindow gives a ring's.
std::vector<float> interleaved(const gg::SampleBuffer &buffer, int64_t from = 0)
{
    std::vector<float> v;
    for (int64_t f = from; f < buffer.frames(); ++f)
    {
        v.push_back(buffer.channel(0)[f]);
        v.push_back(buffer.channel(1)[f]);
    }
    return v;
}

// An FX state whose layer A holds a frozen 100 s ring of a ramp, embedded: 38.4 MB of WAV, over
// the 32 MiB embed cap, so a save files it.
struct BigRing
{
    std::shared_ptr<gg::SampleBuffer> window;
    gg::core::RingEntry entry;
    std::string state;
};

BigRing bigRing()
{
    constexpr int64_t kFrames = 100 * 48000;
    BigRing out;
    gg::core::SessionState s = fxDefaults();
    s.values[slotIndex(LayerParam::Freeze, 0)] = 1.0f;
    s.values[slotIndex(LayerParam::RingLength, 0)] = static_cast<float>(std::log2(100.0));
    out.window = ramp(kFrames);
    gg::media::EncodedRing encoded = gg::media::encodeRing(*out.window);
    out.entry.layer = 0;
    out.entry.frames = kFrames;
    out.entry.sampleRate = kRate;
    out.entry.sha256Hex = encoded.sha256Hex;
    out.entry.fileSize = encoded.size;
    out.entry.embed = true;
    out.entry.audioId = 1;
    s.rings[0] = out.entry;
    s.audio.push_back({1, std::move(encoded.wavBytes)});
    out.state = gg::core::encodeState(s);
    REQUIRE(out.state.size() > gg::core::kEmbedCapBytes);
    return out;
}

// A host that counts request_callback calls (any thread).
class CallbackHost
{
  public:
    CallbackHost() { host_.host_data = this; }
    CallbackHost(const CallbackHost &) = delete;
    CallbackHost &operator=(const CallbackHost &) = delete;
    const clap_host *clap() const noexcept { return &host_; }
    uint32_t callbacks() const noexcept { return callbacks_.load(); }

  private:
    std::atomic<uint32_t> callbacks_{0};
    clap_host host_{
        CLAP_VERSION_INIT,
        nullptr,
        "gg-tests callback host",
        "Giga Grains",
        "",
        "0.0.0",
        [](const clap_host *, const char *) -> const void * { return nullptr; },
        [](const clap_host *) {}, // request_restart
        [](const clap_host *) {}, // request_process
        [](const clap_host *host) { ++static_cast<CallbackHost *>(host->host_data)->callbacks_; },
    };
};

// Loads `state` into a new FX instance (inactive), activates it and renders `blocks` silent
// blocks: the output.
std::vector<float> renderRestored(const std::string &state, int blocks,
                                  std::vector<float> *window = nullptr)
{
    PluginUnderTest fx(kFxId);
    REQUIRE(fx.valid());
    REQUIRE(fx.load(state));
    REQUIRE(fx.activate(kRate, kBlock));
    if (window != nullptr)
    {
        *window = gg::test::ringWindow(*fx.impl().processor(), 0);
    }
    std::vector<float> out;
    render(fx, blocks, 0, &out, true);
    return out;
}
} // namespace

TEST_CASE("a frozen ring round-trips through the state and renders the same", "[plugin]")
{
    // D1, D8: a layer whose Freeze is on saves its ring; a new instance restores it, held from
    // the first sample. The saved session's own render can't continue bit for bit (a reopened
    // session's clock and grain index restart at 0, spec §6.6), so the gates are: the restored
    // ring is the saved one frame for frame; every restore, inactive or active, renders the same
    // bits; and the held ring sounds with no input.
    gg::test::useMediaScratch("ring-roundtrip");
    PluginUnderTest a(kFxId);
    frozenFx(a);
    const std::vector<float> saved = gg::test::ringWindow(*a.impl().processor(), 0);
    REQUIRE(saved.size() > 2 * 94 * kBlock); // the head stopped after the freeze's ramp
    REQUIRE(saved.size() < 2 * 141 * kBlock);
    const std::string state = a.save();
    const gg::core::SessionState s = decoded(state, gg::core::Mode::Fx);
    REQUIRE(s.rings[0].has_value());
    CHECK(s.rings[0]->embed);
    CHECK(s.rings[0]->frames == static_cast<int64_t>(saved.size() / 2));
    CHECK(s.audio.size() == 1);
    CHECK_FALSE(s.rings[1].has_value()); // B's Freeze and Loop are off: not saved (D1)

    std::vector<float> windowB;
    const std::vector<float> outB = renderRestored(state, 188, &windowB);
    CHECK(windowB == saved);
    CHECK(peakOf(outB) > 0.001f); // the held ring sounds, with silence in
    std::vector<float> windowC;
    const std::vector<float> outC = renderRestored(state, 188, &windowC);
    CHECK(windowC == saved);
    CHECK(gg::test::sameBits(outB, outC));

    // Restored while active, before the first call: the same bits again.
    PluginUnderTest d(kFxId);
    REQUIRE(d.valid());
    REQUIRE(d.activate(kRate, kBlock));
    REQUIRE(d.load(state));
    std::vector<float> outD;
    render(d, 188, 0, &outD, true);
    CHECK(gg::test::ringWindow(*d.impl().processor(), 0) == saved); // still held
    CHECK(gg::test::sameBits(outB, outD));
}

TEST_CASE("a filed ring over the embed cap goes to the captures folder", "[plugin]")
{
    // Spec §6.6: a 100 s stereo ring at 48 kHz is 38.4 MB of WAV, over the 32 MiB embed cap: it
    // is filed as <sha256>.wav, the state stays small, and a restore finds it there.
    const fs::path dir = gg::test::useMediaScratch("ring-filed");
    fs::remove_all(dir / "captures");
    const BigRing ring = bigRing();
    const auto &window = ring.window;
    const auto &entry = ring.entry;
    const std::string &big = ring.state;

    std::string state;
    {
        PluginUnderTest fx(kFxId);
        REQUIRE(fx.valid());
        REQUIRE(fx.load(big));
        REQUIRE(fx.activate(kRate, kBlock));
        CHECK(fx.impl().processor()->ringRead(0).storage->frames() == entry.frames);
        state = fx.save();
    }
    CHECK(state.size() < 64 * 1024);
    const gg::core::SessionState saved = decoded(state, gg::core::Mode::Fx);
    REQUIRE(saved.rings[0].has_value());
    CHECK_FALSE(saved.rings[0]->embed);
    const fs::path file = dir / "captures" / (entry.sha256Hex + ".wav");
    CHECK(fs::path(saved.rings[0]->absolutePath) == file);
    CHECK(fs::file_size(file) == entry.fileSize);
    CHECK(saved.audio.empty());

    std::vector<float> restored;
    renderRestored(state, 1, &restored);
    CHECK(restored == interleaved(*window));
}

TEST_CASE("a filed ring that can't be found loads the rest and stays live", "[plugin]")
{
    // Review Focus 4: the state loads, the layer's status says the ring wasn't found, no ring is
    // restored and its Freeze stays live until toggled (FR-8).
    const fs::path dir = gg::test::useMediaScratch("ring-missing");
    gg::core::SessionState s = fxDefaults();
    s.values[slotIndex(LayerParam::Freeze, 0)] = 1.0f;
    s.values[static_cast<std::size_t>(ParamSlot::Density)] = 3.0f;
    gg::core::RingEntry entry;
    entry.frames = 48000;
    entry.sampleRate = kRate;
    entry.sha256Hex = std::string(64, 'a');
    entry.fileSize = 384068;
    entry.absolutePath = gg::test::utf8(dir / "elsewhere" / "Captures" / "gone.wav");
    s.rings[0] = entry;
    PluginUnderTest fx(kFxId);
    REQUIRE(fx.valid());
    REQUIRE(fx.load(gg::core::encodeState(s)));
    CHECK(fx.impl().media().status(0) == "ring not found");
    CHECK(fx.value(paramId(ParamSlot::Density)) == 3.0);
    REQUIRE(fx.activate(kRate, kBlock));
    render(fx, 20, 5);
    const auto &live = fx.impl().processor()->engine(0).live();
    CHECK(live.head() == 20 * kBlock); // live: the head runs
}

TEST_CASE("a ring length set while inactive applies at activation", "[plugin]")
{
    // Review Focus 2: no exchange runs while inactive; activate() builds each ring at its Ring
    // Length, set by a flush or by a state, and the first call renders with it.
    gg::test::useMediaScratch("ring-inactive");
    PluginUnderTest fx(kFxId);
    REQUIRE(fx.valid());
    gg::core::SessionState s = fxDefaults();
    s.values[slotIndex(LayerParam::RingLength, 2)] = 3.0f; // 8 s
    REQUIRE(fx.load(gg::core::encodeState(s)));
    setParam(fx, idOf(LayerParam::RingLength, 0), 2.0);             // 4 s
    setParam(fx, idOf(LayerParam::RingLength, 1), std::log2(30.0)); // 30 s
    REQUIRE(fx.activate(kRate, kBlock));
    const auto &p = *fx.impl().processor();
    CHECK(p.ringRead(0).storage->frames() == 4 * 48000);
    CHECK(p.ringRead(1).storage->frames() == 30 * 48000);
    CHECK(p.ringRead(2).storage->frames() == 8 * 48000);
    CHECK(p.ringRead(3).storage->frames() == 16 * 48000);
    CHECK(p.engine(0).live().frames() == 4 * 48000);
    render(fx, 1, 9);
    CHECK(p.engine(0).live().head() == kBlock);
    CHECK(fx.impl().media().status(0).empty());
}

TEST_CASE("the FX restores a layer B sample", "[plugin]")
{
    // Spec §6.8: an FX layer set to Sample reads its sample, through its own loader.
    const fs::path dir = gg::test::useMediaScratch("fx-layer-b");
    const fs::path file = gg::test::writeSineWav(dir / "b.wav", 48000, 1.0);
    gg::core::SessionState s = fxDefaults();
    s.values[slotIndex(LayerParam::Source, 1)] = 0.0f; // Sample
    s.samples[1] = gg::test::entryOf(file);
    s.samples[1]->layer = 1;
    PluginUnderTest fx(kFxId);
    REQUIRE(fx.valid());
    REQUIRE(fx.load(gg::core::encodeState(s)));
    CHECK(fx.impl().media().loader(1).status().state == SampleState::Loaded);
    CHECK(fx.impl().media().loader(0).status().state == SampleState::None);
    REQUIRE(fx.activate(kRate, kBlock));
    CHECK(fx.impl().processor()->sources(1).held() == 1);
    const gg::core::SessionState saved = decoded(fx.save(), gg::core::Mode::Fx);
    REQUIRE(saved.samples[1].has_value());
    CHECK(saved.samples[1]->layer == 1);
    CHECK(saved.samples[1]->sha256Hex == s.samples[1]->sha256Hex);
    CHECK_FALSE(saved.samples[0].has_value());
}

TEST_CASE("the memory cap refuses a load with a status", "[plugin]")
{
    // Spec §6.5: with a 64 MiB cap and four 16 s rings (23.5 MiB), 40 MiB are free. A 2-minute
    // stereo file decodes to 44 MiB: the decoder refuses it before allocating. A 90 s one decodes
    // (33 MiB) but its source, with the interleaved copy, needs 66 MiB: refused before it is made.
    // A 300 s ring needs 110 MiB: refused, with its status, until the request changes.
    const fs::path dir = gg::test::useMediaScratch("cap-refuses");
    const fs::path longFile = gg::test::writeSineWav(dir / "long.wav", 48000, 120.0);
    const fs::path mediumFile = gg::test::writeSineWav(dir / "medium.wav", 48000, 90.0);
    PluginUnderTest instrument(kInstrumentId);
    REQUIRE(instrument.valid());
    auto &media = instrument.impl().media();
    media.setMemoryCapMiB(64);
    REQUIRE(instrument.activate(kRate, kBlock));
    CHECK(media.memoryUsedBytes() == 4 * gg::media::ringBytes(16 * 48000, kBlock, 1));

    auto &loader = media.loader(0);
    const auto settle = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (loader.status().state == SampleState::Loading &&
               std::chrono::steady_clock::now() < deadline)
        {
            instrument.onMainThread();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return loader.status();
    };
    loader.requestLoad(longFile);
    gg::plugin::SampleStatus status = settle();
    CHECK(status.state == SampleState::Error);
    CHECK(status.message == "long.wav: needs 44 MB, the memory cap allows 40 MB");
    loader.requestLoad(mediumFile);
    status = settle();
    CHECK(status.state == SampleState::Error);
    CHECK(status.message == "medium.wav: over the memory cap: needs 66 MB, 40 MB free of 64 MB");
    CHECK_FALSE(loader.entry().has_value());

    const auto ringLength = gg::test::paramValue(idOf(LayerParam::RingLength, 0), std::log2(300.0));
    gg::test::InputEvents events;
    events.add(&ringLength.header);
    Stereo out(kBlock);
    REQUIRE(instrument.process(kBlock, nullptr, out.data(), events.list()) ==
            CLAP_PROCESS_CONTINUE);
    instrument.onMainThread();
    CHECK(media.status(0) == "over the memory cap: needs 110 MB, 40 MB free of 64 MB");
    CHECK(instrument.impl().processor()->ringRead(0).storage->frames() == 16 * 48000);
    render(instrument, 2, 0, nullptr, true);
    instrument.onMainThread();
    CHECK_FALSE(instrument.impl().processor()->sources(0).ringAdoptionPending());
    CHECK(media.status(0) == "over the memory cap: needs 110 MB, 40 MB free of 64 MB");
}

TEST_CASE("capture, load into a layer, save and restore", "[plugin]")
{
    // Spec §6.4: a capture of layer A's ring becomes layer B's sample (embedded), the session
    // saves it in AUDI, a restore loads it, and saveCapture writes the same audio to a file.
    const fs::path dir = gg::test::useMediaScratch("capture");
    PluginUnderTest fx(kFxId);
    REQUIRE(fx.valid());
    REQUIRE(fx.activate(kRate, kBlock));
    render(fx, 94, 21);
    auto &media = fx.impl().media();
    const auto *processor = fx.impl().processor();
    const auto capture = media.captureRing(0, 0, *processor);
    REQUIRE(capture != nullptr);
    CHECK(capture->frames() == 94 * kBlock);
    CHECK(interleaved(*capture) == gg::test::ringWindow(*processor, 0));
    REQUIRE(media.loadCapture(1, capture));
    REQUIRE(media.loader(1).entry().has_value());
    CHECK(media.loader(1).entry()->embed);
    CHECK(media.loader(1).status().state == SampleState::Loaded);

    const std::string state = fx.save();
    const gg::core::SessionState s = decoded(state, gg::core::Mode::Fx);
    REQUIRE(s.samples[1].has_value());
    CHECK(s.samples[1]->embed);
    CHECK(s.samples[1]->layer == 1);
    REQUIRE(s.audio.size() == 1);
    CHECK(s.samples[1]->audioId == s.audio[0].id);

    PluginUnderTest b(kFxId);
    REQUIRE(b.valid());
    REQUIRE(b.load(state));
    const auto &loader = b.impl().media().loader(1);
    CHECK(loader.status().state == SampleState::Loaded);
    REQUIRE(loader.embeddedBytes() != nullptr);
    CHECK(*loader.embeddedBytes() == s.audio[0].bytes);
    REQUIRE(b.activate(kRate, kBlock));
    CHECK(b.impl().processor()->sources(1).held() == 1);
    // Saved again, it is the same entry and payload.
    const gg::core::SessionState again = decoded(b.save(), gg::core::Mode::Fx);
    CHECK(again.samples[1] == s.samples[1]);
    CHECK(again.audio == s.audio);

    const fs::path file = dir / "out" / "capture.wav";
    fs::remove(file);
    REQUIRE(media.saveCapture(*capture, file));
    const gg::media::DecodeResult read =
        gg::media::decodeAudioFile(file, gg::media::kDefaultDecodeBound);
    REQUIRE(read.audio.has_value());
    CHECK(interleaved(*read.audio->buffer) == interleaved(*capture));

    // A layer with no ring audio yet: nothing to capture.
    PluginUnderTest idle(kFxId);
    REQUIRE(idle.valid());
    REQUIRE(idle.activate(kRate, kBlock));
    CHECK(idle.impl().media().captureRing(2, 0, *idle.impl().processor()) == nullptr);
    CHECK(idle.impl().media().status(2) == "no ring to capture");
}

TEST_CASE("collect and save gathers a filed ring", "[plugin]")
{
    // The controller's ruling: collect and save saves first, so an active instance's filed ring
    // (written to the captures folder) travels with the project, and later saves reference the
    // copy.
    const fs::path dir = gg::test::useMediaScratch("collect-ring");
    fs::remove_all(dir / "captures");
    const BigRing ring = bigRing();
    PluginUnderTest fx(kFxId);
    REQUIRE(fx.valid());
    REQUIRE(fx.load(ring.state));
    REQUIRE(fx.activate(kRate, kBlock));
    const fs::path folder = dir / "collected";
    fs::remove_all(folder);
    const gg::media::CollectResult result = fx.impl().media().collectAndSave(folder);
    CHECK(result.missing.empty());
    REQUIRE(result.copied.size() == 1);
    const fs::path copy = folder / (ring.entry.sha256Hex + ".wav");
    CHECK(fs::file_size(copy) == ring.entry.fileSize);
    const gg::core::SessionState saved = decoded(fx.save(), gg::core::Mode::Fx);
    REQUIRE(saved.rings[0].has_value());
    CHECK(fs::path(saved.rings[0]->absolutePath) == copy);
    // The captures folder may go: the project's copy restores it.
    fs::remove_all(dir / "captures");
    std::vector<float> restored;
    gg::core::SessionState moved = saved;
    renderRestored(gg::core::encodeState(moved), 1, &restored);
    CHECK(restored == interleaved(*ring.window));
}

TEST_CASE("collect and save gathers the files", "[plugin]")
{
    // Spec §6.7: every layer's file copied into one folder, the entries then pointing there.
    const fs::path dir = gg::test::useMediaScratch("collect");
    const fs::path a = gg::test::writeSineWav(dir / "one" / "a.wav", 48000, 0.5);
    const fs::path b = gg::test::writeSineWav(dir / "two" / "b.wav", 48000, 0.5, 330.0);
    gg::core::SessionState s;
    REQUIRE(gg::core::decodeState(PluginUnderTest(kInstrumentId).save(), gg::core::Mode::Instrument,
                                  s));
    s.samples[0] = gg::test::entryOf(a);
    s.samples[2] = gg::test::entryOf(b);
    s.samples[2]->layer = 2;
    PluginUnderTest instrument(kInstrumentId);
    REQUIRE(instrument.valid());
    REQUIRE(instrument.load(gg::core::encodeState(s)));
    const fs::path folder = dir / "collected";
    fs::remove_all(folder);
    const gg::media::CollectResult result = instrument.impl().media().collectAndSave(folder);
    CHECK(result.copied.size() == 2);
    CHECK(result.missing.empty());
    const gg::core::SessionState saved = decoded(instrument.save(), gg::core::Mode::Instrument);
    REQUIRE(saved.samples[0].has_value());
    REQUIRE(saved.samples[2].has_value());
    CHECK(fs::path(saved.samples[0]->absolutePath) == folder / "a.wav");
    CHECK(fs::path(saved.samples[2]->absolutePath) == folder / "b.wav");
    CHECK(fs::exists(folder / "a.wav"));
    CHECK(fs::exists(folder / "b.wav"));
}

TEST_CASE("a Ring Length change resizes while processing", "[plugin]")
{
    // Spec §6.5: the main thread copies the stable part into a 4 s storage and offers it; the
    // audio thread adopts it at the next call, copying the frames written since. The window is
    // the same frame for frame afterwards, and the status passes through "resizing".
    gg::test::useMediaScratch("ring-resize");
    PluginUnderTest fx(kFxId);
    REQUIRE(fx.valid());
    REQUIRE(fx.activate(kRate, kBlock));
    render(fx, 94, 31);
    const auto ringLength = gg::test::paramValue(idOf(LayerParam::RingLength, 0), 2.0);
    gg::test::InputEvents events;
    events.add(&ringLength.header);
    Stereo in(kBlock);
    Stereo out(kBlock);
    gg::test::fillNoise(in, 77);
    REQUIRE(fx.process(kBlock, in.data(), out.data(), events.list()) == CLAP_PROCESS_CONTINUE);
    const auto *processor = fx.impl().processor();
    const std::vector<float> before = gg::test::ringWindow(*processor, 0);
    fx.onMainThread(); // the watcher offers the 4 s ring
    auto &media = fx.impl().media();
    CHECK(media.status(0) == "resizing");
    CHECK(processor->ringRead(0).storage->frames() == 16 * 48000);
    render(fx, 1, 41);
    CHECK(processor->ringRead(0).storage->frames() == 4 * 48000);
    CHECK(media.status(0).empty());
    fx.onMainThread();
    CHECK(media.status(0).empty());
    const std::vector<float> after = gg::test::ringWindow(*processor, 0);
    REQUIRE(after.size() == before.size() + 2 * kBlock);
    CHECK(std::equal(before.begin(), before.end(), after.begin()));
    CHECK(processor->engine(0).live().frames() == 4 * 48000);
}

TEST_CASE("params.flush adopts no ring: the offer waits whole for the next process call",
          "[plugin]")
{
    // The programme manager's ruling (CLAUDE.md: params.flush stays trivial): a restore offered
    // while active is a ring, a source and values on layer A's exchange; a flush takes none of it,
    // and the next process() call applies it all at its start, the ring held from that call's
    // first sample, so it renders as a restore made before the first call does.
    gg::test::useMediaScratch("flush-no-ring");
    PluginUnderTest a(kFxId);
    frozenFx(a);
    gg::core::SessionState s = decoded(a.save(), gg::core::Mode::Fx);
    s.values[static_cast<std::size_t>(ParamSlot::Density)] = 3.0f;
    const std::string state = gg::core::encodeState(s);
    const std::vector<float> expected = renderRestored(state, 20);

    PluginUnderTest b(kFxId);
    REQUIRE(b.valid());
    REQUIRE(b.activate(kRate, kBlock));
    const auto *processor = b.impl().processor();
    const gg::RingStorage *before = processor->ringRead(0).storage;
    REQUIRE(b.load(state));
    REQUIRE(processor->sources(0).ringAdoptionPending());
    b.flush(gg::test::InputEvents().list());
    CHECK(processor->ringRead(0).storage == before); // nothing adopted
    CHECK(processor->sources(0).ringAdoptionPending());
    CHECK(processor->base(ParamSlot::Density) != 3.0f); // the values wait with their ring
    std::vector<float> out;
    render(b, 20, 0, &out, true);
    CHECK_FALSE(processor->sources(0).ringAdoptionPending());
    CHECK(processor->base(ParamSlot::Density) == 3.0f);
    CHECK(gg::test::sameBits(out, expected));
}

TEST_CASE("a Ring Length event in a call's last grid step still reaches the watcher", "[plugin]")
{
    // The base applies at its grid boundary, here the next call's start: the request for the
    // watcher must follow the Processor's request, not the value the host sent.
    gg::test::useMediaScratch("ring-late-event");
    CallbackHost host;
    PluginUnderTest fx(kFxId, host.clap());
    REQUIRE(fx.valid());
    REQUIRE(fx.activate(kRate, kBlock));
    render(fx, 4, 1);
    const auto ringLength = gg::test::paramValue(idOf(LayerParam::RingLength, 0), 2.0, kBlock - 10);
    gg::test::InputEvents events;
    events.add(&ringLength.header);
    Stereo in(kBlock);
    Stereo out(kBlock);
    REQUIRE(fx.process(kBlock, in.data(), out.data(), events.list()) == CLAP_PROCESS_CONTINUE);
    fx.onMainThread(); // the request hasn't changed yet: nothing to resize
    const auto *processor = fx.impl().processor();
    CHECK_FALSE(processor->sources(0).ringAdoptionPending());
    const uint32_t before = host.callbacks();
    render(fx, 1, 2); // the boundary applies the new base at this call's start
    CHECK(host.callbacks() > before);
    fx.onMainThread();
    CHECK(fx.impl().media().status(0) == "resizing");
    render(fx, 1, 3);
    CHECK(processor->ringRead(0).storage->frames() == 4 * 48000);
}

TEST_CASE("a live ring under Loop and Overdub is encoded again at each save", "[plugin]")
{
    // The save's cache holds a frozen ring's bytes alone: a live ring's frames change as it
    // writes, so two saves with writes between give different bytes.
    gg::test::useMediaScratch("ring-overdub-save");
    PluginUnderTest fx(kFxId);
    REQUIRE(fx.valid());
    REQUIRE(fx.activate(kRate, kBlock));
    setParam(fx, idOf(LayerParam::Loop, 0), 1.0);
    setParam(fx, idOf(LayerParam::LoopLength, 0), -2.0);
    setParam(fx, idOf(LayerParam::Overdub, 0), 0.7);
    render(fx, 40, 81);
    const auto &media = fx.impl().media();
    const gg::core::SessionState first = decoded(fx.save(), gg::core::Mode::Fx);
    render(fx, 3, 82);
    const gg::core::SessionState second = decoded(fx.save(), gg::core::Mode::Fx);
    CHECK(media.ringsEncoded() == 2);
    REQUIRE(first.audio.size() == 1);
    REQUIRE(second.audio.size() == 1);
    CHECK(first.audio[0].bytes != second.audio[0].bytes);
    CHECK(first.rings[0]->sha256Hex != second.rings[0]->sha256Hex);
}

TEST_CASE("a ring status goes once the layer is no longer held", "[plugin]")
{
    const fs::path dir = gg::test::useMediaScratch("ring-status-clears");
    gg::core::SessionState s = fxDefaults();
    s.values[slotIndex(LayerParam::Freeze, 0)] = 1.0f;
    gg::core::RingEntry entry;
    entry.frames = 48000;
    entry.sampleRate = kRate;
    entry.sha256Hex = std::string(64, 'd');
    entry.fileSize = 384068;
    entry.absolutePath = gg::test::utf8(dir / "gone" / "Captures" / "gone.wav");
    s.rings[0] = entry;
    PluginUnderTest fx(kFxId);
    REQUIRE(fx.valid());
    REQUIRE(fx.load(gg::core::encodeState(s)));
    REQUIRE(fx.activate(kRate, kBlock));
    CHECK(fx.impl().media().status(0) == "ring not found"); // a restore's status stands
    setParam(fx, paramId(ParamSlot::Freeze), 0.0);
    render(fx, 1, 3);
    fx.save();
    CHECK(fx.impl().media().status(0).empty());
}

TEST_CASE("INST carries the cap", "[plugin]")
{
    gg::test::useMediaScratch("inst-cap");
    PluginUnderTest a(kInstrumentId);
    REQUIRE(a.valid());
    a.impl().media().setMemoryCapMiB(2048);
    const std::string state = a.save();
    CHECK(decoded(state, gg::core::Mode::Instrument).memoryCapMiB == 2048);
    PluginUnderTest b(kInstrumentId);
    REQUIRE(b.valid());
    CHECK(b.impl().media().memoryCapMiB() == gg::core::kMemoryCapDefaultMiB);
    REQUIRE(b.load(state));
    CHECK(b.impl().media().memoryCapMiB() == 2048);
    b.impl().media().setMemoryCapMiB(1);
    CHECK(b.impl().media().memoryCapMiB() == gg::core::kMemoryCapMinMiB);
    b.impl().media().setMemoryCapMiB(1u << 20);
    CHECK(b.impl().media().memoryCapMiB() == gg::core::kMemoryCapMaxMiB);
}

TEST_CASE("an unchanged frozen ring is hashed once", "[plugin]")
{
    // Spec §6.6: the ring's WAV bytes and hash are cached by (layer, epoch, head, window start).
    gg::test::useMediaScratch("ring-hashed-once");
    PluginUnderTest fx(kFxId);
    frozenFx(fx);
    const auto &media = fx.impl().media();
    const std::string first = fx.save();
    CHECK(media.ringsEncoded() == 1);
    render(fx, 10, 51); // frozen: the head stands
    const std::string second = fx.save();
    CHECK(media.ringsEncoded() == 1);
    CHECK(first == second);
    setParam(fx, paramId(ParamSlot::Freeze), 0.0);
    render(fx, 2, 52); // live: the head moves, so the next save encodes again
    setParam(fx, paramId(ParamSlot::Freeze), 1.0);
    render(fx, 4, 53);
    fx.save();
    CHECK(media.ringsEncoded() == 2);
}

TEST_CASE("a Loop layer's save keeps the loop window and only the marks inside it", "[plugin]")
{
    // The controller's ruling (Task 8's review): a saved ring is the loop window, min(L, N)
    // frames with Loop on, with the splice marks inside it as frames from its start; the Splice
    // selection doesn't narrow it.
    gg::test::useMediaScratch("ring-loop-save");
    PluginUnderTest fx(kFxId);
    REQUIRE(fx.valid());
    REQUIRE(fx.activate(kRate, kBlock));
    setParam(fx, idOf(LayerParam::Loop, 0), 1.0);
    setParam(fx, idOf(LayerParam::LoopLength, 0), -2.0); // 0.25 s: 12,000 frames
    render(fx, 47, 61);
    fx.impl().uiCommand({0, gg::core::CommandKind::AddSplice, 0, 0.0f}); // a mark at the head
    render(fx, 47, 62);
    fx.impl().uiCommand({0, gg::core::CommandKind::AddSplice, 0, 0.1f}); // 4,800 frames behind
    render(fx, 1, 63);
    setParam(fx, idOf(LayerParam::Splice, 0), 1.0); // a selection: the save ignores it
    render(fx, 1, 64);
    const auto *processor = fx.impl().processor();
    const auto marks = processor->engine(0).live().splices();
    REQUIRE(marks.size() == 2);
    const int64_t head = processor->engine(0).live().head();
    const gg::core::SessionState s = decoded(fx.save(), gg::core::Mode::Fx);
    REQUIRE(s.rings[0].has_value());
    CHECK(s.rings[0]->frames == 12000);
    CHECK(s.rings[0]->splices == std::vector<int64_t>{marks[1] - (head - 12000)});
    REQUIRE(s.audio.size() == 1);
    const gg::media::DecodeResult saved =
        gg::media::decodeAudioBytes(s.audio[0].bytes, gg::media::kDefaultDecodeBound);
    REQUIRE(saved.audio.has_value());
    const std::vector<float> window = gg::test::ringWindow(*processor, 0);
    CHECK(interleaved(*saved.audio->buffer) ==
          std::vector<float>(window.end() - 2 * 12000, window.end()));
}

TEST_CASE("a capture with splice 0 under Loop is the last L frames", "[plugin]")
{
    // The controller's ruling: a capture follows the loop window, then its splice argument.
    gg::test::useMediaScratch("ring-loop-capture");
    PluginUnderTest fx(kFxId);
    REQUIRE(fx.valid());
    REQUIRE(fx.activate(kRate, kBlock));
    setParam(fx, idOf(LayerParam::Loop, 0), 1.0);
    setParam(fx, idOf(LayerParam::LoopLength, 0), -2.0); // 12,000 frames
    render(fx, 94, 71);
    fx.impl().uiCommand({0, gg::core::CommandKind::AddSplice, 0, 0.05f}); // 2,400 behind
    render(fx, 1, 72);
    const auto *processor = fx.impl().processor();
    auto &media = fx.impl().media();
    const std::vector<float> window = gg::test::ringWindow(*processor, 0);
    const auto whole = media.captureRing(0, 0, *processor);
    REQUIRE(whole != nullptr);
    CHECK(interleaved(*whole) == std::vector<float>(window.end() - 2 * 12000, window.end()));
    // Splice 1: from the one mark inside the window to the head; 5 is past the count: the last.
    const int64_t head = processor->engine(0).live().head();
    const int64_t mark = processor->engine(0).live().splices().back();
    for (const uint32_t k : {1u, 5u})
    {
        const auto splice = media.captureRing(0, k, *processor);
        REQUIRE(splice != nullptr);
        CHECK(splice->frames() == head - mark);
        CHECK(interleaved(*splice) ==
              std::vector<float>(window.end() - 2 * (head - mark), window.end()));
    }
}

TEST_CASE("a ring held at deactivation comes back at the next activation", "[plugin]")
{
    // D1's rings outlive a deactivation, as a sample does: kept, then restored before the first
    // call of the next activation (converted at a new rate).
    gg::test::useMediaScratch("ring-reactivate");
    PluginUnderTest fx(kFxId);
    frozenFx(fx);
    const std::vector<float> saved = gg::test::ringWindow(*fx.impl().processor(), 0);
    fx.deactivate();
    const gg::core::SessionState inactive = decoded(fx.save(), gg::core::Mode::Fx);
    REQUIRE(inactive.rings[0].has_value()); // saved while inactive too
    CHECK(inactive.rings[0]->frames == static_cast<int64_t>(saved.size() / 2));
    REQUIRE(fx.activate(kRate, kBlock));
    CHECK(gg::test::ringWindow(*fx.impl().processor(), 0) == saved);
}

TEST_CASE("an embedded sample whose payload is missing says so", "[plugin]")
{
    // Spec §6.6: an older build that re-saved the state dropped AUDI; the entry loads silent with
    // "embedded audio missing", the rest of the state loads, and a ring's the same.
    gg::test::useMediaScratch("embedded-missing");
    gg::core::SessionState s = fxDefaults();
    s.values[static_cast<std::size_t>(ParamSlot::Density)] = 4.0f;
    gg::core::MediaEntry sample;
    sample.sha256Hex = std::string(64, 'b');
    sample.fileSize = 100;
    sample.format = "wav";
    sample.channels = 2;
    sample.sampleRate = kRate;
    sample.frames = 8;
    sample.embed = true;
    sample.audioId = 7;
    sample.layer = 1;
    s.samples[1] = sample;
    gg::core::RingEntry ring;
    ring.frames = 8;
    ring.sampleRate = kRate;
    ring.sha256Hex = std::string(64, 'c');
    ring.fileSize = 132;
    ring.embed = true;
    ring.audioId = 8;
    ring.layer = 2;
    s.rings[2] = ring;
    s.audio.push_back({7, "x"});
    s.audio.push_back({8, "y"});
    const std::string full = gg::core::encodeState(s);
    // The same state without its AUDI chunk, as an older build writes it back.
    const std::string bytes = gg::test::stateContainer({
        gg::test::stateChunk("PTCH", gg::test::stateChunkPayload(full, "PTCH").value()),
        gg::test::stateChunk("MEDI", gg::test::stateChunkPayload(full, "MEDI").value()),
        gg::test::stateChunk("INST", gg::test::stateChunkPayload(full, "INST").value()),
    });
    PluginUnderTest fx(kFxId);
    REQUIRE(fx.valid());
    REQUIRE(fx.load(bytes));
    const auto &media = fx.impl().media();
    CHECK(media.loader(1).status().state == SampleState::Missing);
    CHECK(media.status(1) == "embedded audio missing");
    CHECK(media.status(2) == "embedded audio missing");
    CHECK(fx.value(paramId(ParamSlot::Density)) == 4.0);
}

TEST_CASE("the ring reader's copies match what the audio thread wrote", "[plugin][threads]")
{
    // Spec §6.4's race-free read, under a running audio thread: one thread renders a Processor
    // whose 1 s ring takes a ramp (frame t holds t mod 1,000,000, the right channel negated), the
    // other copies the ring's window through readRing as fast as it can. Every copy accepted must
    // be the frames written, contiguous up to the head it reports.
    // Bounded by work, not time: the audio thread renders until the reader has made kReads
    // copies, at least one of them trimmed (the head lapped the copy's front mid-copy: the race
    // the read must survive), and kAdopted swaps of a full ring's storage have been adopted,
    // however fast either thread runs. The audio thread's kMaxTotal, and kHang for each wait of
    // the hold's handshake, are only safety bounds: reaching one fails the test.
    constexpr int64_t kN = 48000;
    constexpr uint32_t kPiece = 256;
    constexpr int64_t kMaxTotal = 600 * 48000;
    constexpr uint64_t kReads = 100;
    constexpr uint64_t kTrimmed = 1;
    constexpr uint32_t kAdopted = 3;
    constexpr auto kHang = std::chrono::seconds(60);
    gg::core::Processor p({kRate, kPiece, 7, gg::core::Mode::Fx});
    REQUIRE(p.setRingNow(0, std::make_unique<gg::RingStorage>(kN, kPiece), gg::core::RingOffer{}));
    std::atomic<bool> stop{false};
    std::atomic<bool> done{false};
    // A swap's own copy must be untrimmed to fill the new storage, which a running ring rarely
    // allows once full: the reader holds the audio thread between pieces while it takes that copy.
    // The checked reads never hold it. The handshake is three counters, each written by one
    // thread and never decreasing, so no request, acknowledgment or release can be missed: the
    // reader makes request r (`holdRequested`), the audio thread acknowledges r between pieces
    // (`holdAcked`) and waits until the reader has released r (`holdReleased`).
    std::atomic<uint32_t> holdRequested{0};
    std::atomic<uint32_t> holdAcked{0};
    std::atomic<uint32_t> holdReleased{0};
    std::atomic<bool> hung{false}; // a wait of the handshake reached kHang
    std::thread audio([&] {
        std::vector<float> left(kPiece), right(kPiece), outL(kPiece), outR(kPiece);
        const float *in[2] = {left.data(), right.data()};
        float *out[2] = {outL.data(), outR.data()};
        for (int64_t t = 0; t < kMaxTotal && !stop.load(std::memory_order_acquire); t += kPiece)
        {
            const uint32_t request = holdRequested.load(std::memory_order_acquire);
            if (request != holdAcked.load(std::memory_order_relaxed))
            {
                holdAcked.store(request, std::memory_order_release);
                const auto deadline = std::chrono::steady_clock::now() + kHang;
                while (holdReleased.load(std::memory_order_acquire) != request &&
                       !stop.load(std::memory_order_acquire))
                {
                    if (std::chrono::steady_clock::now() > deadline)
                    {
                        hung.store(true, std::memory_order_release);
                        break;
                    }
                    std::this_thread::yield();
                }
            }
            for (uint32_t i = 0; i < kPiece; ++i)
            {
                const auto v = static_cast<float>((t + i) % 1000000);
                left[i] = v;
                right[i] = -v;
            }
            p.process(in, 2, out, 2, kPiece, {});
        }
        done.store(true, std::memory_order_release);
    });
    uint64_t reads = 0;
    uint64_t trimmed = 0;
    uint64_t bad = 0;
    uint32_t swaps = 0;
    uint32_t declinesSeen = 0;
    uint64_t nextSwap = 24;
    // The latest offer is adopted once none is pending (a declined one stays pending until it is
    // made again and adopted). Only a swap of a full ring's storage counts toward kAdopted.
    bool offerOpen = false;
    bool offerFull = false;
    uint32_t adopted = 0;
    uint32_t adoptedFull = 0;
    const auto settle = [&] {
        if (offerOpen && !p.sources(0).ringAdoptionPending())
        {
            offerOpen = false;
            ++adopted;
            adoptedFull += offerFull ? 1 : 0;
        }
    };
    while (!done.load(std::memory_order_acquire) && !hung.load(std::memory_order_acquire) &&
           !(reads >= kReads && trimmed >= kTrimmed && adoptedFull >= kAdopted))
    {
        // From every 25th read on, a storage swap through the exchange (this thread has the main
        // thread's role: it offers, collects and alone dereferences storage): a new 1 s storage
        // holding a whole-window read, taken with the audio thread held, so reads under way meet
        // a changed epoch once it runs again. A copy that is trimmed all the same is retried at
        // the next read; a declined offer is made again.
        settle(); // so a new offer never hides an adoption not yet counted
        const uint32_t declines = p.ringAdoptionsDeclined(0);
        if (reads >= nextSwap && (!offerOpen || declines != declinesSeen))
        {
            const uint32_t request = holdRequested.load(std::memory_order_relaxed) + 1;
            holdRequested.store(request, std::memory_order_release);
            const auto deadline = std::chrono::steady_clock::now() + kHang;
            while (holdAcked.load(std::memory_order_acquire) != request &&
                   !done.load(std::memory_order_acquire))
            {
                if (std::chrono::steady_clock::now() > deadline)
                {
                    hung.store(true, std::memory_order_release);
                    break;
                }
                std::this_thread::yield();
            }
            const auto copy = gg::plugin::readRing(p, 0, {}, kRate);
            if (copy.has_value() && copy->oldest == std::max<int64_t>(0, copy->head - kN))
            {
                const int64_t frames = kN;
                auto next = std::make_unique<gg::RingStorage>(frames, kPiece);
                const gg::SampleBuffer &w = *copy->window;
                for (int64_t f = std::max(copy->oldest, copy->head - frames); f < copy->head; ++f)
                {
                    const int64_t d = f % next->capacity();
                    next->slots()[2 * d] = w.channel(0)[f - copy->oldest];
                    next->slots()[2 * d + 1] = w.channel(1)[f - copy->oldest];
                }
                gg::core::RingOffer how;
                how.copiedTo = copy->head;
                p.sources(0).offerRing(std::move(next), how);
                declinesSeen = declines; // only an offer made answers a decline
                ++swaps;
                offerOpen = true;
                offerFull = copy->head >= kN;
                nextSwap = reads + 25;
            }
            holdReleased.store(request, std::memory_order_release);
        }
        p.sources(0).collect();
        settle();
        const auto copy = gg::plugin::readRing(p, 0, {}, kRate);
        if (!copy.has_value())
        {
            continue;
        }
        ++reads;
        const gg::SampleBuffer &w = *copy->window;
        trimmed += copy->oldest > std::max<int64_t>(0, copy->head - kN) ? 1 : 0;
        if (copy->oldest + w.frames() != copy->head)
        {
            ++bad;
            continue;
        }
        for (int64_t f = 0; f < w.frames(); ++f)
        {
            const auto v = static_cast<float>((copy->oldest + f) % 1000000);
            if (w.channel(0)[f] != v || w.channel(1)[f] != -v)
            {
                ++bad;
                break;
            }
        }
    }
    stop.store(true, std::memory_order_release);
    audio.join();
    p.sources(0).collect();
    settle();
    INFO("reads " << reads << ", trimmed " << trimmed << ", swaps " << swaps << ", adopted "
                  << adopted << " (" << adoptedFull << " of a full ring), declined "
                  << p.ringAdoptionsDeclined(0));
    // Reaching a safety bound before the work was done fails here.
    CHECK_FALSE(hung.load());
    CHECK(reads >= kReads);
    CHECK(trimmed >= kTrimmed);
    CHECK(adoptedFull >= kAdopted); // adopted mid-run
    CHECK(bad == 0);
}

TEST_CASE("a save of a running ring keeps the window's length", "[plugin][threads]")
{
    // The controller's ruling: when the head laps the front of a save's copy, the save keeps W
    // frames and writes silence over that front, so the loop's period and the marks' offsets
    // survive. One thread renders a Processor whose 1 s ring takes a ramp (as the reader test's);
    // the other saves it (D1: Freeze on in the values saved) as fast as it can. Every saved ring
    // is N frames once the ring is full, silence then the ramp, contiguous.
    // Bounded by work, not time: the audio thread renders until the saver has made kSaves saves,
    // at least one of them lapped (the case the ruling is about), however fast either thread
    // runs. The audio thread's kMaxTotal is only a safety bound: reaching it fails the test.
    const fs::path dir = gg::test::useMediaScratch("ring-save-running");
    constexpr int64_t kN = 48000;
    constexpr uint32_t kPiece = 256;
    constexpr int64_t kMaxTotal = 600 * 48000;
    constexpr uint64_t kSaves = 6;
    gg::core::Processor p({kRate, kPiece, 7, gg::core::Mode::Fx});
    p.setBase(layerSlot(LayerParam::RingLength, 0), 0.0f); // 1 s
    gg::plugin::MediaSession media(gg::core::Mode::Fx, dir / "settings.xml", {}, {});
    media.activate(p, kRate, kPiece);
    REQUIRE(p.ringRead(0).storage->frames() == kN);
    gg::core::ParamValues values = fxDefaults().values;
    values[slotIndex(LayerParam::Freeze, 0)] = 1.0f;
    values[slotIndex(LayerParam::RingLength, 0)] = 0.0f;
    std::atomic<bool> stop{false};
    std::atomic<bool> done{false};
    std::atomic<bool> full{false};
    std::thread audio([&] {
        std::vector<float> left(kPiece), right(kPiece), outL(kPiece), outR(kPiece);
        const float *in[2] = {left.data(), right.data()};
        float *out[2] = {outL.data(), outR.data()};
        for (int64_t t = 0; t < kMaxTotal && !stop.load(std::memory_order_acquire); t += kPiece)
        {
            for (uint32_t i = 0; i < kPiece; ++i)
            {
                const auto v = static_cast<float>(1 + (t + i) % 1000000);
                left[i] = v;
                right[i] = -v;
            }
            p.process(in, 2, out, 2, kPiece, {});
            if (t >= 2 * kN)
            {
                full.store(true, std::memory_order_release);
            }
        }
        done.store(true, std::memory_order_release);
    });
    uint64_t saves = 0;
    uint64_t lapped = 0;
    uint64_t bad = 0;
    while (!done.load(std::memory_order_acquire) && !(saves >= kSaves && lapped > 0))
    {
        if (!full.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
            continue;
        }
        const gg::core::SessionState s = media.save(values, 0, {}, &p);
        if (!s.rings[0].has_value() || s.audio.size() != 1)
        {
            ++bad; // a running 1 s ring is always saved
            continue;
        }
        ++saves;
        const gg::media::DecodeResult wav =
            gg::media::decodeAudioBytes(s.audio[0].bytes, gg::media::kDefaultDecodeBound);
        if (s.rings[0]->frames != kN || !wav.audio.has_value() || wav.audio->buffer->frames() != kN)
        {
            ++bad;
            continue;
        }
        const gg::SampleBuffer &w = *wav.audio->buffer;
        int64_t f = 0;
        while (f < kN && w.channel(0)[f] == 0.0f && w.channel(1)[f] == 0.0f)
        {
            ++f; // the lapped front, silent (the ramp is never 0)
        }
        lapped += f > 0 ? 1 : 0;
        for (int64_t g = f + 1; g < kN; ++g)
        {
            const float expected =
                w.channel(0)[g - 1] == 1000000.0f ? 1.0f : w.channel(0)[g - 1] + 1.0f;
            if (w.channel(0)[g] != expected || w.channel(1)[g] != -expected)
            {
                ++bad;
                break;
            }
        }
    }
    stop.store(true, std::memory_order_release);
    audio.join();
    INFO("saves " << saves << ", lapped " << lapped);
    // Reaching the safety bound before the work was done fails here.
    CHECK(saves >= kSaves);
    CHECK(lapped > 0);
    CHECK(bad == 0);
    media.deactivate(nullptr, values);
}
