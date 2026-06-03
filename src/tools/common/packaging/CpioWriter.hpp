//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/CpioWriter.hpp
// Purpose: Write portable ASCII CPIO archives for native macOS package payloads.
//
// Key invariants:
//   - Uses the POSIX portable ASCII format (magic "070707", all-octal fields).
//   - Entry paths are normalized to "./"-prefixed relative paths; traversal and
//     duplicate paths are rejected at add time.
//   - finish() appends the "TRAILER!!!" sentinel and pads to a 512-byte boundary.
//
// Ownership/Lifetime:
//   - Single-use accumulator: add entries, then call finish() to serialize.
//
// Links: CpioWriter.cpp, MacOSPackageBuilder.cpp, PkgVerify.cpp (reader side)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace viper::pkg {

/// @brief Builds a portable ASCII CPIO archive in memory for macOS .pkg payloads.
/// @details Entries are accumulated by the addXxx() methods and serialized by
///          finish(). Paths are sanitized and de-duplicated on insertion so the
///          resulting archive cannot contain traversal or duplicate entries.
class CpioWriter {
  public:
    /// @brief Add a directory entry (idempotent for an already-added path).
    /// @param path Archive-relative directory path.
    /// @param mode Permission bits (octal); the directory type bit is added.
    /// @param mtime Modification time (Unix timestamp).
    void addDirectory(const std::string &path, uint32_t mode = 0755, uint32_t mtime = 0);

    /// @brief Add a regular file entry from a raw byte buffer.
    /// @param path Archive-relative file path (must not be the archive root).
    /// @param data Pointer to the file bytes (may be null only when @p size is 0).
    /// @param size Number of bytes.
    /// @param mode Permission bits (octal); the regular-file type bit is added.
    /// @param mtime Modification time (Unix timestamp).
    /// @throws std::runtime_error on a root path, duplicate path, or null data.
    void addFile(const std::string &path,
                 const uint8_t *data,
                 size_t size,
                 uint32_t mode = 0644,
                 uint32_t mtime = 0);

    /// @brief Convenience: add a regular file entry from a byte vector.
    void addFileVec(const std::string &path,
                    const std::vector<uint8_t> &data,
                    uint32_t mode = 0644,
                    uint32_t mtime = 0);

    /// @brief Convenience: add a regular file entry from string content.
    void addFileString(const std::string &path,
                       const std::string &content,
                       uint32_t mode = 0644,
                       uint32_t mtime = 0);

    /// @brief Add a symbolic-link entry whose target stays inside the archive.
    /// @param path Archive-relative link path.
    /// @param target Link target (validated as a safe relative path).
    /// @param mtime Modification time (Unix timestamp).
    /// @throws std::runtime_error on a root path, duplicate path, or unsafe target.
    void addSymlink(const std::string &path, const std::string &target, uint32_t mtime = 0);

    /// @brief Serialize all entries into a complete CPIO archive.
    /// @return The archive bytes, including the TRAILER!!! record and 512-byte
    ///         padding.
    std::vector<uint8_t> finish() const;

  private:
    /// @brief The kind of filesystem object an entry represents.
    enum class EntryKind { Directory, File, Symlink };

    /// @brief One pending CPIO entry captured until finish() serializes it.
    struct Entry {
        EntryKind kind{EntryKind::File}; ///< Directory, file, or symlink.
        std::string path;                ///< Normalized "./"-prefixed path.
        std::string symlinkTarget;       ///< Link target (symlink entries only).
        std::vector<uint8_t> data;       ///< File payload (file entries only).
        uint32_t mode{0644};             ///< Mode bits including the type field.
        uint32_t mtime{0};               ///< Modification time (Unix timestamp).
    };

    std::vector<Entry> entries_;       ///< Entries in insertion order.
    std::set<std::string> seenPaths_;  ///< Paths added so far (duplicate guard).
};

} // namespace viper::pkg
