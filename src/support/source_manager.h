// File: src/support/source_manager.h
// Purpose: Declares manager for source file identifiers.
// Key invariants: File ID 0 is invalid.
// Ownership/Lifetime: Manager owns file path strings.
// Links: docs/class-catalog.md
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
/// @brief Tracks mapping from file ids to paths and source locations.
/// @invariant File id 0 is invalid.
/// @ownership Owns stored file path strings.
namespace il::support {
/// @brief Represents a position within a source file.
struct SourceLoc {
  uint32_t file_id = 0; ///< Identifier returned by SourceManager
  uint32_t line = 0;    ///< 1-based line number
  uint32_t column = 0;  ///< 1-based column number
  /// @brief Whether this location refers to a real file.
  bool isValid() const { return file_id != 0; }
};

/// @brief Manages file identifiers and paths.
class SourceManager {
public:
  /// @brief Register file path @p path and return its id.
  /// @param path File system path.
  /// @return New file identifier (>0).
  uint32_t addFile(std::string path);

  /// @brief Retrieve path for @p file_id.
  /// @param file_id Identifier returned by addFile().
  /// @return File path string view.
  std::string_view getPath(uint32_t file_id) const;

private:
  std::vector<std::string> files_; ///< Stored file paths
};
} // namespace il::support
