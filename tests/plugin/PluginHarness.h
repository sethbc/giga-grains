// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_TESTS_PLUGIN_PLUGINHARNESS_H
#define GG_TESTS_PLUGIN_PLUGINHARNESS_H

// A stub CLAP host and a small driver for the plugin tests. The plugins are exercised through
// gg-plugin-impl's factory and their clap_plugin vtables, exactly as a host would call them,
// without loading the built .clap.

#include <cstdint>
#include <vector>

#include <clap/clap.h>

#include "Factory.h"
#include "GigaGrainsPlugin.h"

namespace gg::test
{
/// A minimal clap_host: it offers no extensions, and every request is a no-op.
inline const clap_host *stubHost() noexcept
{
    static const clap_host host{
        CLAP_VERSION_INIT,
        nullptr,
        "gg-tests stub host",
        "Giga Grains",
        "",
        "0.0.0",
        [](const clap_host *, const char *) -> const void * { return nullptr; },
        [](const clap_host *) {}, // request_restart
        [](const clap_host *) {}, // request_process
        [](const clap_host *) {}, // request_callback
    };
    return &host;
}

inline const clap_plugin_factory *pluginFactory() noexcept
{
    return static_cast<const clap_plugin_factory *>(gg::plugin::getFactory(CLAP_PLUGIN_FACTORY_ID));
}

/// A clap_input_events over caller-owned events, in the order given.
class InputEvents
{
  public:
    InputEvents() = default;
    InputEvents(const InputEvents &) = delete;
    InputEvents &operator=(const InputEvents &) = delete;

    void add(const clap_event_header *event) { events_.push_back(event); }
    const clap_input_events *list() const noexcept { return &list_; }

  private:
    static uint32_t size(const clap_input_events *list) noexcept
    {
        return static_cast<uint32_t>(self(list).events_.size());
    }
    static const clap_event_header *get(const clap_input_events *list, uint32_t index) noexcept
    {
        const auto &events = self(list).events_;
        return index < events.size() ? events[index] : nullptr;
    }
    static const InputEvents &self(const clap_input_events *list) noexcept
    {
        return *static_cast<const InputEvents *>(list->ctx);
    }

    std::vector<const clap_event_header *> events_;
    clap_input_events list_{this, &InputEvents::size, &InputEvents::get};
};

/// A clap_output_events that accepts and discards everything.
inline const clap_output_events *discardingOutputEvents() noexcept
{
    static const clap_output_events out{
        nullptr, [](const clap_output_events *, const clap_event_header *) { return true; }};
    return &out;
}

/// One plugin instance, created through the factory and initialized. The destructor stops
/// processing, deactivates and destroys it, in that order. `host` must outlive the instance.
class PluginUnderTest
{
  public:
    explicit PluginUnderTest(const char *pluginId, const clap_host *host = stubHost())
    {
        const auto *factory = pluginFactory();
        if (factory != nullptr)
        {
            plugin_ = factory->create_plugin(factory, host, pluginId);
        }
        if (plugin_ != nullptr && !plugin_->init(plugin_))
        {
            plugin_->destroy(plugin_);
            plugin_ = nullptr;
        }
    }
    PluginUnderTest(const PluginUnderTest &) = delete;
    PluginUnderTest &operator=(const PluginUnderTest &) = delete;
    ~PluginUnderTest()
    {
        if (plugin_ == nullptr)
        {
            return;
        }
        deactivate();
        plugin_->destroy(plugin_);
    }

    bool valid() const noexcept { return plugin_ != nullptr; }
    const clap_plugin *clap() const noexcept { return plugin_; }
    gg::plugin::GigaGrainsPlugin &impl() const noexcept
    {
        return *gg::plugin::GigaGrainsPlugin::fromClap(plugin_);
    }

    /// Activates (main thread) and starts processing (audio thread).
    bool activate(double sampleRate, uint32_t maxFrames)
    {
        if (!plugin_->activate(plugin_, sampleRate, 1, maxFrames))
        {
            return false;
        }
        active_ = true;
        processing_ = plugin_->start_processing(plugin_);
        return processing_;
    }

    void deactivate()
    {
        if (processing_)
        {
            plugin_->stop_processing(plugin_);
            processing_ = false;
        }
        if (active_)
        {
            plugin_->deactivate(plugin_);
            active_ = false;
        }
    }

    /// Runs one process() call. `inputs` and `outputs` hold one stereo port's channel
    /// pointers each; a null pointer means the port is absent from the call.
    clap_process_status process(uint32_t frames, float **inputs, float **outputs,
                                const clap_input_events *events = nullptr)
    {
        InputEvents none;
        clap_audio_buffer in{inputs, nullptr, 2, 0, 0};
        clap_audio_buffer out{outputs, nullptr, 2, 0, 0};
        clap_process process{};
        process.steady_time = -1;
        process.frames_count = frames;
        process.transport = nullptr;
        process.audio_inputs = inputs != nullptr ? &in : nullptr;
        process.audio_inputs_count = inputs != nullptr ? 1 : 0;
        process.audio_outputs = outputs != nullptr ? &out : nullptr;
        process.audio_outputs_count = outputs != nullptr ? 1 : 0;
        process.in_events = events != nullptr ? events : none.list();
        process.out_events = discardingOutputEvents();
        return plugin_->process(plugin_, &process);
    }

  private:
    const clap_plugin *plugin_{nullptr};
    bool active_{false};
    bool processing_{false};
};
} // namespace gg::test

#endif // GG_TESTS_PLUGIN_PLUGINHARNESS_H
