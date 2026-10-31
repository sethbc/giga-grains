// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_GIGAGRAINSPLUGIN_H
#define GG_PLUGIN_GIGAGRAINSPLUGIN_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <clap/clap.h>
#include <sst/cpputils/rtsan_support.h>

#include "HelpersConfig.h"
#include "PresetCatalog.h"
#include "SampleLoader.h"
#include "gg/core/GrainQueue.h"
#include "gg/core/LatestValue.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/Mutate.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"
#include "gg/core/SessionState.h"
#include "gg/engine/TaskRunner.h"
#include "gg/source/SourceMode.h"

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
    /// The load epoch it was queued in (GigaGrainsPlugin's uiEpoch_): a value queued before the
    /// last state load the consumer has applied is stale, and dropped (fix wave I1).
    uint32_t epoch;
};

/// The CLAP descriptor of each product; static storage, never null.
const clap_plugin_descriptor *descriptorFor(Product product) noexcept;

/// The CLAP adapter around gg::core::Processor, for both products: the JUCE-free plugin core
/// (spec §5). It maps CLAP's parameters, state and remote controls onto the Processor; the
/// sound is the Processor's. Both products have an audio input, which feeds every layer's live
/// ring (M2 sub-project 1's spec §6.4): the FX's main input, which also feeds the dry side of the
/// Mix, and the instrument's sidechain (D2), which only a Live layer reads. The instrument's
/// Sample layers play the sample its SampleLoader gives them, from notes, and are silent without
/// one. Both products pass their CLAP and MIDI notes to the Processor (toNoteEvent), their MIDI
/// controls (toControlEvent: Mod Wheel, Aftertouch, Pitch Bend) and the host's transport, all on
/// the control grid. This class has no GUI. The editor is added by a subclass, EditorPlugin
/// (editor/EditorPlugin.h), built only with GG_BUILD_EDITOR; makePlugin() picks the class.
///
/// **Threads** (INTENT §15). The main thread owns the Processor's lifetime (activate,
/// deactivate), the seed, the routes, the state and the sample loader; the audio thread owns the
/// Processor in between, and with it events_, notes_ and controls_. Parameter values cross over
/// in two ways:
/// - values_, an atomic per-parameter store of the base values, which the main thread reads
///   (params.get_value, state save). The audio thread writes it as it hands values to the
///   Processor; the main thread writes it while inactive, and on a state load.
/// - loadedState_ (the FX's values) or the Processor's SourceExchange (the instrument's, with
///   its sample), which take a state loaded while active to the audio thread, where it applies
///   at the start of the next process() or params.flush. loadedState_ also carries the state's
///   engine seed and its mod-matrix routes, in both products, so a seed loaded while active needs
///   no restart (carry-over row 115). Once the audio thread has applied a state's values, and
///   stored them in values_, it sets valuesApplied_ and asks for on_main_thread, which asks the
///   host to rescan the values (carry-over row 123, ruling M1-x): the host's first rescan, at the
///   load, may read a value a block in flight then overwrote.
/// - uiQueue_, the editor's changes (uiBeginGesture, uiSetValue, uiEndGesture: spec §6.3), main
///   thread → audio thread. process() and an active params.flush take them before the host's
///   events and apply the values as the host's are applied, then send each change on to the
///   host as an output event; an inactive flush, on the main thread, stores the values in
///   values_ for activate().
///
/// **The thread pool** (M2 sub-project 1's spec §6.7). The Processor hands each piece's rendering
/// layers to poolRunner_ (activate: Processor::setTaskRunner), which asks the host to run them on
/// its pool (clap_host_thread_pool.request_exec) when the host offers one, and runs them itself, in
/// order, when it doesn't or declines. The host runs each task through clap_plugin_thread_pool.exec
/// (threadPoolExec), on any of its threads, while request_exec waits; the output is the same bits
/// either way.
///
/// **Source kinds and text** (M2 sub-project 1's spec §6.4). A layer's Position (and Spray)
/// text follows its kind: the Live display (seconds behind the head) on a Live layer, the Sample
/// display (a percentage of the file) on a Sample layer, in either product, read from the Source
/// slot's base value in values_ (displayModeOf). When a layer's built kind changes, whoever owns
/// values_ then (the audio thread while active, the main thread while not) notes it
/// (checkKinds) and asks for on_main_thread, which asks the host to rescan the text. The text
/// follows the base value, not the effective one the audio follows, because a host can't rescan
/// text at the modulation rate, and what is shown is what the user set (ruling FR-6).
///
/// **The seed** (sub-project 5's spec §7). A new instance draws its own engine seed, on the main
/// thread at construction, from std::random_device; a session state brings its own, and a preset
/// keeps the instance's (M2 sub-project 5's D6).
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
    /// The most MIDI control events (CC 1, channel pressure, pitch bend) one process() call hands
    /// to the Processor in one go (its control span). Controls beyond it apply at the block's end,
    /// at the next grid boundary, as notes beyond kMaxNoteEvents do.
    static constexpr std::size_t kMaxControlEvents = 1024;
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
    // none is outstanding. A change made while kUiQueueCapacity changes wait is dropped.

    /// The editor's gesture on `slot` begins: CLAP_EVENT_PARAM_GESTURE_BEGIN. A value for `slot`
    /// still waiting unsent (Mutate's, or sendValues') is forgotten here and in uiSetValue: a
    /// later batch must not overwrite what the user just did.
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
    /// The editor's changes uiQueue_ itself dropped because it was full (SpscQueue::dropped):
    /// never applied, never sent to the host. For the tests. Any thread; relaxed, so it may lag.
    uint64_t uiQueueDropped() const noexcept { return uiQueue_.dropped(); }

    /// Whether the editor is open: the Processor publishes its grains and snapshots for it
    /// (Processor::setPublishing), from now while active, and from the next activate()
    /// otherwise. Main thread.
    void setPublishing(bool on) noexcept;
    bool publishing() const noexcept { return publishing_; }

    // ---- Mutate (M2 sub-project 5's spec §6.9): main thread ----

    /// The most slots one batch of Mutate's result sends to the editor's queue, at three entries
    /// each (begin, value, end): 300 entries. flushMutate keeps at most two batches in the queue
    /// at once (600 of kUiQueueCapacity's 1,024), leaving 424 for the editor's other changes.
    static constexpr std::size_t kMutateBatch = 100;

    /// Mutate: moves every parameter not locked by `amount` (gg::core::mutate: 0 to 1; relative
    /// jitter in each one's value domain), press number mutateCounter_ of this instance's seed.
    /// The locks are mutateLocked's ORed with every parameter of a layer that is off now
    /// (offLayerLocks, re-read at each press). Each slot whose value changed is sent as the
    /// editor's gesture (begin, value, end through uiQueue_), so the audio thread, the host and
    /// its automation hear it as a control moved by hand; the slots go in batches of at most
    /// kMutateBatch (flushMutate), the first now if the queue has room. A press starts from the
    /// values Mutate last sent or has yet to send, where the audio thread may not have applied
    /// them, and from the base values elsewhere: so two presses give the same sound however the
    /// host's drains fall between them. A slot still waiting to be sent is sent once, with its
    /// latest value. Deterministic: the same seed, locks, values and presses give the same sound.
    /// May leave the values unchanged (amount 0, or all locked).
    void mutate(float amount) noexcept;
    /// The safe way to set many parameters at once from the main thread (a preset-sized restore,
    /// an undo): each slot is sent as the editor's gesture, begin, value and end, like Mutate's,
    /// through the same pending set, batches and drain witness (flushMutate), so however many
    /// slots and however often it is called between the host's drains, nothing is dropped.
    /// Pushing the gestures straight through uiBeginGesture and the rest would overflow the
    /// queue past about 340 slots. A slot waiting unsent takes the latest value, whoever queued
    /// it (a later set, or a Mutate press); a Mutate press starts from these values like from its
    /// own. An out-of-range slot or a NaN value is ignored; the value is clamped and made a
    /// float as uiSetValue does. A preset load discards the slots not yet sent. Main thread.
    void sendValues(std::span<const std::pair<gg::core::ParamSlot, double>> values) noexcept;
    /// Sends the next batch of the slots a press left, when the queue has room for it: never a
    /// batch while an earlier one has not been seen to start draining (see mutateDrainSeen), so
    /// at most two batches wait in the queue, and nothing of Mutate's is ever dropped whenever
    /// and however rarely the host drains (a host that never drains gets no more than the first
    /// batch). The editor's 30 Hz tick and on_main_thread call it.
    void flushMutate() noexcept;
    /// Whether Mutate leaves `slot` where it is by the user's lock (setMutateLock); a press ORs
    /// these with the off layers' (offLayerLocks), which this doesn't include. The locks start
    /// as structuralLocks(mode()) and keep until changed or reset; they are not state. False for
    /// an out-of-range slot.
    bool mutateLocked(gg::core::ParamSlot slot) const noexcept;
    void setMutateLock(gg::core::ParamSlot slot, bool locked) noexcept;
    /// The locks to structuralLocks(mode()) (spec §6.9's "Reset locks"). An off layer's
    /// parameters stay out of a press by the off-layer rule alone, so a layer switched on after
    /// a reset is mutated.
    void resetMutateLocks() noexcept;

    // ---- Presets (M2 sub-project 5's spec §6.2): main thread ----

    /// Loads the preset file `file` (gg/core/Preset.h) for this product, all or nothing: reads at
    /// most kMaxStateBytes + 1 bytes of it, checks it with readPreset, then applies it as a state
    /// load does (applyState, LoadKind::Preset), keeping the seed (D6) and, when it names no
    /// sample, the sample loaded (D5). False, with `error` saying why and nothing changed, when
    /// the file can't be read ("can't read <file name>", and `osError`, if given, the errno of a
    /// file that can't be opened, 0 otherwise), is longer than kMaxStateBytes ("the preset is
    /// larger than 64 MiB"), or isn't a preset of this product ("not a Giga Grains FX preset",
    /// "not a Giga Grains preset"). A preset of this product whose values don't decode can't be
    /// read either. May throw std::bad_alloc, changing nothing.
    bool loadPresetFile(const std::filesystem::path &file, std::string &error,
                        int32_t *osError = nullptr);
    /// INIT (spec §6.2, D8): applies initState(mode()) as a preset (every default, the factory
    /// routes, the name "Init"), keeping the seed and the sample, as a fresh instance sounds. May
    /// throw std::bad_alloc, changing nothing.
    void loadInit();
    /// The name of the preset loaded last, or of the session restored (its state's `name`):
    /// "Init" in a new instance. stateSave writes it as the state's name.
    std::string presetName() const { return presetName_; }
    /// Whether any base value (values_) differs from the loaded preset's, bit for bit, over every
    /// parameter: the header's edited mark. False in a new instance and right after a load.
    bool presetEdited() const noexcept;

    // ---- The preset browser (spec §6.8): main thread ----

    /// The browser's catalog of this product's presets, made on first use from mode() and the
    /// settings file this instance was created with (the loader's), and scanned then.
    PresetCatalog &presetCatalog();
    /// Loads catalog entry `index` (INIT through loadInit, a file through loadPresetFile) and
    /// tells the host with clap_host_preset_load.loaded, with the location kind, location and
    /// load key presetLoadFromLocation takes for it. "" on success, else the error for the user
    /// ("can't read <file>", "out of memory", "no such preset"), and the host hears nothing.
    std::string loadCatalogPreset(std::size_t index) noexcept;
    /// Save as…: the current sound (the base values, the routes, and the instrument's sample
    /// entry as stateSave writes them) as the user preset `name`, with `tags` (only the
    /// vocabulary's words are kept) and `description`, through PresetCatalog::save. On success it
    /// is the loaded preset (presetName() is `name`, and presetEdited() false) and the host's
    /// state is marked dirty. "" or the error for the user.
    std::string savePreset(const std::string &name, const std::vector<std::string> &tags,
                           const std::string &description, bool overwrite) noexcept;
    /// Saves the per-machine settings' content folder (UTF-8; empty clears it). "" on success,
    /// else why not: a path with a NUL byte (the settings writer would cut it there), or a
    /// settings file that can't be written.
    std::string setContentFolder(const std::string &utf8Folder) noexcept;

    /// The rate of the last activate(), kDefaultTailRate before the first. Main thread.
    double activeSampleRate() const noexcept { return sampleRate_.load(std::memory_order_relaxed); }

    /// The instrument's tail at `sampleRate`: kInstrumentTailSeconds in frames, rounded up,
    /// and kTailInfinite − 1 where that would reach kTailInfinite (or for a NaN or negative
    /// rate), so it is never read as infinite.
    static uint32_t instrumentTailFrames(double sampleRate) noexcept;

    /// Whether `header` is an event toNoteEvent converts, which counts toward kMaxNoteEvents: a
    /// CLAP note-on, note-off or note choke, or a MIDI note-on, note-off or CC 123. Stateless.
    static bool isNoteEvent(const clap_event_header *header) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Whether `header` is an event toControlEvent converts, which counts toward
    /// kMaxControlEvents: a MIDI CC 1, channel pressure or pitch bend. Stateless.
    static bool isControlEvent(const clap_event_header *header) noexcept SST_CPPUTILS_NONBLOCKING;
    /// The MIDI control `header` as a ControlEvent (M2 sub-project 1's spec §6.8), at its time,
    /// on its channel: CC 1 (0xBn, controller 1) is ModWheel, value / 127; channel pressure
    /// (0xDn) is Aftertouch, pressure / 127; pitch bend (0xEn) is PitchBend,
    /// (msb << 7 | lsb) / 16383, so its centre 0x2000 is 8192 / 16383. Each data byte is taken as
    /// its 7 bits. False for any other event. Stateless.
    static bool toControlEvent(const clap_event_header *header,
                               gg::core::ControlEvent &event) noexcept SST_CPPUTILS_NONBLOCKING;

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

    // ---- The thread pool (see the class comment) ----

    bool implementsThreadPool() const noexcept override { return true; }
    /// clap_plugin_thread_pool.exec: task `taskIndex` of the request the Processor's runner made
    /// (Processor::execTask). The host calls it only while that request_exec waits, so only
    /// inside our process(), on any of its threads; an index past the piece's tasks, or a call
    /// while inactive, does nothing.
    void threadPoolExec(uint32_t taskIndex) noexcept SST_CPPUTILS_NONBLOCKING override;

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

    // ---- Preset load (clap.preset-load, spec §6.2) ----

    bool implementsPresetLoad() const noexcept override { return true; }
    /// CLAP_PRESET_DISCOVERY_LOCATION_FILE: loadPresetFile(location); _PLUGIN with the load key
    /// kInitLoadKey: loadInit(). Anything else fails with "unknown preset location". Tells the
    /// host (clap_host_preset_load, when it offers it): loaded() with the same kind, location and
    /// key on success, on_error() with the reason otherwise.
    bool presetLoadFromLocation(uint32_t locationKind, const char *location,
                                const char *loadKey) noexcept override;

    // ---- Remote controls: eight pages, M1's "Perform" (sub-project 5's spec §3.3), "Cloud"
    // (spec §4) and "Space" (sub-project 4's spec §6.6), then "Layers", "Morph" and "Cloud B" to
    // "Cloud D" (M2 sub-project 1's spec §6.9) ----

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
    /// What a state applied by applyState is: a session (state.load), whose seed and sample
    /// replace the instance's, or a preset (a file or INIT), which keeps the seed (D6) and, when
    /// it names no sample, the sample (D5).
    enum class LoadKind
    {
        Session,
        Preset,
    };
    /// The one state-load path (spec §6.2), for stateLoad and the presets: `state`'s values and
    /// routes (and its seed and sample per `kind`) take effect as stateLoad's comment says, its
    /// name becomes presetName_ and its values presetValues_, and the host rescans the values.
    /// Main thread. All or nothing: may throw std::bad_alloc before anything changes.
    void applyState(gg::core::SessionState &&state, LoadKind kind);

    clap_process_status
    processNonblocking(const clap_process *process) noexcept SST_CPPUTILS_NONBLOCKING;
    void countNoteOns(const clap_input_events *events) noexcept SST_CPPUTILS_NONBLOCKING;
    /// The parameter event `header` as a ParamEvent: false for any other event, an unknown
    /// parameter or a NaN value.
    static bool toParamEvent(const clap_event_header *header,
                             gg::core::ParamEvent &event) noexcept SST_CPPUTILS_NONBLOCKING;
    /// The note event `header` as a NoteEvent (spec §6.1): CLAP's note-on, note-off and note
    /// choke, MIDI's note-on, note-off and CC 123 (all notes off on its channel, ruling R14).
    /// False for any other event. An On carries its velocity, the mod matrix's Velocity source
    /// (M2 sub-project 1's spec §6.8): CLAP's 0..1 as it comes, MIDI's / 127. The port is
    /// ignored; out-of-range channels and keys pass through, for the Processor to drop.
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
    /// Applies the parameter, note and control events of `in` from index `first` between two
    /// process() calls: values at once with setBase, modulation, notes and controls at the next
    /// control-grid boundary. Needs the processor; audio thread.
    void applyBetweenBlocks(const clap_input_events *in,
                            uint32_t first) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Applies a state loaded since the last call, if there is one: its seed and its routes, and
    /// its values when it carries them (the FX). Audio thread.
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
    /// ParamEvents at offset 0, for the caller to hand to the Processor (its loop after process
    /// stores them in values_); without, applied with setBase while active and stored in values_
    /// here, active or not. Returns the new count. The audio thread, or the main thread while
    /// inactive (the queue's consumer).
    std::size_t takeUiChanges(const clap_output_events *out, gg::core::ParamEvent *values,
                              std::size_t count) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Pushes `change` to uiQueue_ and asks the host for a flush (see uiBeginGesture). Main
    /// thread.
    void queueUiChange(const UiParamChange &change) noexcept;
    /// Stores the Processor's base values in values_.
    void storeBases() noexcept SST_CPPUTILS_NONBLOCKING;
    /// Layer `layer`'s built kind as values_ has it: builtKind(sourceKindOf(...)) of its Source
    /// slot's base value. Any thread.
    gg::SourceMode kindOf(uint8_t layer) const noexcept SST_CPPUTILS_NONBLOCKING;
    /// The product whose display Position and Spray take for the parameter in `slot`: the FX's
    /// (seconds behind the head) for a Live layer's, the instrument's (a percentage of the file)
    /// for a Sample layer's (kindOf), and the product's own for every other parameter.
    gg::core::Mode displayModeOf(gg::core::ParamSlot slot) const noexcept;
    /// Compares each layer's kindOf with shownKinds_ and, if any changed, records them and asks
    /// for on_main_thread (clap_host.request_callback, thread-safe), once until that call comes,
    /// where the host is asked to rescan the text. Whoever owns values_: the audio thread while
    /// active, the main thread while not. Its real-time safety is the host's, as requestCollect's
    /// is.
    void checkKinds() noexcept SST_CPPUTILS_NONBLOCKING;
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
        gg::core::ModRoutes routes;
        uint64_t seed;
        uint32_t epoch; // uiEpoch_ after the load: the editor's values queued before are stale
        bool hasValues;
    };
    // Main thread → audio thread.
    gg::core::LatestValue<LoadedState> loadedState_;
    // The engine seed: drawn at construction, saved and loaded with the state, given to the
    // processor at activate() and, loaded while active, through loadedState_. Main thread.
    uint64_t seed_;
    // The mod matrix's routes in effect (M2 sub-project 1's spec §6.10): the product's factory
    // routes in a new instance, a loaded state's from its load on; saved with the state, given to
    // the processor at activate() and, loaded while active, through loadedState_. Routes have no
    // other way to change yet. Main thread.
    gg::core::ModRoutes routes_;
    // The loaded preset's name and base values (applyState; "Init" and the defaults in a new
    // instance): presetName(), presetEdited(). Main thread.
    std::string presetName_{"Init"};
    // The browser's catalog (presetCatalog()), made on first use, and the settings file it reads.
    std::filesystem::path catalogSettingsFile_;
    std::unique_ptr<PresetCatalog> catalog_;
    gg::core::ParamValues presetValues_{};
    // Set by the audio thread when it has stored a state's values in values_, cleared by
    // on_main_thread, which then asks the host to rescan them (requestValueRescan).
    std::atomic<bool> valuesApplied_{false};
    // Each layer's built kind as last noted by checkKinds, whose caller owns it (see there).
    std::array<gg::SourceMode, gg::core::kLayers> shownKinds_{};
    // Set by checkKinds when a layer's kind changed, cleared by on_main_thread, which then asks
    // the host to rescan the parameters' text.
    std::atomic<bool> kindsChanged_{false};

    // The Processor's runner (see the class comment): the host's pool, or a loop on the calling
    // thread. Its run() is only ever called by Processor::process, so only inside our process(),
    // on the audio thread: CLAP allows request_exec there alone (clap-helpers checks the thread,
    // when the host offers clap.thread-check).
    class HostPoolRunner final : public gg::TaskRunner
    {
      public:
        explicit HostPoolRunner(GigaGrainsPlugin &plugin) noexcept : plugin_(plugin) {}
        /// Asks the host to run the `count` tasks (request_exec), when it offers its pool;
        /// otherwise, or if it declines (CLAP's rule: the plugin then runs them itself), runs
        /// fn(ctx, i) for each i in order. The host reaches each task through threadPoolExec,
        /// which calls Processor::execTask, the very function fn(ctx, i) is (the Processor's
        /// layerTask with ctx the Processor), so either way each task runs once.
        void run(uint32_t count, gg::TaskFn fn, void *ctx) noexcept override;

      private:
        GigaGrainsPlugin &plugin_;
    };
    HostPoolRunner poolRunner_{*this};

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
    /// The values the next press starts from: a slot's value waiting to be sent, else the value
    /// Mutate last sent it while the audio thread may not have applied it, else values_. Main
    /// thread.
    gg::core::ParamValues mutateInput() const noexcept;
    /// Notes a drain of uiQueue_ begun since Mutate last armed uiFlushRequested_ (mutateArm),
    /// if the flag is clear: every drain clears it first, and only a push or an arm sets it, so
    /// a clear flag seen after an arm is a drain that began after it, which takes every entry
    /// queued before the arm (takeUiChanges pops up to a whole queue's worth, and the pairing
    /// fences there and in mutateArm make it see them; the clear is a release store, which this
    /// acquire load pairs with, so a later push sees the pops and a press the storeValues of every
    /// drain before the one seen; the fences order the flag against the queue, nothing more).
    /// Advances the generations below when it has. Main thread.
    void mutateDrainSeen() noexcept;
    /// Sets uiFlushRequested_ and, when that asks for a flush the host offers (clap.params) and
    /// none was outstanding, asks for one: as queueUiChange does after a push, and on a host
    /// without clap.params too, where only the drains clear the flag. Main thread.
    void mutateArm() noexcept;
    /// Asks the host for on_main_thread while slots wait unsent, once until it comes. Main thread.
    void requestMutateCallback() noexcept;
    /// Forgets a value waiting unsent for `slot` (the editor's own gesture on it begins or sets
    /// it: the user's latest move wins). flushMutate's own sends reach it too, harmlessly: it
    /// clears the slot itself. Main thread.
    void dropPending(gg::core::ParamSlot slot) noexcept;
    /// Marks `slot` waiting to be sent at `value`, replacing a value already waiting. Main thread.
    void queuePending(std::size_t slot, float value) noexcept;
    // Mutate: the user's locks, the press counter, the slots waiting to be sent (one value each,
    // a later press's replacing an earlier's), and the record of the values sent, by batch
    // generation. Main thread only; none of it reaches the audio thread but as uiQueue_'s
    // changes.
    gg::core::LockSet mutateLocks_{};
    uint32_t mutateCounter_{0};
    gg::core::LockSet mutatePending_{}; // slots waiting to be sent
    gg::core::ParamValues mutatePendingValue_{};
    gg::core::ParamValues mutateSentValue_{};                     // what Mutate last sent each slot
    std::array<uint32_t, gg::core::kParamCount> mutateSentGen_{}; // its batch; 0: none
    uint32_t mutateGen_{0};        // the last batch sent (1 the first)
    uint32_t mutateArmedGen_{0};   // mutateGen_ when last armed
    uint32_t mutateStartedGen_{0}; // batches up to this one have had a drain begin after them
    uint32_t mutateAppliedGen_{0}; // and up to this one, a drain end: applied, in values_
    bool mutateArmed_{false};      // armed, and no drain seen since
    bool mutateCallbackRequested_{false}; // on_main_thread asked for, not yet come
    bool mutateBatchArmed_{false};        // a batch went out since the last drain seen
    // The editor's changes, main thread → audio thread (or the main thread's own flush while
    // inactive): see uiBeginGesture.
    gg::core::SpscQueue<UiParamChange, kUiQueueCapacity> uiQueue_;
    // The load epoch (fix wave I1): applyState bumps uiEpoch_ (main thread) and each change
    // queued carries it. uiAppliedEpoch_ is the epoch of the last load the queue's consumer has
    // applied (the audio thread's while active, through loadedState_; the main thread's while
    // inactive, set by applyState itself): takeUiChanges drops a value queued in an earlier one,
    // so a change made before a load never lands on the loaded preset, nor reaches the host.
    uint32_t uiEpoch_{0};
    uint32_t uiAppliedEpoch_{0};
    // Set by the main thread when it asks the host for a flush, cleared by the queue's consumer
    // before it drains: one request outstanding at a time (queueUiChange).
    std::atomic<bool> uiFlushRequested_{false};
    std::atomic<uint64_t> uiEventsDropped_{0}; // the consumer's
    bool publishing_{false};                   // main thread: the editor is open
    // The parameter, note and control events of one process() call. Audio thread.
    std::array<gg::core::ParamEvent, kMaxParamEvents> events_{};
    std::array<gg::core::NoteEvent, kMaxNoteEvents> notes_{};
    std::array<gg::core::ControlEvent, kMaxControlEvents> controls_{};
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
