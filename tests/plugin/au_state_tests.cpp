// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The AU state format, pinned (carry-over "Pin the AU state format with a test", and T7 (223)).
// macOS, with the editor: gg-au-state-tests loads the built Giga Grains.component into its own
// process, registers its two factories there with AudioComponentRegister (nothing is installed,
// and no other process sees them), and drives the AudioUnits through the AudioUnit API, as a host
// does. What clap-wrapper's AUv2 saves as kAudioUnitProperty_ClassInfo is a dictionary whose
// `data` holds the CLAP state bytes unchanged: GGST, with PTCH and MEDI (spec §6.6).
//
// clap-wrapper's three known faults in that path are pinned with the values they give today, so
// that a clap-wrapper update that fixes (or changes) any of them fails here and is looked at:
// - the preset name is not retained: SaveState writes `_current_program_name`, which only
//   RestoreState sets, so a name set by kAudioUnitProperty_PresentPreset never reaches the saved
//   dictionary ("Program" instead), which auval reports as "Preset name is not retained";
// - RestoreState returns noErr when the plugin's load() fails;
// - SaveState ignores save()'s result. Giga Grains' save fails only when its stream refuses
//   bytes, and clap-wrapper's StateMemento never does, so no AU call can reach this one: it is
//   pinned on clap-wrapper's source instead.

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "gg/core/ParamIds.h"
#include "gg/core/SessionState.h"
#include "gg/media/Sample.h"
#include "media/AudioFixtures.h"

namespace
{
using gg::core::paramId;
using gg::core::ParamSlot;

// One of the component's three AudioComponents, and the factory behind it.
struct Identity
{
    OSType type;
    OSType subtype;
    const char *factory;
    gg::core::Mode mode;
    const char *name; // for CAPTURE
};

constexpr OSType kManufacturer = 'Sbcx';
const std::array<Identity, 3> kIdentities{{
    {'aumu', 'GGin', "wrapAsAUV2_inst0Factory", gg::core::Mode::Instrument, "aumu GGin"},
    {'aufx', 'GGfx', "wrapAsAUV2_inst1Factory", gg::core::Mode::Fx, "aufx GGfx"},
    {'aumf', 'GGfx', "wrapAsAUV2_inst1Factory", gg::core::Mode::Fx, "aumf GGfx"},
}};

/// A Core Foundation reference released on destruction.
template <typename T> class Cf
{
  public:
    Cf() = default;
    explicit Cf(T ref) : ref_(ref) {}
    Cf(const Cf &) = delete;
    Cf &operator=(const Cf &) = delete;
    ~Cf()
    {
        if (ref_ != nullptr)
        {
            CFRelease(ref_);
        }
    }
    T get() const noexcept { return ref_; }
    T *out() noexcept { return &ref_; }

  private:
    T ref_{nullptr};
};

std::string toString(CFStringRef s)
{
    if (s == nullptr)
    {
        return "<null>";
    }
    std::array<char, 1024> buffer{};
    return CFStringGetCString(s, buffer.data(), buffer.size(), kCFStringEncodingUTF8)
               ? std::string(buffer.data())
               : "<not UTF-8>";
}

/// The scratch folder of the test `name`, created if needed.
std::filesystem::path scratch(const std::string &name)
{
    const auto dir = std::filesystem::path(GG_TEST_OUTPUT_DIR) / ("au-state-" + name);
    std::filesystem::create_directories(dir);
    return dir;
}

/// The component's AudioComponent for `id`, registered in this process on first use: the
/// built bundle loaded, and its factory registered under the identity's description. The
/// registration is this process's alone; nothing is installed.
AudioComponent component(const Identity &id)
{
    static std::map<std::tuple<OSType, OSType>, AudioComponent> registered;
    const auto key = std::make_tuple(id.type, id.subtype);
    if (const auto it = registered.find(key); it != registered.end())
    {
        return it->second;
    }
    // The plugins read their per-machine settings when an instance is created: a scratch file,
    // never the user's (ruling R16).
    setenv("GG_SETTINGS_FILE", (scratch("settings") / "settings.xml").c_str(), 1);

    static CFBundleRef bundle = [] {
        const std::string path = GG_AU_BUNDLE;
        Cf<CFURLRef> url(CFURLCreateFromFileSystemRepresentation(
            nullptr, reinterpret_cast<const UInt8 *>(path.data()),
            static_cast<CFIndex>(path.size()), true));
        CFBundleRef b = CFBundleCreate(nullptr, url.get()); // kept for the process's life
        return b != nullptr && CFBundleLoadExecutable(b) ? b : nullptr;
    }();
    if (bundle == nullptr)
    {
        return nullptr;
    }
    Cf<CFStringRef> factoryName(
        CFStringCreateWithCString(nullptr, id.factory, kCFStringEncodingUTF8));
    auto *factory = reinterpret_cast<AudioComponentFactoryFunction>(
        CFBundleGetFunctionPointerForName(bundle, factoryName.get()));
    if (factory == nullptr)
    {
        return nullptr;
    }
    const AudioComponentDescription desc{id.type, id.subtype, kManufacturer, 0, 0};
    AudioComponent c = AudioComponentRegister(&desc, CFSTR("gg-au-state-tests"), 1, factory);
    registered.emplace(key, c);
    return c;
}

/// An initialized AudioUnit of `id`, disposed of on destruction.
class Unit
{
  public:
    explicit Unit(const Identity &id)
    {
        AudioComponent c = component(id);
        if (c != nullptr && AudioComponentInstanceNew(c, &unit_) == noErr &&
            AudioUnitInitialize(unit_) == noErr)
        {
            initialized_ = true;
        }
    }
    Unit(const Unit &) = delete;
    Unit &operator=(const Unit &) = delete;
    ~Unit()
    {
        if (unit_ == nullptr)
        {
            return;
        }
        if (initialized_)
        {
            AudioUnitUninitialize(unit_);
        }
        AudioComponentInstanceDispose(unit_);
    }

    bool valid() const noexcept { return initialized_; }
    AudioUnit get() const noexcept { return unit_; }

    /// kAudioUnitProperty_ClassInfo: the saved state (+1 reference). Null on an error.
    CFDictionaryRef classInfo(OSStatus *status = nullptr) const
    {
        CFPropertyListRef plist = nullptr;
        UInt32 size = sizeof(plist);
        const OSStatus s = AudioUnitGetProperty(unit_, kAudioUnitProperty_ClassInfo,
                                                kAudioUnitScope_Global, 0, &plist, &size);
        if (status != nullptr)
        {
            *status = s;
        }
        return s == noErr ? static_cast<CFDictionaryRef>(plist) : nullptr;
    }
    OSStatus restore(CFDictionaryRef state) const
    {
        CFPropertyListRef plist = state;
        return AudioUnitSetProperty(unit_, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0,
                                    &plist, sizeof(plist));
    }
    float parameter(ParamSlot slot) const
    {
        AudioUnitParameterValue v = -12345.0f;
        AudioUnitGetParameter(unit_, paramId(slot), kAudioUnitScope_Global, 0, &v);
        return v;
    }

  private:
    AudioUnit unit_{nullptr};
    bool initialized_{false};
};

/// The CLAP state bytes in a ClassInfo dictionary: its `data`. Empty if there is none.
std::string stateBytes(CFDictionaryRef dict)
{
    const auto *data = static_cast<CFDataRef>(CFDictionaryGetValue(dict, CFSTR(kAUPresetDataKey)));
    if (data == nullptr || CFGetTypeID(data) != CFDataGetTypeID())
    {
        return {};
    }
    return std::string(reinterpret_cast<const char *>(CFDataGetBytePtr(data)),
                       static_cast<std::size_t>(CFDataGetLength(data)));
}

SInt32 numberOf(CFDictionaryRef dict, const char *key)
{
    Cf<CFStringRef> k(CFStringCreateWithCString(nullptr, key, kCFStringEncodingUTF8));
    const auto *n = static_cast<CFNumberRef>(CFDictionaryGetValue(dict, k.get()));
    SInt32 v = -1;
    if (n == nullptr || CFGetTypeID(n) != CFNumberGetTypeID() ||
        !CFNumberGetValue(n, kCFNumberSInt32Type, &v))
    {
        return -1;
    }
    return v;
}

std::string nameOf(CFDictionaryRef dict)
{
    const auto *name =
        static_cast<CFStringRef>(CFDictionaryGetValue(dict, CFSTR(kAUPresetNameKey)));
    return name != nullptr && CFGetTypeID(name) == CFStringGetTypeID() ? toString(name) : "<none>";
}

/// A copy of `dict` with `bytes` as its `data` (+1 reference).
CFDictionaryRef withState(CFDictionaryRef dict, const std::string &bytes)
{
    CFMutableDictionaryRef copy = CFDictionaryCreateMutableCopy(nullptr, 0, dict);
    Cf<CFDataRef> data(CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(bytes.data()),
                                    static_cast<CFIndex>(bytes.size())));
    CFDictionarySetValue(copy, CFSTR(kAUPresetDataKey), data.get());
    return copy;
}

/// A copy of `dict` with `name` as its preset name (+1 reference).
CFDictionaryRef withName(CFDictionaryRef dict, CFStringRef name)
{
    CFMutableDictionaryRef copy = CFDictionaryCreateMutableCopy(nullptr, 0, dict);
    CFDictionarySetValue(copy, CFSTR(kAUPresetNameKey), name);
    return copy;
}

/// A 1 s stereo sine at 48 kHz, as a 32-bit float WAV, in the test's scratch folder.
std::filesystem::path sineFile(const std::string &name)
{
    gg::test::TestAudio audio = gg::test::makeTestAudio(2, 48000, 48000);
    const auto path = scratch(name) / (name + ".wav");
    gg::test::writeFile(path, gg::test::wavBytes(audio, gg::test::SampleFormat::Float32));
    return path;
}
} // namespace

TEST_CASE("au class info carries the clap state and round-trips it", "[plugin][au]")
{
    // Each identity: ClassInfo is the AU preset dictionary (version 0, the identity's type,
    // subtype and manufacturer) with the CLAP state bytes as its `data`: GGST, which
    // decodeState reads. A state given back through ClassInfo is saved again byte for byte, a
    // changed one (other values and seed, and a sample in MEDI for the instrument) included,
    // and its values reach the AU's parameters.
    //
    // The values are changed through a state, not AudioUnitSetParameter: clap-wrapper hands a
    // parameter set on an initialized unit to the plugin at its next render or its editor's
    // idle tick, and this unit has neither, so save() wouldn't see it yet.
    const Identity id = GENERATE(Catch::Generators::from_range(kIdentities));
    CAPTURE(id.name);
    const bool isInstrument = id.mode == gg::core::Mode::Instrument;

    Unit a(id);
    REQUIRE(a.valid());
    Cf<CFDictionaryRef> saved(a.classInfo());
    REQUIRE(saved.get() != nullptr);
    CHECK(numberOf(saved.get(), kAUPresetVersionKey) == 0);
    CHECK(static_cast<OSType>(numberOf(saved.get(), kAUPresetTypeKey)) == id.type);
    CHECK(static_cast<OSType>(numberOf(saved.get(), kAUPresetSubtypeKey)) == id.subtype);
    CHECK(static_cast<OSType>(numberOf(saved.get(), kAUPresetManufacturerKey)) == kManufacturer);

    const std::string bytes = stateBytes(saved.get());
    REQUIRE(bytes.size() > 8);
    CHECK(bytes.substr(0, 4) == "GGST");
    gg::core::SessionState state;
    REQUIRE(gg::core::decodeState(bytes, id.mode, state));
    CHECK(state.values[static_cast<std::size_t>(ParamSlot::Pitch)] == 0.0);
    CHECK_FALSE(state.sample.has_value());

    SECTION("a fresh unit given the state saves the same bytes")
    {
        Unit b(id);
        REQUIRE(b.valid());
        REQUIRE(b.restore(saved.get()) == noErr);
        Cf<CFDictionaryRef> again(b.classInfo());
        REQUIRE(again.get() != nullptr);
        CHECK(stateBytes(again.get()) == bytes);
    }

    SECTION("a changed state, with a sample for the instrument, saves the same bytes")
    {
        state.values[static_cast<std::size_t>(ParamSlot::Pitch)] = 5.0;
        state.values[static_cast<std::size_t>(ParamSlot::Spray)] = 0.25;
        state.seed = 0x0123456789ABCDEFull;
        if (isInstrument)
        {
            const auto file = sineFile("round-trip");
            state.sample = gg::media::loadSample(file, {}).entry;
            REQUIRE(gg::core::isValidMediaEntry(*state.sample));
        }
        const std::string changed = gg::core::encodeState(state);
        REQUIRE(changed != bytes);
        Cf<CFDictionaryRef> given(withState(saved.get(), changed));

        Unit b(id);
        REQUIRE(b.valid());
        REQUIRE(b.restore(given.get()) == noErr);
        CHECK(b.parameter(ParamSlot::Pitch) == 5.0f);
        CHECK(b.parameter(ParamSlot::Spray) == 0.25f);
        Cf<CFDictionaryRef> again(b.classInfo());
        REQUIRE(again.get() != nullptr);
        const std::string resaved = stateBytes(again.get());
        CHECK(resaved == changed);
        gg::core::SessionState back;
        REQUIRE(gg::core::decodeState(resaved, id.mode, back));
        CHECK(back.seed == state.seed);
        CHECK(back.sample == state.sample);
    }
}

TEST_CASE("clap-wrapper's au state faults are pinned", "[plugin][au]")
{
    // T7 (223). Each CHECK marked "fault" holds what clap-wrapper does today, not what it should:
    // when an update changes it, this fails, and the carry-over row is revisited (and the AU
    // state format with it).
    const Identity id = GENERATE(Catch::Generators::from_range(kIdentities));
    CAPTURE(id.name);
    Unit unit(id);
    REQUIRE(unit.valid());

    SECTION("the preset name set through PresentPreset is not retained")
    {
        AUPreset preset{-1, CFSTR("Pinned Name")};
        REQUIRE(AudioUnitSetProperty(unit.get(), kAudioUnitProperty_PresentPreset,
                                     kAudioUnitScope_Global, 0, &preset, sizeof(preset)) == noErr);
        AUPreset present{};
        UInt32 size = sizeof(present);
        REQUIRE(AudioUnitGetProperty(unit.get(), kAudioUnitProperty_PresentPreset,
                                     kAudioUnitScope_Global, 0, &present, &size) == noErr);
        Cf<CFStringRef> presentName(present.presetName);
        CHECK(present.presetNumber == -1);
        CHECK(toString(presentName.get()) == "Pinned Name");

        Cf<CFDictionaryRef> saved(unit.classInfo());
        REQUIRE(saved.get() != nullptr);
        CHECK(nameOf(saved.get()) == "Program"); // fault: should be "Pinned Name"

        // Only a restored ClassInfo sets the name SaveState writes.
        Cf<CFDictionaryRef> named(withName(saved.get(), CFSTR("Restored Name")));
        REQUIRE(unit.restore(named.get()) == noErr);
        Cf<CFDictionaryRef> again(unit.classInfo());
        REQUIRE(again.get() != nullptr);
        CHECK(nameOf(again.get()) == "Restored Name");
    }

    SECTION("restoring a state the plugin can't load reports success")
    {
        // Pitch 7, through a state the plugin loads.
        Cf<CFDictionaryRef> initial(unit.classInfo());
        REQUIRE(initial.get() != nullptr);
        gg::core::SessionState state;
        REQUIRE(gg::core::decodeState(stateBytes(initial.get()), id.mode, state));
        state.values[static_cast<std::size_t>(ParamSlot::Pitch)] = 7.0;
        Cf<CFDictionaryRef> pitched(withState(initial.get(), gg::core::encodeState(state)));
        REQUIRE(unit.restore(pitched.get()) == noErr);
        REQUIRE(unit.parameter(ParamSlot::Pitch) == 7.0f);
        Cf<CFDictionaryRef> before(unit.classInfo());
        REQUIRE(before.get() != nullptr);
        const std::string bytes = stateBytes(before.get());

        // Not a state: the plugin's load() fails, all or nothing, and keeps the state it has.
        Cf<CFDictionaryRef> bad(withState(before.get(), "GGST, but not really a state"));
        CHECK(unit.restore(bad.get()) == noErr); // fault: should be an error
        CHECK(unit.parameter(ParamSlot::Pitch) == 7.0f);
        Cf<CFDictionaryRef> after(unit.classInfo());
        REQUIRE(after.get() != nullptr);
        CHECK(stateBytes(after.get()) == bytes);

        // A dictionary with no `data` at all is refused, as it should be.
        CFMutableDictionaryRef noData = CFDictionaryCreateMutableCopy(nullptr, 0, before.get());
        CFDictionaryRemoveValue(noData, CFSTR(kAUPresetDataKey));
        Cf<CFDictionaryRef> owned(noData);
        CHECK(unit.restore(owned.get()) != noErr);
    }
}

TEST_CASE("clap-wrapper's au save ignores the plugin's save result", "[plugin][au]")
{
    // T7 (223)'s third fault, pinned on clap-wrapper's source: SaveState calls the CLAP state
    // extension's save() and discards its result, then returns noErr with whatever the stream
    // holds. No AU call reaches it for Giga Grains (see the top of this file). The lines below
    // are clap-wrapper's own, at the pinned commit; when an update changes them, this fails.
    // When it does: re-check SaveState and RestoreState by hand (does SaveState now honour
    // save()'s result, and RestoreState load()'s?), update these lines or the behavioural pins
    // above to match, and update its row, 87 (T7 223), in docs/m2-carry-over.md.
    const auto source =
        std::filesystem::path(GG_CLAP_WRAPPER_SOURCE_DIR) / "src" / "wrapasauv2.cpp";
    std::ifstream file(source, std::ios::binary);
    REQUIRE(file.good());
    std::stringstream text;
    text << file.rdbuf();
    const std::string code = text.str();

    // SaveState: the result of save() is dropped.
    CHECK(code.find("    Clap::StateMemento chunk;\n"
                    "    _plugin->_ext._state->save(_plugin->_plugin, chunk);\n") !=
          std::string::npos);
    // SaveState: the name written is _current_program_name, "Program" until a restore.
    CHECK(code.find("_current_program_name = CFStringCreateWithCString(NULL, \"Program\", "
                    "kCFStringEncodingUTF8);") != std::string::npos);
    // RestoreState: the result of load() is dropped too, and noErr returned.
    CHECK(code.find("      _plugin->_ext._state->load(_plugin->_plugin, chunk);\n"
                    "    }\n"
                    "  }\n"
                    "  return noErr;\n") != std::string::npos);
}

namespace
{
// An AU render callback that fills every buffer it is asked for with 0.25 and counts the calls.
OSStatus feedInput(void *refCon, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32,
                   UInt32 frames, AudioBufferList *data)
{
    ++*static_cast<int *>(refCon);
    for (UInt32 b = 0; b < data->mNumberBuffers; ++b)
    {
        auto *samples = static_cast<float *>(data->mBuffers[b].mData);
        const UInt32 n = data->mBuffers[b].mDataByteSize / sizeof(float);
        for (UInt32 i = 0; i < n && i < frames * data->mBuffers[b].mNumberChannels; ++i)
        {
            samples[i] = 0.25f;
        }
    }
    return noErr;
}
} // namespace

TEST_CASE("each au identity has one stereo input and renders pulling it", "[plugin][au]")
{
    // M2 sub-project 1's spec §6.4 (D2): the instrument's sidechain is its AU's input element 0,
    // as the FX's main input is its. Each identity has one stereo input and one stereo output
    // element, and renders slices that pull the input through a render callback: auval's bus and
    // render checks, in process, without the machine's component registry.
    const Identity id = GENERATE(Catch::Generators::from_range(kIdentities));
    CAPTURE(id.name);
    Unit unit(id);
    REQUIRE(unit.valid());
    for (const AudioUnitScope scope : {kAudioUnitScope_Input, kAudioUnitScope_Output})
    {
        CAPTURE(scope);
        UInt32 elements = 0;
        UInt32 size = sizeof(elements);
        REQUIRE(AudioUnitGetProperty(unit.get(), kAudioUnitProperty_ElementCount, scope, 0,
                                     &elements, &size) == noErr);
        CHECK(elements == 1);
        AudioStreamBasicDescription format{};
        size = sizeof(format);
        REQUIRE(AudioUnitGetProperty(unit.get(), kAudioUnitProperty_StreamFormat, scope, 0, &format,
                                     &size) == noErr);
        CHECK(format.mChannelsPerFrame == 2);
    }

    int pulls = 0;
    AURenderCallbackStruct callback{&feedInput, &pulls};
    REQUIRE(AudioUnitSetProperty(unit.get(), kAudioUnitProperty_SetRenderCallback,
                                 kAudioUnitScope_Input, 0, &callback, sizeof(callback)) == noErr);
    AudioStreamBasicDescription out{};
    UInt32 size = sizeof(out);
    REQUIRE(AudioUnitGetProperty(unit.get(), kAudioUnitProperty_StreamFormat,
                                 kAudioUnitScope_Output, 0, &out, &size) == noErr);
    REQUIRE((out.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0);
    constexpr UInt32 kFrames = 512;
    std::array<std::array<float, kFrames>, 2> channels{};
    std::array<std::byte, sizeof(AudioBufferList) + sizeof(AudioBuffer)> storage{};
    auto *buffers = reinterpret_cast<AudioBufferList *>(storage.data());
    buffers->mNumberBuffers = 2;
    AudioTimeStamp time{};
    time.mFlags = kAudioTimeStampSampleTimeValid;
    for (int slice = 0; slice < 4; ++slice)
    {
        CAPTURE(slice);
        for (UInt32 c = 0; c < 2; ++c)
        {
            buffers->mBuffers[c] = AudioBuffer{1, kFrames * sizeof(float), channels[c].data()};
        }
        AudioUnitRenderActionFlags flags = 0;
        CHECK(AudioUnitRender(unit.get(), &flags, &time, 0, kFrames, buffers) == noErr);
        time.mSampleTime += kFrames;
    }
    CHECK(pulls == 4);
}
