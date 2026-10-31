// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "Factory.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <clap/clap.h>
#include <clapwrapper/auv2.h>

#include "GigaGrainsPlugin.h"
#include "PresetDiscovery.h"
#include "gg/plugin/Identity.h"

namespace gg::plugin
{
namespace
{
// The factory index of each product. clap-wrapper's AUv2 build helper and the AUv2 factories
// below are keyed by it, so the order is frozen with the identities.
constexpr uint32_t kInstrumentIndex = 0;
constexpr uint32_t kFxIndex = 1;
constexpr uint32_t kPluginCount = 2;

template <std::size_t N> void copyCode(char (&dst)[N], const char *code) noexcept
{
    static_assert(N == 5, "AudioUnit codes are four characters plus a terminator");
    std::memcpy(dst, code, N - 1);
    dst[N - 1] = '\0';
}

// ---- clap_plugin_factory ----

uint32_t CLAP_ABI pluginCount(const clap_plugin_factory *) noexcept { return kPluginCount; }

const clap_plugin_descriptor *CLAP_ABI pluginDescriptor(const clap_plugin_factory *,
                                                        uint32_t index) noexcept
{
    switch (index)
    {
    case kInstrumentIndex:
        return descriptorFor(Product::Instrument);
    case kFxIndex:
        return descriptorFor(Product::Fx);
    default:
        return nullptr;
    }
}

const clap_plugin *CLAP_ABI createPlugin(const clap_plugin_factory *, const clap_host *host,
                                         const char *pluginId) noexcept
{
    if (host == nullptr || pluginId == nullptr || !clap_version_is_compatible(host->clap_version))
    {
        return nullptr;
    }
    for (const auto product : {Product::Instrument, Product::Fx})
    {
        if (std::strcmp(pluginId, descriptorFor(product)->id) == 0)
        {
            // clap-helpers owns the instance from here: clap_plugin.destroy deletes it.
            try
            {
                return makePlugin(host, product);
            }
            catch (...)
            {
                return nullptr;
            }
        }
    }
    return nullptr;
}

const clap_plugin_factory kPluginFactory{pluginCount, pluginDescriptor, createPlugin};

// ---- clap-wrapper: the AudioUnit identity of each plugin ----

bool CLAP_ABI auv2Info(const clap_plugin_factory_as_auv2 *, uint32_t index,
                       clap_plugin_info_as_auv2_t *info) noexcept
{
    switch (index)
    {
    case kInstrumentIndex:
        copyCode(info->au_type, identity::kAuInstrumentType);
        copyCode(info->au_subt, identity::kAuInstrumentSubtype);
        return true;
    case kFxIndex:
        copyCode(info->au_type, identity::kAuFxType);
        copyCode(info->au_subt, identity::kAuFxSubtype);
        return true;
    default:
        return false;
    }
}

const clap_plugin_factory_as_auv2 kAuv2Factory{identity::kAuManufacturer,
                                               identity::kAuManufacturerName, auv2Info};

// ---- clap-wrapper: the FX's extra `aumf` identity ----
// clap-wrapper reads this at build time and writes a second AudioComponents entry into the
// .component's Info.plist; both entries share the FX's entry point.

uint32_t CLAP_ABI auv2ExtraCount(const clap_plugin_factory_auv2_legacy *,
                                 uint32_t pluginIndex) noexcept
{
    return pluginIndex == kFxIndex ? 1 : 0;
}

bool CLAP_ABI auv2ExtraGet(const clap_plugin_factory_auv2_legacy *, uint32_t pluginIndex,
                           uint32_t n, clap_plugin_auv2_legacy_identity_t *out) noexcept
{
    if (pluginIndex != kFxIndex || n != 0)
    {
        return false;
    }
    copyCode(out->au_type, identity::kAuFxExtraType);
    copyCode(out->au_subt, identity::kAuFxSubtype);
    copyCode(out->au_manu, identity::kAuManufacturer);
    return true;
}

const clap_plugin_factory_auv2_legacy kAuv2ExtraFactory{auv2ExtraCount, auv2ExtraGet};
} // namespace

const void *getFactory(const char *factoryId)
{
    if (factoryId == nullptr)
    {
        return nullptr;
    }
    if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
    {
        return &kPluginFactory;
    }
    if (std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID) == 0 ||
        std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT) == 0)
    {
        return presetDiscoveryFactory();
    }
    if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_INFO_AUV2) == 0)
    {
        return &kAuv2Factory;
    }
    if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_INFO_AUV2_LEGACY) == 0)
    {
        return &kAuv2ExtraFactory;
    }
    return nullptr;
}

// clap_init and clap_deinit must be fast; there is nothing to set up yet.
bool clapInit(const char * /*pluginPath*/) { return true; }

void clapDeinit() {}
} // namespace gg::plugin
