//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: support/source_manager.hpp
// Purpose: Declares manager for source file identifiers.
// Key invariants: File ID 0 is invalid.
// Ownership/Lifetime: Manager owns file path strings.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "source_location.hpp"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/// @brief Tracks mapping from file ids to paths and source locations.
/// @invariant File id 0 is invalid.
/// @ownership Owns stored file path strings.
namespace il::support {

/// @brief Diagnostic text reported when addFile() exhausts the 32-bit id space.
inline constexpr std::string_view kSourceManagerFileIdOverflowMessage =
    "source manager exhausted file identifier space";

/// @brief Friend hook exposing SourceManager internals to white-box tests.
struct SourceManagerTestAccess;

/// Maintains the mapping between numeric file identifiers and their
/// corresponding filesystem paths. Clients can register files and look up
/// paths by identifier.
class SourceManager {
  public:
    /// @brief Register file path @p path and return its id.
    /// @param path File system path.
    /// @return New file identifier (>0 on success, 0 on overflow).
    /// @details Returns zero when the identifier space is exhausted and refuses
    ///          to insert the file. Callers surface the diagnostic in their own
    ///          reporting context.
    uint32_t addFile(std::string path);

    /// @brief Retrieve path for @p file_id.
    /// @param file_id Identifier returned by addFile().
    /// @return File path string view.
    std::string_view getPath(uint32_t file_id) const;

    /// @brief Retrieve a single source line from the given file.
    /// @param file_id 1-based file identifier returned by addFile().
    /// @param line 1-based line number.
    /// @return The source line text (without newline), or empty if unavailable.
    /// @details Lazily loads and caches file contents on first access.
    std::string_view getLine(uint32_t file_id, uint32_t line) const;

    /// @brief Check whether a source line exists even if its text is empty.
    /// @param file_id 1-based file identifier returned by addFile().
    /// @param line 1-based line number.
    /// @return True when the source text for @p file_id contains @p line.
    /// @details This complements @ref getLine, whose empty string_view return value
    ///          is ambiguous between an existing blank line and a missing line.
    [[nodiscard]] bool hasLine(uint32_t file_id, uint32_t line) const;

    /// @brief Drop cached source lines for a file so the next query reloads them.
    /// @param file_id 1-based file identifier returned by addFile().
    /// @details This lets callers recover from an earlier failed disk read or pick
    ///          up updated file contents without replacing the file identifier.
    void invalidateSource(uint32_t file_id) const;

    /// @brief Cache source text for @p file_id without requiring it to exist on disk.
    /// @param file_id Identifier returned by addFile().
    /// @param source Full source text associated with the file id.
    void setSource(uint32_t file_id, std::string source);

  private:
    /// @brief Return whether @p file_id names a registered file.
    /// @param file_id Candidate 1-based file identifier.
    /// @return True when @p file_id is in range for the path storage.
    [[nodiscard]] bool isRegisteredFileId(uint32_t file_id) const noexcept;

    /// @brief Return a path view without acquiring the mutex.
    /// @param file_id Registered 1-based file identifier.
    /// @return Stored display path, or empty when @p file_id is invalid.
    /// @pre Caller must hold @ref mutex_.
    [[nodiscard]] std::string_view getPathLocked(uint32_t file_id) const;

    /// @brief Ensure the line cache for @p file_id exists.
    /// @param file_id Registered 1-based file identifier.
    /// @return Pointer to the cached line vector, or nullptr for invalid ids.
    /// @pre Caller must hold @ref mutex_.
    [[nodiscard]] const std::vector<std::string> *ensureLineCacheLocked(uint32_t file_id) const;

    /// Serializes access to path tables and mutable line cache.
    mutable std::mutex mutex_;
    /// Cached file contents split by line, keyed by file_id.
    /// Mutable because getLine() is logically const but lazily populates the cache.
    mutable std::unordered_map<uint32_t, std::vector<std::string>> lineCache_;
    /// Stored file paths. Index corresponds to file identifier; index 0 is reserved.
    /// Implemented with std::deque to keep string references stable as new files are added.
    std::deque<std::string> files_;

    /// Absolute filesystem paths used for disk I/O; indexes mirror @ref files_.
    std::deque<std::filesystem::path> disk_paths_;

    /// Next identifier to assign; stored as 64-bit to detect overflow safely.
    uint64_t next_file_id_ = 1;

    /// Fast lookup from normalized path to previously assigned identifier.
    std::unordered_map<std::string, uint32_t> path_to_id_;

    friend struct SourceManagerTestAccess;
};
} // namespace il::support
