// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The instrument's sample loader, through the plugin as a host drives it (spec §6.4–§6.7): the
// synchronous session restore (D8), the background loads and relinks, the manifest entry, the
// search for a missing sample, and the source swaps after a load or a rate change. Each test's
// files go under GG_TEST_OUTPUT_DIR/plugin-sample-<name>/, with a settings file of its own.

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
#include <string_view>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <clap/clap.h>

#include "PluginHarness.h"
#include "SampleLoader.h"
#include "gg/core/MachineSettings.h"
#include "gg/core/MathDet.h"
#include "gg/core/Processor.h"
#include "gg/core/SessionState.h"
#include "gg/media/SettingsFile.h"
#include "support/StateContainer.h"

using gg::plugin::SampleState;
using gg::plugin::SampleStatus;
using gg::test::PluginUnderTest;
using gg::test::Stereo;

namespace
{
constexpr const char *kInstrumentId = "org.sethbc.giga-grains";
constexpr const char *kFxId = "org.sethbc.giga-grains-fx";
namespace fs = std::filesystem;

// A host that offers clap.state and counts state.mark_dirty, request_callback and request_restart
// calls. request_callback may come from the loader's worker thread.
class LoaderHost
{
  public:
    LoaderHost() { host_.host_data = this; }
    LoaderHost(const LoaderHost &) = delete;
    LoaderHost &operator=(const LoaderHost &) = delete;

    const clap_host *clap() const noexcept { return &host_; }
    uint32_t dirtyMarks() const noexcept { return dirtyMarks_; }
    uint32_t callbacks() const noexcept { return callbacks_.load(); }

  private:
    static LoaderHost &self(const clap_host *host)
    {
        return *static_cast<LoaderHost *>(host->host_data);
    }
    static const void *getExtension(const clap_host *, const char *id) noexcept
    {
        static const clap_host_state kState{
            [](const clap_host *host) { ++self(host).dirtyMarks_; }, // mark_dirty
        };
        return std::string_view(id) == CLAP_EXT_STATE ? &kState : nullptr;
    }

    uint32_t dirtyMarks_{0};
    std::atomic<uint32_t> callbacks_{0};
    clap_host host_{
        CLAP_VERSION_INIT,
        nullptr,
        "gg-tests loader host",
        "Giga Grains",
        "",
        "0.0.0",
        &LoaderHost::getExtension,
        [](const clap_host *) {}, // request_restart
        [](const clap_host *) {}, // request_process
        [](const clap_host *host) { ++self(host).callbacks_; },
    };
};

// This test's scratch folder, with a settings file of its own that the instances created from
// now on use. `folders` are written to it first.
fs::path setUp(const std::string &name, const gg::core::MachineSettings &folders = {})
{
    const fs::path dir = gg::test::sampleDir(name);
    const fs::path settings = dir / "settings.xml";
    fs::remove(settings);
    if (!(folders == gg::core::MachineSettings{}))
    {
        REQUIRE(gg::media::saveSettings(settings, folders));
    }
    gg::test::useSettingsFile(settings);
    return dir;
}

gg::plugin::SampleLoader &loaderOf(const PluginUnderTest &plugin)
{
    auto *loader = plugin.impl().sampleLoader();
    REQUIRE(loader != nullptr);
    return *loader;
}

// Calls on_main_thread, as a host does after request_callback, until the sample is no longer
// loading: the status then. Gives up after 60 s.
SampleStatus settle(const PluginUnderTest &plugin)
{
    auto &loader = loaderOf(plugin);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    for (;;)
    {
        plugin.onMainThread();
        const SampleStatus status = loader.status();
        if (status.state != SampleState::Loading || std::chrono::steady_clock::now() > deadline)
        {
            return status;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// The saved state's MEDI payload.
std::string savedManifest(const PluginUnderTest &plugin)
{
    return gg::test::stateChunkPayload(plugin.save(), "MEDI").value_or("<no MEDI>");
}

// The sample entry of the plugin's saved state, if any.
std::optional<gg::core::MediaEntry> savedEntry(const PluginUnderTest &plugin)
{
    gg::core::SessionState s;
    REQUIRE(gg::core::decodeState(plugin.save(), gg::core::Mode::Instrument, s));
    return s.sample;
}

float peak(const std::vector<float> &v, std::size_t from = 0,
           std::size_t to = static_cast<std::size_t>(-1))
{
    float p = 0.0f;
    for (std::size_t i = from; i < std::min(to, v.size()); ++i)
    {
        p = std::max(p, std::abs(v[i]));
    }
    return p;
}

// Renders out.left.size() frames in blocks of `block`, with a CLAP note-on of key 60 at the
// first frame.
void renderNote(PluginUnderTest &plugin, Stereo &out, uint32_t block)
{
    const auto on = gg::test::noteEvent(CLAP_EVENT_NOTE_ON, 0, 60, 0);
    const std::size_t frames = out.left.size();
    for (std::size_t done = 0; done < frames;)
    {
        const auto n = static_cast<uint32_t>(std::min<std::size_t>(block, frames - done));
        gg::test::InputEvents events;
        if (done == 0)
        {
            events.add(&on.header);
        }
        float *outputs[2] = {out.left.data() + done, out.right.data() + done};
        REQUIRE(plugin.process(n, nullptr, outputs, events.list()) == CLAP_PROCESS_CONTINUE);
        done += n;
    }
}

// The largest sample-to-sample step |x[i] − x[i−1]| in [from, to), over both channels.
float maxStep(const Stereo &s, std::size_t from, std::size_t to)
{
    float m = 0.0f;
    for (const auto *ch : {&s.left, &s.right})
    {
        for (std::size_t i = std::max<std::size_t>(from, 1); i < std::min(to, ch->size()); ++i)
        {
            m = std::max(m, std::abs((*ch)[i] - (*ch)[i - 1]));
        }
    }
    return m;
}

// Records each spawned grain's onset and rate into preallocated storage (the sink runs in
// process()).
struct RateLog
{
    struct Grain
    {
        int64_t onset;
        double rate;
    };
    static constexpr std::size_t kCapacity = 1 << 16;
    std::vector<Grain> grains = std::vector<Grain>(kCapacity);
    std::size_t count{0};

    static void record(void *ctx, const gg::GrainSpec &g)
    {
        auto &self = *static_cast<RateLog *>(ctx);
        if (self.count < kCapacity)
        {
            self.grains[self.count++] = {g.onsetSample, g.rate};
        }
    }
};
} // namespace

TEST_CASE("a session restored while active plays in the first block after it (D8)", "[plugin]")
{
    setUp("d8-active");
    const fs::path file = gg::test::sineSampleFile("d8-active");
    LoaderHost host;
    PluginUnderTest instrument(kInstrumentId, host.clap());
    REQUIRE(instrument.valid());
    REQUIRE(instrument.activate(48000.0, 512));
    const std::string state = gg::test::withSample(instrument.save(), gg::test::entryOf(file));
    REQUIRE_FALSE(state.empty());

    REQUIRE(instrument.load(state));
    // Ready before stateLoad returned: no on_main_thread, no waiting.
    CHECK(loaderOf(instrument).status().state == SampleState::Loaded);
    Stereo out(512);
    renderNote(instrument, out, 512);
    CHECK(peak(out.left) > 0.01f);
    CHECK(peak(out.right) > 0.01f);
}

TEST_CASE("a session restored while inactive plays in the first block after activate (D8)",
          "[plugin]")
{
    setUp("d8-inactive");
    const fs::path file = gg::test::sineSampleFile("d8-inactive");
    LoaderHost host;
    PluginUnderTest instrument(kInstrumentId, host.clap());
    REQUIRE(instrument.valid());
    REQUIRE(instrument.load(gg::test::withSample(instrument.save(), gg::test::entryOf(file))));
    CHECK(loaderOf(instrument).status().state == SampleState::Loaded);

    REQUIRE(instrument.activate(44100.0, 512)); // converts here, at the host's rate
    Stereo out(512);
    renderNote(instrument, out, 512);
    CHECK(peak(out.left) > 0.01f);
}

TEST_CASE("two instances restoring one session render the same bits (D8)", "[plugin]")
{
    // An export: one host restores the session and then activates; another activates first,
    // then restores it. Both must render the same bits from the first block on, note included.
    setUp("d8-identical");
    const fs::path file = gg::test::sineSampleFile("d8-identical");
    std::string state;
    {
        PluginUnderTest source(kInstrumentId);
        REQUIRE(source.valid());
        state = gg::test::withSample(source.save(), gg::test::entryOf(file));
    }
    REQUIRE_FALSE(state.empty());

    constexpr std::size_t kFrames = 24000;
    LoaderHost hostA;
    PluginUnderTest a(kInstrumentId, hostA.clap());
    REQUIRE(a.valid());
    REQUIRE(a.load(state));
    REQUIRE(a.activate(48000.0, 256));
    Stereo outA(kFrames);
    renderNote(a, outA, 256);

    LoaderHost hostB;
    PluginUnderTest b(kInstrumentId, hostB.clap());
    REQUIRE(b.valid());
    REQUIRE(b.activate(48000.0, 1024));
    REQUIRE(b.load(state));
    Stereo outB(kFrames);
    renderNote(b, outB, 1024);

    CHECK(peak(outA.left) > 0.05f);
    CHECK(gg::test::sameBits(outA.left, outB.left));
    CHECK(gg::test::sameBits(outA.right, outB.right));
}

TEST_CASE("a missing sample stays silent and is saved unchanged", "[plugin]")
{
    // Review Focus 3 (spec §6.6, §6.7): a session whose sample can't be found loads, says so,
    // stays silent, and saves the very entry it loaded, so the reference survives.
    setUp("missing");
    const fs::path file = gg::test::sineSampleFile("missing");
    const gg::core::MediaEntry entry = gg::test::entryOf(file);
    REQUIRE(gg::core::isValidMediaEntry(entry));

    LoaderHost host;
    PluginUnderTest instrument(kInstrumentId, host.clap());
    REQUIRE(instrument.valid());
    std::string state;

    SECTION("missing")
    {
        fs::remove(file);
        state = gg::test::withSample(instrument.save(), entry);
        REQUIRE(instrument.load(state));
        CHECK(loaderOf(instrument).status().state == SampleState::Missing);
    }
    SECTION("changed")
    {
        // The saved path holds other audio: not a match, and the status says the file changed.
        gg::test::writeSineWav(file, 48000, 4.0, 330.0);
        state = gg::test::withSample(instrument.save(), entry);
        REQUIRE(instrument.load(state));
        CHECK(loaderOf(instrument).status().state == SampleState::Changed);
    }
    CHECK(loaderOf(instrument).status().fileName == "missing.wav");
    CHECK_FALSE(loaderOf(instrument).status().message.empty());
    CHECK(host.dirtyMarks() == 0);

    const std::string loadedManifest = gg::test::stateChunkPayload(state, "MEDI").value();
    CHECK(savedManifest(instrument) == loadedManifest);

    REQUIRE(instrument.activate(48000.0, 512));
    Stereo out(4800);
    renderNote(instrument, out, 512);
    CHECK(peak(out.left) == 0.0f);
    CHECK(peak(out.right) == 0.0f);
    // Still the same entry once active, and after a deactivation.
    CHECK(savedManifest(instrument) == loadedManifest);
    instrument.deactivate();
    CHECK(savedManifest(instrument) == loadedManifest);
}

TEST_CASE("a sample moved into the sample folder is found, and the state marked dirty", "[plugin]")
{
    const fs::path dir = gg::test::sampleDir("moved");
    const fs::path library = dir / "library";
    fs::remove_all(library);
    setUp("moved", {library.string(), ""});
    const fs::path saved = gg::test::sineSampleFile("moved");
    const gg::core::MediaEntry entry = gg::test::entryOf(saved);
    const fs::path moved = library / "drums" / "moved.wav";
    fs::create_directories(moved.parent_path());
    fs::rename(saved, moved);

    LoaderHost host;
    PluginUnderTest instrument(kInstrumentId, host.clap());
    REQUIRE(instrument.valid());
    REQUIRE(instrument.load(gg::test::withSample(instrument.save(), entry)));
    const SampleStatus status = loaderOf(instrument).status();
    CHECK(status.state == SampleState::Loaded);
    CHECK(status.fileName == "moved.wav");
    CHECK(status.channels == 2);
    CHECK(status.fileRate == 48000.0);
    CHECK(status.seconds == 4.0);
    CHECK(host.dirtyMarks() == 1);

    const auto after = savedEntry(instrument);
    REQUIRE(after.has_value());
    CHECK(fs::path(after->absolutePath) == fs::absolute(moved).lexically_normal());
    CHECK(after->relativePath == "drums/moved.wav");
    CHECK(after->sha256Hex == entry.sha256Hex);

    REQUIRE(instrument.activate(48000.0, 512));
    Stereo out(512);
    renderNote(instrument, out, 512);
    CHECK(peak(out.left) > 0.01f);
}

TEST_CASE("a load superseded at once never becomes current", "[plugin]")
{
    // Review Focus 2 (spec §6.4): a request supersedes the one before it, waiting or under way;
    // only the last one's result applies. Nothing leaks (ASan's leak check covers this test).
    const fs::path dir = setUp("supersede");
    const fs::path a = gg::test::writeSineWav(dir / "a.wav", 44100, 20.0, 220.0);
    const fs::path b = gg::test::writeSineWav(dir / "b" / "b.wav", 44100, 1.0, 330.0);
    LoaderHost host;
    PluginUnderTest instrument(kInstrumentId, host.clap());
    REQUIRE(instrument.valid());
    REQUIRE(instrument.activate(48000.0, 512));
    auto &loader = loaderOf(instrument);

    loader.requestLoad(a);
    CHECK(loader.status().state == SampleState::Loading);
    CHECK(loader.status().fileName == "a.wav");
    loader.requestLoad(b);
    CHECK(loader.status().fileName == "b.wav");

    const SampleStatus status = settle(instrument);
    CHECK(status.state == SampleState::Loaded);
    CHECK(status.fileName == "b.wav");
    CHECK(status.seconds == 1.0);
    CHECK(loader.resultsApplied() == 1);
    // A's result, if it was under way, is dropped too: nothing more applies.
    for (int i = 0; i < 50; ++i)
    {
        instrument.onMainThread();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(loader.resultsApplied() == 1);
    const auto entry = savedEntry(instrument);
    REQUIRE(entry.has_value());
    CHECK(fs::path(entry->absolutePath).filename() == "b.wav");
    CHECK(host.dirtyMarks() == 1);
    // The settings remember the folder loaded from (spec §6.7), written on the main thread.
    CHECK(fs::path(gg::media::loadSettings(dir / "settings.xml").lastLoadFolder) ==
          fs::absolute(b).lexically_normal().parent_path());

    Stereo out(4800);
    renderNote(instrument, out, 512);
    CHECK(peak(out.left) > 0.01f);
}

TEST_CASE("a load while inactive plays in the first block after activate", "[plugin]")
{
    setUp("inactive-load");
    const fs::path file = gg::test::sineSampleFile("inactive-load");
    LoaderHost host;
    PluginUnderTest instrument(kInstrumentId, host.clap());
    REQUIRE(instrument.valid());
    auto &loader = loaderOf(instrument);
    loader.requestLoad(file);
    CHECK(settle(instrument).state == SampleState::Loaded);
    CHECK(host.callbacks() >= 1);
    CHECK(host.dirtyMarks() == 1);

    REQUIRE(instrument.activate(44100.0, 512));
    Stereo out(512);
    renderNote(instrument, out, 512);
    CHECK(peak(out.left) > 0.01f);
}

TEST_CASE("a source converts again after a rate change, without a click", "[plugin]")
{
    // Review Focus 5 (spec §6.4). A 44.1 kHz file, loaded at 44.1 kHz, then the plugin
    // reactivated at 48 kHz while a note plays: the 44.1 kHz source plays at once, its grains'
    // rates scaled by 44100 / 48000, until the background conversion swaps in the 48 kHz one,
    // whose grains run at exactly 1. The swap adds no step beyond what the sound already has.
    const fs::path dir = setUp("reconvert");
    const fs::path file = gg::test::writeSineWav(dir / "reconvert.wav", 44100, 4.0, 220.0);
    constexpr uint32_t kBlock = 256;
    constexpr std::size_t kWindow = 2400; // 50 ms at 48 kHz

    // The reference: the file at 48 kHz from the start.
    float steadyStep = 0.0f;
    {
        PluginUnderTest reference(kInstrumentId);
        REQUIRE(reference.valid());
        REQUIRE(reference.activate(48000.0, kBlock));
        REQUIRE(reference.loadSampleState(file));
        Stereo out(48000);
        renderNote(reference, out, kBlock);
        steadyStep = maxStep(out, 14400, 48000); // after the first grains' attack
        REQUIRE(steadyStep > 0.0f);
    }

    LoaderHost host;
    PluginUnderTest instrument(kInstrumentId, host.clap());
    REQUIRE(instrument.valid());
    REQUIRE(instrument.activate(44100.0, kBlock));
    REQUIRE(instrument.loadSampleState(file));
    instrument.deactivate();
    REQUIRE(instrument.activate(48000.0, kBlock));
    RateLog log;
    instrument.impl().processor()->setEventSink(&RateLog::record, &log);

    // Render until the 48 kHz source has played 0.3 s, pumping on_main_thread between blocks.
    // The render runs faster than real time, so the swap's sample depends on the machine; at
    // most 60 s of audio.
    const double before = gg::exp2Det(0.0) * 44100.0 / 48000.0;
    std::vector<float> left;
    std::vector<float> right;
    const auto on = gg::test::noteEvent(CLAP_EVENT_NOTE_ON, 0, 60, 0);
    std::optional<int64_t> swap;
    Stereo block(kBlock);
    for (std::size_t done = 0; done < 60 * 48000; done += kBlock)
    {
        gg::test::InputEvents events;
        if (done == 0)
        {
            events.add(&on.header);
        }
        REQUIRE(instrument.process(kBlock, nullptr, block.data(), events.list()) ==
                CLAP_PROCESS_CONTINUE);
        left.insert(left.end(), block.left.begin(), block.left.end());
        right.insert(right.end(), block.right.begin(), block.right.end());
        instrument.onMainThread();
        for (std::size_t g = 0; !swap.has_value() && g < log.count; ++g)
        {
            if (log.grains[g].rate == 1.0)
            {
                swap = log.grains[g].onset;
            }
        }
        if (swap.has_value() && static_cast<int64_t>(left.size()) > *swap + 14400)
        {
            break;
        }
        if (!swap.has_value())
        {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }
    REQUIRE(swap.has_value());
    REQUIRE(*swap > 0); // the old source played first
    std::size_t oldGrains = 0;
    for (std::size_t g = 0; g < log.count; ++g)
    {
        const auto &grain = log.grains[g];
        if (grain.onset < *swap)
        {
            CHECK(grain.rate == before);
            ++oldGrains;
        }
        else
        {
            CHECK(grain.rate == 1.0);
        }
    }
    CHECK(oldGrains > 0);
    CHECK(loaderOf(instrument).status().state == SampleState::Loaded);

    Stereo rendered(static_cast<uint32_t>(left.size()));
    rendered.left = left;
    rendered.right = right;
    const auto s = static_cast<std::size_t>(*swap);
    const float swapStep = maxStep(rendered, s > kWindow ? s - kWindow : 0, s + kWindow);
    CAPTURE(steadyStep, swapStep, *swap);
    CHECK(swapStep <= steadyStep * 1.5f);
}

TEST_CASE("a sample loaded while a note plays swaps in without a click", "[plugin]")
{
    // Review Focus 5 (spec §6.4), the user's load: a 44.1 kHz file loaded into an instance
    // active at 48 kHz, playing another sample under a held note. Grains that started on the old
    // source finish there; the new one's grains run at exactly 1, converted on the worker.
    const fs::path dir = setUp("swap");
    const fs::path first = gg::test::writeSineWav(dir / "first.wav", 48000, 4.0, 220.0);
    const fs::path second = gg::test::writeSineWav(dir / "second.wav", 44100, 4.0, 220.0);
    constexpr uint32_t kBlock = 256;
    constexpr std::size_t kWindow = 2400;
    constexpr std::size_t kLoadAt = 94 * kBlock; // about 0.5 s in: the first sample is steady

    LoaderHost host;
    PluginUnderTest instrument(kInstrumentId, host.clap());
    REQUIRE(instrument.valid());
    REQUIRE(instrument.activate(48000.0, kBlock));
    REQUIRE(instrument.loadSampleState(first));
    RateLog log;
    instrument.impl().processor()->setEventSink(&RateLog::record, &log);

    std::vector<float> left;
    std::vector<float> right;
    const auto on = gg::test::noteEvent(CLAP_EVENT_NOTE_ON, 0, 60, 0);
    std::optional<std::size_t> loadedAt; // the sample of the first block after the swap's offer
    Stereo block(kBlock);
    auto &loader = loaderOf(instrument);
    for (std::size_t done = 0; done < 60 * 48000; done += kBlock)
    {
        gg::test::InputEvents events;
        if (done == 0)
        {
            events.add(&on.header);
        }
        REQUIRE(instrument.process(kBlock, nullptr, block.data(), events.list()) ==
                CLAP_PROCESS_CONTINUE);
        left.insert(left.end(), block.left.begin(), block.left.end());
        right.insert(right.end(), block.right.begin(), block.right.end());
        if (done == kLoadAt)
        {
            loader.requestLoad(second);
        }
        instrument.onMainThread();
        if (!loadedAt.has_value() && loader.resultsApplied() == 1)
        {
            loadedAt = left.size();
        }
        if (loadedAt.has_value() && left.size() > *loadedAt + 14400)
        {
            break;
        }
        if (done >= kLoadAt && !loadedAt.has_value())
        {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }
    REQUIRE(loadedAt.has_value());
    CHECK(loader.status().fileName == "second.wav");
    for (std::size_t g = 0; g < log.count; ++g)
    {
        CHECK(log.grains[g].rate == 1.0);
    }

    Stereo rendered(static_cast<uint32_t>(left.size()));
    rendered.left = left;
    rendered.right = right;
    const float steadyStep = maxStep(rendered, 14400, kLoadAt);
    const float swapStep = maxStep(rendered, *loadedAt - kWindow, *loadedAt + kWindow);
    CAPTURE(steadyStep, swapStep, *loadedAt);
    REQUIRE(steadyStep > 0.0f);
    CHECK(swapStep <= steadyStep * 1.5f);
}

TEST_CASE("relink restores the saved sample, or loads a replacement and says so", "[plugin]")
{
    // Spec §6.7: a missing sample, relinked to a file with its fingerprint, is the saved sample
    // again; a file with another fingerprint loads as a replacement, and the status says so.
    const fs::path dir = setUp("relink");
    const fs::path original = gg::test::sineSampleFile("relink");
    const gg::core::MediaEntry entry = gg::test::entryOf(original);
    const fs::path copy = dir / "elsewhere" / "renamed.wav";
    fs::create_directories(copy.parent_path());
    fs::remove(copy);
    fs::rename(original, copy); // not where any search looks: another folder, another name
    const fs::path other = gg::test::writeSineWav(dir / "other.wav", 48000, 2.0, 330.0);

    LoaderHost host;
    PluginUnderTest instrument(kInstrumentId, host.clap());
    REQUIRE(instrument.valid());
    REQUIRE(instrument.activate(48000.0, 512));
    REQUIRE(instrument.load(gg::test::withSample(instrument.save(), entry)));
    auto &loader = loaderOf(instrument);
    REQUIRE(loader.status().state == SampleState::Missing);

    loader.requestRelink(copy);
    SampleStatus status = settle(instrument);
    CHECK(status.state == SampleState::Loaded);
    CHECK(status.fileName == "renamed.wav");
    CHECK(status.message.empty());
    auto saved = savedEntry(instrument);
    REQUIRE(saved.has_value());
    CHECK(saved->sha256Hex == entry.sha256Hex);
    CHECK(fs::path(saved->absolutePath).filename() == "renamed.wav");

    loader.requestRelink(other);
    status = settle(instrument);
    CHECK(status.state == SampleState::Loaded);
    CHECK(status.fileName == "other.wav");
    CHECK(status.message.find("different audio than the saved sample") != std::string::npos);
    saved = savedEntry(instrument);
    REQUIRE(saved.has_value());
    CHECK(saved->sha256Hex != entry.sha256Hex);
    CHECK(host.dirtyMarks() == 2);

    Stereo out(4800);
    renderNote(instrument, out, 512);
    CHECK(peak(out.left) > 0.01f);
}

TEST_CASE("a failed load keeps the sample and the entry it had", "[plugin]")
{
    const fs::path dir = setUp("failed");
    const fs::path good = gg::test::sineSampleFile("failed");
    const fs::path bad = dir / "not-audio.wav";
    gg::test::writeFile(bad, std::vector<uint8_t>(100, 0x42));

    LoaderHost host;
    PluginUnderTest instrument(kInstrumentId, host.clap());
    REQUIRE(instrument.valid());
    REQUIRE(instrument.activate(48000.0, 512));
    REQUIRE(instrument.loadSampleState(good));
    const std::string manifest = savedManifest(instrument);
    auto &loader = loaderOf(instrument);
    loader.requestLoad(bad);
    const SampleStatus status = settle(instrument);
    CHECK(status.state == SampleState::Error);
    CHECK_FALSE(status.message.empty());
    CHECK(savedManifest(instrument) == manifest);
    CHECK(host.dirtyMarks() == 0);

    Stereo out(512);
    renderNote(instrument, out, 512);
    CHECK(peak(out.left) > 0.01f); // the sample it had still plays
}

TEST_CASE("a session without a sample clears the one loaded", "[plugin]")
{
    setUp("clear");
    const fs::path file = gg::test::sineSampleFile("clear");
    LoaderHost host;
    PluginUnderTest instrument(kInstrumentId, host.clap());
    REQUIRE(instrument.valid());
    REQUIRE(instrument.activate(48000.0, 512));
    const std::string empty = instrument.save();
    REQUIRE(instrument.loadSampleState(file));
    REQUIRE(instrument.load(empty));
    CHECK(loaderOf(instrument).status().state == SampleState::None);
    CHECK_FALSE(savedEntry(instrument).has_value());
    Stereo out(4800);
    renderNote(instrument, out, 512);
    CHECK(peak(out.left) == 0.0f);
}

TEST_CASE("the FX has no loader and saves an empty manifest", "[plugin]")
{
    setUp("fx");
    const fs::path file = gg::test::sineSampleFile("fx");
    PluginUnderTest fx(kFxId);
    REQUIRE(fx.valid());
    CHECK(fx.impl().sampleLoader() == nullptr);
    // A state carrying a sample (from the instrument, say) loads; the FX ignores the sample.
    gg::core::SessionState s;
    REQUIRE(gg::core::decodeState(fx.save(), gg::core::Mode::Fx, s));
    s.sample = gg::test::entryOf(file);
    REQUIRE(fx.load(gg::core::encodeState(s)));
    CHECK(savedManifest(fx) == "<media version=\"1\" />");
}
