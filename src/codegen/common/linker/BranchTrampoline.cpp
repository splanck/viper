//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/BranchTrampoline.cpp
// Purpose: Implements AArch64 branch trampoline insertion.
//          After section merging, scans all Branch26 relocations for out-of-range
//          displacements. For each, appends an ADRP+ADD+BR x16 trampoline to
//          the .text section, applies trampoline relocs inline, and patches
//          the original branch to target the trampoline.
// Key invariants:
//   - Trampoline dedup: one trampoline per unique out-of-range target symbol
//   - Inline reloc application: ADRP+ADD computed directly (no RelocApplier pass)
//   - VA re-assignment after .text extension
// Links: codegen/common/linker/BranchTrampoline.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/BranchTrampoline.hpp"

#include "codegen/common/linker/AlignUtil.hpp"
#include "codegen/common/linker/RelocClassify.hpp"

#include <cstdint>
#include <unordered_map>

namespace viper::codegen::linker {

namespace {

/// Write a 32-bit LE value to a byte pointer.
void writeLE32At(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

/// Read a 32-bit LE value from a byte pointer.
uint32_t readLE32At(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/// Encode (objIndex, secIndex) into a single 64-bit key.
uint64_t makeKey(size_t objIdx, size_t secIdx) {
    return (static_cast<uint64_t>(objIdx) << 32) | static_cast<uint64_t>(secIdx);
}

/// Build LocationMap: (objIdx, secIdx) → (outSecIdx, chunkOffset).
std::unordered_map<uint64_t, std::pair<size_t, size_t>> buildLocMap(const LinkLayout &layout) {
    std::unordered_map<uint64_t, std::pair<size_t, size_t>> map;
    for (size_t si = 0; si < layout.sections.size(); ++si) {
        for (const auto &chunk : layout.sections[si].chunks)
            map[makeKey(chunk.inputObjIndex, chunk.inputSecIndex)] = {si, chunk.outputOffset};
    }
    return map;
}

/// Permission class for VA re-assignment (must match SectionMerger.cpp).
int permClass(const OutputSection &s) {
    if (s.executable)
        return 0;
    if (s.tls)
        return 3;
    if (s.writable)
        return 2;
    return 1;
}

/// Re-assign VAs for all sections, preserving ordering.
/// Must match the algorithm in SectionMerger.cpp (lines 365-379).
void reassignVAs(LinkLayout &layout, LinkPlatform platform, LinkArch arch) {
    uint64_t baseAddr;
    switch (platform) {
        case LinkPlatform::macOS:
            baseAddr = 0x100000000ULL;
            break;
        case LinkPlatform::Windows:
            baseAddr = 0x140000000ULL;
            break;
        case LinkPlatform::Linux:
        default:
            baseAddr = 0x400000ULL;
            break;
    }

    uint64_t currentAddr = baseAddr + layout.pageSize;

    int prevCls = -1;
    for (auto &sec : layout.sections) {
        int cls = permClass(sec);
        if (cls != prevCls) {
            currentAddr = alignUp(currentAddr, layout.pageSize);
            prevCls = cls;
        }
        currentAddr = alignUp(currentAddr, sec.alignment);
        sec.virtualAddr = currentAddr;
        currentAddr += sec.data.size();
    }
}

/// Record of an out-of-range branch that needs a trampoline.
struct OutOfRangeBranch {
    size_t objIdx;
    size_t secIdx;
    size_t relocIdx;
    std::string targetSymName;
    uint64_t targetAddr;
};

} // namespace

bool insertBranchTrampolines(std::vector<ObjFile> &objects,
                             LinkLayout &layout,
                             LinkArch arch,
                             LinkPlatform platform,
                             std::ostream &err) {
    // Only AArch64 needs trampolines.
    if (arch != LinkArch::AArch64)
        return true;

    // Build location map.
    auto locMap = buildLocMap(layout);

    // First: resolve all global symbol addresses (same as RelocApplier first pass).
    for (auto &[name, entry] : layout.globalSyms) {
        if (entry.binding == GlobalSymEntry::Undefined || entry.binding == GlobalSymEntry::Dynamic)
            continue;
        auto it = locMap.find(makeKey(entry.objIndex, entry.secIndex));
        if (it != locMap.end()) {
            entry.resolvedAddr =
                layout.sections[it->second.first].virtualAddr + it->second.second + entry.offset;
        }
    }

    // Scan for out-of-range Branch26 relocations.
    std::vector<OutOfRangeBranch> outOfRange;

    for (size_t oi = 0; oi < objects.size(); ++oi) {
        const auto &obj = objects[oi];
        for (size_t si = 1; si < obj.sections.size(); ++si) {
            const auto &sec = obj.sections[si];
            if (sec.relocs.empty() || sec.data.empty())
                continue;

            auto mapIt = locMap.find(makeKey(oi, static_cast<uint32_t>(si)));
            if (mapIt == locMap.end())
                continue;

            const size_t outSecIdx = mapIt->second.first;
            const size_t chunkBase = mapIt->second.second;
            const uint64_t secVA = layout.sections[outSecIdx].virtualAddr;

            for (size_t ri = 0; ri < sec.relocs.size(); ++ri) {
                const auto &rel = sec.relocs[ri];
                RelocAction action = classifyReloc(obj.format, arch, rel.type);
                if (action != RelocAction::Branch26)
                    continue;

                // Resolve target symbol address.
                const std::string &symName =
                    (rel.symIndex < obj.symbols.size()) ? obj.symbols[rel.symIndex].name : "";
                auto symIt = layout.globalSyms.find(symName);
                if (symIt == layout.globalSyms.end())
                    continue;

                const uint64_t S = symIt->second.resolvedAddr;
                const int64_t A = rel.addend;
                const uint64_t P = secVA + chunkBase + rel.offset;
                const int64_t disp = static_cast<int64_t>(S) + A - static_cast<int64_t>(P);
                const int64_t imm26 = disp >> 2;

                if (imm26 <= 0x1FFFFFF && imm26 >= -0x2000000)
                    continue; // In range — no trampoline needed.

                OutOfRangeBranch oob;
                oob.objIdx = oi;
                oob.secIdx = si;
                oob.relocIdx = ri;
                oob.targetSymName = symName;
                oob.targetAddr = S;
                outOfRange.push_back(std::move(oob));
            }
        }
    }

    if (outOfRange.empty())
        return true; // Nothing to do.

    // Find the .text output section.
    size_t textSecIdx = SIZE_MAX;
    for (size_t i = 0; i < layout.sections.size(); ++i) {
        if (layout.sections[i].executable) {
            textSecIdx = i;
            break;
        }
    }
    if (textSecIdx == SIZE_MAX) {
        err << "error: no executable section found for trampoline insertion\n";
        return false;
    }

    auto &textSec = layout.sections[textSecIdx];
    const size_t origTextSize = textSec.data.size();

    // Deduplicate: one trampoline per unique target symbol.
    // Map: targetSymName → offset within .text where trampoline starts.
    std::unordered_map<std::string, size_t> trampolineOffsets;

    for (const auto &oob : outOfRange) {
        if (trampolineOffsets.count(oob.targetSymName))
            continue;

        // Align trampoline to 4 bytes (AArch64 instruction alignment).
        size_t aligned = alignUp(textSec.data.size(), 4);
        textSec.data.resize(aligned, 0);

        const size_t trampolineOff = textSec.data.size();
        trampolineOffsets[oob.targetSymName] = trampolineOff;

        // Emit 12-byte trampoline: ADRP x16, #0; ADD x16, x16, #0; BR x16
        // ADRP x16 = 0x90000010
        // ADD x16, x16, #0 = 0x91000210
        // BR x16 = 0xD61F0200
        textSec.data.resize(textSec.data.size() + 12, 0);
        uint8_t *tramp = textSec.data.data() + trampolineOff;
        writeLE32At(tramp + 0, 0x90000010); // ADRP x16
        writeLE32At(tramp + 4, 0x91000210); // ADD x16, x16
        writeLE32At(tramp + 8, 0xD61F0200); // BR x16
    }

    // Re-assign VAs since .text grew.
    reassignVAs(layout, platform, arch);

    // Rebuild location map (VAs changed).
    locMap = buildLocMap(layout);

    // Re-resolve global symbol addresses with new VAs.
    for (auto &[name, entry] : layout.globalSyms) {
        if (entry.binding == GlobalSymEntry::Undefined || entry.binding == GlobalSymEntry::Dynamic)
            continue;
        auto it = locMap.find(makeKey(entry.objIndex, entry.secIndex));
        if (it != locMap.end()) {
            entry.resolvedAddr =
                layout.sections[it->second.first].virtualAddr + it->second.second + entry.offset;
        }
    }

    // Apply trampoline relocations inline (ADRP + ADD).
    for (const auto &[symName, tramOff] : trampolineOffsets) {
        auto symIt = layout.globalSyms.find(symName);
        if (symIt == layout.globalSyms.end())
            continue;

        const uint64_t targetAddr = symIt->second.resolvedAddr;
        const uint64_t tramVA = textSec.virtualAddr + tramOff;

        // Apply Page21 reloc to ADRP (offset 0 within trampoline).
        {
            uint8_t *adrpPtr = textSec.data.data() + tramOff;
            uint32_t insn = readLE32At(adrpPtr);
            uint64_t pageS = targetAddr & ~0xFFFULL;
            uint64_t pageP = tramVA & ~0xFFFULL;
            int64_t pageDelta = static_cast<int64_t>(pageS) - static_cast<int64_t>(pageP);
            int64_t immHiLo = pageDelta >> 12;
            if (immHiLo > 0xFFFFF || immHiLo < -0x100000) {
                err << "error: trampoline ADRP out of range for '" << symName << "'\n";
                return false;
            }
            uint32_t immlo = static_cast<uint32_t>(immHiLo) & 0x3;
            uint32_t immhi = (static_cast<uint32_t>(immHiLo) >> 2) & 0x7FFFF;
            insn = (insn & 0x9F00001F) | (immlo << 29) | (immhi << 5);
            writeLE32At(adrpPtr, insn);
        }

        // Apply PageOff12 reloc to ADD (offset 4 within trampoline).
        // ADD is unscaled (no shift), so just extract the 12-bit page offset.
        {
            uint8_t *addPtr = textSec.data.data() + tramOff + 4;
            uint32_t insn = readLE32At(addPtr);
            uint32_t pageOff = static_cast<uint32_t>(targetAddr) & 0xFFF;
            insn = (insn & 0xFFC003FF) | (pageOff << 10);
            writeLE32At(addPtr, insn);
        }
    }

    // Patch original branches to target their trampolines.
    for (const auto &oob : outOfRange) {
        auto mapIt = locMap.find(makeKey(oob.objIdx, static_cast<uint32_t>(oob.secIdx)));
        if (mapIt == locMap.end())
            continue;

        const size_t outSecIdx = mapIt->second.first;
        const size_t chunkBase = mapIt->second.second;
        auto &outSec = layout.sections[outSecIdx];

        const auto &rel = objects[oob.objIdx].sections[oob.secIdx].relocs[oob.relocIdx];
        const size_t patchOff = chunkBase + rel.offset;
        if (patchOff + 4 > outSec.data.size())
            continue;

        const size_t tramOff = trampolineOffsets[oob.targetSymName];
        const uint64_t tramVA = textSec.virtualAddr + tramOff;
        const uint64_t P = outSec.virtualAddr + patchOff;
        const int64_t disp = static_cast<int64_t>(tramVA) - static_cast<int64_t>(P);
        const int64_t imm26 = disp >> 2;

        if (imm26 > 0x1FFFFFF || imm26 < -0x2000000) {
            err << "error: trampoline at offset " << tramOff
                << " is unreachable from branch at VA 0x" << std::hex << P << std::dec
                << " (.text exceeds 128MB; interleaved trampoline islands not yet supported)\n";
            return false;
        }

        // Patch the branch instruction to target the trampoline.
        uint8_t *patch = outSec.data.data() + patchOff;
        uint32_t insn = readLE32At(patch);
        insn = (insn & 0xFC000000) | (static_cast<uint32_t>(imm26) & 0x03FFFFFF);
        writeLE32At(patch, insn);
    }

    return true;
}

} // namespace viper::codegen::linker
