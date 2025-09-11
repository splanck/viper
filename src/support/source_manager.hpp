// File: src/support/source_manager.hpp
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
namespace il::support
{

/// Represents an absolute position within a source file as tracked by
/// SourceManager. A zero file_id denotes an invalid location.
struct SourceLoc
{
    /// Identifier assigned by SourceManager; 0 indicates an invalid location.
    uint32_t file_id = 0;

    /// 1-based line number within the file; 0 if the line is unknown.
    uint32_t line = 0;

    /// 1-based column number within the line; 0 if the column is unknown.
    uint32_t column = 0;

    /// @brief Whether this location refers to a real file.
    bool isValid() const
    {
        return file_id != 0;
    }
};

/// Maintains the mapping between numeric file identifiers and their
/// corresponding filesystem paths. Clients can register files and look up
/// paths by identifier.
class SourceManager
{
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
    /// Stored file paths. Index corresponds to file identifier; index 0 is reserved.
    std::vector<std::string> files_;
};
} // namespace il::support
