//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/ZipReader.hpp
// Purpose: Minimal ZIP archive reader for the packaging library. GC-free.
//          Used by the uninstaller, unit tests, and post-build verification.
//
// Key invariants:
//   - Read-only: does not modify the input buffer.
//   - Supports stored (method 0) and DEFLATE (method 8) entries.
//   - Parses the central directory to enumerate entries.
//
// Ownership/Lifetime:
//   - References external data buffer (caller must keep alive).
//
// Links: ZipWriter.hpp, PkgDeflate.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace viper::pkg {

/// @brief A single entry in a ZIP archive.
struct ZipEntry {
    std::string name;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t method; ///< 0=stored, 8=deflate
    uint32_t crc32;
    uint32_t localHeaderOffset;
};

/// @brief Error thrown on invalid ZIP data.
class ZipReadError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// @brief Read-only ZIP archive backed by an in-memory buffer.
class ZipReader {
  public:
    /// @brief Open a ZIP from a memory buffer.
    /// @param data Pointer to ZIP data (caller-owned, must stay alive).
    /// @param len  Length of ZIP data.
    /// @throws ZipReadError if the buffer is not a valid ZIP.
    ZipReader(const uint8_t *data, size_t len);

    /// @brief List all entries in the archive.
    const std::vector<ZipEntry> &entries() const {
        return entries_;
    }

    /// @brief Find an entry by name.
    /// @return Pointer to entry, or nullptr if not found.
    const ZipEntry *find(const std::string &name) const;

    /// @brief Extract a single entry to a byte vector.
    /// @throws ZipReadError on decompression failure or CRC mismatch.
    std::vector<uint8_t> extract(const ZipEntry &entry) const;

  private:
    const uint8_t *data_;
    size_t len_;
    std::vector<ZipEntry> entries_;

    void parseCentralDirectory();
};

} // namespace viper::pkg
