// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "GigaGrainsPlugin.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>

#include "gg/core/PatchState.h"
#include "gg/plugin/Identity.h"
#include "sst/basic-blocks/params/ParamMetadata.h"

namespace gg::plugin
{
namespace
{
using gg::core::kParamCount;
using gg::core::ParamEvent;
using gg::core::ParamSlot;

// The remote-controls page "Cloud" (spec §4), in slot order.
constexpr std::array<ParamSlot, CLAP_REMOTE_CONTROLS_COUNT> kCloudPage{
    ParamSlot::Position, ParamSlot::Size,   ParamSlot::Density, ParamSlot::Pitch,
    ParamSlot::Spray,    ParamSlot::Stereo, ParamSlot::Freeze,  ParamSlot::Mix,
};

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

void copyName(char (&dst)[CLAP_NAME_SIZE], const char *src) noexcept
{
    std::snprintf(dst, sizeof(dst), "%s", src);
}
} // namespace

const clap_plugin_descriptor *descriptorFor(Product product) noexcept
{
    return product == Product::Instrument ? &kInstrumentDescriptor : &kFxDescriptor;
}

GigaGrainsPlugin::GigaGrainsPlugin(const clap_host *host, Product product)
    : plugHelper_t(descriptorFor(product), host), product_(product)
{
    // Main thread. The first paramMeta() call builds its table, which allocates: it's made here,
    // never on the audio thread.
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        const auto &meta = gg::core::paramMeta(static_cast<ParamSlot>(s));
        minValues_[s] = meta.minVal;
        maxValues_[s] = meta.maxVal;
        storeValue(static_cast<ParamSlot>(s), meta.defaultVal);
    }
}

GigaGrainsPlugin::~GigaGrainsPlugin() = default;

bool GigaGrainsPlugin::activate(double sampleRate, uint32_t /*minFrameCount*/,
                                uint32_t maxFrameCount) noexcept
{
    // Main thread, never concurrent with process(): everything process() needs is allocated
    // here. The Processor throws on an invalid configuration; that fails the activation.
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
            product_ == Product::Fx ? gg::core::Mode::Fx : gg::core::Mode::Instrument,
        };
        auto processor = std::make_unique<gg::core::Processor>(config);
        // The values set while inactive, or left by the last activation.
        for (std::size_t s = 0; s < kParamCount; ++s)
        {
            processor->setBase(static_cast<ParamSlot>(s),
                               values_[s].load(std::memory_order_relaxed));
        }
        processor_ = std::move(processor);
        engineBlockSize_ = config.maxBlockSize;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void GigaGrainsPlugin::deactivate() noexcept
{
    // Main thread, with the audio thread stopped, so this may take loadedState_. A state loaded
    // while active that the audio thread never took is newer than the last block's events: it
    // goes into the values the next activate() starts from, as the next block would have
    // applied it. Otherwise it would apply, stale, at the first block after a reactivation.
    gg::core::ParamValues loaded{};
    if (loadedState_.take(loaded))
    {
        for (std::size_t s = 0; s < kParamCount; ++s)
        {
            storeValue(static_cast<ParamSlot>(s), loaded[s]);
        }
    }
    // The next activate() may bring a new sample rate: the processor doesn't survive into it.
    processor_.reset();
}

void GigaGrainsPlugin::reset() noexcept
{
    if (processor_ != nullptr)
    {
        processor_->reset();
    }
}

clap_process_status GigaGrainsPlugin::process(const clap_process *process) noexcept
{
    return processNonblocking(process);
}

clap_process_status GigaGrainsPlugin::processNonblocking(const clap_process *process) noexcept
{
    countNoteOns(process->in_events);
    if (processor_ == nullptr)
    {
        return CLAP_PROCESS_CONTINUE; // clap-helpers lets process() through only while active
    }
    applyLoadedState();

    // The call's parameter events, in the host's order (by time), as many as events_ holds.
    const clap_input_events *in = process->in_events;
    const uint32_t numEvents = in != nullptr ? in->size(in) : 0;
    std::size_t count = 0;
    uint32_t next = 0;
    for (; next < numEvents && count < kMaxParamEvents; ++next)
    {
        if (toParamEvent(in->get(in, next), events_[count]))
        {
            ++count;
        }
    }

    // The Processor takes care of absent ports and channels: a missing input is silence, a mono
    // one feeds both channels, and the instrument ignores its input.
    const clap_audio_buffer *input =
        process->audio_inputs_count > 0 ? process->audio_inputs : nullptr;
    const clap_audio_buffer *output =
        process->audio_outputs_count > 0 ? process->audio_outputs : nullptr;
    processor_->process(
        input != nullptr ? input->data32 : nullptr, input != nullptr ? input->channel_count : 0,
        output != nullptr ? output->data32 : nullptr, output != nullptr ? output->channel_count : 0,
        process->frames_count, std::span<const ParamEvent>(events_.data(), count));

    // The values just handed over, for the host to read. Not the Processor's base(): an event
    // waits there for its grid boundary, which may come in the next call (ruling M1-u).
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!events_[i].isModulation)
        {
            storeValue(events_[i].slot, static_cast<float>(events_[i].value));
        }
    }

    // An automation storm larger than events_: the rest applies at the block's end.
    if (next < numEvents)
    {
        applyBetweenBlocks(in, next);
    }
    return CLAP_PROCESS_CONTINUE;
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
    // Values apply at once, outside the control grid (Processor::setBase). Modulation has no
    // such setter: it goes through a 0-frame process() call, which queues it for the next grid
    // boundary like modulation inside a block. events_ is free between process() calls.
    std::size_t mods = 0;
    const auto queueMods = [this, &mods] {
        processor_->process(nullptr, 0, nullptr, 0, 0,
                            std::span<const ParamEvent>(events_.data(), mods));
        mods = 0;
    };
    const uint32_t numEvents = in->size(in);
    for (uint32_t i = first; i < numEvents; ++i)
    {
        ParamEvent event{};
        if (!toParamEvent(in->get(in, i), event))
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
            queueMods();
        }
        event.offset = 0;
        events_[mods++] = event;
    }
    if (mods > 0)
    {
        queueMods();
    }
}

void GigaGrainsPlugin::applyLoadedState() noexcept
{
    gg::core::ParamValues loaded{};
    if (!loadedState_.take(loaded))
    {
        return;
    }
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        processor_->setBase(static_cast<ParamSlot>(s), loaded[s]);
        storeValue(static_cast<ParamSlot>(s), loaded[s]);
    }
}

void GigaGrainsPlugin::storeValue(ParamSlot slot, float value) noexcept
{
    const auto s = static_cast<std::size_t>(slot);
    values_[s].store(std::clamp(value, minValues_[s], maxValues_[s]), std::memory_order_relaxed);
}

void GigaGrainsPlugin::paramsFlush(const clap_input_events *in,
                                   const clap_output_events * /*out*/) noexcept
{
    // CLAP puts only parameter events in a flush, but a host may deliver notes there too
    // (clap-wrapper's VST3 does), and those still count.
    countNoteOns(in);
    if (processor_ == nullptr)
    {
        // Inactive, so on the main thread: the values wait for activate(). Modulation belongs
        // to a processing session, and the next one starts without any.
        const uint32_t numEvents = in != nullptr ? in->size(in) : 0;
        for (uint32_t i = 0; i < numEvents; ++i)
        {
            ParamEvent event{};
            if (toParamEvent(in->get(in, i), event) && !event.isModulation)
            {
                storeValue(event.slot, static_cast<float>(event.value));
            }
        }
        return;
    }
    // Active, so on the audio thread, between process() calls.
    applyLoadedState();
    if (in != nullptr)
    {
        applyBetweenBlocks(in, 0);
    }
}

bool GigaGrainsPlugin::paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept
{
    if (paramIndex >= kParamCount || info == nullptr)
    {
        return false;
    }
    const auto slot = static_cast<ParamSlot>(paramIndex);
    *info = clap_param_info{};
    gg::core::paramMeta(slot).toClapParamInfo<CLAP_NAME_SIZE>(info);
    info->cookie = nullptr;
    // The instrument has no dry signal, so Mix does nothing there. It keeps the parameter,
    // hidden, so both products share one parameter list and one state format (spec §4).
    if (product_ == Product::Instrument && slot == ParamSlot::Mix)
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
        const auto text = gg::core::paramMeta(*slot).valueToString(static_cast<float>(value));
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
        const auto parsed = gg::core::paramMeta(*slot).valueFromString(display, error);
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
        const std::string xml = gg::core::toStateXml(values, seed_);

        // A stream may take fewer bytes than offered: write until all are in.
        const char *data = xml.data();
        uint64_t left = xml.size();
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
        // A stream may return fewer bytes than asked for: read until it ends (0).
        std::string xml;
        char buffer[4096];
        for (;;)
        {
            const int64_t n = stream->read(stream, buffer, sizeof(buffer));
            if (n < 0 || static_cast<uint64_t>(n) > sizeof(buffer))
            {
                return false;
            }
            if (n == 0)
            {
                break;
            }
            xml.append(buffer, static_cast<std::size_t>(n));
        }

        // All or nothing: a state fromStateXml rejects changes nothing here either.
        gg::core::ParamValues values{};
        uint64_t seed = 0;
        if (!gg::core::fromStateXml(xml, values, seed))
        {
            return false;
        }
        for (std::size_t s = 0; s < kParamCount; ++s)
        {
            storeValue(static_cast<ParamSlot>(s), values[s]);
        }
        const bool newSeed = seed != seed_;
        seed_ = seed;
        // Inactive, activate() applies the values and the seed. Active, the audio thread applies
        // the values at the start of its next process() or params.flush; its engine took its
        // seed when it was built, so a new seed needs a new activation, which the host makes on
        // a restart request.
        if (processor_ != nullptr)
        {
            loadedState_.push(values);
            if (newSeed)
            {
                _host.requestRestart();
            }
        }
        if (_host.canUseParams())
        {
            _host.paramsRescan(CLAP_PARAM_RESCAN_VALUES);
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool GigaGrainsPlugin::remoteControlsPageGet(uint32_t pageIndex,
                                             clap_remote_controls_page *page) noexcept
{
    if (pageIndex != 0 || page == nullptr)
    {
        return false;
    }
    *page = clap_remote_controls_page{};
    copyName(page->section_name, "Cloud");
    copyName(page->page_name, "Cloud");
    page->page_id = 0;
    page->is_for_preset = false;
    for (std::size_t i = 0; i < kCloudPage.size(); ++i)
    {
        // Mix is hidden in the instrument (see paramsInfo), so its control stays empty there.
        const bool hidden = product_ == Product::Instrument && kCloudPage[i] == ParamSlot::Mix;
        page->param_ids[i] = hidden ? CLAP_INVALID_ID : gg::core::paramId(kCloudPage[i]);
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

uint32_t GigaGrainsPlugin::audioPortsCount(bool isInput) const noexcept
{
    if (isInput)
    {
        return product_ == Product::Fx ? 1 : 0;
    }
    return 1;
}

bool GigaGrainsPlugin::audioPortsInfo(uint32_t index, bool isInput,
                                      clap_audio_port_info *info) const noexcept
{
    if (index != 0 || index >= audioPortsCount(isInput))
    {
        return false;
    }
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    if (product_ == Product::Instrument)
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
