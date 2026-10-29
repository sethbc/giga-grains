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
#include "SampleLoader.h"
#include "gg/core/GrainQueue.h"
#include "gg/core/LatestValue.h"
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

/// One change the editor makes to a parameter, on its way from the main thread to the audio
/// thread (GigaGrainsPlugin's uiQueue_): a gesture's begin or end, or a base value. Trivially
/// copyable, for the SpscQueue.
struct UiParamChange
{
    enum Kind : uint8_t
    {
        kBegin, ///< CLAP_EVENT_PARAM_GESTURE_BEGIN
        kValue, ///< a base value, then CLAP_EVENT_PARAM_VALUE
        kEnd,   ///< CLAP_EVENT_PARAM_GESTURE_END
    };

    clap_id id;
    double value; ///< kValue's: in range, and a float's value (uiSetValue)
    uint8_t kind; ///< a Kind
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
/// - loadedState_ (the FX's values) or the Processor's SourceExchange (the instrument's, with
///   its sample), which take a state loaded while active to the audio thread, where it applies
///   at the start of the next process() or params.flush. loadedState_ also carries the state's
///   engine seed, in both products, so a seed loaded while active needs no restart (carry-over
///   row 115). Once the audio thread has applied a state's values, and stored them in values_,
///   it sets valuesApplied_ and asks for on_main_thread, which asks the host to rescan the
///   values (carry-over row 123, ruling M1-x): the host's first rescan, at the load, may read
///   a value a block in flight then overwrote.
/// - uiQueue_, the editor's changes (uiBeginGesture, uiSetValue, uiEndGesture: spec §6.3), main
///   thread → audio thread. process() and an active params.flush take them before the host's
///   events and apply the values as the host's are applied, then send each change on to the
///   host as an output event; an inactive flush, on the main thread, stores the values in
///   values_ for activate().
///
/// **The seed** (sub-project 5's spec §7). A new instance draws its own engine seed, on the main
/// thread at construction, from std::random_device; a state brings its own.
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
/// **The tail** (sub-project 4's spec §6.7; ruling R-T7a) is a constant per product: the FX's is
/// infinite, the instrument's kInstrumentTailSeconds at the active rate. Nothing calls
/// clap_host_tail.changed. tailGet() says why.
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
    /// The editor's changes that can wait for the audio thread at once (uiQueue_). A change
    /// made while it is full is dropped.
    static constexpr uint32_t kUiQueueCapacity = 1024;
    /// The most parameter events one process() call hands to the Processor in one go (its
    /// event span): the host's, after every value the editor queued (at most kUiQueueCapacity),
    /// so the editor's never overflow. The host's events beyond it apply at the block's end
    /// (applyBetweenBlocks).
    static constexpr std::size_t kMaxParamEvents = 4096 + kUiQueueCapacity;
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
    /// The instrument's tail, in seconds (ruling R-T7a): the longest it can ring after its last
    /// note, at every parameter's maximum: Size's 2 s release, Reverb Pre-delay's 0.5 s and
    /// twice Reverb Decay's longest finite 60 s (spec §6.7's multiple).
    static constexpr double kInstrumentTailSeconds = 2.0 + 0.5 + 2.0 * 60.0;
    /// The rate tailGet() assumes before the first activate().
    static constexpr double kDefaultTailRate = 48000.0;

    // ---- The editor's parameter changes (spec §6.3): main thread ----
    // Each queues a change for the audio thread, which applies it at the start of the next
    // process() call or params.flush, before the host's events there, and sends it on to the
    // host as an output event, so the host records the editor's gesture as automation. Each
    // asks the host for a flush (clap_host_params.request_flush), when it offers clap.params and
    // none is outstanding. A change made while kUiQueueCapacity wait is dropped.

    /// The editor's gesture on `slot` begins: CLAP_EVENT_PARAM_GESTURE_BEGIN.
    void uiBeginGesture(gg::core::ParamSlot slot) noexcept;
    /// Sets `slot`'s base value to `value`, clamped to its range and made a float: applied as a
    /// host's CLAP_EVENT_PARAM_VALUE would be, and sent to the host as one. A NaN is ignored.
    void uiSetValue(gg::core::ParamSlot slot, double value) noexcept;
    /// The editor's gesture on `slot` ends: CLAP_EVENT_PARAM_GESTURE_END.
    void uiEndGesture(gg::core::ParamSlot slot) noexcept;
    /// `slot`'s base value, as params.get_value reads it (values_): a change of the editor's
    /// shows here once the audio thread has taken it.
    float uiBaseValue(gg::core::ParamSlot slot) const noexcept;
    /// The output events of the editor's changes that the host's list refused (try_push false):
    /// each counted, never retried. Any thread.
    uint64_t uiEventsDropped() const noexcept
    {
        return uiEventsDropped_.load(std::memory_order_relaxed);
    }

    /// Whether the editor is open: the Processor publishes its grains and snapshots for it
    /// (Processor::setPublishing), from now while active, and from the next activate()
    /// otherwise. Main thread.
    void setPublishing(bool on) noexcept;
    bool publishing() const noexcept { return publishing_; }

    /// The rate of the last activate(), kDefaultTailRate before the first. Main thread.
    double activeSampleRate() const noexcept { return sampleRate_.load(std::memory_order_relaxed); }

    /// The instrument's tail at `sampleRate`: kInstrumentTailSeconds in frames, rounded up,
    /// and kTailInfinite − 1 where that would reach kTailInfinite (or for a NaN or negative
    /// rate), so it is never read as infinite.
    static uint32_t instrumentTailFrames(double sampleRate) noexcept;

    /// Whether `header` is an event toNoteEvent converts, which counts toward kMaxNoteEvents: a
    /// CLAP note-on, note-off or note choke, or a MIDI note-on, note-off or CC 123. Stateless.
    static bool isNoteEvent(const clap_event_header *header) noexcept SST_CPPUTILS_NONBLOCKING;

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

    // ---- Remote controls: three pages, "Perform" (sub-project 5's spec §3.3), "Cloud" (spec
    // §4) and "Space" (sub-project 4's spec §6.6) ----

    bool implementRemoteControls() const noexcept override { return true; }
    uint32_t remoteControlsPageCount() noexcept override;
    bool remoteControlsPageGet(uint32_t pageIndex,
                               clap_remote_controls_page *page) noexcept override;

    // ---- Tail (see the class comment) ----

    bool implementsTail() const noexcept override { return true; }
    /// In frames: kTailInfinite in the FX; in the instrument kInstrumentTailSeconds at the last
    /// activation's rate (kDefaultTailRate before the first), rounded up and below
    /// kTailInfinite. On the main or the audio thread: real-time safe.
    uint32_t tailGet() const noexcept SST_CPPUTILS_NONBLOCKING override;

  private:
    clap_process_status
    processNonblocking(const clap_process *process) noexcept SST_CPPUTILS_NONBLOCKING;
    void countNoteOns(const clap_input_events *events) noexcept SST_CPPUTILS_NONBLOCKING;
    /// The parameter event `header` as a ParamEvent: false for any other event, an unknown
    /// parameter or a NaN value.
    static bool toParamEvent(const clap_event_header *header,
                             gg::core::ParamEvent &event) noexcept SST_CPPUTILS_NONBLOCKING;
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
    /// Applies a state loaded since the last call, if there is one: its seed, and its values
    /// when it carries them (the FX). Audio thread.
    void applyLoadedState() noexcept SST_CPPUTILS_NONBLOCKING;
    /// Audio thread, after a state's values went into values_: sets valuesApplied_ and asks
    /// the host for on_main_thread (clap_host.request_callback, thread-safe), once until that
    /// call comes, where the host is asked to rescan the values. Its real-time safety is the
    /// host's, as requestCollect's is.
    void requestValueRescan() noexcept SST_CPPUTILS_NONBLOCKING;
    /// Stores a base value in values_, clamped to the parameter's range, as the Processor
    /// clamps it.
    void storeValue(gg::core::ParamSlot slot, float value) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Takes the Processor's offers (Processor::takeOffers) and, when one brought values, puts
    /// them in values_. Audio thread, or the main thread with the audio thread stopped.
    void takeOffers() noexcept SST_CPPUTILS_NONBLOCKING;
    /// Takes the editor's changes queued so far (at most kUiQueueCapacity, so a main thread
    /// still pushing can't keep it here) and sends each on to `out` (try_push; a refusal is
    /// counted in uiEventsDropped_). Values: with `values`, appended to it from `count` on as
    /// ParamEvents at offset 0, for the caller to hand to the Processor; without, applied with
    /// setBase while active and stored in values_ either way. Returns the new count. The audio
    /// thread, or the main thread while inactive (the queue's consumer).
    std::size_t takeUiChanges(const clap_output_events *out, gg::core::ParamEvent *values,
                              std::size_t count) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Pushes `change` to uiQueue_ and asks the host for a flush (see uiBeginGesture). Main
    /// thread.
    void queueUiChange(const UiParamChange &change) noexcept;
    /// Stores the Processor's base values in values_.
    void storeBases() noexcept SST_CPPUTILS_NONBLOCKING;
    /// Audio thread. When a source the audio thread handed back awaits the main thread's
    /// collect, asks the host for on_main_thread (clap_host.request_callback, thread-safe), once
    /// until that call comes, so every retired source is freed soon after it retires. Its
    /// real-time safety is the host's: see the call.
    void requestCollect() noexcept SST_CPPUTILS_NONBLOCKING;

    const Product product_;

    // The parameters' ranges, from their metadata. Constant after construction.
    gg::core::ParamValues minValues_{};
    gg::core::ParamValues maxValues_{};

    // The base values the main thread reads (see the class comment).
    std::array<std::atomic<float>, gg::core::kParamCount> values_{};
    // A state loaded while active, on its way to the audio thread: its seed, and in the FX its
    // values (the instrument's go with its sample, through the SourceExchange).
    struct LoadedState
    {
        gg::core::ParamValues values;
        uint64_t seed;
        bool hasValues;
    };
    // Main thread → audio thread.
    gg::core::LatestValue<LoadedState> loadedState_;
    // The engine seed: drawn at construction, saved and loaded with the state, given to the
    // processor at activate() and, loaded while active, through loadedState_. Main thread.
    uint64_t seed_;
    // Set by the audio thread when it has stored a state's values in values_, cleared by
    // on_main_thread, which then asks the host to rescan them (requestValueRescan).
    std::atomic<bool> valuesApplied_{false};

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
    // The rate of the last activate(), for the instrument's tail: written there, read by tailGet
    // on either thread.
    std::atomic<double> sampleRate_{kDefaultTailRate};
    static_assert(std::atomic<double>::is_always_lock_free, "tailGet reads it on the audio thread");
    // The editor's changes, main thread → audio thread (or the main thread's own flush while
    // inactive): see uiBeginGesture.
    gg::core::SpscQueue<UiParamChange, kUiQueueCapacity> uiQueue_;
    // Set by the main thread when it asks the host for a flush, cleared by the queue's consumer
    // before it drains: one request outstanding at a time (queueUiChange).
    std::atomic<bool> uiFlushRequested_{false};
    std::atomic<uint64_t> uiEventsDropped_{0}; // the consumer's
    bool publishing_{false};                   // main thread: the editor is open
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
