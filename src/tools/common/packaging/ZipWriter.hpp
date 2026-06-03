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
#include <set>
#include <string>
#include <vector>

namespace viper::pkg {

/// @brief Builds a ZIP archive in memory with Unix permission bits preserved.
/// @details Accumulates entries (files, directories, symlinks), optionally
///          DEFLATE-compressing each, and serializes a spec-compliant archive on
///          finish()/finishToVector(). Unix modes are stored in the external file
///          attributes so macOS .app bundles keep their executable bits.
class ZipWriter {
  public:
    /// @brief Metadata about a written entry, used by callers that need the
    ///        exact byte offsets to embed into stub data before finish() is called.
    struct LayoutEntry {
        std::string name;              ///< Normalised entry path as stored in the archive.
        uint32_t localHeaderOffset{0}; ///< Byte offset of the local file header from archive start.
        uint32_t localDataOffset{0};   ///< Byte offset of the compressed data from archive start.
        uint32_t compressedSize{0}; ///< Compressed byte count (equals uncompressedSize for stored).
        uint32_t uncompressedSize{0}; ///< Original byte count before compression.
        uint32_t crc32{0};            ///< CRC-32 of the uncompressed data.
        uint16_t method{0};           ///< Compression method: 0=stored, 8=deflate.
        bool isDirectory{false};      ///< True for directory entries (typeflag 5).
    };

    /// @brief Construct an empty ZIP writer with compression enabled.
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
    // Internal per-entry record used to build the central directory on finish().
    struct Entry {
        std::string name;          ///< Normalised entry path.
        uint32_t crc32;            ///< CRC-32 of uncompressed data.
        uint32_t compressedSize;   ///< Byte count of data as stored in archive.
        uint32_t uncompressedSize; ///< Byte count of data before compression.
        uint16_t method;           ///< Compression method (0=stored, 8=deflate).
        uint16_t modTime;          ///< DOS last-modified time.
        uint16_t modDate;          ///< DOS last-modified date.
        uint32_t localOffset;      ///< Byte offset of this entry's local header from archive start.
        uint32_t externalAttrs;    ///< ZIP external file attributes (Unix mode in upper 16 bits).
    };

    std::vector<uint8_t> buffer_;
    std::vector<Entry> entries_;
    std::vector<LayoutEntry> layoutEntries_;
    std::set<std::string> seenNames_;
    bool compressionEnabled_{true};
    bool finalized_{false};

    /// @brief Throw if the archive has already been finalized.
    void ensureOpen() const;
    /// @brief Sanitize an entry path: reject control characters, absolute paths, and ".." segments.
    std::string normalizeEntryName(const std::string &name) const;
    /// @brief Normalize and validate that a symlink target remains relative inside the archive
    /// root.
    std::string normalizeSymlinkTarget(const std::string &entryName,
                                       const std::string &target) const;
    /// @brief Throw if value > maxValue; prevents central-directory integer overflow on large
    /// archives.
    void validateArchiveLimit(size_t value, size_t maxValue, const char *what) const;
    /// @brief Append len bytes of data to buffer_.
    void writeBytes(const uint8_t *data, size_t len);
    /// @brief Append a 16-bit little-endian integer to buffer_.
    void writeU16(uint16_t v);
    /// @brief Append a 32-bit little-endian integer to buffer_.
    void writeU32(uint32_t v);
    /// @brief Write all central directory records and the EOCD to buffer_.
    void writeCentralDirectory();
    /// @brief Return the current wall-clock time as a DOS time/date pair for entry metadata.
    static void getDosTime(uint16_t &time, uint16_t &date);
};

} // namespace viper::pkg
