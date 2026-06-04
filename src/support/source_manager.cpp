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

#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>

namespace il::support {
namespace {
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
std::string normalizePath(std::string path) {
    std::filesystem::path p(std::move(path));
    std::string normalized = p.lexically_normal().generic_string();

#ifdef _WIN32
    for (char &ch : normalized) {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    }
#endif

    return normalized;
}

/// @brief Build the stable filesystem path used for disk reads.
/// @param path Raw path supplied by the caller.
/// @return Absolute, lexically-normal filesystem path when it can be computed.
/// @details Diagnostic display paths stay relative and portable, but disk reads
///          need a path that is independent of later current-working-directory
///          changes. Filesystem errors fall back to the lexical input path so
///          registration remains non-I/O and tolerant of virtual paths.
std::filesystem::path makeDiskPath(const std::string &path) {
    std::filesystem::path p(path);
    if (p.empty())
        return p;

    std::error_code ec;
    std::filesystem::path absolute = p.is_absolute() ? p : std::filesystem::absolute(p, ec);
    if (ec)
        absolute = p;
    return absolute.lexically_normal();
}

/// @brief Remove a trailing carriage return from a line buffer.
/// @param line Line buffer read from disk or in-memory source.
/// @details Normalizes CRLF text to the same cached representation as LF text.
void stripTrailingCarriageReturn(std::string &line) {
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
}

/// @brief Split source text into cached line strings.
/// @param source Full source text to split on newline characters.
/// @return Source lines without line terminators.
/// @details Mirrors std::getline semantics for trailing newlines while preserving
///          one empty line for an entirely empty source buffer.
std::vector<std::string> splitSourceLines(std::string_view source) {
    std::vector<std::string> lines;
    std::string current;
    for (char ch : source) {
        if (ch == '\n') {
            stripTrailingCarriageReturn(current);
            lines.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty() || source.empty()) {
        stripTrailingCarriageReturn(current);
        lines.push_back(std::move(current));
    }
    return lines;
}
} // namespace

/// @brief Register a file path and assign it a stable identifier.
///
/// @details Normalises @p path using @ref normalizePath before deduplicating it
///          against previously seen entries.  Identifiers start at one so that
///          zero can unambiguously signal "unknown".  When the identifier space
///          would overflow, the helper returns zero so callers can surface a fatal
///          configuration error in their own diagnostic context.  Stored strings live inside the
///          manager's
///          @ref files_ container for the remainder of the manager's lifetime,
///          making it safe to hand out @ref std::string_view references.
///
/// @param path Filesystem path to normalize and store.
/// @return Identifier (>0) representing the stored path, or zero on overflow.
uint32_t SourceManager::addFile(std::string path) {
    std::string normalized = normalizePath(path);
    std::filesystem::path diskPath = makeDiskPath(path);

    std::lock_guard lock(mutex_);
    if (auto it = path_to_id_.find(normalized); it != path_to_id_.end())
        return it->second;

    if (next_file_id_ > std::numeric_limits<uint32_t>::max()) {
        return 0;
    }

    path_to_id_.reserve(path_to_id_.size() + 1);
    const uint32_t file_id = static_cast<uint32_t>(next_file_id_);
    files_.push_back(std::move(normalized));
    try {
        disk_paths_.push_back(std::move(diskPath));
    } catch (...) {
        files_.pop_back();
        throw;
    }
    try {
        path_to_id_.emplace(files_.back(), file_id);
    } catch (...) {
        disk_paths_.pop_back();
        files_.pop_back();
        throw;
    }
    ++next_file_id_;
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
std::string_view SourceManager::getPath(uint32_t file_id) const {
    std::lock_guard lock(mutex_);
    return getPathLocked(file_id);
}

/// @brief Retrieve a single source line, loading and caching the file on demand.
///
/// @details The first request for a given @p file_id reads the entire file from
///          disk and splits it into a per-line cache; subsequent requests are
///          served from that cache.  When the file cannot be opened an empty
///          line vector is cached so repeated lookups do not retry the failing
///          I/O.  Identifier zero, line zero, and out-of-range line numbers all
///          yield an empty view rather than throwing.  The returned view is
///          backed by the cache and stays valid for the manager's lifetime.
///
/// @param file_id 1-based identifier previously returned by addFile().
/// @param line 1-based line number to retrieve.
/// @return The line text without its terminator, or empty view if unavailable.
std::string_view SourceManager::getLine(uint32_t file_id, uint32_t line) const {
    if (file_id == 0 || line == 0)
        return {};

    std::lock_guard lock(mutex_);
    const auto *lines = ensureLineCacheLocked(file_id);
    if (!lines || line > lines->size())
        return {};
    return (*lines)[line - 1];
}

/// @brief Check whether a line exists for a registered source file.
///
/// @details Uses the same lazy cache path as @ref getLine but returns true for
///          existing blank lines, letting diagnostic printers render an empty
///          source line instead of treating it as missing context.
///
/// @param file_id 1-based identifier previously returned by addFile().
/// @param line 1-based line number to query.
/// @return True when @p line exists in the cached source text.
bool SourceManager::hasLine(uint32_t file_id, uint32_t line) const {
    if (file_id == 0 || line == 0)
        return false;

    std::lock_guard lock(mutex_);
    const auto *lines = ensureLineCacheLocked(file_id);
    return lines && line <= lines->size();
}

/// @brief Invalidate cached source text for a single file identifier.
///
/// @details Erases both successful and failed read caches. The next @ref getLine
///          or @ref hasLine call will try to load the file from disk again unless
///          a caller first seeds replacement text with @ref setSource.
///
/// @param file_id 1-based identifier previously returned by addFile().
void SourceManager::invalidateSource(uint32_t file_id) const {
    if (file_id == 0)
        return;

    std::lock_guard lock(mutex_);
    lineCache_.erase(file_id);
}

/// @brief Seed the line cache for a file id directly from in-memory text.
///
/// @details Lets callers associate source text with a @p file_id without the
///          file existing on disk — useful for REPL buffers, generated code, and
///          tests.  The text is split on `'\n'`, with a trailing `'\r'` stripped
///          from each line so Windows CRLF input caches identically to LF input.
///          A final unterminated segment (or an entirely empty @p source) is
///          recorded as a trailing line.  Any previously cached lines for the id
///          are replaced.  Identifier zero is ignored.
///
/// @param file_id Identifier previously returned by addFile().
/// @param source Full source text to split into cached lines.
void SourceManager::setSource(uint32_t file_id, std::string source) {
    std::lock_guard lock(mutex_);
    if (!isRegisteredFileId(file_id))
        return;

    lineCache_[file_id] = splitSourceLines(source);
}

/// @brief Check whether a file identifier is in the registered range.
/// @param file_id Candidate 1-based file identifier.
/// @return True when @p file_id maps to stored path metadata.
bool SourceManager::isRegisteredFileId(uint32_t file_id) const noexcept {
    return file_id != 0 && file_id <= files_.size();
}

/// @brief Resolve a registered file id to its display path without locking.
/// @param file_id Candidate 1-based file identifier.
/// @return Stored display path, or empty for invalid ids.
std::string_view SourceManager::getPathLocked(uint32_t file_id) const {
    if (!isRegisteredFileId(file_id))
        return {};
    return files_[file_id - 1];
}

/// @brief Load or retrieve cached source lines without acquiring the mutex.
/// @param file_id Registered 1-based file identifier.
/// @return Pointer to the cached line vector, or nullptr if @p file_id is invalid.
const std::vector<std::string> *SourceManager::ensureLineCacheLocked(uint32_t file_id) const {
    if (!isRegisteredFileId(file_id))
        return nullptr;

    auto it = lineCache_.find(file_id);
    if (it != lineCache_.end())
        return &it->second;

    std::vector<std::string> lines;
    std::ifstream f(disk_paths_[file_id - 1]);
    if (f) {
        std::string buf;
        while (std::getline(f, buf)) {
            stripTrailingCarriageReturn(buf);
            lines.push_back(std::move(buf));
        }
    }

    auto inserted = lineCache_.emplace(file_id, std::move(lines)).first;
    return &inserted->second;
}
} // namespace il::support
