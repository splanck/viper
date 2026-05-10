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
#include "codegen/common/linker/RelocConstants.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace viper::codegen::linker {

static void writeLE32(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

static void writeLE16(uint8_t *p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

static void writeLE64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        p[i] = static_cast<uint8_t>(v >> (i * 8));
}

static uint32_t readLE32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static bool writeCheckedRel32(uint8_t *patch,
                              int64_t value,
                              const ObjFile &obj,
                              const std::string &symName,
                              const char *kind,
                              std::ostream &err) {
    if (value < std::numeric_limits<int32_t>::min() ||
        value > std::numeric_limits<int32_t>::max()) {
        err << "error: " << obj.name << ": " << kind << " relocation out of range";
        if (!symName.empty())
            err << " for '" << symName << "'";
        err << "\n";
        return false;
    }
    writeLE32(patch, static_cast<uint32_t>(static_cast<int32_t>(value)));
    return true;
}

static bool checkAArch64BranchAlignment(int64_t disp,
                                        const ObjFile &obj,
                                        const std::string &symName,
                                        const char *kind,
                                        std::ostream &err) {
    if ((disp & 0x3) == 0)
        return true;
    err << "error: " << obj.name << ": " << kind << " target is not instruction aligned";
    if (!symName.empty())
        err << " for '" << symName << "'";
    err << "\n";
    return false;
}

static bool checkPageOffsetAlignment(uint32_t pageOff,
                                     uint32_t shift,
                                     const ObjFile &obj,
                                     const std::string &symName,
                                     std::ostream &err) {
    const uint32_t scale = 1u << shift;
    if ((pageOff & (scale - 1u)) == 0)
        return true;
    err << "error: " << obj.name << ": AArch64 page offset for '" << symName
        << "' is not aligned to " << scale << " bytes\n";
    return false;
}

static bool isAArch64UnsignedLdStOffset(uint32_t insn) {
    return (insn & 0x3B000000u) == 0x39000000u;
}

static bool isAArch64LdrXUnsignedOffset(uint32_t insn) {
    return (insn & 0xFFC00000u) == 0xF9400000u;
}

static uint32_t aarch64LdStOffsetShift(uint32_t insn) {
    uint32_t shift = insn >> 30;
    if ((insn & 0x04800000u) == 0x04800000u)
        shift = 4;
    return shift;
}

static bool checkedAddU64(uint64_t lhs, uint64_t rhs, uint64_t &out) {
    if (lhs > std::numeric_limits<uint64_t>::max() - rhs)
        return false;
    out = lhs + rhs;
    return true;
}

static uint64_t int64Magnitude(int64_t value) {
    if (value >= 0)
        return static_cast<uint64_t>(value);
    return static_cast<uint64_t>(-(value + 1)) + 1;
}

static bool checkedAddSignedU64(uint64_t base, int64_t addend, uint64_t &out) {
    if (addend >= 0)
        return checkedAddU64(base, static_cast<uint64_t>(addend), out);

    const uint64_t mag = int64Magnitude(addend);
    if (base < mag)
        return false;
    out = base - mag;
    return true;
}

static bool checkedAddressDelta(uint64_t lhs, uint64_t rhs, int64_t &out) {
    if (lhs >= rhs) {
        const uint64_t delta = lhs - rhs;
        if (delta > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
            return false;
        out = static_cast<int64_t>(delta);
        return true;
    }

    const uint64_t delta = rhs - lhs;
    const uint64_t minMag =
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1ULL;
    if (delta > minMag)
        return false;
    out = (delta == minMag) ? std::numeric_limits<int64_t>::min()
                            : -static_cast<int64_t>(delta);
    return true;
}

static bool checkedRelocTarget(uint64_t S,
                               int64_t A,
                               const ObjFile &obj,
                               const std::string &symName,
                               const char *kind,
                               std::ostream &err,
                               uint64_t &target) {
    if (checkedAddSignedU64(S, A, target))
        return true;
    err << "error: " << obj.name << ": " << kind << " relocation address overflow";
    if (!symName.empty())
        err << " for '" << symName << "'";
    err << "\n";
    return false;
}

static bool checkedRelocDelta(uint64_t target,
                              uint64_t P,
                              const ObjFile &obj,
                              const std::string &symName,
                              const char *kind,
                              std::ostream &err,
                              int64_t &delta) {
    if (checkedAddressDelta(target, P, delta))
        return true;
    err << "error: " << obj.name << ": " << kind << " relocation delta out of range";
    if (!symName.empty())
        err << " for '" << symName << "'";
    err << "\n";
    return false;
}

static bool checkedU32Value(int64_t value,
                            const ObjFile &obj,
                            const std::string &symName,
                            const char *kind,
                            std::ostream &err,
                            uint32_t &out) {
    if (value >= 0 && value <= std::numeric_limits<uint32_t>::max()) {
        out = static_cast<uint32_t>(value);
        return true;
    }
    err << "error: " << obj.name << ": " << kind << " relocation out of 32-bit range";
    if (!symName.empty())
        err << " for '" << symName << "'";
    err << "\n";
    return false;
}

static bool sortWindowsPdata(LinkLayout &layout, LinkArch arch, std::ostream &err) {
    const size_t recordSize = (arch == LinkArch::AArch64) ? 8 : 12;

    for (auto &sec : layout.sections) {
        if (sec.name != ".pdata" || sec.data.size() < recordSize)
            continue;
        if ((sec.data.size() % recordSize) != 0) {
            err << "error: .pdata section size " << sec.data.size()
                << " is not a multiple of unwind record size " << recordSize << "\n";
            return false;
        }

        const size_t recordCount = sec.data.size() / recordSize;
        std::vector<std::vector<uint8_t>> records(recordCount, std::vector<uint8_t>(recordSize));
        for (size_t i = 0; i < recordCount; ++i)
            std::memcpy(records[i].data(), sec.data.data() + i * recordSize, recordSize);

        std::stable_sort(records.begin(), records.end(), [](const auto &a, const auto &b) {
            return readLE32(a.data()) < readLE32(b.data());
        });

        for (size_t i = 0; i < recordCount; ++i)
            std::memcpy(sec.data.data() + i * recordSize, records[i].data(), recordSize);
    }
    return true;
}

/// Encode (objIndex, secIndex) into a single 64-bit key for hash map lookup.
static uint64_t makeKey(size_t objIdx, size_t secIdx) {
    return (static_cast<uint64_t>(objIdx) << 32) | static_cast<uint64_t>(secIdx);
}

/// Pre-built reverse index: (objIdx, secIdx) → (outSecIdx, outputOffset).
/// Built once at the start of applyRelocations(), replaces the previous O(S×C)
/// linear scan with O(1) amortized lookup per relocation.
struct OutputLocation {
    size_t outSecIdx = 0;
    size_t outputOffset = 0;
    size_t inputSize = 0;
};

using LocationMap = std::unordered_map<uint64_t, OutputLocation>;

/// Build the reverse-index map from the link layout.
static LocationMap buildLocationMap(const LinkLayout &layout) {
    LocationMap map;
    for (size_t si = 0; si < layout.sections.size(); ++si) {
        for (const auto &chunk : layout.sections[si].chunks)
            map[makeKey(chunk.inputObjIndex, chunk.inputSecIndex)] =
                OutputLocation{si, chunk.outputOffset, chunk.size};
    }
    return map;
}

/// Look up the output section and offset for a given (objIndex, secIndex).
static bool findOutputLocation(const LocationMap &locMap,
                               size_t objIdx,
                               uint32_t secIdx,
                               size_t &outSecIdx,
                               size_t &outOffset,
                               size_t *inputSize = nullptr) {
    auto it = locMap.find(makeKey(objIdx, secIdx));
    if (it == locMap.end())
        return false;
    outSecIdx = it->second.outSecIdx;
    outOffset = it->second.outputOffset;
    if (inputSize)
        *inputSize = it->second.inputSize;
    return true;
}

/// Resolve a local symbol address from the object's symbol table.
/// For symbols not in globalSyms (e.g., static functions), compute their
/// address from their section and offset within the output layout.
static bool resolveLocalSymAddr(const ObjSymbol &sym,
                                size_t objIdx,
                                const LocationMap &locMap,
                                const LinkLayout &layout,
                                uint64_t &addr,
                                size_t *resolvedOutSecIdx = nullptr) {
    if (sym.absolute) {
        addr = static_cast<uint64_t>(sym.offset);
        return true;
    }
    if (sym.sectionIndex == 0)
        return false;
    size_t outSecIdx = 0;
    size_t chunkOff = 0;
    size_t inputSize = 0;
    if (!findOutputLocation(locMap, objIdx, sym.sectionIndex, outSecIdx, chunkOff, &inputSize))
        return false;
    const auto &outSec = layout.sections[outSecIdx];
    if (chunkOff > outSec.data.size() || sym.offset > inputSize)
        return false; // Symbol offset exceeds section bounds (malformed .o).
    uint64_t withChunk = 0;
    if (!checkedAddU64(outSec.virtualAddr, static_cast<uint64_t>(chunkOff), withChunk) ||
        !checkedAddU64(withChunk, static_cast<uint64_t>(sym.offset), addr))
        return false;
    if (resolvedOutSecIdx)
        *resolvedOutSecIdx = outSecIdx;
    return true;
}

static bool resolveGlobalSymLocation(const std::string &symName,
                                     const std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
                                     const LocationMap &locMap,
                                     const LinkLayout &layout,
                                     uint64_t &addr,
                                     size_t *resolvedOutSecIdx = nullptr) {
    auto it = globalSyms.find(symName);
    if (it == globalSyms.end())
        return false;

    const auto &entry = it->second;
    if (entry.absolute) {
        addr = static_cast<uint64_t>(entry.offset);
        return true;
    }

    if (entry.secIndex > 0) {
        size_t outSecIdx = 0;
        size_t chunkOff = 0;
        size_t inputSize = 0;
        if (findOutputLocation(locMap, entry.objIndex, entry.secIndex, outSecIdx, chunkOff, &inputSize)) {
            const auto &outSec = layout.sections[outSecIdx];
            if (chunkOff > outSec.data.size() || entry.offset > inputSize)
                return false;
            uint64_t withChunk = 0;
            if (!checkedAddU64(outSec.virtualAddr, static_cast<uint64_t>(chunkOff), withChunk) ||
                !checkedAddU64(withChunk, static_cast<uint64_t>(entry.offset), addr))
                return false;
            if (resolvedOutSecIdx)
                *resolvedOutSecIdx = outSecIdx;
            return true;
        }
    }

    if (entry.resolvedAddr == 0 &&
        (entry.binding == GlobalSymEntry::Undefined || entry.binding == GlobalSymEntry::Dynamic))
        return false;
    addr = entry.resolvedAddr;
    return true;
}

// Relocation classification (RelocAction, classifyReloc) is in RelocClassify.hpp.

bool applyRelocations(const std::vector<ObjFile> &objects,
                      LinkLayout &layout,
                      const std::unordered_set<std::string> &dynamicSyms,
                      LinkPlatform platform,
                      LinkArch arch,
                      std::ostream &err) {
    // Build reverse-index map once: (objIdx, secIdx) → (outSecIdx, outputOffset).
    // This replaces the previous O(S×C) linear scan per lookup with O(1) amortized.
    const LocationMap locMap = buildLocationMap(layout);

    // First pass: resolve all symbol addresses.
    for (auto &[name, entry] : layout.globalSyms) {
        if (entry.binding == GlobalSymEntry::Undefined || entry.binding == GlobalSymEntry::Dynamic)
            continue;
        if (entry.absolute) {
            entry.resolvedAddr = static_cast<uint64_t>(entry.offset);
            continue;
        }

        size_t outSecIdx = 0;
        size_t chunkOffset = 0;
        size_t inputSize = 0;
        if (findOutputLocation(
                locMap, entry.objIndex, entry.secIndex, outSecIdx, chunkOffset, &inputSize)) {
            const auto &outSec = layout.sections[outSecIdx];
            if (chunkOffset <= outSec.data.size() && entry.offset <= inputSize) {
                uint64_t withChunk = 0;
                if (checkedAddU64(outSec.virtualAddr, static_cast<uint64_t>(chunkOffset), withChunk))
                    checkedAddU64(withChunk, static_cast<uint64_t>(entry.offset), entry.resolvedAddr);
            }
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
            if (!findOutputLocation(locMap, oi, static_cast<uint32_t>(si), outSecIdx, chunkBase)) {
                if (sec.stripped || !sec.alloc)
                    continue;
                err << "error: " << obj.name << ": live section '" << sec.name
                    << "' has relocations but was not placed in the output layout\n";
                return false;
            }

            auto &outSec = layout.sections[outSecIdx];
            const uint64_t secVA = outSec.virtualAddr;

            for (const auto &rel : sec.relocs) {
                if (rel.offset >= sec.data.size()) {
                    err << "error: relocation offset " << rel.offset << " exceeds section size "
                        << sec.data.size() << " in '" << obj.name << "'\n";
                    return false;
                }

                if (rel.symIndex >= obj.symbols.size()) {
                    err << "error: " << obj.name << ": relocation references invalid symbol index "
                        << rel.symIndex << "\n";
                    return false;
                }

                const ObjSymbol &targetSym = obj.symbols[rel.symIndex];
                const std::string &symName = targetSym.name;
                const std::string targetDisplay =
                    symName.empty() ? std::string("<anonymous section symbol>") : symName;

                uint64_t S = 0;
                bool symResolved = false;
                size_t symOutSecIdx = SIZE_MAX;
                bool hasSymOutputSection = false;
                if (targetSym.sectionIndex > 0 || targetSym.absolute)
                    symResolved =
                        resolveLocalSymAddr(targetSym, oi, locMap, layout, S, &symOutSecIdx);
                if (symResolved && symOutSecIdx != SIZE_MAX)
                    hasSymOutputSection = true;
                if (!symResolved && !symName.empty()) {
                    symResolved =
                        resolveGlobalSymLocation(symName, layout.globalSyms, locMap, layout, S, &symOutSecIdx);
                    if (symResolved && symOutSecIdx != SIZE_MAX)
                        hasSymOutputSection = true;
                }
                if (!symResolved && !symName.empty() && targetSym.weakExternal) {
                    const std::string &fallback = targetSym.weakDefaultName;
                    if (!fallback.empty()) {
                        symResolved = resolveGlobalSymLocation(
                            fallback, layout.globalSyms, locMap, layout, S, &symOutSecIdx);
                        if (symResolved && symOutSecIdx != SIZE_MAX)
                            hasSymOutputSection = true;
                    }
                    if (!symResolved) {
                        S = 0;
                        symResolved = true;
                    }
                }
                if (!symResolved && !symName.empty()) {
                    if (platform == LinkPlatform::Windows && symName == "__ImageBase") {
                        S = 0x140000000ULL;
                        symResolved = true;
                    } else if (platform == LinkPlatform::Windows && symName == "vm_trap") {
                        symResolved = resolveGlobalSymLocation("vm_trap_default",
                                                               layout.globalSyms,
                                                               locMap,
                                                               layout,
                                                               S,
                                                               &symOutSecIdx) ||
                                      resolveGlobalSymLocation("rt_abort",
                                                               layout.globalSyms,
                                                               locMap,
                                                               layout,
                                                               S,
                                                               &symOutSecIdx);
                        if (symResolved && symOutSecIdx != SIZE_MAX)
                            hasSymOutputSection = true;
                    }
                }
                if (!symResolved) {
                    err << "error: " << obj.name << ": undefined symbol '" << targetDisplay << "'\n";
                    return false;
                }

                const int64_t A = rel.addend;
                uint64_t P = 0;
                uint64_t secChunkVA = 0;
                if (!checkedAddU64(secVA, static_cast<uint64_t>(chunkBase), secChunkVA) ||
                    !checkedAddU64(secChunkVA, static_cast<uint64_t>(rel.offset), P)) {
                    err << "error: " << obj.name << ": relocation place address overflow in '"
                        << outSec.name << "'\n";
                    return false;
                }
                if (rel.offset > std::numeric_limits<size_t>::max() - chunkBase) {
                    err << "error: " << obj.name << ": relocation file offset overflow in '"
                        << outSec.name << "'\n";
                    return false;
                }
                const size_t patchOff = chunkBase + rel.offset;

                if (patchOff > outSec.data.size()) {
                    err << "error: relocation at offset " << patchOff << " out of bounds in '"
                        << outSec.name << "' (size=" << outSec.data.size() << ")\n";
                    return false;
                }

                uint8_t *patch = outSec.data.data() + patchOff;
                auto requirePatchBytes = [&](size_t width, const char *kind) -> bool {
                    if (width <= outSec.data.size() - patchOff)
                        return true;
                    err << "error: " << kind << " relocation at offset " << patchOff
                        << " out of bounds in '" << outSec.name << "' (size="
                        << outSec.data.size() << ")\n";
                    return false;
                };

                auto requireTargetOutputSection = [&](const char *kind) -> bool {
                    if (hasSymOutputSection)
                        return true;
                    err << "error: " << obj.name << ": " << kind << " target '" << targetDisplay
                        << "' has no output section\n";
                    return false;
                };

                if (obj.format == ObjFileFormat::COFF && arch == LinkArch::X86_64) {
                    if (rel.type == coff_x64::kSecRel) {
                        if (!requirePatchBytes(4, "SECREL"))
                            return false;
                        if (!requireTargetOutputSection("SECREL"))
                            return false;
                        uint64_t target = 0;
                        int64_t secRel = 0;
                        uint32_t val = 0;
                        if (!checkedRelocTarget(S, A, obj, symName, "SECREL", err, target) ||
                            !checkedRelocDelta(target,
                                               layout.sections[symOutSecIdx].virtualAddr,
                                               obj,
                                               symName,
                                               "SECREL",
                                               err,
                                               secRel) ||
                            !checkedU32Value(secRel, obj, symName, "SECREL", err, val))
                            return false;
                        writeLE32(patch, val);
                        continue;
                    }
                    if (rel.type == coff_x64::kSection) {
                        if (!requirePatchBytes(2, "SECTION"))
                            return false;
                        if (!requireTargetOutputSection("SECTION"))
                            return false;
                        if (symOutSecIdx + 1 > std::numeric_limits<uint16_t>::max()) {
                            err << "error: " << obj.name << ": SECTION relocation target index "
                                << (symOutSecIdx + 1) << " exceeds 16-bit COFF limit\n";
                            return false;
                        }
                        writeLE16(patch, static_cast<uint16_t>(symOutSecIdx + 1));
                        continue;
                    }
                    if (rel.type == coff_x64::kAddr32Nb) {
                        if (!requirePatchBytes(4, "ADDR32NB"))
                            return false;
                        const uint64_t imageBase = 0x140000000ULL;
                        uint64_t target = 0;
                        int64_t rva = 0;
                        uint32_t val = 0;
                        if (!checkedRelocTarget(S, A, obj, symName, "ADDR32NB", err, target) ||
                            !checkedRelocDelta(target, imageBase, obj, symName, "ADDR32NB", err, rva) ||
                            !checkedU32Value(rva, obj, symName, "ADDR32NB", err, val))
                            return false;
                        writeLE32(patch, val);
                        continue;
                    }
                    if (rel.type == coff_x64::kRel32 ||
                        (rel.type >= coff_x64::kRel32_1 && rel.type <= coff_x64::kRel32_5)) {
                        if (!requirePatchBytes(4, "COFF REL32"))
                            return false;
                        // COFF AMD64 REL32 relocations are relative to the byte
                        // following the relocated field, not the field address.
                        // REL32_n variants apply the same base bias plus an
                        // additional n-byte adjustment for instructions whose
                        // displacement isn't immediately followed by the next
                        // instruction boundary.
                        const int64_t extraBias =
                            (rel.type == coff_x64::kRel32)
                                ? 0
                                : static_cast<int64_t>(rel.type - coff_x64::kRel32);
                        uint64_t target = 0;
                        int64_t delta = 0;
                        if (!checkedRelocTarget(S, A, obj, symName, "COFF REL32", err, target) ||
                            !checkedRelocDelta(target, P, obj, symName, "COFF REL32", err, delta))
                            return false;
                        const int64_t val = delta - 4 - extraBias;
                        if (!writeCheckedRel32(patch, val, obj, symName, "COFF REL32", err))
                            return false;
                        continue;
                    }
                }

                if (obj.format == ObjFileFormat::COFF && arch == LinkArch::AArch64) {
                    if (rel.type == coff_a64::kSecRel) {
                        if (!requirePatchBytes(4, "SECREL"))
                            return false;
                        if (!requireTargetOutputSection("SECREL"))
                            return false;
                        uint64_t target = 0;
                        int64_t secRel = 0;
                        uint32_t val = 0;
                        if (!checkedRelocTarget(S, A, obj, symName, "SECREL", err, target) ||
                            !checkedRelocDelta(target,
                                               layout.sections[symOutSecIdx].virtualAddr,
                                               obj,
                                               symName,
                                               "SECREL",
                                               err,
                                               secRel) ||
                            !checkedU32Value(secRel, obj, symName, "SECREL", err, val))
                            return false;
                        writeLE32(patch, val);
                        continue;
                    }
                    if (rel.type == coff_a64::kSection) {
                        if (!requirePatchBytes(2, "SECTION"))
                            return false;
                        if (!requireTargetOutputSection("SECTION"))
                            return false;
                        if (symOutSecIdx + 1 > std::numeric_limits<uint16_t>::max()) {
                            err << "error: " << obj.name << ": SECTION relocation target index "
                                << (symOutSecIdx + 1) << " exceeds 16-bit COFF limit\n";
                            return false;
                        }
                        writeLE16(patch, static_cast<uint16_t>(symOutSecIdx + 1));
                        continue;
                    }
                    if (rel.type == coff_a64::kAddr32Nb) {
                        if (!requirePatchBytes(4, "ADDR32NB"))
                            return false;
                        const uint64_t imageBase = 0x140000000ULL;
                        uint64_t target = 0;
                        int64_t rva = 0;
                        uint32_t val = 0;
                        if (!checkedRelocTarget(S, A, obj, symName, "ADDR32NB", err, target) ||
                            !checkedRelocDelta(target, imageBase, obj, symName, "ADDR32NB", err, rva) ||
                            !checkedU32Value(rva, obj, symName, "ADDR32NB", err, val))
                            return false;
                        writeLE32(patch, val);
                        continue;
                    }
                    if (rel.type == coff_a64::kSecRelLow12A ||
                        rel.type == coff_a64::kSecRelHigh12A ||
                        rel.type == coff_a64::kSecRelLow12L) {
                        if (!requirePatchBytes(4, "SECREL_LOW/HIGH"))
                            return false;
                        if (!requireTargetOutputSection("SECREL"))
                            return false;
                        uint64_t target = 0;
                        int64_t secRel = 0;
                        uint32_t value = 0;
                        if (!checkedRelocTarget(S, A, obj, symName, "SECREL", err, target) ||
                            !checkedRelocDelta(target,
                                               layout.sections[symOutSecIdx].virtualAddr,
                                               obj,
                                               symName,
                                               "SECREL",
                                               err,
                                               secRel) ||
                            !checkedU32Value(secRel, obj, symName, "SECREL", err, value))
                            return false;
                        if (rel.type == coff_a64::kSecRelHigh12A) {
                            if ((value >> 12) > 0xFFFu) {
                                err << "error: " << obj.name
                                    << ": SECREL_HIGH12A relocation out of 24-bit range";
                                if (!symName.empty())
                                    err << " for '" << symName << "'";
                                err << "\n";
                                return false;
                            }
                            value >>= 12;
                        } else {
                            value &= 0xFFF;
                        }

                        uint32_t insn = readLE32(patch);
                        if (rel.type == coff_a64::kSecRelLow12L) {
                            if (!isAArch64UnsignedLdStOffset(insn)) {
                                err << "error: " << obj.name
                                    << ": SECREL_LOW12L relocation is not applied to an "
                                       "AArch64 unsigned-offset load/store";
                                if (!symName.empty())
                                    err << " for '" << symName << "'";
                                err << "\n";
                                return false;
                            }
                            uint32_t shift = aarch64LdStOffsetShift(insn);
                            if (!checkPageOffsetAlignment(value, shift, obj, symName, err))
                                return false;
                            value >>= shift;
                        }
                        insn = (insn & 0xFFC003FF) | ((value & 0xFFF) << 10);
                        writeLE32(patch, insn);
                        continue;
                    }
                }

                if (obj.format == ObjFileFormat::MachO && arch == LinkArch::X86_64) {
                    if (rel.type == macho_x64::kSigned || rel.type == macho_x64::kSigned1 ||
                        rel.type == macho_x64::kSigned2 || rel.type == macho_x64::kSigned4) {
                        if (!requirePatchBytes(4, "Mach-O signed"))
                            return false;
                        const int64_t extraBias = rel.type == macho_x64::kSigned1
                                                      ? 1
                                                      : (rel.type == macho_x64::kSigned2
                                                             ? 2
                                                             : (rel.type == macho_x64::kSigned4 ? 4 : 0));
                        uint64_t target = 0;
                        int64_t delta = 0;
                        if (!checkedRelocTarget(S, A, obj, symName, "Mach-O signed", err, target) ||
                            !checkedRelocDelta(target, P, obj, symName, "Mach-O signed", err, delta))
                            return false;
                        const int64_t val = delta - extraBias;
                        if (!writeCheckedRel32(patch, val, obj, symName, "Mach-O signed", err))
                            return false;
                        continue;
                    }
                }

                const RelocAction action = classifyReloc(obj.format, arch, rel.type);

                switch (action) {
                    case RelocAction::PCRel32: {
                        if (!requirePatchBytes(4, "PC-relative"))
                            return false;
                        uint64_t target = 0;
                        int64_t val = 0;
                        if (!checkedRelocTarget(S, A, obj, symName, "PC-relative", err, target) ||
                            !checkedRelocDelta(target, P, obj, symName, "PC-relative", err, val))
                            return false;
                        if (!writeCheckedRel32(patch, val, obj, symName, "PC-relative", err))
                            return false;
                        break;
                    }
                    case RelocAction::Abs64: {
                        if (!requirePatchBytes(8, "64-bit absolute"))
                            return false;
                        uint64_t val = 0;
                        if (!checkedRelocTarget(S, A, obj, symName, "64-bit absolute", err, val))
                            return false;

                        const bool isDynamicSym =
                            !symName.empty() && dynamicSyms.count(symName) != 0;
                        const bool hasDynamicGot =
                            !symName.empty() && layout.globalSyms.count("__got_" + symName);

                        // Linux x86_64: imported data/function pointers are emitted as
                        // runtime-loader relocations instead of being resolved to the
                        // local jump stub address.
                        if (platform == LinkPlatform::Linux && (isDynamicSym || hasDynamicGot)) {
                            writeLE64(patch, 0);
                            layout.bindEntries.push_back({symName, outSecIdx, patchOff});
                            break;
                        }

                        // Mach-O: writable data pointers to imported symbols are left
                        // for dyld bind opcodes. Code branches use the synthetic stubs.
                        if (outSec.writable && (isDynamicSym || hasDynamicGot)) {
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
                            // TLS-relative offset.  _tlv_bootstrap expects offsets
                            // relative to the START of the TLS template (first
                            // S_THREAD_LOCAL_REGULAR section), not per-section VAs.
                            // The template spans all non-.tdata TLS sections including
                            // any alignment gaps between them.
                            uint64_t templateStartVA = 0;
                            for (const auto &ls : layout.sections) {
                                if (ls.tls && ls.name != ".tdata") {
                                    templateStartVA = ls.virtualAddr;
                                    break;
                                }
                            }
                            bool tlvMatch = false;
                            for (const auto &ls : layout.sections) {
                                if (!ls.tls || ls.name == ".tdata")
                                    continue; // Skip the descriptor section itself.
                                uint64_t tlsEnd = 0;
                                if (!checkedAddU64(ls.virtualAddr,
                                                   static_cast<uint64_t>(ls.data.size()),
                                                   tlsEnd))
                                    return false;
                                if (val >= ls.virtualAddr && val < tlsEnd) {
                                    val -= templateStartVA;
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

                        // Record image-base rebase entries for executable writers.
                        // Windows ARM64 requires a base relocation directory instead of
                        // fixed images; PE x64 can consume the same DIR64 fixups. Mach-O
                        // keeps its narrower writable-data rule because TLS and bind
                        // entries use separate dyld mechanisms there.
                        if (platform == LinkPlatform::Windows && outSec.alloc && val != 0)
                            layout.rebaseEntries.push_back({outSecIdx, patchOff});
                        else if (platform == LinkPlatform::macOS && outSec.writable &&
                                 !outSec.tls && val != 0)
                            layout.rebaseEntries.push_back({outSecIdx, patchOff});

                        break;
                    }
                    case RelocAction::Abs32: {
                        if (!requirePatchBytes(4, "32-bit absolute"))
                            return false;
                        uint64_t target = 0;
                        if (!checkedRelocTarget(S, A, obj, symName, "32-bit absolute", err, target))
                            return false;
                        if (target > std::numeric_limits<uint32_t>::max()) {
                            err << "error: " << obj.name
                                << ": 32-bit absolute relocation out of range";
                            if (!symName.empty())
                                err << " for '" << symName << "'";
                            err << "\n";
                            return false;
                        }
                        writeLE32(patch, static_cast<uint32_t>(target));
                        break;
                    }
                    case RelocAction::Branch26: {
                        if (!requirePatchBytes(4, "branch"))
                            return false;
                        uint32_t insn = readLE32(patch);
                        uint64_t target = 0;
                        int64_t disp = 0;
                        if (!checkedRelocTarget(S, A, obj, symName, "branch", err, target) ||
                            !checkedRelocDelta(target, P, obj, symName, "branch", err, disp))
                            return false;
                        if (!checkAArch64BranchAlignment(disp, obj, symName, "branch", err))
                            return false;
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
                        if (!requirePatchBytes(4, "ADRP"))
                            return false;
                        uint32_t insn = readLE32(patch);
                        uint64_t target = 0;
                        if (!checkedRelocTarget(S, A, obj, symName, "ADRP", err, target))
                            return false;
                        uint64_t pageS = target & ~0xFFFULL;
                        uint64_t pageP = P & ~0xFFFULL;
                        int64_t pageDelta = 0;
                        if (!checkedRelocDelta(pageS, pageP, obj, symName, "ADRP", err, pageDelta))
                            return false;
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
                        if (!requirePatchBytes(4, "page offset"))
                            return false;
                        uint32_t insn = readLE32(patch);
                        uint64_t target = 0;
                        if (!checkedRelocTarget(S, A, obj, symName, "page offset", err, target))
                            return false;
                        uint32_t pageOff = static_cast<uint32_t>(target) & 0xFFF;

                        // Mach-O ARM64_RELOC_PAGEOFF12 is used for both ADD (unscaled)
                        // and LDR/STR (scaled by access size). The linker must inspect the
                        // instruction to determine the correct scale factor.
                        //
                        // LDR/STR unsigned offset encoding: bits [31:30] = size,
                        // bits [29:24] = 11100x. Test: (insn & 0x3B000000) == 0x39000000.
                        // Scale = 1 << size, except 128-bit SIMD where scale = 16.
                        if (isAArch64UnsignedLdStOffset(insn)) {
                            uint32_t shift = aarch64LdStOffsetShift(insn);
                            if (!checkPageOffsetAlignment(pageOff, shift, obj, symName, err))
                                return false;
                            pageOff >>= shift;
                        }

                        insn = (insn & 0xFFC003FF) | (pageOff << 10);
                        writeLE32(patch, insn);
                        break;
                    }
                    case RelocAction::LdSt64Off: {
                        if (!requirePatchBytes(4, "load/store page offset"))
                            return false;
                        uint32_t insn = readLE32(patch);
                        uint64_t target = 0;
                        if (!checkedRelocTarget(S, A, obj, symName, "load/store page offset", err, target))
                            return false;
                        uint32_t pageOff = static_cast<uint32_t>(target) & 0xFFF;
                        if (!checkPageOffsetAlignment(pageOff, 3, obj, symName, err))
                            return false;
                        pageOff >>= 3;
                        insn = (insn & 0xFFC003FF) | (pageOff << 10);
                        writeLE32(patch, insn);
                        break;
                    }
                    case RelocAction::LdSt32Off: {
                        if (!requirePatchBytes(4, "load/store page offset"))
                            return false;
                        uint32_t insn = readLE32(patch);
                        uint64_t target = 0;
                        if (!checkedRelocTarget(S, A, obj, symName, "load/store page offset", err, target))
                            return false;
                        uint32_t pageOff = static_cast<uint32_t>(target) & 0xFFF;
                        if (!checkPageOffsetAlignment(pageOff, 2, obj, symName, err))
                            return false;
                        pageOff >>= 2;
                        insn = (insn & 0xFFC003FF) | (pageOff << 10);
                        writeLE32(patch, insn);
                        break;
                    }
                    case RelocAction::LdSt128Off: {
                        if (!requirePatchBytes(4, "load/store page offset"))
                            return false;
                        uint32_t insn = readLE32(patch);
                        uint64_t target = 0;
                        if (!checkedRelocTarget(S, A, obj, symName, "load/store page offset", err, target))
                            return false;
                        uint32_t pageOff = static_cast<uint32_t>(target) & 0xFFF;
                        if (!checkPageOffsetAlignment(pageOff, 4, obj, symName, err))
                            return false;
                        pageOff >>= 4;
                        insn = (insn & 0xFFC003FF) | (pageOff << 10);
                        writeLE32(patch, insn);
                        break;
                    }
                    case RelocAction::CondBr19: {
                        if (!requirePatchBytes(4, "conditional branch"))
                            return false;
                        uint32_t insn = readLE32(patch);
                        uint64_t target = 0;
                        int64_t disp = 0;
                        if (!checkedRelocTarget(S, A, obj, symName, "conditional branch", err, target) ||
                            !checkedRelocDelta(target, P, obj, symName, "conditional branch", err, disp))
                            return false;
                        if (!checkAArch64BranchAlignment(
                                disp, obj, symName, "conditional branch", err))
                            return false;
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
                        if (!requirePatchBytes(4, "GOT ADRP"))
                            return false;
                        // GOT ADRP: if symbol is dynamic, point at its GOT entry;
                        // otherwise, GOT relaxation → point directly at symbol.
                        uint64_t target = S;
                        auto git = layout.globalSyms.find("__got_" + symName);
                        if (git != layout.globalSyms.end() && git->second.resolvedAddr != 0)
                            target = git->second.resolvedAddr; // Use GOT entry for dynamic sym.

                        uint32_t insn = readLE32(patch);
                        uint64_t targetWithAddend = 0;
                        if (!checkedRelocTarget(target, A, obj, symName, "GOT ADRP", err, targetWithAddend))
                            return false;
                        uint64_t pageT = targetWithAddend & ~0xFFFULL;
                        uint64_t pageP = P & ~0xFFFULL;
                        int64_t pageDelta = 0;
                        if (!checkedRelocDelta(pageT, pageP, obj, symName, "GOT ADRP", err, pageDelta))
                            return false;
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
                        if (!requirePatchBytes(4, "GOT page offset"))
                            return false;
                        // GOT LDR pageoff: if symbol is dynamic, keep LDR with GOT entry offset;
                        // otherwise, GOT relaxation → rewrite LDR as ADD.
                        auto git = layout.globalSyms.find("__got_" + symName);
                        if (git != layout.globalSyms.end() && git->second.resolvedAddr != 0) {
                            // Dynamic: LDR from GOT entry (8-byte scaled).
                            uint32_t insn = readLE32(patch);
                            if (!isAArch64LdrXUnsignedOffset(insn)) {
                                err << "error: " << obj.name
                                    << ": GOT page-offset relocation is not applied to LDR X";
                                if (!symName.empty())
                                    err << " for '" << symName << "'";
                                err << "\n";
                                return false;
                            }
                            uint64_t gotTarget = 0;
                            if (!checkedRelocTarget(git->second.resolvedAddr,
                                                    A,
                                                    obj,
                                                    symName,
                                                    "GOT page offset",
                                                    err,
                                                    gotTarget))
                                return false;
                            uint32_t pageOff = static_cast<uint32_t>(gotTarget) & 0xFFF;
                            if (!checkPageOffsetAlignment(pageOff, 3, obj, symName, err))
                                return false;
                            pageOff >>= 3; // 8-byte scale for LDR X
                            insn = (insn & 0xFFC003FF) | (pageOff << 10);
                            writeLE32(patch, insn);
                        } else {
                            // Local: GOT relaxation — rewrite LDR to ADD.
                            uint32_t insn = readLE32(patch);
                            if (!isAArch64LdrXUnsignedOffset(insn)) {
                                err << "error: " << obj.name
                                    << ": local GOT relaxation requires LDR X";
                                if (!symName.empty())
                                    err << " for '" << symName << "'";
                                err << "\n";
                                return false;
                            }
                            uint32_t rd = insn & 0x1F;
                            uint32_t rn = (insn >> 5) & 0x1F;
                            uint64_t target = 0;
                            if (!checkedRelocTarget(S, A, obj, symName, "GOT page offset", err, target))
                                return false;
                            uint32_t pageOff = static_cast<uint32_t>(target) & 0xFFF;
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

    if (platform == LinkPlatform::Windows && !sortWindowsPdata(layout, arch, err))
        return false;

    return true;
}

} // namespace viper::codegen::linker
