//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PEBuilder.cpp
// Purpose: Emit PE32+ (x86-64) executables from scratch.
//
// Key invariants:
//   - DOS header at offset 0x0000, e_lfanew = 0x80.
//   - PE signature "PE\0\0" at offset 0x0080.
//   - COFF header at 0x0084, Optional header at 0x0098.
//   - Section headers start at 0x0188 (after 240-byte optional header).
//   - First section data at file offset 0x200 (or later, file-aligned).
//   - All sections virtually aligned to 0x1000.
//   - Import directory: ILT + IAT + HintName entries + DLL name strings.
//   - Resource directory: three-level tree (Type → Name → Language).
//
// Ownership/Lifetime:
//   - Pure function returning byte vector. No file I/O in buildPE.
//
// Links: PEBuilder.hpp, Microsoft PE Format specification
//
//===----------------------------------------------------------------------===//

#include "PEBuilder.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace viper::pkg {

namespace {

constexpr uint32_t kFileAlignment = 0x200;
constexpr uint32_t kSectionAlignment = 0x1000;
constexpr uint64_t kImageBase = 0x0000000140000000ULL;

// DOS header is 64 bytes, DOS stub is 64 bytes = 128 bytes (0x80)
constexpr uint32_t kPESignatureOffset = 0x80;

// PE sig(4) + COFF(20) + OptHdr(240) = 264 bytes
// Section headers start at 0x80 + 264 = 0x0188
constexpr uint32_t kCoffHeaderSize = 20;
constexpr uint32_t kOptionalHeaderSize = 240;
constexpr uint32_t kSectionHeaderSize = 40;
constexpr uint32_t kNumDataDirectories = 16;

/// @brief Round up to alignment boundary.
uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

/// @brief Write a little-endian uint16_t to a buffer at given offset.
void putLE16(std::vector<uint8_t> &buf, size_t offset, uint16_t val) {
    buf[offset + 0] = static_cast<uint8_t>(val & 0xFF);
    buf[offset + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
}

/// @brief Write a little-endian uint32_t to a buffer at given offset.
void putLE32(std::vector<uint8_t> &buf, size_t offset, uint32_t val) {
    buf[offset + 0] = static_cast<uint8_t>(val & 0xFF);
    buf[offset + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    buf[offset + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    buf[offset + 3] = static_cast<uint8_t>((val >> 24) & 0xFF);
}

/// @brief Write a little-endian uint64_t to a buffer at given offset.
void putLE64(std::vector<uint8_t> &buf, size_t offset, uint64_t val) {
    putLE32(buf, offset, static_cast<uint32_t>(val & 0xFFFFFFFF));
    putLE32(buf, offset + 4, static_cast<uint32_t>(val >> 32));
}

/// @brief Append a little-endian uint16_t.
void appendLE16(std::vector<uint8_t> &buf, uint16_t val) {
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
}

/// @brief Append a little-endian uint32_t.
void appendLE32(std::vector<uint8_t> &buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

/// @brief Pad buffer to alignment boundary with zeros.
void padTo(std::vector<uint8_t> &buf, uint32_t alignment) {
    size_t aligned = alignUp(static_cast<uint32_t>(buf.size()), alignment);
    buf.resize(aligned, 0);
}

/// @brief Section descriptor during layout.
struct SectionLayout {
    char name[8];
    uint32_t virtualSize;
    uint32_t virtualAddress;
    uint32_t rawDataSize;
    uint32_t rawDataOffset;
    uint32_t characteristics;
    std::vector<uint8_t> data;
};

/// @brief Build the import directory tables for the .rdata section.
///
/// Layout within .rdata:
///   1. Import Directory Table: (N+1) entries × 20 bytes (null-terminated)
///   2. Import Lookup Tables: per-DLL array of 8-byte entries (null-terminated)
///   3. Hint/Name entries: per-function Hint(2) + ASCII name + pad
///   4. DLL name strings: null-terminated ASCII
///   5. Import Address Tables: mirrors ILT layout
///
/// Returns: (rdataBytes, importDirRVA, importDirSize, iatRVA, iatSize)
struct ImportResult {
    std::vector<uint8_t> data;
    uint32_t importDirRVA;
    uint32_t importDirSize;
    uint32_t iatRVA;
    uint32_t iatSize;
};

ImportResult buildImportTables(const std::vector<PEImport> &imports, uint32_t rdataRVA) {
    ImportResult result{};
    if (imports.empty())
        return result;

    auto &buf = result.data;

    // Count total functions
    size_t totalFuncs = 0;
    for (const auto &imp : imports)
        totalFuncs += imp.functions.size();

    // Phase 1: Calculate layout sizes
    uint32_t idtSize = static_cast<uint32_t>((imports.size() + 1) * 20); // +1 null

    // ILT: per DLL, (numFuncs + 1) * 8 bytes (null-terminated array of 8-byte entries)
    uint32_t iltSize = 0;
    for (const auto &imp : imports)
        iltSize += static_cast<uint32_t>((imp.functions.size() + 1) * 8);

    // Hint/Name: per function, 2 + strlen + 1 + pad
    uint32_t hintNameSize = 0;
    for (const auto &imp : imports)
        for (const auto &fn : imp.functions)
            hintNameSize += alignUp(static_cast<uint32_t>(2 + fn.size() + 1), 2);

    // DLL name strings
    uint32_t dllNameSize = 0;
    for (const auto &imp : imports)
        dllNameSize += static_cast<uint32_t>(imp.dllName.size() + 1);

    // IAT: same layout as ILT
    uint32_t iatSize = iltSize;

    // Offsets within rdata section (all relative to section start)
    uint32_t idtOff = 0;
    uint32_t iltOff = idtOff + idtSize;
    uint32_t hintOff = iltOff + iltSize;
    uint32_t dllNameOff = hintOff + hintNameSize;
    uint32_t iatOff = dllNameOff + dllNameSize;
    // Align IAT to 8 bytes
    iatOff = alignUp(iatOff, 8);
    uint32_t totalSize = iatOff + iatSize;

    buf.resize(totalSize, 0);

    // Phase 2: Write ILT entries, Hint/Name entries, DLL name strings
    uint32_t curIltOff = iltOff;
    uint32_t curHintOff = hintOff;
    uint32_t curDllNameOff = dllNameOff;
    uint32_t curIatOff = iatOff;

    for (size_t i = 0; i < imports.size(); i++) {
        const auto &imp = imports[i];

        // IDT entry (20 bytes at idtOff + i*20)
        uint32_t idtEntryOff = idtOff + static_cast<uint32_t>(i) * 20;
        putLE32(buf, idtEntryOff + 0, rdataRVA + curIltOff);      // OriginalFirstThunk (ILT)
        putLE32(buf, idtEntryOff + 4, 0);                         // TimeDateStamp
        putLE32(buf, idtEntryOff + 8, 0);                         // ForwarderChain
        putLE32(buf, idtEntryOff + 12, rdataRVA + curDllNameOff); // Name RVA
        putLE32(buf, idtEntryOff + 16, rdataRVA + curIatOff);     // FirstThunk (IAT)

        // DLL name string
        std::memcpy(buf.data() + curDllNameOff, imp.dllName.c_str(), imp.dllName.size() + 1);
        curDllNameOff += static_cast<uint32_t>(imp.dllName.size() + 1);

        // ILT + IAT + Hint/Name entries for this DLL
        for (const auto &fn : imp.functions) {
            // Hint/Name entry at curHintOff
            uint32_t hintNameRVA = rdataRVA + curHintOff;
            putLE16(buf, curHintOff, 0); // Hint = 0 (lookup by name)
            std::memcpy(buf.data() + curHintOff + 2, fn.c_str(), fn.size() + 1);
            uint32_t entryLen = alignUp(static_cast<uint32_t>(2 + fn.size() + 1), 2);
            curHintOff += entryLen;

            // ILT entry (8 bytes): bit 63 = 0 (import by name), bits 30:0 = HintName RVA
            putLE32(buf, curIltOff, hintNameRVA);
            putLE32(buf, curIltOff + 4, 0);
            curIltOff += 8;

            // IAT entry (mirrors ILT before binding)
            putLE32(buf, curIatOff, hintNameRVA);
            putLE32(buf, curIatOff + 4, 0);
            curIatOff += 8;
        }

        // Null terminator for ILT and IAT
        curIltOff += 8; // already zeroed
        curIatOff += 8;
    }

    // IDT null terminator entry (20 zeros) — already zeroed

    result.importDirRVA = rdataRVA + idtOff;
    result.importDirSize = idtSize;
    result.iatRVA = rdataRVA + iatOff;
    result.iatSize = iatSize;

    return result;
}

/// @brief Build a .rsrc section with RT_MANIFEST and optional RT_ICON resources.
///
/// Resource Directory is a three-level tree: Type → Name/ID → Language.
/// Each level: Directory header (16 bytes) + N entries (8 bytes each).
/// Leaf entries point to Resource Data Entries (16 bytes) which hold RVA+size.
///
/// When iconData is provided, we parse the ICO header and embed:
///   - RT_ICON (type 3): one resource per icon image, IDs 1..N
///   - RT_GROUP_ICON (type 14): GRPICONDIR header linking to RT_ICONs
///   - RT_MANIFEST (type 24): UAC manifest XML
struct ResourceResult {
    std::vector<uint8_t> data;
    uint32_t rsrcRVA; // filled in by caller
    uint32_t rsrcSize;
};

/// @brief A single resource data item to embed.
struct ResItem {
    uint16_t typeId;           ///< RT_ICON=3, RT_GROUP_ICON=14, RT_MANIFEST=24
    uint16_t nameId;           ///< Resource name/ID within the type
    std::vector<uint8_t> data; ///< Raw resource data
};

/// @brief Parse ICO data into RT_ICON + RT_GROUP_ICON resource items.
void parseIcoToResources(const std::vector<uint8_t> &ico, std::vector<ResItem> &items) {
    if (ico.size() < 6)
        return;

    // ICONDIR: reserved(2) + type(2) + count(2)
    uint16_t count = static_cast<uint16_t>(ico[4] | (ico[5] << 8));
    if (count == 0 || ico.size() < 6u + count * 16u)
        return;

    // Build GRPICONDIR: same as ICONDIR but entries use nID instead of dwImageOffset
    // GRPICONDIR = ICONDIR(6) + GRPICONDIRENTRY[count](14 each)
    std::vector<uint8_t> grpIcon;
    // Copy ICONDIR header (6 bytes)
    grpIcon.insert(grpIcon.end(), ico.begin(), ico.begin() + 6);

    for (uint16_t i = 0; i < count; i++) {
        size_t entryOff = 6 + i * 16;
        // ICONDIRENTRY: w(1) h(1) colorCount(1) reserved(1) planes(2) bitCount(2)
        //               sizeInBytes(4) fileOffset(4) = 16 bytes
        uint32_t imgSize =
            static_cast<uint32_t>(ico[entryOff + 8] | (ico[entryOff + 9] << 8) |
                                  (ico[entryOff + 10] << 16) | (ico[entryOff + 11] << 24));
        uint32_t imgOffset =
            static_cast<uint32_t>(ico[entryOff + 12] | (ico[entryOff + 13] << 8) |
                                  (ico[entryOff + 14] << 16) | (ico[entryOff + 15] << 24));

        // GRPICONDIRENTRY: same first 12 bytes, but last 2 bytes = nID (uint16)
        // instead of last 4 bytes = dwImageOffset
        grpIcon.insert(grpIcon.end(), ico.begin() + entryOff, ico.begin() + entryOff + 12);
        uint16_t iconId = static_cast<uint16_t>(i + 1);
        grpIcon.push_back(static_cast<uint8_t>(iconId & 0xFF));
        grpIcon.push_back(static_cast<uint8_t>((iconId >> 8) & 0xFF));

        // Extract the actual icon image data
        if (imgOffset + imgSize <= ico.size()) {
            ResItem icon;
            icon.typeId = 3; // RT_ICON
            icon.nameId = iconId;
            icon.data.assign(ico.begin() + imgOffset, ico.begin() + imgOffset + imgSize);
            items.push_back(std::move(icon));
        }
    }

    // Add RT_GROUP_ICON resource
    ResItem grp;
    grp.typeId = 14; // RT_GROUP_ICON
    grp.nameId = 1;
    grp.data = std::move(grpIcon);
    items.push_back(std::move(grp));
}

ResourceResult buildResourceSection(const std::string &manifest,
                                    const std::vector<uint8_t> &iconData,
                                    uint32_t rsrcRVA) {
    ResourceResult result{};

    // Collect all resource items
    std::vector<ResItem> items;

    if (!iconData.empty())
        parseIcoToResources(iconData, items);

    if (!manifest.empty()) {
        ResItem man;
        man.typeId = 24; // RT_MANIFEST
        man.nameId = 1;
        man.data.assign(manifest.begin(), manifest.end());
        items.push_back(std::move(man));
    }

    if (items.empty())
        return result;

    // Group items by type
    struct TypeGroup {
        uint16_t typeId;
        std::vector<size_t> itemIndices; // indices into items[]
    };

    std::vector<TypeGroup> types;
    for (size_t i = 0; i < items.size(); i++) {
        bool found = false;
        for (auto &tg : types) {
            if (tg.typeId == items[i].typeId) {
                tg.itemIndices.push_back(i);
                found = true;
                break;
            }
        }
        if (!found)
            types.push_back({items[i].typeId, {i}});
    }

    // Sort types by ID (Windows requires sorted entries)
    std::sort(types.begin(), types.end(), [](const TypeGroup &a, const TypeGroup &b) {
        return a.typeId < b.typeId;
    });

    // Calculate layout:
    // Level 1 (Type dir):    16 + types.size()*8
    // Level 2 (Name dirs):   per type: 16 + numItems*8
    // Level 3 (Lang dirs):   per item: 16 + 1*8
    // Data entries:          per item: 16
    // Data blobs:            per item: alignUp(size, 4)

    uint32_t numTypes = static_cast<uint32_t>(types.size());
    uint32_t totalItems = static_cast<uint32_t>(items.size());

    uint32_t typeDirSize = 16 + numTypes * 8;
    uint32_t nameDirsSize = 0;
    for (const auto &tg : types)
        nameDirsSize += 16 + static_cast<uint32_t>(tg.itemIndices.size()) * 8;
    uint32_t langDirsSize = totalItems * (16 + 8); // one lang dir per item
    uint32_t dataEntriesSize = totalItems * 16;
    uint32_t dirTreeSize = typeDirSize + nameDirsSize + langDirsSize + dataEntriesSize;

    // Calculate total data size
    uint32_t dataSize = 0;
    for (const auto &item : items)
        dataSize += alignUp(static_cast<uint32_t>(item.data.size()), 4);

    uint32_t totalSize = dirTreeSize + dataSize;
    auto &buf = result.data;
    buf.resize(totalSize, 0);

    // ─── Write directory tree ──────────────────────────────────────────

    uint32_t off = 0; // current write offset

    // Level 1: Type directory
    uint32_t typeDirOff = off;
    putLE16(buf, typeDirOff + 14, static_cast<uint16_t>(numTypes));
    off += 16;

    // Reserve space for type entries (filled below after we know name dir offsets)
    uint32_t typeEntriesOff = off;
    off += numTypes * 8;

    // Level 2: Name directories (one per type)
    std::vector<uint32_t> nameDirOffsets(numTypes);
    std::vector<std::vector<uint32_t>> langDirOffsetsPerType(numTypes);

    for (uint32_t t = 0; t < numTypes; t++) {
        nameDirOffsets[t] = off;
        uint32_t numNames = static_cast<uint32_t>(types[t].itemIndices.size());
        putLE16(buf, off + 14, static_cast<uint16_t>(numNames));
        off += 16;

        // Reserve space for name entries
        langDirOffsetsPerType[t].resize(numNames);
        off += numNames * 8;
    }

    // Level 3: Language directories (one per item)
    for (uint32_t t = 0; t < numTypes; t++) {
        for (uint32_t n = 0; n < types[t].itemIndices.size(); n++) {
            langDirOffsetsPerType[t][n] = off;
            putLE16(buf, off + 14, 1);      // 1 language entry
            putLE32(buf, off + 16, 0x0409); // en-US
            off += 16 + 8;                  // dir header + 1 entry
        }
    }

    // Data entries (one per item)
    uint32_t dataEntryBaseOff = off;
    off += totalItems * 16;

    // Data blobs
    uint32_t dataBlobOff = off;

    // ─── Fill in offsets ───────────────────────────────────────────────

    // Type entries → name directories
    for (uint32_t t = 0; t < numTypes; t++) {
        uint32_t entryOff = typeEntriesOff + t * 8;
        putLE32(buf, entryOff, types[t].typeId);
        putLE32(buf, entryOff + 4, nameDirOffsets[t] | 0x80000000);
    }

    // Name entries → language directories
    uint32_t itemIdx = 0;
    for (uint32_t t = 0; t < numTypes; t++) {
        uint32_t nameEntryOff = nameDirOffsets[t] + 16;
        for (uint32_t n = 0; n < types[t].itemIndices.size(); n++) {
            size_t ii = types[t].itemIndices[n];
            putLE32(buf, nameEntryOff, items[ii].nameId);
            putLE32(buf, nameEntryOff + 4, langDirOffsetsPerType[t][n] | 0x80000000);
            nameEntryOff += 8;
        }

        // Lang entries → data entries
        for (uint32_t n = 0; n < types[t].itemIndices.size(); n++) {
            uint32_t langEntryOff = langDirOffsetsPerType[t][n] + 16 + 4;
            // Point to data entry (no high bit = leaf)
            putLE32(buf, langEntryOff, dataEntryBaseOff + itemIdx * 16);
            itemIdx++;
        }
    }

    // Data entries → data blobs
    uint32_t curBlobOff = dataBlobOff;
    for (uint32_t i = 0; i < totalItems; i++) {
        // Resolve flat index: iterate types in order
        size_t flatIdx = 0;
        for (uint32_t t = 0; t < numTypes; t++) {
            for (size_t n = 0; n < types[t].itemIndices.size(); n++) {
                if (flatIdx == i) {
                    size_t ii = types[t].itemIndices[n];
                    uint32_t deOff = dataEntryBaseOff + i * 16;
                    putLE32(buf, deOff + 0, rsrcRVA + curBlobOff); // RVA
                    putLE32(buf, deOff + 4, static_cast<uint32_t>(items[ii].data.size()));
                    // Copy data
                    std::memcpy(
                        buf.data() + curBlobOff, items[ii].data.data(), items[ii].data.size());
                    curBlobOff += alignUp(static_cast<uint32_t>(items[ii].data.size()), 4);
                    goto nextItem;
                }
                flatIdx++;
            }
        }
    nextItem:;
    }

    result.rsrcSize = totalSize;
    return result;
}

} // namespace

std::vector<uint8_t> buildPE(const PEBuildParams &params) {
    // ─── Section planning ──────────────────────────────────────────────

    // Count sections: .text is required; .rdata if imports; .rsrc if manifest
    uint32_t numSections = 1; // .text
    bool hasRdata = !params.imports.empty() || !params.rdataSection.empty();
    bool hasRsrc = !params.manifest.empty() || !params.iconData.empty();
    if (hasRdata)
        numSections++;
    if (hasRsrc)
        numSections++;

    // Headers size: DOS(0x80) + PE sig(4) + COFF(20) + OptHdr(240) + Sections(40*N)
    uint32_t headersRaw = kPESignatureOffset + 4 + kCoffHeaderSize + kOptionalHeaderSize +
                          kSectionHeaderSize * numSections;
    uint32_t headersAligned = alignUp(headersRaw, kFileAlignment);
    uint32_t headersVirtual = alignUp(headersRaw, kSectionAlignment);

    // ─── Layout sections ───────────────────────────────────────────────

    std::vector<SectionLayout> sections;

    // .text section
    {
        SectionLayout s{};
        std::memcpy(s.name, ".text\0\0\0", 8);
        s.data = params.textSection;
        s.virtualSize = static_cast<uint32_t>(s.data.size());
        s.rawDataSize = alignUp(s.virtualSize, kFileAlignment);
        s.characteristics = 0x60000020; // CODE | EXECUTE | READ
        sections.push_back(std::move(s));
    }

    // .rdata section (import tables)
    uint32_t rdataIdx = 0;
    ImportResult importResult{};
    if (hasRdata) {
        rdataIdx = static_cast<uint32_t>(sections.size());
        SectionLayout s{};
        std::memcpy(s.name, ".rdata\0\0", 8);

        // Calculate RVA for .rdata section
        uint32_t rdataVA = headersVirtual;
        for (const auto &sec : sections)
            rdataVA += alignUp(sec.virtualSize, kSectionAlignment);

        if (!params.imports.empty()) {
            importResult = buildImportTables(params.imports, rdataVA);
            s.data = importResult.data;
        }
        if (!params.rdataSection.empty()) {
            s.data.insert(
                s.data.end(), params.rdataSection.begin(), params.rdataSection.end());
        }
        s.virtualSize = static_cast<uint32_t>(s.data.size());
        s.rawDataSize = alignUp(s.virtualSize, kFileAlignment);
        s.characteristics = 0x40000040; // INITIALIZED_DATA | READ
        sections.push_back(std::move(s));
    }

    // .rsrc section (manifest)
    uint32_t rsrcIdx = 0;
    ResourceResult rsrcResult{};
    if (hasRsrc) {
        rsrcIdx = static_cast<uint32_t>(sections.size());
        SectionLayout s{};
        std::memcpy(s.name, ".rsrc\0\0\0", 8);

        // Calculate RVA for .rsrc section
        uint32_t rsrcVA = headersVirtual;
        for (const auto &sec : sections)
            rsrcVA += alignUp(sec.virtualSize, kSectionAlignment);

        rsrcResult = buildResourceSection(params.manifest, params.iconData, rsrcVA);
        s.data = rsrcResult.data;
        s.virtualSize = static_cast<uint32_t>(s.data.size());
        s.rawDataSize = alignUp(s.virtualSize, kFileAlignment);
        s.characteristics = 0x40000040; // INITIALIZED_DATA | READ
        sections.push_back(std::move(s));
    }

    // Assign virtual addresses and file offsets
    uint32_t nextVA = headersVirtual;
    uint32_t nextFileOff = headersAligned;
    for (auto &s : sections) {
        s.virtualAddress = nextVA;
        s.rawDataOffset = nextFileOff;
        nextVA += alignUp(s.virtualSize, kSectionAlignment);
        nextFileOff += s.rawDataSize;
    }

    uint32_t sizeOfImage = nextVA;

    // ─── Build PE buffer ───────────────────────────────────────────────

    std::vector<uint8_t> pe;
    pe.resize(nextFileOff, 0);

    // ─── DOS Header (64 bytes at offset 0) ─────────────────────────────

    putLE16(pe, 0x00, 0x5A4D);             // e_magic = "MZ"
    putLE32(pe, 0x3C, kPESignatureOffset); // e_lfanew

    // DOS stub message (at offset 0x40, 64 bytes)
    const char *dosStub = "This program cannot be run in DOS mode.\r\n$";
    std::memcpy(pe.data() + 0x40, dosStub, std::strlen(dosStub));

    // ─── PE Signature (4 bytes at 0x80) ────────────────────────────────

    putLE32(pe, kPESignatureOffset, 0x00004550); // "PE\0\0"

    // ─── COFF Header (20 bytes at 0x84) ────────────────────────────────

    uint32_t coffOff = kPESignatureOffset + 4;
    uint16_t machineType =
        (params.arch == "arm64") ? static_cast<uint16_t>(0xAA64) : static_cast<uint16_t>(0x8664);
    putLE16(pe, coffOff + 0, machineType); // Machine = AMD64 or ARM64
    putLE16(pe, coffOff + 2, static_cast<uint16_t>(numSections));
    putLE32(pe, coffOff + 4, 0);                    // TimeDateStamp
    putLE32(pe, coffOff + 8, 0);                    // PointerToSymbolTable
    putLE32(pe, coffOff + 12, 0);                   // NumberOfSymbols
    putLE16(pe, coffOff + 16, kOptionalHeaderSize); // SizeOfOptionalHeader
    putLE16(pe, coffOff + 18, 0x0022);              // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE

    // ─── Optional Header PE32+ (240 bytes at 0x98) ─────────────────────

    uint32_t optOff = coffOff + kCoffHeaderSize;

    putLE16(pe, optOff + 0, 0x020B); // Magic = PE32+
    pe[optOff + 2] = 0x0E;           // MajorLinkerVersion
    pe[optOff + 3] = 0x00;           // MinorLinkerVersion

    // SizeOfCode = sum of .text section raw sizes
    uint32_t sizeOfCode = sections[0].rawDataSize;
    putLE32(pe, optOff + 4, sizeOfCode);

    // SizeOfInitializedData
    uint32_t sizeOfInitData = 0;
    for (size_t i = 1; i < sections.size(); i++)
        sizeOfInitData += sections[i].rawDataSize;
    putLE32(pe, optOff + 8, sizeOfInitData);

    putLE32(pe, optOff + 12, 0); // SizeOfUninitializedData

    // AddressOfEntryPoint (RVA)
    putLE32(pe, optOff + 16, sections[0].virtualAddress + params.entryPointOffset);

    putLE32(pe, optOff + 20, sections[0].virtualAddress); // BaseOfCode

    putLE64(pe, optOff + 24, kImageBase);        // ImageBase
    putLE32(pe, optOff + 32, kSectionAlignment); // SectionAlignment
    putLE32(pe, optOff + 36, kFileAlignment);    // FileAlignment

    putLE16(pe, optOff + 40, 6); // MajorOperatingSystemVersion
    putLE16(pe, optOff + 42, 0); // MinorOperatingSystemVersion
    putLE16(pe, optOff + 44, 0); // MajorImageVersion
    putLE16(pe, optOff + 46, 0); // MinorImageVersion
    putLE16(pe, optOff + 48, 6); // MajorSubsystemVersion
    putLE16(pe, optOff + 50, 0); // MinorSubsystemVersion

    putLE32(pe, optOff + 52, 0);              // Win32VersionValue
    putLE32(pe, optOff + 56, sizeOfImage);    // SizeOfImage
    putLE32(pe, optOff + 60, headersAligned); // SizeOfHeaders
    putLE32(pe, optOff + 64, 0);              // CheckSum (not required for EXE)

    putLE16(pe, optOff + 68, params.subsystem);
    putLE16(pe, optOff + 70, params.dllCharacteristics);

    putLE64(pe, optOff + 72, params.stackReserve);
    putLE64(pe, optOff + 80, params.stackCommit);
    putLE64(pe, optOff + 88, params.heapReserve);
    putLE64(pe, optOff + 96, params.heapCommit);

    putLE32(pe, optOff + 104, 0);                   // LoaderFlags
    putLE32(pe, optOff + 108, kNumDataDirectories); // NumberOfRvaAndSizes

    // Data Directories (16 × 8 = 128 bytes starting at optOff + 112)
    uint32_t ddOff = optOff + 112;
    // [0] Export = 0,0 (already zeroed)
    // [1] Import Directory
    if (hasRdata && !params.imports.empty()) {
        putLE32(pe, ddOff + 1 * 8 + 0, importResult.importDirRVA);
        putLE32(pe, ddOff + 1 * 8 + 4, importResult.importDirSize);
    }
    // [2] Resource Table
    if (hasRsrc) {
        putLE32(pe, ddOff + 2 * 8 + 0, sections[rsrcIdx].virtualAddress);
        putLE32(pe, ddOff + 2 * 8 + 4, rsrcResult.rsrcSize);
    }
    // [12] IAT
    if (hasRdata && !params.imports.empty()) {
        putLE32(pe, ddOff + 12 * 8 + 0, importResult.iatRVA);
        putLE32(pe, ddOff + 12 * 8 + 4, importResult.iatSize);
    }

    // ─── Section Headers ───────────────────────────────────────────────

    uint32_t secHdrOff = optOff + kOptionalHeaderSize;
    for (size_t i = 0; i < sections.size(); i++) {
        const auto &s = sections[i];
        uint32_t off = secHdrOff + static_cast<uint32_t>(i) * kSectionHeaderSize;

        std::memcpy(pe.data() + off, s.name, 8);
        putLE32(pe, off + 8, s.virtualSize);     // VirtualSize
        putLE32(pe, off + 12, s.virtualAddress); // VirtualAddress
        putLE32(pe, off + 16, s.rawDataSize);    // SizeOfRawData
        putLE32(pe, off + 20, s.rawDataOffset);  // PointerToRawData
        putLE32(pe, off + 24, 0);                // PointerToRelocations
        putLE32(pe, off + 28, 0);                // PointerToLinenumbers
        putLE16(pe, off + 32, 0);                // NumberOfRelocations
        putLE16(pe, off + 34, 0);                // NumberOfLinenumbers
        putLE32(pe, off + 36, s.characteristics);
    }

    // ─── Section Data ──────────────────────────────────────────────────

    for (const auto &s : sections) {
        if (!s.data.empty()) {
            std::memcpy(pe.data() + s.rawDataOffset, s.data.data(), s.data.size());
        }
    }

    // ─── Overlay ───────────────────────────────────────────────────────

    if (!params.overlay.empty()) {
        pe.insert(pe.end(), params.overlay.begin(), params.overlay.end());
    }

    return pe;
}

void writePEToFile(const std::vector<uint8_t> &pe, const std::string &path) {
    std::ofstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot write PE: " + path);
    f.write(reinterpret_cast<const char *>(pe.data()), static_cast<std::streamsize>(pe.size()));
    if (!f)
        throw std::runtime_error("failed to write PE: " + path);
}

std::string generateUacManifest() {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
           "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" "
           "manifestVersion=\"1.0\">\n"
           "  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">\n"
           "    <security><requestedPrivileges>\n"
           "      <requestedExecutionLevel level=\"requireAdministrator\" "
           "uiAccess=\"false\"/>\n"
           "    </requestedPrivileges></security>\n"
           "  </trustInfo>\n"
           "</assembly>\n";
}

std::string generateAsInvokerManifest() {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
           "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" "
           "manifestVersion=\"1.0\">\n"
           "  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">\n"
           "    <security><requestedPrivileges>\n"
           "      <requestedExecutionLevel level=\"asInvoker\" "
           "uiAccess=\"false\"/>\n"
           "    </requestedPrivileges></security>\n"
           "  </trustInfo>\n"
           "</assembly>\n";
}

} // namespace viper::pkg
