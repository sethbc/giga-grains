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
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include <clap/clap.h>

#include "Factory.h"
#include "GigaGrainsPlugin.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Rng.h"
#include "gg/core/SessionState.h"
#include "gg/media/Sample.h"
#include "media/AudioFixtures.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "support/StubPool.h"

namespace gg::test
{
/// Points the plugins' per-machine settings file at `file`: GG_SETTINGS_FILE, which an instance
/// reads when it is created (GigaGrainsPlugin). Call it before creating the instance.
inline void useSettingsFile(const std::filesystem::path &file)
{
#if defined(_WIN32)
    _wputenv_s(L"GG_SETTINGS_FILE", file.c_str());
#else
    setenv("GG_SETTINGS_FILE", file.c_str(), 1);
#endif
}

/// Every test process starts with the scratch settings file below, so no test reads or writes
/// the user's (ruling R16). It is one path, shared by every process: a test that reads or writes
/// the settings points at a file of its own (useSettingsFile) first.
inline const bool kScratchSettings = [] {
    useSettingsFile(std::filesystem::path(GG_TEST_OUTPUT_DIR) / "plugin-settings" / "settings.xml");
    return true;
}();

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

/// A clap_output_events that records what the plugin pushes: the parameter value and gesture
/// events in full, any other event by its header alone. With `capacity`, it accepts that many
/// pushes and refuses (try_push returns false) every one after, as a host's full queue may. The
/// storage is reserved up front, so a push into it doesn't allocate (the [rt] tests).
class OutputEvents
{
  public:
    struct Event
    {
        clap_event_header header{};
        clap_id paramId{CLAP_INVALID_ID}; ///< the value's and the gesture's
        double value{0.0};                ///< CLAP_EVENT_PARAM_VALUE's
        void *cookie{nullptr};            ///< CLAP_EVENT_PARAM_VALUE's
        int32_t noteId{0};                ///< CLAP_EVENT_PARAM_VALUE's
        int16_t portIndex{0};
        int16_t channel{0};
        int16_t key{0};
    };

    explicit OutputEvents(std::size_t capacity = 4096) : capacity_(capacity)
    {
        events_.reserve(capacity);
    }
    OutputEvents(const OutputEvents &) = delete;
    OutputEvents &operator=(const OutputEvents &) = delete;

    const clap_output_events *list() const noexcept { return &list_; }
    const std::vector<Event> &events() const noexcept { return events_; }
    /// The pushes refused because the capacity was reached.
    std::size_t refused() const noexcept { return refused_; }
    void clear() noexcept { events_.clear(); }

  private:
    static bool tryPush(const clap_output_events *list, const clap_event_header *header) noexcept
    {
        auto &self = *static_cast<OutputEvents *>(list->ctx);
        if (header == nullptr || self.events_.size() >= self.capacity_)
        {
            ++self.refused_;
            return false;
        }
        Event e;
        e.header = *header;
        if (header->space_id == CLAP_CORE_EVENT_SPACE_ID && header->type == CLAP_EVENT_PARAM_VALUE)
        {
            const auto *v = reinterpret_cast<const clap_event_param_value *>(header);
            e.paramId = v->param_id;
            e.value = v->value;
            e.cookie = v->cookie;
            e.noteId = v->note_id;
            e.portIndex = v->port_index;
            e.channel = v->channel;
            e.key = v->key;
        }
        else if (header->space_id == CLAP_CORE_EVENT_SPACE_ID &&
                 (header->type == CLAP_EVENT_PARAM_GESTURE_BEGIN ||
                  header->type == CLAP_EVENT_PARAM_GESTURE_END))
        {
            e.paramId = reinterpret_cast<const clap_event_param_gesture *>(header)->param_id;
        }
        self.events_.push_back(e); // within the reserved capacity: no allocation
        return true;
    }

    std::size_t capacity_;
    std::size_t refused_{0};
    std::vector<Event> events_;
    clap_output_events list_{this, &OutputEvents::tryPush};
};

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

/// A clap_event_transport at time 0: the beats timeline at `beats` (in beats), `tempo` BPM, playing
/// or stopped, in `tsigNum`/4. The seconds timeline, the loop and the bar fields are absent (0).
inline clap_event_transport transportEvent(double tempo, bool playing, double beats = 0.0,
                                           uint16_t tsigNum = 4) noexcept
{
    clap_event_transport e{};
    e.header.size = sizeof(e);
    e.header.time = 0;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = CLAP_EVENT_TRANSPORT;
    // One integer type throughout: GCC's -Wextra rejects a conditional whose arms mix an
    // enumerator and an unsigned.
    uint32_t flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE |
                     CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
    if (playing)
    {
        flags |= CLAP_TRANSPORT_IS_PLAYING;
    }
    e.flags = flags;
    e.song_pos_beats = static_cast<clap_beattime>(std::llround(beats * CLAP_BEATTIME_FACTOR));
    e.tempo = tempo;
    e.tsig_num = tsigNum;
    e.tsig_denom = 4;
    return e;
}

/// A host that offers clap.thread-pool (M2 sub-project 1's spec §6.7: the stand-in for a host's
/// pool). Its request_exec counts the request and the tasks it asks for, then runs them through
/// the plugin's own clap_plugin_thread_pool.exec on a StubPool of two std::threads (and the
/// calling thread, as a host's pool may use it), returning once all have returned. With
/// declining set, it counts the request and returns false, running nothing, as a busy host may:
/// the plugin then runs them itself. Call attach() with the instance once it exists. Like
/// StubPool it is the host's side of the request, so it isn't real-time safe and RTSan doesn't
/// check it; the tasks it runs, the plugin's, are checked.
class PoolHost
{
  public:
    explicit PoolHost(bool declining = false) : declining_(declining) { host_.host_data = this; }
    PoolHost(const PoolHost &) = delete;
    PoolHost &operator=(const PoolHost &) = delete;

    const clap_host *clap() const noexcept { return &host_; }
    void attach(const clap_plugin *plugin) noexcept { plugin_ = plugin; }
    uint64_t requests() const noexcept { return requests_; }
    uint64_t tasks() const noexcept { return tasks_; }
    void resetCounts() noexcept
    {
        requests_ = 0;
        tasks_ = 0;
    }

  private:
    static PoolHost &self(const clap_host *host) noexcept
    {
        return *static_cast<PoolHost *>(host->host_data);
    }
    static bool requestExec(const clap_host *host, uint32_t numTasks) noexcept
    {
        auto &me = self(host);
        ++me.requests_;
        me.tasks_ += numTasks;
        if (me.declining_ || me.plugin_ == nullptr)
        {
            return false;
        }
        me.exec_ = static_cast<const clap_plugin_thread_pool *>(
            me.plugin_->get_extension(me.plugin_, CLAP_EXT_THREAD_POOL));
        if (me.exec_ == nullptr)
        {
            return false;
        }
        me.pool_.run(numTasks, &PoolHost::task, &me);
        return true;
    }
    static void task(void *ctx, uint32_t index) noexcept
    {
        auto &me = *static_cast<PoolHost *>(ctx);
        me.exec_->exec(me.plugin_, index);
    }
    static const void *getExtension(const clap_host *, const char *id) noexcept
    {
        static const clap_host_thread_pool kPool{&PoolHost::requestExec};
        return std::strcmp(id, CLAP_EXT_THREAD_POOL) == 0 ? &kPool : nullptr;
    }

    const bool declining_;
    const clap_plugin *plugin_{nullptr};
    const clap_plugin_thread_pool *exec_{nullptr};
    uint64_t requests_{0};
    uint64_t tasks_{0};
    StubPool pool_{2};
    clap_host host_{
        CLAP_VERSION_INIT,
        nullptr,
        "gg-tests pool host",
        "Giga Grains",
        "",
        "0.0.0",
        &PoolHost::getExtension,
        [](const clap_host *) {}, // request_restart
        [](const clap_host *) {}, // request_process
        [](const clap_host *) {}, // request_callback
    };
};

/// The scratch folder of the plugin test `name`: GG_TEST_OUTPUT_DIR/plugin-sample-<name>/,
/// created if needed.
inline std::filesystem::path sampleDir(const std::string &name)
{
    const auto dir = std::filesystem::path(GG_TEST_OUTPUT_DIR) / ("plugin-sample-" + name);
    std::filesystem::create_directories(dir);
    return dir;
}

/// Writes `path`: a stereo sine of `hz` at amplitude `amplitude`, `seconds` long at `rate`, as a
/// 32-bit float WAV, the same in both channels. Returns `path`.
inline std::filesystem::path writeSineWav(const std::filesystem::path &path, uint32_t rate = 48000,
                                          double seconds = 4.0, double hz = 220.0,
                                          double amplitude = 0.5)
{
    TestAudio audio{2, rate, {}};
    const auto frames = static_cast<int64_t>(std::llround(seconds * rate));
    audio.samples.resize(static_cast<std::size_t>(frames) * 2);
    for (int64_t i = 0; i < frames; ++i)
    {
        const double x = amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) /
                                              static_cast<double>(rate));
        audio.samples[static_cast<std::size_t>(i) * 2] = x;
        audio.samples[static_cast<std::size_t>(i) * 2 + 1] = x;
    }
    std::filesystem::create_directories(path.parent_path());
    writeFile(path, wavBytes(audio, SampleFormat::Float32));
    return path;
}

/// A 4 s stereo sine at 220 Hz and amplitude 0.5, at 48 kHz, in the scratch folder of the test
/// `name`: the instrument's usual test sample.
inline std::filesystem::path sineSampleFile(const std::string &name)
{
    return writeSineWav(sampleDir(name) / (name + ".wav"));
}

/// The manifest entry of `file`, as a load of it records (gg::media::loadSample), with no sample
/// folder. Empty if the file doesn't load.
inline gg::core::MediaEntry entryOf(const std::filesystem::path &file)
{
    return gg::media::loadSample(file, {}).entry;
}

/// `state`, a state the instrument saved, with `entry` (or none) as its sample (layer A's), and
/// this build's manifest.
inline std::string withSample(const std::string &state,
                              const std::optional<gg::core::MediaEntry> &entry)
{
    gg::core::SessionState s;
    if (!gg::core::decodeState(state, gg::core::Mode::Instrument, s))
    {
        return {};
    }
    s.samples[0] = entry;
    s.newerMedia.reset();
    return gg::core::encodeState(s);
}

/// The MEDI payload of a newer build's manifest (version 3), as this build can't read it: an
/// element and attributes it doesn't know.
inline const std::string kNewerMedia = R"(<media version="3"><layer id="0"><sample )"
                                       R"(sha256="00" path="/Future/pad.wav" /></layer></media>)";

/// `state`, a state the instrument saved, with `media`, a newer build's manifest, as its MEDI
/// and no sample.
inline std::string withNewerMedia(const std::string &state, const std::string &media = kNewerMedia)
{
    gg::core::SessionState s;
    if (!gg::core::decodeState(state, gg::core::Mode::Instrument, s))
    {
        return {};
    }
    s.samples[0].reset();
    s.newerMedia = media;
    return gg::core::encodeState(s);
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

    /// params.flush of `events`, through the vtable, into `out` (or a list that discards them).
    void flush(const clap_input_events *events, const clap_output_events *out = nullptr) const
    {
        params()->flush(plugin_, events, out != nullptr ? out : discardingOutputEvents());
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
    /// clap_plugin.on_main_thread, as a host calls it after request_callback.
    void onMainThread() const { plugin_->on_main_thread(plugin_); }
    /// Gives the instrument `file` as its sample the way a host restoring a session does: its own
    /// state, with the file in the manifest, through state.load. Synchronous (D8). False if the
    /// state doesn't load.
    bool loadSampleState(const std::filesystem::path &file) const
    {
        return load(withSample(save(), entryOf(file)));
    }
    /// Loads a state of every parameter at its default (paramMeta), no sample and engine seed
    /// `seed`, through state.load. A new instance draws a seed of its own (sub-project 5's spec
    /// §7), so a test that compares an instance bit for bit with another, or checks a level its
    /// render reaches, loads this first: loadDefaultsWithSeed(0) gives every instance the seed
    /// they all had before, the same in every run. In the instrument it clears the sample, so
    /// load the sample after it.
    bool loadDefaultsWithSeed(uint64_t seed) const
    {
        gg::core::SessionState s;
        for (std::size_t i = 0; i < gg::core::kParamCount; ++i)
        {
            s.values[i] =
                gg::core::paramMeta(static_cast<gg::core::ParamSlot>(i), impl().mode()).defaultVal;
        }
        s.seed = seed;
        s.routes = gg::core::factoryRoutes(impl().mode());
        return load(gg::core::encodeState(s));
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
    /// The plugin's output events go to `outEvents`, or to a list that discards them. The call
    /// carries `transport` (process.transport), or none.
    clap_process_status process(uint32_t frames, float **inputs, float **outputs,
                                const clap_input_events *events = nullptr,
                                uint32_t inputChannels = 2,
                                const clap_output_events *outEvents = nullptr,
                                const clap_event_transport *transport = nullptr)
    {
        InputEvents none;
        clap_audio_buffer in{inputs, nullptr, inputChannels, 0, 0};
        clap_audio_buffer out{outputs, nullptr, 2, 0, 0};
        clap_process process{};
        process.steady_time = -1;
        process.frames_count = frames;
        process.transport = transport;
        process.audio_inputs = inputs != nullptr ? &in : nullptr;
        process.audio_inputs_count = inputs != nullptr ? 1 : 0;
        process.audio_outputs = outputs != nullptr ? &out : nullptr;
        process.audio_outputs_count = outputs != nullptr ? 1 : 0;
        process.in_events = events != nullptr ? events : none.list();
        process.out_events = outEvents != nullptr ? outEvents : discardingOutputEvents();
        return plugin_->process(plugin_, &process);
    }

  private:
    const clap_plugin *plugin_{nullptr};
    bool active_{false};
    bool processing_{false};
};
} // namespace gg::test

#endif // GG_TESTS_PLUGIN_PLUGINHARNESS_H
