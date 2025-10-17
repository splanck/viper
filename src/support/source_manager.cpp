// File: src/support/source_manager.cpp
// License: MIT License (c) 2024 The Viper Project Authors. See LICENSE in the
//          project root for details.
// Purpose: Implement the SourceManager that tracks canonicalised file paths and
//          hands out stable identifiers for diagnostics and front-ends.
// Ownership/Lifetime: Manager owns the stored strings; views remain valid until
//                     the manager is destroyed.
// Links: docs/contributor-guide.md, docs/codemap.md#support

/// @file
/// @brief Provides the backing store for file identifiers used in diagnostics.
/// @details Front ends hand a `SourceManager` file paths to obtain lightweight
///          integer identifiers.  Those identifiers flow through tokens,
///          diagnostics, and serialized IL artifacts.  Housing the lookup logic
///          in a dedicated translation unit keeps the hot path header minimal
///          while letting this implementation focus on normalization details.

#include "source_manager.hpp"

#include <filesystem>

namespace il::support
{

/// @brief Register a file path and assign it a stable identifier.
///
/// @details The path is normalized into a generic string so diagnostics print
///          platform independent output regardless of OS path conventions.
///          Identifiers start at one, leaving zero to represent an unknown
///          location.  Ownership of the normalized string remains with the
///          `SourceManager`, allowing callers to hold `std::string_view`
///          references safely for the manager's lifetime.
///
/// @param path Filesystem path to normalize and store.
/// @return Identifier (>0) representing the stored path.
uint32_t SourceManager::addFile(std::string path)
{
    std::filesystem::path p(std::move(path));
    files_.push_back(p.lexically_normal().generic_string());
    return static_cast<uint32_t>(files_.size());
}

/// @brief Retrieve the canonical path associated with a file identifier.
///
/// @details Identifiers outside the valid range, including the sentinel zero,
///          yield an empty view.  Successful lookups return a view into the
///          SourceManager's own storage; callers must not outlive the manager
///          when holding the view.  The method is noexcept and inexpensive so it
///          can appear on hot diagnostic paths.
///
/// @param file_id 1-based identifier previously returned by addFile().
/// @return Stored path, or empty string view if @p file_id is invalid.
std::string_view SourceManager::getPath(uint32_t file_id) const
{
    if (file_id == 0 || file_id > files_.size())
        return {};
    return files_[file_id - 1];
}
} // namespace il::support
