// File: src/support/path_utils.cpp
// Purpose: Implement helpers for normalizing and caching source file paths.
// Key invariants: Normalization always yields forward slashes and resolves dot
// segments; cache entries remain valid for the lifetime of the cache object.
// Ownership/Lifetime: PathCache stores cached strings for reuse across calls.
// Links: docs/codemap.md

#include "support/path_utils.hpp"

#include "support/source_manager.hpp"

#include <algorithm>
#include <filesystem>

namespace il::support
{
namespace
{
[[nodiscard]] std::string normalizeImpl(std::string_view path)
{
    std::string key(path);
    std::string sanitized = key;
    std::replace(sanitized.begin(), sanitized.end(), '\\', '/');

    if (sanitized.empty())
        return std::string{"."};

    std::filesystem::path fsPath(sanitized);
    std::string generic = fsPath.lexically_normal().generic_string();

    if (generic.empty())
        generic = sanitized.front() == '/' ? std::string{"/"} : std::string{"."};

    return generic;
}
} // namespace

std::string PathCache::normalize(std::string_view path) const
{
    std::string key(path);
    auto it = stringCache_.find(key);
    if (it != stringCache_.end())
        return it->second;

    std::string normalized = normalizeImpl(path);
    auto [pos, inserted] = stringCache_.emplace(std::move(key), std::move(normalized));
    (void)inserted;
    return pos->second;
}

const std::string &
PathCache::getOrNormalize(const SourceManager &sm, uint32_t fileId, std::string_view fallback) const
{
    static const std::string kEmpty;
    if (fileId == 0)
        return kEmpty;

    auto [it, inserted] = fileIdCache_.try_emplace(fileId);
    if (!inserted)
        return it->second;

    std::string raw;
    if (!fallback.empty())
        raw.assign(fallback);
    else
        raw.assign(sm.getPath(fileId));

    it->second = normalize(raw);
    return it->second;
}

std::string basename(std::string_view path)
{
    if (path.empty())
        return {};
    size_t pos = path.find_last_of('/');
    if (pos == std::string_view::npos)
        return std::string(path);
    if (pos + 1 >= path.size())
        return {};
    return std::string(path.substr(pos + 1));
}

} // namespace il::support

