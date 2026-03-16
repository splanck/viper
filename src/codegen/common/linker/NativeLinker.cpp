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
#include "codegen/common/linker/SectionMerger.hpp"
#include "codegen/common/linker/StringDedup.hpp"
#include "codegen/common/linker/SymbolResolver.hpp"

#include <algorithm>

namespace viper::codegen::linker
{

int nativeLink(const NativeLinkerOptions &opts, std::ostream & /*out*/, std::ostream &err)
{
    // Step 1: Read the user's object file.
    ObjFile userObj;
    if (!readObjFile(opts.objPath, userObj, err))
    {
        err << "error: failed to read object file '" << opts.objPath << "'\n";
        return 1;
    }

    // Step 2: Read all archive files.
    std::vector<Archive> archives;
    for (const auto &arPath : opts.archivePaths)
    {
        Archive ar;
        if (!readArchive(arPath, ar, err))
        {
            err << "warning: failed to read archive '" << arPath << "', skipping\n";
            continue;
        }
        archives.push_back(std::move(ar));
    }

    // Step 3: Symbol resolution (iterative archive extraction).
    std::vector<ObjFile> initialObjects = {userObj};
    std::unordered_map<std::string, GlobalSymEntry> globalSyms;
    std::vector<ObjFile> allObjects;
    std::unordered_set<std::string> dynamicSyms;

    if (!resolveSymbols(initialObjects, archives, globalSyms, allObjects, dynamicSyms, err))
    {
        err << "error: symbol resolution failed\n";
        return 1;
    }
    // Step 3.5a: Generate ObjC selector stubs (macOS — objc_msgSend$selector symbols).
    // Must come before dynamic stubs since it moves symbols from dynamicSyms and
    // ensures objc_msgSend itself is in the dynamic set.
    if (opts.arch == LinkArch::AArch64 && opts.platform == LinkPlatform::macOS)
    {
        ObjFile objcStubs = generateObjcSelectorStubsAArch64(dynamicSyms);
        if (!objcStubs.sections.empty())
        {
            const size_t objcIdx = allObjects.size();
            allObjects.push_back(std::move(objcStubs));

            const auto &stubs = allObjects[objcIdx];
            for (size_t i = 1; i < stubs.symbols.size(); ++i)
            {
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
    if (!dynamicSyms.empty() && opts.arch == LinkArch::AArch64)
    {
        ObjFile stubObj = generateDynStubsAArch64(dynamicSyms);
        const size_t stubObjIdx = allObjects.size();
        allObjects.push_back(std::move(stubObj));

        // Manually register stub and GOT symbols in globalSyms.
        // This overrides Dynamic entries with Global entries pointing to stubs.
        const auto &stubs = allObjects[stubObjIdx];
        for (size_t i = 1; i < stubs.symbols.size(); ++i)
        {
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
        for (const auto &[name, entry] : globalSyms)
        {
            if (entry.binding == GlobalSymEntry::Dynamic)
                continue; // Dynamic symbols have no section reference.
            if (entry.objIndex < allObjects.size() &&
                entry.secIndex < allObjects[entry.objIndex].sections.size() &&
                allObjects[entry.objIndex].sections[entry.secIndex].data.empty())
            {
                deadSyms.push_back(name);
            }
        }
        for (const auto &name : deadSyms)
            globalSyms.erase(name);
    }

    // Step 4: Merge sections and compute layout.
    LinkLayout layout;
    layout.globalSyms = std::move(globalSyms);
    if (!mergeSections(allObjects, opts.platform, opts.arch, layout, err))
    {
        err << "error: section merging failed\n";
        return 1;
    }

    // Step 5: Resolve entry point.
    {
        auto it = findWithMachoFallback(layout.globalSyms, opts.entrySymbol);
        if (it != layout.globalSyms.end())
            layout.entryAddr = it->second.resolvedAddr;
    }

    // Step 5.5: Insert branch trampolines for out-of-range AArch64 B/BL instructions.
    if (!insertBranchTrampolines(allObjects, layout, opts.arch, opts.platform, err))
    {
        err << "error: branch trampoline insertion failed\n";
        return 1;
    }

    // Step 6: Apply relocations.
    if (!applyRelocations(allObjects, layout, dynamicSyms, opts.platform, opts.arch, err))
    {
        err << "error: relocation application failed\n";
        return 1;
    }

    // Step 6.5: Build GOT entry table for the executable writer (needed for bind opcodes).
    for (const auto &[name, entry] : layout.globalSyms)
    {
        if (name.size() > 6 && name.substr(0, 6) == "__got_")
        {
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
    switch (opts.platform)
    {
        case LinkPlatform::Linux:
            writeOk = writeElfExe(opts.exePath, layout, opts.arch, err);
            break;
        case LinkPlatform::macOS:
        {
            std::vector<DylibImport> dylibs;
            // Always link libSystem.B.dylib on macOS.
            dylibs.push_back({"/usr/lib/libSystem.B.dylib"});

            // Detect required frameworks from dynamic symbol prefixes.
            // Each entry: {list of symbol prefixes, dylib path}.
            // A framework is linked if ANY dynamic symbol starts with one of its prefixes.
            struct FrameworkRule
            {
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
            };

            // Helper: strip all leading underscores from a symbol name for
            // prefix matching. Needed because some C symbols retain underscores
            // after the MachOReader strips the Mach-O mangling prefix (e.g.,
            // __CFConstantStringClassReference, _objc_empty_cache).
            auto stripUnderscores = [](const std::string &s) -> std::string
            {
                size_t i = 0;
                while (i < s.size() && s[i] == '_')
                    ++i;
                return (i > 0) ? s.substr(i) : s;
            };

            for (const auto &rule : kFrameworkRules)
            {
                bool needed = false;
                for (const auto &sym : dynamicSyms)
                {
                    const std::string stripped = stripUnderscores(sym);
                    for (const char *const *p = rule.prefixes; *p; ++p)
                    {
                        if (sym.find(*p) == 0 || stripped.find(*p) == 0)
                        {
                            needed = true;
                            break;
                        }
                    }
                    if (!needed)
                    {
                        for (const char *const *e = rule.exactSyms; *e; ++e)
                        {
                            if (sym == *e)
                            {
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

                for (const auto &sym : dynamicSyms)
                {
                    // ObjC class/metaclass symbols use flat lookup — they live in
                    // the framework that defines the class, which can't be determined
                    // from the symbol prefix alone (e.g., OBJC_CLASS_$_NSWindow is
                    // in AppKit, not libobjc).
                    if (sym.find("OBJC_CLASS_$_") == 0 || sym.find("OBJC_METACLASS_$_") == 0)
                    {
                        symOrdinals[sym] = 0; // flat lookup
                        continue;
                    }

                    // Try prefix/exact matching against framework rules.
                    bool matched = false;
                    const std::string stripped = stripUnderscores(sym);
                    for (const auto &rule : kFrameworkRules)
                    {
                        // Try prefix match against both raw and underscore-stripped name.
                        for (const char *const *p = rule.prefixes; *p; ++p)
                        {
                            if (sym.find(*p) == 0 || stripped.find(*p) == 0)
                            {
                                auto pit = pathToOrdinal.find(rule.dylibPath);
                                if (pit != pathToOrdinal.end())
                                    symOrdinals[sym] = pit->second;
                                matched = true;
                                break;
                            }
                        }
                        if (!matched)
                        {
                            for (const char *const *e = rule.exactSyms; *e; ++e)
                            {
                                if (sym == *e)
                                {
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
        case LinkPlatform::Windows:
        {
            std::vector<DllImport> imports;
            imports.push_back({"kernel32.dll", {"ExitProcess"}});
            writeOk = writePeExe(opts.exePath, layout, opts.arch, imports, err);
            break;
        }
    }

    if (!writeOk)
    {
        err << "error: failed to write executable '" << opts.exePath << "'\n";
        return 1;
    }

    return 0;
}

} // namespace viper::codegen::linker
