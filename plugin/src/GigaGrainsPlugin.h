// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_GIGAGRAINSPLUGIN_H
#define GG_PLUGIN_GIGAGRAINSPLUGIN_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include <clap/clap.h>
#include <sst/cpputils/rtsan_support.h>

#include "HelpersConfig.h"
#include "LatestValue.h"
#include "SampleLoader.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"

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

/// The CLAP adapter around gg::core::Processor, for both products: the JUCE-free plugin core
/// (spec §5). It maps CLAP's parameters, state and remote controls onto the Processor; the
/// sound is the Processor's. The FX's input feeds the live ring and the dry side of the Mix; the
/// instrument plays the sample its SampleLoader gives it from notes, and is silent without one.
/// Both products pass their CLAP and MIDI notes to the Processor (toNoteEvent), on the control
/// grid. This class has no GUI. The editor is added by a subclass, EditorPlugin
/// (editor/EditorPlugin.h), built only with GG_BUILD_EDITOR; makePlugin() picks the class.
///
/// **Threads** (INTENT §15). The main thread owns the Processor's lifetime (activate,
/// deactivate), the seed, the state and the sample loader; the audio thread owns the Processor
/// in between, and with it events_ and notes_. Parameter values cross over in two ways:
/// - values_, an atomic per-parameter store of the base values, which the main thread reads
///   (params.get_value, state save). The audio thread writes it as it hands values to the
///   Processor; the main thread writes it while inactive, and on a state load.
/// - loadedState_ (the FX) or the Processor's SourceExchange (the instrument, with its sample),
///   which take a state loaded while active to the audio thread, where it applies at the start
///   of the next process() or params.flush.
///
/// **The sample** (the instrument; spec §6.4–§6.7). The SampleLoader keeps the manifest entry
/// and the source on the main thread, and loads on its own worker thread. Sources reach the
/// audio thread through the Processor's SourceExchange (Processor::sources()): the main thread
/// offers, process() adopts at the start of a call and hands back what no grain reads, asking
/// for on_main_thread (requestCollect), where the main thread frees it. A session restore is
/// synchronous (D8): stateLoad finds and decodes the sample before it returns; while active, it
/// offers the source and the loaded values as one hand-off (SourceOffer), so the process() that
/// takes them applies both (the source may wait up to Size more if two are already in play);
/// while inactive, activate() converts it at the new rate before it returns, as it does the
/// sample it has at any new rate (ruling R18). The FX takes a state's values through
/// loadedState_. The per-machine
/// settings file is $GG_SETTINGS_FILE when that is set and not empty (read once, at
/// construction: the tests' way to keep away from the user's file), and
/// gg::media::defaultSettingsPath() otherwise.
///
/// **The tail** (sub-project 4's spec §6.7). tail.get computes it from values_ and the active
/// rate: the grains' part (the instrument's Size; the FX's Position + Spray + Size, in seconds as
/// toCloudParams maps them), plus Pre-delay + kTailDecayMultiple × Decay when Reverb Amount is
/// above 0; kTailInfinite when Amount is above 0 at Decay ∞, or in the FX while Freeze is engaged
/// or a note is held (it keeps sounding with no input). The last two live on the audio thread,
/// which publishes them as sustaining_ (publishTail) at the end of each process(), active
/// params.flush and reset(). There too, when that flips or a value entering the tail has changed
/// (on either thread), it calls the host's tail.changed if the tail differs from the one it last
/// reported. CLAP marks clap_host_tail.changed [audio-thread], and clap-validator fails a plugin
/// that calls it from the main thread, so a state load or a change while inactive is reported
/// by the next process() (or active params.flush).
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
    /// The product's gg-core mode: its parameter metadata (paramMeta) and its Processor's.
    gg::core::Mode mode() const noexcept
    {
        return product_ == Product::Fx ? gg::core::Mode::Fx : gg::core::Mode::Instrument;
    }

    /// The processor built by the last activate(); nullptr while the plugin is inactive. For
    /// tests: its state belongs to the audio thread while the plugin is active.
    const gg::core::Processor *processor() const noexcept { return processor_.get(); }
    gg::core::Processor *processor() noexcept { return processor_.get(); }

    /// The instrument's sample loader, for the editor and tests; nullptr in the FX, which loads
    /// no samples. Main thread.
    SampleLoader *sampleLoader() noexcept { return loader_.get(); }
    /// The processor's engine; nullptr while the plugin is inactive. As processor().
    const gg::Engine *engine() const noexcept
    {
        return processor_ != nullptr ? &processor_->engine() : nullptr;
    }

    /// The most frames the engine renders at once: the host's maxFrameCount, capped at
    /// kMaxEngineBlock, as of the last activate(). The live ring's memory grows with it (ruling
    /// M1-o); the output doesn't.
    uint32_t engineBlockSize() const noexcept { return engineBlockSize_; }

    /// The cap on engineBlockSize(). CLAP allows a maxFrameCount up to INT32_MAX.
    static constexpr uint32_t kMaxEngineBlock = 4096;
    /// The most parameter events one process() call hands to the Processor in one go (its
    /// event span). Events beyond it apply at the block's end (applyBetweenBlocks).
    static constexpr std::size_t kMaxParamEvents = 4096;
    /// The most note events one process() call hands to the Processor in one go (its note span).
    /// Notes beyond it apply at the block's end, at the next grid boundary (applyBetweenBlocks):
    /// they move to that boundary from their own, so past this cap the output is no longer block-
    /// size invariant.
    static constexpr std::size_t kMaxNoteEvents = 1024;
    /// The CLAP note IDs remembered for note-offs addressed by ID alone (ruling R15). When the
    /// table is full, a new ID replaces the oldest.
    static constexpr std::size_t kMaxNoteIds = 256;
    /// CLAP's infinite tail: tail.get values from INT32_MAX up (clap/ext/tail.h).
    static constexpr uint32_t kTailInfinite = 0x7FFFFFFF;
    /// The reverb's part of the tail is Pre-delay plus this many times Decay (spec §6.7).
    static constexpr double kTailDecayMultiple = 2.0;

  protected:
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override;
    void deactivate() noexcept override;
    /// Applies the sample loader's finished work and frees the sources the audio thread handed
    /// back (SourceExchange::collect).
    void onMainThread() noexcept override;
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

    // ---- Parameters (spec §4): gg::core's ten, identical in both products ----
    // The extension also lets clap-wrapper's VST3 IMidiMapping create its MIDI-controller
    // parameters, from 0xB00000 up (ruling T8-d).

    bool implementsParams() const noexcept override { return true; }
    uint32_t paramsCount() const noexcept override
    {
        return static_cast<uint32_t>(gg::core::kParamCount);
    }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override;
    bool paramsValue(clap_id paramId, double *value) noexcept override;
    bool paramsValueToText(clap_id paramId, double value, char *display,
                           uint32_t size) noexcept override;
    bool paramsTextToValue(clap_id paramId, const char *display, double *value) noexcept override;
    /// On the audio thread while active (and on the main thread while not): real-time safe.
    void paramsFlush(const clap_input_events *in,
                     const clap_output_events *out) noexcept SST_CPPUTILS_NONBLOCKING override;

    // ---- State: the session container (gg/core/SessionState.h): the values and the seed, and
    // the instrument's sample entry ----

    bool implementsState() const noexcept override { return true; }
    bool stateSave(const clap_ostream *stream) noexcept override;
    bool stateLoad(const clap_istream *stream) noexcept override;

    // ---- Remote controls: two pages, "Cloud" (spec §4) and "Space" (sub-project 4's spec
    // §6.6) ----

    bool implementRemoteControls() const noexcept override { return true; }
    uint32_t remoteControlsPageCount() noexcept override { return 2; }
    bool remoteControlsPageGet(uint32_t pageIndex,
                               clap_remote_controls_page *page) noexcept override;

    // ---- Tail (see the class comment) ----

    bool implementsTail() const noexcept override { return true; }
    /// In frames at the last activation's rate (0 before the first). On the main or the audio
    /// thread: real-time safe.
    uint32_t tailGet() const noexcept SST_CPPUTILS_NONBLOCKING override;

  private:
    clap_process_status
    processNonblocking(const clap_process *process) noexcept SST_CPPUTILS_NONBLOCKING;
    void countNoteOns(const clap_input_events *events) noexcept SST_CPPUTILS_NONBLOCKING;
    /// The parameter event `header` as a ParamEvent: false for any other event, an unknown
    /// parameter or a NaN value.
    static bool toParamEvent(const clap_event_header *header,
                             gg::core::ParamEvent &event) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Whether `header` is an event toNoteEvent may convert: a CLAP note-on, note-off or note
    /// choke, or a MIDI event. Stateless.
    static bool isNoteEvent(const clap_event_header *header) noexcept SST_CPPUTILS_NONBLOCKING;
    /// The note event `header` as a NoteEvent (spec §6.1): CLAP's note-on, note-off and note
    /// choke, MIDI's note-on, note-off and CC 123 (all notes off on its channel, ruling R14).
    /// False for any other event. Velocity and the port are ignored; out-of-range channels and
    /// keys pass through, for the Processor to drop.
    ///
    /// CLAP note IDs (ruling R15): a note-on with an ID ≥ 0 and an in-range channel and key is
    /// remembered in noteIds_. A note-off or choke with an ID ≥ 0 forgets it, and if its channel
    /// and key are both −1 releases the ID's (channel, key) alone; an unknown ID keeps the
    /// wildcards, which release everything. Every off (CLAP or MIDI, CC 123 included) also
    /// forgets the IDs of the notes it releases. So call it once per event, in the host's order.
    /// Audio thread.
    bool toNoteEvent(const clap_event_header *header,
                     gg::core::NoteEvent &event) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Forgets the note IDs remembered for (`channel`, `key`), −1 matching any. Audio thread.
    void forgetNoteIds(int16_t channel, int16_t key) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Applies the parameter and note events of `in` from index `first` between two process()
    /// calls: values at once with setBase, modulation and notes at the next control-grid
    /// boundary. Needs the processor; audio thread.
    void applyBetweenBlocks(const clap_input_events *in,
                            uint32_t first) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Applies a state loaded since the last call, if there is one. Audio thread.
    void applyLoadedState() noexcept SST_CPPUTILS_NONBLOCKING;
    /// Stores a base value in values_, clamped to the parameter's range, as the Processor
    /// clamps it.
    void storeValue(gg::core::ParamSlot slot, float value) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Takes the Processor's offers (Processor::takeOffers) and, when one brought values, puts
    /// them in values_. Audio thread, or the main thread with the audio thread stopped.
    void takeOffers() noexcept SST_CPPUTILS_NONBLOCKING;
    /// Stores the Processor's base values in values_.
    void storeBases() noexcept SST_CPPUTILS_NONBLOCKING;
    /// Audio thread. When a source the audio thread handed back awaits the main thread's
    /// collect, asks the host for on_main_thread (clap_host.request_callback, thread-safe), once
    /// until that call comes, so every retired source is freed soon after it retires. Its
    /// real-time safety is the host's: see the call.
    void requestCollect() noexcept SST_CPPUTILS_NONBLOCKING;
    /// Audio thread, with the processor. Publishes whether the FX keeps sounding with no input
    /// (sustaining_) and, when that flipped or a value entering the tail changed since the last
    /// call, calls the host's tail.changed if tailGet() differs from the tail last reported. Its
    /// real-time safety is the host's, as requestCollect's is.
    void publishTail() noexcept SST_CPPUTILS_NONBLOCKING;

    const Product product_;

    // The parameters' ranges, from their metadata. Constant after construction.
    gg::core::ParamValues minValues_{};
    gg::core::ParamValues maxValues_{};

    // The base values the main thread reads (see the class comment).
    std::array<std::atomic<float>, gg::core::kParamCount> values_{};
    // A state loaded while active, on its way to the audio thread. Main thread → audio thread.
    LatestValue<gg::core::ParamValues> loadedState_;
    // The engine seed: saved and loaded with the state, and given to the processor at
    // activate(). Main thread.
    uint64_t seed_{0};

    // Built by activate() and destroyed by deactivate(), on the main thread; between the two,
    // the audio thread's.
    std::unique_ptr<gg::core::Processor> processor_;
    uint32_t engineBlockSize_{0}; // main thread
    // The instrument's sample loader; null in the FX. Main thread. After processor_, so it is
    // destroyed first: its callbacks use processor_.
    std::unique_ptr<SampleLoader> loader_;
    // Set by the audio thread when it asks for on_main_thread to collect retired sources,
    // cleared by on_main_thread (requestCollect).
    std::atomic<bool> collectRequested_{false};
    // The tail (see the class comment). The rate of the last activate(), written there and read
    // by tailGet on either thread.
    std::atomic<double> sampleRate_{0.0};
    // The FX keeps sounding with no input: Freeze engaged or a note held. Written by the audio
    // thread (publishTail), cleared by activate() and deactivate().
    std::atomic<bool> sustaining_{false};
    // A value entering the tail changed (storeValue, on either thread), not yet published
    // (publishTail).
    std::atomic<bool> tailValuesChanged_{false};
    // The tail last reported to the host, or read at activate(). Audio thread; activate() sets
    // it.
    uint32_t reportedTail_{0};
    // The parameter and note events of one process() call. Audio thread.
    std::array<gg::core::ParamEvent, kMaxParamEvents> events_{};
    std::array<gg::core::NoteEvent, kMaxNoteEvents> notes_{};
    // The CLAP note IDs of the notes held (toNoteEvent). A free slot has id −1; age orders the
    // slots, the oldest replaced first. Audio thread; activate() and reset() clear it.
    struct NoteIdSlot
    {
        int32_t id{-1};
        int16_t channel{0};
        int16_t key{0};
        uint64_t age{0};
    };
    std::array<NoteIdSlot, kMaxNoteIds> noteIds_{};
    uint64_t noteIdAge_{0};
};

/// Creates a plugin instance of `product` for the factory: a GigaGrainsPlugin in a headless
/// build (headless/MakePlugin.cpp), an EditorPlugin when the editor is built
/// (editor/MakePlugin.cpp). Exactly one of the two is linked. clap-helpers owns the instance:
/// clap_plugin.destroy deletes it. Throws whatever the plugin's constructor throws.
/// Editor builds: gg-plugin-core leaves this unresolved; link the editor objects (IMPL_TARGET).
const clap_plugin *makePlugin(const clap_host *host, Product product);
} // namespace gg::plugin

#endif // GG_PLUGIN_GIGAGRAINSPLUGIN_H
