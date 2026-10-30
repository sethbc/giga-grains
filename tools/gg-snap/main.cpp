// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// gg-snap <out-dir> [--sizes 960x600,640x360]
//
// Renders the editor's four views, in both products and both themes, to PNGs (M2 sub-project 6's
// spec §6.13): <product>-<view>-<theme>-<w>x<h>.png, with product fx or inst, view perform, edit,
// mod or fx, and theme dark or light. Headless: the plugins are made through their CLAP factory,
// as tests/plugin/editor_frame_tests.cpp makes them, and each editor is drawn with
// createComponentSnapshot, never shown in a window.
//
// What the pictures show: the instrument has a generated 2 s sine loaded (written as a scratch
// WAV), both products have layers B and C on and one route added, a few hundred blocks have run so
// the canvas has grains, and the canvas draws GG_STRESS_GRAINS=2048 synthetic grains (the seam is
// read when the canvas is made, so it is set before the editor). The per-machine settings point at
// a scratch file, so the tool never reads or writes the user's.
//
// Exit status: 0, or 1 with a message on stderr (2 for bad usage).

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <clap/clap.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Factory.h"
#include "editor/EditorPlugin.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/ui/Editor.h"

namespace
{
using gg::core::ParamSlot;
using gg::ui::ThemeChoice;
using gg::ui::View;

namespace fs = std::filesystem;

struct Size
{
    int w, h;
};

struct Product
{
    const char *id;
    const char *name; ///< the file name's product part
    bool fx;
};

constexpr std::array<Product, 2> kProducts{
    {{"org.sethbc.giga-grains-fx", "fx", true}, {"org.sethbc.giga-grains", "inst", false}}};
constexpr std::array<std::pair<View, const char *>, 4> kViews{
    {{View::Perform, "perform"}, {View::Edit, "edit"}, {View::Mod, "mod"}, {View::Fx, "fx"}}};
constexpr std::array<std::pair<ThemeChoice, const char *>, 2> kThemes{
    {{ThemeChoice::Dark, "dark"}, {ThemeChoice::Light, "light"}}};

constexpr uint32_t kBlock = 512;
constexpr double kRate = 48000.0;

int fail(const std::string &message)
{
    std::fprintf(stderr, "gg-snap: %s\n", message.c_str());
    return 1;
}

void setEnv(const char *name, const std::string &value)
{
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

/// "960x600,640x360" -> sizes; false if malformed.
bool parseSizes(const std::string &text, std::vector<Size> &out)
{
    std::size_t at = 0;
    while (at <= text.size())
    {
        const std::size_t comma = std::min(text.find(',', at), text.size());
        const std::string item = text.substr(at, comma - at);
        Size s{};
        char tail = 0;
        if (std::sscanf(item.c_str(), "%dx%d%c", &s.w, &s.h, &tail) != 2 || s.w < 1 || s.h < 1 ||
            s.w > 8192 || s.h > 8192)
        {
            return false;
        }
        out.push_back(s);
        at = comma + 1;
    }
    return !out.empty();
}

/// A mono 16-bit PCM WAV: `seconds` of a sine at `hz`.
bool writeSineWav(const fs::path &path, double seconds, double hz)
{
    const auto frames = static_cast<uint32_t>(seconds * kRate);
    std::vector<uint8_t> bytes;
    const auto put32 = [&bytes](uint32_t v) {
        for (int i = 0; i < 4; ++i)
        {
            bytes.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }
    };
    const auto put16 = [&bytes](uint16_t v) {
        bytes.push_back(static_cast<uint8_t>(v));
        bytes.push_back(static_cast<uint8_t>(v >> 8));
    };
    const auto tag = [&bytes](const char *t) { bytes.insert(bytes.end(), t, t + 4); };
    tag("RIFF");
    put32(36 + frames * 2);
    tag("WAVE");
    tag("fmt ");
    put32(16);
    put16(1); // PCM
    put16(1); // mono
    put32(static_cast<uint32_t>(kRate));
    put32(static_cast<uint32_t>(kRate) * 2);
    put16(2);
    put16(16);
    tag("data");
    put32(frames * 2);
    for (uint32_t i = 0; i < frames; ++i)
    {
        const double x = 0.5 * std::sin(2.0 * std::numbers::pi * hz * i / kRate);
        put16(static_cast<uint16_t>(static_cast<int16_t>(std::lround(x * 32767.0))));
    }
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

/// A clap_input_events over one note-on, or none.
struct Events
{
    clap_event_note note{};
    bool hasNote{false};
    clap_input_events list{this, &Events::size, &Events::get};

    static uint32_t size(const clap_input_events *l)
    {
        return static_cast<const Events *>(l->ctx)->hasNote ? 1u : 0u;
    }
    static const clap_event_header *get(const clap_input_events *l, uint32_t i)
    {
        const auto *self = static_cast<const Events *>(l->ctx);
        return self->hasNote && i == 0 ? &self->note.header : nullptr;
    }
};

const clap_host *stubHost()
{
    static const clap_host host{
        CLAP_VERSION_INIT,
        nullptr,
        "gg-snap",
        "Giga Grains",
        "",
        "0.0.0",
        [](const clap_host *, const char *) -> const void * { return nullptr; },
        [](const clap_host *) {},
        [](const clap_host *) {},
        [](const clap_host *) {},
    };
    return &host;
}

const clap_output_events *discardingOutput()
{
    static const clap_output_events out{
        nullptr, [](const clap_output_events *, const clap_event_header *) { return true; }};
    return &out;
}

/// One plugin instance, activated and processing.
class Instance
{
  public:
    explicit Instance(const Product &product) : fx_(product.fx)
    {
        const auto *factory = static_cast<const clap_plugin_factory *>(
            gg::plugin::getFactory(CLAP_PLUGIN_FACTORY_ID));
        if (factory == nullptr)
        {
            return;
        }
        plugin_ = factory->create_plugin(factory, stubHost(), product.id);
        if (plugin_ != nullptr && !plugin_->init(plugin_))
        {
            plugin_->destroy(plugin_);
            plugin_ = nullptr;
        }
        if (plugin_ != nullptr && plugin_->activate(plugin_, kRate, 1, kBlock) &&
            plugin_->start_processing(plugin_))
        {
            processing_ = true;
        }
    }
    Instance(const Instance &) = delete;
    Instance &operator=(const Instance &) = delete;
    ~Instance()
    {
        if (plugin_ == nullptr)
        {
            return;
        }
        if (processing_)
        {
            plugin_->stop_processing(plugin_);
            plugin_->deactivate(plugin_);
        }
        plugin_->destroy(plugin_);
    }

    bool ok() const { return plugin_ != nullptr && processing_; }
    gg::plugin::EditorPlugin *editorPlugin() const
    {
        return dynamic_cast<gg::plugin::EditorPlugin *>(
            gg::plugin::GigaGrainsPlugin::fromClap(plugin_));
    }
    void mainThread() const { plugin_->on_main_thread(plugin_); }

    /// Runs one block: the FX gets a stereo 330 Hz sine, the instrument a held note (sent at the
    /// first block).
    bool block()
    {
        std::vector<float> in(kBlock * 2, 0.0f), out(kBlock * 2, 0.0f);
        float *inPtr[2] = {in.data(), in.data() + kBlock};
        float *outPtr[2] = {out.data(), out.data() + kBlock};
        if (fx_)
        {
            for (uint32_t i = 0; i < kBlock; ++i)
            {
                const auto x =
                    static_cast<float>(0.4 * std::sin(2.0 * std::numbers::pi * 330.0 *
                                                      static_cast<double>(n_ + i) / kRate));
                in[i] = x;
                in[kBlock + i] = x;
            }
        }
        Events events;
        if (!fx_ && n_ == 0)
        {
            events.hasNote = true;
            events.note.header = {sizeof(clap_event_note), 0, 0, CLAP_EVENT_NOTE_ON, 0};
            events.note.note_id = 1;
            events.note.port_index = 0;
            events.note.channel = 0;
            events.note.key = 60;
            events.note.velocity = 0.8;
        }
        clap_audio_buffer inBuf{inPtr, nullptr, 2, 0, 0};
        clap_audio_buffer outBuf{outPtr, nullptr, 2, 0, 0};
        clap_process process{};
        process.steady_time = -1;
        process.frames_count = kBlock;
        process.audio_inputs = fx_ ? &inBuf : nullptr;
        process.audio_inputs_count = fx_ ? 1 : 0;
        process.audio_outputs = &outBuf;
        process.audio_outputs_count = 1;
        process.in_events = &events.list;
        process.out_events = discardingOutput();
        n_ += kBlock;
        return plugin_->process(plugin_, &process) != CLAP_PROCESS_ERROR;
    }

  private:
    const clap_plugin *plugin_{nullptr};
    bool fx_;
    bool processing_{false};
    int64_t n_{0};
};

void tick(juce::Component &editor, int n = 1)
{
    if (auto *timer = dynamic_cast<juce::Timer *>(&editor))
    {
        for (int i = 0; i < n; ++i)
        {
            timer->timerCallback();
        }
    }
}

bool writePng(const juce::Image &image, const fs::path &path)
{
    juce::File file(juce::String::fromUTF8(path.u8string().c_str()));
    file.deleteFile();
    juce::FileOutputStream stream(file);
    if (!stream.openedOk())
    {
        return false;
    }
    juce::PNGImageFormat png;
    return png.writeImageToStream(image, stream);
}

/// Everything for one product; returns an error message, or empty.
std::string snapProduct(const Product &product, const fs::path &outDir,
                        const std::vector<Size> &sizes, const fs::path &scratch)
{
    Instance instance(product);
    if (!instance.ok())
    {
        return std::string("can't create or activate ") + product.id;
    }
    auto *plugin = instance.editorPlugin();
    if (plugin == nullptr)
    {
        return "the plugin has no editor";
    }
    // The editor first: createEditor fills the context's callbacks afresh.
    std::unique_ptr<juce::Component> editor = plugin->createEditor();
    if (editor == nullptr)
    {
        return "createEditor failed";
    }
    const gg::ui::EditorContext &context = plugin->editorContext();

    // The instrument's sample: a 2 s sine, loaded through the loader strip's own path.
    if (!product.fx)
    {
        const fs::path wav = scratch / "sine-2s.wav";
        if (!writeSineWav(wav, 2.0, 220.0))
        {
            return "can't write " + wav.string();
        }
        context.loadSample(wav.string());
        bool loaded = false;
        for (int i = 0; i < 500 && !loaded; ++i)
        {
            instance.mainThread();
            loaded = context.sampleStatus().state == gg::ui::SampleStatusView::State::Loaded;
            if (!loaded)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        if (!loaded)
        {
            return "the generated sample didn't load: " + context.sampleStatus().message;
        }
    }

    // Layers B and C on, each change a gesture applied by a block.
    for (const uint8_t layer : {uint8_t{1}, uint8_t{2}})
    {
        const ParamSlot slot = gg::core::layerOnSlot(layer);
        context.beginGesture(slot);
        context.setValue(slot, 1.0);
        context.endGesture(slot);
        if (!instance.block())
        {
            return "process failed";
        }
    }
    // One route added in the first empty slot: LFO 1 to layer A's Pitch.
    gg::core::ModRoutes routes = context.routes();
    const auto empty = std::ranges::find_if(
        routes, [](const gg::core::ModRoute &r) { return r.source == gg::core::ModSource::None; });
    if (empty == routes.end())
    {
        return "no empty route slot";
    }
    empty->source = gg::core::ModSource::Lfo1;
    empty->target = ParamSlot::Pitch;
    empty->targetLayer = 0;
    empty->depth = 0.4f;
    empty->curve = gg::core::ModCurve::Bipolar;
    context.setRoutes(routes);

    // Some audio with the editor open: published grains and snapshots for the canvas.
    for (int i = 0; i < 200; ++i)
    {
        if (!instance.block())
        {
            return "process failed";
        }
        if (i % 4 == 0)
        {
            tick(*editor);
        }
    }

    for (const auto &[theme, themeName] : kThemes)
    {
        if (!gg::ui::setTheme(*editor, theme))
        {
            return "setTheme failed";
        }
        for (const auto &[view, viewName] : kViews)
        {
            if (!gg::ui::selectView(*editor, view))
            {
                return std::string("selectView failed: ") + viewName;
            }
            for (const Size &size : sizes)
            {
                editor->setSize(size.w, size.h);
                instance.block();
                tick(*editor, 2);
                const juce::Image image =
                    editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                if (!image.isValid() || image.getWidth() != size.w || image.getHeight() != size.h)
                {
                    return std::string("no snapshot of ") + viewName;
                }
                const fs::path file =
                    outDir / (std::string(product.name) + "-" + viewName + "-" + themeName + "-" +
                              std::to_string(size.w) + "x" + std::to_string(size.h) + ".png");
                if (!writePng(image, file))
                {
                    return "can't write " + file.string();
                }
            }
        }
    }
    editor.reset();
    return {};
}
} // namespace

int main(int argc, char **argv)
{
    fs::path outDir;
    std::vector<Size> sizes;
    std::string sizeText = "960x600,640x360";
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--sizes" && i + 1 < argc)
        {
            sizeText = argv[++i];
        }
        else if (!arg.empty() && arg[0] != '-' && outDir.empty())
        {
            outDir = arg;
        }
        else
        {
            std::fprintf(stderr, "usage: gg-snap <out-dir> [--sizes 960x600,640x360]\n");
            return 2;
        }
    }
    if (outDir.empty() || !parseSizes(sizeText, sizes))
    {
        std::fprintf(stderr, "usage: gg-snap <out-dir> [--sizes 960x600,640x360]\n");
        return 2;
    }

    std::error_code ec;
    // JUCE's File wants an absolute path.
    outDir = fs::absolute(outDir, ec);
    fs::create_directories(outDir, ec);
    if (ec)
    {
        return fail("can't create " + outDir.string() + ": " + ec.message());
    }
    // A scratch folder for the sample and the settings; the settings seam is read when an
    // instance is made, and the canvas's stress seam when a canvas is.
    const fs::path scratch =
        fs::temp_directory_path() /
        ("gg-snap-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(scratch, ec);
    if (ec)
    {
        return fail("can't create " + scratch.string() + ": " + ec.message());
    }
    setEnv("GG_SETTINGS_FILE", (scratch / "settings.xml").string());
    setEnv("GG_STRESS_GRAINS", "2048");

    int status = 0;
    {
        const juce::ScopedJuceInitialiser_GUI juceGui;
        for (const Product &product : kProducts)
        {
            const std::string error = snapProduct(product, outDir, sizes, scratch);
            if (!error.empty())
            {
                status = fail(error);
                break;
            }
        }
    }
    fs::remove_all(scratch, ec);
    return status;
}
