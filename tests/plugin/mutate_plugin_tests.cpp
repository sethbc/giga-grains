// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Mutate in the plugin (M2 sub-project 5's spec §6.9): the press is deterministic main-thread work
// whose result reaches the audio thread only as the editor's gestures, begin, value and end for
// each slot that moved, in batches so the editor's queue never overflows. Driven through the CLAP
// factory with a recording output list; the FX is used throughout (it reads no settings file).

#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <clap/clap.h>

#include "GigaGrainsPlugin.h"
#include "PluginHarness.h"
#include "gg/core/Mutate.h"
#include "gg/core/ParamIds.h"

namespace
{
using gg::core::kParamCount;
using gg::core::LockSet;
using gg::core::Mode;
using gg::core::ParamSlot;
using gg::plugin::GigaGrainsPlugin;
using Event = gg::test::OutputEvents::Event;

constexpr const char *kFx = "org.sethbc.giga-grains-fx";

/// What the host heard of one slot.
struct Heard
{
    int begins{0}, values{0}, ends{0};
    double value{0.0};
};

std::map<clap_id, Heard> heard(const gg::test::OutputEvents &out)
{
    std::map<clap_id, Heard> result;
    for (const Event &e : out.events())
    {
        if (e.header.type == CLAP_EVENT_PARAM_GESTURE_BEGIN)
        {
            ++result[e.paramId].begins;
        }
        else if (e.header.type == CLAP_EVENT_PARAM_GESTURE_END)
        {
            ++result[e.paramId].ends;
        }
        else if (e.header.type == CLAP_EVENT_PARAM_VALUE)
        {
            Heard &h = result[e.paramId];
            ++h.values;
            h.value = e.value;
        }
    }
    return result;
}

/// One params.flush: what the host would hear, and the values applied.
std::map<clap_id, Heard> drain(const gg::test::PluginUnderTest &plugin)
{
    gg::test::InputEvents in;
    gg::test::OutputEvents out(8192);
    plugin.flush(in.list(), out.list());
    CHECK(out.refused() == 0);
    return heard(out);
}

std::vector<float> bases(const GigaGrainsPlugin &impl)
{
    std::vector<float> v(kParamCount);
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        v[s] = impl.uiBaseValue(static_cast<ParamSlot>(s));
    }
    return v;
}

bool same(float a, float b) { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b); }

void unlockAll(GigaGrainsPlugin &impl)
{
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        impl.setMutateLock(static_cast<ParamSlot>(s), false);
    }
}
} // namespace

TEST_CASE("mutate sends begin, value and end for exactly the slots that moved", "[plugin][mutate]")
{
    gg::test::PluginUnderTest plugin(kFx);
    REQUIRE(plugin.valid());
    GigaGrainsPlugin &impl = plugin.impl();
    const auto before = bases(impl);
    gg::core::ParamValues values{};
    LockSet locks;
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        values[s] = before[s];
        locks.set(s, impl.mutateLocked(static_cast<ParamSlot>(s)));
    }
    locks |= gg::core::offLayerLocks(values);

    impl.mutate(0.2f);
    const auto h = drain(plugin);
    REQUIRE_FALSE(h.empty());
    const auto after = bases(impl);
    std::size_t changed = 0;
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        const auto id = gg::core::paramId(static_cast<ParamSlot>(s));
        const bool moved = !same(before[s], after[s]);
        if (locks.test(s))
        {
            CHECK_FALSE(moved);
            CHECK(h.count(id) == 0);
        }
        if (moved)
        {
            ++changed;
            REQUIRE(h.count(id) == 1);
            CHECK(h.at(id).begins == 1);
            CHECK(h.at(id).values == 1);
            CHECK(h.at(id).ends == 1);
            CHECK(static_cast<float>(h.at(id).value) == after[s]);
            CHECK(plugin.value(id) == static_cast<double>(after[s]));
        }
        else
        {
            CHECK(h.count(id) == 0);
        }
    }
    CHECK(changed == h.size());
    CHECK(changed > 10);
    CHECK(impl.uiEventsDropped() == 0);

    // A second press moves the sound again, to something else.
    impl.mutate(0.2f);
    const auto second = drain(plugin);
    REQUIRE_FALSE(second.empty());
    CHECK(bases(impl) != after);
}

TEST_CASE("mutate at zero sends nothing", "[plugin][mutate]")
{
    gg::test::PluginUnderTest plugin(kFx);
    REQUIRE(plugin.valid());
    GigaGrainsPlugin &impl = plugin.impl();
    const auto before = bases(impl);
    impl.mutate(0.0f);
    CHECK(drain(plugin).empty());
    CHECK(bases(impl) == before);
}

TEST_CASE("the changed slots arrive in batches and the queue never overflows", "[plugin][mutate]")
{
    gg::test::PluginUnderTest plugin(kFx);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.loadDefaultsWithSeed(1));
    GigaGrainsPlugin &impl = plugin.impl();
    unlockAll(impl);
    {
        // Layers B to D on, so their parameters move too (an off layer's never do).
        gg::test::InputEvents in;
        std::vector<clap_event_param_value> events;
        for (const ParamSlot on : {ParamSlot::LayerBOn, ParamSlot::LayerCOn, ParamSlot::LayerDOn})
        {
            events.push_back(gg::test::paramValue(gg::core::paramId(on), 1.0));
        }
        for (const auto &e : events)
        {
            in.add(&e.header);
        }
        plugin.flush(in.list());
    }
    const auto before = bases(impl);

    impl.mutate(1.0f);
    // Whatever the first batch left, a tick (on_main_thread) sends the next once the host has
    // drained: all of it arrives, in order, with nothing dropped.
    std::map<clap_id, Heard> all;
    for (int round = 0; round < 8; ++round)
    {
        for (const auto &[id, h] : drain(plugin))
        {
            all[id].begins += h.begins;
            all[id].values += h.values;
            all[id].ends += h.ends;
            all[id].value = h.value;
        }
        plugin.onMainThread();
    }
    const auto after = bases(impl);
    std::size_t changed = 0;
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        if (!same(before[s], after[s]))
        {
            ++changed;
            const auto id = gg::core::paramId(static_cast<ParamSlot>(s));
            REQUIRE(all.count(id) == 1);
            CHECK(all.at(id).begins == 1);
            CHECK(all.at(id).values == 1);
            CHECK(all.at(id).ends == 1);
        }
    }
    CHECK(changed >= 100); // of 218, with every layer on and nothing locked
    CHECK(all.size() == changed);
    CHECK(impl.uiEventsDropped() == 0);
}

TEST_CASE("the locks persist across presses and reset restores the defaults", "[plugin][mutate]")
{
    gg::test::PluginUnderTest plugin(kFx);
    REQUIRE(plugin.valid());
    GigaGrainsPlugin &impl = plugin.impl();

    // The structural locks at the start.
    CHECK(impl.mutateLocked(ParamSlot::Mix));
    CHECK(impl.mutateLocked(ParamSlot::Macro1));
    CHECK_FALSE(impl.mutateLocked(ParamSlot::Size));

    impl.setMutateLock(ParamSlot::Size, true);
    impl.setMutateLock(ParamSlot::Mix, false);
    const float size = impl.uiBaseValue(ParamSlot::Size);
    for (int i = 0; i < 3; ++i)
    {
        impl.mutate(0.5f);
        drain(plugin);
        CHECK(same(impl.uiBaseValue(ParamSlot::Size), size));
        CHECK(impl.mutateLocked(ParamSlot::Size));
        CHECK_FALSE(impl.mutateLocked(ParamSlot::Mix));
    }

    impl.resetMutateLocks();
    gg::core::ParamValues values{};
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        values[s] = impl.uiBaseValue(static_cast<ParamSlot>(s));
    }
    const LockSet defaults = gg::core::defaultMutateLocks(values, Mode::Fx);
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        CHECK(impl.mutateLocked(static_cast<ParamSlot>(s)) == defaults.test(s));
    }
}

TEST_CASE("a press while inactive stores the values for the next activation", "[plugin][mutate]")
{
    gg::test::PluginUnderTest plugin(kFx);
    REQUIRE(plugin.valid());
    GigaGrainsPlugin &impl = plugin.impl();
    const auto before = bases(impl);
    impl.mutate(0.3f);
    drain(plugin); // inactive: the main thread's own flush stores them
    const auto after = bases(impl);
    CHECK(after != before);
    REQUIRE(plugin.activate(48000.0, 64));
    CHECK(bases(impl) == after);
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        const auto id = gg::core::paramId(static_cast<ParamSlot>(s));
        CHECK(plugin.value(id) == static_cast<double>(after[s]));
    }
}

TEST_CASE("the same seed and press give the same sound", "[plugin][mutate]")
{
    auto press = [](uint64_t seed) {
        gg::test::PluginUnderTest plugin(kFx);
        REQUIRE(plugin.loadDefaultsWithSeed(seed));
        GigaGrainsPlugin &impl = plugin.impl();
        impl.mutate(0.4f);
        drain(plugin);
        return bases(impl);
    };
    CHECK(press(7) == press(7));
    CHECK(press(7) != press(8));
}
