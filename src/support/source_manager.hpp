// File: src/support/source_manager.hpp
// Purpose: Declares manager for source file identifiers.
// Key invariants: File ID 0 is invalid.
// Ownership/Lifetime: Manager owns file path strings.
// Links: docs/class-catalog.md
#pragma once

#include "support/source_loc.hpp"

#include <string>
#include <string_view>
#include <vector>

/// @brief Tracks mapping from file ids to paths and source locations.
/// @invariant File id 0 is invalid.
/// @ownership Owns stored file path strings.
namespace il::support
{

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
