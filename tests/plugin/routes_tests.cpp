// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The route path from the editor (M2 sub-project 6's spec §6.6, §7): GigaGrainsPlugin's
// uiRoutes() and uiSetRoutes(), main thread. A route edit reaches the render as a state load's
// routes do, is saved with the state, loses to a state load that follows it before the next
// block, allocates nothing on the audio thread, and while inactive applies at activate().

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <clap/clap.h>

#include "PluginHarness.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"
#include "gg/core/SessionState.h"
#include "support/AllocTripwire.h"

using gg::core::ModCurve;
using gg::core::ModRoute;
using gg::core::ModRoutes;
using gg::core::ModSource;
using gg::core::ParamSlot;
using gg::test::AllocTripwire;
using gg::test::PluginUnderTest;
using gg::test::Stereo;

namespace
{
constexpr const char *kInstrumentId = "org.sethbc.giga-grains";
constexpr const char *kFxId = "org.sethbc.giga-grains-fx";

constexpr double kRate = 48000.0;
constexpr uint32_t kBlock = 512;

// The product's factory routes with `route` in slot `slot` (0-based).
ModRoutes withRoute(gg::core::Mode mode, std::size_t slot, const ModRoute &route)
{
    auto routes = gg::core::factoryRoutes(mode);
    routes[slot] = route;
    return routes;
}

// The brief's route: slot 13 (index 12), Macro 3 to Pitch on every layer, depth 12.
ModRoutes macro3ToPitch(gg::core::Mode mode)
{
    return withRoute(mode, 12,
                     {ModSource::Macro3, gg::core::kSameLayer, ParamSlot::Pitch,
                      gg::core::kAllLayers, 12.0f, ModCurve::Unipolar});
}

// A second set, different from macro3ToPitch's: slot 20 (index 19), Macro 4 to Size on layer A.
ModRoutes macro4ToSize(gg::core::Mode mode)
{
    return withRoute(
        mode, 19,
        {ModSource::Macro4, gg::core::kSameLayer, ParamSlot::Size, 0, -0.5f, ModCurve::Bipolar});
}

// A state of every parameter at its default but Macro 3 (`macro3`) and Pitch (`pitch`), engine
// seed 0, and `routes`.
std::string stateWith(gg::core::Mode mode, const ModRoutes &routes, float macro3 = 0.5f,
                      float pitch = 0.0f)
{
    gg::core::SessionState s;
    for (std::size_t i = 0; i < gg::core::kParamCount; ++i)
    {
        s.values[i] = gg::core::paramMeta(static_cast<ParamSlot>(i), mode).defaultVal;
    }
    s.values[static_cast<std::size_t>(ParamSlot::Macro3)] = macro3;
    s.values[static_cast<std::size_t>(ParamSlot::Pitch)] = pitch;
    s.seed = 0;
    s.routes = routes;
    return gg::core::encodeState(s);
}

bool isFx(const char *id) { return std::string_view(id) == kFxId; }

// Loads `state` and, in the instrument, a sine sample after it (the state clears the sample).
void prepare(PluginUnderTest &plugin, const char *id, const std::string &state)
{
    REQUIRE(plugin.load(state));
    if (!isFx(id))
    {
        REQUIRE(plugin.loadSampleState(gg::test::sineSampleFile("routes-render")));
    }
}

// One second through process() in kBlock-frame calls, both channels in turn: the FX over noise,
// the instrument with one note held from the first frame.
std::vector<float> renderSecond(PluginUnderTest &plugin, const char *id)
{
    const auto total = static_cast<uint32_t>(kRate);
    Stereo in(kBlock);
    Stereo out(kBlock);
    std::vector<float> rendered;
    rendered.reserve(2 * total);
    const auto on = gg::test::noteEvent(CLAP_EVENT_NOTE_ON, 0, 60, 0);
    for (uint32_t done = 0; done < total; done += kBlock)
    {
        const uint32_t frames = std::min(kBlock, total - done);
        gg::test::fillNoise(in, 1000 + done);
        gg::test::InputEvents events;
        if (done == 0)
        {
            events.add(&on.header);
        }
        REQUIRE(plugin.process(frames, isFx(id) ? in.data() : nullptr, out.data(), events.list()) ==
                CLAP_PROCESS_CONTINUE);
        rendered.insert(rendered.end(), out.left.begin(), out.left.begin() + frames);
        rendered.insert(rendered.end(), out.right.begin(), out.right.begin() + frames);
    }
    return rendered;
}

// One kBlock-frame call, silent input and no events.
void processBlock(PluginUnderTest &plugin, const char *id)
{
    Stereo in(kBlock);
    Stereo out(kBlock);
    REQUIRE(plugin.process(kBlock, isFx(id) ? in.data() : nullptr, out.data()) ==
            CLAP_PROCESS_CONTINUE);
}

// A host that offers clap.state and counts state.mark_dirty.
class StateHost
{
  public:
    StateHost() { host_.host_data = this; }
    StateHost(const StateHost &) = delete;
    StateHost &operator=(const StateHost &) = delete;

    const clap_host *clap() const noexcept { return &host_; }
    uint32_t dirtyMarks() const noexcept { return dirtyMarks_; }

  private:
    static const void *getExtension(const clap_host *, const char *id) noexcept
    {
        static const clap_host_state kState{
            [](const clap_host *host) { ++static_cast<StateHost *>(host->host_data)->dirtyMarks_; },
        };
        return std::strcmp(id, CLAP_EXT_STATE) == 0 ? &kState : nullptr;
    }

    uint32_t dirtyMarks_{0};
    clap_host host_{
        CLAP_VERSION_INIT,
        nullptr,
        "gg-tests state host",
        "Giga Grains",
        "",
        "0.0.0",
        &StateHost::getExtension,
        [](const clap_host *) {}, // request_restart
        [](const clap_host *) {}, // request_process
        [](const clap_host *) {}, // request_callback
    };
};
} // namespace

TEST_CASE("routes set from the editor reach the render", "[plugin]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    PluginUnderTest edited(id);
    PluginUnderTest loaded(id);
    PluginUnderTest plain(id);
    REQUIRE(edited.valid());
    REQUIRE(loaded.valid());
    REQUIRE(plain.valid());
    const auto mode = edited.impl().mode();
    const auto routes = macro3ToPitch(mode);
    REQUIRE(routes != gg::core::factoryRoutes(mode));

    // The same values, seed and sample in all three, Macro 3 at 1: the routes differ only in how
    // they arrive (the editor, a state load) or not at all. Pitch is at -24, so the factory
    // route's +24 from Macro 3 (slot 5) brings it to 0 and the editor's +12 to 12, inside Pitch's
    // ±24 (at Pitch 0 both would clamp at 24, and the edit wouldn't be heard).
    prepare(edited, id, stateWith(mode, gg::core::factoryRoutes(mode), 1.0f, -24.0f));
    prepare(loaded, id, stateWith(mode, routes, 1.0f, -24.0f));
    prepare(plain, id, stateWith(mode, gg::core::factoryRoutes(mode), 1.0f, -24.0f));
    REQUIRE(edited.activate(kRate, kBlock));
    REQUIRE(loaded.activate(kRate, kBlock));
    REQUIRE(plain.activate(kRate, kBlock));

    edited.impl().uiSetRoutes(routes); // active: through the audio thread's next call
    CHECK(edited.impl().uiRoutes() == routes);

    const auto a = renderSecond(edited, id);
    const auto b = renderSecond(loaded, id);
    const auto c = renderSecond(plain, id);
    CHECK(edited.impl().processor()->routes() == routes);
    CHECK(edited.impl().processor()->effective(ParamSlot::Pitch) == 12.0f);
    CHECK(plain.impl().processor()->effective(ParamSlot::Pitch) == 0.0f);
    CHECK(gg::test::sameBits(a, b));
    CHECK_FALSE(gg::test::sameBits(a, c)); // the route is heard
}

TEST_CASE("routes set from the editor are saved", "[plugin]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const auto mode = plugin.impl().mode();
    const auto routes = macro3ToPitch(mode);
    CHECK(plugin.impl().uiRoutes() == gg::core::factoryRoutes(mode)); // a new instance's
    REQUIRE(plugin.activate(kRate, kBlock));

    plugin.impl().uiSetRoutes(routes);
    const std::string saved = plugin.save();
    REQUIRE_FALSE(saved.empty());

    PluginUnderTest restored(id);
    REQUIRE(restored.valid());
    REQUIRE(restored.load(saved));
    CHECK(restored.impl().uiRoutes() == routes);
    gg::core::SessionState decoded;
    REQUIRE(gg::core::decodeState(saved, mode, decoded));
    CHECK(decoded.routes == routes);
}

TEST_CASE("a route edit from the editor marks the state dirty", "[plugin]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    StateHost host;
    PluginUnderTest plugin(id, host.clap());
    REQUIRE(plugin.valid());
    const uint32_t before = host.dirtyMarks();
    plugin.impl().uiSetRoutes(macro3ToPitch(plugin.impl().mode()));
    CHECK(host.dirtyMarks() == before + 1);
}

TEST_CASE("a UI route edit and a state load in one block: the load wins", "[plugin]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const auto mode = plugin.impl().mode();
    const auto a = macro3ToPitch(mode);
    const auto b = macro4ToSize(mode);
    REQUIRE(a != b);
    REQUIRE(plugin.activate(kRate, kBlock));
    processBlock(plugin, id);

    plugin.impl().uiSetRoutes(a);
    REQUIRE(plugin.load(stateWith(mode, b)));
    processBlock(plugin, id);
    CHECK(plugin.impl().processor()->routes() == b);
    CHECK(plugin.impl().uiRoutes() == b);
    processBlock(plugin, id); // and stays so: nothing older is left to apply
    CHECK(plugin.impl().processor()->routes() == b);
}

TEST_CASE("a state load and a UI route edit in one block: the edit wins", "[plugin]")
{
    // The other order: the edit made after the load is the newer, and is what plays and saves.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const auto mode = plugin.impl().mode();
    const auto a = macro3ToPitch(mode);
    const auto b = macro4ToSize(mode);
    REQUIRE(plugin.activate(kRate, kBlock));
    processBlock(plugin, id);

    REQUIRE(plugin.load(stateWith(mode, b)));
    plugin.impl().uiSetRoutes(a);
    processBlock(plugin, id);
    CHECK(plugin.impl().processor()->routes() == a);
    CHECK(plugin.impl().uiRoutes() == a);
    processBlock(plugin, id);
    CHECK(plugin.impl().processor()->routes() == a);
}

TEST_CASE("the route path allocates nothing on the audio thread", "[plugin][rt]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    if (!isFx(id))
    {
        REQUIRE(plugin.loadSampleState(gg::test::sineSampleFile("routes-rt")));
    }
    REQUIRE(plugin.activate(kRate, kBlock));
    const auto mode = plugin.impl().mode();
    const auto a = macro3ToPitch(mode);
    const auto b = macro4ToSize(mode);

    // Everything the calls touch exists before the tripwire is armed.
    Stereo in(kBlock);
    Stereo out(kBlock);
    gg::test::fillNoise(in, 7);
    const auto on = gg::test::noteEvent(CLAP_EVENT_NOTE_ON, 0, 60, 0);
    gg::test::InputEvents events;
    events.add(&on.header);
    float **inputs = isFx(id) ? in.data() : nullptr;
    clap_process_status status[4]{};

    AllocTripwire::resetCount();
    plugin.impl().uiSetRoutes(a); // main thread, outside the armed scopes
    {
        AllocTripwire::Scope armed;
        status[0] = plugin.process(0, inputs, out.data(), events.list());
        status[1] = plugin.process(1, inputs, out.data(), events.list());
    }
    const bool tookA = plugin.impl().processor()->routes() == a;
    plugin.impl().uiSetRoutes(b);
    {
        AllocTripwire::Scope armed;
        status[2] = plugin.process(kBlock, inputs, out.data(), events.list());
        status[3] = plugin.process(kBlock, inputs, out.data(), events.list());
    }
    const uint64_t allocations = AllocTripwire::count();

    for (const auto s : status)
    {
        CHECK(s == CLAP_PROCESS_CONTINUE);
    }
    CHECK(tookA); // the 0-frame call takes the edit too
    CHECK(plugin.impl().processor()->routes() == b);
    if (AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("an inactive plugin's route edit applies at activate", "[plugin]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const auto mode = plugin.impl().mode();
    const auto a = macro3ToPitch(mode);
    const auto b = macro4ToSize(mode);

    plugin.impl().uiSetRoutes(a);
    CHECK(plugin.impl().uiRoutes() == a);
    REQUIRE(plugin.activate(kRate, kBlock));
    CHECK(plugin.impl().processor()->routes() == a); // before any process()
    processBlock(plugin, id);
    CHECK(plugin.impl().processor()->routes() == a);

    // An edit the audio thread never took, made just before a deactivation, isn't applied after
    // the next activation over the routes edited while inactive.
    plugin.impl().uiSetRoutes(b);
    plugin.deactivate();
    plugin.impl().uiSetRoutes(a);
    REQUIRE(plugin.activate(kRate, kBlock));
    CHECK(plugin.impl().processor()->routes() == a);
    processBlock(plugin, id);
    CHECK(plugin.impl().processor()->routes() == a);
}
