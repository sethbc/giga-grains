// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_GIGAGRAINSPLUGIN_H
#define GG_PLUGIN_GIGAGRAINSPLUGIN_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <clap/clap.h>
#include <sst/cpputils/rtsan_support.h>

#include "HelpersConfig.h"

namespace gg
{
class Engine;
}

namespace gg::plugin
{
/// The two products built from the one engine (INTENT §2).
enum class Product
{
    Instrument, ///< Giga Grains: an instrument with a note input and a stereo output.
    Fx,         ///< Giga Grains FX: a stereo effect that also accepts notes.
};

/// The CLAP descriptor of each product; static storage, never null.
const clap_plugin_descriptor *descriptorFor(Product product) noexcept;

/// The CLAP plugin shell around gg::Engine, for both products: the JUCE-free plugin core.
///
/// M0 shell: the engine has no source yet, so the instrument outputs silence and the FX passes
/// its input through unchanged (dry plus the engine's silent output). Note-ons are counted but
/// not yet played. This class has no GUI. The editor is added by a subclass, EditorPlugin
/// (editor/EditorPlugin.h), built only with GG_BUILD_EDITOR; makePlugin() picks the class.
class GigaGrainsPlugin : public plugHelper_t
{
  public:
    GigaGrainsPlugin(const clap_host *host, Product product);
    ~GigaGrainsPlugin() override;

    /// The instance behind a clap_plugin this class created.
    static GigaGrainsPlugin *fromClap(const clap_plugin *plugin) noexcept
    {
        return static_cast<GigaGrainsPlugin *>(static_cast<plugHelper_t *>(plugin->plugin_data));
    }

    /// CLAP NOTE_ON events plus MIDI note-ons (0x9n with velocity > 0) seen by process() or by
    /// a params flush.
    std::atomic<uint32_t> noteOnCount{0};

    Product product() const noexcept { return product_; }

    /// The engine built by the last activate(); nullptr while the plugin is inactive.
    const gg::Engine *engine() const noexcept { return engine_.get(); }

  protected:
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override;
    void deactivate() noexcept override;
    /// On the audio thread, never concurrent with process(): real-time safe.
    void reset() noexcept SST_CPPUTILS_NONBLOCKING override;
    clap_process_status process(const clap_process *process) noexcept override;

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool isInput) const noexcept override;
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info *info) const noexcept override;

    bool implementsNotePorts() const noexcept override { return true; }
    uint32_t notePortsCount(bool isInput) const noexcept override;
    bool notePortsInfo(uint32_t index, bool isInput,
                       clap_note_port_info *info) const noexcept override;

    // ---- Parameters: none yet, but the extension is there ----
    // clap-wrapper's VST3 IMidiMapping creates its MIDI-controller parameters only for a plugin
    // with CLAP's params extension; without one, a wrapped VST3 maps every controller to an
    // unknown ParamID. Parameter IDs are frozen at the first release, so none are added here.

    bool implementsParams() const noexcept override { return true; }
    uint32_t paramsCount() const noexcept override { return 0; }
    bool paramsInfo(uint32_t /*paramIndex*/, clap_param_info * /*info*/) const noexcept override
    {
        return false;
    }
    bool paramsValue(clap_id /*paramId*/, double * /*value*/) noexcept override { return false; }
    bool paramsValueToText(clap_id /*paramId*/, double /*value*/, char * /*display*/,
                           uint32_t /*size*/) noexcept override
    {
        return false;
    }
    bool paramsTextToValue(clap_id /*paramId*/, const char * /*display*/,
                           double * /*value*/) noexcept override
    {
        return false;
    }
    /// On the audio thread while active (and on the main thread while not): real-time safe.
    void paramsFlush(const clap_input_events *in,
                     const clap_output_events *out) noexcept SST_CPPUTILS_NONBLOCKING override;

  private:
    clap_process_status
    processNonblocking(const clap_process *process) noexcept SST_CPPUTILS_NONBLOCKING;
    void countNoteOns(const clap_input_events *events) noexcept SST_CPPUTILS_NONBLOCKING;
    void addEngineOutput(float *const *out, uint32_t numChannels,
                         uint32_t numFrames) noexcept SST_CPPUTILS_NONBLOCKING;

    const Product product_;
    std::unique_ptr<gg::Engine> engine_;

    // The FX renders the engine here, then adds it to the dry signal: Engine::process
    // overwrites its output. Two channels of scratchFrames_ each, allocated by activate().
    std::vector<float> scratch_;
    uint32_t scratchFrames_{0};
};

/// Creates a plugin instance of `product` for the factory: a GigaGrainsPlugin in a headless
/// build (headless/MakePlugin.cpp), an EditorPlugin when the editor is built
/// (editor/MakePlugin.cpp). Exactly one of the two is linked. clap-helpers owns the instance:
/// clap_plugin.destroy deletes it. Throws whatever the plugin's constructor throws.
const clap_plugin *makePlugin(const clap_host *host, Product product);
} // namespace gg::plugin

#endif // GG_PLUGIN_GIGAGRAINSPLUGIN_H
