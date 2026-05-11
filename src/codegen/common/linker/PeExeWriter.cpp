//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/PeExeWriter.cpp
// Purpose: Write PE32+ executables from a linked layout.
// Key invariants:
//   - Existing section RVAs from SectionMerger are preserved
//   - Generated PE import data lives in a dedicated read-only section
//   - Windows x64 gets a tiny startup shim that calls the resolved entry point
//     and then terminates via ExitProcess
//   - ASLR flags are not advertised unless relocations are emitted
// Links: codegen/common/linker/PeExeWriter.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/PeExeWriter.hpp"

#include "codegen/common/linker/AlignUtil.hpp"
#include "codegen/common/linker/ExeWriterUtil.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper::codegen::linker {

using encoding::padTo;
using encoding::writeLE16;
using encoding::writeLE32;
using encoding::writeLE64;

namespace {

static constexpr uint16_t IMAGE_FILE_MACHINE_AMD64 = 0x8664;
static constexpr uint16_t IMAGE_FILE_MACHINE_ARM64 = 0xAA64;

static constexpr uint16_t kDllCharHighEntropyVA = 0x0020;
static constexpr uint16_t kDllCharDynamicBase = 0x0040;
static constexpr uint16_t kDllCharNXCompat = 0x0100;
static constexpr uint16_t kDllCharTermServerAware = 0x8000;

static constexpr uint16_t kImageRelBasedAbsolute = 0;
static constexpr uint16_t kImageRelBasedDir64 = 10;
static constexpr uint32_t kRelocSectionChars = 0x42000040; // INIT_DATA | DISCARDABLE | READ

struct ImportLayout {
    std::vector<uint8_t> data;
    uint32_t importDirRva = 0;
    uint32_t importDirSize = 0;
    uint32_t iatRva = 0;
    uint32_t iatSize = 0;
    std::unordered_map<std::string, uint32_t> slotRvas;
    std::vector<std::pair<uint32_t, uint64_t>> slotInitializers;
};

struct TlsLayout {
    std::vector<uint8_t> data;
    uint32_t directoryRva = 0;
    uint32_t directorySize = 0;
};

struct ExceptionLayout {
    uint32_t directoryRva = 0;
    uint32_t directorySize = 0;
};

struct BaseRelocLayout {
    std::vector<uint8_t> data;
    uint32_t directoryRva = 0;
    uint32_t directorySize = 0;
};

struct ResourceLayout {
    std::vector<uint8_t> data;
    uint32_t directoryRva = 0;
    uint32_t directorySize = 0;
};

struct PeSection {
    std::string name;
    const std::vector<uint8_t> *data = nullptr;
    uint32_t virtualAddress = 0;
    uint32_t virtualSize = 0;
    uint32_t sizeOfRawData = 0;
    uint32_t pointerToRawData = 0;
    uint32_t characteristics = 0;
    bool alloc = true;
    bool executable = false;
    bool writable = false;
    bool zeroFill = false;
};

/// @brief In-place little-endian uint16 patch at @p offset within @p buf.
void putLE16(std::vector<uint8_t> &buf, size_t offset, uint16_t val) {
    buf[offset + 0] = static_cast<uint8_t>(val & 0xFF);
    buf[offset + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
}

/// @brief In-place little-endian uint32 patch at @p offset within @p buf.
void putLE32(std::vector<uint8_t> &buf, size_t offset, uint32_t val) {
    buf[offset + 0] = static_cast<uint8_t>(val & 0xFF);
    buf[offset + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    buf[offset + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    buf[offset + 3] = static_cast<uint8_t>((val >> 24) & 0xFF);
}

/// @brief In-place little-endian uint64 patch at @p offset within @p buf.
void putLE64(std::vector<uint8_t> &buf, size_t offset, uint64_t val) {
    putLE32(buf, offset, static_cast<uint32_t>(val & 0xFFFFFFFFULL));
    putLE32(buf, offset + 4, static_cast<uint32_t>(val >> 32));
}

/// @brief Append a little-endian uint16 to @p buf.
void appendLE16(std::vector<uint8_t> &buf, uint16_t val) {
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
}

/// @brief Append a little-endian uint32 to @p buf.
void appendLE32(std::vector<uint8_t> &buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

/// @brief Build the IMAGE_SCN_* characteristics word for a PE section.
/// @details Picks code/data/rwdata/discardable bits based on the section's
///          allocate/exec/write/zerofill attributes. Discardable applies to
///          non-allocatable sections (.debug_*) so the loader can skip them.
uint32_t sectionChars(bool executable, bool writable, bool alloc, bool zeroFill = false) {
    if (!alloc)
        return 0x42000040; // CNT_INITIALIZED_DATA | MEM_DISCARDABLE | MEM_READ
    if (executable)
        return 0x60000020; // CNT_CODE | MEM_EXECUTE | MEM_READ
    if (zeroFill)
        return 0xC0000080; // CNT_UNINITIALIZED_DATA | MEM_READ | MEM_WRITE
    if (writable)
        return 0xC0000040; // CNT_INITIALIZED_DATA | MEM_READ | MEM_WRITE
    return 0x40000040;     // CNT_INITIALIZED_DATA | MEM_READ
}

/// @brief Build the .idata import directory blob from a DLL import list.
/// @details Materialises the IDT, ILT, hint/name table, DLL name table, and
///          (when @p externalSlotRvas is empty) a fresh IAT into a single
///          contiguous buffer. When @p externalSlotRvas is provided each
///          import binds to a pre-allocated slot in another section instead,
///          and the writer later patches each slot to the hint-name RVA via
///          @c slotInitializers.
/// @param imports          DLL import groups to serialise.
/// @param sectionRva       RVA at which the resulting buffer will be placed.
/// @param externalSlotRvas Optional symbol→RVA map for IAT slot reuse.
/// @return Self-contained blob plus IDT/IAT range descriptors.
ImportLayout buildImportTables(const std::vector<DllImport> &imports,
                               uint32_t sectionRva,
                               const std::unordered_map<std::string, uint32_t> &externalSlotRvas) {
    ImportLayout result{};
    if (imports.empty())
        return result;

    uint32_t idtSize = static_cast<uint32_t>((imports.size() + 1) * 20);
    uint32_t iltSize = 0;
    uint32_t hintNameSize = 0;
    uint32_t dllNameSize = 0;
    bool useExternalSlots = !externalSlotRvas.empty();

    for (const auto &imp : imports) {
        iltSize += static_cast<uint32_t>((imp.functions.size() + 1) * 8);
        dllNameSize += static_cast<uint32_t>(imp.dllName.size() + 1);
        for (const auto &fn : imp.functions) {
            auto importIt = imp.importNames.find(fn);
            const std::string &importName =
                (importIt != imp.importNames.end()) ? importIt->second : fn;
            hintNameSize += static_cast<uint32_t>(alignUp(2 + importName.size() + 1, 2));
        }
    }

    uint32_t idtOff = 0;
    uint32_t iltOff = idtOff + idtSize;
    uint32_t hintOff = iltOff + iltSize;
    uint32_t dllNameOff = hintOff + hintNameSize;
    uint32_t iatOff = 0;
    uint32_t iatSize = 0;
    uint32_t totalSize = static_cast<uint32_t>(alignUp(dllNameOff + dllNameSize, 8));
    if (!useExternalSlots) {
        iatOff = totalSize;
        iatSize = iltSize;
        totalSize = iatOff + iatSize;
    }

    result.data.resize(totalSize, 0);

    uint32_t curIltOff = iltOff;
    uint32_t curHintOff = hintOff;
    uint32_t curDllNameOff = dllNameOff;
    uint32_t curIatOff = iatOff;
    uint32_t minIatRva = UINT32_MAX;
    uint32_t maxIatEndRva = 0;

    for (size_t i = 0; i < imports.size(); ++i) {
        const auto &imp = imports[i];
        const uint32_t idtEntryOff = idtOff + static_cast<uint32_t>(i) * 20;
        uint32_t firstThunkRva = 0;
        if (useExternalSlots) {
            if (!imp.functions.empty()) {
                const auto firstIt = externalSlotRvas.find(imp.functions.front());
                if (firstIt != externalSlotRvas.end())
                    firstThunkRva = firstIt->second;
            }
        } else {
            firstThunkRva = sectionRva + curIatOff;
        }

        putLE32(result.data, idtEntryOff + 0, sectionRva + curIltOff);
        putLE32(result.data, idtEntryOff + 4, 0);
        putLE32(result.data, idtEntryOff + 8, 0);
        putLE32(result.data, idtEntryOff + 12, sectionRva + curDllNameOff);
        putLE32(result.data, idtEntryOff + 16, firstThunkRva);

        std::memcpy(
            result.data.data() + curDllNameOff, imp.dllName.c_str(), imp.dllName.size() + 1);
        curDllNameOff += static_cast<uint32_t>(imp.dllName.size() + 1);

        for (const auto &fn : imp.functions) {
            auto importIt = imp.importNames.find(fn);
            const std::string &importName =
                (importIt != imp.importNames.end()) ? importIt->second : fn;
            const uint32_t hintNameRva = sectionRva + curHintOff;
            putLE16(result.data, curHintOff, 0);
            std::memcpy(
                result.data.data() + curHintOff + 2, importName.c_str(), importName.size() + 1);
            curHintOff += static_cast<uint32_t>(alignUp(2 + importName.size() + 1, 2));

            putLE32(result.data, curIltOff, hintNameRva);
            putLE32(result.data, curIltOff + 4, 0);
            curIltOff += 8;

            if (useExternalSlots) {
                const auto slotIt = externalSlotRvas.find(fn);
                if (slotIt != externalSlotRvas.end()) {
                    result.slotRvas[fn] = slotIt->second;
                    result.slotInitializers.push_back({slotIt->second, hintNameRva});
                    minIatRva = std::min(minIatRva, slotIt->second);
                    maxIatEndRva = std::max(maxIatEndRva, slotIt->second + 8);
                }
            } else {
                putLE32(result.data, curIatOff, hintNameRva);
                putLE32(result.data, curIatOff + 4, 0);
                result.slotRvas[fn] = sectionRva + curIatOff;
                curIatOff += 8;
            }
        }

        curIltOff += 8;
        if (!useExternalSlots)
            curIatOff += 8;
        else if (firstThunkRva != 0) {
            minIatRva = std::min(minIatRva, firstThunkRva);
            maxIatEndRva =
                std::max(maxIatEndRva,
                         firstThunkRva + static_cast<uint32_t>((imp.functions.size() + 1) * 8));
        }
    }

    result.importDirRva = sectionRva + idtOff;
    result.importDirSize = idtSize;
    if (useExternalSlots) {
        if (minIatRva != UINT32_MAX) {
            result.iatRva = minIatRva;
            result.iatSize = maxIatEndRva - minIatRva;
        }
    } else {
        result.iatRva = sectionRva + iatOff;
        result.iatSize = iatSize;
    }
    return result;
}

/// @brief Test whether @p value fits in a signed 32-bit field (PE PC-rel reach).
bool fitsInt32(int64_t value) {
    return value >= static_cast<int64_t>(INT32_MIN) && value <= static_cast<int64_t>(INT32_MAX);
}

/// @brief Test whether @p value starts with the C-string @p prefix.
bool hasPrefix(const std::string &value, const char *prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool checkedU32(uint64_t value, const char *what, std::ostream &err, uint32_t &out) {
    if (value > std::numeric_limits<uint32_t>::max()) {
        err << "error: PE " << what << " exceeds 32-bit file format limit\n";
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

bool checkedSizeU32(size_t value, const char *what, std::ostream &err, uint32_t &out) {
    if (value > std::numeric_limits<uint32_t>::max()) {
        err << "error: PE " << what << " exceeds 32-bit file format limit\n";
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

bool checkedAddU32(uint32_t lhs,
                   uint32_t rhs,
                   const char *what,
                   std::ostream &err,
                   uint32_t &out) {
    if (lhs > std::numeric_limits<uint32_t>::max() - rhs) {
        err << "error: PE " << what << " overflows 32-bit file format limit\n";
        return false;
    }
    out = lhs + rhs;
    return true;
}

bool checkedRva(uint64_t imageBase,
                uint64_t virtualAddr,
                const std::string &what,
                std::ostream &err,
                uint32_t &out) {
    if (virtualAddr < imageBase) {
        err << "error: PE section '" << what << "' is below the image base\n";
        return false;
    }
    return checkedU32(virtualAddr - imageBase, "RVA", err, out);
}

bool checkedAlignUpU32(uint64_t value,
                       uint32_t alignment,
                       const char *what,
                       std::ostream &err,
                       uint32_t &out) {
    if (value > std::numeric_limits<size_t>::max()) {
        err << "error: PE " << what << " exceeds addressable size\n";
        return false;
    }
    size_t aligned = 0;
    try {
        aligned = alignUp(static_cast<size_t>(value), alignment);
    } catch (const std::exception &ex) {
        err << "error: PE " << what << " alignment failed: " << ex.what() << "\n";
        return false;
    }
    return checkedSizeU32(aligned, what, err, out);
}

/// @brief Encode a power-of-two alignment as the COFF section-alignment bit field.
/// @details COFF stores alignment in bits 20-23 of Characteristics. A value of
///          1 means 1-byte (no alignment), 2 means 2-byte, etc. up to 8192.
uint32_t encodeCoffAlignment(uint32_t alignment) {
    if (alignment <= 1)
        return 0;

    uint32_t power = 1;
    uint32_t bits = 1;
    while (power < alignment && bits < 0xF) {
        power <<= 1;
        ++bits;
    }
    return bits << 20;
}

/// @brief Detect whether the layout contains any TLS-section data.
/// @details Used to gate emission of the IMAGE_DIRECTORY_ENTRY_TLS data directory.
bool layoutHasTls(const LinkLayout &layout) {
    for (const auto &sec : layout.sections) {
        if (sec.alloc && sec.tls && !sec.data.empty())
            return true;
    }
    return false;
}

/// @brief Synthesise a Windows x64 _start shim that calls main and ExitProcess.
/// @details The shim runs *before* main, aligns RSP, calls the resolved entry
///          point, then tail-jumps through the IAT slot for kernel32!ExitProcess
///          with main's return value as the exit code. Must reach both
///          @p entryAddr and @p exitProcessIatRva via 32-bit PC-relative
///          displacement, which the caller checks with @c fitsInt32.
std::vector<uint8_t> buildX64StartupStub(uint64_t imageBase,
                                         uint64_t entryAddr,
                                         uint32_t stubRva,
                                         uint32_t exitProcessIatRva,
                                         std::ostream &err) {
    std::vector<uint8_t> stub = {
        0x48,
        0x83,
        0xEC,
        0x28, // sub rsp, 40
        0xE8,
        0x00,
        0x00,
        0x00,
        0x00, // call entry
        0x89,
        0xC1, // mov ecx, eax
        0xFF,
        0x15,
        0x00,
        0x00,
        0x00,
        0x00, // call [rip+disp32] ; ExitProcess
        0xCC, // int3 if ExitProcess returns
    };

    const uint64_t stubVa = imageBase + stubRva;
    const int64_t callDisp = static_cast<int64_t>(entryAddr) - static_cast<int64_t>(stubVa + 9);
    const int64_t iatDisp =
        static_cast<int64_t>(exitProcessIatRva) - static_cast<int64_t>(stubRva + 17);

    if (!fitsInt32(callDisp)) {
        err << "error: PE startup stub cannot reach entry point\n";
        return {};
    }
    if (!fitsInt32(iatDisp)) {
        err << "error: PE startup stub cannot reach ExitProcess IAT slot\n";
        return {};
    }

    putLE32(stub, 5, static_cast<uint32_t>(static_cast<int32_t>(callDisp)));
    putLE32(stub, 13, static_cast<uint32_t>(static_cast<int32_t>(iatDisp)));
    return stub;
}

std::vector<uint8_t> buildAArch64StartupStub(uint64_t imageBase,
                                             uint64_t entryAddr,
                                             uint32_t stubRva,
                                             uint32_t exitProcessIatRva,
                                             std::ostream &err) {
    std::vector<uint8_t> stub = {
        0x10, 0x00, 0x00, 0x90, // adrp x16, entry
        0x10, 0x02, 0x00, 0x91, // add  x16, x16, #entry_lo12
        0x00, 0x02, 0x3F, 0xD6, // blr  x16
        0x10, 0x00, 0x00, 0x90, // adrp x16, __imp_ExitProcess
        0x10, 0x02, 0x40, 0xF9, // ldr  x16, [x16, #iat_lo12]
        0x00, 0x02, 0x1F, 0xD6, // br   x16
        0x00, 0x00, 0x20, 0xD4, // brk  #0
    };

    auto patchAdrp = [&](size_t offset, uint64_t targetVa) {
        const uint64_t pcVa = imageBase + stubRva + static_cast<uint32_t>(offset);
        const uint64_t pageTarget = targetVa & ~0xFFFULL;
        const uint64_t pagePc = pcVa & ~0xFFFULL;
        const int64_t pageDelta = static_cast<int64_t>(pageTarget) - static_cast<int64_t>(pagePc);
        const int64_t immHiLo = pageDelta >> 12;
        if (immHiLo > 0xFFFFF || immHiLo < -0x100000) {
            err << "error: PE ARM64 startup stub ADRP target out of range\n";
            return false;
        }

        uint32_t insn = 0x90000010U;
        const uint32_t immlo = static_cast<uint32_t>(immHiLo) & 0x3;
        const uint32_t immhi = (static_cast<uint32_t>(immHiLo) >> 2) & 0x7FFFF;
        insn = (insn & 0x9F00001F) | (immlo << 29) | (immhi << 5);
        putLE32(stub, offset, insn);
        return true;
    };

    auto patchAddLo12 = [&](size_t offset, uint64_t targetVa) {
        const uint32_t pageOff = static_cast<uint32_t>(targetVa & 0xFFFULL);
        uint32_t insn = 0x91000210U;
        insn = (insn & 0xFFC003FF) | (pageOff << 10);
        putLE32(stub, offset, insn);
    };

    auto patchLdrLo12 = [&](size_t offset, uint64_t targetVa) {
        const uint32_t pageOff = static_cast<uint32_t>((targetVa & 0xFFFULL) >> 3);
        uint32_t insn = 0xF9400210U;
        insn = (insn & 0xFFC003FF) | (pageOff << 10);
        putLE32(stub, offset, insn);
    };

    if (!patchAdrp(0, entryAddr))
        return {};
    patchAddLo12(4, entryAddr);

    const uint64_t exitProcessIatVa = imageBase + static_cast<uint64_t>(exitProcessIatRva);
    if (!patchAdrp(12, exitProcessIatVa))
        return {};
    patchLdrLo12(16, exitProcessIatVa);
    return stub;
}

std::string sectionNameFor(const OutputSection &sec) {
    if (sec.tls)
        return ".tls";
    if (sec.zeroFill)
        return ".bss";
    if (hasPrefix(sec.name, ".pdata"))
        return ".pdata";
    if (hasPrefix(sec.name, ".xdata"))
        return ".xdata";
    if (sec.executable)
        return ".text";
    if (sec.writable)
        return ".data";
    return ".rdata";
}

TlsLayout buildTlsDirectory(uint64_t imageBase,
                            const LinkLayout &layout,
                            uint32_t sectionRva,
                            std::ostream &err) {
    TlsLayout tls{};

    uint32_t tlsStartRva = UINT32_MAX;
    uint32_t tlsRawEndRva = 0;
    uint32_t tlsMemEndRva = 0;
    uint32_t tlsAlignment = 1;
    bool sawRawTemplateBytes = false;
    for (const auto &sec : layout.sections) {
        if (!sec.alloc || !sec.tls || sec.data.empty())
            continue;
        uint32_t secRva = 0;
        if (!checkedRva(imageBase, sec.virtualAddr, sec.name, err, secRva))
            return {};
        uint32_t secSize = 0;
        if (!checkedSizeU32(sec.data.size(), "TLS section size", err, secSize))
            return {};
        uint32_t secEnd = 0;
        if (!checkedAddU32(secRva, secSize, "TLS section RVA range", err, secEnd))
            return {};
        tlsStartRva = std::min(tlsStartRva, secRva);
        tlsMemEndRva = std::max(tlsMemEndRva, secEnd);
        if (!sec.zeroFill) {
            sawRawTemplateBytes = true;
            tlsRawEndRva = std::max(tlsRawEndRva, secEnd);
        }
        tlsAlignment = std::max(tlsAlignment, sec.alignment);
    }

    if (tlsStartRva == UINT32_MAX)
        return tls;

    const auto tlsIndexIt = layout.globalSyms.find("_tls_index");
    if (tlsIndexIt == layout.globalSyms.end() || tlsIndexIt->second.resolvedAddr == 0) {
        err << "error: PE TLS image is missing a resolved _tls_index symbol\n";
        return {};
    }

    const uint32_t tlsEndRva = sawRawTemplateBytes ? tlsRawEndRva : tlsStartRva;
    const uint32_t tlsZeroFill = tlsMemEndRva > tlsEndRva ? tlsMemEndRva - tlsEndRva : 0;
    uint32_t callbackArrayRva = 0;
    if (!checkedAddU32(sectionRva, 40, "TLS callback-array RVA", err, callbackArrayRva))
        return {};

    tls.data.resize(48, 0);
    putLE64(tls.data, 0, imageBase + tlsStartRva);
    putLE64(tls.data, 8, imageBase + tlsEndRva);
    putLE64(tls.data, 16, tlsIndexIt->second.resolvedAddr);
    putLE64(tls.data, 24, imageBase + callbackArrayRva); // Null-terminated callback array.
    putLE32(tls.data, 32, tlsZeroFill);
    putLE32(tls.data, 36, encodeCoffAlignment(tlsAlignment));
    tls.directoryRva = sectionRva;
    tls.directorySize = 40;
    return tls;
}

ExceptionLayout findExceptionDirectory(const std::vector<PeSection> &sections) {
    ExceptionLayout result{};
    for (const auto &sec : sections) {
        if (sec.alloc && sec.name == ".pdata") {
            result.directoryRva = sec.virtualAddress;
            result.directorySize = sec.virtualSize;
            break;
        }
    }
    return result;
}

std::vector<uint8_t> buildBaseRelocationBlocks(std::vector<uint32_t> rvas, bool forceNonEmpty) {
    std::sort(rvas.begin(), rvas.end());
    rvas.erase(std::unique(rvas.begin(), rvas.end()), rvas.end());

    std::vector<uint8_t> data;
    if (rvas.empty()) {
        if (forceNonEmpty) {
            appendLE32(data, 0);
            appendLE32(data, 8);
        }
        return data;
    }

    size_t i = 0;
    while (i < rvas.size()) {
        const uint32_t pageRva = rvas[i] & ~0xFFFu;
        std::vector<uint16_t> entries;
        while (i < rvas.size() && (rvas[i] & ~0xFFFu) == pageRva) {
            entries.push_back(static_cast<uint16_t>((kImageRelBasedDir64 << 12) |
                                                    (rvas[i] & 0x0FFFu)));
            ++i;
        }
        if ((entries.size() & 1u) != 0)
            entries.push_back(static_cast<uint16_t>(kImageRelBasedAbsolute << 12));

        appendLE32(data, pageRva);
        appendLE32(data, static_cast<uint32_t>(8 + entries.size() * sizeof(uint16_t)));
        for (uint16_t entry : entries)
            appendLE16(data, entry);
    }
    return data;
}

ResourceLayout buildDefaultManifestResource(uint32_t sectionRva) {
    static constexpr uint16_t kRtManifest = 24;
    static constexpr uint16_t kExeManifestId = 1;
    static constexpr uint16_t kLangEnUs = 0x0409;

    static const char kManifest[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\n"
        "  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">\n"
        "    <security><requestedPrivileges>\n"
        "      <requestedExecutionLevel level=\"asInvoker\" uiAccess=\"false\"/>\n"
        "    </requestedPrivileges></security>\n"
        "  </trustInfo>\n"
        "</assembly>\n";

    ResourceLayout result{};

    const uint32_t rootDirOff = 0;
    const uint32_t rootEntryOff = rootDirOff + 16;
    const uint32_t typeDirOff = rootEntryOff + 8;
    const uint32_t typeEntryOff = typeDirOff + 16;
    const uint32_t nameDirOff = typeEntryOff + 8;
    const uint32_t langEntryOff = nameDirOff + 16;
    const uint32_t dataEntryOff = langEntryOff + 8;
    const uint32_t manifestOff = static_cast<uint32_t>(alignUp(dataEntryOff + 16, 4));
    const uint32_t manifestSize = static_cast<uint32_t>(sizeof(kManifest) - 1);
    const uint32_t totalSize = static_cast<uint32_t>(alignUp(manifestOff + manifestSize, 4));

    std::vector<uint8_t> data(totalSize, 0);
    auto putDir = [&](uint32_t off, uint16_t idEntryCount) {
        putLE16(data, off + 12, 0);           // NumberOfNamedEntries
        putLE16(data, off + 14, idEntryCount); // NumberOfIdEntries
    };

    putDir(rootDirOff, 1);
    putLE32(data, rootEntryOff + 0, kRtManifest);
    putLE32(data, rootEntryOff + 4, 0x80000000u | typeDirOff);

    putDir(typeDirOff, 1);
    putLE32(data, typeEntryOff + 0, kExeManifestId);
    putLE32(data, typeEntryOff + 4, 0x80000000u | nameDirOff);

    putDir(nameDirOff, 1);
    putLE32(data, langEntryOff + 0, kLangEnUs);
    putLE32(data, langEntryOff + 4, dataEntryOff);

    putLE32(data, dataEntryOff + 0, sectionRva + manifestOff);
    putLE32(data, dataEntryOff + 4, manifestSize);
    putLE32(data, dataEntryOff + 8, 65001); // UTF-8
    std::memcpy(data.data() + manifestOff, kManifest, manifestSize);

    result.data = std::move(data);
    result.directoryRva = sectionRva;
    result.directorySize = totalSize;
    return result;
}

} // namespace

bool writePeExe(const std::string &path,
                const LinkLayout &layout,
                LinkArch arch,
                const std::vector<DllImport> &imports,
                const std::unordered_map<std::string, uint32_t> &slotRvas,
                bool emitStartupStub,
                std::size_t stackSize,
                std::ostream &err) {
    const uint16_t machine =
        (arch == LinkArch::AArch64) ? IMAGE_FILE_MACHINE_ARM64 : IMAGE_FILE_MACHINE_AMD64;
    const uint64_t imageBase = defaultImageBaseForPlatform(LinkPlatform::Windows);
    const uint32_t sectionAlignment = 0x1000;
    const uint32_t fileAlignment = 0x200;
    const uint64_t stackReserve =
        stackSize != 0 ? static_cast<uint64_t>(alignUp(stackSize, static_cast<std::size_t>(0x1000)))
                       : 0x100000ULL;
    const uint64_t stackCommit = std::min<uint64_t>(stackReserve, 0x1000ULL);

    if (layout.entryAddr == 0) {
        err << "error: no PE entry point was resolved\n";
        return false;
    }

    std::vector<std::vector<uint8_t>> ownedSectionData;
    ownedSectionData.reserve(layout.sections.size());

    std::vector<PeSection> sections;
    sections.reserve(layout.sections.size() + 4);

    uint32_t nextGeneratedRva = sectionAlignment;
    for (const auto &sec : layout.sections) {
        // PE images cannot carry ELF/Mach-O style non-alloc debug sections.
        // Emitting them with RVA 0 makes Windows reject the executable.
        if (!sec.alloc)
            continue;
        if (sec.data.empty())
            continue;

        PeSection ps;
        ps.name = sectionNameFor(sec);
        if (ps.name.size() > 8) {
            err << "error: PE section name '" << ps.name << "' exceeds 8 bytes\n";
            return false;
        }
        ownedSectionData.push_back(sec.data);
        ps.data = &ownedSectionData.back();
        ps.alloc = sec.alloc;
        ps.executable = sec.executable;
        ps.writable = sec.writable;
        ps.zeroFill = sec.zeroFill;
        if (!checkedRva(imageBase, sec.virtualAddr, sec.name, err, ps.virtualAddress))
            return false;
        if (!checkedSizeU32(ownedSectionData.back().size(), "section virtual size", err, ps.virtualSize))
            return false;
        ps.characteristics = sectionChars(sec.executable, sec.writable, sec.alloc, sec.zeroFill);
        sections.push_back(ps);

        if (sec.alloc) {
            uint32_t alignedVirtualSize = 0;
            if (!checkedAlignUpU32(
                    ps.virtualSize, sectionAlignment, "section virtual size", err, alignedVirtualSize))
                return false;
            uint32_t end = 0;
            if (!checkedAddU32(ps.virtualAddress, alignedVirtualSize, "section RVA range", err, end))
                return false;
            nextGeneratedRva = std::max(nextGeneratedRva, end);
        }
    }

    std::vector<uint8_t> generatedImportData;
    std::vector<uint8_t> generatedStubData;
    std::vector<uint8_t> generatedTlsData;
    std::vector<uint8_t> generatedBaseRelocData;
    std::vector<uint8_t> generatedResourceData;
    ImportLayout importLayout{};
    TlsLayout tlsLayout{};
    BaseRelocLayout baseRelocLayout{};
    ResourceLayout resourceLayout{};

    if (!imports.empty()) {
        uint32_t importRva = 0;
        if (!checkedAlignUpU32(
                nextGeneratedRva, sectionAlignment, "import section RVA", err, importRva))
            return false;
        importLayout = buildImportTables(imports, importRva, slotRvas);
        generatedImportData = importLayout.data;

        auto patchExternalIatSlot = [&](uint32_t slotRva, uint64_t thunkValue) -> bool {
            for (auto &sec : sections) {
                if (!sec.alloc || sec.data == nullptr)
                    continue;
                if (slotRva < sec.virtualAddress ||
                    slotRva + 8 > sec.virtualAddress + sec.virtualSize)
                    continue;
                auto *bytes = const_cast<std::vector<uint8_t> *>(sec.data);
                const size_t off = static_cast<size_t>(slotRva - sec.virtualAddress);
                if (off + 8 > bytes->size())
                    return false;
                putLE64(*bytes, off, thunkValue);
                return true;
            }
            return false;
        };

        for (const auto &[slotRva, thunkValue] : importLayout.slotInitializers) {
            if (!patchExternalIatSlot(slotRva, thunkValue)) {
                err << "error: PE import slot RVA 0x" << std::hex << slotRva << std::dec
                    << " does not map to a writable output section\n";
                return false;
            }
        }

        PeSection importSec;
        importSec.name = ".idata";
        importSec.data = &generatedImportData;
        importSec.virtualAddress = importRva;
        if (!checkedSizeU32(
                generatedImportData.size(), "import section size", err, importSec.virtualSize))
            return false;
        importSec.characteristics = sectionChars(false, false, true);
        sections.push_back(importSec);
        uint32_t alignedImportSize = 0;
        if (!checkedAlignUpU32(generatedImportData.size(),
                               sectionAlignment,
                               "import section size",
                               err,
                               alignedImportSize) ||
            !checkedAddU32(
                importRva, alignedImportSize, "import section RVA range", err, nextGeneratedRva))
            return false;
    }

    const bool haveTls = layoutHasTls(layout);
    uint32_t tlsDirRva = 0;
    if (!checkedAlignUpU32(nextGeneratedRva, sectionAlignment, "TLS directory RVA", err, tlsDirRva))
        return false;
    tlsLayout = buildTlsDirectory(imageBase, layout, tlsDirRva, err);
    if (haveTls && tlsLayout.directorySize == 0)
        return false;
    if (tlsLayout.directorySize != 0) {
        generatedTlsData = tlsLayout.data;

        PeSection tlsMetaSec;
        tlsMetaSec.name = ".rdata";
        tlsMetaSec.data = &generatedTlsData;
        tlsMetaSec.virtualAddress = tlsDirRva;
        if (!checkedSizeU32(
                generatedTlsData.size(), "TLS metadata section size", err, tlsMetaSec.virtualSize))
            return false;
        tlsMetaSec.characteristics = sectionChars(false, false, true);
        sections.push_back(tlsMetaSec);
        uint32_t alignedTlsSize = 0;
        if (!checkedAlignUpU32(generatedTlsData.size(),
                               sectionAlignment,
                               "TLS metadata section size",
                               err,
                               alignedTlsSize) ||
            !checkedAddU32(
                tlsDirRva, alignedTlsSize, "TLS metadata section RVA range", err, nextGeneratedRva))
            return false;
    }

    uint32_t entryRva = 0;
    if (!checkedRva(imageBase, layout.entryAddr, "<entry>", err, entryRva))
        return false;
    if (emitStartupStub && !imports.empty()) {
        const auto exitIt = importLayout.slotRvas.find("ExitProcess");
        if (exitIt == importLayout.slotRvas.end()) {
            err << "error: PE writer requires ExitProcess for startup stub\n";
            return false;
        }

        uint32_t stubRva = 0;
        if (!checkedAlignUpU32(
                nextGeneratedRva, sectionAlignment, "startup stub RVA", err, stubRva))
            return false;
        if (arch == LinkArch::AArch64) {
            generatedStubData =
                buildAArch64StartupStub(imageBase, layout.entryAddr, stubRva, exitIt->second, err);
        } else {
            generatedStubData =
                buildX64StartupStub(imageBase, layout.entryAddr, stubRva, exitIt->second, err);
        }
        if (generatedStubData.empty())
            return false;

        PeSection stubSec;
        stubSec.name = ".text";
        stubSec.data = &generatedStubData;
        stubSec.virtualAddress = stubRva;
        if (!checkedSizeU32(
                generatedStubData.size(), "startup stub section size", err, stubSec.virtualSize))
            return false;
        stubSec.executable = true;
        stubSec.characteristics = sectionChars(true, false, true);
        sections.push_back(stubSec);
        entryRva = stubRva;
        uint32_t alignedStubSize = 0;
        if (!checkedAlignUpU32(generatedStubData.size(),
                               sectionAlignment,
                               "startup stub section size",
                               err,
                               alignedStubSize) ||
            !checkedAddU32(
                stubRva, alignedStubSize, "startup stub RVA range", err, nextGeneratedRva))
            return false;
    }

    std::vector<uint32_t> baseRelocRvas;
    for (const auto &entry : layout.rebaseEntries) {
        if (entry.sectionIndex >= layout.sections.size())
            continue;
        const auto &sec = layout.sections[entry.sectionIndex];
        if (!sec.alloc || entry.offset + 8 > sec.data.size())
            continue;
        if (sec.virtualAddr < imageBase)
            continue;
        if (entry.offset > std::numeric_limits<uint64_t>::max() - (sec.virtualAddr - imageBase))
            continue;
        const uint64_t rva64 = (sec.virtualAddr - imageBase) + entry.offset;
        if (rva64 <= UINT32_MAX)
            baseRelocRvas.push_back(static_cast<uint32_t>(rva64));
    }
    if (tlsLayout.directorySize != 0) {
        baseRelocRvas.push_back(tlsLayout.directoryRva + 0);
        baseRelocRvas.push_back(tlsLayout.directoryRva + 8);
        baseRelocRvas.push_back(tlsLayout.directoryRva + 16);
        baseRelocRvas.push_back(tlsLayout.directoryRva + 24);
    }

    generatedBaseRelocData =
        buildBaseRelocationBlocks(std::move(baseRelocRvas), arch == LinkArch::AArch64);
    if (!generatedBaseRelocData.empty()) {
        uint32_t relocRva = 0;
        if (!checkedAlignUpU32(
                nextGeneratedRva, sectionAlignment, "base relocation section RVA", err, relocRva))
            return false;

        PeSection relocSec;
        relocSec.name = ".reloc";
        relocSec.data = &generatedBaseRelocData;
        relocSec.virtualAddress = relocRva;
        if (!checkedSizeU32(generatedBaseRelocData.size(),
                            "base relocation section size",
                            err,
                            relocSec.virtualSize))
            return false;
        relocSec.characteristics = kRelocSectionChars;
        sections.push_back(relocSec);

        baseRelocLayout.data = generatedBaseRelocData;
        baseRelocLayout.directoryRva = relocRva;
        baseRelocLayout.directorySize = relocSec.virtualSize;
        uint32_t alignedRelocSize = 0;
        if (!checkedAlignUpU32(generatedBaseRelocData.size(),
                               sectionAlignment,
                               "base relocation section size",
                               err,
                               alignedRelocSize) ||
            !checkedAddU32(
                relocRva, alignedRelocSize, "base relocation section RVA range", err, nextGeneratedRva))
            return false;
    }

    uint32_t resourceRva = 0;
    if (!checkedAlignUpU32(
            nextGeneratedRva, sectionAlignment, "resource section RVA", err, resourceRva))
        return false;
    resourceLayout = buildDefaultManifestResource(resourceRva);
    generatedResourceData = resourceLayout.data;
    if (!generatedResourceData.empty()) {
        PeSection resourceSec;
        resourceSec.name = ".rsrc";
        resourceSec.data = &generatedResourceData;
        resourceSec.virtualAddress = resourceRva;
        if (!checkedSizeU32(
                generatedResourceData.size(), "resource section size", err, resourceSec.virtualSize))
            return false;
        resourceSec.characteristics = sectionChars(false, false, true);
        sections.push_back(resourceSec);

        uint32_t alignedResourceSize = 0;
        if (!checkedAlignUpU32(generatedResourceData.size(),
                               sectionAlignment,
                               "resource section size",
                               err,
                               alignedResourceSize) ||
            !checkedAddU32(
                resourceRva, alignedResourceSize, "resource section RVA range", err, nextGeneratedRva))
            return false;
    }

    std::stable_sort(sections.begin(), sections.end(), [](const PeSection &a, const PeSection &b) {
        if (a.alloc != b.alloc)
            return a.alloc > b.alloc;
        if (!a.alloc)
            return a.name < b.name;
        return a.virtualAddress < b.virtualAddress;
    });

    const ExceptionLayout exceptionLayout = findExceptionDirectory(sections);

    if (sections.size() > std::numeric_limits<uint16_t>::max()) {
        err << "error: PE section count exceeds 16-bit file format limit\n";
        return false;
    }
    const uint16_t numSections = static_cast<uint16_t>(sections.size());
    if (sections.size() > std::numeric_limits<size_t>::max() / 40) {
        err << "error: PE section table size overflows addressable size\n";
        return false;
    }
    const size_t sectionTableBytes = sections.size() * 40;
    if (sectionTableBytes > std::numeric_limits<size_t>::max() - (64 + 4 + 20 + 240)) {
        err << "error: PE header size overflows addressable size\n";
        return false;
    }
    const size_t headersWithoutPadding = 64 + 4 + 20 + 240 + sectionTableBytes;
    uint32_t sizeOfHeaders = 0;
    if (!checkedAlignUpU32(headersWithoutPadding, fileAlignment, "header size", err, sizeOfHeaders))
        return false;

    uint32_t currentFileOff = sizeOfHeaders;
    uint32_t sizeOfCode = 0;
    uint32_t sizeOfInitData = 0;
    uint32_t sizeOfUninitData = 0;
    uint32_t baseOfCode = 0;
    uint32_t sizeOfImage = sectionAlignment;

    for (auto &sec : sections) {
        if (sec.name.size() > 8) {
            err << "error: PE section name '" << sec.name << "' exceeds 8 bytes\n";
            return false;
        }
        if (sec.zeroFill) {
            sec.sizeOfRawData = 0;
        } else if (!checkedAlignUpU32(
                       sec.virtualSize, fileAlignment, "section raw size", err, sec.sizeOfRawData)) {
            return false;
        }
        sec.pointerToRawData = sec.sizeOfRawData == 0 ? 0 : currentFileOff;
        if (!checkedAddU32(
                currentFileOff, sec.sizeOfRawData, "file offset", err, currentFileOff))
            return false;

        if (sec.alloc) {
            uint32_t alignedVirtualSize = 0;
            if (!checkedAlignUpU32(sec.virtualSize,
                                   sectionAlignment,
                                   "section virtual size",
                                   err,
                                   alignedVirtualSize))
                return false;
            uint32_t secEnd = 0;
            if (!checkedAddU32(
                    sec.virtualAddress, alignedVirtualSize, "section RVA range", err, secEnd))
                return false;
            sizeOfImage = std::max(sizeOfImage, secEnd);
            if (sec.executable) {
                if (!checkedAddU32(
                        sizeOfCode, sec.sizeOfRawData, "SizeOfCode", err, sizeOfCode))
                    return false;
                if (baseOfCode == 0)
                    baseOfCode = sec.virtualAddress;
            } else if (sec.zeroFill) {
                if (!checkedAddU32(sizeOfUninitData,
                                   alignedVirtualSize,
                                   "SizeOfUninitializedData",
                                   err,
                                   sizeOfUninitData))
                    return false;
            } else {
                if (!checkedAddU32(
                        sizeOfInitData, sec.sizeOfRawData, "SizeOfInitializedData", err, sizeOfInitData))
                    return false;
            }
        }
    }

    std::vector<uint8_t> file;
    file.resize(64, 0);
    file[0] = 'M';
    file[1] = 'Z';
    putLE32(file, 0x3C, 64);

    uint16_t dllCharacteristics = kDllCharNXCompat | kDllCharTermServerAware;
    if (baseRelocLayout.directoryRva != 0)
        dllCharacteristics |= kDllCharDynamicBase | kDllCharHighEntropyVA;

    writeLE32(file, 0x00004550); // "PE\0\0"

    writeLE16(file, machine);
    writeLE16(file, numSections);
    writeLE32(file, 0);
    writeLE32(file, 0);
    writeLE32(file, 0);
    writeLE16(file, 240);
    writeLE16(file, 0x0022); // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE

    const size_t optHeaderStart = file.size();
    writeLE16(file, 0x020B);
    writeLE16(file, 0);
    writeLE32(file, sizeOfCode);
    writeLE32(file, sizeOfInitData);
    writeLE32(file, sizeOfUninitData);
    writeLE32(file, entryRva);
    writeLE32(file, baseOfCode);
    writeLE64(file, imageBase);
    writeLE32(file, sectionAlignment);
    writeLE32(file, fileAlignment);
    writeLE16(file, 6);
    writeLE16(file, 0);
    writeLE16(file, 0);
    writeLE16(file, 0);
    writeLE16(file, 6);
    writeLE16(file, 0);
    writeLE32(file, 0);
    writeLE32(file, sizeOfImage);
    writeLE32(file, sizeOfHeaders);
    writeLE32(file, 0);
    writeLE16(file, 3); // IMAGE_SUBSYSTEM_WINDOWS_CUI
    writeLE16(file, dllCharacteristics);
    writeLE64(file, stackReserve);
    writeLE64(file, stackCommit);
    writeLE64(file, 0x100000);
    writeLE64(file, 0x1000);
    writeLE32(file, 0);
    writeLE32(file, 16);

    for (int i = 0; i < 16; ++i) {
        writeLE32(file, 0);
        writeLE32(file, 0);
    }

    if (!imports.empty()) {
        putLE32(file, optHeaderStart + 112 + 1 * 8 + 0, importLayout.importDirRva);
        putLE32(file, optHeaderStart + 112 + 1 * 8 + 4, importLayout.importDirSize);
        putLE32(file, optHeaderStart + 112 + 12 * 8 + 0, importLayout.iatRva);
        putLE32(file, optHeaderStart + 112 + 12 * 8 + 4, importLayout.iatSize);
    }
    if (resourceLayout.directoryRva != 0) {
        putLE32(file, optHeaderStart + 112 + 2 * 8 + 0, resourceLayout.directoryRva);
        putLE32(file, optHeaderStart + 112 + 2 * 8 + 4, resourceLayout.directorySize);
    }
    if (exceptionLayout.directoryRva != 0) {
        putLE32(file, optHeaderStart + 112 + 3 * 8 + 0, exceptionLayout.directoryRva);
        putLE32(file, optHeaderStart + 112 + 3 * 8 + 4, exceptionLayout.directorySize);
    }
    if (baseRelocLayout.directoryRva != 0) {
        putLE32(file, optHeaderStart + 112 + 5 * 8 + 0, baseRelocLayout.directoryRva);
        putLE32(file, optHeaderStart + 112 + 5 * 8 + 4, baseRelocLayout.directorySize);
    }
    if (tlsLayout.directoryRva != 0) {
        putLE32(file, optHeaderStart + 112 + 9 * 8 + 0, tlsLayout.directoryRva);
        putLE32(file, optHeaderStart + 112 + 9 * 8 + 4, tlsLayout.directorySize);
    }

    for (const auto &sec : sections) {
        char secName[8] = {};
        std::strncpy(secName, sec.name.c_str(), sizeof(secName));
        file.insert(file.end(), secName, secName + sizeof(secName));

        writeLE32(file, sec.virtualSize);
        writeLE32(file, sec.virtualAddress);
        writeLE32(file, sec.sizeOfRawData);
        writeLE32(file, sec.pointerToRawData);
        writeLE32(file, 0);
        writeLE32(file, 0);
        writeLE16(file, 0);
        writeLE16(file, 0);
        writeLE32(file, sec.characteristics);
    }

    padTo(file, sizeOfHeaders);

    for (const auto &sec : sections) {
        if (sec.sizeOfRawData == 0)
            continue;
        padTo(file, sec.pointerToRawData);
        file.insert(file.end(), sec.data->begin(), sec.data->end());
        padTo(file, alignUp(file.size(), fileAlignment));
    }

    return writeBinaryFileAtomically(path, file, true, err);
}

} // namespace viper::codegen::linker
