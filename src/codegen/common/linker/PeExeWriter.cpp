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
#include <fstream>
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

static constexpr uint16_t kDllCharNXCompat = 0x0100;
static constexpr uint16_t kDllCharTermServerAware = 0x8000;
static constexpr uint16_t kDllCharacteristics = kDllCharNXCompat | kDllCharTermServerAware;

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
};

void putLE16(std::vector<uint8_t> &buf, size_t offset, uint16_t val) {
    buf[offset + 0] = static_cast<uint8_t>(val & 0xFF);
    buf[offset + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
}

void putLE32(std::vector<uint8_t> &buf, size_t offset, uint32_t val) {
    buf[offset + 0] = static_cast<uint8_t>(val & 0xFF);
    buf[offset + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    buf[offset + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    buf[offset + 3] = static_cast<uint8_t>((val >> 24) & 0xFF);
}

void putLE64(std::vector<uint8_t> &buf, size_t offset, uint64_t val) {
    putLE32(buf, offset, static_cast<uint32_t>(val & 0xFFFFFFFFULL));
    putLE32(buf, offset + 4, static_cast<uint32_t>(val >> 32));
}

uint32_t sectionChars(bool executable, bool writable, bool alloc) {
    if (!alloc)
        return 0x42000040; // CNT_INITIALIZED_DATA | MEM_DISCARDABLE | MEM_READ
    if (executable)
        return 0x60000020; // CNT_CODE | MEM_EXECUTE | MEM_READ
    if (writable)
        return 0xC0000040; // CNT_INITIALIZED_DATA | MEM_READ | MEM_WRITE
    return 0x40000040;     // CNT_INITIALIZED_DATA | MEM_READ
}

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

bool fitsInt32(int64_t value) {
    return value >= static_cast<int64_t>(INT32_MIN) && value <= static_cast<int64_t>(INT32_MAX);
}

bool hasPrefix(const std::string &value, const char *prefix) {
    return value.rfind(prefix, 0) == 0;
}

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

bool layoutHasTls(const LinkLayout &layout) {
    for (const auto &sec : layout.sections) {
        if (sec.alloc && sec.tls && !sec.data.empty())
            return true;
    }
    return false;
}

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
    if (!sec.alloc)
        return ".debug";
    if (sec.tls)
        return ".tls";
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
    uint32_t tlsEndRva = 0;
    uint32_t tlsAlignment = 1;
    for (const auto &sec : layout.sections) {
        if (!sec.alloc || !sec.tls || sec.data.empty())
            continue;
        const uint32_t secRva = static_cast<uint32_t>(sec.virtualAddr - imageBase);
        tlsStartRva = std::min(tlsStartRva, secRva);
        tlsEndRva = std::max(tlsEndRva, secRva + static_cast<uint32_t>(sec.data.size()));
        tlsAlignment = std::max(tlsAlignment, sec.alignment);
    }

    if (tlsStartRva == UINT32_MAX)
        return tls;

    const auto tlsIndexIt = layout.globalSyms.find("_tls_index");
    if (tlsIndexIt == layout.globalSyms.end() || tlsIndexIt->second.resolvedAddr == 0) {
        err << "error: PE TLS image is missing a resolved _tls_index symbol\n";
        return {};
    }

    tls.data.resize(48, 0);
    putLE64(tls.data, 0, imageBase + tlsStartRva);
    putLE64(tls.data, 8, imageBase + tlsEndRva);
    putLE64(tls.data, 16, tlsIndexIt->second.resolvedAddr);
    putLE64(tls.data, 24, imageBase + sectionRva + 40); // Null-terminated callback array.
    putLE32(tls.data, 32, 0); // Zero-fill is already materialized in the template.
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
    const uint64_t imageBase = 0x140000000ULL;
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
    sections.reserve(layout.sections.size() + 2);

    uint32_t nextGeneratedRva = sectionAlignment;
    for (const auto &sec : layout.sections) {
        if (sec.data.empty())
            continue;

        PeSection ps;
        ps.name = sectionNameFor(sec);
        ownedSectionData.push_back(sec.data);
        ps.data = &ownedSectionData.back();
        ps.alloc = sec.alloc;
        ps.executable = sec.executable;
        ps.writable = sec.writable;
        ps.virtualAddress = sec.alloc ? static_cast<uint32_t>(sec.virtualAddr - imageBase) : 0;
        ps.virtualSize = static_cast<uint32_t>(ownedSectionData.back().size());
        ps.characteristics = sectionChars(sec.executable, sec.writable, sec.alloc);
        sections.push_back(ps);

        if (sec.alloc) {
            const uint32_t end = ps.virtualAddress +
                                 static_cast<uint32_t>(alignUp(ps.virtualSize, sectionAlignment));
            nextGeneratedRva = std::max(nextGeneratedRva, end);
        }
    }

    std::vector<uint8_t> generatedImportData;
    std::vector<uint8_t> generatedStubData;
    std::vector<uint8_t> generatedTlsData;
    ImportLayout importLayout{};
    TlsLayout tlsLayout{};

    if (!imports.empty()) {
        const uint32_t importRva =
            static_cast<uint32_t>(alignUp(nextGeneratedRva, sectionAlignment));
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
        importSec.virtualSize = static_cast<uint32_t>(generatedImportData.size());
        importSec.characteristics = sectionChars(false, false, true);
        sections.push_back(importSec);
        nextGeneratedRva = importRva + static_cast<uint32_t>(
                                           alignUp(generatedImportData.size(), sectionAlignment));
    }

    const bool haveTls = layoutHasTls(layout);
    const uint32_t tlsDirRva = static_cast<uint32_t>(alignUp(nextGeneratedRva, sectionAlignment));
    tlsLayout = buildTlsDirectory(imageBase, layout, tlsDirRva, err);
    if (haveTls && tlsLayout.directorySize == 0)
        return false;
    if (tlsLayout.directorySize != 0) {
        generatedTlsData = tlsLayout.data;

        PeSection tlsMetaSec;
        tlsMetaSec.name = ".rdata";
        tlsMetaSec.data = &generatedTlsData;
        tlsMetaSec.virtualAddress = tlsDirRva;
        tlsMetaSec.virtualSize = static_cast<uint32_t>(generatedTlsData.size());
        tlsMetaSec.characteristics = sectionChars(false, false, true);
        sections.push_back(tlsMetaSec);
        nextGeneratedRva =
            tlsDirRva + static_cast<uint32_t>(alignUp(generatedTlsData.size(), sectionAlignment));
    }

    uint32_t entryRva = static_cast<uint32_t>(layout.entryAddr - imageBase);
    if (emitStartupStub && !imports.empty()) {
        const auto exitIt = importLayout.slotRvas.find("ExitProcess");
        if (exitIt == importLayout.slotRvas.end()) {
            err << "error: PE writer requires ExitProcess for startup stub\n";
            return false;
        }

        const uint32_t stubRva = static_cast<uint32_t>(alignUp(nextGeneratedRva, sectionAlignment));
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
        stubSec.virtualSize = static_cast<uint32_t>(generatedStubData.size());
        stubSec.executable = true;
        stubSec.characteristics = sectionChars(true, false, true);
        sections.push_back(stubSec);
        entryRva = stubRva;
    }

    std::stable_sort(sections.begin(), sections.end(), [](const PeSection &a, const PeSection &b) {
        if (a.alloc != b.alloc)
            return a.alloc > b.alloc;
        if (!a.alloc)
            return a.name < b.name;
        return a.virtualAddress < b.virtualAddress;
    });

    const ExceptionLayout exceptionLayout = findExceptionDirectory(sections);

    const uint16_t numSections = static_cast<uint16_t>(sections.size());
    const size_t headersWithoutPadding = 64 + 4 + 20 + 240 + numSections * 40;
    const uint32_t sizeOfHeaders =
        static_cast<uint32_t>(alignUp(headersWithoutPadding, fileAlignment));

    uint32_t currentFileOff = sizeOfHeaders;
    uint32_t sizeOfCode = 0;
    uint32_t sizeOfInitData = 0;
    uint32_t baseOfCode = 0;
    uint32_t sizeOfImage = sectionAlignment;

    for (auto &sec : sections) {
        sec.sizeOfRawData = static_cast<uint32_t>(alignUp(sec.virtualSize, fileAlignment));
        sec.pointerToRawData = currentFileOff;
        currentFileOff += sec.sizeOfRawData;

        if (sec.alloc) {
            const uint32_t secEnd =
                sec.virtualAddress +
                static_cast<uint32_t>(alignUp(sec.virtualSize, sectionAlignment));
            sizeOfImage = std::max(sizeOfImage, secEnd);
            if (sec.executable) {
                sizeOfCode += sec.sizeOfRawData;
                if (baseOfCode == 0)
                    baseOfCode = sec.virtualAddress;
            } else {
                sizeOfInitData += sec.sizeOfRawData;
            }
        }
    }

    std::vector<uint8_t> file;
    file.resize(64, 0);
    file[0] = 'M';
    file[1] = 'Z';
    putLE32(file, 0x3C, 64);

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
    writeLE32(file, 0);
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
    writeLE16(file, kDllCharacteristics);
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
    if (exceptionLayout.directoryRva != 0) {
        putLE32(file, optHeaderStart + 112 + 3 * 8 + 0, exceptionLayout.directoryRva);
        putLE32(file, optHeaderStart + 112 + 3 * 8 + 4, exceptionLayout.directorySize);
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
        padTo(file, sec.pointerToRawData);
        file.insert(file.end(), sec.data->begin(), sec.data->end());
        padTo(file, alignUp(file.size(), fileAlignment));
    }

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        err << "error: cannot open '" << path << "' for writing\n";
        return false;
    }
    f.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!f) {
        err << "error: write failed to '" << path << "'\n";
        return false;
    }

    return true;
}

} // namespace viper::codegen::linker
