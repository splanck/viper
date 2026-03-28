//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/IconGenerator.cpp
// Purpose: Generate ICNS, ICO, and multi-size PNG icons from a source image.
//
// Key invariants:
//   - ICNS: "icns" magic + big-endian total_size + type/size/PNG entries.
//   - ICO: ICONDIR header + ICONDIRENTRY array + PNG data blobs.
//   - All numeric fields are little-endian in ICO, big-endian in ICNS.
//   - Source image is never modified; resized copies are created for each size.
//
// Ownership/Lifetime:
//   - Pure functions returning byte vectors. No file I/O.
//
// Links: IconGenerator.hpp, PkgPNG.hpp
//
//===----------------------------------------------------------------------===//

#include "IconGenerator.hpp"

namespace viper::pkg {

//=============================================================================
// ICNS Generation
//=============================================================================

namespace {

/// @brief ICNS icon type codes and their target pixel sizes.
struct IcnsTypeEntry {
    char type[5];  ///< 4-char ICNS type code (NUL-terminated for convenience)
    uint32_t size; ///< Target pixel dimension (square)
};

// Modern ICNS types that embed PNG data directly.
// Each entry is a PNG at the specified pixel size.
static const IcnsTypeEntry kIcnsTypes[] = {
    {"ic11", 32},   // 32x32   (Retina 16)
    {"ic12", 64},   // 64x64   (Retina 32)
    {"ic07", 128},  // 128x128
    {"ic13", 256},  // 256x256 (Retina 128)
    {"ic08", 256},  // 256x256
    {"ic14", 512},  // 512x512 (Retina 256)
    {"ic09", 512},  // 512x512
    {"ic10", 1024}, // 1024x1024 (Retina 512)
};

void writeBE32(std::vector<uint8_t> &out, uint32_t val) {
    out.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(val & 0xFF));
}

void writeLE16(std::vector<uint8_t> &out, uint16_t val) {
    out.push_back(static_cast<uint8_t>(val & 0xFF));
    out.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
}

void writeLE32(std::vector<uint8_t> &out, uint32_t val) {
    out.push_back(static_cast<uint8_t>(val & 0xFF));
    out.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

/// @brief Standard icon sizes for Linux hicolor theme.
static const uint32_t kLinuxIconSizes[] = {16, 32, 48, 128, 256};

/// @brief Standard icon sizes for Windows ICO.
static const uint32_t kIcoSizes[] = {16, 24, 32, 48, 64, 128, 256};

} // namespace

std::vector<uint8_t> generateIcns(const PkgImage &srcImage) {
    std::vector<uint8_t> result;

    // ICNS header: magic "icns" (4 bytes) + total_size placeholder (4 bytes)
    result.push_back('i');
    result.push_back('c');
    result.push_back('n');
    result.push_back('s');
    // Reserve 4 bytes for total size — fill in at the end
    result.resize(8, 0);

    for (const auto &entry : kIcnsTypes) {
        // Skip sizes larger than source
        if (entry.size > srcImage.width && entry.size > srcImage.height)
            continue;

        // Resize to target size
        PkgImage resized = imageResize(srcImage, entry.size, entry.size);

        // Encode as PNG
        auto png = pngEncode(resized);

        // Write entry: type(4) + entry_size(4, big-endian) + PNG data
        // entry_size includes the 8-byte type+size header
        uint32_t entrySize = static_cast<uint32_t>(8 + png.size());

        result.push_back(static_cast<uint8_t>(entry.type[0]));
        result.push_back(static_cast<uint8_t>(entry.type[1]));
        result.push_back(static_cast<uint8_t>(entry.type[2]));
        result.push_back(static_cast<uint8_t>(entry.type[3]));

        writeBE32(result, entrySize);

        result.insert(result.end(), png.begin(), png.end());
    }

    // Fill in total size (big-endian) at offset 4
    uint32_t totalSize = static_cast<uint32_t>(result.size());
    result[4] = static_cast<uint8_t>((totalSize >> 24) & 0xFF);
    result[5] = static_cast<uint8_t>((totalSize >> 16) & 0xFF);
    result[6] = static_cast<uint8_t>((totalSize >> 8) & 0xFF);
    result[7] = static_cast<uint8_t>(totalSize & 0xFF);

    return result;
}

//=============================================================================
// ICO Generation
//=============================================================================

std::vector<uint8_t> generateIco(const PkgImage &srcImage) {
    // First, generate all the PNG blobs for sizes that fit
    struct IcoEntry {
        uint32_t size;
        std::vector<uint8_t> pngData;
    };

    std::vector<IcoEntry> entries;

    for (uint32_t sz : kIcoSizes) {
        if (sz > srcImage.width && sz > srcImage.height)
            continue;

        PkgImage resized = imageResize(srcImage, sz, sz);
        auto png = pngEncode(resized);
        entries.push_back({sz, std::move(png)});
    }

    if (entries.empty())
        return {};

    std::vector<uint8_t> result;

    // ICONDIR header (6 bytes):
    //   Reserved (2) = 0
    //   Type     (2) = 1 (ICO)
    //   Count    (2) = number of entries
    writeLE16(result, 0);                                     // Reserved
    writeLE16(result, 1);                                     // Type = ICO
    writeLE16(result, static_cast<uint16_t>(entries.size())); // Count

    // Calculate data offset: after ICONDIR (6) + all ICONDIRENTRYs (16 each)
    uint32_t dataOffset = 6 + static_cast<uint32_t>(entries.size()) * 16;

    // ICONDIRENTRY array (16 bytes each):
    //   Width      (1) — 0 means 256
    //   Height     (1) — 0 means 256
    //   ColorCount (1) = 0 (no palette)
    //   Reserved   (1) = 0
    //   Planes     (2) = 1
    //   BitCount   (2) = 32 (RGBA)
    //   SizeInBytes(4) = PNG data size
    //   FileOffset (4) = offset from file start to PNG data
    uint32_t currentOffset = dataOffset;
    for (const auto &e : entries) {
        // Width/Height: 0 encodes 256
        uint8_t w = (e.size >= 256) ? 0 : static_cast<uint8_t>(e.size);
        uint8_t h = w;
        result.push_back(w);                                        // Width
        result.push_back(h);                                        // Height
        result.push_back(0);                                        // ColorCount
        result.push_back(0);                                        // Reserved
        writeLE16(result, 1);                                       // Planes
        writeLE16(result, 32);                                      // BitCount (32bpp RGBA)
        writeLE32(result, static_cast<uint32_t>(e.pngData.size())); // Size
        writeLE32(result, currentOffset);                           // FileOffset

        currentOffset += static_cast<uint32_t>(e.pngData.size());
    }

    // PNG data blocks
    for (const auto &e : entries) {
        result.insert(result.end(), e.pngData.begin(), e.pngData.end());
    }

    return result;
}

//=============================================================================
// Multi-Size PNG Generation
//=============================================================================

std::map<uint32_t, std::vector<uint8_t>> generateMultiSizePngs(const PkgImage &srcImage) {
    std::map<uint32_t, std::vector<uint8_t>> result;

    for (uint32_t sz : kLinuxIconSizes) {
        if (sz > srcImage.width && sz > srcImage.height)
            continue;

        PkgImage resized = imageResize(srcImage, sz, sz);
        result[sz] = pngEncode(resized);
    }

    return result;
}

} // namespace viper::pkg
