/**
 * @file source_manager.cpp
 * @brief Implements the source manager responsible for tracking file paths.
 * @copyright
 *     MIT License. See the LICENSE file in the project root for full terms.
 * @details
 *     The source manager assigns monotonically increasing identifiers to source
 *     files and exposes utilities for path lookup used by diagnostics and
 *     parsing.
 */

#include "source_manager.hpp"

#include <filesystem>

namespace il::support
{

/**
 * @brief Registers a file path and returns its numeric identifier.
 *
 * The path is normalized to a portable string representation before being
 * stored.  Identifiers are 1-based and correspond to the index of the path in
 * the internal array, making `0` a sentinel for "unknown".
 *
 * @param path Path to record.
 * @return New identifier that can later be used with `getPath`.
 */
uint32_t SourceManager::addFile(std::string path)
{
    std::filesystem::path p(std::move(path));
    files_.push_back(p.lexically_normal().generic_string());
    return static_cast<uint32_t>(files_.size());
}

/**
 * @brief Resolves a previously registered identifier back to its path.
 *
 * The function validates the identifier range and, if valid, returns a view into
 * the stored canonical string.  Invalid identifiers yield an empty view so that
 * callers can detect missing entries.
 *
 * @param file_id Identifier returned by `addFile`.
 * @return Canonical string path, or empty when @p file_id is invalid.
 */
std::string_view SourceManager::getPath(uint32_t file_id) const
{
    if (file_id == 0 || file_id > files_.size())
        return {};
    return files_[file_id - 1];
}
} // namespace il::support
