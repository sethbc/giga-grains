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

TEST_CASE("every layer loads a sample in both products", "[plugin][media]")
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

TEST_CASE("the ring view follows the ring", "[plugin][media]")
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

    context.ringCommand(0, gg::ui::RingCommand::AddSplice, 0.25f);
    rig.process(kBlock);
    CHECK(context.ringView(0).spliceCount == 1);
    CHECK(context.ringView(0).splices[0] > 0);
    context.ringCommand(0, gg::ui::RingCommand::ClearSplices, 0.0f);
    rig.process(kBlock);
    CHECK(context.ringView(0).spliceCount == 0);
}

TEST_CASE("capture loads the window into a layer", "[plugin][media]")
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

TEST_CASE("capture refuses honestly", "[plugin][media]")
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

TEST_CASE("the memory cap is a saved setting", "[plugin][media]")
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

TEST_CASE("an unchanged route set marks nothing", "[plugin][media]")
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
#endif
