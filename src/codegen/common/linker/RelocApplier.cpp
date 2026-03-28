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
#include "codegen/common/linker/RelocClassify.hpp"

#include <cstring>

namespace viper::codegen::linker {

static void writeLE32(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

static void writeLE64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        p[i] = static_cast<uint8_t>(v >> (i * 8));
}

static uint32_t readLE32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/// Encode (objIndex, secIndex) into a single 64-bit key for hash map lookup.
static uint64_t makeKey(size_t objIdx, size_t secIdx) {
    return (static_cast<uint64_t>(objIdx) << 32) | static_cast<uint64_t>(secIdx);
}

/// Pre-built reverse index: (objIdx, secIdx) → (outSecIdx, outputOffset).
/// Built once at the start of applyRelocations(), replaces the previous O(S×C)
/// linear scan with O(1) amortized lookup per relocation.
using LocationMap = std::unordered_map<uint64_t, std::pair<size_t, size_t>>;

/// Build the reverse-index map from the link layout.
static LocationMap buildLocationMap(const LinkLayout &layout) {
    LocationMap map;
    for (size_t si = 0; si < layout.sections.size(); ++si) {
        for (const auto &chunk : layout.sections[si].chunks)
            map[makeKey(chunk.inputObjIndex, chunk.inputSecIndex)] = {si, chunk.outputOffset};
    }
    return map;
}

/// Look up the output section and offset for a given (objIndex, secIndex).
static bool findOutputLocation(const LocationMap &locMap,
                               size_t objIdx,
                               uint32_t secIdx,
                               size_t &outSecIdx,
                               size_t &outOffset) {
    auto it = locMap.find(makeKey(objIdx, secIdx));
    if (it == locMap.end())
        return false;
    outSecIdx = it->second.first;
    outOffset = it->second.second;
    return true;
}

/// Resolve a symbol name to its virtual address.
static bool resolveSymAddr(const std::string &symName,
                           const std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
                           uint64_t &addr) {
    auto it = globalSyms.find(symName);
    if (it == globalSyms.end())
        return false;
    addr = it->second.resolvedAddr;
    return true;
}

/// Resolve a local symbol address from the object's symbol table.
/// For symbols not in globalSyms (e.g., static functions), compute their
/// address from their section and offset within the output layout.
static bool resolveLocalSymAddr(const ObjSymbol &sym,
                                size_t objIdx,
                                const LocationMap &locMap,
                                const LinkLayout &layout,
                                uint64_t &addr) {
    if (sym.sectionIndex == 0)
        return false;
    size_t outSecIdx = 0;
    size_t chunkOff = 0;
    if (!findOutputLocation(locMap, objIdx, sym.sectionIndex, outSecIdx, chunkOff))
        return false;
    const auto &outSec = layout.sections[outSecIdx];
    if (chunkOff + sym.offset > outSec.data.size())
        return false; // Symbol offset exceeds section bounds (malformed .o).
    addr = outSec.virtualAddr + chunkOff + sym.offset;
    return true;
}

// Relocation classification (RelocAction, classifyReloc) is in RelocClassify.hpp.

bool applyRelocations(const std::vector<ObjFile> &objects,
                      LinkLayout &layout,
                      const std::unordered_set<std::string> & /*dynamicSyms*/,
                      LinkPlatform /*platform*/,
                      LinkArch arch,
                      std::ostream &err) {
    // Build reverse-index map once: (objIdx, secIdx) → (outSecIdx, outputOffset).
    // This replaces the previous O(S×C) linear scan per lookup with O(1) amortized.
    const LocationMap locMap = buildLocationMap(layout);

    // First pass: resolve all symbol addresses.
    for (auto &[name, entry] : layout.globalSyms) {
        if (entry.binding == GlobalSymEntry::Undefined || entry.binding == GlobalSymEntry::Dynamic)
            continue;

        size_t outSecIdx = 0;
        size_t chunkOffset = 0;
        if (findOutputLocation(locMap, entry.objIndex, entry.secIndex, outSecIdx, chunkOffset)) {
            entry.resolvedAddr =
                layout.sections[outSecIdx].virtualAddr + chunkOffset + entry.offset;
        }
    }

    // Second pass: apply relocations.
    for (size_t oi = 0; oi < objects.size(); ++oi) {
        const auto &obj = objects[oi];
        for (size_t si = 1; si < obj.sections.size(); ++si) {
            const auto &sec = obj.sections[si];
            if (sec.relocs.empty())
                continue;

            size_t outSecIdx = 0;
            size_t chunkBase = 0;
            if (!findOutputLocation(locMap, oi, static_cast<uint32_t>(si), outSecIdx, chunkBase))
                continue;

            auto &outSec = layout.sections[outSecIdx];
            const uint64_t secVA = outSec.virtualAddr;

            for (const auto &rel : sec.relocs) {
                if (rel.offset >= sec.data.size()) {
                    err << "error: relocation offset " << rel.offset << " exceeds section size "
                        << sec.data.size() << " in '" << obj.name << "'\n";
                    return false;
                }

                const std::string &symName =
                    (rel.symIndex < obj.symbols.size()) ? obj.symbols[rel.symIndex].name : "";

                uint64_t S = 0;
                bool symResolved = false;
                if (!symName.empty()) {
                    symResolved = resolveSymAddr(symName, layout.globalSyms, S);
                    // Fall back to local symbol resolution for static functions.
                    if (!symResolved && rel.symIndex < obj.symbols.size())
                        symResolved =
                            resolveLocalSymAddr(obj.symbols[rel.symIndex], oi, locMap, layout, S);
                }
                if (!symResolved && !symName.empty()) {
                    err << "error: " << obj.name << ": undefined symbol '" << symName << "'\n";
                    return false;
                }

                const int64_t A = rel.addend;
                const uint64_t P = secVA + chunkBase + rel.offset;
                const size_t patchOff = chunkBase + rel.offset;

                if (patchOff + 4 > outSec.data.size()) {
                    err << "error: relocation at offset " << patchOff << " out of bounds in '"
                        << outSec.name << "' (size=" << outSec.data.size() << ")\n";
                    return false;
                }

                uint8_t *patch = outSec.data.data() + patchOff;
                const RelocAction action = classifyReloc(obj.format, arch, rel.type);

                switch (action) {
                    case RelocAction::PCRel32: {
                        int64_t val = static_cast<int64_t>(S) + A - static_cast<int64_t>(P);
                        writeLE32(patch, static_cast<uint32_t>(val));
                        break;
                    }
                    case RelocAction::Abs64: {
                        if (patchOff + 8 > outSec.data.size()) {
                            err << "error: 64-bit relocation at offset " << patchOff
                                << " out of bounds in '" << outSec.name
                                << "' (size=" << outSec.data.size() << ")\n";
                            return false;
                        }
                        uint64_t val = static_cast<uint64_t>(static_cast<int64_t>(S) + A);

                        // Dynamic symbol data references: if the symbol was stubbed
                        // (has a __got_ entry), it's a dynamic symbol. For data pointers
                        // (Abs64), write 0 and record a bind entry — dyld fills the
                        // actual value at load time. Stubs are only for code branches.
                        if (!symName.empty() && outSec.writable &&
                            layout.globalSyms.count("__got_" + symName)) {
                            writeLE64(patch, 0);
                            layout.bindEntries.push_back({symName, outSecIdx, patchOff});
                            break;
                        }

                        // Mach-O TLV descriptor fixups (in __thread_vars/.tdata):
                        if (outSec.tls && outSec.name == ".tdata") {
                            // Thunk field (_tlv_bootstrap): write 0 — dyld fills this via
                            // bind opcodes and sets up the TLS infrastructure. If we
                            // statically resolve it, dyld never discovers the TLV
                            // descriptors and _tlv_bootstrap (a fail-stub) aborts.
                            if (symName.find("tlv_bootstrap") != std::string::npos) {
                                writeLE64(patch, 0);
                                break;
                            }

                            // Offset field ($tlv$init symbols): convert absolute VA to
                            // TLS-relative offset. _tlv_bootstrap expects offsets relative
                            // to the TLS template start, not absolute VAs.
                            bool tlvMatch = false;
                            for (const auto &ls : layout.sections) {
                                if (!ls.tls || ls.name == ".tdata")
                                    continue; // Skip the descriptor section itself.
                                if (val >= ls.virtualAddr &&
                                    val < ls.virtualAddr + ls.data.size()) {
                                    val -= ls.virtualAddr;
                                    tlvMatch = true;
                                    break;
                                }
                            }
                            if (!tlvMatch && val != 0) {
                                err << "warning: TLV offset for '" << symName
                                    << "' could not be converted to TLS-relative\n";
                            }
                        }

                        writeLE64(patch, val);

                        // Record rebase entry for ASLR fixup (Mach-O MH_PIE).
                        // Abs64 pointers in writable sections need dyld to adjust
                        // by the ASLR slide at load time. TLS sections use separate
                        // mechanisms (bind opcodes for thunks, relative offsets).
                        if (outSec.writable && !outSec.tls && val != 0)
                            layout.rebaseEntries.push_back({outSecIdx, patchOff});

                        break;
                    }
                    case RelocAction::Abs32: {
                        writeLE32(patch, static_cast<uint32_t>(S + A));
                        break;
                    }
                    case RelocAction::Branch26: {
                        uint32_t insn = readLE32(patch);
                        int64_t disp = static_cast<int64_t>(S) + A - static_cast<int64_t>(P);
                        int64_t imm26 = disp >> 2;
                        if (imm26 > 0x1FFFFFF || imm26 < -0x2000000) {
                            err << "error: " << obj.name << ": branch out of range for '" << symName
                                << "'\n";
                            return false;
                        }
                        insn = (insn & 0xFC000000) | (static_cast<uint32_t>(imm26) & 0x03FFFFFF);
                        writeLE32(patch, insn);
                        break;
                    }
                    case RelocAction::Page21: {
                        uint32_t insn = readLE32(patch);
                        uint64_t pageS =
                            (static_cast<uint64_t>(static_cast<int64_t>(S) + A)) & ~0xFFFULL;
                        uint64_t pageP = P & ~0xFFFULL;
                        int64_t pageDelta =
                            static_cast<int64_t>(pageS) - static_cast<int64_t>(pageP);
                        int64_t immHiLo = pageDelta >> 12;
                        if (immHiLo > 0xFFFFF || immHiLo < -0x100000) {
                            err << "error: " << obj.name << ": ADRP page offset out of range for '"
                                << symName << "'\n";
                            return false;
                        }
                        uint32_t immlo = static_cast<uint32_t>(immHiLo) & 0x3;
                        uint32_t immhi = (static_cast<uint32_t>(immHiLo) >> 2) & 0x7FFFF;
                        insn = (insn & 0x9F00001F) | (immlo << 29) | (immhi << 5);
                        writeLE32(patch, insn);
                        break;
                    }
                    case RelocAction::PageOff12: {
                        uint32_t insn = readLE32(patch);
                        uint32_t pageOff = static_cast<uint32_t>(
                                               static_cast<uint64_t>(static_cast<int64_t>(S) + A)) &
                                           0xFFF;

                        // Mach-O ARM64_RELOC_PAGEOFF12 is used for both ADD (unscaled)
                        // and LDR/STR (scaled by access size). The linker must inspect the
                        // instruction to determine the correct scale factor.
                        //
                        // LDR/STR unsigned offset encoding: bits [31:30] = size,
                        // bits [29:24] = 11100x. Test: (insn & 0x3B000000) == 0x39000000.
                        // Scale = 1 << size, except 128-bit SIMD where scale = 16.
                        if ((insn & 0x3B000000) == 0x39000000) {
                            uint32_t shift = insn >> 30; // 0=1B, 1=2B, 2=4B, 3=8B
                            // 128-bit SIMD: V=1 (bit 26) and opc[1]=1 (bit 23) → shift=4.
                            if ((insn & 0x04800000) == 0x04800000)
                                shift = 4;
                            pageOff >>= shift;
                        }

                        insn = (insn & 0xFFC003FF) | (pageOff << 10);
                        writeLE32(patch, insn);
                        break;
                    }
                    case RelocAction::LdSt64Off: {
                        uint32_t insn = readLE32(patch);
                        uint32_t pageOff = static_cast<uint32_t>(
                                               static_cast<uint64_t>(static_cast<int64_t>(S) + A)) &
                                           0xFFF;
                        pageOff >>= 3;
                        insn = (insn & 0xFFC003FF) | (pageOff << 10);
                        writeLE32(patch, insn);
                        break;
                    }
                    case RelocAction::LdSt32Off: {
                        uint32_t insn = readLE32(patch);
                        uint32_t pageOff = static_cast<uint32_t>(
                                               static_cast<uint64_t>(static_cast<int64_t>(S) + A)) &
                                           0xFFF;
                        pageOff >>= 2;
                        insn = (insn & 0xFFC003FF) | (pageOff << 10);
                        writeLE32(patch, insn);
                        break;
                    }
                    case RelocAction::LdSt128Off: {
                        uint32_t insn = readLE32(patch);
                        uint32_t pageOff = static_cast<uint32_t>(
                                               static_cast<uint64_t>(static_cast<int64_t>(S) + A)) &
                                           0xFFF;
                        pageOff >>= 4;
                        insn = (insn & 0xFFC003FF) | (pageOff << 10);
                        writeLE32(patch, insn);
                        break;
                    }
                    case RelocAction::CondBr19: {
                        uint32_t insn = readLE32(patch);
                        int64_t disp = static_cast<int64_t>(S) + A - static_cast<int64_t>(P);
                        int64_t imm19 = disp >> 2;
                        if (imm19 > 0x3FFFF || imm19 < -0x40000) {
                            err << "error: " << obj.name
                                << ": conditional branch out of range for '" << symName << "'\n";
                            return false;
                        }
                        insn =
                            (insn & 0xFF00001F) | ((static_cast<uint32_t>(imm19) & 0x7FFFF) << 5);
                        writeLE32(patch, insn);
                        break;
                    }
                    case RelocAction::GotPage21: {
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
                        if (immHiLo > 0xFFFFF || immHiLo < -0x100000) {
                            err << "error: " << obj.name
                                << ": GOT ADRP page offset out of range for '" << symName << "'\n";
                            return false;
                        }
                        uint32_t immlo = static_cast<uint32_t>(immHiLo) & 0x3;
                        uint32_t immhi = (static_cast<uint32_t>(immHiLo) >> 2) & 0x7FFFF;
                        insn = (insn & 0x9F00001F) | (immlo << 29) | (immhi << 5);
                        writeLE32(patch, insn);
                        break;
                    }
                    case RelocAction::GotPageOff12: {
                        // GOT LDR pageoff: if symbol is dynamic, keep LDR with GOT entry offset;
                        // otherwise, GOT relaxation → rewrite LDR as ADD.
                        auto git = layout.globalSyms.find("__got_" + symName);
                        if (git != layout.globalSyms.end() && git->second.resolvedAddr != 0) {
                            // Dynamic: LDR from GOT entry (8-byte scaled).
                            uint32_t insn = readLE32(patch);
                            uint32_t pageOff =
                                static_cast<uint32_t>(git->second.resolvedAddr + A) & 0xFFF;
                            pageOff >>= 3; // 8-byte scale for LDR X
                            insn = (insn & 0xFFC003FF) | (pageOff << 10);
                            writeLE32(patch, insn);
                        } else {
                            // Local: GOT relaxation — rewrite LDR to ADD.
                            uint32_t insn = readLE32(patch);
                            uint32_t rd = insn & 0x1F;
                            uint32_t rn = (insn >> 5) & 0x1F;
                            uint32_t pageOff = static_cast<uint32_t>(static_cast<uint64_t>(
                                                   static_cast<int64_t>(S) + A)) &
                                               0xFFF;
                            // ADD Xd, Xn, #imm12 = 0x91000000 | (imm12 << 10) | (Rn << 5) | Rd
                            insn = 0x91000000 | (pageOff << 10) | (rn << 5) | rd;
                            writeLE32(patch, insn);
                        }
                        break;
                    }
                    case RelocAction::Unknown:
                        err << "error: " << obj.name << ": unknown reloc type " << rel.type
                            << " (format=" << static_cast<int>(obj.format) << ") for symbol '"
                            << symName << "'\n";
                        return false;
                }
            }
        }
    }

    return true;
}

} // namespace viper::codegen::linker
