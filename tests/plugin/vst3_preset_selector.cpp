// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// gg-vst3-preset-selector (macOS): loads a built VST3 bundle, creates one class's instance and
// checks the preset selector clap-wrapper's VST3 adds to its controller (M2 sub-project 5's spec
// §6.3): a parameter with id 0xC00000 (the first free ID at or above it, which our IDs, all below
// 0xB00000, leave to clap-wrapper), kIsProgramChange set, and a step count that makes the host's
// program list at least <min steps> long (a host reads stepCount + 1 as the count). Our own
// parameter IDs never reach the selector's block: no other ID lies in [0xC00000, 0xC00010).
//
//   gg-vst3-preset-selector <bundle> <binary> <class name> <expected id hex> <min steps>
//
// Exit status: 0 if the class is there with the expected ID and the selector is as described;
// 1 if not; 2 for usage or loading errors.
//
// It talks to the factory and the controller through raw COM vtables, so it needs no VST3 SDK
// headers.
//
// What clap-wrapper does, which this accommodates (libs/clap/clap-wrapper/src/wrapasvst3.cpp):
// - ClapAsVst3::initialize (line 149) calls context->queryInterface without a null check, so a
//   null host context crashes it: the host context here is a minimal IHostApplication whose
//   getName answers "gg-tests".
// - The selector's step count comes from a preset index shared by the module and crawled on a
//   thread. An instance made while the crawl runs gets a hidden selector with stepCount 0, and
//   grows it only from onIdle() (line 2020), which no host calls here. So the executable makes
//   a new instance, after a pause, until the count suffices or its time is up.

#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace
{
// Steinberg::PClassInfo, PFactoryInfo and IPluginFactory's vtable (pluginterfaces/base/
// ipluginbase.h).
struct PClassInfo
{
    unsigned char cid[16];
    int32_t cardinality;
    char category[32];
    char name[64];
};

struct IPluginFactory;
struct IPluginFactoryVtbl
{
    int32_t (*queryInterface)(IPluginFactory *, const unsigned char *, void **);
    uint32_t (*addRef)(IPluginFactory *);
    uint32_t (*release)(IPluginFactory *);
    void *getFactoryInfo;
    int32_t (*countClasses)(IPluginFactory *);
    int32_t (*getClassInfo)(IPluginFactory *, int32_t, PClassInfo *);
    int32_t (*createInstance)(IPluginFactory *, const char *cid, const char *iid, void **obj);
};

struct IPluginFactory
{
    IPluginFactoryVtbl *vtbl;
};

// Steinberg::Vst::ParameterInfo (vst/ivstcomponent.h, ivsteditcontroller.h).
struct ParameterInfo
{
    uint32_t id;
    char16_t title[128];
    char16_t shortTitle[128];
    char16_t units[128];
    int32_t stepCount;
    double defaultNormalizedValue;
    int32_t unitId;
    int32_t flags;
};
constexpr int32_t kIsProgramChange = 1 << 15;
constexpr uint32_t kSelectorId = 0xC00000;
constexpr uint32_t kSelectorBlockEnd = 0xC00010;

// A COM object: a pointer to a vtable whose first three entries are FUnknown's.
struct Unknown;
struct UnknownVtbl
{
    int32_t (*queryInterface)(Unknown *, const unsigned char *, void **);
    uint32_t (*addRef)(Unknown *);
    uint32_t (*release)(Unknown *);
};
struct Unknown
{
    UnknownVtbl *vtbl;
};

// IPluginBase: FUnknown, then initialize and terminate. IEditController continues it with
// setComponentState, setState, getState, getParameterCount, getParameterInfo.
struct IEditController;
struct IEditControllerVtbl
{
    int32_t (*queryInterface)(IEditController *, const unsigned char *, void **);
    uint32_t (*addRef)(IEditController *);
    uint32_t (*release)(IEditController *);
    int32_t (*initialize)(IEditController *, void *context);
    int32_t (*terminate)(IEditController *);
    void *setComponentState;
    void *setState;
    void *getState;
    int32_t (*getParameterCount)(IEditController *);
    int32_t (*getParameterInfo)(IEditController *, int32_t, ParameterInfo *);
};
struct IEditController
{
    IEditControllerVtbl *vtbl;
};

// The interface IDs, as the non-Windows build lays a FUID out: each of its four words big-endian.
// FUnknown 00000000 00000000 C0000000 00000046; IComponent E831FF31 F2D54301 928EBBEE 25697802;
// IEditController DCD7BBE3 7742448D A874AACC 979C759E; IHostApplication 58E595CC DB2D4969 8B6AAF8C
// 36A664E5.
const unsigned char kFUnknown[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46};
const unsigned char kIComponent[16] = {0xE8, 0x31, 0xFF, 0x31, 0xF2, 0xD5, 0x43, 0x01,
                                       0x92, 0x8E, 0xBB, 0xEE, 0x25, 0x69, 0x78, 0x02};
const unsigned char kIEditController[16] = {0xDC, 0xD7, 0xBB, 0xE3, 0x77, 0x42, 0x44, 0x8D,
                                            0xA8, 0x74, 0xAA, 0xCC, 0x97, 0x9C, 0x75, 0x9E};
const unsigned char kIHostApplication[16] = {0x58, 0xE5, 0x95, 0xCC, 0xDB, 0x2D, 0x49, 0x69,
                                             0x8B, 0x6A, 0xAF, 0x8C, 0x36, 0xA6, 0x64, 0xE5};

// A minimal IHostApplication (vst/ivsthostapplication.h): getName and a createInstance that
// creates nothing.
struct HostApplication;
struct HostApplicationVtbl
{
    int32_t (*queryInterface)(HostApplication *, const unsigned char *, void **);
    uint32_t (*addRef)(HostApplication *);
    uint32_t (*release)(HostApplication *);
    int32_t (*getName)(HostApplication *, char16_t *name);
    int32_t (*createInstance)(HostApplication *, const char *, const char *, void **);
};
struct HostApplication
{
    HostApplicationVtbl *vtbl;
};

int32_t hostQuery(HostApplication *self, const unsigned char *iid, void **obj)
{
    if (std::memcmp(iid, kFUnknown, 16) == 0 || std::memcmp(iid, kIHostApplication, 16) == 0)
    {
        *obj = self;
        return 0;
    }
    *obj = nullptr;
    return -1; // kNoInterface
}
uint32_t hostRef(HostApplication *) { return 1; }
int32_t hostName(HostApplication *, char16_t *name)
{
    const char *text = "gg-tests";
    std::size_t i = 0;
    for (; text[i] != 0; ++i)
    {
        name[i] = static_cast<char16_t>(text[i]);
    }
    name[i] = 0;
    return 0;
}
int32_t hostCreate(HostApplication *, const char *, const char *, void **obj)
{
    *obj = nullptr;
    return -1;
}
HostApplicationVtbl hostVtbl{hostQuery, hostRef, hostRef, hostName, hostCreate};
HostApplication host{&hostVtbl};

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

// One attempt: a new instance, its selector's facts. `selector` is true if one is there.
struct Selector
{
    bool found{false};
    bool programChange{false};
    int32_t stepCount{0};
    int32_t intruders{0}; // other IDs in the selector's block
    int32_t parameters{0};
};

bool inspect(IPluginFactory *factory, const unsigned char (&cid)[16], Selector &out)
{
    void *component = nullptr;
    if (factory->vtbl->createInstance(factory, reinterpret_cast<const char *>(cid),
                                      reinterpret_cast<const char *>(kIComponent),
                                      &component) != 0 ||
        component == nullptr)
    {
        std::fprintf(stderr, "createInstance(IComponent) failed\n");
        return false;
    }
    auto *unknown = static_cast<Unknown *>(component);
    void *controllerPtr = nullptr;
    if (unknown->vtbl->queryInterface(unknown, kIEditController, &controllerPtr) != 0 ||
        controllerPtr == nullptr)
    {
        std::fprintf(stderr, "the component has no IEditController\n");
        unknown->vtbl->release(unknown);
        return false;
    }
    auto *controller = static_cast<IEditController *>(controllerPtr);
    if (controller->vtbl->initialize(controller, &host) != 0)
    {
        std::fprintf(stderr, "IPluginBase::initialize failed\n");
        controller->vtbl->release(controller);
        unknown->vtbl->release(unknown);
        return false;
    }

    out = {};
    out.parameters = controller->vtbl->getParameterCount(controller);
    for (int32_t i = 0; i < out.parameters; ++i)
    {
        ParameterInfo info{};
        if (controller->vtbl->getParameterInfo(controller, i, &info) != 0)
        {
            continue;
        }
        if (info.id == kSelectorId)
        {
            out.found = true;
            out.programChange = (info.flags & kIsProgramChange) != 0;
            out.stepCount = info.stepCount;
        }
        else if (info.id > kSelectorId && info.id < kSelectorBlockEnd)
        {
            ++out.intruders;
        }
    }

    controller->vtbl->terminate(controller);
    controller->vtbl->release(controller);
    unknown->vtbl->release(unknown);
    return true;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 6)
    {
        std::fprintf(stderr, "usage: gg-vst3-preset-selector <bundle> <binary> <class name> "
                             "<expected id hex> <min steps>\n");
        return 2;
    }
    const std::string className = argv[3];
    const std::string expectedId = argv[4];
    const long minSteps = std::strtol(argv[5], nullptr, 10);
    if (expectedId.size() != 32 || minSteps < 1)
    {
        std::fprintf(stderr, "bad id (32 hex digits) or min steps (1 or more)\n");
        return 2;
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

    // The class by name, with the expected ID.
    PClassInfo info{};
    bool found = false;
    const int32_t count = factory->vtbl->countClasses(factory);
    for (int32_t i = 0; i < count && !found; ++i)
    {
        PClassInfo candidate{};
        if (factory->vtbl->getClassInfo(factory, i, &candidate) == 0 &&
            std::string(candidate.name, strnlen(candidate.name, sizeof(candidate.name))) ==
                className)
        {
            info = candidate;
            found = true;
        }
    }
    if (!found)
    {
        std::printf("FAIL: no class named \"%s\"\n", className.c_str());
        return 1;
    }
    unsigned char cid[16];
    std::memcpy(cid, info.cid, 16);
    if (hex(cid) != expectedId)
    {
        std::printf("FAIL: \"%s\" has id %s, expected %s\n", className.c_str(), hex(cid).c_str(),
                    expectedId.c_str());
        return 1;
    }

    // New instances until the shared preset index has finished its crawl (see the top).
    Selector selector;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    for (int attempt = 1;; ++attempt)
    {
        if (!inspect(factory, cid, selector))
        {
            return 2;
        }
        std::printf("attempt %d: %d parameters, selector %s, stepCount %d\n", attempt,
                    selector.parameters, selector.found ? "found" : "missing", selector.stepCount);
        if ((selector.found && selector.stepCount >= minSteps - 1) ||
            std::chrono::steady_clock::now() > deadline)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    bool ok = true;
    if (!selector.found)
    {
        std::printf("FAIL: no parameter with id 0x%X\n", kSelectorId);
        ok = false;
    }
    else
    {
        if (!selector.programChange)
        {
            std::printf("FAIL: the selector lacks kIsProgramChange\n");
            ok = false;
        }
        if (selector.stepCount < minSteps - 1)
        {
            std::printf("FAIL: stepCount %d, expected at least %ld\n", selector.stepCount,
                        minSteps - 1);
            ok = false;
        }
    }
    if (selector.intruders != 0)
    {
        std::printf("FAIL: %d other parameters have an id in [0xC00000, 0xC00010)\n",
                    selector.intruders);
        ok = false;
    }

    factory->vtbl->release(factory);
    bundleExit();
    CFRelease(bundle);
    CFRelease(url);
    std::printf(ok ? "VST3 preset selector as expected\n" : "VST3 preset selector differs\n");
    return ok ? 0 : 1;
}
