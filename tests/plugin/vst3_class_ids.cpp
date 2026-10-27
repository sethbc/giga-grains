// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// gg-vst3-class-ids (macOS): loads a built VST3 bundle, lists its factory's classes, and checks
// them against the expected ones. The VST3 class IDs (FUIDs) are frozen at the first public
// release (INTENT §18). clap-wrapper derives them from the CLAP IDs, so this pins what the built
// plugin actually reports, whatever clap-wrapper's derivation does after an update.
//
//   gg-vst3-class-ids <bundle> <binary> <name>=<32 hex digits> ...
//
// Exit status: 0 if the factory has exactly the expected classes, in order, each an
// "Audio Module Class" with the expected name and ID; 1 if not; 2 for usage or loading errors.
//
// It talks to the factory through the raw COM vtable, so it needs no VST3 SDK headers.

#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
// The layouts of Steinberg::PClassInfo, PFactoryInfo and the start of IPluginFactory's vtable
// (pluginterfaces/base/ipluginbase.h).
struct PClassInfo
{
    unsigned char cid[16];
    int32_t cardinality;
    char category[32];
    char name[64];
};

struct PFactoryInfo
{
    char vendor[64];
    char url[256];
    char email[128];
    int32_t flags;
};

struct IPluginFactory;
struct IPluginFactoryVtbl
{
    int32_t (*queryInterface)(IPluginFactory *, const char *, void **);
    uint32_t (*addRef)(IPluginFactory *);
    uint32_t (*release)(IPluginFactory *);
    int32_t (*getFactoryInfo)(IPluginFactory *, PFactoryInfo *);
    int32_t (*countClasses)(IPluginFactory *);
    int32_t (*getClassInfo)(IPluginFactory *, int32_t, PClassInfo *);
};

struct IPluginFactory
{
    IPluginFactoryVtbl *vtbl;
};

struct Expected
{
    std::string name;
    std::string cid; // 32 uppercase hex digits
};

std::string hex(const unsigned char (&bytes)[16])
{
    std::string out;
    char digits[3];
    for (const unsigned char b : bytes)
    {
        std::snprintf(digits, sizeof(digits), "%02X", b);
        out += digits;
    }
    return out;
}

// Copies a fixed-size C string field that may lack its terminator.
template <std::size_t N> std::string field(const char (&chars)[N])
{
    return std::string(chars, strnlen(chars, N));
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: gg-vst3-class-ids <bundle> <binary> <name>=<cid> ...\n");
        return 2;
    }
    std::vector<Expected> expected;
    for (int i = 3; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto eq = arg.rfind('=');
        if (eq == std::string::npos || arg.size() - eq - 1 != 32)
        {
            std::fprintf(stderr, "bad expectation '%s': want <name>=<32 hex digits>\n", argv[i]);
            return 2;
        }
        expected.push_back({arg.substr(0, eq), arg.substr(eq + 1)});
    }

    void *module = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    if (module == nullptr)
    {
        std::fprintf(stderr, "dlopen %s: %s\n", argv[2], dlerror());
        return 2;
    }
    using BundleEntry = bool (*)(CFBundleRef);
    using BundleExit = bool (*)();
    using GetFactory = IPluginFactory *(*)();
    const auto bundleEntry = reinterpret_cast<BundleEntry>(dlsym(module, "bundleEntry"));
    const auto bundleExit = reinterpret_cast<BundleExit>(dlsym(module, "bundleExit"));
    const auto getFactory = reinterpret_cast<GetFactory>(dlsym(module, "GetPluginFactory"));
    if (bundleEntry == nullptr || bundleExit == nullptr || getFactory == nullptr)
    {
        std::fprintf(stderr, "%s doesn't export bundleEntry, bundleExit and GetPluginFactory\n",
                     argv[2]);
        return 2;
    }

    CFURLRef url =
        CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8 *>(argv[1]),
                                                static_cast<CFIndex>(std::strlen(argv[1])), true);
    CFBundleRef bundle = url != nullptr ? CFBundleCreate(nullptr, url) : nullptr;
    if (bundle == nullptr || !bundleEntry(bundle))
    {
        std::fprintf(stderr, "bundleEntry failed for %s\n", argv[1]);
        return 2;
    }
    IPluginFactory *factory = getFactory();
    if (factory == nullptr)
    {
        std::fprintf(stderr, "GetPluginFactory returned null\n");
        return 2;
    }

    bool ok = true;
    const int32_t count = factory->vtbl->countClasses(factory);
    if (count != static_cast<int32_t>(expected.size()))
    {
        std::printf("FAIL: %d classes, expected %zu\n", count, expected.size());
        ok = false;
    }
    for (int32_t i = 0; i < count; ++i)
    {
        PClassInfo info{};
        if (factory->vtbl->getClassInfo(factory, i, &info) != 0)
        {
            std::printf("FAIL: getClassInfo(%d) failed\n", i);
            ok = false;
            continue;
        }
        const std::string cid = hex(info.cid);
        const std::string name = field(info.name);
        const std::string category = field(info.category);
        std::printf("class %d: cid=%s category=\"%s\" name=\"%s\"\n", i, cid.c_str(),
                    category.c_str(), name.c_str());
        if (i < static_cast<int32_t>(expected.size()))
        {
            const auto &want = expected[static_cast<std::size_t>(i)];
            if (cid != want.cid || name != want.name || category != "Audio Module Class")
            {
                std::printf("FAIL: class %d should be cid=%s category=\"Audio Module Class\" "
                            "name=\"%s\"\n",
                            i, want.cid.c_str(), want.name.c_str());
                ok = false;
            }
        }
    }

    factory->vtbl->release(factory);
    bundleExit();
    CFRelease(bundle);
    CFRelease(url);
    std::printf(ok ? "VST3 class IDs match\n" : "VST3 class IDs differ\n");
    return ok ? 0 : 1;
}
