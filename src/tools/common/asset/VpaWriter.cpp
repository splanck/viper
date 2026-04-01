//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/asset/VpaWriter.cpp
// Purpose: Implementation of VPA (Viper Pack Archive) writer. Serializes asset
//          entries into the VPA binary format with optional DEFLATE compression.
//
// Key invariants:
//   - Header is always 32 bytes.
//   - Data entries are 8-byte aligned.
//   - TOC written after all data; header patched with TOC offset.
//   - Pre-compressed formats are never double-compressed.
//
// Ownership/Lifetime:
//   - All output is returned as std::vector or written to file.
//   - No internal state is modified by write methods (const).
//
// Links: VpaWriter.hpp (API), PkgDeflate.hpp (compression)
//
//===----------------------------------------------------------------------===//

#include "VpaWriter.hpp"

#include "PkgDeflate.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace viper::asset {

// ─── VPA format constants ───────────────────────────────────────────────────

static constexpr uint8_t kMagic[4] = {'V', 'P', 'A', '1'};
static constexpr uint16_t kVersion = 1;
static constexpr size_t kHeaderSize = 32;
static constexpr size_t kAlignment = 8;

// ─── Pre-compressed extension skip list ─────────────────────────────────────

bool VpaWriter::isPreCompressed(const std::string &name) {
    auto dot = name.rfind('.');
    if (dot == std::string::npos)
        return false;

    std::string ext = name.substr(dot);
    // Lowercase the extension for comparison.
    for (auto &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Formats that are already compressed — DEFLATE would make them larger.
    static const char *skip[] = {
        ".png", ".jpg",  ".jpeg", ".gif",  ".ogg", ".mp3",  ".vaf",
        ".glb", ".gz",   ".zip",  ".vpa",  ".zst", ".br",   ".webp",
    };
    for (const char *s : skip) {
        if (ext == s)
            return true;
    }
    return false;
}

// ─── addEntry ───────────────────────────────────────────────────────────────

void VpaWriter::addEntry(const std::string &name, const uint8_t *data, size_t size, bool compress) {
    Entry entry;
    entry.name = name;
    entry.originalSize = size;
    entry.compressed = false;

    bool shouldCompress = compress && size > 0 && !isPreCompressed(name);

    if (shouldCompress) {
        try {
            auto deflated = viper::pkg::deflate(data, size, 6);
            // Only use compressed version if it's actually smaller.
            if (deflated.size() < size) {
                entry.storedData = std::move(deflated);
                entry.compressed = true;
            } else {
                entry.storedData.assign(data, data + size);
            }
        } catch (const viper::pkg::DeflateError &) {
            // Compression failed — store uncompressed.
            entry.storedData.assign(data, data + size);
        }
    } else {
        entry.storedData.assign(data, data + size);
    }

    entries_.push_back(std::move(entry));
}

// ─── Little-endian write helpers ────────────────────────────────────────────

static void write16LE(std::vector<uint8_t> &out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

static void write32LE(std::vector<uint8_t> &out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 24));
}

static void write64LE(std::vector<uint8_t> &out, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

static void alignTo(std::vector<uint8_t> &out, size_t alignment) {
    size_t rem = out.size() % alignment;
    if (rem != 0)
        out.insert(out.end(), alignment - rem, 0);
}

// ─── writeToMemory ──────────────────────────────────────────────────────────

std::vector<uint8_t> VpaWriter::writeToMemory() const {
    std::vector<uint8_t> out;

    // Reserve rough estimate: header + data + TOC
    size_t estimate = kHeaderSize;
    for (const auto &e : entries_)
        estimate += e.storedData.size() + e.name.size() + 64;
    out.reserve(estimate);

    // ── Write header (32 bytes) ──
    // Magic (4 bytes)
    out.insert(out.end(), kMagic, kMagic + 4);
    // Version (2 bytes)
    write16LE(out, kVersion);
    // Flags (2 bytes) — bit 0 = archive has any compressed entries
    uint16_t flags = 0;
    for (const auto &e : entries_) {
        if (e.compressed) {
            flags |= 1;
            break;
        }
    }
    write16LE(out, flags);
    // Entry count (4 bytes)
    write32LE(out, static_cast<uint32_t>(entries_.size()));
    // TOC offset placeholder (8 bytes) — patched later
    size_t tocOffsetPos = out.size();
    write64LE(out, 0);
    // TOC size placeholder (8 bytes) — patched later
    size_t tocSizePos = out.size();
    write64LE(out, 0);
    // Reserved (4 bytes)
    write32LE(out, 0);

    // ── Write data entries (each 8-byte aligned) ──
    std::vector<uint64_t> dataOffsets;
    dataOffsets.reserve(entries_.size());

    for (const auto &e : entries_) {
        alignTo(out, kAlignment);
        dataOffsets.push_back(static_cast<uint64_t>(out.size()));
        out.insert(out.end(), e.storedData.begin(), e.storedData.end());
    }

    // ── Write TOC ──
    alignTo(out, kAlignment);
    uint64_t tocOffset = static_cast<uint64_t>(out.size());
    size_t tocStart = out.size();

    for (size_t i = 0; i < entries_.size(); ++i) {
        const auto &e = entries_[i];

        // name_len (2 bytes)
        write16LE(out, static_cast<uint16_t>(e.name.size()));

        // name (UTF-8 bytes, no null terminator)
        out.insert(out.end(), e.name.begin(), e.name.end());

        // data_offset (8 bytes)
        write64LE(out, dataOffsets[i]);

        // data_size — original uncompressed size (8 bytes)
        write64LE(out, e.originalSize);

        // stored_size — size in data region (8 bytes)
        write64LE(out, static_cast<uint64_t>(e.storedData.size()));

        // flags (2 bytes) — bit 0 = compressed
        write16LE(out, e.compressed ? 1 : 0);

        // reserved (2 bytes)
        write16LE(out, 0);
    }

    uint64_t tocSize = static_cast<uint64_t>(out.size() - tocStart);

    // ── Patch header: TOC offset and size ──
    for (int i = 0; i < 8; ++i)
        out[tocOffsetPos + i] = static_cast<uint8_t>(tocOffset >> (i * 8));
    for (int i = 0; i < 8; ++i)
        out[tocSizePos + i] = static_cast<uint8_t>(tocSize >> (i * 8));

    return out;
}

// ─── writeToFile ────────────────────────────────────────────────────────────

bool VpaWriter::writeToFile(const std::string &path, std::string &err) const {
    auto blob = writeToMemory();

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        err = "cannot open file for writing: " + path;
        return false;
    }

    f.write(reinterpret_cast<const char *>(blob.data()), static_cast<std::streamsize>(blob.size()));
    if (!f) {
        err = "failed to write VPA data to: " + path;
        return false;
    }

    return true;
}

} // namespace viper::asset
