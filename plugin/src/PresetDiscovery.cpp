// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "PresetDiscovery.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include <clap/clap.h>

#include "GigaGrainsPlugin.h"
#include "SettingsPath.h"
#include "gg/core/Preset.h"
#include "gg/core/PresetInfo.h"
#include "gg/core/SessionState.h"
#include "gg/media/ContentPaths.h"
#include "gg/media/SettingsFile.h"
#include "gg/media/Utf8Path.h"

namespace gg::plugin
{
namespace fs = std::filesystem;

std::vector<PresetLocation> presetLocations(const core::MachineSettings &settings,
                                            const fs::path &settingsFile)
{
    std::vector<PresetLocation> locations;
    for (auto &root : media::contentRoots(settings))
    {
        auto folder = media::factoryPresetsFolder(root);
        std::error_code error;
        if (fs::is_directory(folder, error))
        {
            locations.push_back({std::move(folder), true, std::move(root)});
        }
    }
    if (auto user = media::userPresetsFolder(settingsFile); !user.empty())
    {
        locations.push_back({std::move(user), false, {}});
    }
    return locations;
}

namespace
{
constexpr const char *kProviderId = "org.sethbc.giga-grains.presets";
constexpr const char *kSoundpackId = "org.sethbc.giga-grains.factory";
constexpr const char *kVendor = "sethbc";
constexpr const char *kPluginAbi = "clap";
constexpr const char *kNotAPreset = "not a Giga Grains preset";
constexpr const char *kUnreadable = "can't read the file";

/// The longest content-version.txt line read: a tag and a SHA-256 fit many times over.
constexpr std::size_t kMaxVersionBytes = 1024;

const clap_preset_discovery_provider_descriptor kDescriptor{CLAP_VERSION_INIT, kProviderId,
                                                            "Giga Grains presets", kVendor};

/// The first line of `file`, without its line ending and at most kMaxVersionBytes long; empty
/// when the file can't be read. It reads no more than the cap, whatever the file holds.
std::string firstLine(const fs::path &file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        return {};
    }
    std::string line(kMaxVersionBytes, '\0');
    in.read(line.data(), static_cast<std::streamsize>(line.size()));
    line.resize(static_cast<std::size_t>(in.gcount()));
    if (const auto newline = line.find('\n'); newline != std::string::npos)
    {
        line.resize(newline);
    }
    if (!line.empty() && line.back() == '\r')
    {
        line.pop_back();
    }
    return line;
}

/// `file`'s bytes; nullopt when it can't be read. A file longer than kMaxStateBytes, which no
/// preset is, isn't read: its bytes are empty, which presetInfoOf refuses as it would the file.
std::optional<std::string> readPresetBytes(const fs::path &file)
{
    std::error_code error;
    const auto size = fs::file_size(file, error);
    if (error)
    {
        return std::nullopt;
    }
    if (size > core::kMaxStateBytes)
    {
        return std::string();
    }
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (static_cast<std::uintmax_t>(in.gcount()) != size)
    {
        return std::nullopt;
    }
    return bytes;
}

/// `file`'s modification time in seconds since the Unix epoch; CLAP_TIMESTAMP_UNKNOWN when it
/// can't be read or is before the epoch.
clap_timestamp modificationTime(const fs::path &file)
{
    std::error_code error;
    const auto written = fs::last_write_time(file, error);
    if (error)
    {
        return CLAP_TIMESTAMP_UNKNOWN;
    }
#if defined(_WIN32)
    // MSVC's STL has no file_clock::to_sys; clock_cast goes through utc_clock.
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(written);
#else
    const auto system = std::chrono::file_clock::to_sys(written);
#endif
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
    return seconds > 0 ? static_cast<clap_timestamp>(seconds) : CLAP_TIMESTAMP_UNKNOWN;
}

/// Whether `file` is inside `folder`, compared lexically.
bool isInside(const fs::path &file, const fs::path &folder)
{
    const auto relative = file.lexically_normal().lexically_relative(folder.lexically_normal());
    return !relative.empty() && *relative.begin() != "..";
}

Product productOf(core::Mode mode) noexcept
{
    return mode == core::Mode::Instrument ? Product::Instrument : Product::Fx;
}

/// The provider: what init declared, kept for get_metadata (which needs the factory folders) and
/// so that every string handed to the indexer outlives its call.
class Provider
{
  public:
    explicit Provider(const clap_preset_discovery_indexer *indexer) : indexer_(indexer)
    {
        provider_.desc = &kDescriptor;
        provider_.provider_data = this;
        provider_.init = &Provider::init;
        provider_.destroy = &Provider::destroy;
        provider_.get_metadata = &Provider::getMetadata;
        provider_.get_extension = &Provider::getExtension;
    }

    const clap_preset_discovery_provider *clap() const noexcept { return &provider_; }

  private:
    static Provider &self(const clap_preset_discovery_provider *p) noexcept
    {
        return *static_cast<Provider *>(p->provider_data);
    }

    static bool CLAP_ABI init(const clap_preset_discovery_provider *p) noexcept
    {
        try
        {
            self(p).declare();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    static void CLAP_ABI destroy(const clap_preset_discovery_provider *p) noexcept
    {
        delete &self(p);
    }

    static bool CLAP_ABI getMetadata(const clap_preset_discovery_provider *p, uint32_t kind,
                                     const char *location,
                                     const clap_preset_discovery_metadata_receiver *r) noexcept
    {
        if (r == nullptr)
        {
            return false;
        }
        try
        {
            if (kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN)
            {
                return location == nullptr && reportInit(r);
            }
            if (kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE && location != nullptr)
            {
                return self(p).reportFile(location, r);
            }
            return false;
        }
        catch (...)
        {
            return false;
        }
    }

    static const void *CLAP_ABI getExtension(const clap_preset_discovery_provider *,
                                             const char *) noexcept
    {
        return nullptr;
    }

    void declare()
    {
        const auto file = settingsFile();
        locations_ = presetLocations(media::loadSettings(file), file);

        const clap_preset_discovery_filetype filetype{
            "Giga Grains preset", "A Giga Grains or Giga Grains FX preset", extension_.c_str()};
        indexer_->declare_filetype(indexer_, &filetype);

        paths_.clear();
        paths_.reserve(locations_.size());
        for (const auto &location : locations_)
        {
            // The user's folder is made here, so a host that caches the declarations sees the
            // first save; an error is ignored (the folder is declared all the same).
            if (!location.factory)
            {
                std::error_code error;
                fs::create_directories(location.folder, error);
            }
            const auto path = media::toUtf8(location.folder);
            if (!path)
            {
                continue; // a folder the indexer couldn't be given (Windows: not valid UTF-16)
            }
            paths_.push_back(*path);
            const clap_preset_discovery_location declared{
                location.factory ? uint32_t{CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT}
                                 : uint32_t{CLAP_PRESET_DISCOVERY_IS_USER_CONTENT},
                location.factory ? "Factory presets" : "User presets",
                CLAP_PRESET_DISCOVERY_LOCATION_FILE, paths_.back().c_str()};
            indexer_->declare_location(indexer_, &declared);
        }

        const clap_preset_discovery_location init{CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT, "Init",
                                                  CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr};
        indexer_->declare_location(indexer_, &init);

        contentVersion_.clear();
        for (const auto &location : locations_)
        {
            if (location.factory)
            {
                contentVersion_ = firstLine(media::contentVersionFile(location.root));
                break;
            }
        }
        const clap_preset_discovery_soundpack soundpack{CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT,
                                                        kSoundpackId,
                                                        "Giga Grains Factory",
                                                        contentVersion_.c_str(),
                                                        nullptr,
                                                        kVendor,
                                                        nullptr,
                                                        CLAP_TIMESTAMP_UNKNOWN};
        indexer_->declare_soundpack(indexer_, &soundpack);
    }

    /// INIT, the plugin's defaults: one preset, load key kInitLoadKey, for both products' ids.
    /// clap-wrapper's index is per plugin id and keeps a preset for every id it names, so each
    /// product's list holds one INIT. (Not one preset per product: two begin_preset calls in one
    /// get_metadata deadlock clap-validator 0.4.1's receiver, and the two would share one
    /// discovery identity, kind, location and load key.)
    static bool reportInit(const clap_preset_discovery_metadata_receiver *r)
    {
        const std::string loadKey(core::kInitLoadKey);
        if (!r->begin_preset(r, "Init", loadKey.c_str()))
        {
            return true;
        }
        for (const auto product : {Product::Instrument, Product::Fx})
        {
            const clap_universal_plugin_id id{kPluginAbi, descriptorFor(product)->id};
            r->add_plugin_id(r, &id);
        }
        r->set_flags(r, CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
        return true;
    }

    bool reportFile(const char *location, const clap_preset_discovery_metadata_receiver *r) const
    {
        const auto path = media::fromUtf8(location);
        if (!path)
        {
            r->on_error(r, 0, kUnreadable);
            return false;
        }
        const auto bytes = readPresetBytes(*path);
        if (!bytes)
        {
            r->on_error(r, 0, kUnreadable);
            return false;
        }
        core::PresetInfo info;
        if (!core::presetInfoOf(*bytes, info))
        {
            r->on_error(r, 0, kNotAPreset);
            return false;
        }

        if (!r->begin_preset(r, info.name.c_str(), nullptr))
        {
            return true;
        }
        const clap_universal_plugin_id id{kPluginAbi, descriptorFor(productOf(info.product))->id};
        r->add_plugin_id(r, &id);
        bool factory = false;
        for (const auto &l : locations_)
        {
            if (l.factory && isInside(*path, l.folder))
            {
                factory = true;
                break;
            }
        }
        if (factory)
        {
            r->set_soundpack_id(r, kSoundpackId);
        }
        r->set_flags(r, factory ? CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT
                                : CLAP_PRESET_DISCOVERY_IS_USER_CONTENT);
        if (!info.author.empty())
        {
            r->add_creator(r, info.author.c_str());
        }
        r->set_description(r, info.description.c_str());
        r->set_timestamps(r, CLAP_TIMESTAMP_UNKNOWN, modificationTime(*path));
        for (const auto &tag : info.tags)
        {
            r->add_feature(r, tag.c_str());
        }
        return true;
    }

    clap_preset_discovery_provider provider_{};
    const clap_preset_discovery_indexer *indexer_;
    /// The filetype's extension: kPresetExtension without its dot.
    const std::string extension_{core::kPresetExtension.substr(1)};
    std::vector<PresetLocation> locations_;
    std::vector<std::string> paths_;
    std::string contentVersion_;
};

// ---- clap_preset_discovery_factory ----

uint32_t CLAP_ABI providerCount(const clap_preset_discovery_factory *) noexcept { return 1; }

const clap_preset_discovery_provider_descriptor *CLAP_ABI
providerDescriptor(const clap_preset_discovery_factory *, uint32_t index) noexcept
{
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_preset_discovery_provider *CLAP_ABI
createProvider(const clap_preset_discovery_factory *, const clap_preset_discovery_indexer *indexer,
               const char *providerId) noexcept
{
    if (indexer == nullptr || providerId == nullptr || std::strcmp(providerId, kProviderId) != 0)
    {
        return nullptr;
    }
    try
    {
        // The provider's destroy deletes it.
        return (new Provider(indexer))->clap();
    }
    catch (...)
    {
        return nullptr;
    }
}

const clap_preset_discovery_factory kFactory{providerCount, providerDescriptor, createProvider};
} // namespace

const clap_preset_discovery_factory *presetDiscoveryFactory() noexcept { return &kFactory; }
} // namespace gg::plugin
