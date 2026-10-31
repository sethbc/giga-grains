// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The preset browser's model (plugin/src/PresetCatalog.h; the presets spec §6.8) and the
// favorites file (plugin/src/Favorites.h): the list's order and product filter, the search, tag
// and favorites filters, favorites kept across catalogs, Save as… (the file name made safe, an
// overwrite refused unless asked), Delete, the content status line and the per-folder cap. Each
// test case works in a folder of its own under GG_TEST_OUTPUT_DIR, with GG_CONTENT_DIR,
// GG_SETTINGS_FILE and the install roots (GG_SYSTEM_CONTENT_DIR, ContentPaths.h's test seam)
// pointed into it (restored when it ends), so no installed content or user preset is ever seen;
// each case runs in a process of its own.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Favorites.h"
#include "PresetCatalog.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Preset.h"
#include "gg/core/PresetInfo.h"
#include "gg/media/ContentPaths.h"
#include "gg/media/Utf8Path.h"
#include "media/AudioFixtures.h"
#include "support/Utf8.h"

namespace
{
namespace fs = std::filesystem;
using gg::core::Mode;
using gg::core::PresetInfo;
using gg::plugin::PresetCatalog;
using gg::plugin::PresetKind;
using gg::test::pathOf;
using gg::test::utf8;

/// Sets (or, given nullptr, unsets) an environment variable for its lifetime. Names are ASCII,
/// values UTF-8.
class ScopedEnv
{
  public:
    ScopedEnv(const char *name, const char *value) : name_(name)
    {
        if (const auto old = get(); old)
        {
            old_ = old;
        }
        set(value);
    }
    ~ScopedEnv() { set(old_ ? old_->c_str() : nullptr); }
    ScopedEnv(const ScopedEnv &) = delete;
    ScopedEnv &operator=(const ScopedEnv &) = delete;

  private:
#if defined(_WIN32)
    static std::wstring wide(std::string_view text)
    {
        return std::wstring(text.begin(), text.end());
    }
    std::optional<std::string> get() const
    {
        const wchar_t *value = ::_wgetenv(wide(name_).c_str());
        if (value == nullptr)
        {
            return std::nullopt;
        }
        return utf8(fs::path(value));
    }
    void set(const char *value) const
    {
        // An empty value removes the variable: Windows can't hold an empty one.
        (void)::_wputenv_s(wide(name_).c_str(), value != nullptr ? pathOf(value).c_str() : L"");
    }
#else
    std::optional<std::string> get() const
    {
        const char *value = std::getenv(name_);
        return value != nullptr ? std::optional<std::string>(value) : std::nullopt;
    }
    void set(const char *value) const
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
#endif
    const char *name_;
    std::optional<std::string> old_;
};

/// A scratch machine of the test's own: `<root>/content` is GG_CONTENT_DIR (its `Presets`
/// folder made only when a test asks), `<root>/settings/settings.xml` the settings file
/// (GG_SETTINGS_FILE, so the user presets folder is `<root>/settings/Presets`), and the install
/// roots are `<root>/home/system` (GG_SYSTEM_CONTENT_DIR), which holds nothing.
class Machine
{
  public:
    explicit Machine(const char *name)
        : root_(fresh(name)), contentEnv_("GG_CONTENT_DIR", utf8(content()).c_str()),
          settingsEnv_("GG_SETTINGS_FILE", utf8(settingsFile()).c_str()),
          systemEnv_("GG_SYSTEM_CONTENT_DIR", utf8(root_ / "home" / "system").c_str())
    {
    }

    fs::path root() const { return root_; }
    fs::path content() const { return root_ / "content"; }
    fs::path factory() const { return gg::media::factoryPresetsFolder(content()); }
    fs::path settingsFile() const { return root_ / "settings" / "settings.xml"; }
    fs::path user() const { return gg::media::userPresetsFolder(settingsFile()); }
    fs::path favorites() const { return gg::media::favoritesFile(settingsFile()); }

  private:
    static fs::path fresh(const char *name)
    {
        const auto dir = fs::path(GG_TEST_OUTPUT_DIR) / "catalog" / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        return dir;
    }

    fs::path root_;
    ScopedEnv contentEnv_;
    ScopedEnv settingsEnv_;
    ScopedEnv systemEnv_; // the install roots (the tests' seam, ContentPaths.h)
};

PresetInfo infoOf(std::string name, Mode product, std::vector<std::string> tags = {},
                  std::string description = {})
{
    PresetInfo info;
    info.name = std::move(name);
    info.product = product;
    info.author = "gg-tests";
    info.description = std::move(description);
    info.tags = std::move(tags);
    return info;
}

/// The preset file of `info` over the INIT state of its product.
std::string presetBytes(const PresetInfo &info)
{
    return gg::core::encodePreset(info, gg::core::initState(info.product));
}

void putBytes(const fs::path &file, std::string_view bytes)
{
    fs::create_directories(file.parent_path());
    gg::test::writeFile(file,
                        std::span(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()));
}

void putPreset(const fs::path &file, const PresetInfo &info) { putBytes(file, presetBytes(info)); }

std::string readText(const fs::path &file)
{
    const auto bytes = gg::test::readFile(file);
    return std::string(bytes.begin(), bytes.end());
}

std::vector<std::string> namesOf(const PresetCatalog &catalog)
{
    std::vector<std::string> names;
    for (const auto &entry : catalog.entries())
    {
        names.push_back(entry.info.name);
    }
    return names;
}

std::vector<std::string> namesAt(const PresetCatalog &catalog, const std::vector<size_t> &indices)
{
    std::vector<std::string> names;
    for (const auto index : indices)
    {
        names.push_back(catalog.entries().at(index).info.name);
    }
    return names;
}

/// The index of the entry named `name`.
size_t indexOf(const PresetCatalog &catalog, std::string_view name)
{
    const auto &entries = catalog.entries();
    const auto found = std::find_if(entries.begin(), entries.end(),
                                    [&](const auto &entry) { return entry.info.name == name; });
    REQUIRE(found != entries.end());
    return static_cast<size_t>(found - entries.begin());
}

std::string fileKey(const fs::path &file)
{
    return "file:" + gg::media::toGenericUtf8(file).value();
}

/// Every regular file under `folder`, recursively.
std::vector<fs::path> filesUnder(const fs::path &folder)
{
    std::vector<fs::path> files;
    for (const auto &entry : fs::recursive_directory_iterator(folder))
    {
        if (entry.is_regular_file())
        {
            files.push_back(entry.path());
        }
    }
    return files;
}

/// The FX and instrument presets the order and filter tests share: four FX and one instrument
/// preset in the factory folder (with a file that isn't a preset, one in another case of the
/// extension, one in a subfolder and a text file), three FX (one in a subfolder) and one
/// instrument preset in the user folder.
void putLibrary(const Machine &machine)
{
    putPreset(machine.factory() / "02 Shimmer.ggpreset",
              infoOf("Shimmer", Mode::Fx, {"texture", "bright", "reverb"}, "A bright wash"));
    putPreset(machine.factory() / "01 Smear Pad.ggpreset",
              infoOf("Smear Pad", Mode::Fx, {"texture", "pad", "smear"}, "Slow and wide"));
    putPreset(machine.factory() / "03 Stutter.ggpreset",
              infoOf("Stutter", Mode::Fx, {"rhythmic", "glitch"}, "Chopped repeats"));
    putPreset(machine.factory() / "04 Keys.ggpreset",
              infoOf("Bell Keys", Mode::Instrument, {"playable", "keys"}, "Struck"));
    putPreset(machine.factory() / "05 Upper.GGPRESET", infoOf("Upper", Mode::Fx));
    putBytes(machine.factory() / "06 Junk.ggpreset", "not a preset at all");
    putBytes(machine.factory() / "readme.txt", "notes");
    putPreset(machine.user() / "b mine.ggpreset",
              infoOf("Mine", Mode::Fx, {"dark", "texture"}, "My own SHIMMER"));
    putPreset(machine.user() / "a drone.ggpreset", infoOf("Drone", Mode::Fx, {"drone"}));
    putPreset(machine.user() / "c lead.ggpreset", infoOf("Lead", Mode::Instrument, {"playable"}));
    // A subfolder's presets are listed too, by their path relative to the folder (I2), as the
    // hosts' crawl lists them.
    putPreset(machine.factory() / "Sub" / "07 Deep.ggpreset", infoOf("Deep", Mode::Fx));
    putPreset(machine.user() / "d folder" / "nested.ggpreset", infoOf("Nested", Mode::Fx));
}
} // namespace

TEST_CASE("the catalog lists Init, the factory presets by file name, then the user presets",
          "[plugin][catalog]")
{
    const Machine machine("order");
    putLibrary(machine);

    const PresetCatalog fx(Mode::Fx, machine.settingsFile());
    // The extension matched in any case (05 Upper.GGPRESET), the subfolders walked, and each
    // folder's files in the byte order of their relative paths.
    REQUIRE(namesOf(fx) == std::vector<std::string>{"Init", "Smear Pad", "Shimmer", "Stutter",
                                                    "Upper", "Deep", "Drone", "Mine", "Nested"});
    const auto &entries = fx.entries();
    REQUIRE(entries[0].kind == PresetKind::Init);
    REQUIRE(entries[0].file.empty());
    REQUIRE(entries[0].info.product == Mode::Fx);
    REQUIRE(entries[0].info.tags.empty());
    REQUIRE(!entries[0].favorite);
    REQUIRE(entries[1].kind == PresetKind::Factory);
    REQUIRE(entries[1].file == machine.factory() / "01 Smear Pad.ggpreset");
    REQUIRE(entries[1].info.description == "Slow and wide");
    REQUIRE(entries[1].info.tags == std::vector<std::string>{"texture", "pad", "smear"});
    REQUIRE(entries[3].kind == PresetKind::Factory);
    REQUIRE(entries[4].file == machine.factory() / "05 Upper.GGPRESET");
    REQUIRE(entries[5].kind == PresetKind::Factory);
    REQUIRE(entries[5].file == machine.factory() / "Sub" / "07 Deep.ggpreset");
    REQUIRE(entries[6].kind == PresetKind::User);
    REQUIRE(entries[6].file == machine.user() / "a drone.ggpreset");
    REQUIRE(entries[7].kind == PresetKind::User);
    REQUIRE(entries[8].file == machine.user() / "d folder" / "nested.ggpreset");

    // The instrument lists its own presets alone (D9, option 1).
    const PresetCatalog instrument(Mode::Instrument, machine.settingsFile());
    REQUIRE(namesOf(instrument) == std::vector<std::string>{"Init", "Bell Keys", "Lead"});
    REQUIRE(instrument.entries()[0].info.product == Mode::Instrument);
    REQUIRE(instrument.entries()[1].kind == PresetKind::Factory);
    REQUIRE(instrument.entries()[2].kind == PresetKind::User);

    REQUIRE(gg::plugin::presetKey(entries[0]) == "plugin:init");
    REQUIRE(gg::plugin::presetKey(entries[1]) == fileKey(entries[1].file));
}

TEST_CASE("rescan picks up a preset added after the catalog was made", "[plugin][catalog]")
{
    const Machine machine("rescan");
    PresetCatalog fx(Mode::Fx, machine.settingsFile());
    REQUIRE(namesOf(fx) == std::vector<std::string>{"Init"});
    putPreset(machine.factory() / "Late.ggpreset", infoOf("Late", Mode::Fx));
    REQUIRE(namesOf(fx) == std::vector<std::string>{"Init"});
    fx.rescan();
    REQUIRE(namesOf(fx) == std::vector<std::string>{"Init", "Late"});
}

TEST_CASE("filtered matches the search, every tag and the favorites", "[plugin][catalog]")
{
    const Machine machine("filtered");
    putLibrary(machine);
    PresetCatalog fx(Mode::Fx, machine.settingsFile());
    const std::vector<std::string> none;

    // Nothing asked: everything, Init included, in the list's order.
    REQUIRE(namesAt(fx, fx.filtered("", none, false)) == namesOf(fx));

    // The search: a substring of the name, the description or a tag, in any case.
    REQUIRE(namesAt(fx, fx.filtered("shim", none, false)) ==
            std::vector<std::string>{"Shimmer", "Mine"});
    REQUIRE(namesAt(fx, fx.filtered("WIDE", none, false)) == std::vector<std::string>{"Smear Pad"});
    REQUIRE(namesAt(fx, fx.filtered("glit", none, false)) == std::vector<std::string>{"Stutter"});
    REQUIRE(namesAt(fx, fx.filtered("init", none, false)) == std::vector<std::string>{"Init"});
    REQUIRE(fx.filtered("no such words", none, false).empty());

    // The tags: every one given is required, exactly.
    const std::vector<std::string> texture{"texture"};
    REQUIRE(namesAt(fx, fx.filtered("", texture, false)) ==
            std::vector<std::string>{"Smear Pad", "Shimmer", "Mine"});
    const std::vector<std::string> texturePad{"texture", "pad"};
    REQUIRE(namesAt(fx, fx.filtered("", texturePad, false)) ==
            std::vector<std::string>{"Smear Pad"});
    const std::vector<std::string> textureRhythmic{"texture", "rhythmic"};
    REQUIRE(fx.filtered("", textureRhythmic, false).empty());
    REQUIRE(namesAt(fx, fx.filtered("shim", texture, false)) ==
            std::vector<std::string>{"Shimmer", "Mine"});

    // The favorites.
    REQUIRE(fx.filtered("", none, true).empty());
    REQUIRE(fx.setFavorite(indexOf(fx, "Stutter"), true));
    REQUIRE(fx.setFavorite(indexOf(fx, "Init"), true));
    REQUIRE(namesAt(fx, fx.filtered("", none, true)) ==
            std::vector<std::string>{"Init", "Stutter"});
    REQUIRE(namesAt(fx, fx.filtered("stu", none, true)) == std::vector<std::string>{"Stutter"});
    const std::vector<std::string> rhythmic{"rhythmic"};
    REQUIRE(namesAt(fx, fx.filtered("", rhythmic, true)) == std::vector<std::string>{"Stutter"});
}

TEST_CASE("tagsInUse is the listed presets' tags in the vocabulary's order", "[plugin][catalog]")
{
    const Machine machine("tags");
    putLibrary(machine);
    const PresetCatalog fx(Mode::Fx, machine.settingsFile());
    REQUIRE(fx.tagsInUse() == std::vector<std::string>{"texture", "rhythmic", "pad", "drone",
                                                       "smear", "glitch", "dark", "bright",
                                                       "reverb"});
    const PresetCatalog instrument(Mode::Instrument, machine.settingsFile());
    REQUIRE(instrument.tagsInUse() == std::vector<std::string>{"playable", "keys"});
}

TEST_CASE("a favorite persists across catalogs, one key per line", "[plugin][catalog]")
{
    const Machine machine("favorites");
    putLibrary(machine);
    // Another product's favorite, already in the file, survives this one's saves.
    putBytes(machine.favorites(), "file:/elsewhere/Other.ggpreset\n\n");

    {
        PresetCatalog fx(Mode::Fx, machine.settingsFile());
        REQUIRE(fx.setFavorite(indexOf(fx, "Shimmer"), true));
        REQUIRE(fx.setFavorite(indexOf(fx, "Init"), true));
        REQUIRE(fx.setFavorite(indexOf(fx, "Mine"), true));
        REQUIRE(fx.setFavorite(indexOf(fx, "Mine"), true)); // no duplicate
        REQUIRE(fx.entries()[indexOf(fx, "Shimmer")].favorite);
        REQUIRE(!fx.setFavorite(fx.entries().size(), true));
    }
    const std::string shimmer = fileKey(machine.factory() / "02 Shimmer.ggpreset");
    const std::string mine = fileKey(machine.user() / "b mine.ggpreset");
    REQUIRE(readText(machine.favorites()) ==
            "file:/elsewhere/Other.ggpreset\n" + shimmer + "\nplugin:init\n" + mine + "\n");
    REQUIRE(
        gg::plugin::loadFavorites(machine.favorites()) ==
        std::vector<std::string>{"file:/elsewhere/Other.ggpreset", shimmer, "plugin:init", mine});

    PresetCatalog again(Mode::Fx, machine.settingsFile());
    REQUIRE(again.entries()[indexOf(again, "Init")].favorite);
    REQUIRE(again.entries()[indexOf(again, "Shimmer")].favorite);
    REQUIRE(again.entries()[indexOf(again, "Mine")].favorite);
    REQUIRE(!again.entries()[indexOf(again, "Stutter")].favorite);
    REQUIRE(again.setFavorite(indexOf(again, "Shimmer"), false));
    REQUIRE(!again.entries()[indexOf(again, "Shimmer")].favorite);
    REQUIRE(readText(machine.favorites()) ==
            "file:/elsewhere/Other.ggpreset\nplugin:init\n" + mine + "\n");

    // The instrument shares the file: its INIT is a favorite too, and its own files are not.
    const PresetCatalog instrument(Mode::Instrument, machine.settingsFile());
    REQUIRE(instrument.entries()[0].favorite);
    REQUIRE(!instrument.entries()[1].favorite);
}

TEST_CASE("the favorites file reads lines and is written whole", "[plugin][catalog]")
{
    const Machine machine("favorites-file");
    const fs::path file = machine.root() / "deeper" / "still" / "favorites.txt";
    REQUIRE(gg::plugin::loadFavorites(file).empty());

    const std::vector<std::string> keys{"plugin:init", "file:/a/b.ggpreset", "file:/ü/ñ.ggpreset"};
    REQUIRE(gg::plugin::saveFavorites(file, keys));
    REQUIRE(readText(file) == "plugin:init\nfile:/a/b.ggpreset\nfile:/ü/ñ.ggpreset\n");
    REQUIRE(gg::plugin::loadFavorites(file) == keys);
    // Only the file itself is left: the temporary was renamed over it.
    REQUIRE(filesUnder(file.parent_path()) == std::vector<fs::path>{file});

    // Blank lines (white space alone) and a CR before the LF are dropped; the last line needs no
    // LF.
    putBytes(file, "\r\nplugin:init\r\n\n \t\nfile:/x.ggpreset");
    REQUIRE(gg::plugin::loadFavorites(file) ==
            std::vector<std::string>{"plugin:init", "file:/x.ggpreset"});

    REQUIRE(gg::plugin::saveFavorites(file, std::vector<std::string>{}));
    REQUIRE(readText(file).empty());

    // A key that would break the format, or no file to write, fails and leaves the file alone.
    REQUIRE(gg::plugin::saveFavorites(file, std::vector<std::string>{"plugin:init"}));
    REQUIRE(!gg::plugin::saveFavorites(file, std::vector<std::string>{"file:/a\nb.ggpreset"}));
    REQUIRE(!gg::plugin::saveFavorites({}, std::vector<std::string>{"plugin:init"}));
    REQUIRE(readText(file) == "plugin:init\n");
}

TEST_CASE("a save name is made safe", "[plugin][catalog]")
{
    using gg::plugin::safeFileName;
    REQUIRE(safeFileName("Smear Pad") == "Smear Pad");
    REQUIRE(safeFileName("a/b:c") == "a_b_c");
    REQUIRE(safeFileName("\\*?\"<>|") == "_______");
    REQUIRE(safeFileName("tab\there\x7f") == "tab_here_");
    REQUIRE(safeFileName(".hidden") == "_hidden");
    REQUIRE(safeFileName("..") == "__"); // a leading and a trailing dot
    REQUIRE(safeFileName("a.b") == "a.b");
    REQUIRE(safeFileName("Grüße") == "Grüße");
    REQUIRE(safeFileName("").empty());
    // Windows' reserved device names, in any case, by the part before the first dot (M1), and
    // a trailing dot or space, which Windows drops.
    for (const char *reserved : {"CON", "con", "Prn", "AUX", "NUL", "COM1", "com9", "LPT1", "lpt9",
                                 "nul.txt", "CON.my pad"})
    {
        INFO(reserved);
        REQUIRE(safeFileName(reserved) == std::string("_") + reserved);
    }
    for (const char *fine : {"CONSOLE", "COM10", "COM0", "LPT", "Null", "a CON", "COM"})
    {
        INFO(fine);
        REQUIRE(safeFileName(fine) == fine);
    }
    REQUIRE(safeFileName("Pad.") == "Pad_");
    REQUIRE(safeFileName("Pad. .") == "Pad___");
    REQUIRE(safeFileName("Pad ") == "Pad_");
    REQUIRE(safeFileName(std::string(200, 'x')) == std::string(100, 'x'));
    // Cut on a UTF-8 boundary: "x" then two-byte "é"s, so byte 100 is inside one.
    std::string accented = "x";
    for (int i = 0; i < 60; ++i)
    {
        accented += "é";
    }
    const std::string cut = safeFileName(accented);
    REQUIRE(cut.size() == 99);
    REQUIRE(cut == accented.substr(0, 99));

    const Machine machine("save-names");
    putLibrary(machine);
    PresetCatalog fx(Mode::Fx, machine.settingsFile());
    for (const std::string &name :
         std::vector<std::string>{"a/b:c", ".hidden", "../../escape", std::string(200, 'y')})
    {
        INFO(name);
        REQUIRE(
            fx.save(infoOf(name, Mode::Fx), presetBytes(infoOf(name, Mode::Fx)), false).saved());
    }
    const auto empty = fx.save(infoOf("", Mode::Fx), presetBytes(infoOf("", Mode::Fx)), false);
    REQUIRE(empty.status == gg::plugin::SaveResult::Status::Failed);
    REQUIRE(empty.error == "the name is empty");

    // Each save is in the user folder, under its safe name, and listed (by its own name).
    for (const auto &[name, file] : std::vector<std::pair<std::string, std::string>>{
             {"a/b:c", "a_b_c.ggpreset"},
             {".hidden", "_hidden.ggpreset"},
             {"../../escape", "_._.._escape.ggpreset"},
             {std::string(200, 'y'), std::string(100, 'y') + ".ggpreset"}})
    {
        INFO(name);
        REQUIRE(fs::is_regular_file(machine.user() / file));
        const auto &entry = fx.entries()[indexOf(fx, name)];
        REQUIRE(entry.kind == PresetKind::User);
        REQUIRE(entry.file == machine.user() / file);
    }
    // Nothing was written anywhere else: every file under the scratch root is in the user
    // folder (the saves) or the factory folder (the fixture's presets). This test writes no
    // favorites and no settings.
    for (const auto &file : filesUnder(machine.root()))
    {
        INFO(utf8(file));
        const auto inside = [&](const fs::path &folder) {
            const auto relative = file.lexically_relative(folder);
            return !relative.empty() && *relative.begin() != "..";
        };
        REQUIRE((inside(machine.user()) || inside(machine.factory())));
    }
    REQUIRE(filesUnder(machine.user()).size() == 4 + 4);
}

TEST_CASE("save refuses to overwrite a preset unless asked", "[plugin][catalog]")
{
    const Machine machine("save-overwrite");
    PresetCatalog fx(Mode::Fx, machine.settingsFile());
    REQUIRE(!fs::exists(machine.user()));

    const auto first = infoOf("Pad", Mode::Fx, {"pad"}, "first");
    REQUIRE(fx.save(first, presetBytes(first), false).saved());
    REQUIRE(namesOf(fx) == std::vector<std::string>{"Init", "Pad"});
    REQUIRE(fx.entries()[1].info.description == "first");
    const fs::path file = machine.user() / "Pad.ggpreset";
    REQUIRE(readText(file) == presetBytes(first));

    const auto second = infoOf("Pad", Mode::Fx, {"pad", "dark"}, "second");
    const auto refused = fx.save(second, presetBytes(second), false);
    REQUIRE(refused.status == gg::plugin::SaveResult::Status::Exists);
    REQUIRE(refused.existingName == "Pad");
    REQUIRE(refused.error == "a preset named Pad exists");
    REQUIRE(readText(file) == presetBytes(first));
    REQUIRE(fx.save(second, presetBytes(second), true).saved());
    REQUIRE(readText(file) == presetBytes(second));
    REQUIRE(namesOf(fx) == std::vector<std::string>{"Init", "Pad"});
    REQUIRE(fx.entries()[1].info.description == "second");
    // No temporary file is left beside it.
    REQUIRE(filesUnder(machine.user()) == std::vector<fs::path>{file});
}

TEST_CASE("an existing file under another name is named by the name it holds", "[plugin][catalog]")
{
    // "a/b" and "a:b" both make a_b.ggpreset (M2): the refusal names the preset the file holds,
    // so the "Replace?" prompt names what would be lost.
    const Machine machine("save-other-name");
    PresetCatalog fx(Mode::Fx, machine.settingsFile());
    const auto first = infoOf("a/b", Mode::Fx);
    REQUIRE(fx.save(first, presetBytes(first), false).saved());
    const auto second = infoOf("a:b", Mode::Fx);
    const auto refused = fx.save(second, presetBytes(second), false);
    REQUIRE(refused.status == gg::plugin::SaveResult::Status::Exists);
    REQUIRE(refused.existingName == "a/b");
    REQUIRE(refused.error == "a preset named a/b exists");
    // A file there that isn't a preset is named by its file name.
    putBytes(machine.user() / "junk.ggpreset", "not a preset");
    const auto junk = infoOf("junk", Mode::Fx);
    const auto overJunk = fx.save(junk, presetBytes(junk), false);
    REQUIRE(overJunk.status == gg::plugin::SaveResult::Status::Exists);
    REQUIRE(overJunk.existingName == "junk.ggpreset");
}

TEST_CASE("a content folder's presets are listed as the user's pack, not the factory's",
          "[plugin][catalog]")
{
    // M6: the folder chosen with "Content folder..." holds a pack of the user's own.
    const Machine machine("content-folder");
    const fs::path pack = machine.root() / "pack";
    putPreset(gg::media::factoryPresetsFolder(pack) / "Theirs.ggpreset",
              infoOf("Theirs", Mode::Fx));
    putPreset(machine.factory() / "Ours.ggpreset", infoOf("Ours", Mode::Fx));
    {
        gg::core::MachineSettings settings;
        settings.contentFolder = utf8(pack);
        REQUIRE(gg::media::saveSettings(machine.settingsFile(), settings));
    }
    PresetCatalog fx(Mode::Fx, machine.settingsFile());
    REQUIRE(namesOf(fx) == std::vector<std::string>{"Init", "Ours", "Theirs"});
    CHECK(fx.entries()[1].kind == PresetKind::Factory);
    CHECK(fx.entries()[2].kind == PresetKind::Content);
    CHECK(!fx.remove(2)); // not the user's own presets folder: never deleted
    CHECK(fx.contentStatus() ==
          "Factory content in " + utf8(machine.content()) + "; Content folder: " + utf8(pack));
}

TEST_CASE("with a content folder alone the status line names it and no factory content",
          "[plugin][catalog]")
{
    const Machine machine("content-folder-alone");
    const ScopedEnv noDev("GG_CONTENT_DIR", nullptr); // after the Machine sets it
    const fs::path pack = machine.root() / "pack";
    putPreset(gg::media::factoryPresetsFolder(pack) / "Theirs.ggpreset",
              infoOf("Theirs", Mode::Fx));
    putBytes(gg::media::contentVersionFile(pack), "giga-grains-content v9 00\n");
    {
        gg::core::MachineSettings settings;
        settings.contentFolder = utf8(pack);
        REQUIRE(gg::media::saveSettings(machine.settingsFile(), settings));
    }
    const PresetCatalog fx(Mode::Fx, machine.settingsFile());
    REQUIRE(namesOf(fx) == std::vector<std::string>{"Init", "Theirs"});
    CHECK(fx.contentStatus() == "No factory content found; Content folder: " + utf8(pack));
}

TEST_CASE("remove deletes a user preset and refuses Init and a factory preset", "[plugin][catalog]")
{
    const Machine machine("remove");
    putLibrary(machine);
    PresetCatalog fx(Mode::Fx, machine.settingsFile());
    REQUIRE(fx.setFavorite(indexOf(fx, "Mine"), true));

    REQUIRE(!fx.remove(indexOf(fx, "Init")));
    REQUIRE(!fx.remove(indexOf(fx, "Shimmer")));
    REQUIRE(fs::is_regular_file(machine.factory() / "02 Shimmer.ggpreset"));
    REQUIRE(!fx.remove(fx.entries().size()));

    REQUIRE(fx.remove(indexOf(fx, "Mine")));
    REQUIRE(!fs::exists(machine.user() / "b mine.ggpreset"));
    REQUIRE(namesOf(fx) == std::vector<std::string>{"Init", "Smear Pad", "Shimmer", "Stutter",
                                                    "Upper", "Deep", "Drone", "Nested"});
    // Its favorite goes with it.
    REQUIRE(gg::plugin::loadFavorites(machine.favorites()).empty());
}

TEST_CASE("an empty machine lists INIT alone", "[plugin][catalog]")
{
    const Machine machine("empty");
    const PresetCatalog fx(Mode::Fx, machine.settingsFile());
    REQUIRE(namesOf(fx) == std::vector<std::string>{"Init"});
    REQUIRE(fx.entries()[0].kind == PresetKind::Init);
    REQUIRE(fx.tagsInUse().empty());
    REQUIRE(fx.contentStatus() ==
            "No factory content found: install it, or choose a Content folder");

    // A catalog with no settings file has no user folder, and saves nowhere.
    PresetCatalog nowhere(Mode::Instrument, {});
    REQUIRE(namesOf(nowhere) == std::vector<std::string>{"Init"});
    REQUIRE(nowhere.entries()[0].info.product == Mode::Instrument);
    REQUIRE(!nowhere.save(infoOf("X", Mode::Instrument), "bytes", false).saved());
    REQUIRE(!nowhere.setFavorite(0, true));
}

TEST_CASE("the status line names the content release and its root", "[plugin][catalog]")
{
    const Machine machine("status");
    fs::create_directories(machine.factory());
    {
        const PresetCatalog fx(Mode::Fx, machine.settingsFile());
        REQUIRE(fx.contentStatus() == "Factory content in " + utf8(machine.content()));
    }
    putBytes(gg::media::contentVersionFile(machine.content()),
             "giga-grains-content v1 "
             "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\nmore\n");
    const PresetCatalog fx(Mode::Fx, machine.settingsFile());
    REQUIRE(fx.contentStatus() == "Factory content v1 in " + utf8(machine.content()));
}

// The cap applies to the files listed, before any is read, so the folder is filled with empty
// files (none a preset) between a first and a last valid one: the first is listed, the 5,001st
// is past the cap. [serial]: it writes 5,001 files.
TEST_CASE("a folder of 5001 files lists 5000 and the status says so", "[plugin][catalog][serial]")
{
    const Machine machine("cap");
    fs::create_directories(machine.factory());
    putPreset(machine.factory() / "0000.ggpreset", infoOf("First", Mode::Fx));
    for (int i = 1; i < 5000; ++i)
    {
        std::string name = std::to_string(i);
        name.insert(0, 4 - name.size(), '0');
        std::ofstream(machine.factory() / (name + ".ggpreset"));
    }
    putPreset(machine.factory() / "5000.ggpreset", infoOf("Past the cap", Mode::Fx));

    const PresetCatalog fx(Mode::Fx, machine.settingsFile());
    REQUIRE(namesOf(fx) == std::vector<std::string>{"Init", "First"});
    REQUIRE(fx.contentStatus() ==
            "Factory content in " + utf8(machine.content()) + " (1 more file not listed)");
}

TEST_CASE("changedOnDisk sees a file added to a folder whose time did not move",
          "[plugin][catalog]")
{
    // R3: a file system whose clock ticks coarsely (NTFS, ext4) gives two writes milliseconds
    // apart one time; the folder's entry count still moves.
    const Machine machine("poll-count");
    putPreset(machine.user() / "one.ggpreset", infoOf("One", Mode::Fx));
    const PresetCatalog catalog(Mode::Fx, machine.settingsFile());
    REQUIRE_FALSE(catalog.changedOnDisk());
    const auto before = fs::last_write_time(machine.user());
    putPreset(machine.user() / "two.ggpreset", infoOf("Two", Mode::Fx));
    fs::last_write_time(machine.user(), before); // the same tick
    CHECK(catalog.changedOnDisk());
}

TEST_CASE("the walk stops at the depth clap-wrapper's crawl stops at", "[plugin][catalog]")
{
    // R5: a file directly in the folder is at depth 0; depth 8 is listed, depth 9 is not.
    const Machine machine("depth");
    fs::path folder = machine.user();
    putPreset(folder / "d0.ggpreset", infoOf("Depth 0", Mode::Fx));
    for (int depth = 1; depth <= 10; ++depth)
    {
        folder /= "d" + std::to_string(depth);
        putPreset(folder / "p.ggpreset", infoOf("Depth " + std::to_string(depth), Mode::Fx));
    }
    const PresetCatalog catalog(Mode::Fx, machine.settingsFile());
    const auto names = namesOf(catalog);
    CHECK(std::count(names.begin(), names.end(), "Depth 8") == 1);
    CHECK(std::count(names.begin(), names.end(), "Depth 9") == 0);
    CHECK(std::count(names.begin(), names.end(), "Depth 10") == 0);
    CHECK(names.size() == 1 + 9); // INIT and depths 0 to 8
}
