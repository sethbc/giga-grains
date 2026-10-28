// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_TESTS_PLUGIN_PLUGINHARNESS_H
#define GG_TESTS_PLUGIN_PLUGINHARNESS_H

// A stub CLAP host and a small driver for the plugin tests, with the events, state streams and
// audio buffers they share. The plugins are exercised through gg::plugin-impl's factory and
// their clap_plugin vtables, exactly as a host would call them, without loading the built .clap.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include <clap/clap.h>

#include "Factory.h"
#include "GigaGrainsPlugin.h"
#include "gg/core/Rng.h"

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

/// A CLAP_EVENT_PARAM_VALUE for parameter `id`, global (no note, port, channel or key).
inline clap_event_param_value paramValue(clap_id id, double value, uint32_t time = 0) noexcept
{
    clap_event_param_value e{};
    e.header.size = sizeof(e);
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = CLAP_EVENT_PARAM_VALUE;
    e.param_id = id;
    e.note_id = -1;
    e.port_index = -1;
    e.channel = -1;
    e.key = -1;
    e.value = value;
    return e;
}

/// A CLAP_EVENT_PARAM_MOD for parameter `id`, global, as paramValue().
inline clap_event_param_mod paramMod(clap_id id, double amount, uint32_t time = 0) noexcept
{
    clap_event_param_mod e{};
    e.header.size = sizeof(e);
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = CLAP_EVENT_PARAM_MOD;
    e.param_id = id;
    e.note_id = -1;
    e.port_index = -1;
    e.channel = -1;
    e.key = -1;
    e.amount = amount;
    return e;
}

/// A CLAP note event of `type` (CLAP_EVENT_NOTE_ON, _OFF or _CHOKE) on port 0, with no note ID.
inline clap_event_note noteEvent(uint16_t type, int16_t channel, int16_t key,
                                 uint32_t time = 0) noexcept
{
    clap_event_note e{};
    e.header.size = sizeof(e);
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = type;
    e.note_id = -1;
    e.port_index = 0;
    e.channel = channel;
    e.key = key;
    e.velocity = 0.8;
    return e;
}

/// A CLAP_EVENT_MIDI with the three bytes given, on port 0.
inline clap_event_midi midiEvent(uint8_t status, uint8_t data1, uint8_t data2,
                                 uint32_t time = 0) noexcept
{
    clap_event_midi e{};
    e.header.size = sizeof(e);
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = CLAP_EVENT_MIDI;
    e.port_index = 0;
    e.data[0] = status;
    e.data[1] = data1;
    e.data[2] = data2;
    return e;
}

/// A 4 s stereo sine at 220 Hz and amplitude 0.5, at 48 kHz: a source for the instrument
/// (GigaGrainsPlugin::setSourceForTest).
inline std::shared_ptr<const gg::PreparedSource> sineSource()
{
    constexpr int64_t kFrames = 4 * 48000;
    auto buffer = std::make_shared<gg::SampleBuffer>(2, kFrames, 48000.0);
    for (uint32_t c = 0; c < 2; ++c)
    {
        for (int64_t i = 0; i < kFrames; ++i)
        {
            buffer->channel(c)[i] = static_cast<float>(
                0.5 * std::sin(2.0 * std::numbers::pi * 220.0 * static_cast<double>(i) / 48000.0));
        }
    }
    return gg::PreparedSource::make(std::move(buffer));
}

/// A clap_ostream into memory that accepts at most `chunk` bytes per write, as a host's stream
/// may; with failAfter set, every write once that many bytes are in fails (-1).
class MemoryOStream
{
  public:
    explicit MemoryOStream(std::size_t chunk = SIZE_MAX, std::size_t failAfter = SIZE_MAX)
        : chunk_(chunk), failAfter_(failAfter)
    {
    }
    MemoryOStream(const MemoryOStream &) = delete;
    MemoryOStream &operator=(const MemoryOStream &) = delete;

    const clap_ostream *stream() const noexcept { return &stream_; }
    const std::string &data() const noexcept { return data_; }

  private:
    static int64_t write(const clap_ostream *stream, const void *buffer, uint64_t size) noexcept
    {
        auto &self = *static_cast<MemoryOStream *>(stream->ctx);
        if (self.data_.size() >= self.failAfter_)
        {
            return -1;
        }
        const auto n = static_cast<std::size_t>(std::min<uint64_t>(size, self.chunk_));
        self.data_.append(static_cast<const char *>(buffer), n);
        return static_cast<int64_t>(n);
    }

    std::size_t chunk_;
    std::size_t failAfter_;
    std::string data_;
    clap_ostream stream_{this, &MemoryOStream::write};
};

/// A clap_istream over `data` that returns at most `chunk` bytes per read, as a host's stream
/// may; with failAfter set, every read once that many bytes are out fails (-1).
class MemoryIStream
{
  public:
    explicit MemoryIStream(std::string data, std::size_t chunk = SIZE_MAX,
                           std::size_t failAfter = SIZE_MAX)
        : data_(std::move(data)), chunk_(chunk), failAfter_(failAfter)
    {
    }
    MemoryIStream(const MemoryIStream &) = delete;
    MemoryIStream &operator=(const MemoryIStream &) = delete;

    const clap_istream *stream() const noexcept { return &stream_; }

  private:
    static int64_t read(const clap_istream *stream, void *buffer, uint64_t size) noexcept
    {
        auto &self = *static_cast<MemoryIStream *>(stream->ctx);
        if (self.pos_ >= self.failAfter_)
        {
            return -1;
        }
        const auto n = static_cast<std::size_t>(
            std::min<uint64_t>({size, self.chunk_, self.data_.size() - self.pos_}));
        std::memcpy(buffer, self.data_.data() + self.pos_, n);
        self.pos_ += n;
        return static_cast<int64_t>(n);
    }

    std::string data_;
    std::size_t chunk_;
    std::size_t failAfter_;
    std::size_t pos_{0};
    clap_istream stream_{this, &MemoryIStream::read};
};

/// A stereo buffer: two channels of `frames` samples each.
struct Stereo
{
    explicit Stereo(uint32_t frames, float fill = 0.0f)
        : left(frames, fill), right(frames, fill), ptrs{left.data(), right.data()}
    {
    }
    Stereo(const Stereo &) = delete;
    Stereo &operator=(const Stereo &) = delete;

    float **data() noexcept { return ptrs; }

    std::vector<float> left;
    std::vector<float> right;
    float *ptrs[2];
};

/// Deterministic noise in [-1, 1): never -0.0, so "bit-exact" is meaningful for x + 0.0f.
inline void fillNoise(Stereo &s, uint64_t seed)
{
    for (std::size_t i = 0; i < s.left.size(); ++i)
    {
        s.left[i] = static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(seed + 2 * i)));
        s.right[i] = static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(seed + 2 * i + 1)));
    }
}

inline bool sameBits(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
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
    const clap_plugin_params *params() const noexcept
    {
        return static_cast<const clap_plugin_params *>(
            plugin_->get_extension(plugin_, CLAP_EXT_PARAMS));
    }
    const clap_plugin_state *state() const noexcept
    {
        return static_cast<const clap_plugin_state *>(
            plugin_->get_extension(plugin_, CLAP_EXT_STATE));
    }

    /// params.flush of `events`, through the vtable.
    void flush(const clap_input_events *events) const
    {
        params()->flush(plugin_, events, discardingOutputEvents());
    }
    /// params.get_value; NaN if the call fails.
    double value(clap_id id) const
    {
        double v = 0.0;
        return params()->get_value(plugin_, id, &v) ? v : std::numeric_limits<double>::quiet_NaN();
    }
    /// state.save into a string; empty if the call fails.
    std::string save() const
    {
        MemoryOStream out;
        return state()->save(plugin_, out.stream()) ? out.data() : std::string();
    }
    bool load(const std::string &data) const
    {
        MemoryIStream in(data);
        return state()->load(plugin_, in.stream());
    }
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

    /// Runs one process() call. `inputs` and `outputs` hold one port's channel pointers each
    /// (`inputChannels` and 2 of them); a null pointer means the port is absent from the call.
    clap_process_status process(uint32_t frames, float **inputs, float **outputs,
                                const clap_input_events *events = nullptr,
                                uint32_t inputChannels = 2)
    {
        InputEvents none;
        clap_audio_buffer in{inputs, nullptr, inputChannels, 0, 0};
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
