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
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/// @brief Tracks mapping from file ids to paths and source locations.
/// @invariant File id 0 is invalid.
/// @ownership Owns stored file path strings.
namespace il::support
{

inline constexpr std::string_view kSourceManagerFileIdOverflowMessage =
    "source manager exhausted file identifier space";

struct SourceManagerTestAccess;

/// Maintains the mapping between numeric file identifiers and their
/// corresponding filesystem paths. Clients can register files and look up
/// paths by identifier.
class SourceManager
{
  public:
    /// @brief Register file path @p path and return its id.
    /// @param path File system path.
    /// @return New file identifier (>0 on success, 0 on overflow).
    /// @details Emits an error diagnostic when the identifier space is
    ///          exhausted and refuses to insert the file.
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

  private:
    /// Cached file contents split by line, keyed by file_id.
    /// Mutable because getLine() is logically const but lazily populates the cache.
    mutable std::unordered_map<uint32_t, std::vector<std::string>> lineCache_;
    /// Stored file paths. Index corresponds to file identifier; index 0 is reserved.
    /// Implemented with std::deque to keep string references stable as new files are added.
    std::deque<std::string> files_;

    /// Next identifier to assign; stored as 64-bit to detect overflow safely.
    uint64_t next_file_id_ = 1;

    /// Fast lookup from normalized path to previously assigned identifier.
    std::unordered_map<std::string, uint32_t> path_to_id_;

    friend struct SourceManagerTestAccess;
};
} // namespace il::support
