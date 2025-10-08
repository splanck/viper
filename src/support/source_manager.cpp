//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the SourceManager utility responsible for tracking source files
// referenced by diagnostics and front-end components.  The manager assigns
// stable numeric identifiers to file paths and resolves those identifiers back
// to normalized strings when printing diagnostics.
//
//===----------------------------------------------------------------------===//

#include "source_manager.hpp"

#include <filesystem>

namespace il::support
{

/// @brief Register a file path and assign it a stable identifier.
///
/// The path is normalized into a generic string so diagnostics print platform
/// independent output.  Identifiers start at one, leaving zero to represent an
/// unknown location.  Ownership of the normalized string remains with the
/// SourceManager.
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
/// Identifiers outside the valid range, including the sentinel zero, yield an
/// empty view.  Successful lookups return a view into the SourceManager's own
/// storage; callers must not outlive the manager when holding the view.
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
