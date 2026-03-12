//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/RelocApplier.cpp
// Purpose: Applies relocations to merged output section data.
// Key invariants:
//   - Dispatches by object file format (ELF/Mach-O/COFF) since reloc type
//     numbers collide across formats
//   - All addresses resolved before patching
//   - Range-checked for branch instructions
// Links: codegen/common/linker/RelocApplier.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/RelocApplier.hpp"

#include <cstring>

namespace viper::codegen::linker
{

static void writeLE32(uint8_t *p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

static void writeLE64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        p[i] = static_cast<uint8_t>(v >> (i * 8));
}

static uint32_t readLE32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/// Resolve a symbol name to its virtual address.
static bool resolveSymAddr(const std::string &symName,
                           const std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
                           uint64_t &addr)
{
    auto it = globalSyms.find(symName);
    if (it == globalSyms.end())
        return false;
    addr = it->second.resolvedAddr;
    return true;
}

/// Find the output section and offset for a given (objIndex, secIndex).
static bool findOutputLocation(const LinkLayout &layout, size_t objIdx, uint32_t secIdx,
                               size_t &outSecIdx, size_t &outOffset)
{
    for (size_t si = 0; si < layout.sections.size(); ++si)
    {
        for (const auto &chunk : layout.sections[si].chunks)
        {
            if (chunk.inputObjIndex == objIdx && chunk.inputSecIndex == secIdx)
            {
                outSecIdx = si;
                outOffset = chunk.outputOffset;
                return true;
            }
        }
    }
    return false;
}

/// Resolve a local symbol address from the object's symbol table.
/// For symbols not in globalSyms (e.g., static functions), compute their
/// address from their section and offset within the output layout.
static bool resolveLocalSymAddr(const ObjSymbol &sym, size_t objIdx,
                                 const LinkLayout &layout, uint64_t &addr)
{
    if (sym.sectionIndex == 0)
        return false;
    size_t outSecIdx = 0;
    size_t chunkOff = 0;
    if (!findOutputLocation(layout, objIdx, sym.sectionIndex, outSecIdx, chunkOff))
        return false;
    addr = layout.sections[outSecIdx].virtualAddr + chunkOff + sym.offset;
    return true;
}

/// Relocation categories (format-independent).
enum class RelocAction
{
    PCRel32,       // S + A - P (32-bit)
    Abs64,         // S + A (64-bit)
    Abs32,         // S + A (32-bit)
    Branch26,      // AArch64: ((S+A-P)>>2) & 0x3FFFFFF
    Page21,        // AArch64: ADRP page delta
    PageOff12,     // AArch64: ADD page offset
    LdSt64Off,    // AArch64: LDR/STR 64-bit scaled offset
    LdSt32Off,    // AArch64: LDR/STR 32-bit scaled offset
    LdSt128Off,   // AArch64: LDR/STR 128-bit scaled offset
    CondBr19,      // AArch64: B.cond 19-bit
    GotPage21,     // AArch64: GOT ADRP (relaxable to Page21 for local symbols)
    GotPageOff12,  // AArch64: GOT LDR pageoff (relaxable to ADD for local symbols)
    Unknown,
};

/// Map ELF x86_64 relocation type to action.
static RelocAction elfX64Action(uint32_t type)
{
    switch (type)
    {
    case 1:
        return RelocAction::Abs64; // R_X86_64_64
    case 2:
        return RelocAction::PCRel32; // R_X86_64_PC32
    case 4:
        return RelocAction::PCRel32; // R_X86_64_PLT32
    case 10:
        return RelocAction::Abs32; // R_X86_64_32
    default:
        return RelocAction::Unknown;
    }
}

/// Map ELF AArch64 relocation type to action.
static RelocAction elfA64Action(uint32_t type)
{
    switch (type)
    {
    case 257:
        return RelocAction::Abs64; // R_AARCH64_ABS64
    case 275:
        return RelocAction::Page21; // R_AARCH64_ADR_PREL_PG_HI21
    case 277:
        return RelocAction::PageOff12; // R_AARCH64_ADD_ABS_LO12_NC
    case 280:
        return RelocAction::CondBr19; // R_AARCH64_CONDBR19
    case 282:
        return RelocAction::Branch26; // R_AARCH64_JUMP26
    case 283:
        return RelocAction::Branch26; // R_AARCH64_CALL26
    case 285:
        return RelocAction::LdSt32Off; // R_AARCH64_LDST32_ABS_LO12_NC
    case 286:
        return RelocAction::LdSt64Off; // R_AARCH64_LDST64_ABS_LO12_NC
    case 299:
        return RelocAction::LdSt128Off; // R_AARCH64_LDST128_ABS_LO12_NC
    default:
        return RelocAction::Unknown;
    }
}

/// Map Mach-O x86_64 relocation type to action.
static RelocAction machoX64Action(uint32_t type)
{
    switch (type)
    {
    case 0:
        return RelocAction::Abs64; // X86_64_RELOC_UNSIGNED
    case 1:
        return RelocAction::PCRel32; // X86_64_RELOC_SIGNED
    case 2:
        return RelocAction::PCRel32; // X86_64_RELOC_BRANCH
    default:
        return RelocAction::Unknown;
    }
}

/// Map Mach-O ARM64 relocation type to action.
static RelocAction machoA64Action(uint32_t type)
{
    switch (type)
    {
    case 0:
        return RelocAction::Abs64; // ARM64_RELOC_UNSIGNED
    case 2:
        return RelocAction::Branch26; // ARM64_RELOC_BRANCH26
    case 3:
        return RelocAction::Page21; // ARM64_RELOC_PAGE21
    case 4:
        return RelocAction::PageOff12; // ARM64_RELOC_PAGEOFF12
    case 5:
        return RelocAction::GotPage21; // ARM64_RELOC_GOT_LOAD_PAGE21
    case 6:
        return RelocAction::GotPageOff12; // ARM64_RELOC_GOT_LOAD_PAGEOFF12
    case 8:
        return RelocAction::Page21; // ARM64_RELOC_TLVP_LOAD_PAGE21
    case 9:
        // ARM64_RELOC_TLVP_LOAD_PAGEOFF12 — for static linking, the TLV descriptor
        // address is known at link time. Rewrite LDR to ADD (GOT relaxation style)
        // so the code gets a pointer TO the descriptor, not a load FROM it.
        return RelocAction::GotPageOff12;
    default:
        return RelocAction::Unknown;
    }
}

/// Map COFF AMD64 relocation type to action.
static RelocAction coffX64Action(uint32_t type)
{
    switch (type)
    {
    case 1:
        return RelocAction::Abs64; // IMAGE_REL_AMD64_ADDR64
    case 2:
        return RelocAction::Abs32; // IMAGE_REL_AMD64_ADDR32
    case 4:
        return RelocAction::PCRel32; // IMAGE_REL_AMD64_REL32
    default:
        return RelocAction::Unknown;
    }
}

/// Map COFF ARM64 relocation type to action.
static RelocAction coffA64Action(uint32_t type)
{
    switch (type)
    {
    case 3:
        return RelocAction::Branch26; // IMAGE_REL_ARM64_BRANCH26
    case 4:
        return RelocAction::Page21; // IMAGE_REL_ARM64_PAGEBASE_REL21
    case 6:
        return RelocAction::PageOff12; // IMAGE_REL_ARM64_PAGEOFFSET_12A
    case 7:
        return RelocAction::LdSt64Off; // IMAGE_REL_ARM64_PAGEOFFSET_12L
    case 8:
        return RelocAction::CondBr19; // IMAGE_REL_ARM64_BRANCH19
    default:
        return RelocAction::Unknown;
    }
}

/// Dispatch relocation type to action based on format and architecture.
static RelocAction classifyReloc(ObjFileFormat format, LinkArch arch, uint32_t type)
{
    switch (format)
    {
    case ObjFileFormat::ELF:
        return (arch == LinkArch::X86_64) ? elfX64Action(type) : elfA64Action(type);
    case ObjFileFormat::MachO:
        return (arch == LinkArch::X86_64) ? machoX64Action(type) : machoA64Action(type);
    case ObjFileFormat::COFF:
        return (arch == LinkArch::X86_64) ? coffX64Action(type) : coffA64Action(type);
    default:
        return RelocAction::Unknown;
    }
}

bool applyRelocations(const std::vector<ObjFile> &objects, LinkLayout &layout,
                      const std::unordered_set<std::string> & /*dynamicSyms*/,
                      LinkPlatform /*platform*/, LinkArch arch, std::ostream &err)
{
    // First pass: resolve all symbol addresses.
    for (auto &[name, entry] : layout.globalSyms)
    {
        if (entry.binding == GlobalSymEntry::Undefined || entry.binding == GlobalSymEntry::Dynamic)
            continue;

        size_t outSecIdx = 0;
        size_t chunkOffset = 0;
        if (findOutputLocation(layout, entry.objIndex, entry.secIndex, outSecIdx, chunkOffset))
        {
            entry.resolvedAddr =
                layout.sections[outSecIdx].virtualAddr + chunkOffset + entry.offset;
        }
    }

    // Second pass: apply relocations.
    for (size_t oi = 0; oi < objects.size(); ++oi)
    {
        const auto &obj = objects[oi];
        for (size_t si = 1; si < obj.sections.size(); ++si)
        {
            const auto &sec = obj.sections[si];
            if (sec.relocs.empty())
                continue;

            size_t outSecIdx = 0;
            size_t chunkBase = 0;
            if (!findOutputLocation(layout, oi, static_cast<uint32_t>(si), outSecIdx, chunkBase))
                continue;

            auto &outSec = layout.sections[outSecIdx];
            const uint64_t secVA = outSec.virtualAddr;

            for (const auto &rel : sec.relocs)
            {
                const std::string &symName =
                    (rel.symIndex < obj.symbols.size()) ? obj.symbols[rel.symIndex].name : "";

                uint64_t S = 0;
                bool symResolved = false;
                if (!symName.empty())
                {
                    symResolved = resolveSymAddr(symName, layout.globalSyms, S);
                    // Fall back to local symbol resolution for static functions.
                    if (!symResolved && rel.symIndex < obj.symbols.size())
                        symResolved = resolveLocalSymAddr(obj.symbols[rel.symIndex], oi,
                                                          layout, S);
                }
                const int64_t A = rel.addend;
                const uint64_t P = secVA + chunkBase + rel.offset;
                const size_t patchOff = chunkBase + rel.offset;

                if (patchOff + 4 > outSec.data.size())
                    continue;

                uint8_t *patch = outSec.data.data() + patchOff;
                const RelocAction action = classifyReloc(obj.format, arch, rel.type);

                switch (action)
                {
                case RelocAction::PCRel32:
                {
                    int64_t val = static_cast<int64_t>(S) + A - static_cast<int64_t>(P);
                    writeLE32(patch, static_cast<uint32_t>(val));
                    break;
                }
                case RelocAction::Abs64:
                {
                    if (patchOff + 8 > outSec.data.size())
                        break;
                    uint64_t val = static_cast<uint64_t>(static_cast<int64_t>(S) + A);

                    // Mach-O TLV descriptor fixups (in __thread_vars/.tdata):
                    if (outSec.tls && outSec.name == ".tdata")
                    {
                        // Thunk field (_tlv_bootstrap): write 0 — dyld fills this via
                        // bind opcodes and sets up the TLS infrastructure. If we
                        // statically resolve it, dyld never discovers the TLV
                        // descriptors and _tlv_bootstrap (a fail-stub) aborts.
                        if (symName.find("tlv_bootstrap") != std::string::npos)
                        {
                            writeLE64(patch, 0);
                            break;
                        }

                        // Offset field ($tlv$init symbols): convert absolute VA to
                        // TLS-relative offset. _tlv_bootstrap expects offsets relative
                        // to the TLS template start, not absolute VAs.
                        for (const auto &ls : layout.sections)
                        {
                            if (!ls.tls || ls.name == ".tdata")
                                continue; // Skip the descriptor section itself.
                            if (val >= ls.virtualAddr &&
                                val < ls.virtualAddr + ls.data.size())
                            {
                                val -= ls.virtualAddr;
                                break;
                            }
                        }
                    }

                    writeLE64(patch, val);
                    break;
                }
                case RelocAction::Abs32:
                {
                    writeLE32(patch, static_cast<uint32_t>(S + A));
                    break;
                }
                case RelocAction::Branch26:
                {
                    uint32_t insn = readLE32(patch);
                    int64_t disp = static_cast<int64_t>(S) + A - static_cast<int64_t>(P);
                    int64_t imm26 = disp >> 2;
                    if (imm26 > 0x1FFFFFF || imm26 < -0x2000000)
                    {
                        err << "error: " << obj.name << ": branch out of range for '" << symName
                            << "'\n";
                        return false;
                    }
                    insn = (insn & 0xFC000000) | (static_cast<uint32_t>(imm26) & 0x03FFFFFF);
                    writeLE32(patch, insn);
                    break;
                }
                case RelocAction::Page21:
                {
                    uint32_t insn = readLE32(patch);
                    uint64_t pageS =
                        (static_cast<uint64_t>(static_cast<int64_t>(S) + A)) & ~0xFFFULL;
                    uint64_t pageP = P & ~0xFFFULL;
                    int64_t pageDelta =
                        static_cast<int64_t>(pageS) - static_cast<int64_t>(pageP);
                    int64_t immHiLo = pageDelta >> 12;
                    uint32_t immlo = static_cast<uint32_t>(immHiLo) & 0x3;
                    uint32_t immhi = (static_cast<uint32_t>(immHiLo) >> 2) & 0x7FFFF;
                    insn = (insn & 0x9F00001F) | (immlo << 29) | (immhi << 5);
                    writeLE32(patch, insn);
                    break;
                }
                case RelocAction::PageOff12:
                {
                    uint32_t insn = readLE32(patch);
                    uint32_t pageOff =
                        static_cast<uint32_t>(
                            static_cast<uint64_t>(static_cast<int64_t>(S) + A)) &
                        0xFFF;
                    insn = (insn & 0xFFC003FF) | (pageOff << 10);
                    writeLE32(patch, insn);
                    break;
                }
                case RelocAction::LdSt64Off:
                {
                    uint32_t insn = readLE32(patch);
                    uint32_t pageOff =
                        static_cast<uint32_t>(
                            static_cast<uint64_t>(static_cast<int64_t>(S) + A)) &
                        0xFFF;
                    pageOff >>= 3;
                    insn = (insn & 0xFFC003FF) | (pageOff << 10);
                    writeLE32(patch, insn);
                    break;
                }
                case RelocAction::LdSt32Off:
                {
                    uint32_t insn = readLE32(patch);
                    uint32_t pageOff =
                        static_cast<uint32_t>(
                            static_cast<uint64_t>(static_cast<int64_t>(S) + A)) &
                        0xFFF;
                    pageOff >>= 2;
                    insn = (insn & 0xFFC003FF) | (pageOff << 10);
                    writeLE32(patch, insn);
                    break;
                }
                case RelocAction::LdSt128Off:
                {
                    uint32_t insn = readLE32(patch);
                    uint32_t pageOff =
                        static_cast<uint32_t>(
                            static_cast<uint64_t>(static_cast<int64_t>(S) + A)) &
                        0xFFF;
                    pageOff >>= 4;
                    insn = (insn & 0xFFC003FF) | (pageOff << 10);
                    writeLE32(patch, insn);
                    break;
                }
                case RelocAction::CondBr19:
                {
                    uint32_t insn = readLE32(patch);
                    int64_t disp = static_cast<int64_t>(S) + A - static_cast<int64_t>(P);
                    int64_t imm19 = disp >> 2;
                    if (imm19 > 0x3FFFF || imm19 < -0x40000)
                    {
                        err << "error: " << obj.name
                            << ": conditional branch out of range for '" << symName << "'\n";
                        return false;
                    }
                    insn = (insn & 0xFF00001F) |
                           ((static_cast<uint32_t>(imm19) & 0x7FFFF) << 5);
                    writeLE32(patch, insn);
                    break;
                }
                case RelocAction::GotPage21:
                {
                    // GOT ADRP: if symbol is dynamic, point at its GOT entry;
                    // otherwise, GOT relaxation → point directly at symbol.
                    uint64_t target = S;
                    auto git = layout.globalSyms.find("__got_" + symName);
                    if (git != layout.globalSyms.end() && git->second.resolvedAddr != 0)
                        target = git->second.resolvedAddr; // Use GOT entry for dynamic sym.

                    uint32_t insn = readLE32(patch);
                    uint64_t pageT =
                        (static_cast<uint64_t>(static_cast<int64_t>(target) + A)) & ~0xFFFULL;
                    uint64_t pageP = P & ~0xFFFULL;
                    int64_t pageDelta =
                        static_cast<int64_t>(pageT) - static_cast<int64_t>(pageP);
                    int64_t immHiLo = pageDelta >> 12;
                    uint32_t immlo = static_cast<uint32_t>(immHiLo) & 0x3;
                    uint32_t immhi = (static_cast<uint32_t>(immHiLo) >> 2) & 0x7FFFF;
                    insn = (insn & 0x9F00001F) | (immlo << 29) | (immhi << 5);
                    writeLE32(patch, insn);
                    break;
                }
                case RelocAction::GotPageOff12:
                {
                    // GOT LDR pageoff: if symbol is dynamic, keep LDR with GOT entry offset;
                    // otherwise, GOT relaxation → rewrite LDR as ADD.
                    auto git = layout.globalSyms.find("__got_" + symName);
                    if (git != layout.globalSyms.end() && git->second.resolvedAddr != 0)
                    {
                        // Dynamic: LDR from GOT entry (8-byte scaled).
                        uint32_t insn = readLE32(patch);
                        uint32_t pageOff =
                            static_cast<uint32_t>(git->second.resolvedAddr + A) & 0xFFF;
                        pageOff >>= 3; // 8-byte scale for LDR X
                        insn = (insn & 0xFFC003FF) | (pageOff << 10);
                        writeLE32(patch, insn);
                    }
                    else
                    {
                        // Local: GOT relaxation — rewrite LDR to ADD.
                        uint32_t insn = readLE32(patch);
                        uint32_t rd = insn & 0x1F;
                        uint32_t rn = (insn >> 5) & 0x1F;
                        uint32_t pageOff =
                            static_cast<uint32_t>(
                                static_cast<uint64_t>(static_cast<int64_t>(S) + A)) &
                            0xFFF;
                        // ADD Xd, Xn, #imm12 = 0x91000000 | (imm12 << 10) | (Rn << 5) | Rd
                        insn = 0x91000000 | (pageOff << 10) | (rn << 5) | rd;
                        writeLE32(patch, insn);
                    }
                    break;
                }
                case RelocAction::Unknown:
                    err << "warning: " << obj.name << ": unknown reloc type " << rel.type
                        << " (format=" << static_cast<int>(obj.format) << ") for symbol '"
                        << symName << "'\n";
                    break;
                }
            }
        }
    }

    return true;
}

} // namespace viper::codegen::linker
