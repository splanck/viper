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
#include "codegen/common/linker/ElfExeWriter.hpp"
#include "codegen/common/linker/MachOExeWriter.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/PeExeWriter.hpp"
#include "codegen/common/linker/RelocApplier.hpp"
#include "codegen/common/linker/SectionMerger.hpp"
#include "codegen/common/linker/SymbolResolver.hpp"

#include <algorithm>

namespace viper::codegen::linker
{

/// Generate a synthetic ObjFile containing ObjC selector stubs for
/// `objc_msgSend$selector` symbols. Each stub:
///   - Loads the selector reference pointer into x1 (arg 2 for objc_msgSend)
///   - Branches (tail-call) to objc_msgSend
/// The selector string and reference pointer are synthesized in the stub object.
/// objc_msgSend itself must also be a dynamic symbol (resolved by dyld).
static ObjFile generateObjcSelectorStubsAArch64(
    std::unordered_set<std::string> &dynamicSyms)
{
    ObjFile stubObj;
    stubObj.name = "<objc-stubs>";
    stubObj.format = ObjFileFormat::ELF; // ELF reloc format for the reloc applier
    stubObj.is64bit = true;
    stubObj.isLittleEndian = true;
    stubObj.machine = 183; // EM_AARCH64

    // Collect objc_msgSend$selector symbols and remove them from dynamicSyms
    // (they'll be resolved by our generated stubs, not by dyld).
    std::vector<std::string> selectorSyms;
    for (auto it = dynamicSyms.begin(); it != dynamicSyms.end();)
    {
        if (it->find("objc_msgSend$") == 0)
        {
            selectorSyms.push_back(*it);
            it = dynamicSyms.erase(it);
        }
        else
            ++it;
    }
    if (selectorSyms.empty())
        return stubObj;

    std::sort(selectorSyms.begin(), selectorSyms.end());

    // Ensure objc_msgSend itself is in the dynamic symbol set.
    dynamicSyms.insert("objc_msgSend");

    // Section 0: null.
    stubObj.sections.push_back(ObjSection{});

    // Section 1: .text (stub trampolines).
    ObjSection textSec;
    textSec.name = ".text";
    textSec.executable = true;
    textSec.writable = false;
    textSec.alloc = true;
    textSec.alignment = 4;

    // Section 2: .data (selector reference pointers + selector strings).
    // We put both sel refs and strings here for simplicity. The sel ref pointers
    // are 8 bytes each; strings follow after all pointers.
    ObjSection dataSec;
    dataSec.name = ".data";
    dataSec.executable = false;
    dataSec.writable = true;
    dataSec.alloc = true;
    dataSec.alignment = 8;

    // Section 3: .rodata (selector strings).
    ObjSection rodataSec;
    rodataSec.name = ".rodata";
    rodataSec.executable = false;
    rodataSec.writable = false;
    rodataSec.alloc = true;
    rodataSec.alignment = 1;

    // Symbol 0: null.
    stubObj.symbols.push_back(ObjSymbol{});

    auto emitLE32 = [](std::vector<uint8_t> &buf, uint32_t v)
    {
        buf.push_back(static_cast<uint8_t>(v));
        buf.push_back(static_cast<uint8_t>(v >> 8));
        buf.push_back(static_cast<uint8_t>(v >> 16));
        buf.push_back(static_cast<uint8_t>(v >> 24));
    };

    // First pass: build selector strings in rodata and sel ref pointers in data.
    // Layout: dataSec = [selref0(8), selref1(8), ...]
    //         rodataSec = [string0\0, string1\0, ...]
    std::vector<size_t> strOffsets; // offset of each string in rodataSec
    for (const auto &sym : selectorSyms)
    {
        // Extract selector from "objc_msgSend$selector".
        std::string selector = sym.substr(sym.find('$') + 1);

        // Record string offset and write the string.
        strOffsets.push_back(rodataSec.data.size());
        rodataSec.data.insert(rodataSec.data.end(), selector.begin(), selector.end());
        rodataSec.data.push_back(0); // NUL terminator

        // Selector reference pointer (8 bytes, will be relocated to point to string).
        // For now, write 0 — we'll add a relocation.
        for (int j = 0; j < 8; ++j)
            dataSec.data.push_back(0);
    }

    // Build symbols and relocations.
    // We need a symbol for each selector string (in rodata) and each selref (in data),
    // plus the stub entry points (in text).
    for (size_t i = 0; i < selectorSyms.size(); ++i)
    {
        const size_t stubOff = i * 12;    // 3 instructions per stub
        const size_t selrefOff = i * 8;   // 8 bytes per selref pointer

        // Symbol for the selector string in rodata (section 3).
        const uint32_t strSymIdx = static_cast<uint32_t>(stubObj.symbols.size());
        ObjSymbol strSym;
        strSym.name = "__objc_selstr_" + std::to_string(i);
        strSym.binding = ObjSymbol::Local;
        strSym.sectionIndex = 3; // rodata
        strSym.offset = strOffsets[i];
        stubObj.symbols.push_back(std::move(strSym));

        // Symbol for the selector reference pointer in data (section 2).
        const uint32_t selrefSymIdx = static_cast<uint32_t>(stubObj.symbols.size());
        ObjSymbol selrefSym;
        selrefSym.name = "__objc_selref_" + std::to_string(i);
        selrefSym.binding = ObjSymbol::Global;
        selrefSym.sectionIndex = 2; // data
        selrefSym.offset = selrefOff;
        stubObj.symbols.push_back(std::move(selrefSym));

        // Stub entry point symbol (section 1, replaces the dynamic symbol).
        ObjSymbol stubSym;
        stubSym.name = selectorSyms[i];
        stubSym.binding = ObjSymbol::Global;
        stubSym.sectionIndex = 1; // text
        stubSym.offset = stubOff;
        stubObj.symbols.push_back(std::move(stubSym));

        // Relocation: selref pointer → selector string (Abs64).
        ObjReloc strReloc;
        strReloc.offset = selrefOff;
        strReloc.type = 257; // R_AARCH64_ABS64
        strReloc.symIndex = strSymIdx;
        strReloc.addend = 0;
        dataSec.relocs.push_back(strReloc);

        // Emit stub instructions:
        //   adrp x1, selref@PAGE      → 0x90000001
        //   ldr  x1, [x1, selref@OFF] → 0xF9400021
        //   b    objc_msgSend          → 0x14000000
        emitLE32(textSec.data, 0x90000001); // adrp x1, #0
        emitLE32(textSec.data, 0xF9400021); // ldr x1, [x1, #0]
        emitLE32(textSec.data, 0x14000000); // b #0

        // Relocation: ADRP x1, selref@PAGE
        ObjReloc adrpReloc;
        adrpReloc.offset = stubOff;
        adrpReloc.type = 275; // R_AARCH64_ADR_PREL_PG_HI21
        adrpReloc.symIndex = selrefSymIdx;
        adrpReloc.addend = 0;
        textSec.relocs.push_back(adrpReloc);

        // Relocation: LDR x1, [x1, selref@PAGEOFF]
        ObjReloc ldrReloc;
        ldrReloc.offset = stubOff + 4;
        ldrReloc.type = 286; // R_AARCH64_LDST64_ABS_LO12_NC
        ldrReloc.symIndex = selrefSymIdx;
        ldrReloc.addend = 0;
        textSec.relocs.push_back(ldrReloc);

        // Relocation: B objc_msgSend (resolved later when objc_msgSend stub exists)
        // Use the objc_msgSend stub symbol name — it will be resolved during relocation.
        // We need a symbol reference to "objc_msgSend" for the B reloc.
        // Add it if not already present.
    }

    // Add a symbol for objc_msgSend (undefined, to be resolved by the reloc applier).
    const uint32_t msgSendSymIdx = static_cast<uint32_t>(stubObj.symbols.size());
    ObjSymbol msgSendSym;
    msgSendSym.name = "objc_msgSend";
    msgSendSym.binding = ObjSymbol::Undefined;
    msgSendSym.sectionIndex = 0;
    stubObj.symbols.push_back(std::move(msgSendSym));

    // Add B relocations for each stub → objc_msgSend.
    for (size_t i = 0; i < selectorSyms.size(); ++i)
    {
        ObjReloc bReloc;
        bReloc.offset = i * 12 + 8; // 3rd instruction in each stub
        bReloc.type = 282; // R_AARCH64_JUMP26
        bReloc.symIndex = msgSendSymIdx;
        bReloc.addend = 0;
        textSec.relocs.push_back(bReloc);
    }

    stubObj.sections.push_back(std::move(textSec));
    stubObj.sections.push_back(std::move(dataSec));
    stubObj.sections.push_back(std::move(rodataSec));
    return stubObj;
}

/// Generate a synthetic ObjFile containing stub trampolines and GOT entries
/// for dynamic symbols. Each dynamic symbol "foo" gets:
///   - A 12-byte stub in .text: adrp x16, GOT@page; ldr x16, [x16, off]; br x16
///   - An 8-byte GOT slot in .data (filled by dyld at load time)
///   - ELF-format relocations so the standard reloc applier patches the stubs
/// The stub symbol "foo" overrides the Dynamic entry in globalSyms, so all
/// call-site relocations naturally resolve to the stub address.
static ObjFile generateDynStubsAArch64(
    const std::unordered_set<std::string> &dynamicSyms)
{
    ObjFile stubObj;
    stubObj.name = "<dyld-stubs>";
    // Use ELF format so the reloc applier dispatches through elfA64Action(),
    // which correctly maps type 275 → Page21 and type 286 → LdSt64Off.
    stubObj.format = ObjFileFormat::ELF;
    stubObj.is64bit = true;
    stubObj.isLittleEndian = true;
    stubObj.machine = 183; // EM_AARCH64

    // Section 0: null (required by ObjFile convention).
    stubObj.sections.push_back(ObjSection{});

    // Section 1: .text (stub trampolines, executable).
    ObjSection textSec;
    textSec.name = ".text";
    textSec.executable = true;
    textSec.writable = false;
    textSec.alloc = true;
    textSec.alignment = 4;

    // Section 2: .got (GOT entries, writable).
    ObjSection gotSec;
    gotSec.name = ".data";
    gotSec.executable = false;
    gotSec.writable = true;
    gotSec.alloc = true;
    gotSec.alignment = 8;

    // Symbol 0: null (required).
    stubObj.symbols.push_back(ObjSymbol{});

    // Sort dynamic symbols for deterministic layout.
    std::vector<std::string> sorted(dynamicSyms.begin(), dynamicSyms.end());
    std::sort(sorted.begin(), sorted.end());

    auto emitLE32 = [](std::vector<uint8_t> &buf, uint32_t v)
    {
        buf.push_back(static_cast<uint8_t>(v));
        buf.push_back(static_cast<uint8_t>(v >> 8));
        buf.push_back(static_cast<uint8_t>(v >> 16));
        buf.push_back(static_cast<uint8_t>(v >> 24));
    };

    for (size_t i = 0; i < sorted.size(); ++i)
    {
        const size_t stubOff = i * 12;
        const size_t gotOff = i * 8;

        // GOT entry symbol (Global so it appears in globalSyms for reloc resolution).
        const uint32_t gotSymIdx = static_cast<uint32_t>(stubObj.symbols.size());
        ObjSymbol gotSym;
        gotSym.name = "__got_" + sorted[i];
        gotSym.binding = ObjSymbol::Global;
        gotSym.sectionIndex = 2; // .got section
        gotSym.offset = gotOff;
        stubObj.symbols.push_back(std::move(gotSym));

        // Stub entry point symbol (Global, overrides Dynamic entry for this symbol).
        ObjSymbol stubSym;
        stubSym.name = sorted[i];
        stubSym.binding = ObjSymbol::Global;
        stubSym.sectionIndex = 1; // .text section
        stubSym.offset = stubOff;
        stubObj.symbols.push_back(std::move(stubSym));

        // Emit stub instructions (placeholders — reloc applier patches ADRP/LDR).
        // adrp x16, #0  →  0x90000010
        emitLE32(textSec.data, 0x90000010);
        // ldr x16, [x16, #0]  →  0xF9400210
        emitLE32(textSec.data, 0xF9400210);
        // br x16  →  0xD61F0200
        emitLE32(textSec.data, 0xD61F0200);

        // Relocation: ADRP x16, GOT_entry@PAGE
        ObjReloc adrpReloc;
        adrpReloc.offset = stubOff;
        adrpReloc.type = 275; // R_AARCH64_ADR_PREL_PG_HI21
        adrpReloc.symIndex = gotSymIdx;
        adrpReloc.addend = 0;
        textSec.relocs.push_back(adrpReloc);

        // Relocation: LDR x16, [x16, GOT_entry@PAGEOFF] (64-bit scaled)
        ObjReloc ldrReloc;
        ldrReloc.offset = stubOff + 4;
        ldrReloc.type = 286; // R_AARCH64_LDST64_ABS_LO12_NC
        ldrReloc.symIndex = gotSymIdx;
        ldrReloc.addend = 0;
        textSec.relocs.push_back(ldrReloc);

        // GOT entry: 8 bytes of zeros (dyld fills at load time).
        for (int j = 0; j < 8; ++j)
            gotSec.data.push_back(0);
    }

    stubObj.sections.push_back(std::move(textSec));
    stubObj.sections.push_back(std::move(gotSec));
    return stubObj;
}

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
    std::sort(layout.gotEntries.begin(), layout.gotEntries.end(),
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
            if (sym.find("NS") == 0 && sym.find("NSS") != 0)  // NS* but not NSS* (NSURL etc.)
                needCocoa = true;
            if (sym.find("NSApp") == 0 || sym.find("NSWindow") == 0 || sym.find("NSView") == 0 ||
                sym.find("NSColor") == 0 || sym.find("NSEvent") == 0 || sym.find("NSCursor") == 0 ||
                sym.find("NSGraphicsContext") == 0 || sym.find("NSOpenGL") == 0 ||
                sym.find("NSApplication") == 0)
                needAppKit = true;
            if (sym.find("CG") == 0)
                needCoreGraphics = true;
            if (sym.find("NSLog") == 0 || sym.find("NSSearchPathForDirectories") == 0)
                needFoundation = true;
            if (sym.find("IOKit") == 0 || sym.find("IOHID") == 0 || sym.find("IOService") == 0 ||
                sym.find("IORegistryEntry") == 0)
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
            dylibs.push_back({"/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation"});
        if (needUTI)
            dylibs.push_back({"/System/Library/Frameworks/UniformTypeIdentifiers.framework/Versions/A/UniformTypeIdentifiers"});
        if (needAppKit)
            dylibs.push_back({"/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit"});
        if (needCoreGraphics)
            dylibs.push_back({"/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics"});
        if (needFoundation)
            dylibs.push_back({"/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation"});
        if (needObjC)
            dylibs.push_back({"/usr/lib/libobjc.A.dylib"});
        if (needAudioToolbox)
            dylibs.push_back({"/System/Library/Frameworks/AudioToolbox.framework/Versions/A/AudioToolbox"});

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
