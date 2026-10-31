// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "GigaGrainsPlugin.h"

#include <algorithm>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <span>
#include <string>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Preset.h"
#include "gg/core/PresetInfo.h"
#include "gg/core/Rng.h"
#include "gg/core/SessionState.h"
#include "SettingsPath.h"
#include "gg/media/SettingsFile.h"
#include "gg/media/Utf8Path.h"
#include "gg/plugin/Identity.h"
#include "sst/basic-blocks/params/ParamMetadata.h"

namespace gg::plugin
{
namespace
{
using gg::core::ControlEvent;
using gg::core::ControlKind;
using gg::core::kParamCount;
using gg::core::LayerParam;
using gg::core::NoteEvent;
using gg::core::NoteEventType;
using gg::core::ParamEvent;
using gg::core::ParamSlot;

// The remote-controls pages, in page order, each in slot order: "Perform", the eight macros
// (sub-project 5's spec §3.3), then "Cloud" (layer A's, spec §4) and "Space" (sub-project 4's
// spec §6.6), M1's three, unchanged at indices 0 to 2; then "Layers", "Morph" and "Cloud B" to
// "Cloud D" (M2 sub-project 1's spec §6.9). A page's index is its page_id. ParamSlot::Count is an
// empty slot (paramId gives it CLAP_INVALID_ID).
struct RemotePage
{
    const char *name;
    std::array<ParamSlot, CLAP_REMOTE_CONTROLS_COUNT> slots;
};

// Layer `layer`'s cloud page: its Position, Size, Density, Pitch, Spray, Stereo, Freeze and
// Level.
constexpr RemotePage cloudPage(const char *name, uint8_t layer) noexcept
{
    using gg::core::layerSlot;
    return {name,
            {layerSlot(LayerParam::Position, layer), layerSlot(LayerParam::Size, layer),
             layerSlot(LayerParam::Density, layer), layerSlot(LayerParam::Pitch, layer),
             layerSlot(LayerParam::Spray, layer), layerSlot(LayerParam::Stereo, layer),
             layerSlot(LayerParam::Freeze, layer), gg::core::levelSlot(layer)}};
}

constexpr ParamSlot kEmpty = ParamSlot::Count;

constexpr std::array<RemotePage, 8> kRemotePages{{
    {"Perform",
     {ParamSlot::Macro1, ParamSlot::Macro2, ParamSlot::Macro3, ParamSlot::Macro4, ParamSlot::Macro5,
      ParamSlot::Macro6, ParamSlot::Macro7, ParamSlot::Macro8}},
    {"Cloud",
     {ParamSlot::Position, ParamSlot::Size, ParamSlot::Density, ParamSlot::Pitch, ParamSlot::Spray,
      ParamSlot::Stereo, ParamSlot::Freeze, ParamSlot::Mix}},
    {"Space",
     {ParamSlot::Diffusion, ParamSlot::ReverbAmount, ParamSlot::ReverbDecay, ParamSlot::ReverbSize,
      ParamSlot::ReverbDamping, ParamSlot::ReverbPredelay, ParamSlot::ReverbModulation,
      ParamSlot::DryToReverb}},
    {"Layers",
     {ParamSlot::LayerAOn, ParamSlot::LayerBOn, ParamSlot::LayerCOn, ParamSlot::LayerDOn,
      ParamSlot::Level, ParamSlot::LevelB, ParamSlot::LevelC, ParamSlot::LevelD}},
    {"Morph",
     {ParamSlot::MorphX, ParamSlot::MorphY, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty}},
    cloudPage("Cloud B", 1),
    cloudPage("Cloud C", 2),
    cloudPage("Cloud D", 3),
}};

const char *const kInstrumentFeatures[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_GRANULAR,
    CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const char *const kFxFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_GRANULAR,
                                   CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const clap_plugin_descriptor kInstrumentDescriptor{
    CLAP_VERSION_INIT,
    identity::kInstrumentId,
    identity::kInstrumentName,
    identity::kVendor,
    identity::kUrl,
    "", // manual_url
    "", // support_url
    GG_VERSION_STRING,
    "A granular instrument for evolving textures",
    kInstrumentFeatures,
};

const clap_plugin_descriptor kFxDescriptor{
    CLAP_VERSION_INIT,
    identity::kFxId,
    identity::kFxName,
    identity::kVendor,
    identity::kUrl,
    "", // manual_url
    "", // support_url
    GG_VERSION_STRING,
    "A granular effect for evolving textures, playable from notes",
    kFxFeatures,
};

/// Whether `slot` is hidden in `product`: the instrument has no dry signal, so Mix and Dry to
/// Reverb do nothing there. It keeps the parameters, hidden, so both products share one parameter
/// list and one state format (spec §4; sub-project 4's spec §6.5). Feedback is shown in both
/// products (M2 sub-project 1's spec §6.4, §6.9). paramsInfo flags them, and their remote
/// controls stay empty.
constexpr bool hiddenIn(Product product, ParamSlot slot) noexcept
{
    return product == Product::Instrument &&
           (slot == ParamSlot::Mix || slot == ParamSlot::DryToReverb);
}

void copyName(char (&dst)[CLAP_NAME_SIZE], const char *src) noexcept
{
    std::snprintf(dst, sizeof(dst), "%s", src);
}

// A new instance's engine seed (sub-project 5's spec §7): two 32-bit draws from the OS's entropy
// source, or, if std::random_device throws (it may when it has no source), a splitmix64 of the
// steady clock. Main thread. The audio thread's randomness still comes only from gg::rng,
// keyed by this seed.
uint64_t freshSeed() noexcept
{
    try
    {
        std::random_device device;
        const auto high = static_cast<uint64_t>(device()) & 0xffffffffu;
        const auto low = static_cast<uint64_t>(device()) & 0xffffffffu;
        return (high << 32) | low;
    }
    catch (...)
    {
        return gg::rng::splitmix64(
            static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    }
}
} // namespace

const clap_plugin_descriptor *descriptorFor(Product product) noexcept
{
    return product == Product::Instrument ? &kInstrumentDescriptor : &kFxDescriptor;
}

GigaGrainsPlugin::GigaGrainsPlugin(const clap_host *host, Product product)
    : plugHelper_t(descriptorFor(product), host), product_(product), seed_(freshSeed()),
      routes_(gg::core::factoryRoutes(mode()))
{
    // Main thread. The first paramMeta() call builds its tables, which allocates: it's made here,
    // never on the audio thread.
    catalogSettingsFile_ = settingsFile(); // the browser's catalog reads this file (presetCatalog)
    mutateLocks_ = gg::core::structuralLocks(mode());
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        const auto &meta = gg::core::paramMeta(static_cast<ParamSlot>(s), mode());
        minValues_[s] = meta.minVal;
        maxValues_[s] = meta.maxVal;
        storeValue(static_cast<ParamSlot>(s), meta.defaultVal);
        presetValues_[s] = values_[s].load(std::memory_order_relaxed); // INIT's: not edited
    }
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        shownKinds_[layer] = kindOf(layer); // the defaults' kinds: nothing to rescan yet
    }
    if (product_ == Product::Instrument)
    {
        SampleLoader::Callbacks callbacks;
        // From the loader's worker: clap_host.request_callback is thread-safe.
        callbacks.requestCallback = [this] { _host.requestCallback(); };
        callbacks.markDirty = [this] {
            if (_host.canUseState())
            {
                _host.stateMarkDirty();
            }
        };
        callbacks.offer = [this](std::shared_ptr<const gg::PreparedSource> source) {
            if (processor_ != nullptr)
            {
                processor_->sources().offer(std::move(source));
            }
        };
        loader_ = std::make_unique<SampleLoader>(settingsFile(), std::move(callbacks));
    }
}

GigaGrainsPlugin::~GigaGrainsPlugin()
{
    // The worker first: it calls back into this object.
    loader_.reset();
}

bool GigaGrainsPlugin::activate(double sampleRate, uint32_t /*minFrameCount*/,
                                uint32_t maxFrameCount) noexcept
{
    // Main thread, never concurrent with process(): everything process() needs is allocated
    // here. The Processor throws on an invalid configuration; that fails the activation.
    bool loaderActive = false;
    try
    {
        // The engine renders in blocks of at most kMaxEngineBlock frames, so its buffers stay
        // small whatever maxFrameCount the host announces: the live ring holds N + maxBlockSize
        // frames (ruling M1-o). The Processor renders longer calls in pieces, and its output
        // doesn't depend on the block size (INTENT §8.6).
        const gg::core::ProcessorConfig config{
            sampleRate,
            std::clamp<uint32_t>(maxFrameCount, 1, kMaxEngineBlock),
            seed_,
            mode(),
        };
        auto processor = std::make_unique<gg::core::Processor>(config);
        // The values set while inactive, or left by the last activation, set at once (ruling
        // FR-3: one recomputation, not one per slot).
        gg::core::ParamValues values{};
        for (std::size_t s = 0; s < kParamCount; ++s)
        {
            values[s] = values_[s].load(std::memory_order_relaxed);
        }
        processor->setBases(values);
        // The routes in effect: the factory routes, or a loaded state's.
        processor->setRoutes(routes_);
        // The host's pool, when it offers one, renders the layers (spec §6.7).
        processor->setTaskRunner(&poolRunner_);
        // The sample, at this rate before the first block (D8): a restored one converted here,
        // and one loaded at another rate converted again here (ruling R18).
        if (loader_ != nullptr)
        {
            loaderActive = true;
            processor->setSourceNow(loader_->activate(sampleRate));
        }
        processor->setPublishing(publishing_); // an editor opened before this activation
        processor_ = std::move(processor);
        engineBlockSize_ = config.maxBlockSize;
        noteIds_.fill(NoteIdSlot{}); // a new session holds no notes
        collectRequested_.store(false, std::memory_order_relaxed);
        sampleRate_.store(sampleRate, std::memory_order_relaxed); // the tail's (tailGet)
        return true;
    }
    catch (...)
    {
        if (loaderActive)
        {
            loader_->deactivate();
        }
        return false;
    }
}

void GigaGrainsPlugin::deactivate() noexcept
{
    // Main thread, with the audio thread stopped, so this may take loadedState_. A state loaded
    // while active that the audio thread never took is newer than the last block's events: it
    // goes into the values the next activate() starts from, as the next block would have
    // applied it. Otherwise it would apply, stale, at the first block after a reactivation.
    // Its seed is seed_ already, which the next activate() builds the engine with.
    LoadedState loaded{};
    if (loadedState_.take(loaded) && loaded.hasValues)
    {
        for (std::size_t s = 0; s < kParamCount; ++s)
        {
            storeValue(static_cast<ParamSlot>(s), loaded.values[s]);
        }
    }
    // The same for an offer the audio thread never took (the instrument's restores hand their
    // values over with their source, D8): its values apply now, and go into values_.
    if (processor_ != nullptr && processor_->takeOffers())
    {
        storeBases();
    }
    // The main thread owns values_ from here: a kind those values changed asks for its rescan.
    checkKinds();
    // The loader's worker stops (a job under way is cancelled and queued again), and what it
    // finished applies with nothing offered: the next activate() gives it to the new processor.
    if (loader_ != nullptr)
    {
        loader_->deactivate();
    }
    // The next activate() may bring a new sample rate: the processor doesn't survive into it.
    // Its SourceExchange frees the sources it still holds, here on the main thread.
    processor_.reset();
}

void GigaGrainsPlugin::onMainThread() noexcept
{
    if (loader_ != nullptr)
    {
        loader_->onMainThread();
    }
    if (processor_ != nullptr)
    {
        // Cleared first: a source handed back after the collect asks for another call. The
        // fence pairs with requestCollect's: the flag's store and the ring's load here, the
        // ring's store and the flag's exchange there, are a store-buffering pattern, and without
        // both fences each side could miss the other's store, leaving a retired source
        // uncollected with no call requested. With them, either this collect sees the source or
        // that exchange sees the flag cleared and asks again.
        collectRequested_.store(false, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        try
        {
            processor_->sources().collect();
        }
        catch (...)
        {
            // collect() only frees; nothing it calls throws.
        }
    }
    // The next batch of a Mutate press, once the host has drained the last one. Cleared first:
    // slots still waiting after it ask for another call (flushMutate).
    mutateCallbackRequested_ = false;
    flushMutate();
    // A state's values the audio thread has applied, and stored in values_ after the load's own
    // rescan: the host reads them again (carry-over row 123). Cleared first, so values applied
    // after this ask for another call.
    if (valuesApplied_.exchange(false, std::memory_order_acq_rel) && _host.canUseParams())
    {
        _host.paramsRescan(CLAP_PARAM_RESCAN_VALUES);
    }
    // A layer whose kind changed shows Position in the other display (spec §6.4): the host reads
    // the text again. Cleared first, as above.
    if (kindsChanged_.exchange(false, std::memory_order_acq_rel) && _host.canUseParams())
    {
        _host.paramsRescan(CLAP_PARAM_RESCAN_TEXT);
    }
}

void GigaGrainsPlugin::reset() noexcept
{
    if (processor_ != nullptr)
    {
        processor_->reset();
    }
    noteIds_.fill(NoteIdSlot{}); // the reset released every note
}

clap_process_status GigaGrainsPlugin::process(const clap_process *process) noexcept
{
    return processNonblocking(process);
}

void GigaGrainsPlugin::threadPoolExec(uint32_t taskIndex) noexcept
{
    // On one of the host's pool threads, while the request the runner made waits (spec §6.7).
    if (processor_ != nullptr)
    {
        processor_->execTask(taskIndex);
    }
}

void GigaGrainsPlugin::HostPoolRunner::run(uint32_t count, gg::TaskFn fn, void *ctx) noexcept
{
    // Audio thread, inside process(): Processor::process is the only caller. The host runs the
    // tasks through threadPoolExec, the same work as fn(ctx, i) (see the declaration). Its
    // request_exec's real-time safety is the host's; clap-wrapper's formats never offer the pool,
    // so their hosts always take the loop.
    if (count == 0)
    {
        return;
    }
    if (plugin_._host.canUseThreadPool() && plugin_._host.threadPoolRequestExec(count))
    {
        return;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        fn(ctx, i);
    }
}

clap_process_status GigaGrainsPlugin::processNonblocking(const clap_process *process) noexcept
{
    countNoteOns(process->in_events);
    if (processor_ == nullptr)
    {
        return CLAP_PROCESS_CONTINUE; // clap-helpers lets process() through only while active
    }
    // The values of a state loaded while active, and the instrument's offers (a restored
    // session's values and sample together, D8): both apply from this call's first frame.
    applyLoadedState();
    takeOffers();

    // The editor's values first, at the call's first frame, so the host's events (automation
    // of the same parameter, say) come after them; then the call's parameter and note events,
    // in the host's order (by time), up to the first that finds its array (events_ or notes_)
    // full. The editor's take at most kUiQueueCapacity of events_.
    std::size_t count = takeUiChanges(process->out_events, events_.data(), 0);
    const clap_input_events *in = process->in_events;
    const uint32_t numEvents = in != nullptr ? in->size(in) : 0;
    std::size_t noteCount = 0;
    std::size_t controlCount = 0;
    uint32_t next = 0;
    for (; next < numEvents; ++next)
    {
        const clap_event_header *header = in->get(in, next);
        ParamEvent event{};
        if (toParamEvent(header, event))
        {
            if (count == kMaxParamEvents)
            {
                break;
            }
            events_[count++] = event;
        }
        else if (isNoteEvent(header))
        {
            // Full before converting: toNoteEvent updates the note IDs, so the event it converts
            // must not be converted again by applyBetweenBlocks.
            if (noteCount == kMaxNoteEvents)
            {
                break;
            }
            if (toNoteEvent(header, notes_[noteCount]))
            {
                ++noteCount;
            }
        }
        else if (isControlEvent(header))
        {
            if (controlCount == kMaxControlEvents)
            {
                break;
            }
            if (toControlEvent(header, controls_[controlCount]))
            {
                ++controlCount;
            }
        }
    }

    // The host's transport, for the transport sources (spec §6.8), from this call's first
    // boundary. A call without one leaves the Processor's as it was: it plays on from there.
    if (process->transport != nullptr)
    {
        const clap_event_transport &t = *process->transport;
        processor_->setTransport({
            .hasBeats = (t.flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0,
            .beatPosition =
                static_cast<double>(t.song_pos_beats) / static_cast<double>(CLAP_BEATTIME_FACTOR),
            .tempoBpm = (t.flags & CLAP_TRANSPORT_HAS_TEMPO) != 0 ? t.tempo : 120.0,
            .playing = (t.flags & CLAP_TRANSPORT_IS_PLAYING) != 0,
            .beatsPerBar = (t.flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE) != 0 && t.tsig_num > 0
                               ? t.tsig_num
                               : 4,
        });
    }

    // The Processor takes care of absent ports and channels: a missing input is silence and a
    // mono one feeds both channels. In both products the input feeds every layer's live ring;
    // only the FX's feeds a dry path too (the instrument's sidechain, D2).
    const clap_audio_buffer *input =
        process->audio_inputs_count > 0 ? process->audio_inputs : nullptr;
    const clap_audio_buffer *output =
        process->audio_outputs_count > 0 ? process->audio_outputs : nullptr;
    processor_->process(
        input != nullptr ? input->data32 : nullptr, input != nullptr ? input->channel_count : 0,
        output != nullptr ? output->data32 : nullptr, output != nullptr ? output->channel_count : 0,
        process->frames_count, std::span<const ParamEvent>(events_.data(), count),
        std::span<const NoteEvent>(notes_.data(), noteCount),
        std::span<const ControlEvent>(controls_.data(), controlCount));

    // The values just handed over, for the host to read. Not the Processor's base(): an event
    // waits there for its grid boundary, which may come in the next call (ruling M1-u).
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!events_[i].isModulation)
        {
            storeValue(events_[i].slot, static_cast<float>(events_[i].value));
        }
    }

    // An automation, note or control storm larger than events_, notes_ or controls_: the rest
    // applies at the block's end.
    if (next < numEvents)
    {
        applyBetweenBlocks(in, next);
    }
    requestCollect();
    checkKinds();
    return CLAP_PROCESS_CONTINUE;
}

void GigaGrainsPlugin::takeOffers() noexcept
{
    if (processor_->takeOffers())
    {
        storeBases();
        requestValueRescan();
    }
}

void GigaGrainsPlugin::requestValueRescan() noexcept
{
    // At most one request outstanding: on_main_thread clears the flag before it rescans.
    if (!valuesApplied_.exchange(true, std::memory_order_acq_rel))
    {
        // clap_host.request_callback from the audio thread, as in requestCollect (see there).
        // It runs at most once per state applied.
        _host.requestCallback();
    }
}

gg::SourceMode GigaGrainsPlugin::kindOf(uint8_t layer) const noexcept
{
    // sourceKindOf reads the layer's Source slot alone.
    gg::core::ParamValues values{};
    const auto s = static_cast<std::size_t>(gg::core::layerSlot(LayerParam::Source, layer));
    values[s] = values_[s].load(std::memory_order_relaxed);
    return gg::core::builtKind(gg::core::sourceKindOf(values, layer), mode());
}

gg::core::Mode GigaGrainsPlugin::displayModeOf(ParamSlot slot) const noexcept
{
    const auto ref = gg::core::layerOf(slot);
    if (!ref.has_value() || (ref->param != LayerParam::Position && ref->param != LayerParam::Spray))
    {
        return mode();
    }
    return kindOf(ref->layer) == gg::SourceMode::Live ? gg::core::Mode::Fx
                                                      : gg::core::Mode::Instrument;
}

void GigaGrainsPlugin::checkKinds() noexcept
{
    bool changed = false;
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        const gg::SourceMode kind = kindOf(layer);
        if (kind != shownKinds_[layer])
        {
            shownKinds_[layer] = kind;
            changed = true;
        }
    }
    // At most one request outstanding: on_main_thread clears the flag before it rescans.
    if (changed && !kindsChanged_.exchange(true, std::memory_order_acq_rel))
    {
        // clap_host.request_callback, as in requestCollect (see there): once per kind change.
        _host.requestCallback();
    }
}

void GigaGrainsPlugin::storeBases() noexcept
{
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        const auto slot = static_cast<ParamSlot>(s);
        storeValue(slot, processor_->base(slot));
    }
}

void GigaGrainsPlugin::requestCollect() noexcept
{
    // Pairs with onMainThread's fence (see there): the retire's store to the ring comes before
    // it, the flag's exchange after.
    std::atomic_thread_fence(std::memory_order_seq_cst);
    // At most one request outstanding: on_main_thread clears the flag before it collects.
    if (processor_->sources().awaitingCollect() &&
        !collectRequested_.exchange(true, std::memory_order_acq_rel))
    {
        // clap_host.request_callback from the audio thread. CLAP marks it thread-safe, not
        // real-time safe, so this depends on the host: every clap-wrapper format (VST3, AU,
        // standalone) only sets an atomic flag there, and only a native CLAP host's own
        // implementation could lock or allocate. It runs at most once per retired source.
        _host.requestCallback();
    }
}

uint32_t GigaGrainsPlugin::tailGet() const noexcept
{
    // A constant per product (ruling R-T7a), never a function of the values, Freeze or the
    // notes, and never announced with clap_host_tail.changed. Don't "improve" it: clap-wrapper
    // turns tail.changed into VST3's restartComponent(kLatencyChanged), on which the host must
    // deactivate and reactivate the plugin (the VST3 SDK's documentation), and into AU's
    // PropertyChanged(TailTime), both main-thread calls while CLAP makes tail.changed an
    // audio-thread one. A tail that followed the values would restart the plugin, emptying the
    // FX's live ring and dropping its held notes and Freeze, at every note, Freeze toggle or
    // Reverb Amount change. So the FX, a live-buffer effect that can sound with no input at any
    // time (Freeze, a held note, a 16 s read, an infinite reverb), is always infinite, and the
    // instrument's is its longest finite one, kInstrumentTailSeconds.
    if (product_ == Product::Fx)
    {
        return kTailInfinite;
    }
    return instrumentTailFrames(sampleRate_.load(std::memory_order_relaxed));
}

uint32_t GigaGrainsPlugin::instrumentTailFrames(double sampleRate) noexcept
{
    const double frames = std::ceil(kInstrumentTailSeconds * sampleRate);
    // A NaN fails both comparisons, so it saturates too.
    return frames < static_cast<double>(kTailInfinite) && frames >= 0.0
               ? static_cast<uint32_t>(frames)
               : kTailInfinite - 1;
}

bool GigaGrainsPlugin::toParamEvent(const clap_event_header *header, ParamEvent &event) noexcept
{
    if (header == nullptr || header->space_id != CLAP_CORE_EVENT_SPACE_ID)
    {
        return false;
    }
    clap_id id = CLAP_INVALID_ID;
    double value = 0.0;
    bool isModulation = false;
    if (header->type == CLAP_EVENT_PARAM_VALUE)
    {
        const auto *e = reinterpret_cast<const clap_event_param_value *>(header);
        id = e->param_id;
        value = e->value;
    }
    else if (header->type == CLAP_EVENT_PARAM_MOD)
    {
        const auto *e = reinterpret_cast<const clap_event_param_mod *>(header);
        id = e->param_id;
        value = e->amount;
        isModulation = true;
    }
    else
    {
        return false;
    }
    // Parameter values are floats end to end: the host's double is converted once, on receipt
    // (Global Constraints), so a saved and reloaded state renders identically.
    const auto v = static_cast<float>(value);
    const auto slot = gg::core::slotForId(id);
    if (!slot.has_value() || std::isnan(v))
    {
        return false;
    }
    event = ParamEvent{header->time, *slot, isModulation, v};
    return true;
}

void GigaGrainsPlugin::applyBetweenBlocks(const clap_input_events *in, uint32_t first) noexcept
{
    // Values apply at once, outside the control grid (Processor::setBase). Modulation, notes and
    // controls have no such setter: they go through a 0-frame process() call, which queues them
    // for the next grid boundary like those inside a block. events_, notes_ and controls_ are
    // free between process() calls.
    std::size_t mods = 0;
    std::size_t notes = 0;
    std::size_t controls = 0;
    const auto queue = [this, &mods, &notes, &controls] {
        processor_->process(nullptr, 0, nullptr, 0, 0,
                            std::span<const ParamEvent>(events_.data(), mods),
                            std::span<const NoteEvent>(notes_.data(), notes),
                            std::span<const ControlEvent>(controls_.data(), controls));
        mods = 0;
        notes = 0;
        controls = 0;
    };
    const uint32_t numEvents = in->size(in);
    for (uint32_t i = first; i < numEvents; ++i)
    {
        const clap_event_header *header = in->get(in, i);
        ParamEvent event{};
        NoteEvent note{};
        ControlEvent control{};
        if (toNoteEvent(header, note))
        {
            if (notes == kMaxNoteEvents)
            {
                queue();
            }
            note.offset = 0;
            notes_[notes++] = note;
            continue;
        }
        if (toControlEvent(header, control))
        {
            if (controls == kMaxControlEvents)
            {
                queue();
            }
            control.offset = 0;
            controls_[controls++] = control;
            continue;
        }
        if (!toParamEvent(header, event))
        {
            continue;
        }
        if (!event.isModulation)
        {
            const auto value = static_cast<float>(event.value);
            processor_->setBase(event.slot, value);
            storeValue(event.slot, value);
            continue;
        }
        if (mods == kMaxParamEvents)
        {
            queue();
        }
        event.offset = 0;
        events_[mods++] = event;
    }
    if (mods > 0 || notes > 0 || controls > 0)
    {
        queue();
    }
}

bool GigaGrainsPlugin::isNoteEvent(const clap_event_header *header) noexcept
{
    if (header == nullptr || header->space_id != CLAP_CORE_EVENT_SPACE_ID)
    {
        return false;
    }
    if (header->type == CLAP_EVENT_MIDI)
    {
        // Only what toNoteEvent converts, so other MIDI (the controls toControlEvent converts,
        // clock, other CCs) doesn't count toward kMaxNoteEvents.
        const auto *e = reinterpret_cast<const clap_event_midi *>(header);
        const auto status = static_cast<uint8_t>(e->data[0] & 0xF0);
        return status == 0x80 || status == 0x90 || (status == 0xB0 && e->data[1] == 123);
    }
    return header->type == CLAP_EVENT_NOTE_ON || header->type == CLAP_EVENT_NOTE_OFF ||
           header->type == CLAP_EVENT_NOTE_CHOKE;
}

bool GigaGrainsPlugin::isControlEvent(const clap_event_header *header) noexcept
{
    if (header == nullptr || header->space_id != CLAP_CORE_EVENT_SPACE_ID ||
        header->type != CLAP_EVENT_MIDI)
    {
        return false;
    }
    const auto *e = reinterpret_cast<const clap_event_midi *>(header);
    const auto status = static_cast<uint8_t>(e->data[0] & 0xF0);
    return (status == 0xB0 && e->data[1] == 1) || status == 0xD0 || status == 0xE0;
}

bool GigaGrainsPlugin::toControlEvent(const clap_event_header *header, ControlEvent &event) noexcept
{
    if (!isControlEvent(header))
    {
        return false;
    }
    const auto *e = reinterpret_cast<const clap_event_midi *>(header);
    const auto status = static_cast<uint8_t>(e->data[0] & 0xF0);
    const auto channel = static_cast<int16_t>(e->data[0] & 0x0F);
    const auto data1 = static_cast<uint32_t>(e->data[1] & 0x7F);
    const auto data2 = static_cast<uint32_t>(e->data[2] & 0x7F);
    if (status == 0xB0)
    {
        event = ControlEvent{header->time, ControlKind::ModWheel, channel,
                             static_cast<float>(data2) / 127.0f};
    }
    else if (status == 0xD0)
    {
        event = ControlEvent{header->time, ControlKind::Aftertouch, channel,
                             static_cast<float>(data1) / 127.0f};
    }
    else
    {
        event = ControlEvent{header->time, ControlKind::PitchBend, channel,
                             static_cast<float>((data2 << 7) | data1) / 16383.0f};
    }
    return true;
}

bool GigaGrainsPlugin::toNoteEvent(const clap_event_header *header, NoteEvent &event) noexcept
{
    if (!isNoteEvent(header))
    {
        return false;
    }
    if (header->type == CLAP_EVENT_NOTE_ON)
    {
        // Velocity reaches the mod matrix alone (the grains ignore it, D6), and the CLAP dialect
        // has no velocity-0 rule: every note-on is an On. Its ID, if any, is remembered for a
        // note-off by ID (ruling R15): in the slot it already has, or a free one, or else the
        // oldest one.
        const auto *e = reinterpret_cast<const clap_event_note *>(header);
        if (e->note_id >= 0 && e->channel >= 0 && e->channel <= 15 && e->key >= 0 && e->key <= 127)
        {
            NoteIdSlot *slot = &noteIds_[0];
            for (NoteIdSlot &s : noteIds_)
            {
                if (s.id == e->note_id)
                {
                    slot = &s;
                    break;
                }
                if (slot->id >= 0 && (s.id < 0 || s.age < slot->age))
                {
                    slot = &s;
                }
            }
            *slot = NoteIdSlot{e->note_id, e->channel, e->key, ++noteIdAge_};
        }
        event = NoteEvent{header->time, NoteEventType::On, e->channel, e->key,
                          static_cast<float>(e->velocity)};
        return true;
    }
    if (header->type == CLAP_EVENT_NOTE_OFF || header->type == CLAP_EVENT_NOTE_CHOKE)
    {
        // A choke is a note-off. Wildcards (−1) release every held note they match, except that
        // an off with both wildcards and a known ID releases that ID's note alone (ruling R15).
        // An unknown ID keeps the release of everything: safer than a stuck note.
        const auto *e = reinterpret_cast<const clap_event_note *>(header);
        int16_t channel = e->channel;
        int16_t key = e->key;
        if (e->note_id >= 0)
        {
            for (NoteIdSlot &s : noteIds_)
            {
                if (s.id == e->note_id)
                {
                    if (channel == -1 && key == -1)
                    {
                        channel = s.channel;
                        key = s.key;
                    }
                    s = NoteIdSlot{}; // the note has ended
                    break;
                }
            }
        }
        forgetNoteIds(channel, key);
        event = NoteEvent{header->time, NoteEventType::Off, channel, key};
        return true;
    }
    if (header->type == CLAP_EVENT_MIDI)
    {
        const auto *e = reinterpret_cast<const clap_event_midi *>(header);
        const auto status = static_cast<uint8_t>(e->data[0] & 0xF0);
        const auto channel = static_cast<int16_t>(e->data[0] & 0x0F);
        const auto key = static_cast<int16_t>(e->data[1]);
        if (status == 0x90)
        {
            // A note-on with velocity 0 is a note-off (MIDI 1.0).
            const auto type = e->data[2] > 0 ? NoteEventType::On : NoteEventType::Off;
            if (type == NoteEventType::Off)
            {
                forgetNoteIds(channel, key);
            }
            event = NoteEvent{header->time, type, channel, key,
                              static_cast<float>(e->data[2] & 0x7F) / 127.0f};
            return true;
        }
        if (status == 0x80)
        {
            forgetNoteIds(channel, key);
            event = NoteEvent{header->time, NoteEventType::Off, channel, key};
            return true;
        }
        if (status == 0xB0 && e->data[1] == 123)
        {
            // CC 123, all notes off: it releases the notes held on its channel (ruling R14).
            forgetNoteIds(channel, -1);
            event = NoteEvent{header->time, NoteEventType::AllOff, channel, -1};
            return true;
        }
    }
    return false;
}

void GigaGrainsPlugin::forgetNoteIds(int16_t channel, int16_t key) noexcept
{
    // The notes an off releases have ended, IDs and all: an ID left behind would take a slot,
    // and once the table filled, evict a held note's ID. −1 matches any channel or key.
    for (NoteIdSlot &s : noteIds_)
    {
        if (s.id >= 0 && (channel == -1 || s.channel == channel) && (key == -1 || s.key == key))
        {
            s = NoteIdSlot{};
        }
    }
}

void GigaGrainsPlugin::applyLoadedState() noexcept
{
    LoadedState loaded{};
    if (!loadedState_.take(loaded))
    {
        return;
    }
    // The grains scheduled from this call's first sample on are keyed by it (Engine::setSeed).
    processor_->setSeed(loaded.seed);
    processor_->setRoutes(loaded.routes);
    if (!loaded.hasValues)
    {
        return; // the instrument's come with its sample (takeOffers)
    }
    // One recomputation for the whole state on the audio thread, not one per slot (ruling FR-3).
    processor_->setBases(loaded.values);
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        storeValue(static_cast<ParamSlot>(s), loaded.values[s]);
    }
    requestValueRescan();
}

void GigaGrainsPlugin::storeValue(ParamSlot slot, float value) noexcept
{
    const auto s = static_cast<std::size_t>(slot);
    values_[s].store(std::clamp(value, minValues_[s], maxValues_[s]), std::memory_order_relaxed);
}

void GigaGrainsPlugin::uiBeginGesture(ParamSlot slot) noexcept
{
    dropPending(slot);
    queueUiChange({gg::core::paramId(slot), 0.0, UiParamChange::kBegin});
}

void GigaGrainsPlugin::uiSetValue(ParamSlot slot, double value) noexcept
{
    const auto s = static_cast<std::size_t>(slot);
    if (s >= kParamCount || std::isnan(value))
    {
        return;
    }
    dropPending(slot); // the user's latest move wins over a value still waiting unsent
    // Clamped as storeValue clamps, in double first (a double beyond float's range doesn't
    // convert), then made a float, as toParamEvent makes the host's: the host is told the value
    // the plugin applies, and a reloaded automation lane renders the same.
    const auto v = static_cast<float>(
        std::clamp(value, static_cast<double>(minValues_[s]), static_cast<double>(maxValues_[s])));
    queueUiChange({gg::core::paramId(slot), static_cast<double>(v), UiParamChange::kValue});
}

void GigaGrainsPlugin::uiEndGesture(ParamSlot slot) noexcept
{
    queueUiChange({gg::core::paramId(slot), 0.0, UiParamChange::kEnd});
}

float GigaGrainsPlugin::uiBaseValue(ParamSlot slot) const noexcept
{
    const auto s = static_cast<std::size_t>(slot);
    return s < kParamCount ? values_[s].load(std::memory_order_relaxed) : 0.0f;
}

void GigaGrainsPlugin::queueUiChange(const UiParamChange &change) noexcept
{
    if (change.id == CLAP_INVALID_ID || !uiQueue_.push(change))
    {
        return; // not a parameter, or the queue is full (SpscQueue::dropped counts it)
    }
    // Pairs with takeUiChanges' fence, as requestCollect's does with onMainThread's: the push
    // and the flag's exchange here, the flag's store and the queue's load there, are a
    // store-buffering pattern. With both fences, either that drain sees this change or this
    // exchange sees the flag cleared and asks again, so no change waits with no flush asked for.
    std::atomic_thread_fence(std::memory_order_seq_cst);
    // clap_host_params.request_flush is [thread-safe, !audio-thread]; this is the main thread.
    // One request outstanding: the host will call process() or flush, which drains every change
    // queued by then.
    if (_host.canUseParams() && !uiFlushRequested_.exchange(true, std::memory_order_acq_rel))
    {
        _host.paramsRequestFlush();
    }
}

std::size_t GigaGrainsPlugin::takeUiChanges(const clap_output_events *out, ParamEvent *values,
                                            std::size_t count) noexcept
{
    // Cleared first: a change queued after the drain asks for another flush (queueUiChange).
    // Release (T10-R2a): the main thread's acquire load of the cleared flag (mutateDrainSeen)
    // then sees everything this thread did before the clear, an earlier drain's pops and its
    // storeValues. Nothing else on this thread changes.
    uiFlushRequested_.store(false, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    // At most a queue's worth: the main thread may keep pushing while this runs, and the caller's
    // array holds kUiQueueCapacity values beyond the host's (kMaxParamEvents).
    UiParamChange change{};
    for (uint32_t n = 0; n < kUiQueueCapacity && uiQueue_.pop(change); ++n)
    {
        const auto slot = gg::core::slotForId(change.id);
        if (!slot.has_value())
        {
            continue; // uiBeginGesture and its siblings queue only parameters' IDs
        }
        bool sent = false;
        if (change.kind == UiParamChange::kValue)
        {
            const auto v = static_cast<float>(change.value); // a float's value already
            if (values != nullptr)
            {
                values[count++] = ParamEvent{0, *slot, false, v};
            }
            else
            {
                if (processor_ != nullptr)
                {
                    processor_->setBase(*slot, v);
                }
                storeValue(*slot, v);
            }
            clap_event_param_value e{};
            e.header = {sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
            e.param_id = change.id;
            e.cookie = nullptr;
            e.note_id = -1;
            e.port_index = -1;
            e.channel = -1;
            e.key = -1;
            e.value = change.value;
            sent = out != nullptr && out->try_push(out, &e.header);
        }
        else
        {
            clap_event_param_gesture e{};
            e.header = {sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID,
                        change.kind == UiParamChange::kBegin
                            ? static_cast<uint16_t>(CLAP_EVENT_PARAM_GESTURE_BEGIN)
                            : static_cast<uint16_t>(CLAP_EVENT_PARAM_GESTURE_END),
                        0};
            e.param_id = change.id;
            sent = out != nullptr && out->try_push(out, &e.header);
        }
        if (!sent)
        {
            // The host's list is full: the value applied all the same, but the host doesn't hear
            // of it. Counted, never retried: a retry here could spin for as long as the host's
            // list stays full.
            uiEventsDropped_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return count;
}

void GigaGrainsPlugin::setPublishing(bool on) noexcept
{
    publishing_ = on;
    if (processor_ != nullptr)
    {
        processor_->setPublishing(on);
    }
}

void GigaGrainsPlugin::paramsFlush(const clap_input_events *in,
                                   const clap_output_events *out) noexcept
{
    // CLAP puts only parameter events in a flush, but a host may deliver notes there too
    // (clap-wrapper's VST3 does), and those still count, and play.
    countNoteOns(in);
    if (processor_ == nullptr)
    {
        // Inactive, so on the main thread: the values wait for activate(). Modulation and notes
        // belong to a processing session, and the next one starts without any. The editor's
        // changes come first, as the host's would in process().
        takeUiChanges(out, nullptr, 0);
        const uint32_t numEvents = in != nullptr ? in->size(in) : 0;
        for (uint32_t i = 0; i < numEvents; ++i)
        {
            ParamEvent event{};
            if (toParamEvent(in->get(in, i), event) && !event.isModulation)
            {
                storeValue(event.slot, static_cast<float>(event.value));
            }
        }
        checkKinds();
        return;
    }
    // Active, so on the audio thread, between process() calls: notes and controls, like
    // modulation, join the next grid boundary.
    applyLoadedState();
    takeOffers();
    takeUiChanges(out, nullptr, 0);
    if (in != nullptr)
    {
        applyBetweenBlocks(in, 0);
    }
    requestCollect();
    checkKinds();
}

bool GigaGrainsPlugin::paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept
{
    if (paramIndex >= kParamCount || info == nullptr)
    {
        return false;
    }
    const auto slot = static_cast<ParamSlot>(paramIndex);
    *info = clap_param_info{};
    gg::core::paramMeta(slot, mode()).toClapParamInfo<CLAP_NAME_SIZE>(info);
    // "Layer A" to "Layer D" for a layer parameter, "" for a global (spec §6.9).
    std::snprintf(info->module, sizeof(info->module), "%s", gg::core::paramModule(slot));
    info->cookie = nullptr;
    if (hiddenIn(product_, slot))
    {
        info->flags |= CLAP_PARAM_IS_HIDDEN;
    }
    return true;
}

bool GigaGrainsPlugin::paramsValue(clap_id paramId, double *value) noexcept
{
    const auto slot = gg::core::slotForId(paramId);
    if (!slot.has_value() || value == nullptr)
    {
        return false;
    }
    *value = values_[static_cast<std::size_t>(*slot)].load(std::memory_order_relaxed);
    return true;
}

bool GigaGrainsPlugin::paramsValueToText(clap_id paramId, double value, char *display,
                                         uint32_t size) noexcept
{
    const auto slot = gg::core::slotForId(paramId);
    if (!slot.has_value() || display == nullptr || size == 0)
    {
        return false;
    }
    try
    {
        // Position's display follows its layer's kind (displayModeOf).
        const auto text = gg::core::paramMeta(*slot, displayModeOf(*slot))
                              .valueToString(static_cast<float>(value));
        if (!text.has_value())
        {
            return false;
        }
        std::snprintf(display, size, "%s", text->c_str());
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool GigaGrainsPlugin::paramsTextToValue(clap_id paramId, const char *display,
                                         double *value) noexcept
{
    const auto slot = gg::core::slotForId(paramId);
    if (!slot.has_value() || display == nullptr || value == nullptr)
    {
        return false;
    }
    try
    {
        std::string error;
        const auto parsed =
            gg::core::paramMeta(*slot, displayModeOf(*slot)).valueFromString(display, error);
        if (!parsed.has_value() || std::isnan(*parsed))
        {
            return false;
        }
        const auto s = static_cast<std::size_t>(*slot);
        *value = std::clamp(*parsed, minValues_[s], maxValues_[s]);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool GigaGrainsPlugin::stateSave(const clap_ostream *stream) noexcept
{
    if (stream == nullptr)
    {
        return false;
    }
    try
    {
        gg::core::ParamValues values{};
        for (std::size_t s = 0; s < kParamCount; ++s)
        {
            values[s] = values_[s].load(std::memory_order_relaxed);
        }
        // The session container (gg/core/SessionState.h), with the instrument's manifest entry
        // as it stands, whether the sample is loaded, loading, missing or failed (spec §6.6), or
        // a newer build's manifest as it came. The FX has no sample: an empty manifest. The
        // routes are those in effect (routes_).
        const std::string bytes = gg::core::encodeState(
            {values, seed_, loader_ != nullptr ? loader_->entry() : std::nullopt,
             loader_ != nullptr ? loader_->newerMedia() : std::nullopt, routes_, presetName_,
             std::nullopt}); // the preset's name, but a session: no <preset>

        // A stream may take fewer bytes than offered: write until all are in.
        const char *data = bytes.data();
        uint64_t left = bytes.size();
        while (left > 0)
        {
            const int64_t n = stream->write(stream, data, left);
            if (n <= 0 || static_cast<uint64_t>(n) > left)
            {
                return false; // -1 is an error, and a stream that takes nothing never finishes
            }
            data += n;
            left -= static_cast<uint64_t>(n);
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool GigaGrainsPlugin::stateLoad(const clap_istream *stream) noexcept
{
    if (stream == nullptr)
    {
        return false;
    }
    try
    {
        // A stream may return fewer bytes than asked for: read until it ends (0). A state is at
        // most kMaxStateBytes long, so read one byte past that at most, and fail there.
        std::string bytes;
        char buffer[4096];
        for (;;)
        {
            const uint64_t want =
                std::min<uint64_t>(sizeof(buffer), gg::core::kMaxStateBytes + 1 - bytes.size());
            const int64_t n = stream->read(stream, buffer, want);
            if (n < 0 || static_cast<uint64_t>(n) > want)
            {
                return false;
            }
            if (n == 0)
            {
                break;
            }
            bytes.append(buffer, static_cast<std::size_t>(n));
            if (bytes.size() > gg::core::kMaxStateBytes)
            {
                return false;
            }
        }

        // All or nothing: a state decodeState rejects changes nothing here either.
        gg::core::SessionState loaded;
        if (!gg::core::decodeState(bytes, mode(), loaded))
        {
            return false;
        }
        applyState(std::move(loaded), LoadKind::Session);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void GigaGrainsPlugin::applyState(gg::core::SessionState &&state, LoadKind kind)
{
    // The one allocation left before the state takes effect, made before anything changes:
    // room in the exchange for the offer below, which then can't throw, so a bad_alloc here
    // still changes nothing (all or nothing).
    if (loader_ != nullptr && processor_ != nullptr)
    {
        processor_->sources().reserve();
    }
    // The instrument's sample, found and decoded before this returns (D8); while active,
    // offered to the audio thread below in one hand-off with the values, so the process() that
    // takes them applies both. The sample already in memory, when it is the session's, is kept
    // and only the values are offered (ruling R19). A preset that names no sample keeps the one
    // loaded, entry and source, and reads nothing (D5): its values are offered alone, as R19's
    // are. The FX ignores a sample (a state from the instrument).
    // restore() never throws: a sample it can't load leaves its status saying why.
    const bool keepSample = kind == LoadKind::Preset && !state.sample.has_value();
    SampleLoader::Restored restored;
    if (loader_ != nullptr && !keepSample)
    {
        // content-relative references resolve here (Task 14)
        restored = loader_->restore(state.sample, std::move(state.newerMedia));
    }
    const auto &values = state.values;
    // A Mutate press cut short by this load (T10-R2b): the slots it had not sent are dropped,
    // and every batch it did send counts as applied, so the next press starts from these values.
    mutatePending_.reset();
    mutateSentGen_.fill(0);
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        storeValue(static_cast<ParamSlot>(s), values[s]);
        // As storeValue clamps it: what presetEdited compares values_ with.
        presetValues_[s] = std::clamp(values[s], minValues_[s], maxValues_[s]);
    }
    // stateSave writes them from now on: the seed and the routes the audio thread is about to
    // use, and the name. A preset keeps the instance's seed (D6): its own is ignored.
    if (kind == LoadKind::Session)
    {
        seed_ = state.seed;
    }
    routes_ = state.routes;
    presetName_ = std::move(state.name); // a move: nothing left to throw
    // Inactive, activate() applies the values, the seed and the routes. Active, the audio thread
    // applies them at the start of its next process() or params.flush (carry-over row 115): the
    // seed and the routes through loadedState_ in both products, the values with them in the FX
    // and with the sample in the instrument (D8). The instrument's two hand-offs are separate,
    // so its seed and routes may apply a block before or after its values.
    if (processor_ != nullptr)
    {
        if (loader_ != nullptr && (keepSample || restored.keep))
        {
            // The sample in memory stays (R19, D5): the values alone.
            processor_->sources().offerValues(values); // never allocates
        }
        else if (loader_ != nullptr)
        {
            processor_->sources().offer(std::move(restored.source), &values); // reserved
        }
        loadedState_.push({values, routes_, seed_, loader_ == nullptr});
    }
    else
    {
        checkKinds(); // inactive: the main thread owns values_
    }
    if (_host.canUseParams())
    {
        _host.paramsRescan(CLAP_PARAM_RESCAN_VALUES);
    }
    // A preset changed the plugin's state (its values, routes and name): the host should save it.
    if (kind == LoadKind::Preset && _host.canUseState())
    {
        _host.stateMarkDirty();
    }
}

// ---- Mutate (spec §6.9) ----
//
// The queue's room (T10-I2). uiQueue_ holds kUiQueueCapacity (1,024) entries and its producer,
// this thread, can't read how many wait, so Mutate bounds its own share by when it sends: a batch
// (at most kMutateBatch slots, 300 entries) goes out only once every earlier batch has been seen
// to start draining (mutateDrainSeen). A drain seen to begin after batch n's arm takes all of
// batch n; batch n - 1 went out only after an earlier drain was seen, which has ended by then.
// So at any moment at most two of Mutate's batches wait in the queue, batch n being drained and
// batch n + 1: 600 entries, and the other 424 are the editor's other changes'. Each batch is
// whole gestures, so no begin, value and end is ever split by the queue filling. When nothing
// drains (a host without clap.params while inactive, or one that never flushes), Mutate waits
// with its slots unsent: nothing is dropped.

void GigaGrainsPlugin::mutateDrainSeen() noexcept
{
    if (!mutateArmed_ || uiFlushRequested_.load(std::memory_order_acquire))
    {
        return;
    }
    // A drain began after the arm, so after the drain seen before it (which this thread saw
    // before it armed again) had ended: what that one took is applied, in values_.
    mutateArmed_ = false;
    mutateAppliedGen_ = mutateStartedGen_;
    mutateStartedGen_ = mutateArmedGen_;
    mutateBatchArmed_ = false;
}

void GigaGrainsPlugin::mutateArm() noexcept
{
    // Pairs with takeUiChanges' fence, as queueUiChange's does: a drain whose clearing of the
    // flag comes after this exchange sees every entry pushed before it.
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (!uiFlushRequested_.exchange(true, std::memory_order_acq_rel) && _host.canUseParams())
    {
        _host.paramsRequestFlush(); // [thread-safe, !audio-thread]; this is the main thread
    }
    mutateArmed_ = true;
    mutateArmedGen_ = mutateGen_;
}

gg::core::ParamValues GigaGrainsPlugin::mutateInput() const noexcept
{
    gg::core::ParamValues in{};
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        in[s] = mutatePending_.test(s) ? mutatePendingValue_[s]
                : mutateSentGen_[s] > mutateAppliedGen_
                    ? mutateSentValue_[s]
                    : values_[s].load(std::memory_order_relaxed);
    }
    return in;
}

void GigaGrainsPlugin::mutate(float amount) noexcept
{
    mutateDrainSeen();
    const gg::core::ParamValues in = mutateInput();
    // The off layers' parameters stay as they are however the locks stand: a layer that is off
    // now is not mutated, and one that was switched on since is.
    const gg::core::LockSet locks = mutateLocks_ | gg::core::offLayerLocks(in);
    gg::core::ParamValues out{};
    gg::core::mutate(in, locks, amount, seed_, mutateCounter_++, mode(), out);
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        if (out[s] != in[s])
        {
            // One value a slot: a slot still waiting from an earlier press takes this one's.
            queuePending(s, out[s]);
        }
    }
    flushMutate();
}

void GigaGrainsPlugin::dropPending(ParamSlot slot) noexcept
{
    const auto s = static_cast<std::size_t>(slot);
    if (s < kParamCount)
    {
        mutatePending_.reset(s);
    }
}

void GigaGrainsPlugin::queuePending(std::size_t slot, float value) noexcept
{
    mutatePending_.set(slot);
    mutatePendingValue_[slot] = value;
}

void GigaGrainsPlugin::sendValues(
    std::span<const std::pair<gg::core::ParamSlot, double>> values) noexcept
{
    for (const auto &[slot, value] : values)
    {
        const auto s = static_cast<std::size_t>(slot);
        if (s >= kParamCount || std::isnan(value))
        {
            continue;
        }
        // Clamped in double first (a double beyond float's range doesn't convert), then made a
        // float, as uiSetValue does, so a mutate press that starts from this value reads what
        // the audio thread will hold.
        queuePending(s, static_cast<float>(std::clamp(value, static_cast<double>(minValues_[s]),
                                                      static_cast<double>(maxValues_[s]))));
    }
    flushMutate();
}

void GigaGrainsPlugin::flushMutate() noexcept
{
    mutateDrainSeen();
    if (mutatePending_.any() && !mutateBatchArmed_)
    {
        ++mutateGen_;
        std::size_t sent = 0;
        for (std::size_t s = 0; s < kParamCount && sent < kMutateBatch; ++s)
        {
            if (!mutatePending_.test(s))
            {
                continue;
            }
            const auto slot = static_cast<ParamSlot>(s);
            const float value = mutatePendingValue_[s];
            uiBeginGesture(slot);
            uiSetValue(slot, static_cast<double>(value));
            uiEndGesture(slot);
            mutatePending_.reset(s);
            mutateSentValue_[s] = value;
            mutateSentGen_[s] = mutateGen_;
            ++sent;
        }
        mutateBatchArmed_ = true;
        mutateArm();
        requestMutateCallback();
        return;
    }
    // Nothing to send now: arm once more after the last batch's drain was seen, so the drain
    // after it shows that batch applied and the next press reads values_ again.
    if (!mutateArmed_ && mutateAppliedGen_ < mutateGen_)
    {
        mutateArm();
    }
    requestMutateCallback();
}

void GigaGrainsPlugin::requestMutateCallback() noexcept
{
    // Slots wait for a drain. With the editor closed nothing else ticks, and a host without
    // clap.params flushes only inside process: ask for on_main_thread, once until it comes, so
    // the rest is sent as the drains are seen (T10-R2b).
    if (mutatePending_.any() && !mutateCallbackRequested_)
    {
        mutateCallbackRequested_ = true;
        _host.requestCallback(); // [thread-safe]; this is the main thread
    }
}

bool GigaGrainsPlugin::mutateLocked(ParamSlot slot) const noexcept
{
    const auto s = static_cast<std::size_t>(slot);
    return s < kParamCount && mutateLocks_.test(s);
}

void GigaGrainsPlugin::setMutateLock(ParamSlot slot, bool locked) noexcept
{
    const auto s = static_cast<std::size_t>(slot);
    if (s < kParamCount)
    {
        mutateLocks_.set(s, locked);
    }
}

void GigaGrainsPlugin::resetMutateLocks() noexcept
{
    mutateLocks_ = gg::core::structuralLocks(mode());
}

bool GigaGrainsPlugin::presetEdited() const noexcept
{
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        if (std::bit_cast<uint32_t>(values_[s].load(std::memory_order_relaxed)) !=
            std::bit_cast<uint32_t>(presetValues_[s]))
        {
            return true;
        }
    }
    return false;
}

bool GigaGrainsPlugin::loadPresetFile(const std::filesystem::path &file, std::string &error,
                                      int32_t *osError)
{
    if (osError != nullptr)
    {
        *osError = 0;
    }
    const std::string name = gg::media::displayName(file);
    errno = 0;
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        if (osError != nullptr)
        {
            *osError = errno;
        }
        error = "can't read " + name;
        return false;
    }
    // At most one byte past kMaxStateBytes, as stateLoad reads a state: a longer file fails there.
    std::string bytes;
    char buffer[4096];
    for (;;)
    {
        const auto want =
            std::min<std::size_t>(sizeof(buffer), gg::core::kMaxStateBytes + 1 - bytes.size());
        in.read(buffer, static_cast<std::streamsize>(want));
        const auto got = static_cast<std::size_t>(in.gcount());
        bytes.append(buffer, got);
        if (bytes.size() > gg::core::kMaxStateBytes)
        {
            error = "the preset is larger than 64 MiB";
            return false;
        }
        if (got < want)
        {
            if (in.bad())
            {
                error = "can't read " + name;
                return false;
            }
            break; // the end of the file
        }
    }

    gg::core::PresetInfo info;
    gg::core::SessionState state;
    if (!gg::core::readPreset(bytes, mode(), info, state))
    {
        // A preset of this product that still doesn't load is damaged; anything else, a session
        // state, the other product's preset or another file, isn't one of ours.
        gg::core::PresetInfo found;
        if (gg::core::presetInfoOf(bytes, found) && found.product == mode())
        {
            error = "can't read " + name;
        }
        else
        {
            error = std::string("not a ") + descriptorFor(product_)->name + " preset";
        }
        return false;
    }
    applyState(std::move(state), LoadKind::Preset);
    return true;
}

void GigaGrainsPlugin::loadInit() { applyState(gg::core::initState(mode()), LoadKind::Preset); }

PresetCatalog &GigaGrainsPlugin::presetCatalog()
{
    if (catalog_ == nullptr)
    {
        catalog_ = std::make_unique<PresetCatalog>(mode(), catalogSettingsFile_);
    }
    return *catalog_;
}

std::string GigaGrainsPlugin::loadCatalogPreset(std::size_t index) noexcept
{
    try
    {
        const auto &entries = presetCatalog().entries();
        if (index >= entries.size())
        {
            return "no such preset";
        }
        const PresetEntry entry = entries[index]; // a copy: loading may not touch the catalog
        std::string error;
        uint32_t kind = CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN;
        std::string location;
        if (entry.kind == PresetKind::Init)
        {
            loadInit();
        }
        else
        {
            kind = CLAP_PRESET_DISCOVERY_LOCATION_FILE;
            const auto utf8 = gg::media::toUtf8(entry.file);
            if (!utf8 || !loadPresetFile(entry.file, error))
            {
                return error.empty() ? "can't read " + gg::media::displayName(entry.file) : error;
            }
            location = *utf8;
        }
        if (_host.canUsePresetLoad())
        {
            // The identity the host's own load would use: a file's location with no load key,
            // INIT's load key with no location (PresetDiscovery).
            const std::string key(gg::core::kInitLoadKey);
            _host.presetLoadLoaded(
                kind, kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE ? location.c_str() : nullptr,
                kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE ? nullptr : key.c_str());
        }
        return {};
    }
    catch (...)
    {
        return "out of memory";
    }
}

std::string GigaGrainsPlugin::savePreset(const std::string &name,
                                         const std::vector<std::string> &tags,
                                         const std::string &description, bool overwrite) noexcept
{
    try
    {
        gg::core::PresetInfo info;
        info.name = name;
        info.product = mode();
        info.description = description;
        for (const auto &tag : tags)
        {
            if (gg::core::isPresetTag(tag) &&
                std::find(info.tags.begin(), info.tags.end(), tag) == info.tags.end())
            {
                info.tags.push_back(tag);
            }
        }
        gg::core::ParamValues values{};
        for (std::size_t s = 0; s < kParamCount; ++s)
        {
            values[s] = values_[s].load(std::memory_order_relaxed);
        }
        const std::string bytes = gg::core::encodePreset(
            info, {values, 0, loader_ != nullptr ? loader_->entry() : std::nullopt,
                   loader_ != nullptr ? loader_->newerMedia() : std::nullopt, routes_, info.name,
                   std::nullopt});
        std::string error = presetCatalog().save(info, bytes, overwrite);
        if (!error.empty())
        {
            return error;
        }
        presetName_ = info.name;
        presetValues_ = values; // the saved sound is the loaded preset: not edited
        if (_host.canUseState())
        {
            _host.stateMarkDirty();
        }
        return {};
    }
    catch (...)
    {
        return "out of memory";
    }
}

std::string GigaGrainsPlugin::setContentFolder(const std::string &utf8Folder) noexcept
{
    // The settings writer cuts a string at its first NUL (gg/core/MachineSettings.h): refuse it
    // before anything is saved.
    if (utf8Folder.find('\0') != std::string::npos)
    {
        return "the content folder's path is not valid";
    }
    try
    {
        if (catalogSettingsFile_.empty())
        {
            return "can't save the content folder: there is no settings file";
        }
        auto settings = gg::media::loadSettings(catalogSettingsFile_);
        settings.contentFolder = utf8Folder;
        if (!gg::media::saveSettings(catalogSettingsFile_, settings))
        {
            return "can't save the content folder: the settings file can't be written";
        }
        return {};
    }
    catch (...)
    {
        return "can't save the content folder: not enough memory";
    }
}

bool GigaGrainsPlugin::presetLoadFromLocation(uint32_t locationKind, const char *location,
                                              const char *loadKey) noexcept
{
    bool loaded = false;
    bool outOfMemory = false;
    int32_t osError = 0;
    std::string error;
    try
    {
        if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_FILE && location != nullptr)
        {
            if (const auto file = gg::media::fromUtf8(location))
            {
                loaded = loadPresetFile(*file, error, &osError);
            }
            else
            {
                error = std::string("can't read ") + location; // not a path on this system
            }
        }
        else if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && loadKey != nullptr &&
                 loadKey == gg::core::kInitLoadKey)
        {
            loadInit();
            loaded = true;
        }
        else
        {
            error = "unknown preset location";
        }
    }
    catch (...)
    {
        loaded = false; // std::bad_alloc, before anything changed
        outOfMemory = true;
    }
    if (_host.canUsePresetLoad())
    {
        if (loaded)
        {
            _host.presetLoadLoaded(locationKind, location, loadKey);
        }
        else
        {
            _host.presetLoadOnError(locationKind, location, loadKey, osError,
                                    outOfMemory ? "out of memory" : error.c_str());
        }
    }
    return loaded;
}

uint32_t GigaGrainsPlugin::remoteControlsPageCount() noexcept
{
    return static_cast<uint32_t>(kRemotePages.size());
}

bool GigaGrainsPlugin::remoteControlsPageGet(uint32_t pageIndex,
                                             clap_remote_controls_page *page) noexcept
{
    if (pageIndex >= kRemotePages.size() || page == nullptr)
    {
        return false;
    }
    const RemotePage &source = kRemotePages[pageIndex];
    *page = clap_remote_controls_page{};
    copyName(page->section_name, source.name);
    copyName(page->page_name, source.name);
    page->page_id = pageIndex;
    page->is_for_preset = false;
    for (std::size_t i = 0; i < source.slots.size(); ++i)
    {
        // A hidden parameter's control stays empty (hiddenIn).
        const ParamSlot slot = source.slots[i];
        page->param_ids[i] = hiddenIn(product_, slot) ? CLAP_INVALID_ID : gg::core::paramId(slot);
    }
    return true;
}

void GigaGrainsPlugin::countNoteOns(const clap_input_events *events) noexcept
{
    if (events == nullptr)
    {
        return;
    }
    uint32_t count = 0;
    const uint32_t size = events->size(events);
    for (uint32_t i = 0; i < size; ++i)
    {
        const clap_event_header *e = events->get(events, i);
        if (e == nullptr || e->space_id != CLAP_CORE_EVENT_SPACE_ID)
        {
            continue;
        }
        if (e->type == CLAP_EVENT_NOTE_ON)
        {
            ++count;
        }
        else if (e->type == CLAP_EVENT_MIDI)
        {
            const auto *midi = reinterpret_cast<const clap_event_midi *>(e);
            // A note-on with velocity 0 is a note-off (MIDI 1.0).
            if ((midi->data[0] & 0xF0) == 0x90 && midi->data[2] > 0)
            {
                ++count;
            }
        }
    }
    if (count > 0)
    {
        noteOnCount.fetch_add(count, std::memory_order_relaxed);
    }
}

uint32_t GigaGrainsPlugin::audioPortsCount(bool /*isInput*/) const noexcept
{
    // One input and one output in both products: the FX's main input, the instrument's sidechain
    // (M2 sub-project 1's spec §6.4, D2).
    return 1;
}

bool GigaGrainsPlugin::audioPortsInfo(uint32_t index, bool isInput,
                                      clap_audio_port_info *info) const noexcept
{
    if (index >= audioPortsCount(isInput) || info == nullptr)
    {
        return false;
    }
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    if (product_ == Product::Instrument && isInput)
    {
        // The sidechain: not main (the instrument's main signal is its notes), and no in-place
        // pair, since it is no dry path to the output. ID 2, after the output's 0 and the FX's
        // ports' 0 and 1.
        info->id = 2;
        info->flags = 0;
        info->in_place_pair = CLAP_INVALID_ID;
        copyName(info->name, "Sidechain In");
    }
    else if (product_ == Product::Instrument)
    {
        info->id = 0;
        info->in_place_pair = CLAP_INVALID_ID;
        copyName(info->name, "Main Out");
    }
    else if (isInput)
    {
        info->id = 0;
        info->in_place_pair = 1;
        copyName(info->name, "Main In");
    }
    else
    {
        info->id = 1;
        info->in_place_pair = 0;
        copyName(info->name, "Main Out");
    }
    return true;
}

uint32_t GigaGrainsPlugin::notePortsCount(bool isInput) const noexcept { return isInput ? 1 : 0; }

bool GigaGrainsPlugin::notePortsInfo(uint32_t index, bool isInput,
                                     clap_note_port_info *info) const noexcept
{
    if (!isInput || index != 0)
    {
        return false;
    }
    info->id = 0;
    info->supported_dialects =
        CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    copyName(info->name, "Note In");
    return true;
}
} // namespace gg::plugin
