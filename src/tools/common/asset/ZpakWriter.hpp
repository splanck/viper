//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/asset/ZpakWriter.hpp
// Purpose: Writer for the ZPAK (Zanna Pack Archive) binary format. Serializes
//          named asset entries into a compact archive with optional per-entry
//          DEFLATE compression. Used for both embedded .rodata blobs and
//          standalone .zpak distribution files.
//
// Key invariants:
//   - Entry names are UTF-8 relative paths with forward slashes.
//   - Each entry's data is 8-byte aligned in the output.
//   - TOC is written at the end; header toc_offset is patched after TOC.
//   - Pre-compressed formats (.png, .jpg, .ogg, etc.) skip compression.
//   - Current output is ZPAK v2 and checksums every original entry payload.
//
// Ownership/Lifetime:
//   - Value type. Entries are copied into internal storage on addEntry().
//   - Output vectors/files are caller-owned after write.
//
// Links: rt_zpak_reader.h (runtime reader), AssetCompiler.hpp (build tool)
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace zanna::asset {

/// @brief Writer for ZPAK (Zanna Pack Archive) binary format.
///
/// Usage:
///   ZpakWriter w;
///   w.addEntry("sprites/hero.png", data, size, /*compress=*/true);
///   auto blob = w.writeToMemory();
///
/// @invariant Entry names must not contain backslashes or be empty.
/// @invariant Entry data may be empty (zero-length assets are valid).
class ZpakWriter {
  public:
    /// @brief Add an asset entry to the archive.
    ///
    /// If compress is true and the file extension is not in the pre-compressed
    /// skip list (.png, .jpg, .ogg, .mp3, etc.), the entry data will be
    /// DEFLATE-compressed in the output.
    ///
    /// @param name  Relative path (forward slashes, UTF-8).
    /// @param data  Raw asset bytes.
    /// @param size  Length of data in bytes.
    /// @param compress  Whether to attempt DEFLATE compression.
    void addEntry(const std::string &name, const uint8_t *data, size_t size, bool compress);

    /// @brief Write the complete ZPAK archive to a memory buffer.
    /// @details Emits format version 2 with one CRC-32 of the original
    ///          uncompressed bytes on every TOC entry. Data and TOC offsets are
    ///          checked while the vector grows; the writer remains unchanged.
    /// @return ZPAK-format byte vector (header + data + TOC).
    std::vector<uint8_t> writeToMemory() const;

    /// @brief Write the complete ZPAK archive to a file.
    /// @param path  Output file path.
    /// @param err   Error message on failure.
    /// @return true on success, false on I/O error.
    bool writeToFile(const std::string &path, std::string &err) const;

    /// @brief Number of entries added so far.
    size_t entryCount() const noexcept {
        return entries_.size();
    }

    /// @brief Check if any entries have been added.
    bool empty() const noexcept {
        return entries_.empty();
    }

  private:
    /// @brief Check if a file extension should skip compression.
    static bool isPreCompressed(const std::string &name);

    struct Entry {
        std::string name;                ///< Relative entry name (forward slashes).
        std::vector<uint8_t> storedData; ///< Possibly compressed.
        uint64_t originalSize{0};        ///< Uncompressed size.
        uint32_t crc32{0};               ///< CRC-32 of the original uncompressed bytes.
        bool compressed{false};          ///< True if storedData is DEFLATE'd.
    };

    std::vector<Entry> entries_;
    std::unordered_set<std::string> seenNames_; ///< Names already added, for duplicate rejection.
};

} // namespace zanna::asset
