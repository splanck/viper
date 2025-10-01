// File: src/support/path_utils.hpp
// Purpose: Declare helpers for normalizing and caching source file paths.
// Key invariants: Normalized paths always use forward slashes and have dot
// segments resolved; cache entries stay consistent for the lifetime of the
// cache instance.
// Ownership/Lifetime: PathCache owns cached strings and must outlive
// references returned by getOrNormalize().
// Links: docs/codemap.md
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace il::support
{
class SourceManager;

/// @brief Cache that normalizes file system paths and remembers results.
/// @invariant Returned normalized paths always use forward slashes.
/// @ownership Owns cached strings for both raw paths and file identifiers.
class PathCache
{
  public:
    /// @brief Normalize @p path and cache the result for reuse.
    /// @param path Arbitrary file system path, possibly using backslashes.
    /// @return Normalized path with dot segments collapsed and forward slashes.
    [[nodiscard]] std::string normalize(std::string_view path) const;

    /// @brief Retrieve normalized path for @p fileId from @p sm, caching it on demand.
    /// @param sm Source manager providing canonical file paths.
    /// @param fileId Identifier assigned by the source manager.
    /// @param fallback Optional raw path to avoid redundant lookups.
    /// @return Reference to the cached normalized path or an empty string when unavailable.
    [[nodiscard]] const std::string &
    getOrNormalize(const SourceManager &sm, uint32_t fileId, std::string_view fallback = {}) const;

  private:
    mutable std::unordered_map<std::string, std::string> stringCache_; ///< Raw -> normalized cache.
    mutable std::unordered_map<uint32_t, std::string> fileIdCache_;    ///< File id -> normalized cache.
};

/// @brief Compute basename component of @p path after normalization.
/// @param path Path expressed with forward slashes.
/// @return Last path component or empty string when none exists.
[[nodiscard]] std::string basename(std::string_view path);

} // namespace il::support

