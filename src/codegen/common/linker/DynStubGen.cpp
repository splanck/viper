//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/DynStubGen.cpp
// Purpose: Generate synthetic AArch64 object files containing stub trampolines
//          for dynamic symbols and ObjC selector dispatch.
// Key invariants:
//   - Stub ObjFiles use ELF relocation format so the reloc applier dispatches
//     through elfA64Action() for type 275 (Page21) and type 286 (LdSt64Off).
//   - ObjC selector stubs transfer symbols from dynamicSyms to local stubs.
//   - Dynamic stubs generate GOT entries filled by dyld at load time.
// Ownership/Lifetime:
//   - Returned ObjFile values are owned by the caller.
// Links: codegen/common/linker/NativeLinker.cpp, codegen/common/linker/RelocApplier.cpp
//
//===----------------------------------------------------------------------===//

#include "DynStubGen.hpp"
#include "RelocConstants.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace viper::codegen::linker
{

namespace
{

void emitLE32(std::vector<uint8_t> &buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>(v));
    buf.push_back(static_cast<uint8_t>(v >> 8));
    buf.push_back(static_cast<uint8_t>(v >> 16));
    buf.push_back(static_cast<uint8_t>(v >> 24));
}

} // namespace

ObjFile generateObjcSelectorStubsAArch64(std::unordered_set<std::string> &dynamicSyms)
{
    ObjFile stubObj;
    stubObj.name = "<objc-stubs>";
    stubObj.format = ObjFileFormat::ELF;
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

    // Section 2: __objc_selrefs (selector reference pointers).
    ObjSection dataSec;
    dataSec.name = "__DATA,__objc_selrefs";
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

    // First pass: build selector strings in rodata and sel ref pointers in data.
    std::vector<size_t> strOffsets;
    for (const auto &sym : selectorSyms)
    {
        std::string selector = sym.substr(sym.find('$') + 1);

        strOffsets.push_back(rodataSec.data.size());
        rodataSec.data.insert(rodataSec.data.end(), selector.begin(), selector.end());
        rodataSec.data.push_back(0);

        for (int j = 0; j < 8; ++j)
            dataSec.data.push_back(0);
    }

    // Build symbols and relocations.
    for (size_t i = 0; i < selectorSyms.size(); ++i)
    {
        const size_t stubOff = i * 12;
        const size_t selrefOff = i * 8;

        // Symbol for the selector string in rodata (section 3).
        const uint32_t strSymIdx = static_cast<uint32_t>(stubObj.symbols.size());
        ObjSymbol strSym;
        strSym.name = "__objc_selstr_" + std::to_string(i);
        strSym.binding = ObjSymbol::Local;
        strSym.sectionIndex = 3;
        strSym.offset = strOffsets[i];
        stubObj.symbols.push_back(std::move(strSym));

        // Symbol for the selector reference pointer in data (section 2).
        const uint32_t selrefSymIdx = static_cast<uint32_t>(stubObj.symbols.size());
        ObjSymbol selrefSym;
        selrefSym.name = "__objc_selref_" + std::to_string(i);
        selrefSym.binding = ObjSymbol::Global;
        selrefSym.sectionIndex = 2;
        selrefSym.offset = selrefOff;
        stubObj.symbols.push_back(std::move(selrefSym));

        // Stub entry point symbol (section 1).
        ObjSymbol stubSym;
        stubSym.name = selectorSyms[i];
        stubSym.binding = ObjSymbol::Global;
        stubSym.sectionIndex = 1;
        stubSym.offset = stubOff;
        stubObj.symbols.push_back(std::move(stubSym));

        // Relocation: selref pointer -> selector string (Abs64).
        ObjReloc strReloc;
        strReloc.offset = selrefOff;
        strReloc.type = elf_a64::kAbs64;
        strReloc.symIndex = strSymIdx;
        strReloc.addend = 0;
        dataSec.relocs.push_back(strReloc);

        // Emit stub instructions:
        //   adrp x1, selref@PAGE      -> 0x90000001
        //   ldr  x1, [x1, selref@OFF] -> 0xF9400021
        //   b    objc_msgSend          -> 0x14000000
        emitLE32(textSec.data, 0x90000001);
        emitLE32(textSec.data, 0xF9400021);
        emitLE32(textSec.data, 0x14000000);

        // Relocation: ADRP x1, selref@PAGE
        ObjReloc adrpReloc;
        adrpReloc.offset = stubOff;
        adrpReloc.type = elf_a64::kAdrPrelPgHi21;
        adrpReloc.symIndex = selrefSymIdx;
        adrpReloc.addend = 0;
        textSec.relocs.push_back(adrpReloc);

        // Relocation: LDR x1, [x1, selref@PAGEOFF]
        ObjReloc ldrReloc;
        ldrReloc.offset = stubOff + 4;
        ldrReloc.type = elf_a64::kLdSt64Lo12Nc;
        ldrReloc.symIndex = selrefSymIdx;
        ldrReloc.addend = 0;
        textSec.relocs.push_back(ldrReloc);
    }

    // Add a symbol for objc_msgSend (undefined, resolved by the reloc applier).
    const uint32_t msgSendSymIdx = static_cast<uint32_t>(stubObj.symbols.size());
    ObjSymbol msgSendSym;
    msgSendSym.name = "objc_msgSend";
    msgSendSym.binding = ObjSymbol::Undefined;
    msgSendSym.sectionIndex = 0;
    stubObj.symbols.push_back(std::move(msgSendSym));

    // Add B relocations for each stub -> objc_msgSend.
    for (size_t i = 0; i < selectorSyms.size(); ++i)
    {
        ObjReloc bReloc;
        bReloc.offset = i * 12 + 8;
        bReloc.type = elf_a64::kJump26;
        bReloc.symIndex = msgSendSymIdx;
        bReloc.addend = 0;
        textSec.relocs.push_back(bReloc);
    }

    stubObj.sections.push_back(std::move(textSec));
    stubObj.sections.push_back(std::move(dataSec));
    stubObj.sections.push_back(std::move(rodataSec));
    return stubObj;
}

ObjFile generateDynStubsAArch64(const std::unordered_set<std::string> &dynamicSyms)
{
    ObjFile stubObj;
    stubObj.name = "<dyld-stubs>";
    stubObj.format = ObjFileFormat::ELF;
    stubObj.is64bit = true;
    stubObj.isLittleEndian = true;
    stubObj.machine = 183; // EM_AARCH64

    // Section 0: null.
    stubObj.sections.push_back(ObjSection{});

    // Section 1: .text (stub trampolines).
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

    // Symbol 0: null.
    stubObj.symbols.push_back(ObjSymbol{});

    // Sort dynamic symbols for deterministic layout.
    std::vector<std::string> sorted(dynamicSyms.begin(), dynamicSyms.end());
    std::sort(sorted.begin(), sorted.end());

    for (size_t i = 0; i < sorted.size(); ++i)
    {
        const size_t stubOff = i * 12;
        const size_t gotOff = i * 8;

        // GOT entry symbol.
        const uint32_t gotSymIdx = static_cast<uint32_t>(stubObj.symbols.size());
        ObjSymbol gotSym;
        gotSym.name = "__got_" + sorted[i];
        gotSym.binding = ObjSymbol::Global;
        gotSym.sectionIndex = 2;
        gotSym.offset = gotOff;
        stubObj.symbols.push_back(std::move(gotSym));

        // Stub entry point symbol (overrides Dynamic entry for this symbol).
        ObjSymbol stubSym;
        stubSym.name = sorted[i];
        stubSym.binding = ObjSymbol::Global;
        stubSym.sectionIndex = 1;
        stubSym.offset = stubOff;
        stubObj.symbols.push_back(std::move(stubSym));

        // Emit stub instructions.
        emitLE32(textSec.data, 0x90000010); // adrp x16, #0
        emitLE32(textSec.data, 0xF9400210); // ldr x16, [x16, #0]
        emitLE32(textSec.data, 0xD61F0200); // br x16

        // Relocation: ADRP x16, GOT_entry@PAGE
        ObjReloc adrpReloc;
        adrpReloc.offset = stubOff;
        adrpReloc.type = elf_a64::kAdrPrelPgHi21;
        adrpReloc.symIndex = gotSymIdx;
        adrpReloc.addend = 0;
        textSec.relocs.push_back(adrpReloc);

        // Relocation: LDR x16, [x16, GOT_entry@PAGEOFF]
        ObjReloc ldrReloc;
        ldrReloc.offset = stubOff + 4;
        ldrReloc.type = elf_a64::kLdSt64Lo12Nc;
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

} // namespace viper::codegen::linker
