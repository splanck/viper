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
#include <limits>
#include <stdexcept>
#include <string_view>

namespace viper::asset {
namespace {

/// @brief Detect empty, "." or ".." components in a forward-slash entry name.
/// @details Splits @p name on '/' and reports whether any segment is empty (e.g.
///          a leading, trailing, or doubled slash) or is a "." / ".." traversal
///          component, which would let an archive entry escape its intended
///          directory when later extracted.
/// @param name Candidate entry name (forward-slash separated).
/// @return true when @p name contains an unsafe segment.
bool hasUnsafePathSegment(std::string_view name) {
    std::size_t begin = 0;
    while (begin <= name.size()) {
        const std::size_t end = name.find('/', begin);
        const std::string_view segment =
            end == std::string_view::npos ? name.substr(begin) : name.substr(begin, end - begin);
        if (segment.empty() || segment == "." || segment == "..")
            return true;
        if (end == std::string_view::npos)
            break;
        begin = end + 1;
    }
    return false;
}

} // namespace

// ─── VPA format constants ───────────────────────────────────────────────────

static constexpr uint8_t kMagic[4] = {'V', 'P', 'A', '1'};
static constexpr uint16_t kVersion = 1;
static constexpr size_t kHeaderSize = 32;
static constexpr size_t kAlignment = 8;

// ─── Pre-compressed extension skip list ─────────────────────────────────────

/// @brief Decide whether an entry's extension is already-compressed.
/// @details Lowercases the file extension and tests it against a skip list of
///          formats (PNG/JPEG/OGG/MP3/GLB/ZIP/etc.) whose data is already
///          entropy-coded, so re-running DEFLATE would only add overhead.
/// @param name Entry name whose extension is examined.
/// @return true when compression should be skipped for @p name.
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
        ".png",
        ".jpg",
        ".jpeg",
        ".gif",
        ".ogg",
        ".mp3",
        ".glb",
        ".gz",
        ".zip",
        ".vpa",
        ".zst",
        ".br",
        ".webp",
    };
    for (const char *s : skip) {
        if (ext == s)
            return true;
    }
    return false;
}

// ─── addEntry ───────────────────────────────────────────────────────────────

/// @brief Validate and store one asset entry.
/// @details Rejects empty/over-long names, null data with non-zero size, archive
///          overflow, absolute or backslash/colon paths, '.'/'..' segments, and
///          duplicate names. When @p compress is requested for a non-empty,
///          non-pre-compressed entry, the data is DEFLATE'd (level 6) and the
///          compressed form is kept only if it is actually smaller; otherwise the
///          bytes are stored verbatim. See the header for the parameter contract.
void VpaWriter::addEntry(const std::string &name, const uint8_t *data, size_t size, bool compress) {
    if (name.empty())
        throw std::invalid_argument("VPA entry name must not be empty");
    if (name.size() > std::numeric_limits<uint16_t>::max())
        throw std::length_error("VPA entry name is too long: " + name);
    if (size > 0 && data == nullptr)
        throw std::invalid_argument("VPA entry data is null for: " + name);
    if (entries_.size() >= std::numeric_limits<uint32_t>::max())
        throw std::length_error("VPA archive has too many entries");
    if (name.front() == '/' || name.find('\\') != std::string::npos ||
        name.find(":") != std::string::npos) {
        throw std::invalid_argument("VPA entry name must be a relative forward-slash path: " +
                                    name);
    }
    if (hasUnsafePathSegment(name))
        throw std::invalid_argument("VPA entry name must not contain empty, '.' or '..' segments: " +
                                    name);
    for (const auto &existing : entries_) {
        if (existing.name == name)
            throw std::invalid_argument("duplicate VPA entry: " + name);
    }

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
                if (size > 0)
                    entry.storedData.assign(data, data + size);
            }
        } catch (const viper::pkg::DeflateError &ex) {
            throw std::runtime_error("VPA compression failed for '" + name + "': " + ex.what());
        }
    } else {
        if (size > 0)
            entry.storedData.assign(data, data + size);
    }

    entries_.push_back(std::move(entry));
}

// ─── Little-endian write helpers ────────────────────────────────────────────

/// @brief Append a 16-bit value to @p out in little-endian byte order.
static void write16LE(std::vector<uint8_t> &out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

/// @brief Append a 32-bit value to @p out in little-endian byte order.
static void write32LE(std::vector<uint8_t> &out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 24));
}

/// @brief Append a 64-bit value to @p out in little-endian byte order.
static void write64LE(std::vector<uint8_t> &out, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

/// @brief Pad @p out with zero bytes up to the next @p alignment boundary.
/// @details No-op when the buffer is already aligned; used to keep data entries
///          and the TOC 8-byte aligned for fast runtime reads.
static void alignTo(std::vector<uint8_t> &out, size_t alignment) {
    size_t rem = out.size() % alignment;
    if (rem != 0)
        out.insert(out.end(), alignment - rem, 0);
}

// ─── writeToMemory ──────────────────────────────────────────────────────────

/// @brief Serialize all entries into an in-memory VPA archive.
/// @details Lays out the archive as: a 32-byte header (magic "VPA1", version,
///          flags with bit0 set when any entry is compressed, entry count, and
///          placeholder TOC offset/size patched at the end), then each entry's
///          stored bytes 8-byte aligned, then the table of contents (per-entry
///          name length, name, data offset, original size, stored size, and
///          flags). The TOC offset/size placeholders in the header are patched
///          once the TOC is written. See the header for the return contract.
std::vector<uint8_t> VpaWriter::writeToMemory() const {
    std::vector<uint8_t> out;
    if (entries_.size() > std::numeric_limits<uint32_t>::max())
        throw std::length_error("VPA archive has too many entries");

    // Reserve rough estimate: header + data + TOC
    size_t estimate = kHeaderSize;
    for (const auto &e : entries_) {
        const size_t add = e.storedData.size() + e.name.size() + 64u;
        if (estimate > std::numeric_limits<size_t>::max() - add)
            throw std::length_error("VPA archive is too large");
        estimate += e.storedData.size() + e.name.size() + 64;
    }
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
        if (e.name.size() > std::numeric_limits<uint16_t>::max())
            throw std::length_error("VPA entry name is too long: " + e.name);

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

/// @brief Serialize the archive and write it to a file.
/// @details Builds the archive with writeToMemory() and writes it in one binary,
///          truncating pass; open or write failures set @p err. See the header
///          for the parameter and return contract.
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
