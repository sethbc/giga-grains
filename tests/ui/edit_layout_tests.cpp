// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// EditLayout: the views' placement table (M2 sub-project 6's spec §6.5), which names every
// parameter's view, panel, order and paired spread. These tests need no editor or JUCE, so they
// also run in the sanitizer builds.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/ParamIds.h"
#include "gg/ui/EditLayout.h"
#include "gg/ui/LayerColours.h"

using gg::core::LayerParam;
using gg::core::ParamSlot;
using gg::ui::Panel;
using gg::ui::View;

namespace
{
/// The layer whose panels draw `slot`: a layer parameter's or a per-layer global's; 0 for the
/// other globals (their panels ignore the layer).
uint8_t layerFor(ParamSlot slot)
{
    if (const auto ref = gg::core::layerOf(slot))
    {
        return ref->layer;
    }
    return gg::core::perLayerGlobalLayer(slot).value_or(0);
}

std::string nameOf(ParamSlot slot)
{
    return "slot " + std::to_string(static_cast<std::size_t>(slot)) + " (id " +
           std::to_string(gg::core::paramId(slot)) + ")";
}
} // namespace

TEST_CASE("every parameter has exactly one placement", "[ui]")
{
    for (std::size_t i = 0; i < gg::core::kParamCount; ++i)
    {
        const auto slot = static_cast<ParamSlot>(i);
        INFO(nameOf(slot));
        const auto placement = gg::ui::placementOf(slot);
        REQUIRE(placement.has_value());
        CHECK(placement->panel != Panel::Count);

        const uint8_t layer = layerFor(slot);
        int seen = 0;
        for (std::size_t p = 0; p < static_cast<std::size_t>(Panel::Count); ++p)
        {
            const auto panel = static_cast<Panel>(p);
            for (const ParamSlot drawn : gg::ui::slotsIn(panel, layer))
            {
                if (drawn == slot)
                {
                    ++seen;
                    CHECK(panel == placement->panel);
                }
            }
        }
        CHECK(seen == 1);
    }
    CHECK_FALSE(gg::ui::placementOf(ParamSlot::Count).has_value());
}

TEST_CASE("layers B to D place as layer A does", "[ui]")
{
    for (std::size_t i = 0; i < gg::core::kLayerParamCount; ++i)
    {
        const auto p = static_cast<LayerParam>(i);
        const auto a = gg::ui::placementOf(gg::core::layerSlot(p, 0));
        REQUIRE(a.has_value());
        for (uint8_t layer = 1; layer < gg::core::kLayers; ++layer)
        {
            INFO("parameter " << i << ", layer " << int(layer));
            const auto other = gg::ui::placementOf(gg::core::layerSlot(p, layer));
            REQUIRE(other.has_value());
            CHECK(other->view == a->view);
            CHECK(other->panel == a->panel);
            CHECK(other->order == a->order);
        }
    }
    CHECK(gg::ui::placementOf(ParamSlot::B_Size)->panel ==
          gg::ui::placementOf(ParamSlot::Size)->panel);
    CHECK(gg::ui::placementOf(ParamSlot::LevelC)->panel ==
          gg::ui::placementOf(ParamSlot::Level)->panel);
    CHECK(gg::ui::placementOf(ParamSlot::LayerDOn)->panel ==
          gg::ui::placementOf(ParamSlot::LayerAOn)->panel);
    // A layer's panel draws that layer's slots; a global panel ignores the layer.
    CHECK(gg::ui::slotsIn(Panel::Pitch, 2) ==
          std::vector<ParamSlot>{ParamSlot::C_Pitch, ParamSlot::C_PitchSpread});
    CHECK(gg::ui::slotsIn(Panel::Output, 3) == gg::ui::slotsIn(Panel::Output, 0));
}

TEST_CASE("the spread pairs", "[ui]")
{
    CHECK(gg::ui::placementOf(ParamSlot::Size)->pairedSpread == ParamSlot::SizeSpread);
    CHECK(gg::ui::placementOf(ParamSlot::Position)->pairedSpread == ParamSlot::Spray);
    CHECK(gg::ui::placementOf(ParamSlot::Pitch)->pairedSpread == ParamSlot::PitchSpread);
    CHECK(gg::ui::placementOf(ParamSlot::Spray)->pairedSpread == ParamSlot::Count);
    CHECK(gg::ui::placementOf(ParamSlot::Density)->pairedSpread == ParamSlot::Count);
    // A layer's pair is its own layer's spread.
    CHECK(gg::ui::placementOf(ParamSlot::D_Size)->pairedSpread == ParamSlot::D_SizeSpread);
    CHECK(gg::ui::placementOf(ParamSlot::B_Position)->pairedSpread == ParamSlot::B_Spray);
}

TEST_CASE("the hidden slots", "[ui]")
{
    using gg::core::Mode;
    for (std::size_t i = 0; i < gg::core::kParamCount; ++i)
    {
        const auto slot = static_cast<ParamSlot>(i);
        INFO(nameOf(slot));
        const bool hiddenInInstrument = slot == ParamSlot::Mix || slot == ParamSlot::DryToReverb;
        CHECK(gg::ui::hiddenIn(slot, Mode::Instrument) == hiddenInInstrument);
        CHECK_FALSE(gg::ui::hiddenIn(slot, Mode::Fx));
    }
}

TEST_CASE("the panel order is the spec's", "[ui]")
{
    CHECK(gg::ui::slotsIn(Panel::Grain, 0) ==
          std::vector<ParamSlot>{ParamSlot::Size, ParamSlot::SizeSpread, ParamSlot::Position,
                                 ParamSlot::Spray, ParamSlot::Texture});
    std::vector<ParamSlot> step{ParamSlot::StepRate, ParamSlot::StepLength};
    for (std::size_t i = 0; i < 16; ++i)
    {
        step.push_back(static_cast<ParamSlot>(static_cast<std::size_t>(ParamSlot::Step1) + i));
    }
    CHECK(gg::ui::slotsIn(Panel::Step, 0) == step);
    CHECK(gg::ui::slotsIn(Panel::Scheduler, 1) == std::vector<ParamSlot>{ParamSlot::B_Density,
                                                                         ParamSlot::B_Distribution,
                                                                         ParamSlot::B_Freeze});
    CHECK(gg::ui::slotsIn(Panel::Space, 1) ==
          std::vector<ParamSlot>{ParamSlot::B_Stereo, ParamSlot::B_Diffusion, ParamSlot::B_Feedback,
                                 ParamSlot::LevelB});
    CHECK(gg::ui::slotsIn(Panel::Filter, 0).empty()); // until PR 2
    CHECK(std::string(gg::ui::panelName(Panel::Lfo1)) == "LFO 1");
    CHECK(std::string(gg::ui::panelName(Panel::Random2)) == "Random 2");
    // Each panel's slots are in their placements' order, and every placed slot's view is the
    // panel's.
    for (std::size_t p = 0; p < static_cast<std::size_t>(Panel::Count); ++p)
    {
        const auto panel = static_cast<Panel>(p);
        INFO(gg::ui::panelName(panel));
        CHECK(std::string(gg::ui::panelName(panel)).size() > 0);
        const auto slots = gg::ui::slotsIn(panel, 0);
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            const auto placement = gg::ui::placementOf(slots[i]);
            REQUIRE(placement.has_value());
            CHECK(placement->order == i);
            CHECK(placement->view == gg::ui::placementOf(slots[0])->view);
        }
    }
    CHECK(gg::ui::placementOf(ParamSlot::ReverbSize)->view == View::Fx);
    CHECK(gg::ui::placementOf(ParamSlot::MorphX)->view == View::Perform);
    CHECK(gg::ui::placementOf(ParamSlot::B_Lfo1Rate)->view == View::Edit);
}

TEST_CASE("the layer colours are Okabe-Ito's", "[ui]")
{
    CHECK(gg::ui::kLayerTints == std::array<uint32_t, 4>{0xE69F00, 0x56B4E9, 0x009E73, 0xCC79A7});
    CHECK(gg::ui::kLayerLetters == std::array<char, 4>{'A', 'B', 'C', 'D'});
}
