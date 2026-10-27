// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "GigaGrainsPlugin.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

// createEditor() returns a std::unique_ptr<juce::Component>, whose destructor the return
// statement may invoke, so juce::Component must be complete here.
#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/engine/Engine.h"
#include "gg/plugin/Identity.h"
#include "gg/ui/Editor.h"

namespace gg::plugin
{
namespace
{
// The FX renders the engine in chunks of at most this many frames, so its scratch buffer stays
// small whatever maxFrameCount the host announces. The engine's output does not depend on how
// a render is split into blocks (INTENT §8.6).
constexpr uint32_t kMaxScratchFrames = 1024;

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
    // As Shortcircuit XT does. Creating the shim needs no window and no message loop: the shim
    // starts JUCE's GUI in guiCreate, so a plugin that never shows its editor (or a headless
    // test) never does.
    clapJuceShim = std::make_unique<sst::clap_juce_shim::ClapJuceShim>(this);
    clapJuceShim->setResizable(true);
}

GigaGrainsPlugin::~GigaGrainsPlugin() = default;

std::unique_ptr<juce::Component> GigaGrainsPlugin::createEditor()
{
    return gg::ui::makeEditor({descriptorFor(product_)->name, &noteOnCount});
}

bool GigaGrainsPlugin::registerOrUnregisterTimer(clap_id &id, int ms, bool reg)
{
    if (!_host.canUseTimerSupport())
    {
        return false;
    }
    if (reg)
    {
        return _host.timerSupportRegister(static_cast<uint32_t>(ms), &id);
    }
    return _host.timerSupportUnregister(id);
}

bool GigaGrainsPlugin::registerOrUnregisterPosixFd(int fd, clap_posix_fd_flags_t flags, bool reg)
{
    if (!_host.canUsePosixFdSupport())
    {
        return false;
    }
    if (reg)
    {
        return _host.posixFdSupportRegister(fd, flags);
    }
    return _host.posixFdSupportUnregister(fd);
}

bool GigaGrainsPlugin::activate(double sampleRate, uint32_t /*minFrameCount*/,
                                uint32_t maxFrameCount) noexcept
{
    // Main thread, never concurrent with process(): everything process() needs is allocated
    // here. Engine throws on an invalid configuration; that fails the activation.
    try
    {
        EngineConfig config;
        config.sampleRate = sampleRate;
        config.maxBlockSize = std::max<uint32_t>(maxFrameCount, 1);
        auto engine = std::make_unique<gg::Engine>(config);

        std::vector<float> scratch;
        uint32_t scratchFrames = 0;
        if (product_ == Product::Fx)
        {
            scratchFrames = std::clamp<uint32_t>(maxFrameCount, 1, kMaxScratchFrames);
            scratch.assign(2 * static_cast<std::size_t>(scratchFrames), 0.0f);
        }

        engine_ = std::move(engine);
        scratch_ = std::move(scratch);
        scratchFrames_ = scratchFrames;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void GigaGrainsPlugin::deactivate() noexcept
{
    // The next activate() may bring a new sample rate: nothing survives into it.
    engine_.reset();
    std::vector<float>().swap(scratch_);
    scratchFrames_ = 0;
}

void GigaGrainsPlugin::reset() noexcept
{
    if (engine_ != nullptr)
    {
        engine_->reset();
    }
}

clap_process_status GigaGrainsPlugin::process(const clap_process *process) noexcept
{
    return processNonblocking(process);
}

clap_process_status GigaGrainsPlugin::processNonblocking(const clap_process *process) noexcept
{
    countNoteOns(process->in_events);

    const uint32_t frames = process->frames_count;
    if (frames == 0 || engine_ == nullptr || process->audio_outputs_count == 0 ||
        process->audio_outputs == nullptr || process->audio_outputs[0].data32 == nullptr)
    {
        return CLAP_PROCESS_CONTINUE;
    }

    float *const *out = process->audio_outputs[0].data32;
    const uint32_t outChannels = process->audio_outputs[0].channel_count;

    if (product_ == Product::Instrument)
    {
        engine_->process(out, outChannels, frames);
        return CLAP_PROCESS_CONTINUE;
    }

    // FX: the dry input first. The host may hand the same buffers in and out (in-place), in
    // which case the dry signal is already where it belongs.
    const clap_audio_buffer *in = process->audio_inputs_count > 0 ? process->audio_inputs : nullptr;
    for (uint32_t c = 0; c < outChannels; ++c)
    {
        const float *src = (in != nullptr && in->data32 != nullptr && c < in->channel_count)
                               ? in->data32[c]
                               : nullptr;
        if (src == nullptr)
        {
            std::memset(out[c], 0, frames * sizeof(float));
        }
        else if (src != out[c])
        {
            std::memmove(out[c], src, frames * sizeof(float));
        }
    }

    addEngineOutput(out, std::min<uint32_t>(outChannels, 2), frames);
    return CLAP_PROCESS_CONTINUE;
}

void GigaGrainsPlugin::addEngineOutput(float *const *out, uint32_t numChannels,
                                       uint32_t numFrames) noexcept
{
    if (scratchFrames_ == 0)
    {
        return;
    }
    float *const wet[2] = {scratch_.data(), scratch_.data() + scratchFrames_};
    for (uint32_t done = 0; done < numFrames;)
    {
        const uint32_t n = std::min(numFrames - done, scratchFrames_);
        engine_->process(wet, numChannels, n);
        for (uint32_t c = 0; c < numChannels; ++c)
        {
            float *dst = out[c] + done;
            for (uint32_t i = 0; i < n; ++i)
            {
                dst[i] += wet[c][i];
            }
        }
        done += n;
    }
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
