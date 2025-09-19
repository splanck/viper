// File: src/support/source_manager.cpp
// Purpose: Implements utilities to manage source buffers.
// License: MIT License. See LICENSE in the project root for details.
// Key invariants: None.
// Ownership/Lifetime: SourceManager owns loaded buffers.
// Links: docs/class-catalog.md

#include "source_manager.hpp"

#include <filesystem>

namespace il::support
{

/// @brief Register a new file path and assign it a unique identifier.
/// @param path Filesystem path to lexically normalize and store.
/// @return Identifier (>0) representing the stored path.
uint32_t SourceManager::addFile(std::string path)
{
    std::filesystem::path p(std::move(path));
    files_.push_back(p.lexically_normal().generic_string());
    return static_cast<uint32_t>(files_.size());
}

/// @brief Retrieve the canonical path associated with @p file_id.
/// @param file_id 1-based identifier previously returned by addFile().
/// @return Stored path, or empty string view if @p file_id is invalid.
std::string_view SourceManager::getPath(uint32_t file_id) const
{
    if (file_id == 0 || file_id > files_.size())
        return {};
    return files_[file_id - 1];
}
} // namespace il::support
