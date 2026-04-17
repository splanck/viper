//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/MacImportPlanner.cpp
// Purpose: macOS framework and dylib import planning for the native linker.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/PlatformImportPlanner.hpp"

#include "codegen/common/linker/DynamicSymbolPolicy.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper::codegen::linker {
namespace {

struct MacImportRule {
    const char *dylibPath;
    const char *const *prefixes;
    const char *const *exactSyms;
};

std::string stripLeadingUnderscores(const std::string &name) {
    size_t i = 0;
    while (i < name.size() && name[i] == '_')
        ++i;
    return (i > 0) ? name.substr(i) : name;
}

bool isObjcClassLookupSymbol(const std::string &name) {
    const std::string stripped = stripLeadingUnderscores(name);
    return stripped.rfind("OBJC_CLASS_$_", 0) == 0 || stripped.rfind("OBJC_METACLASS_$_", 0) == 0;
}

bool isObjcFrameworkTypeSymbol(const std::string &name) {
    const std::string stripped = stripLeadingUnderscores(name);
    return stripped.rfind("OBJC_CLASS_$_", 0) == 0 || stripped.rfind("OBJC_METACLASS_$_", 0) == 0 ||
           stripped.rfind("OBJC_EHTYPE_$_", 0) == 0;
}

std::string normalizeMacFrameworkSymbol(const std::string &name) {
    std::string normalized = stripLeadingUnderscores(name);
    static constexpr const char *kObjcPrefixes[] = {
        "OBJC_CLASS_$_",
        "OBJC_METACLASS_$_",
        "OBJC_EHTYPE_$_",
    };
    for (const char *prefix : kObjcPrefixes) {
        if (normalized.rfind(prefix, 0) == 0)
            return normalized.substr(std::char_traits<char>::length(prefix));
    }
    return normalized;
}

static constexpr const char *kMacNoMatches[] = {nullptr};
static constexpr const char *kMacLibSystemPrefixes[] = {
    "Block_",
    "NSConcrete",
    "dispatch_",
    "mach_",
    "task_",
    "host_",
    "vm_",
    "kern_",
    "os_",
    nullptr,
};
static constexpr const char *kMacLibSystemExact[] = {
    "_NSGetExecutablePath", "_Block_copy",        "_Block_release",     "_Block_object_assign",
    "_Block_object_dispose","dyld_stub_binder",   "_tlv_atexit",        "_tlv_bootstrap",
    "mach_timebase_info",   "mach_absolute_time", "mach_task_self_",    "mach_host_self",
    "task_info",            "host_page_size",     "_os_unfair_lock_lock","_os_unfair_lock_unlock",
    "os_unfair_lock_lock",  "os_unfair_lock_unlock", nullptr,
};
static constexpr const char *kMacCoreFoundationPrefixes[] = {"CF", "kCF", nullptr};
static constexpr const char *kMacFoundationPrefixes[] = {
    "NSString","NSAttributedString","NSArray","NSDictionary","NSSet","NSMutable","NSData","NSError",
    "NSURL","NSBundle","NSFileManager","NSDate","NSLocale","NSProcessInfo","NSRunLoop","NSTimer",
    "NSThread","NSNotification","NSIndexSet","NSCharacterSet","NSPredicate","NSCoder","NSJSON",
    "NSUserDefaults","NSAutoreleasePool","NSObject","NSDefaultRunLoop", nullptr,
};
static constexpr const char *kMacFoundationExact[] = {"NSLog","NSSearchPathForDirectoriesInDomains",nullptr};
static constexpr const char *kMacAppKitPrefixes[] = {
    "NSApp","NSApplication","NSWindow","NSView","NSColor","NSEvent","NSCursor","NSGraphicsContext",
    "NSOpenGL","NSMenu","NSMenuItem","NSScreen","NSImage","NSFont","NSResponder","NSPanel",
    "NSPasteboard","NSText","NSControl","NSButton","NSScroll","NSTable","NSOutline","NSBezierPath",
    "NSMake","NSRect","NSPoint","NSSize","NSDrag","NSBackingStore","NSWindowStyle",
    "NSApplicationActivationPolicy", nullptr,
};
static constexpr const char *kMacCoreGraphicsPrefixes[] = {"CG", "kCG", nullptr};
static constexpr const char *kMacIOKitPrefixes[] = {"IOKit","IOHID","IOService","IORegistryEntry",nullptr};
static constexpr const char *kMacObjCPrefixes[] = {"objc_", "OBJC_", "_objc_", nullptr};
static constexpr const char *kMacObjCExact[] = {"sel_registerName", "sel_getName", nullptr};
static constexpr const char *kMacUTIPrefixes[] = {"UTType", "UTCopy", nullptr};
static constexpr const char *kMacAudioToolboxPrefixes[] = {"AudioQueue", "AudioServices", "AudioComponent", nullptr};
static constexpr const char *kMacCoreAudioPrefixes[] = {"AudioObject", "AudioDevice", nullptr};
static constexpr const char *kMacMetalPrefixes[] = {"MTLCreate", "MTL", nullptr};
static constexpr const char *kMacQuartzCorePrefixes[] = {"CAMetalLayer","CATransaction","CALayer","CAAnimation","CAMediaTiming",nullptr};
static constexpr const char *kMacSecurityPrefixes[] = {"Sec", "kSec", nullptr};

static constexpr MacImportRule kMacImportRules[] = {
    {"/usr/lib/libSystem.B.dylib", kMacLibSystemPrefixes, kMacLibSystemExact},
    {"/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation", kMacCoreFoundationPrefixes, kMacNoMatches},
    {"/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation", kMacFoundationPrefixes, kMacFoundationExact},
    {"/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit", kMacAppKitPrefixes, kMacNoMatches},
    {"/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics", kMacCoreGraphicsPrefixes, kMacNoMatches},
    {"/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit", kMacIOKitPrefixes, kMacNoMatches},
    {"/usr/lib/libobjc.A.dylib", kMacObjCPrefixes, kMacObjCExact},
    {"/System/Library/Frameworks/UniformTypeIdentifiers.framework/Versions/A/UniformTypeIdentifiers", kMacUTIPrefixes, kMacNoMatches},
    {"/System/Library/Frameworks/AudioToolbox.framework/Versions/A/AudioToolbox", kMacAudioToolboxPrefixes, kMacNoMatches},
    {"/System/Library/Frameworks/CoreAudio.framework/Versions/A/CoreAudio", kMacCoreAudioPrefixes, kMacNoMatches},
    {"/System/Library/Frameworks/Metal.framework/Versions/A/Metal", kMacMetalPrefixes, kMacNoMatches},
    {"/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore", kMacQuartzCorePrefixes, kMacNoMatches},
    {"/System/Library/Frameworks/Security.framework/Versions/A/Security", kMacSecurityPrefixes, kMacNoMatches},
};

bool macSymbolMatchesRule(const std::string &sym, const MacImportRule &rule) {
    const std::string stripped = stripLeadingUnderscores(sym);
    const std::string normalized = normalizeMacFrameworkSymbol(sym);

    for (const char *const *e = rule.exactSyms; e != nullptr && *e != nullptr; ++e) {
        if (sym == *e || stripped == *e || normalized == *e)
            return true;
    }
    for (const char *const *p = rule.prefixes; p != nullptr && *p != nullptr; ++p) {
        if (sym.find(*p) == 0 || stripped.find(*p) == 0 || normalized.find(*p) == 0)
            return true;
    }
    return false;
}

const MacImportRule *findMacImportRule(const std::string &sym) {
    for (const auto &rule : kMacImportRules) {
        if (isObjcFrameworkTypeSymbol(sym) &&
            std::string(rule.dylibPath) == "/usr/lib/libobjc.A.dylib") {
            continue;
        }
        if (macSymbolMatchesRule(sym, rule))
            return &rule;
    }
    return nullptr;
}

bool isMacFrameworkLikeSymbol(const std::string &sym) {
    static constexpr const char *kFrameworkPrefixes[] = {
        "CF","kCF","CG","kCG","NS","IOKit","IOHID","IOService","IORegistryEntry","objc_","OBJC_",
        "_objc_","UTType","UTCopy","AudioQueue","AudioServices","AudioComponent","AudioObject",
        "AudioDevice","MTL","Sec","kSec","CAMetalLayer","CATransaction","CALayer", nullptr,
    };

    const std::string stripped = stripLeadingUnderscores(sym);
    const std::string normalized = normalizeMacFrameworkSymbol(sym);
    for (const char *const *p = kFrameworkPrefixes; *p != nullptr; ++p) {
        if (sym.find(*p) == 0 || stripped.find(*p) == 0 || normalized.find(*p) == 0)
            return true;
    }
    return false;
}

uint32_t ensureMacDylibOrdinal(const char *path,
                               MacImportPlan &plan,
                               std::unordered_map<std::string, uint32_t> &pathToOrdinal) {
    auto it = pathToOrdinal.find(path);
    if (it != pathToOrdinal.end())
        return it->second;

    plan.dylibs.push_back({path});
    const uint32_t ordinal = static_cast<uint32_t>(plan.dylibs.size());
    pathToOrdinal.emplace(path, ordinal);
    return ordinal;
}

} // namespace

bool planMacImports(const std::unordered_set<std::string> &dynamicSyms,
                    MacImportPlan &plan,
                    std::ostream &err) {
    static constexpr const char *kCocoaPath =
        "/System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa";
    static constexpr const char *kLibcxxPath = "/usr/lib/libc++.1.dylib";

    std::unordered_map<std::string, uint32_t> pathToOrdinal;
    ensureMacDylibOrdinal("/usr/lib/libSystem.B.dylib", plan, pathToOrdinal);

    std::vector<std::string> sortedSyms(dynamicSyms.begin(), dynamicSyms.end());
    std::sort(sortedSyms.begin(), sortedSyms.end());

    for (const auto &sym : sortedSyms) {
        if (const MacImportRule *rule = findMacImportRule(sym)) {
            const uint32_t ordinal = ensureMacDylibOrdinal(rule->dylibPath, plan, pathToOrdinal);
            plan.symOrdinals[sym] = isObjcClassLookupSymbol(sym) ? 0 : ordinal;
            continue;
        }

        if (isKnownMacLibcxxDynamicSymbol(sym)) {
            const uint32_t ordinal = ensureMacDylibOrdinal(kLibcxxPath, plan, pathToOrdinal);
            plan.symOrdinals[sym] = ordinal;
            continue;
        }

        const std::string normalized = normalizeMacFrameworkSymbol(sym);
        if (isObjcClassLookupSymbol(sym) && normalized.rfind("NS", 0) == 0) {
            ensureMacDylibOrdinal(kCocoaPath, plan, pathToOrdinal);
            plan.symOrdinals[sym] = 0;
            continue;
        }

        if (!isMacFrameworkLikeSymbol(sym) && isKnownDynamicSymbol(sym, LinkPlatform::macOS)) {
            plan.symOrdinals[sym] = 1;
            continue;
        }

        err << "error: macOS import symbol '" << sym
            << "' is unresolved but has no dylib mapping\n";
        return false;
    }

    return true;
}

} // namespace viper::codegen::linker
