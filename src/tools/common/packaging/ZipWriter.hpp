//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/ZipWriter.hpp
// Purpose: ZIP archive writer with Unix permission support for the packaging
//          library. Produces valid ZIP files with proper file mode bits so
//          that macOS .app bundles preserve executable permissions.
//
// Key invariants:
//   - version_made_by = (3 << 8) | 20 — Unix, version 2.0.
//   - external_file_attributes encodes Unix mode in upper 16 bits.
//   - Entries >64 bytes are DEFLATE-compressed if compression reduces size.
//   - CRC-32 is computed for every entry.
//   - All integers are stored little-endian per PKWARE spec.
//
// Ownership/Lifetime:
//   - ZipWriter accumulates data in memory, writes to disk on finish().
//   - After finish(), the writer is consumed and should not be reused.
//
// Links: src/runtime/io/rt_archive.c (reference), PkgDeflate.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace viper::pkg {

class ZipWriter {
  public:
    struct LayoutEntry {
        std::string name;
        uint32_t localHeaderOffset{0};
        uint32_t localDataOffset{0};
        uint32_t compressedSize{0};
        uint32_t uncompressedSize{0};
        uint16_t method{0};
        bool isDirectory{false};
    };

    ZipWriter();
    ~ZipWriter();

    ZipWriter(const ZipWriter &) = delete;
    ZipWriter &operator=(const ZipWriter &) = delete;

    /// @brief Add a file entry with data and Unix permissions.
    /// @param name Entry name (forward-slash separators).
    /// @param data File contents.
    /// @param len Length of data.
    /// @param unixMode Unix permission bits (e.g. 0100755 for executable).
    void addFile(const std::string &name,
                 const uint8_t *data,
                 size_t len,
                 uint32_t unixMode = 0100644);

    /// @brief Add a file entry from a string.
    void addFileString(const std::string &name,
                       const std::string &content,
                       uint32_t unixMode = 0100644);

    /// @brief Add a directory entry.
    /// @param name Directory name (trailing / added if missing).
    /// @param unixMode Unix directory mode (default 040755).
    void addDirectory(const std::string &name, uint32_t unixMode = 040755);

    /// @brief Add a symlink entry.
    /// @param name Symlink name in archive.
    /// @param target Symlink target path.
    void addSymlink(const std::string &name, const std::string &target);

    /// @brief Finalize and write the ZIP to a file.
    /// @param path Output file path.
    /// @throws std::runtime_error on write failure.
    void finish(const std::string &path);

    /// @brief Finalize and return the ZIP as bytes.
    std::vector<uint8_t> finishToVector();

    /// @brief Enable or disable per-entry compression.
    void setCompressionEnabled(bool enabled) noexcept {
        compressionEnabled_ = enabled;
    }

    /// @brief Inspect local-header/data offsets for entries already added.
    const std::vector<LayoutEntry> &layoutEntries() const noexcept {
        return layoutEntries_;
    }

  private:
    struct Entry {
        std::string name;
        uint32_t crc32;
        uint32_t compressedSize;
        uint32_t uncompressedSize;
        uint16_t method;
        uint16_t modTime;
        uint16_t modDate;
        uint32_t localOffset;
        uint32_t externalAttrs;
    };

    std::vector<uint8_t> buffer_;
    std::vector<Entry> entries_;
    std::vector<LayoutEntry> layoutEntries_;
    bool compressionEnabled_{true};

    void validateEntryName(const std::string &name) const;
    void validateArchiveLimit(size_t value, size_t maxValue, const char *what) const;
    void writeBytes(const uint8_t *data, size_t len);
    void writeU16(uint16_t v);
    void writeU32(uint32_t v);
    void writeCentralDirectory();
    static void getDosTime(uint16_t &time, uint16_t &date);
};

} // namespace viper::pkg
