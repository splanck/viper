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
//     merge sections → apply relocations → write executable
//   - For macOS: generates GOT entries and stub trampolines for dynamic symbols,
//     uses non-lazy binding (dyld fills GOT at load time)
//   - Falls back gracefully with clear error messages
// Links: codegen/common/linker/NativeLinker.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/NativeLinker.hpp"

#include "codegen/common/linker/ArchiveReader.hpp"
#include "codegen/common/linker/DeadStripPass.hpp"
#include "codegen/common/linker/DynStubGen.hpp"
#include "codegen/common/linker/ElfExeWriter.hpp"
#include "codegen/common/linker/MachOExeWriter.hpp"
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
        auto it = layout.globalSyms.find(opts.entrySymbol);
        if (it != layout.globalSyms.end())
            layout.entryAddr = it->second.resolvedAddr;
        else
        {
            // Try with underscore prefix (Mach-O convention stripped during reading).
            auto it2 = layout.globalSyms.find("_" + opts.entrySymbol);
            if (it2 != layout.globalSyms.end())
                layout.entryAddr = it2->second.resolvedAddr;
        }
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

            // Detect required frameworks from dynamic symbol names.
            // macOS frameworks are discovered from the dyld shared cache at load time;
            // the paths here are conventional and dyld intercepts them.
            bool needCoreFoundation = false;
            bool needCocoa = false;
            bool needAppKit = false;
            bool needCoreGraphics = false;
            bool needFoundation = false;
            bool needIOKit = false;
            bool needObjC = false;
            bool needUTI = false;
            bool needAudioToolbox = false;

            for (const auto &sym : dynamicSyms)
            {
                if (sym.find("CF") == 0 || sym.find("kCF") == 0)
                    needCoreFoundation = true;
                if (sym.find("NS") == 0 && sym.find("NSS") != 0) // NS* but not NSS* (NSURL etc.)
                    needCocoa = true;
                if (sym.find("NSApp") == 0 || sym.find("NSWindow") == 0 ||
                    sym.find("NSView") == 0 || sym.find("NSColor") == 0 ||
                    sym.find("NSEvent") == 0 || sym.find("NSCursor") == 0 ||
                    sym.find("NSGraphicsContext") == 0 || sym.find("NSOpenGL") == 0 ||
                    sym.find("NSApplication") == 0)
                    needAppKit = true;
                if (sym.find("CG") == 0)
                    needCoreGraphics = true;
                if (sym.find("NSLog") == 0 || sym.find("NSSearchPathForDirectories") == 0)
                    needFoundation = true;
                if (sym.find("IOKit") == 0 || sym.find("IOHID") == 0 ||
                    sym.find("IOService") == 0 || sym.find("IORegistryEntry") == 0)
                    needIOKit = true;
                if (sym.find("objc_") == 0 || sym.find("OBJC_") == 0 || sym == "sel_registerName" ||
                    sym == "sel_getName")
                    needObjC = true;
                if (sym.find("UTType") == 0 || sym.find("UTCopy") == 0)
                    needUTI = true;
                if (sym.find("AudioQueue") == 0 || sym.find("AudioServices") == 0)
                    needAudioToolbox = true;
            }

            if (needCocoa)
                dylibs.push_back({"/System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa"});
            if (needIOKit)
                dylibs.push_back({"/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit"});
            if (needCoreFoundation)
                dylibs.push_back({"/System/Library/Frameworks/CoreFoundation.framework/Versions/A/"
                                  "CoreFoundation"});
            if (needUTI)
                dylibs.push_back({"/System/Library/Frameworks/UniformTypeIdentifiers.framework/"
                                  "Versions/A/UniformTypeIdentifiers"});
            if (needAppKit)
                dylibs.push_back({"/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit"});
            if (needCoreGraphics)
                dylibs.push_back(
                    {"/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics"});
            if (needFoundation)
                dylibs.push_back(
                    {"/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation"});
            if (needObjC)
                dylibs.push_back({"/usr/lib/libobjc.A.dylib"});
            if (needAudioToolbox)
                dylibs.push_back(
                    {"/System/Library/Frameworks/AudioToolbox.framework/Versions/A/AudioToolbox"});

            writeOk = writeMachOExe(opts.exePath, layout, opts.arch, dylibs, dynamicSyms, err);
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
