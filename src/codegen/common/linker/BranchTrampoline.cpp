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
#include "codegen/common/linker/SectionMerger.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
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

constexpr size_t kTrampolineSize = 12;

bool branch26Reachable(uint64_t from, uint64_t to) {
    const int64_t disp = static_cast<int64_t>(to) - static_cast<int64_t>(from);
    const int64_t imm26 = disp >> 2;
    return imm26 <= 0x1FFFFFF && imm26 >= -0x2000000;
}

std::vector<size_t> collectChunkBoundaries(const OutputSection &textSec) {
    std::vector<size_t> boundaries;
    boundaries.push_back(0);
    for (const auto &chunk : textSec.chunks) {
        boundaries.push_back(chunk.outputOffset);
        boundaries.push_back(chunk.outputOffset + chunk.size);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    return boundaries;
}

bool chooseReachableBoundary(const std::vector<size_t> &boundaries,
                             uint64_t sourceOffset,
                             size_t &chosenBoundary) {
    bool found = false;
    uint64_t bestDistance = 0;
    for (size_t boundary : boundaries) {
        const uint64_t from = sourceOffset;
        const uint64_t to = boundary;
        if (!branch26Reachable(from, to))
            continue;
        const uint64_t dist = (from > to) ? (from - to) : (to - from);
        if (!found || dist < bestDistance) {
            found = true;
            bestDistance = dist;
            chosenBoundary = boundary;
        }
    }
    return found;
}

/// Record of an out-of-range branch that needs a trampoline.
struct OutOfRangeBranch {
    size_t objIdx;
    size_t secIdx;
    size_t relocIdx;
    std::string targetSymName;
    uint64_t targetAddr;
    size_t islandBoundary = 0;
    std::string trampolineSymName;
};

struct TrampolineEntry {
    std::string targetSymName;
    uint64_t targetAddr = 0;
    size_t islandBoundary = 0;
    size_t actualOffset = 0;
    std::string symbolName;
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
                oob.targetAddr = static_cast<uint64_t>(static_cast<int64_t>(S) + A);
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
    const std::vector<size_t> chunkBoundaries = collectChunkBoundaries(textSec);
    std::unordered_map<std::string, TrampolineEntry> trampolines;

    for (auto &oob : outOfRange) {
        size_t chosenBoundary = 0;
        const auto &rel = objects[oob.objIdx].sections[oob.secIdx].relocs[oob.relocIdx];
        auto mapIt = locMap.find(makeKey(oob.objIdx, static_cast<uint32_t>(oob.secIdx)));
        if (mapIt == locMap.end())
            continue;
        const uint64_t sourceOffset = mapIt->second.second + rel.offset;

        TrampolineEntry *chosenExisting = nullptr;
        uint64_t bestExistingDistance = 0;
        for (auto &[key, trampoline] : trampolines) {
            if (trampoline.targetSymName != oob.targetSymName ||
                !branch26Reachable(sourceOffset, trampoline.islandBoundary))
                continue;
            const uint64_t dist = (sourceOffset > trampoline.islandBoundary)
                                      ? (sourceOffset - trampoline.islandBoundary)
                                      : (trampoline.islandBoundary - sourceOffset);
            if (chosenExisting == nullptr || dist < bestExistingDistance) {
                chosenExisting = &trampoline;
                bestExistingDistance = dist;
            }
        }
        if (chosenExisting != nullptr) {
            oob.islandBoundary = chosenExisting->islandBoundary;
            oob.trampolineSymName = chosenExisting->symbolName;
            continue;
        }

        if (!chooseReachableBoundary(chunkBoundaries, sourceOffset, chosenBoundary)) {
            err << "error: no reachable trampoline island boundary for branch to '"
                << oob.targetSymName << "'\n";
            return false;
        }
        oob.islandBoundary = chosenBoundary;

        const std::string trampolineKey =
            oob.targetSymName + "@" + std::to_string(oob.islandBoundary);
        auto [it, inserted] = trampolines.emplace(trampolineKey, TrampolineEntry{});
        if (inserted) {
            it->second.targetSymName = oob.targetSymName;
            it->second.targetAddr = oob.targetAddr;
            it->second.islandBoundary = oob.islandBoundary;
            it->second.symbolName = "__viper_trampoline_" + std::to_string(trampolines.size() - 1);
        }
        oob.trampolineSymName = it->second.symbolName;
    }

    if (trampolines.empty())
        return true;

    std::map<size_t, std::vector<TrampolineEntry *>> islands;
    for (auto &[key, trampoline] : trampolines)
        islands[trampoline.islandBoundary].push_back(&trampoline);

    const std::vector<uint8_t> originalText = textSec.data;
    std::vector<uint8_t> newText;
    size_t totalGrowth = 0;
    for (const auto &[boundary, entries] : islands)
        totalGrowth += entries.size() * kTrampolineSize;
    newText.reserve(originalText.size() + totalGrowth);

    struct IslandPlacement {
        size_t boundary = 0;
        size_t size = 0;
    };

    std::vector<IslandPlacement> placements;
    placements.reserve(islands.size());

    size_t cursor = 0;
    for (const auto &[boundary, entries] : islands) {
        const size_t clampedBoundary = std::min(boundary, originalText.size());
        newText.insert(newText.end(),
                       originalText.begin() + static_cast<std::ptrdiff_t>(cursor),
                       originalText.begin() + static_cast<std::ptrdiff_t>(clampedBoundary));
        const size_t islandOffset = newText.size();
        for (size_t i = 0; i < entries.size(); ++i) {
            entries[i]->actualOffset = islandOffset + i * kTrampolineSize;
            newText.resize(newText.size() + kTrampolineSize, 0);
        }
        placements.push_back({boundary, entries.size() * kTrampolineSize});
        cursor = clampedBoundary;
    }
    newText.insert(newText.end(),
                   originalText.begin() + static_cast<std::ptrdiff_t>(cursor),
                   originalText.end());
    textSec.data = std::move(newText);

    for (auto &chunk : textSec.chunks) {
        size_t shift = 0;
        for (const auto &placement : placements) {
            if (chunk.outputOffset >= placement.boundary)
                shift += placement.size;
        }
        chunk.outputOffset += shift;
    }

    assignSectionVirtualAddresses(layout, platform);
    locMap = buildLocMap(layout);

    for (auto &[name, entry] : layout.globalSyms) {
        if (entry.binding == GlobalSymEntry::Undefined || entry.binding == GlobalSymEntry::Dynamic)
            continue;
        auto it = locMap.find(makeKey(entry.objIndex, entry.secIndex));
        if (it != locMap.end()) {
            entry.resolvedAddr =
                layout.sections[it->second.first].virtualAddr + it->second.second + entry.offset;
        }
    }

    for (auto &[key, trampoline] : trampolines) {
        const uint64_t tramVA = textSec.virtualAddr + trampoline.actualOffset;
        layout.globalSyms[trampoline.symbolName] =
            GlobalSymEntry{trampoline.symbolName, GlobalSymEntry::Global, 0, 0, 0, tramVA};

        uint8_t *tramp = textSec.data.data() + trampoline.actualOffset;
        writeLE32At(tramp + 0, 0x90000010); // ADRP x16
        writeLE32At(tramp + 4, 0x91000210); // ADD x16, x16
        writeLE32At(tramp + 8, 0xD61F0200); // BR x16

        uint32_t adrpInsn = readLE32At(tramp);
        uint64_t pageS = trampoline.targetAddr & ~0xFFFULL;
        uint64_t pageP = tramVA & ~0xFFFULL;
        int64_t pageDelta = static_cast<int64_t>(pageS) - static_cast<int64_t>(pageP);
        int64_t immHiLo = pageDelta >> 12;
        if (immHiLo > 0xFFFFF || immHiLo < -0x100000) {
            err << "error: trampoline ADRP out of range for '" << trampoline.targetSymName << "'\n";
            return false;
        }
        uint32_t immlo = static_cast<uint32_t>(immHiLo) & 0x3;
        uint32_t immhi = (static_cast<uint32_t>(immHiLo) >> 2) & 0x7FFFF;
        adrpInsn = (adrpInsn & 0x9F00001F) | (immlo << 29) | (immhi << 5);
        writeLE32At(tramp, adrpInsn);

        uint32_t addInsn = readLE32At(tramp + 4);
        uint32_t pageOff = static_cast<uint32_t>(trampoline.targetAddr) & 0xFFF;
        addInsn = (addInsn & 0xFFC003FF) | (pageOff << 10);
        writeLE32At(tramp + 4, addInsn);
    }

    // Patch original branches to target their trampolines.
    std::unordered_map<std::string, uint32_t> syntheticSymIndexByObjectAndName;
    for (auto &oob : outOfRange) {
        auto mapIt = locMap.find(makeKey(oob.objIdx, static_cast<uint32_t>(oob.secIdx)));
        if (mapIt == locMap.end())
            continue;

        const size_t outSecIdx = mapIt->second.first;
        const size_t chunkBase = mapIt->second.second;
        auto &outSec = layout.sections[outSecIdx];

        auto &rel = objects[oob.objIdx].sections[oob.secIdx].relocs[oob.relocIdx];
        const size_t patchOff = chunkBase + rel.offset;
        if (patchOff + 4 > outSec.data.size())
            continue;

        const auto trampIt =
            std::find_if(trampolines.begin(), trampolines.end(), [&](const auto &entry) {
                return entry.second.symbolName == oob.trampolineSymName;
            });
        if (trampIt == trampolines.end())
            continue;
        const size_t tramOff = trampIt->second.actualOffset;
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

        auto &obj = objects[oob.objIdx];
        const std::string symbolKey = std::to_string(oob.objIdx) + ":" + oob.trampolineSymName;
        auto symIt = syntheticSymIndexByObjectAndName.find(symbolKey);
        uint32_t trampolineSymIdx = 0;
        if (symIt != syntheticSymIndexByObjectAndName.end()) {
            trampolineSymIdx = symIt->second;
        } else {
            ObjSymbol trampolineSym;
            trampolineSym.name = oob.trampolineSymName;
            trampolineSym.binding = ObjSymbol::Undefined;
            obj.symbols.push_back(std::move(trampolineSym));
            trampolineSymIdx = static_cast<uint32_t>(obj.symbols.size() - 1);
            syntheticSymIndexByObjectAndName[symbolKey] = trampolineSymIdx;
        }
        rel.symIndex = trampolineSymIdx;
        rel.addend = 0;
    }

    return true;
}

} // namespace viper::codegen::linker
