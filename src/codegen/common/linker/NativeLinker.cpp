//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/NativeLinker.cpp
// Purpose: Top-level native linker implementation.
// Key invariants:
//   - Pipeline: parse .o → parse archives → resolve symbols → generate stubs →
//     merge sections → branch trampolines → apply relocations → write executable
//   - For macOS: generates GOT entries and stub trampolines for dynamic symbols,
//     uses non-lazy binding (dyld fills GOT at load time)
//   - Falls back gracefully with clear error messages
// Links: codegen/common/linker/NativeLinker.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/NativeLinker.hpp"

#include "codegen/common/linker/ArchiveReader.hpp"
#include "codegen/common/linker/BranchTrampoline.hpp"
#include "codegen/common/linker/DeadStripPass.hpp"
#include "codegen/common/linker/DynStubGen.hpp"
#include "codegen/common/linker/ElfExeWriter.hpp"
#include "codegen/common/linker/ICF.hpp"
#include "codegen/common/linker/MachOExeWriter.hpp"
#include "codegen/common/linker/NameMangling.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/PeExeWriter.hpp"
#include "codegen/common/linker/RelocApplier.hpp"
#include "codegen/common/linker/RelocConstants.hpp"
#include "codegen/common/linker/SectionMerger.hpp"
#include "codegen/common/linker/StringDedup.hpp"
#include "codegen/common/linker/SymbolResolver.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace viper::codegen::linker {

namespace {

struct WindowsImportPlan {
    ObjFile obj;
    std::vector<DllImport> imports;
};

void registerSyntheticSymbols(const ObjFile &obj,
                              size_t objIdx,
                              std::unordered_map<std::string, GlobalSymEntry> &globalSyms) {
    for (size_t i = 1; i < obj.symbols.size(); ++i) {
        const auto &sym = obj.symbols[i];
        if (sym.binding == ObjSymbol::Local || sym.binding == ObjSymbol::Undefined || sym.name.empty())
            continue;

        GlobalSymEntry e;
        e.name = sym.name;
        e.binding = GlobalSymEntry::Global;
        e.objIndex = objIdx;
        e.secIndex = sym.sectionIndex;
        e.offset = sym.offset;
        globalSyms[sym.name] = std::move(e);
    }
}

void removeDynamicSymbol(const char *name, std::unordered_set<std::string> &dynamicSyms) {
    dynamicSyms.erase(name);
}

ObjFile makeUndefinedRootObject(const ObjFile &userObj, const std::string &symbolName) {
    ObjFile root;
    root.name = "<entry-root>";
    root.format = userObj.format;
    root.is64bit = userObj.is64bit;
    root.isLittleEndian = userObj.isLittleEndian;
    root.machine = userObj.machine;
    root.sections.push_back(ObjSection{});
    root.symbols.push_back(ObjSymbol{});

    ObjSymbol sym;
    sym.name = symbolName;
    sym.binding = ObjSymbol::Undefined;
    root.symbols.push_back(std::move(sym));
    return root;
}

std::string stripImpPrefix(const std::string &name) {
    if (name.rfind("__imp_", 0) == 0)
        return name.substr(6);
    return name;
}

bool usesDebugWindowsRuntime(const std::vector<std::string> &archivePaths) {
    for (const auto &path : archivePaths) {
        std::string lower = path;
        std::transform(lower.begin(),
                       lower.end(),
                       lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.find("\\debug\\") != std::string::npos || lower.find("/debug/") != std::string::npos ||
            lower.rfind("msvcrtd.lib") != std::string::npos ||
            lower.rfind("ucrtd.lib") != std::string::npos ||
            lower.rfind("vcruntimed.lib") != std::string::npos)
            return true;
    }
    return false;
}

bool isWindowsHelperSymbol(const std::string &name) {
    return name == "_fltused" || name == "__security_cookie" || name == "__security_check_cookie" ||
           name == "__security_init_cookie" || name == "__GSHandlerCheck" ||
           name == "_RTC_InitBase" || name == "_RTC_Shutdown" ||
           name == "_RTC_CheckStackVars" || name == "__report_rangecheckfailure" ||
           name == "__chkstk" || name == "_tls_index" ||
           name == "?_OptionsStorage@?1??__local_stdio_printf_options@@9@9" ||
           name == "vm_trap" || name == "rt_audio_shutdown";
}

std::string dllForImport(const std::string &name, bool debugRuntime) {
    static const std::unordered_set<std::string> kernel32 = {
        "ExitProcess",          "GetCurrentThreadId",   "GetEnvironmentVariableA",
        "GetLastError",         "GetStdHandle",         "InitOnceExecuteOnce",
        "InitializeCriticalSection",
        "LeaveCriticalSection", "DeleteCriticalSection", "EnterCriticalSection",
        "SetEnvironmentVariableA",
        "SetErrorMode",         "SwitchToThread",       "WriteFile",
        "GetTickCount64",       "AddVectoredExceptionHandler",
        "InitializeSRWLock",    "AcquireSRWLockExclusive",
        "AcquireSRWLockShared", "ReleaseSRWLockExclusive",
        "ReleaseSRWLockShared",
    };
    static const std::unordered_set<std::string> advapi32 = {
        "CryptAcquireContextA",
        "CryptGenRandom",
        "CryptReleaseContext",
    };
    static const std::unordered_set<std::string> bcrypt = {"BCryptGenRandom"};

    if (kernel32.count(name))
        return "kernel32.dll";
    if (advapi32.count(name))
        return "advapi32.dll";
    if (bcrypt.count(name))
        return "bcrypt.dll";

    if (name == "__C_specific_handler" || name == "__C_specific_handler_noexcept" ||
        name == "__current_exception" || name == "__current_exception_context" ||
        name.rfind("__vcrt_", 0) == 0) {
        return debugRuntime ? "VCRUNTIME140D.dll" : "VCRUNTIME140.dll";
    }

    return debugRuntime ? "ucrtbased.dll" : "ucrtbase.dll";
}

std::string importNameForSymbol(const std::string &name) {
    static const std::unordered_map<std::string, std::string> remap = {
        {"atexit", "_crt_atexit"},
        {"close", "_close"},
        {"lseek", "_lseek"},
        {"open", "_open"},
        {"read", "_read"},
        {"strdup", "_strdup"},
        {"unlink", "_unlink"},
        {"write", "_write"},
    };

    auto it = remap.find(name);
    if (it != remap.end())
        return it->second;
    return name;
}

WindowsImportPlan generateWindowsX64Imports(const std::unordered_set<std::string> &dynamicSyms,
                                            bool debugRuntime) {
    WindowsImportPlan plan;
    plan.obj.name = "<win64-imports>";
    plan.obj.format = ObjFileFormat::COFF;
    plan.obj.is64bit = true;
    plan.obj.isLittleEndian = true;
    plan.obj.machine = 0x8664;
    plan.obj.sections.push_back(ObjSection{});
    plan.obj.symbols.push_back(ObjSymbol{});

    ObjSection textSec;
    textSec.name = ".text";
    textSec.executable = true;
    textSec.alloc = true;
    textSec.alignment = 16;

    ObjSection dataSec;
    dataSec.name = ".data";
    dataSec.writable = true;
    dataSec.alloc = true;
    dataSec.alignment = 8;

    std::unordered_map<std::string, std::vector<std::string>> dllToFuncs;
    std::unordered_set<std::string> seenFuncs;
    for (const auto &sym : dynamicSyms) {
        if (isWindowsHelperSymbol(sym) || sym == "__ImageBase")
            continue;
        const std::string base = stripImpPrefix(sym);
        if (base.rfind("rt_", 0) == 0)
            continue;
        if (!seenFuncs.insert(base).second)
            continue;
        dllToFuncs[dllForImport(base, debugRuntime)].push_back(base);
    }

    std::vector<std::string> dllNames;
    dllNames.reserve(dllToFuncs.size());
    for (const auto &[dll, _] : dllToFuncs)
        dllNames.push_back(dll);
    std::sort(dllNames.begin(), dllNames.end());

    for (const auto &dll : dllNames) {
        auto funcs = dllToFuncs[dll];
        std::sort(funcs.begin(), funcs.end());
        DllImport dllImport;
        dllImport.dllName = dll;
        dllImport.functions = funcs;
        for (const auto &fn : funcs) {
            const std::string importName = importNameForSymbol(fn);
            if (importName != fn)
                dllImport.importNames.emplace(fn, importName);
        }
        plan.imports.push_back(std::move(dllImport));

        for (const auto &fn : funcs) {
            const size_t slotOff = dataSec.data.size();
            dataSec.data.resize(slotOff + 8, 0);

            ObjSymbol slotSym;
            slotSym.name = "__imp_" + fn;
            slotSym.binding = ObjSymbol::Global;
            slotSym.sectionIndex = 2;
            slotSym.offset = slotOff;
            const uint32_t slotSymIdx = static_cast<uint32_t>(plan.obj.symbols.size());
            plan.obj.symbols.push_back(std::move(slotSym));

            const size_t stubOff = textSec.data.size();
            textSec.data.insert(textSec.data.end(), {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00});

            ObjSymbol stubSym;
            stubSym.name = fn;
            stubSym.binding = ObjSymbol::Global;
            stubSym.sectionIndex = 1;
            stubSym.offset = stubOff;
            plan.obj.symbols.push_back(std::move(stubSym));

            ObjReloc reloc;
            reloc.offset = stubOff + 2;
            reloc.type = coff_x64::kRel32;
            reloc.symIndex = slotSymIdx;
            reloc.addend = 0;
            textSec.relocs.push_back(reloc);
        }

        dataSec.data.resize(dataSec.data.size() + 8, 0);
    }

    if (!textSec.data.empty())
        plan.obj.sections.push_back(std::move(textSec));
    if (!dataSec.data.empty()) {
        if (plan.obj.sections.size() == 1)
            plan.obj.sections.push_back(ObjSection{});
        plan.obj.sections.push_back(std::move(dataSec));
    }

    return plan;
}

ObjFile generateWindowsX64Helpers(const std::unordered_set<std::string> &dynamicSyms,
                                  bool haveVmTrapDefault) {
    ObjFile obj;
    obj.name = "<win64-helpers>";
    obj.format = ObjFileFormat::COFF;
    obj.is64bit = true;
    obj.isLittleEndian = true;
    obj.machine = 0x8664;
    obj.sections.push_back(ObjSection{});
    obj.symbols.push_back(ObjSymbol{});

    ObjSection textSec;
    textSec.name = ".text";
    textSec.executable = true;
    textSec.alloc = true;
    textSec.alignment = 16;

    ObjSection dataSec;
    dataSec.name = ".data";
    dataSec.writable = true;
    dataSec.alloc = true;
    dataSec.alignment = 8;

    auto addRetFn = [&](const std::string &name, std::initializer_list<uint8_t> bytes) {
        const size_t off = textSec.data.size();
        textSec.data.insert(textSec.data.end(), bytes.begin(), bytes.end());
        ObjSymbol sym;
        sym.name = name;
        sym.binding = ObjSymbol::Global;
        sym.sectionIndex = 1;
        sym.offset = off;
        obj.symbols.push_back(std::move(sym));
    };

    auto addData = [&](const std::string &name, const std::vector<uint8_t> &bytes, uint32_t align) {
        while ((dataSec.data.size() % align) != 0)
            dataSec.data.push_back(0);
        const size_t off = dataSec.data.size();
        dataSec.data.insert(dataSec.data.end(), bytes.begin(), bytes.end());
        ObjSymbol sym;
        sym.name = name;
        sym.binding = ObjSymbol::Global;
        sym.sectionIndex = 2;
        sym.offset = off;
        obj.symbols.push_back(std::move(sym));
    };

    if (dynamicSyms.count("_fltused"))
        addData("_fltused", {1, 0, 0, 0}, 4);
    if (dynamicSyms.count("__security_cookie"))
        addData("__security_cookie", {0x32, 0xA2, 0xDF, 0x2D, 0x99, 0x2B, 0x00, 0x00}, 8);
    if (dynamicSyms.count("_tls_index"))
        addData("_tls_index", {0, 0, 0, 0}, 4);
    if (dynamicSyms.count("?_OptionsStorage@?1??__local_stdio_printf_options@@9@9"))
        addData("?_OptionsStorage@?1??__local_stdio_printf_options@@9@9",
                {0, 0, 0, 0, 0, 0, 0, 0},
                8);

    if (dynamicSyms.count("__security_check_cookie"))
        addRetFn("__security_check_cookie", {0xC3});
    if (dynamicSyms.count("__security_init_cookie"))
        addRetFn("__security_init_cookie", {0xC3});
    if (dynamicSyms.count("__GSHandlerCheck"))
        addRetFn("__GSHandlerCheck", {0xC3});
    if (dynamicSyms.count("_RTC_CheckStackVars"))
        addRetFn("_RTC_CheckStackVars", {0xC3});
    if (dynamicSyms.count("_RTC_InitBase"))
        addRetFn("_RTC_InitBase", {0x31, 0xC0, 0xC3});
    if (dynamicSyms.count("_RTC_Shutdown"))
        addRetFn("_RTC_Shutdown", {0xC3});
    if (dynamicSyms.count("__report_rangecheckfailure"))
        addRetFn("__report_rangecheckfailure", {0xCC, 0xC3});
    if (dynamicSyms.count("__chkstk"))
        addRetFn("__chkstk", {0xC3});
    if (dynamicSyms.count("rt_audio_shutdown"))
        addRetFn("rt_audio_shutdown", {0xC3});

    if (dynamicSyms.count("vm_trap")) {
        const size_t off = textSec.data.size();
        textSec.data.insert(textSec.data.end(), {0xE9, 0x00, 0x00, 0x00, 0x00});

        ObjSymbol vmTrap;
        vmTrap.name = "vm_trap";
        vmTrap.binding = ObjSymbol::Global;
        vmTrap.sectionIndex = 1;
        vmTrap.offset = off;
        obj.symbols.push_back(std::move(vmTrap));

        ObjSymbol target;
        target.name = haveVmTrapDefault ? "vm_trap_default" : "rt_abort";
        target.binding = ObjSymbol::Undefined;
        const uint32_t targetIdx = static_cast<uint32_t>(obj.symbols.size());
        obj.symbols.push_back(std::move(target));

        ObjReloc reloc;
        reloc.offset = off + 1;
        reloc.type = coff_x64::kRel32;
        reloc.symIndex = targetIdx;
        reloc.addend = 0;
        textSec.relocs.push_back(reloc);
    }

    if (!textSec.data.empty())
        obj.sections.push_back(std::move(textSec));
    if (!dataSec.data.empty()) {
        if (obj.sections.size() == 1)
            obj.sections.push_back(ObjSection{});
        obj.sections.push_back(std::move(dataSec));
    }
    return obj;
}

} // namespace

int nativeLink(const NativeLinkerOptions &opts, std::ostream & /*out*/, std::ostream &err) {
    // Step 1: Read the user's object file.
    ObjFile userObj;
    if (!readObjFile(opts.objPath, userObj, err)) {
        err << "error: failed to read object file '" << opts.objPath << "'\n";
        return 1;
    }

    // Step 2: Read all archive files.
    std::vector<Archive> archives;
    for (const auto &arPath : opts.archivePaths) {
        Archive ar;
        if (!readArchive(arPath, ar, err)) {
            err << "warning: failed to read archive '" << arPath << "', skipping\n";
            continue;
        }
        archives.push_back(std::move(ar));
    }

    // Step 3: Symbol resolution (iterative archive extraction).
    std::vector<ObjFile> initialObjects = {userObj};
    if (!opts.entrySymbol.empty())
        initialObjects.push_back(makeUndefinedRootObject(userObj, opts.entrySymbol));
    std::unordered_map<std::string, GlobalSymEntry> globalSyms;
    std::vector<ObjFile> allObjects;
    std::unordered_set<std::string> dynamicSyms;
    const bool debugWindowsRuntime =
        opts.platform == LinkPlatform::Windows &&
        (usesDebugWindowsRuntime(opts.archivePaths)
#if defined(NDEBUG)
             || false
#else
             || true
#endif
        );

    if (!resolveSymbols(initialObjects, archives, globalSyms, allObjects, dynamicSyms, err)) {
        err << "error: symbol resolution failed\n";
        return 1;
    }
    // Step 3.5a: Generate ObjC selector stubs (macOS — objc_msgSend$selector symbols).
    // Must come before dynamic stubs since it moves symbols from dynamicSyms and
    // ensures objc_msgSend itself is in the dynamic set.
    if (opts.arch == LinkArch::AArch64 && opts.platform == LinkPlatform::macOS) {
        ObjFile objcStubs = generateObjcSelectorStubsAArch64(dynamicSyms);
        if (!objcStubs.sections.empty()) {
            const size_t objcIdx = allObjects.size();
            allObjects.push_back(std::move(objcStubs));

            const auto &stubs = allObjects[objcIdx];
            for (size_t i = 1; i < stubs.symbols.size(); ++i) {
                const auto &sym = stubs.symbols[i];
                if (sym.binding == ObjSymbol::Local || sym.binding == ObjSymbol::Undefined)
                    continue;
                GlobalSymEntry e;
                e.name = sym.name;
                e.binding = GlobalSymEntry::Global;
                e.objIndex = objcIdx;
                e.secIndex = sym.sectionIndex;
                e.offset = sym.offset;
                globalSyms[sym.name] = std::move(e);
            }
        }
    }

    // Step 3.5b: Generate dynamic symbol stubs (macOS/ELF — needed for shared library imports).
    std::vector<DllImport> peImports;
    std::unordered_map<std::string, uint32_t> peImportSlotRvas;
    if (opts.platform == LinkPlatform::Windows && opts.arch == LinkArch::X86_64) {
        dynamicSyms.erase("__ImageBase");
        const bool haveVmTrapDefault = globalSyms.find("vm_trap_default") != globalSyms.end();

        ObjFile helperObj = generateWindowsX64Helpers(dynamicSyms, haveVmTrapDefault);
        if (!helperObj.sections.empty()) {
            const size_t helperIdx = allObjects.size();
            allObjects.push_back(std::move(helperObj));
            registerSyntheticSymbols(allObjects[helperIdx], helperIdx, globalSyms);
            for (const auto &sym : allObjects[helperIdx].symbols) {
                if (sym.binding == ObjSymbol::Local || sym.binding == ObjSymbol::Undefined ||
                    sym.name.empty())
                    continue;
                dynamicSyms.erase(sym.name);
            }
        }

        WindowsImportPlan importPlan = generateWindowsX64Imports(dynamicSyms, debugWindowsRuntime);
        peImports = importPlan.imports;
        if (!importPlan.obj.sections.empty()) {
            const size_t importIdx = allObjects.size();
            allObjects.push_back(std::move(importPlan.obj));
            registerSyntheticSymbols(allObjects[importIdx], importIdx, globalSyms);
            for (const auto &imp : peImports) {
                for (const auto &fn : imp.functions) {
                    dynamicSyms.erase(fn);
                    dynamicSyms.erase("__imp_" + fn);
                }
            }
        }
    }

    const bool supportsDynamicStubs =
        opts.platform == LinkPlatform::macOS && opts.arch == LinkArch::AArch64;
    if (!dynamicSyms.empty() && supportsDynamicStubs) {
        ObjFile stubObj = generateDynStubsAArch64(dynamicSyms);
        const size_t stubObjIdx = allObjects.size();
        allObjects.push_back(std::move(stubObj));

        // Manually register stub and GOT symbols in globalSyms.
        // This overrides Dynamic entries with Global entries pointing to stubs.
        const auto &stubs = allObjects[stubObjIdx];
        for (size_t i = 1; i < stubs.symbols.size(); ++i) {
            const auto &sym = stubs.symbols[i];
            if (sym.binding == ObjSymbol::Local)
                continue;

            GlobalSymEntry e;
            e.name = sym.name;
            e.binding = GlobalSymEntry::Global;
            e.objIndex = stubObjIdx;
            e.secIndex = sym.sectionIndex;
            e.offset = sym.offset;
            globalSyms[sym.name] = std::move(e);
        }
    }

    if (opts.platform == LinkPlatform::Windows && opts.arch == LinkArch::X86_64) {
        dynamicSyms.erase("__ImageBase");
        dynamicSyms.erase("vm_trap");
    }

    if (!dynamicSyms.empty() && !supportsDynamicStubs) {
        std::vector<std::string> unsupported(dynamicSyms.begin(), dynamicSyms.end());
        std::sort(unsupported.begin(), unsupported.end());

        err << "error: native linker does not support dynamic imports on this target\n";
        err << "error: unresolved dynamic symbols:";
        for (const auto &sym : unsupported)
            err << ' ' << sym;
        err << "\nerror: use the system linker for programs that depend on CRT or OS import "
               "libraries\n";
        return 1;
    }

    // Step 3.5c: Dead-strip unused sections from archive-extracted objects.
    // User objects (index 0) are always live; archive extracts are GC'd.
    deadStrip(allObjects, initialObjects.size(), globalSyms, opts.entrySymbol, err);

    // Step 3.5d: Deduplicate identical rodata strings across object files.
    deduplicateStrings(allObjects, globalSyms);

    // Step 3.5d2: Fold identical .text sections (Identical Code Folding).
    foldIdenticalCode(allObjects, globalSyms);

    // Step 3.5e: Remove global symbols that reference stripped (empty) sections.
    // After dead stripping, sections with cleared data are skipped by the merger,
    // so symbols pointing to them would resolve to invalid addresses.
    {
        std::vector<std::string> deadSyms;
        for (const auto &[name, entry] : globalSyms) {
            if (entry.binding == GlobalSymEntry::Dynamic)
                continue; // Dynamic symbols have no section reference.
            if (entry.objIndex < allObjects.size() &&
                entry.secIndex < allObjects[entry.objIndex].sections.size() &&
                allObjects[entry.objIndex].sections[entry.secIndex].data.empty()) {
                deadSyms.push_back(name);
            }
        }
        for (const auto &name : deadSyms)
            globalSyms.erase(name);
    }

    // Step 4: Merge sections and compute layout.
    LinkLayout layout;
    layout.globalSyms = std::move(globalSyms);
    if (!mergeSections(allObjects, opts.platform, opts.arch, layout, err)) {
        err << "error: section merging failed\n";
        return 1;
    }

    // Step 5: Insert branch trampolines for out-of-range AArch64 B/BL instructions.
    if (!insertBranchTrampolines(allObjects, layout, opts.arch, opts.platform, err)) {
        err << "error: branch trampoline insertion failed\n";
        return 1;
    }

    // Step 6: Apply relocations. This also resolves final symbol addresses.
    if (!applyRelocations(allObjects, layout, dynamicSyms, opts.platform, opts.arch, err)) {
        err << "error: relocation application failed\n";
        return 1;
    }

    // Step 6.25: Resolve the final entry point after symbol addresses are known.
    {
        auto it = findWithMachoFallback(layout.globalSyms, opts.entrySymbol);
        if (it != layout.globalSyms.end())
            layout.entryAddr = it->second.resolvedAddr;
    }

    // Step 6.5: Build GOT entry table for the executable writer (needed for bind opcodes).
    for (const auto &[name, entry] : layout.globalSyms) {
        if (name.size() > 6 && name.substr(0, 6) == "__got_") {
            GotEntry ge;
            ge.symbolName = name.substr(6); // Remove "__got_" prefix → original symbol name.
            ge.gotAddr = entry.resolvedAddr;
            layout.gotEntries.push_back(std::move(ge));
        }
    }
    std::sort(layout.gotEntries.begin(),
              layout.gotEntries.end(),
              [](const GotEntry &a, const GotEntry &b) { return a.symbolName < b.symbolName; });

    // Step 7: Write executable.
    bool writeOk = false;
    switch (opts.platform) {
        case LinkPlatform::Linux:
            writeOk = writeElfExe(opts.exePath, layout, opts.arch, err);
            break;
        case LinkPlatform::macOS: {
            std::vector<DylibImport> dylibs;
            // Always link libSystem.B.dylib on macOS.
            dylibs.push_back({"/usr/lib/libSystem.B.dylib"});

            // Detect required frameworks from dynamic symbol prefixes.
            // Each entry: {list of symbol prefixes, dylib path}.
            // A framework is linked if ANY dynamic symbol starts with one of its prefixes.
            struct FrameworkRule {
                const char *prefixes[10]; // NUL-terminated list of symbol prefixes.
                const char *exactSyms[3]; // NUL-terminated list of exact-match symbol names.
                const char *dylibPath;
            };

            static constexpr FrameworkRule kFrameworkRules[] = {
                {{"CF", "kCF"},
                 {},
                 "/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation"},
                {{"NS"}, {}, "/System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa"},
                {{"NSApp",
                  "NSWindow",
                  "NSView",
                  "NSColor",
                  "NSEvent",
                  "NSCursor",
                  "NSGraphicsContext",
                  "NSOpenGL",
                  "NSApplication"},
                 {},
                 "/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit"},
                {{"CG"},
                 {},
                 "/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics"},
                {{"NSLog", "NSSearchPathForDirectories"},
                 {},
                 "/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation"},
                {{"IOKit", "IOHID", "IOService", "IORegistryEntry"},
                 {},
                 "/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit"},
                {{"objc_", "OBJC_"},
                 {"sel_registerName", "sel_getName"},
                 "/usr/lib/libobjc.A.dylib"},
                {{"UTType", "UTCopy"},
                 {},
                 "/System/Library/Frameworks/UniformTypeIdentifiers.framework/Versions/A/"
                 "UniformTypeIdentifiers"},
                {{"AudioQueue", "AudioServices"},
                 {},
                 "/System/Library/Frameworks/AudioToolbox.framework/Versions/A/AudioToolbox"},
                {{"MTL", "MTLCreate"},
                 {},
                 "/System/Library/Frameworks/Metal.framework/Versions/A/Metal"},
                {{"CAMetalLayer", "CATransaction", "CALayer"},
                 {},
                 "/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore"},
            };

            // Helper: strip all leading underscores from a symbol name for
            // prefix matching. Needed because some C symbols retain underscores
            // after the MachOReader strips the Mach-O mangling prefix (e.g.,
            // __CFConstantStringClassReference, _objc_empty_cache).
            auto stripUnderscores = [](const std::string &s) -> std::string {
                size_t i = 0;
                while (i < s.size() && s[i] == '_')
                    ++i;
                return (i > 0) ? s.substr(i) : s;
            };

            for (const auto &rule : kFrameworkRules) {
                bool needed = false;
                for (const auto &sym : dynamicSyms) {
                    const std::string stripped = stripUnderscores(sym);
                    for (const char *const *p = rule.prefixes; *p; ++p) {
                        if (sym.find(*p) == 0 || stripped.find(*p) == 0) {
                            needed = true;
                            break;
                        }
                    }
                    if (!needed) {
                        for (const char *const *e = rule.exactSyms; *e; ++e) {
                            if (sym == *e) {
                                needed = true;
                                break;
                            }
                        }
                    }
                    if (needed)
                        break;
                }
                if (needed)
                    dylibs.push_back({rule.dylibPath});
            }

            // Build symbol-to-dylib ordinal map for MH_TWOLEVEL.
            // Ordinal = 1-based index into the dylibs vector.
            // Ordinal 0 = flat lookup (for ObjC class/metaclass symbols).
            std::unordered_map<std::string, uint32_t> symOrdinals;
            {
                // Map dylib path → ordinal (1-based).
                std::unordered_map<std::string, uint32_t> pathToOrdinal;
                for (size_t di = 0; di < dylibs.size(); ++di)
                    pathToOrdinal[dylibs[di].path] = static_cast<uint32_t>(di + 1);

                for (const auto &sym : dynamicSyms) {
                    // ObjC class/metaclass symbols use flat lookup — they live in
                    // the framework that defines the class, which can't be determined
                    // from the symbol prefix alone (e.g., OBJC_CLASS_$_NSWindow is
                    // in AppKit, not libobjc).
                    if (sym.find("OBJC_CLASS_$_") == 0 || sym.find("OBJC_METACLASS_$_") == 0) {
                        symOrdinals[sym] = 0; // flat lookup
                        continue;
                    }

                    // Try prefix/exact matching against framework rules.
                    bool matched = false;
                    const std::string stripped = stripUnderscores(sym);
                    for (const auto &rule : kFrameworkRules) {
                        // Try prefix match against both raw and underscore-stripped name.
                        for (const char *const *p = rule.prefixes; *p; ++p) {
                            if (sym.find(*p) == 0 || stripped.find(*p) == 0) {
                                auto pit = pathToOrdinal.find(rule.dylibPath);
                                if (pit != pathToOrdinal.end())
                                    symOrdinals[sym] = pit->second;
                                matched = true;
                                break;
                            }
                        }
                        if (!matched) {
                            for (const char *const *e = rule.exactSyms; *e; ++e) {
                                if (sym == *e) {
                                    auto pit = pathToOrdinal.find(rule.dylibPath);
                                    if (pit != pathToOrdinal.end())
                                        symOrdinals[sym] = pit->second;
                                    matched = true;
                                    break;
                                }
                            }
                        }
                        if (matched)
                            break;
                    }

                    // Default: ordinal 1 (libSystem.B.dylib).
                    if (!matched)
                        symOrdinals[sym] = 1;
                }
            }

            writeOk = writeMachOExe(
                opts.exePath, layout, opts.arch, dylibs, dynamicSyms, symOrdinals, err);
            break;
        }
        case LinkPlatform::Windows: {
            if (peImports.empty())
                peImports.push_back({"kernel32.dll", {"ExitProcess"}, {}});
            for (const auto &imp : peImports) {
                for (const auto &fn : imp.functions) {
                    auto it = layout.globalSyms.find("__imp_" + fn);
                    if (it != layout.globalSyms.end())
                        peImportSlotRvas[fn] =
                            static_cast<uint32_t>(it->second.resolvedAddr - 0x140000000ULL);
                }
            }
            const bool emitStartupStub = opts.entrySymbol == "main";
            writeOk = writePeExe(
                opts.exePath, layout, opts.arch, peImports, peImportSlotRvas, emitStartupStub, err);
            break;
        }
    }

    if (!writeOk) {
        err << "error: failed to write executable '" << opts.exePath << "'\n";
        return 1;
    }

    return 0;
}

} // namespace viper::codegen::linker
