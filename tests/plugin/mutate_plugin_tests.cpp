// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Mutate in the plugin (M2 sub-project 5's spec §6.9): the press is deterministic main-thread work
// whose result reaches the audio thread only as the editor's gestures, begin, value and end for
// each slot that moved, in batches so the editor's queue never overflows. Driven through the CLAP
// factory with a recording output list; the FX is used throughout (it reads no settings file).
// The stub host offers no clap.params, so it never asks for a flush; FlushHost below does.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <utility>
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

/// A host that offers clap.params and counts its request_flush calls; it flushes only when the
/// test says so.
class FlushHost
{
  public:
    FlushHost() { host_.host_data = this; }
    FlushHost(const FlushHost &) = delete;
    FlushHost &operator=(const FlushHost &) = delete;

    const clap_host *clap() const noexcept { return &host_; }
    uint32_t flushRequests() const noexcept { return flushRequests_; }
    uint32_t callbackRequests() const noexcept { return callbackRequests_; }

  private:
    static const void *getExtension(const clap_host *, const char *id) noexcept
    {
        static const clap_host_params kParams{
            [](const clap_host *, clap_param_rescan_flags) {},
            [](const clap_host *, clap_id, clap_param_clear_flags) {},
            [](const clap_host *host) {
                ++static_cast<FlushHost *>(host->host_data)->flushRequests_;
            },
        };
        return std::strcmp(id, CLAP_EXT_PARAMS) == 0 ? &kParams : nullptr;
    }

    uint32_t flushRequests_{0};
    uint32_t callbackRequests_{0};
    clap_host host_{
        CLAP_VERSION_INIT,
        nullptr,
        "gg-tests flush host",
        "Giga Grains",
        "",
        "0.0.0",
        &FlushHost::getExtension,
        [](const clap_host *) {},   // request_restart
        [](const clap_host *) {},   // request_process
        [](const clap_host *host) { // request_callback
            ++static_cast<FlushHost *>(host->host_data)->callbackRequests_;
        },
    };
};

/// What the host heard of one slot.
struct Heard
{
    int begins{0}, values{0}, ends{0};
    double value{0.0};
};

using HeardMap = std::map<clap_id, Heard>;

HeardMap heard(const gg::test::OutputEvents &out)
{
    HeardMap result;
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
HeardMap drain(const gg::test::PluginUnderTest &plugin)
{
    gg::test::InputEvents in;
    gg::test::OutputEvents out(8192);
    plugin.flush(in.list(), out.list());
    CHECK(out.refused() == 0);
    return heard(out);
}

/// Every slot of one drain heard as whole gestures: as many begins as values as ends.
void checkWholeTriplets(const HeardMap &h)
{
    for (const auto &[id, one] : h)
    {
        CHECK(one.begins == one.values);
        CHECK(one.ends == one.values);
    }
}

/// Adds one drain's gestures to `all`.
void accumulate(HeardMap &all, const HeardMap &h)
{
    for (const auto &[id, one] : h)
    {
        all[id].begins += one.begins;
        all[id].values += one.values;
        all[id].ends += one.ends;
        all[id].value = one.value;
    }
}

/// Drains and ticks (on_main_thread, which sends Mutate's next batch) until a drain hears
/// nothing, at most `rounds` times; returns all it heard. Each drain's gestures are whole.
HeardMap drainAll(const gg::test::PluginUnderTest &plugin, int rounds = 16)
{
    HeardMap all;
    for (int round = 0; round < rounds; ++round)
    {
        const auto h = drain(plugin);
        checkWholeTriplets(h);
        accumulate(all, h);
        plugin.onMainThread();
        if (h.empty())
        {
            break;
        }
    }
    return all;
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

/// Sets `slots` to `value` with one params.flush, as the host would.
void flushValues(const gg::test::PluginUnderTest &plugin, std::initializer_list<ParamSlot> slots,
                 double value)
{
    gg::test::InputEvents in;
    std::vector<clap_event_param_value> events;
    events.reserve(slots.size());
    for (const ParamSlot slot : slots)
    {
        events.push_back(gg::test::paramValue(gg::core::paramId(slot), value));
    }
    for (const auto &e : events)
    {
        in.add(&e.header);
    }
    plugin.flush(in.list());
}

/// Layers B to D on, so their parameters move too (an off layer's never do), and nothing locked:
/// a press then moves far more slots than one batch holds.
void everythingMoves(const gg::test::PluginUnderTest &plugin)
{
    unlockAll(plugin.impl());
    flushValues(plugin, {ParamSlot::LayerBOn, ParamSlot::LayerCOn, ParamSlot::LayerDOn}, 1.0);
}

std::size_t countMoved(const std::vector<float> &before, const std::vector<float> &after)
{
    std::size_t n = 0;
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        n += same(before[s], after[s]) ? 0 : 1;
    }
    return n;
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
    CHECK(impl.uiQueueDropped() == 0);

    // A second press moves the sound again, to something else.
    impl.mutate(0.2f);
    const auto second = drainAll(plugin);
    REQUIRE_FALSE(second.empty());
    CHECK(bases(impl) != after);
    CHECK(impl.uiQueueDropped() == 0);
}

TEST_CASE("mutate at zero sends nothing", "[plugin][mutate]")
{
    gg::test::PluginUnderTest plugin(kFx);
    REQUIRE(plugin.valid());
    GigaGrainsPlugin &impl = plugin.impl();
    const auto before = bases(impl);
    impl.mutate(0.0f);
    CHECK(drainAll(plugin).empty());
    CHECK(bases(impl) == before);
}

TEST_CASE("a press of more than one batch sends the rest at later ticks and splits no gesture",
          "[plugin][mutate]")
{
    gg::test::PluginUnderTest plugin(kFx);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.loadDefaultsWithSeed(1));
    GigaGrainsPlugin &impl = plugin.impl();
    everythingMoves(plugin);
    const auto before = bases(impl);

    impl.mutate(1.0f);
    // The first drain hears one batch alone, whole gestures each; the ticks send the rest, one
    // batch per drain the host makes.
    const auto first = drain(plugin);
    checkWholeTriplets(first);
    CHECK(first.size() == GigaGrainsPlugin::kMutateBatch);
    plugin.onMainThread();
    HeardMap all = first;
    const auto rest = drainAll(plugin);
    accumulate(all, rest);

    const auto after = bases(impl);
    const std::size_t changed = countMoved(before, after);
    CHECK(changed > GigaGrainsPlugin::kMutateBatch); // of 218, with every layer on, none locked
    CHECK(all.size() == changed);
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        if (!same(before[s], after[s]))
        {
            const auto id = gg::core::paramId(static_cast<ParamSlot>(s));
            REQUIRE(all.count(id) == 1);
            CHECK(all.at(id).begins == 1);
            CHECK(all.at(id).values == 1);
            CHECK(all.at(id).ends == 1);
            CHECK(static_cast<float>(all.at(id).value) == after[s]);
        }
    }
    CHECK(impl.uiQueueDropped() == 0);
}

TEST_CASE("three presses before any drain drop nothing on a host without clap.params",
          "[plugin][mutate]")
{
    // The stub host never asks for a flush, so the editor's queue's flag stays clear: Mutate
    // must still not queue a batch on top of one nobody has drained. Three presses of every
    // slot are more than the queue's 1,024 entries.
    gg::test::PluginUnderTest plugin(kFx);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.loadDefaultsWithSeed(2));
    GigaGrainsPlugin &impl = plugin.impl();
    everythingMoves(plugin);

    impl.mutate(1.0f);
    impl.mutate(1.0f);
    impl.mutate(1.0f);
    for (int tick = 0; tick < 40; ++tick)
    {
        plugin.onMainThread(); // the host takes its time: nothing drains
    }
    CHECK(impl.uiQueueDropped() == 0);
    const auto all = drainAll(plugin, 32);
    CHECK(impl.uiQueueDropped() == 0);
    // Every slot heard ends where the three presses left it.
    const auto after = bases(impl);
    for (const auto &[id, one] : all)
    {
        const auto slot = gg::core::slotForId(id);
        REQUIRE(slot.has_value());
        CHECK(static_cast<float>(one.value) == after[static_cast<std::size_t>(*slot)]);
    }
}

TEST_CASE("Mutate waits for the host's drain however long it takes, with clap.params",
          "[plugin][mutate]")
{
    FlushHost host;
    gg::test::PluginUnderTest plugin(kFx, host.clap());
    REQUIRE(plugin.valid());
    REQUIRE(plugin.loadDefaultsWithSeed(3));
    GigaGrainsPlugin &impl = plugin.impl();
    everythingMoves(plugin);
    const auto before = bases(impl);

    impl.mutate(1.0f);
    CHECK(host.flushRequests() >= 1);
    impl.mutate(0.5f); // a second press on top, before anything drains
    for (int tick = 0; tick < 40; ++tick)
    {
        plugin.onMainThread();
    }
    CHECK(impl.uiQueueDropped() == 0);
    // Still one batch queued, however many ticks passed.
    const auto first = drain(plugin);
    checkWholeTriplets(first);
    CHECK(first.size() == GigaGrainsPlugin::kMutateBatch);
    plugin.onMainThread();
    HeardMap all = first;
    accumulate(all, drainAll(plugin, 32));
    CHECK(impl.uiQueueDropped() == 0);
    const auto after = bases(impl);
    for (const auto &[id, one] : all)
    {
        const auto s = static_cast<std::size_t>(*gg::core::slotForId(id));
        CHECK(static_cast<float>(one.value) == after[s]);
    }
    // Every slot that moved was heard (one the second press put back where it was may be too).
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        if (!same(before[s], after[s]))
        {
            CHECK(all.count(gg::core::paramId(static_cast<ParamSlot>(s))) == 1);
        }
    }
    CHECK(all.size() >= countMoved(before, after));
}

TEST_CASE("a preset load during a multi-batch press discards the unsent slots", "[plugin][mutate]")
{
    const auto run = [](bool withLoad) {
        gg::test::PluginUnderTest plugin(kFx);
        REQUIRE(plugin.valid());
        REQUIRE(plugin.loadDefaultsWithSeed(5));
        GigaGrainsPlugin &impl = plugin.impl();
        everythingMoves(plugin);
        std::vector<float> result;
        if (withLoad)
        {
            impl.mutate(1.0f);
            const auto first = drain(plugin);
            CHECK(first.size() == GigaGrainsPlugin::kMutateBatch);
            // Slots still wait: the load comes before the rest is sent.
            impl.loadInit();
            // Nothing of the press reaches the host again.
            const auto rest = drainAll(plugin);
            CHECK(rest.empty());
            // The values are INIT's: its defaults (layers B to D off again).
            for (std::size_t s = 0; s < kParamCount; ++s)
            {
                CHECK(same(impl.uiBaseValue(static_cast<ParamSlot>(s)),
                           gg::core::paramMeta(static_cast<ParamSlot>(s), impl.mode()).defaultVal));
            }
        }
        else
        {
            impl.mutate(0.0f); // the same press counter, no change
            impl.loadInit();
            drainAll(plugin);
        }
        // The next press starts from the loaded values.
        unlockAll(impl);
        impl.mutate(1.0f);
        drainAll(plugin);
        CHECK(impl.uiQueueDropped() == 0);
        return bases(impl);
    };
    // A press that starts from the loaded values gives what it gives from the same values with
    // the same counter, whether or not a press was cut short before.
    CHECK(run(true) == run(false));
}

TEST_CASE("slots a press leaves waiting ask the host for a main-thread callback",
          "[plugin][mutate]")
{
    FlushHost host;
    gg::test::PluginUnderTest plugin(kFx, host.clap());
    REQUIRE(plugin.valid());
    REQUIRE(plugin.loadDefaultsWithSeed(6));
    GigaGrainsPlugin &impl = plugin.impl();
    const uint32_t before = host.callbackRequests();
    impl.mutate(1.0f); // layers B to D off: one batch, nothing waits
    CHECK(host.callbackRequests() == before);
    everythingMoves(plugin);
    impl.mutate(1.0f); // more than one batch: the rest waits for the host's drain
    CHECK(host.callbackRequests() > before);
}

TEST_CASE("two presses before a drain give the same sound as two presses a drain apart",
          "[plugin][mutate]")
{
    auto run = [](bool drainBetween, bool every) {
        gg::test::PluginUnderTest plugin(kFx);
        REQUIRE(plugin.loadDefaultsWithSeed(4));
        GigaGrainsPlugin &impl = plugin.impl();
        if (every)
        {
            everythingMoves(plugin);
        }
        impl.mutate(0.3f);
        if (drainBetween)
        {
            drainAll(plugin, 32);
        }
        impl.mutate(0.3f);
        drainAll(plugin, 32);
        CHECK(impl.uiQueueDropped() == 0);
        return bases(impl);
    };
    // The default locks (one batch a press), then everything moving (several).
    CHECK(run(true, false) == run(false, false));
    CHECK(run(true, true) == run(false, true));
}

TEST_CASE("a slot a press left unsent is sent once, with the next press's value",
          "[plugin][mutate]")
{
    gg::test::PluginUnderTest plugin(kFx);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.loadDefaultsWithSeed(5));
    GigaGrainsPlugin &impl = plugin.impl();
    everythingMoves(plugin);

    impl.mutate(1.0f); // the first batch queued, the rest waiting
    impl.mutate(1.0f); // moves the waiting slots again
    drain(plugin);
    plugin.onMainThread();
    const auto rest = drainAll(plugin, 32);
    REQUIRE_FALSE(rest.empty());
    const auto after = bases(impl);
    for (const auto &[id, one] : rest)
    {
        // After the first batch, each slot is heard once, with its latest value: one that waited
        // for its turn and that the second press moved again is not sent twice. (A slot of the
        // first batch may be heard here again: the second press moved it after it was queued.)
        CHECK(one.begins == 1);
        CHECK(one.values == 1);
        const auto s = static_cast<std::size_t>(*gg::core::slotForId(id));
        CHECK(static_cast<float>(one.value) == after[s]);
    }
    CHECK(impl.uiQueueDropped() == 0);
}

TEST_CASE("the locks persist across presses and reset restores the structural locks",
          "[plugin][mutate]")
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
        drainAll(plugin);
        CHECK(same(impl.uiBaseValue(ParamSlot::Size), size));
        CHECK(impl.mutateLocked(ParamSlot::Size));
        CHECK_FALSE(impl.mutateLocked(ParamSlot::Mix));
    }

    impl.resetMutateLocks();
    const LockSet structural = gg::core::structuralLocks(Mode::Fx);
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        CHECK(impl.mutateLocked(static_cast<ParamSlot>(s)) == structural.test(s));
    }
}

TEST_CASE("a layer switched on after Reset locks is mutated", "[plugin][mutate]")
{
    gg::test::PluginUnderTest plugin(kFx);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.loadDefaultsWithSeed(6));
    GigaGrainsPlugin &impl = plugin.impl();
    REQUIRE(impl.uiBaseValue(ParamSlot::LayerBOn) < 0.5f); // off by default

    impl.resetMutateLocks(); // while layer B is off
    CHECK_FALSE(impl.mutateLocked(ParamSlot::B_Size));
    flushValues(plugin, {ParamSlot::LayerBOn}, 1.0);
    const auto before = bases(impl);
    impl.mutate(0.5f);
    drainAll(plugin);
    const auto after = bases(impl);
    std::size_t movedB = 0;
    for (uint8_t p = 0; p < gg::core::kLayerParamCount; ++p)
    {
        const auto s =
            static_cast<std::size_t>(gg::core::layerSlot(static_cast<gg::core::LayerParam>(p), 1));
        movedB += same(before[s], after[s]) ? 0 : 1;
    }
    CHECK(movedB > 5);

    // Switched off again, its parameters stay where they are, whatever the locks.
    flushValues(plugin, {ParamSlot::LayerBOn}, 0.0);
    const auto offBefore = bases(impl);
    impl.mutate(0.5f);
    drainAll(plugin);
    const auto offAfter = bases(impl);
    for (uint8_t p = 0; p < gg::core::kLayerParamCount; ++p)
    {
        const auto s =
            static_cast<std::size_t>(gg::core::layerSlot(static_cast<gg::core::LayerParam>(p), 1));
        CHECK(same(offBefore[s], offAfter[s]));
    }
}

TEST_CASE("a press while inactive stores the values for the next activation", "[plugin][mutate]")
{
    gg::test::PluginUnderTest plugin(kFx);
    REQUIRE(plugin.valid());
    GigaGrainsPlugin &impl = plugin.impl();
    const auto before = bases(impl);
    impl.mutate(0.3f);
    drainAll(plugin); // inactive: the main thread's own flush stores them
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
        drainAll(plugin);
        return bases(impl);
    };
    CHECK(press(7) == press(7));
    CHECK(press(7) != press(8));
}

// ---- sendValues (Task 10b): the safe main-thread entry for many values at once ----

namespace
{
using ValueList = std::vector<std::pair<ParamSlot, double>>;

/// Every slot, each at a distinct value inside its range: `phase` shifts the pattern so two sets
/// differ in every slot.
ValueList wholeSet(Mode mode, int phase)
{
    ValueList list;
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        const auto slot = static_cast<ParamSlot>(s);
        const auto &meta = gg::core::paramMeta(slot, mode);
        const double frac =
            static_cast<double>((s * 7 + static_cast<std::size_t>(phase) * 3) % 17) / 17.0;
        const float value = static_cast<float>(
            meta.minVal + frac * (static_cast<double>(meta.maxVal) - meta.minVal));
        list.emplace_back(slot, static_cast<double>(value));
    }
    return list;
}

/// The values of `list` as the queue holds them: a float each.
void checkEnds(const gg::test::PluginUnderTest &plugin, const ValueList &list)
{
    for (const auto &[slot, value] : list)
    {
        CHECK(plugin.value(gg::core::paramId(slot)) ==
              static_cast<double>(static_cast<float>(value)));
    }
}

void twoSetsBeforeAnyDrain(const gg::test::PluginUnderTest &plugin)
{
    GigaGrainsPlugin &impl = plugin.impl();
    const ValueList first = wholeSet(impl.mode(), 1);
    const ValueList second = wholeSet(impl.mode(), 2);
    impl.sendValues(first);
    impl.sendValues(second); // before anything drained
    for (int tick = 0; tick < 40; ++tick)
    {
        plugin.onMainThread();
    }
    CHECK(impl.uiQueueDropped() == 0);
    const auto all = drainAll(plugin, 32);
    CHECK(impl.uiQueueDropped() == 0);
    for (const auto &[id, one] : all)
    {
        CHECK(one.begins == one.values);
        CHECK(one.ends == one.values);
    }
    checkEnds(plugin, second);
}
} // namespace

TEST_CASE("pushing a whole state through the gesture calls overflows the queue (the hazard)",
          "[plugin][queue]")
{
    gg::test::PluginUnderTest plugin(kFx);
    REQUIRE(plugin.valid());
    GigaGrainsPlugin &impl = plugin.impl();
    for (const ValueList &set : {wholeSet(impl.mode(), 1), wholeSet(impl.mode(), 2)})
    {
        for (const auto &[slot, value] : set)
        {
            impl.uiBeginGesture(slot);
            impl.uiSetValue(slot, value);
            impl.uiEndGesture(slot);
        }
    }
    CHECK(impl.uiQueueDropped() > 0); // 2 x 218 x 3 entries into 1,024
}

TEST_CASE("two whole-state sets before any drain drop nothing, with clap.params", "[plugin][queue]")
{
    FlushHost host;
    gg::test::PluginUnderTest plugin(kFx, host.clap());
    REQUIRE(plugin.valid());
    twoSetsBeforeAnyDrain(plugin);
}

TEST_CASE("two whole-state sets before any drain drop nothing on a host without clap.params",
          "[plugin][queue]")
{
    gg::test::PluginUnderTest plugin(kFx);
    REQUIRE(plugin.valid());
    twoSetsBeforeAnyDrain(plugin);
}

TEST_CASE("a whole-state set and a Mutate press interleaved give one deterministic end state",
          "[plugin][queue][mutate]")
{
    struct Result
    {
        std::vector<float> values;
        HeardMap heard;
    };
    const auto run = [](bool drainBetween) {
        gg::test::PluginUnderTest plugin(kFx);
        REQUIRE(plugin.valid());
        REQUIRE(plugin.loadDefaultsWithSeed(4));
        GigaGrainsPlugin &impl = plugin.impl();
        unlockAll(impl);
        impl.sendValues(wholeSet(impl.mode(), 1));
        HeardMap all;
        if (drainBetween)
        {
            accumulate(all, drain(plugin)); // the first batch only
            plugin.onMainThread();
        }
        impl.mutate(0.5f);
        accumulate(all, drainAll(plugin, 32));
        CHECK(impl.uiQueueDropped() == 0);
        return Result{bases(impl), all};
    };
    const Result with = run(true);
    const Result without = run(false);
    CHECK(with.values == without.values);
    checkWholeTriplets(with.heard);
    checkWholeTriplets(without.heard);
    // Without a drain between, a slot goes out once per batch it is pending for, ending at its
    // latest value: the first batch left with the set, so only its slots can go out again (with
    // the press's value); the rest go out once.
    std::size_t sentTwice = 0;
    for (const auto &[id, one] : without.heard)
    {
        CHECK(one.values >= 1);
        CHECK(one.values <= 2);
        sentTwice += one.values == 2 ? 1 : 0;
        const auto s = static_cast<std::size_t>(*gg::core::slotForId(id));
        CHECK(static_cast<float>(one.value) == without.values[s]);
    }
    CHECK(sentTwice <= GigaGrainsPlugin::kMutateBatch);
    CHECK(without.heard.size() == kParamCount);
}

TEST_CASE("a preset load while a set is half sent discards the rest", "[plugin][queue]")
{
    gg::test::PluginUnderTest plugin(kFx);
    REQUIRE(plugin.valid());
    GigaGrainsPlugin &impl = plugin.impl();
    impl.sendValues(wholeSet(impl.mode(), 1));
    const auto first = drain(plugin);
    CHECK(first.size() == GigaGrainsPlugin::kMutateBatch);
    impl.loadInit(); // slots still wait
    const auto rest = drainAll(plugin);
    CHECK(rest.empty());
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        CHECK(same(impl.uiBaseValue(static_cast<ParamSlot>(s)),
                   gg::core::paramMeta(static_cast<ParamSlot>(s), impl.mode()).defaultVal));
    }
    CHECK(impl.uiQueueDropped() == 0);
}

TEST_CASE("sendValues clamps a value beyond float's range to the slot's limits", "[plugin][queue]")
{
    gg::test::PluginUnderTest plugin(kFx);
    REQUIRE(plugin.valid());
    GigaGrainsPlugin &impl = plugin.impl();
    const auto &meta = gg::core::paramMeta(ParamSlot::Size, impl.mode());
    const auto &metaPitch = gg::core::paramMeta(ParamSlot::Pitch, impl.mode());
    impl.sendValues(ValueList{{ParamSlot::Size, 1e300}, {ParamSlot::Pitch, -1e300}});
    drainAll(plugin);
    CHECK(impl.uiBaseValue(ParamSlot::Size) == meta.maxVal);
    CHECK(impl.uiBaseValue(ParamSlot::Pitch) == metaPitch.minVal);
    CHECK(impl.uiQueueDropped() == 0);
}

TEST_CASE("the user's gesture on a slot still waiting unsent wins over the waiting value",
          "[plugin][queue]")
{
    gg::test::PluginUnderTest plugin(kFx);
    REQUIRE(plugin.valid());
    GigaGrainsPlugin &impl = plugin.impl();
    const ValueList set = wholeSet(impl.mode(), 1);
    impl.sendValues(set); // the first batch leaves now; the rest waits
    // A slot of the last batch is still pending: the user then moves it by hand.
    const ParamSlot slot = set.back().first;
    const auto &meta = gg::core::paramMeta(slot, impl.mode());
    const double mine =
        static_cast<double>(static_cast<float>(0.5 * (double(meta.minVal) + double(meta.maxVal))));
    REQUIRE(mine != set.back().second);
    impl.uiBeginGesture(slot);
    impl.uiSetValue(slot, mine);
    impl.uiEndGesture(slot);
    const auto all = drainAll(plugin, 32);
    CHECK(impl.uiQueueDropped() == 0);
    CHECK(plugin.value(gg::core::paramId(slot)) == mine);
    const Heard &one = all.at(gg::core::paramId(slot));
    CHECK(one.begins == one.values);
    CHECK(one.ends == one.values);
    CHECK(one.value == mine);
}
