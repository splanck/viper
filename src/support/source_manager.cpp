//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/support/source_manager.cpp
// Purpose: Implement the diagnostic-aware source file registry used across the
//          compiler pipeline.
// Key invariants: File identifiers are assigned monotonically starting at one;
//                 identifier zero is reserved to represent "unknown" locations.
//                 Path normalisation produces stable, slash-separated strings so
//                 diagnostics do not leak host-specific formatting.
// Ownership/Lifetime: SourceManager owns all stored path strings and hands out
//                     string_view references that remain valid for the lifetime
//                     of the manager instance.  Callers are responsible for
//                     respecting that lifetime.
// Links: docs/codemap.md#support-library, docs/architecture.md#diagnostics
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Provides the backing store for file identifiers used in diagnostics.
/// @details Front ends hand a `SourceManager` file paths to obtain lightweight
///          integer identifiers.  Those identifiers flow through tokens,
///          diagnostics, and serialized IL artifacts.  Housing the lookup logic
///          in a dedicated translation unit keeps the hot path header minimal
///          while letting this implementation focus on normalization details.

#include "source_manager.hpp"

#include "support/diag_expected.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

namespace il::support
{
namespace
{
/// @brief Normalise a filesystem path into the canonical representation used by diagnostics.
/// @details The helper constructs a `std::filesystem::path` from the raw input,
///          applies @ref std::filesystem::path::lexically_normal to collapse
///          redundant components, and finally emits the generic (forward-slash
///          separated) representation.  On Windows the routine additionally
///          lowercases ASCII letters so diagnostic comparisons become
///          case-insensitive, mirroring how the rest of the compiler treats
///          paths.  The resulting string is suitable for persistent storage
///          inside the @ref SourceManager.
/// @param path Raw filesystem path supplied by the caller.
/// @return Normalised path string owned by the caller.
std::string normalizePath(std::string path)
{
    std::filesystem::path p(std::move(path));
    std::string normalized = p.lexically_normal().generic_string();

#ifdef _WIN32
    for (char &ch : normalized)
    {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    }
#endif

    return normalized;
}
} // namespace

/// @brief Register a file path and assign it a stable identifier.
///
/// @details Normalises @p path using @ref normalizePath before deduplicating it
///          against previously seen entries.  Identifiers start at one so that
///          zero can unambiguously signal "unknown".  When the identifier space
///          would overflow, the helper emits a diagnostic via
///          @ref printDiag and returns zero so callers can surface a fatal
///          configuration error.  Stored strings live inside the manager's
///          @ref files_ container for the remainder of the manager's lifetime,
///          making it safe to hand out @ref std::string_view references.
///
/// @param path Filesystem path to normalize and store.
/// @return Identifier (>0) representing the stored path, or zero on overflow.
uint32_t SourceManager::addFile(std::string path)
{
    std::string normalized = normalizePath(std::move(path));

    if (auto it = path_to_id_.find(normalized); it != path_to_id_.end())
        return it->second;

    if (next_file_id_ > std::numeric_limits<uint32_t>::max())
    {
        auto diag = makeError({}, std::string{kSourceManagerFileIdOverflowMessage});
        printDiag(diag, std::cerr);
        return 0;
    }

    const uint32_t file_id = static_cast<uint32_t>(next_file_id_++);
    files_.push_back(std::move(normalized));
    path_to_id_.emplace(files_.back(), file_id);
    return file_id;
}

/// @brief Retrieve the canonical path associated with a file identifier.
///
/// @details Performs range checks against the stored vector to guarantee that
///          invalid identifiers (including zero) result in an empty view rather
///          than undefined behaviour.  Successful lookups return an owning
///          manager-backed string view, allowing diagnostics to print the path
///          without copying.  Callers must ensure the @ref SourceManager outlives
///          any view they retain.
///
/// @param file_id 1-based identifier previously returned by addFile().
/// @return Stored path, or empty string view if @p file_id is invalid.
std::string_view SourceManager::getPath(uint32_t file_id) const
{
    if (file_id == 0 || file_id > files_.size())
        return {};
    return files_[file_id - 1];
}

std::string_view SourceManager::getLine(uint32_t file_id, uint32_t line) const
{
    if (file_id == 0 || line == 0)
        return {};

    // Check cache first
    auto it = lineCache_.find(file_id);
    if (it == lineCache_.end())
    {
        // Load file from disk
        auto path = getPath(file_id);
        if (path.empty())
            return {};

        std::vector<std::string> lines;
        std::filesystem::path fsPath;
#if defined(__cpp_char8_t)
        const auto *raw = reinterpret_cast<const char8_t *>(path.data());
        const std::u8string u8Path(raw, raw + path.size());
        fsPath = std::filesystem::path(u8Path);
#else
        fsPath = std::filesystem::path(std::string(path));
#endif
        std::ifstream f(fsPath);
        if (!f)
        {
            // Cache empty vector to avoid repeated I/O attempts
            lineCache_.emplace(file_id, std::vector<std::string>{});
            return {};
        }
        std::string buf;
        while (std::getline(f, buf))
            lines.push_back(buf);

        it = lineCache_.emplace(file_id, std::move(lines)).first;
    }

    const auto &lines = it->second;
    if (line > lines.size())
        return {};
    return lines[line - 1];
}
} // namespace il::support
