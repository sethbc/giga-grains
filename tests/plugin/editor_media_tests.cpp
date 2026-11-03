// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The editor's media and ring API (M2 sub-project 6, PR 2, Task 4): the per-layer sample members
// of gg::ui::EditorContext, the ring's metadata and splice commands, capture, and the memory
// cap, as EditorPlugin fills them.

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <clap/clap.h>

#include "PluginHarness.h"

#if GG_BUILD_EDITOR
#include <juce_gui_basics/juce_gui_basics.h>

#include "editor/EditorPlugin.h"
#include "gg/core/ModMatrix.h"
#include "gg/ui/Editor.h"

using gg::test::PluginUnderTest;
using gg::test::Stereo;

namespace
{
constexpr const char *kInstrumentId = "org.sethbc.giga-grains";
constexpr const char *kFxId = "org.sethbc.giga-grains-fx";
constexpr uint32_t kBlock = 512;

// A host that offers clap.state and counts state.mark_dirty.
class StateHost
{
  public:
    StateHost() { host_.host_data = this; }
    StateHost(const StateHost &) = delete;
    StateHost &operator=(const StateHost &) = delete;

    const clap_host *clap() const noexcept { return &host_; }
    uint32_t dirtyMarks() const noexcept { return dirtyMarks_; }

  private:
    static const void *getExtension(const clap_host *, const char *id) noexcept
    {
        static const clap_host_state kState{
            [](const clap_host *host) { ++static_cast<StateHost *>(host->host_data)->dirtyMarks_; },
        };
        return std::strcmp(id, CLAP_EXT_STATE) == 0 ? &kState : nullptr;
    }

    uint32_t dirtyMarks_{0};
    clap_host host_{
        CLAP_VERSION_INIT,
        nullptr,
        "gg-tests state host",
        "Giga Grains",
        "",
        "0.0.0",
        &StateHost::getExtension,
        [](const clap_host *) {}, // request_restart
        [](const clap_host *) {}, // request_process
        [](const clap_host *) {}, // request_callback
    };
};

// An editor over a plugin, whose context the cases call.
struct Rig
{
    explicit Rig(const char *id, const clap_host *host = gg::test::stubHost()) : plugin(id, host)
    {
        REQUIRE(plugin.valid());
        impl = dynamic_cast<gg::plugin::EditorPlugin *>(&plugin.impl());
        REQUIRE(impl != nullptr);
        editor = impl->createEditor();
        REQUIRE(editor != nullptr);
    }
    const gg::ui::EditorContext &context() const { return impl->editorContext(); }

    // `frames` frames of noise through process(), kBlock at a time (the instrument: silence).
    void process(uint32_t frames, uint64_t seed = 1)
    {
        const bool fx = impl->mode() == gg::core::Mode::Fx;
        for (uint32_t done = 0; done < frames; done += kBlock)
        {
            const uint32_t n = std::min(kBlock, frames - done);
            Stereo in(n);
            Stereo out(n);
            gg::test::fillNoise(in, seed + done);
            REQUIRE(plugin.process(n, fx ? in.data() : nullptr, out.data()) ==
                    CLAP_PROCESS_CONTINUE);
        }
    }

    // Polls layer `layer`'s status until it is `state` (the loader's worker finishes it).
    bool waitFor(uint8_t layer, gg::ui::SampleStatusView::State state)
    {
        for (int i = 0; i < 1000; ++i)
        {
            if (context().sampleStatusFor(layer).state == state)
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    juce::ScopedJuceInitialiser_GUI juceGui;
    PluginUnderTest plugin;
    gg::plugin::EditorPlugin *impl{nullptr};
    std::unique_ptr<juce::Component> editor;
};
} // namespace

TEST_CASE("every layer loads a sample in both products", "[plugin]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    Rig rig(id);
    const auto wav = gg::test::sineSampleFile("editor-media-load");
    const auto &context = rig.context();
    REQUIRE(context.loadSampleFor);
    REQUIRE(context.sampleStatusFor);
    REQUIRE(context.sampleWaveformFor);
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        CAPTURE(int(layer));
        CHECK(context.sampleStatusFor(layer).state == gg::ui::SampleStatusView::State::None);
        REQUIRE(context.loadSampleFor(layer, wav.string()));
        REQUIRE(rig.waitFor(layer, gg::ui::SampleStatusView::State::Loaded));
        CHECK(context.sampleWaveformFor(layer).peaks != nullptr);
        CHECK(context.sampleWaveformFor(layer).frames > 0);
    }
    CHECK_FALSE(context.loadSampleFor(gg::core::kLayers, wav.string()));
    CHECK(context.sampleStatusFor(gg::core::kLayers).state ==
          gg::ui::SampleStatusView::State::None);
}

TEST_CASE("the ring view follows the ring", "[plugin]")
{
    Rig rig(kFxId);
    const auto &context = rig.context();
    REQUIRE(context.ringView);
    CHECK_FALSE(context.ringView(0).valid); // inactive
    REQUIRE(rig.plugin.activate(48000.0, kBlock));
    rig.process(48000);
    const auto view = context.ringView(0);
    REQUIRE(view.valid);
    CHECK(view.head == rig.impl->processor()->ringMeta(0).head);
    CHECK(view.head > 0);
    CHECK(view.spliceCount == 0);
    // The ring's length: its storage's frames, the 16 s default (what a Live lane spans).
    CHECK(view.ringFrames == rig.impl->processor()->ringRead(0).storage->frames());
    CHECK(view.ringFrames == 16 * 48000);

    context.ringCommand(0, gg::ui::RingCommand::AddSplice, 0.25f);
    rig.process(kBlock);
    CHECK(context.ringView(0).spliceCount == 1);
    CHECK(context.ringView(0).splices[0] > 0);
    context.ringCommand(0, gg::ui::RingCommand::ClearSplices, 0.0f);
    rig.process(kBlock);
    CHECK(context.ringView(0).spliceCount == 0);
}

TEST_CASE("capture loads the window into a layer", "[plugin]")
{
    Rig rig(kFxId);
    const auto &context = rig.context();
    REQUIRE(rig.plugin.activate(48000.0, kBlock));
    rig.process(2 * 48000);
    CHECK(context.sampleStatusFor(1).state == gg::ui::SampleStatusView::State::None);
    CHECK(context.captureTo(0, 0, 1).empty());
    CHECK(rig.waitFor(1, gg::ui::SampleStatusView::State::Loaded));
    CHECK(context.sampleWaveformFor(1).frames > 0);

    // The same window saved as a WAV.
    const auto file = gg::test::sampleDir("editor-media-capture") / "capture.wav";
    std::filesystem::remove(file);
    CHECK(context.saveCaptureTo(0, 0, file.string()).empty());
    CHECK(std::filesystem::exists(file));
    CHECK_FALSE(context.capturesFolder().empty());
}

TEST_CASE("the editor's capture takes the splice the engine plays", "[plugin]")
{
    // Row 386: the context's captureTo is the lane's Capture, so it takes the Splice parameter's
    // splice as the engine numbers it (from the first mark in storage, Loop ignored).
    Rig rig(kFxId);
    const auto &context = rig.context();
    REQUIRE(rig.plugin.activate(48000.0, kBlock));
    rig.process(3 * 48000);
    context.ringCommand(0, gg::ui::RingCommand::AddSplice, 1.0f);
    rig.process(kBlock);
    context.ringCommand(0, gg::ui::RingCommand::AddSplice, 0.5f);
    rig.process(kBlock);
    const auto set = [&](gg::core::LayerParam p, double v) {
        const auto slot = gg::core::layerSlot(p, 0);
        context.beginGesture(slot);
        context.setValue(slot, v);
        context.endGesture(slot);
    };
    set(gg::core::LayerParam::Loop, 1.0);
    set(gg::core::LayerParam::LoopLength, -2.0); // 0.25 s: both marks are older
    set(gg::core::LayerParam::Splice, 1.0);
    rig.process(kBlock);
    const gg::ui::RingView ring = context.ringView(0);
    REQUIRE(ring.spliceCount == 2);
    REQUIRE(ring.splices[1] < ring.head - 12000);
    CHECK(context.captureTo(0, 1, 2).empty());
    REQUIRE(rig.waitFor(2, gg::ui::SampleStatusView::State::Loaded));
    CHECK(context.sampleWaveformFor(2).frames == ring.splices[1] - ring.splices[0]);
}

TEST_CASE("the editor's collect summarizes what it copied and what was missing", "[plugin]")
{
    // Row 388 (j): EditorPlugin's collectSamples over a real session, "Collected N files", and
    // "; M missing" when a sample's file isn't there. (Its captures folder: row 384's test.)
    const std::filesystem::path dir = gg::test::useMediaScratch("editor-collect-summary");
    const auto a = gg::test::writeSineWav(dir / "one" / "a.wav", 48000, 0.5);
    const auto b = gg::test::writeSineWav(dir / "two" / "b.wav", 48000, 0.5, 330.0);
    const auto gone = gg::test::writeSineWav(dir / "three" / "c.wav", 48000, 0.5, 440.0);
    Rig rig(kInstrumentId);
    const auto &context = rig.context();
    REQUIRE(context.collectSamples);
    gg::core::SessionState s;
    REQUIRE(gg::core::decodeState(rig.plugin.save(), gg::core::Mode::Instrument, s));
    s.samples[0] = gg::test::entryOf(a);
    s.samples[1] = gg::test::entryOf(gone);
    s.samples[1]->layer = 1;
    s.samples[2] = gg::test::entryOf(b);
    s.samples[2]->layer = 2;
    std::filesystem::remove(gone);
    REQUIRE(rig.plugin.load(gg::core::encodeState(s)));
    const std::filesystem::path folder = dir / "collected";
    std::filesystem::remove_all(folder);
    CHECK(context.collectSamples(gg::test::utf8(folder)) == "Collected 2 files; 1 missing");
    CHECK(std::filesystem::exists(folder / "a.wav"));
    CHECK(std::filesystem::exists(folder / "b.wav"));

    // Nothing missing: no "missing" part.
    Rig fresh(kInstrumentId);
    const std::filesystem::path none = dir / "collected-none";
    std::filesystem::remove_all(none);
    CHECK(fresh.context().collectSamples(gg::test::utf8(none)) == "Collected 0 files");
}

TEST_CASE("capture refuses honestly", "[plugin]")
{
    Rig rig(kFxId);
    const auto &context = rig.context();
    CHECK(context.captureTo(0, 0, 1) == "the plugin isn't running");
    CHECK(context.saveCaptureTo(0, 0, "x.wav") == "the plugin isn't running");

    // At 192 kHz the four 16 s rings take about 98 MB, past a cap of 64 MiB, so no capture fits.
    REQUIRE(rig.plugin.activate(192000.0, kBlock));
    rig.process(kBlock);
    context.setMemoryCapMiB(64);
    const std::string message = context.captureTo(0, 0, 1);
    CAPTURE(message);
    CHECK(message.starts_with("over the memory cap"));
}

TEST_CASE("the memory cap is a saved setting", "[plugin]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    StateHost host;
    Rig rig(id, host.clap());
    const auto &context = rig.context();
    REQUIRE(context.memory);
    CHECK(context.memory().capMiB == 1024);
    const uint64_t before = context.memory().usedBytes;

    const uint32_t marks = host.dirtyMarks();
    context.setMemoryCapMiB(512);
    CHECK(context.memory().capMiB == 512);
    CHECK(host.dirtyMarks() == marks + 1);

    const std::string saved = rig.plugin.save();
    REQUIRE_FALSE(saved.empty());
    PluginUnderTest again(id);
    REQUIRE(again.valid());
    REQUIRE(again.load(saved));
    CHECK(again.impl().media().memoryCapMiB() == 512);

    context.setMemoryCapMiB(1);
    CHECK(context.memory().capMiB == 64);

    REQUIRE(context.loadSampleFor(2, gg::test::sineSampleFile("editor-media-used").string()));
    REQUIRE(rig.waitFor(2, gg::ui::SampleStatusView::State::Loaded));
    CHECK(context.memory().usedBytes > before);
}

TEST_CASE("an unchanged route set marks nothing", "[plugin]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    StateHost host;
    PluginUnderTest plugin(id, host.clap());
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, kBlock));
    auto routes = gg::core::factoryRoutes(plugin.impl().mode());
    const uint32_t marks = host.dirtyMarks();
    plugin.impl().uiSetRoutes(routes);
    CHECK(host.dirtyMarks() == marks);
    routes[12].depth += 1.0f;
    plugin.impl().uiSetRoutes(routes);
    CHECK(host.dirtyMarks() == marks + 1);
    plugin.impl().uiSetRoutes(routes);
    CHECK(host.dirtyMarks() == marks + 1);
}

TEST_CASE("a load past the memory cap is refused and unloads nothing", "[plugin]")
{
    Rig rig(kFxId);
    const auto &context = rig.context();
    const auto wav = gg::test::sineSampleFile("editor-media-over-cap");
    REQUIRE(rig.plugin.activate(192000.0, kBlock)); // the four rings take about 98 MB
    rig.process(kBlock);
    REQUIRE(context.loadSampleFor(0, wav.string()));
    REQUIRE(rig.waitFor(0, gg::ui::SampleStatusView::State::Loaded));
    const int64_t frames = context.sampleWaveformFor(0).frames;
    REQUIRE(frames > 0);

    context.setMemoryCapMiB(64); // below what is in use
    context.loadSampleFor(1, wav.string());
    REQUIRE(rig.waitFor(1, gg::ui::SampleStatusView::State::Error));
    const std::string message = context.sampleStatusFor(1).message;
    CAPTURE(message);
    // A load's refusal names the file ("x.wav: needs 2 MB, the memory cap allows 0 MB"); a
    // capture's starts "over the memory cap" (MediaSession's two wordings).
    CHECK(message.find("the memory cap allows") != std::string::npos);
    CHECK(context.sampleStatusFor(0).state == gg::ui::SampleStatusView::State::Loaded);
    CHECK(context.sampleWaveformFor(0).frames == frames);
}
#endif
