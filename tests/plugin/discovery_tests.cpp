// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The CLAP preset-discovery factory and its provider (M2 sub-project 5's spec §6.3), driven
// through getFactory as a host's indexer would, with a stub indexer and a stub metadata receiver
// that record every call. Each test's files go under GG_TEST_OUTPUT_DIR/discovery-<name>/: a
// content root (GG_CONTENT_DIR) whose Presets folder holds two presets and a junk file, and a
// settings file of its own (GG_SETTINGS_FILE), whose folder holds the user presets.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <clap/clap.h>

#include "Factory.h"
#include "GigaGrainsPlugin.h"
#include "PresetDiscovery.h"
#include "gg/core/Preset.h"
#include "gg/core/PresetInfo.h"
#include "gg/media/ContentPaths.h"
#include "gg/media/SettingsFile.h"
#include "gg/media/Utf8Path.h"

namespace fs = std::filesystem;
using gg::core::Mode;
using gg::core::PresetInfo;
using gg::plugin::descriptorFor;
using gg::plugin::Product;

namespace
{
/// Sets an environment variable for the test's process and restores it on scope exit. An empty
/// value unsets it.
class ScopedEnv
{
  public:
#if defined(_WIN32)
    ScopedEnv(const wchar_t *name, const fs::path &value) : name_(name)
    {
        if (const wchar_t *old = ::_wgetenv(name); old != nullptr)
        {
            old_ = std::wstring(old);
        }
        _wputenv_s(name_, value.c_str());
    }
    ~ScopedEnv() { _wputenv_s(name_, old_ ? old_->c_str() : L""); }
#else
    ScopedEnv(const char *name, const fs::path &value) : name_(name)
    {
        if (const char *old = std::getenv(name); old != nullptr)
        {
            old_ = std::string(old);
        }
        set(value.empty() ? nullptr : value.c_str());
    }
    ~ScopedEnv() { set(old_ ? old_->c_str() : nullptr); }
#endif
    ScopedEnv(const ScopedEnv &) = delete;
    ScopedEnv &operator=(const ScopedEnv &) = delete;

  private:
#if defined(_WIN32)
    const wchar_t *name_;
    std::optional<std::wstring> old_;
#else
    void set(const char *value)
    {
        if (value != nullptr)
        {
            ::setenv(name_, value, 1);
        }
        else
        {
            ::unsetenv(name_);
        }
    }
    const char *name_;
    std::optional<std::string> old_;
#endif
};

#if defined(_WIN32)
#define GG_ENV(name) L##name
#else
#define GG_ENV(name) name
#endif

/// `s` as a string; nothing for null. (Not a ternary: GCC 13 warns on a std::optional built in
/// one.)
std::optional<std::string> opt(const char *s)
{
    if (s == nullptr)
    {
        return std::nullopt;
    }
    return std::string(s);
}

/// What a provider declares in init.
struct Declared
{
    struct Filetype
    {
        std::string name, description, extension;
    };
    struct Location
    {
        uint32_t flags;
        std::string name;
        uint32_t kind;
        std::optional<std::string> location;
    };
    struct Soundpack
    {
        uint32_t flags;
        std::string id, name, description, vendor;
        clap_timestamp released;
    };

    std::vector<Filetype> filetypes;
    std::vector<Location> locations;
    std::vector<Soundpack> soundpacks;

    clap_preset_discovery_indexer indexer{CLAP_VERSION_INIT,
                                          "gg-tests stub indexer",
                                          "Giga Grains",
                                          "",
                                          "0.0.0",
                                          this,
                                          &Declared::declareFiletype,
                                          &Declared::declareLocation,
                                          &Declared::declareSoundpack,
                                          [](const clap_preset_discovery_indexer *,
                                             const char *) -> const void * { return nullptr; }};

    std::vector<Location> locationsOfKind(uint32_t kind) const
    {
        std::vector<Location> out;
        for (const auto &l : locations)
        {
            if (l.kind == kind)
            {
                out.push_back(l);
            }
        }
        return out;
    }

  private:
    static Declared &self(const clap_preset_discovery_indexer *i)
    {
        return *static_cast<Declared *>(i->indexer_data);
    }
    static std::string str(const char *s) { return s != nullptr ? s : "<null>"; }
    static bool declareFiletype(const clap_preset_discovery_indexer *i,
                                const clap_preset_discovery_filetype *f)
    {
        self(i).filetypes.push_back({str(f->name), str(f->description), str(f->file_extension)});
        return true;
    }
    static bool declareLocation(const clap_preset_discovery_indexer *i,
                                const clap_preset_discovery_location *l)
    {
        self(i).locations.push_back({l->flags, str(l->name), l->kind, opt(l->location)});
        return true;
    }
    static bool declareSoundpack(const clap_preset_discovery_indexer *i,
                                 const clap_preset_discovery_soundpack *s)
    {
        self(i).soundpacks.push_back({s->flags, str(s->id), str(s->name), str(s->description),
                                      str(s->vendor), s->release_timestamp});
        return true;
    }
};

/// What a provider reports through a metadata receiver.
struct Received
{
    struct Preset
    {
        std::optional<std::string> name, loadKey;
        std::vector<std::pair<std::string, std::string>> pluginIds;
        std::optional<std::string> soundpack;
        std::optional<uint32_t> flags;
        std::vector<std::string> creators;
        std::optional<std::string> description;
        std::optional<std::pair<clap_timestamp, clap_timestamp>> timestamps;
        std::vector<std::string> features;
    };

    std::vector<Preset> presets;
    std::vector<std::string> errors;
    int callsOutsidePreset = 0;

    clap_preset_discovery_metadata_receiver receiver{this,
                                                     &Received::onError,
                                                     &Received::beginPreset,
                                                     &Received::addPluginId,
                                                     &Received::setSoundpackId,
                                                     &Received::setFlags,
                                                     &Received::addCreator,
                                                     &Received::setDescription,
                                                     &Received::setTimestamps,
                                                     &Received::addFeature,
                                                     &Received::addExtraInfo};

  private:
    using R = clap_preset_discovery_metadata_receiver;
    static Received &self(const R *r) { return *static_cast<Received *>(r->receiver_data); }
    static Preset *current(const R *r)
    {
        auto &me = self(r);
        if (me.presets.empty())
        {
            ++me.callsOutsidePreset;
            return nullptr;
        }
        return &me.presets.back();
    }
    static void onError(const R *r, int32_t, const char *message)
    {
        self(r).errors.emplace_back(message != nullptr ? message : "<null>");
    }
    static bool beginPreset(const R *r, const char *name, const char *loadKey)
    {
        self(r).presets.push_back({opt(name), opt(loadKey), {}, {}, {}, {}, {}, {}, {}});
        return true;
    }
    static void addPluginId(const R *r, const clap_universal_plugin_id *id)
    {
        if (auto *p = current(r))
        {
            p->pluginIds.emplace_back(id->abi, id->id);
        }
    }
    static void setSoundpackId(const R *r, const char *id)
    {
        if (auto *p = current(r))
        {
            p->soundpack = id;
        }
    }
    static void setFlags(const R *r, uint32_t flags)
    {
        if (auto *p = current(r))
        {
            p->flags = flags;
        }
    }
    static void addCreator(const R *r, const char *creator)
    {
        if (auto *p = current(r))
        {
            p->creators.emplace_back(creator);
        }
    }
    static void setDescription(const R *r, const char *description)
    {
        if (auto *p = current(r))
        {
            p->description = description;
        }
    }
    static void setTimestamps(const R *r, clap_timestamp created, clap_timestamp modified)
    {
        if (auto *p = current(r))
        {
            p->timestamps = {created, modified};
        }
    }
    static void addFeature(const R *r, const char *feature)
    {
        if (auto *p = current(r))
        {
            p->features.emplace_back(feature);
        }
    }
    static void addExtraInfo(const R *, const char *, const char *) {}
};

const clap_preset_discovery_factory *discoveryFactory()
{
    return static_cast<const clap_preset_discovery_factory *>(
        gg::plugin::getFactory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
}

std::string utf8(const fs::path &p) { return gg::media::toUtf8(p).value(); }

void writeFile(const fs::path &file, const std::string &bytes)
{
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(out.good());
}

PresetInfo fxInfo()
{
    return PresetInfo{"Glass Hold",
                      Mode::Fx,
                      "Giga Grains",
                      "A held, glassy freeze \xc3\xa4.",
                      {"texture", "freeze", "live-input"}};
}

PresetInfo instrumentInfo()
{
    return PresetInfo{"Bowed Bed", Mode::Instrument, "", "Slow bowed grains.", {"strings"}};
}

/// One test's scratch tree: a content root with its Presets (two presets and a junk file) and its
/// content-version.txt, a settings file's folder, and HOME and the platform's install roots
/// pointed inside it so that no per-user install of the machine's is found.
struct Scratch
{
    fs::path dir, content, settingsFile, fxPreset, instrumentPreset, junk;

    explicit Scratch(const std::string &name)
        : dir(fs::path(GG_TEST_OUTPUT_DIR) / ("discovery-" + name)), content(dir / "content"),
          settingsFile(dir / "settings" / "settings.xml"),
          fxPreset(content / "Presets" / "Glass Hold.ggpreset"),
          instrumentPreset(content / "Presets" /
                           gg::media::fromUtf8("Bowed Bed \xc3\xa4.ggpreset").value()),
          junk(content / "Presets" / "junk.ggpreset")
    {
        fs::remove_all(dir);
        writeFile(fxPreset, gg::core::encodePreset(fxInfo(), gg::core::initState(Mode::Fx)));
        writeFile(instrumentPreset,
                  gg::core::encodePreset(instrumentInfo(), gg::core::initState(Mode::Instrument)));
        writeFile(junk, "not a preset at all");
        writeFile(content / "content-version.txt",
                  "giga-grains-content v1 0123abcd\r\nsecond line\n");
        fs::create_directories(dir / "home");
    }
};

/// The environment a test's provider reads: the content root (or none), the settings file and
/// the install roots, all inside the scratch tree.
struct Environment
{
    ScopedEnv content, settings;
#if defined(_WIN32)
    ScopedEnv local, common;
#else
    ScopedEnv home, xdgHome, xdgDirs;
#endif

    Environment(const Scratch &s, bool withContent)
        : content(GG_ENV("GG_CONTENT_DIR"), withContent ? s.content : fs::path()),
          settings(GG_ENV("GG_SETTINGS_FILE"), s.settingsFile),
#if defined(_WIN32)
          local(GG_ENV("LOCALAPPDATA"), s.dir / "home"),
          common(GG_ENV("ProgramData"), s.dir / "home")
#else
          home(GG_ENV("HOME"), s.dir / "home"), xdgHome(GG_ENV("XDG_DATA_HOME"), s.dir / "home"),
          xdgDirs(GG_ENV("XDG_DATA_DIRS"), s.dir / "home")
#endif
    {
    }
};

/// A provider created and initialized through the factory, destroyed on scope exit.
struct Provider
{
    Declared declared;
    const clap_preset_discovery_provider *provider = nullptr;

    Provider()
    {
        const auto *factory = discoveryFactory();
        REQUIRE(factory != nullptr);
        provider =
            factory->create(factory, &declared.indexer, factory->get_descriptor(factory, 0)->id);
        REQUIRE(provider != nullptr);
        REQUIRE(provider->init(provider));
    }
    ~Provider() { provider->destroy(provider); }
    Provider(const Provider &) = delete;
    Provider &operator=(const Provider &) = delete;

    bool metadataOf(const fs::path &file, Received &received) const
    {
        const std::string location = utf8(file);
        return provider->get_metadata(provider, CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                                      location.c_str(), &received.receiver);
    }
};

bool isUnder(const std::string &location, const fs::path &folder)
{
    const auto path = gg::media::fromUtf8(location).value();
    const auto relative = path.lexically_relative(folder);
    return !relative.empty() && *relative.begin() != "..";
}
} // namespace

TEST_CASE("the preset-discovery factory answers both ids with one provider", "[plugin][discovery]")
{
    const void *factory = gg::plugin::getFactory(CLAP_PRESET_DISCOVERY_FACTORY_ID);
    REQUIRE(factory != nullptr);
    CHECK(gg::plugin::getFactory(CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT) == factory);
    CHECK(factory == gg::plugin::presetDiscoveryFactory());

    const auto *discovery = discoveryFactory();
    REQUIRE(discovery->count(discovery) == 1);
    const auto *descriptor = discovery->get_descriptor(discovery, 0);
    REQUIRE(descriptor != nullptr);
    CHECK(clap_version_is_compatible(descriptor->clap_version));
    CHECK(std::string(descriptor->id) == "org.sethbc.giga-grains.presets");
    CHECK(std::string(descriptor->name) == "Giga Grains presets");
    CHECK(std::string(descriptor->vendor) == "sethbc");
    CHECK(discovery->get_descriptor(discovery, 1) == nullptr);

    Declared declared;
    CHECK(discovery->create(discovery, &declared.indexer, "org.example.other") == nullptr);
    CHECK(discovery->create(discovery, &declared.indexer, nullptr) == nullptr);
    CHECK(discovery->create(discovery, nullptr, descriptor->id) == nullptr);
    const auto *provider = discovery->create(discovery, &declared.indexer, descriptor->id);
    REQUIRE(provider != nullptr);
    CHECK(provider->desc == descriptor);
    CHECK(provider->get_extension(provider, "anything") == nullptr);
    provider->destroy(provider);

    // The plugin factories are unchanged beside it, and the entry's init and deinit still return.
    CHECK(gg::plugin::getFactory(CLAP_PLUGIN_FACTORY_ID) != nullptr);
    CHECK(gg::plugin::getFactory("clap.preset-discovery-factory/1") == nullptr);
    CHECK(gg::plugin::clapInit(""));
    gg::plugin::clapDeinit();
}

TEST_CASE("the provider's init declares the filetype, the locations and the soundpack",
          "[plugin][discovery]")
{
    const Scratch scratch("init");
    const Environment env(scratch, true);
    fs::create_directories(scratch.settingsFile.parent_path() / "Presets");
    const Provider p;
    const auto &d = p.declared;

    REQUIRE(d.filetypes.size() == 1);
    CHECK(d.filetypes[0].name == "Giga Grains preset");
    CHECK(d.filetypes[0].description == "A Giga Grains or Giga Grains FX preset");
    CHECK(d.filetypes[0].extension == "ggpreset");

    // The content root's Presets first (a machine-wide install, which the test can't move, may
    // follow it), then the user presets folder beside the settings file, then Init.
    const auto files = d.locationsOfKind(CLAP_PRESET_DISCOVERY_LOCATION_FILE);
    REQUIRE(files.size() >= 2);
    CHECK(files.front().location == utf8(scratch.content / "Presets"));
    CHECK(files.front().flags == CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
    std::size_t underScratch = 0;
    for (std::size_t i = 0; i + 1 < files.size(); ++i)
    {
        CHECK(files[i].flags == CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
        underScratch += isUnder(*files[i].location, scratch.dir) ? 1 : 0;
    }
    CHECK(underScratch == 1);
    CHECK(files.back().location == utf8(scratch.settingsFile.parent_path() / "Presets"));
    CHECK(files.back().flags == CLAP_PRESET_DISCOVERY_IS_USER_CONTENT);

    const auto plugins = d.locationsOfKind(CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN);
    REQUIRE(plugins.size() == 1);
    CHECK(plugins[0].name == "Init");
    CHECK_FALSE(plugins[0].location.has_value());
    CHECK(plugins[0].flags == CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
    CHECK(d.locations.size() == files.size() + 1);

    REQUIRE(d.soundpacks.size() == 1);
    CHECK(d.soundpacks[0].id == "org.sethbc.giga-grains.factory");
    CHECK(d.soundpacks[0].name == "Giga Grains Factory");
    CHECK(d.soundpacks[0].vendor == "sethbc");
    CHECK(d.soundpacks[0].description == "giga-grains-content v1 0123abcd");
    CHECK(d.soundpacks[0].flags == CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
    CHECK(d.soundpacks[0].released == CLAP_TIMESTAMP_UNKNOWN);
}

TEST_CASE("get_metadata reports a factory preset's metadata", "[plugin][discovery]")
{
    const Scratch scratch("factory");
    const Environment env(scratch, true);
    const Provider p;

    Received fx;
    REQUIRE(p.metadataOf(scratch.fxPreset, fx));
    CHECK(fx.errors.empty());
    CHECK(fx.callsOutsidePreset == 0);
    REQUIRE(fx.presets.size() == 1);
    const auto &preset = fx.presets[0];
    CHECK(preset.name == "Glass Hold");
    CHECK_FALSE(preset.loadKey.has_value());
    REQUIRE(preset.pluginIds.size() == 1);
    CHECK(preset.pluginIds[0].first == "clap");
    CHECK(preset.pluginIds[0].second == descriptorFor(Product::Fx)->id);
    CHECK(preset.soundpack == "org.sethbc.giga-grains.factory");
    CHECK(preset.flags == CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
    CHECK(preset.creators == std::vector<std::string>{"Giga Grains"});
    CHECK(preset.description == "A held, glassy freeze \xc3\xa4.");
    CHECK(preset.features == std::vector<std::string>{"texture", "freeze", "live-input"});
    REQUIRE(preset.timestamps.has_value());
    CHECK(preset.timestamps->first == CLAP_TIMESTAMP_UNKNOWN);
    // Written a moment ago: the modification time is now, in seconds since the epoch.
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    CHECK(preset.timestamps->second > static_cast<clap_timestamp>(now - 86400));
    CHECK(preset.timestamps->second < static_cast<clap_timestamp>(now + 86400));

    // The instrument's preset, at a non-ASCII path, names the instrument and no creator.
    Received inst;
    REQUIRE(p.metadataOf(scratch.instrumentPreset, inst));
    REQUIRE(inst.presets.size() == 1);
    CHECK(inst.presets[0].name == "Bowed Bed");
    REQUIRE(inst.presets[0].pluginIds.size() == 1);
    CHECK(inst.presets[0].pluginIds[0].second == descriptorFor(Product::Instrument)->id);
    CHECK(inst.presets[0].creators.empty());
    CHECK(inst.presets[0].features == std::vector<std::string>{"strings"});
}

TEST_CASE("get_metadata of a file that isn't a preset reports an error and no preset",
          "[plugin][discovery]")
{
    const Scratch scratch("junk");
    const Environment env(scratch, true);
    const Provider p;

    Received junk;
    CHECK_FALSE(p.metadataOf(scratch.junk, junk));
    CHECK(junk.presets.empty());
    CHECK(junk.callsOutsidePreset == 0);
    CHECK(junk.errors == std::vector<std::string>{"not a Giga Grains preset"});

    // A session state (no <preset> element) isn't a preset either, nor is a missing file.
    const auto session = scratch.content / "Presets" / "session.ggpreset";
    writeFile(session, gg::core::encodeState(gg::core::initState(Mode::Fx)));
    Received sessionReceived;
    CHECK_FALSE(p.metadataOf(session, sessionReceived));
    CHECK(sessionReceived.presets.empty());
    CHECK(sessionReceived.errors.size() == 1);

    Received missing;
    CHECK_FALSE(p.metadataOf(scratch.content / "Presets" / "gone.ggpreset", missing));
    CHECK(missing.presets.empty());
    CHECK(missing.errors.size() == 1);

    // A FILE location of null is refused without a call.
    Received none;
    CHECK_FALSE(p.provider->get_metadata(p.provider, CLAP_PRESET_DISCOVERY_LOCATION_FILE, nullptr,
                                         &none.receiver));
    CHECK(none.presets.empty());
}

TEST_CASE("get_metadata of the plugin location reports Init for both products",
          "[plugin][discovery]")
{
    const Scratch scratch("init-presets");
    const Environment env(scratch, true);
    const Provider p;

    // One preset naming both plugin ids: clap-wrapper's index is per plugin id and keeps a
    // preset for every id it names, so each product's list holds one INIT. (Two begin_preset
    // calls in one get_metadata deadlock clap-validator 0.4.1's receiver.)
    Received init;
    REQUIRE(p.provider->get_metadata(p.provider, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr,
                                     &init.receiver));
    CHECK(init.errors.empty());
    CHECK(init.callsOutsidePreset == 0);
    REQUIRE(init.presets.size() == 1);
    CHECK(init.presets[0].name == "Init");
    CHECK(init.presets[0].loadKey == std::string(gg::core::kInitLoadKey));
    CHECK(init.presets[0].flags == CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
    using Id = std::pair<std::string, std::string>;
    CHECK(init.presets[0].pluginIds ==
          std::vector<Id>{{"clap", descriptorFor(Product::Instrument)->id},
                          {"clap", descriptorFor(Product::Fx)->id}});

    // A PLUGIN location that isn't null is refused without a call.
    Received named;
    CHECK_FALSE(p.provider->get_metadata(p.provider, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, "x",
                                         &named.receiver));
    CHECK(named.presets.empty());
}

TEST_CASE("a user preset is user content, outside the soundpack", "[plugin][discovery]")
{
    const Scratch scratch("user");
    const Environment env(scratch, true);
    const auto user = scratch.settingsFile.parent_path() / "Presets" / "Mine" / "Mine.ggpreset";
    writeFile(user, gg::core::encodePreset(fxInfo(), gg::core::initState(Mode::Fx)));
    const Provider p;

    Received mine;
    REQUIRE(p.metadataOf(user, mine));
    REQUIRE(mine.presets.size() == 1);
    CHECK(mine.presets[0].flags == CLAP_PRESET_DISCOVERY_IS_USER_CONTENT);
    CHECK_FALSE(mine.presets[0].soundpack.has_value());
}

TEST_CASE("without a content root there is no factory location and the rest is declared",
          "[plugin][discovery]")
{
    const Scratch scratch("no-content");
    const Environment env(scratch, false);
    fs::create_directories(scratch.settingsFile.parent_path() / "Presets");
    const Provider p;
    const auto &d = p.declared;

    CHECK(d.filetypes.size() == 1);
    const auto files = d.locationsOfKind(CLAP_PRESET_DISCOVERY_LOCATION_FILE);
    REQUIRE(!files.empty());
    bool anyFactory = false;
    for (const auto &l : files)
    {
        CHECK_FALSE(isUnder(*l.location, scratch.content));
        anyFactory = anyFactory || l.flags == CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT;
    }
    CHECK(files.back().location == utf8(scratch.settingsFile.parent_path() / "Presets"));
    CHECK(files.back().flags == CLAP_PRESET_DISCOVERY_IS_USER_CONTENT);
    CHECK(d.locationsOfKind(CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN).size() == 1);
    REQUIRE(d.soundpacks.size() == 1);
    CHECK(d.soundpacks[0].id == "org.sethbc.giga-grains.factory");
    if (!anyFactory) // a machine-wide install, which the test can't move, would give its version
    {
        CHECK(d.soundpacks[0].description.empty());
    }
}

TEST_CASE("init creates the user folder and declares it", "[plugin][discovery]")
{
    // A host that caches the declarations must see the first save, so init makes the folder.
    const Scratch scratch("no-user-folder");
    const Environment env(scratch, true);
    const auto user = scratch.settingsFile.parent_path() / "Presets";
    REQUIRE_FALSE(fs::exists(user));
    const Provider p;

    CHECK(fs::is_directory(user));
    const auto files = p.declared.locationsOfKind(CLAP_PRESET_DISCOVERY_LOCATION_FILE);
    REQUIRE(!files.empty());
    CHECK(files.front().location == utf8(scratch.content / "Presets"));
    CHECK(files.back().location == utf8(user));
    CHECK(files.back().flags == CLAP_PRESET_DISCOVERY_IS_USER_CONTENT);
    const auto listed = gg::plugin::presetLocations(gg::media::loadSettings(scratch.settingsFile),
                                                    scratch.settingsFile);
    REQUIRE(!listed.empty());
    CHECK(listed.back().folder == user);
    CHECK_FALSE(listed.back().factory);
}

TEST_CASE("every FILE location is an absolute path in the OS's own form", "[plugin][discovery]")
{
    // The CLAP spec (preset-discovery.h, CLAP_PRESET_DISCOVERY_LOCATION_FILE): "a path which
    // works with the OS file system functions (open, stat, ...)", with '/' and '\' both separators
    // on Windows. So a Windows location starts with its drive, never with a '/' (clap-validator
    // 0.4.1-127 asks for '/C:\...', which no Windows file function opens); a POSIX one with '/'.
    const Scratch scratch("native-form");
    const Environment env(scratch, true);
    const Provider p;
    const auto files = p.declared.locationsOfKind(CLAP_PRESET_DISCOVERY_LOCATION_FILE);
    REQUIRE(files.size() >= 2);
    for (const auto &l : files)
    {
        REQUIRE(l.location.has_value());
        const std::string &location = *l.location;
        CAPTURE(location);
        REQUIRE(location.size() > 3);
        CHECK(gg::media::fromUtf8(location).value().is_absolute());
#if defined(_WIN32)
        const char drive = location[0];
        CHECK(((drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z')));
        CHECK(location[1] == ':');
        CHECK((location[2] == '\\' || location[2] == '/'));
#else
        CHECK(location[0] == '/');
#endif
    }

    // A host crawls a declared folder and hands back each file as the folder's location, a
    // separator (on Windows the native '\', as std::filesystem and Rust's walkers join) and the
    // name: get_metadata takes that form.
#if defined(_WIN32)
    const std::string separator = "\\";
#else
    const std::string separator = "/";
#endif
    const std::string crawled = *files.front().location + separator + "Glass Hold.ggpreset";
    Received received;
    REQUIRE(p.provider->get_metadata(p.provider, CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                                     crawled.c_str(), &received.receiver));
    CHECK(received.errors.empty());
    REQUIRE(received.presets.size() == 1);
    CHECK(received.presets[0].name == "Glass Hold");
    CHECK(received.presets[0].flags == CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
}

TEST_CASE("a content-version.txt line longer than the cap is cut", "[plugin][discovery]")
{
    const Scratch scratch("long-version");
    const Environment env(scratch, true);
    writeFile(scratch.content / "content-version.txt", std::string(5000, 'v'));
    const Provider p;

    REQUIRE(p.declared.soundpacks.size() == 1);
    const auto &description = p.declared.soundpacks[0].description;
    CHECK(description == std::string(1024, 'v'));
}

TEST_CASE("presetLocations lists each content root's Presets, then the user's",
          "[plugin][discovery]")
{
    const Scratch scratch("locations");
    const Environment env(scratch, true);
    // A second root without a Presets folder adds nothing; a third with one adds it, in order.
    const auto empty = scratch.dir / "empty-root";
    const auto pack = scratch.dir / gg::media::fromUtf8("pack \xe2\x9c\x93").value();
    fs::create_directories(empty);
    fs::create_directories(pack / "Presets");

    gg::core::MachineSettings settings;
    settings.contentFolder = utf8(empty);
    auto locations = gg::plugin::presetLocations(settings, scratch.settingsFile);
    REQUIRE(locations.size() >= 2);
    CHECK(locations.front().folder == scratch.content / "Presets");
    CHECK(locations.front().root == scratch.content);
    CHECK(locations.front().factory);
    CHECK(locations.back().folder == gg::media::userPresetsFolder(scratch.settingsFile));
    CHECK_FALSE(locations.back().factory);
    CHECK(locations.back().root.empty());

    settings.contentFolder = utf8(pack);
    locations = gg::plugin::presetLocations(settings, scratch.settingsFile);
    REQUIRE(locations.size() >= 3);
    CHECK(locations[0].folder == scratch.content / "Presets");
    CHECK(locations[1].folder == pack / "Presets");
    CHECK(locations[1].root == pack);
    CHECK(locations[1].factory);
}
