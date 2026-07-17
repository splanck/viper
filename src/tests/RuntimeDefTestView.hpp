//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/RuntimeDefTestView.hpp
// Purpose: Materialize an include-expanded textual view of runtime.def for tests.
// Key invariants:
//   - Includes are resolved relative to the including definition file.
//   - Definition files cannot escape the canonical runtime definition root.
// Ownership/Lifetime:
//   - Returned strings own all loaded definition text.
//   - The helper performs read-only filesystem access to repository sources.
// Links: src/il/runtime/runtime.def, src/tools/rtgen/rtgen.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef ZANNA_SOURCE_DIR
#define ZANNA_SOURCE_DIR "."
#endif

namespace zanna::tests {
namespace detail {

/// @brief Return a quoted include target or an empty string for a non-include line.
inline std::string runtimeDefinitionInclude(std::string_view line) {
    std::size_t cursor = 0;
    while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])))
        ++cursor;
    constexpr std::string_view directive = "#include";
    if (line.substr(cursor, directive.size()) != directive)
        return {};
    cursor += directive.size();
    while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])))
        ++cursor;
    if (cursor >= line.size() || line[cursor] != '"')
        return {};
    const std::size_t end = line.find('"', cursor + 1U);
    if (end == std::string_view::npos)
        throw std::runtime_error("unterminated include in runtime definition test view");
    return std::string(line.substr(cursor + 1U, end - cursor - 1U));
}

/// @brief Check that @p candidate is contained by @p root after canonicalization.
inline bool runtimeDefinitionIsWithin(const std::filesystem::path &candidate,
                                      const std::filesystem::path &root) {
    const std::filesystem::path relative = candidate.lexically_relative(root);
    if (relative.empty())
        return candidate == root;
    return *relative.begin() != "..";
}

/// @brief Recursively append one runtime definition file and its includes.
inline void appendRuntimeDefinition(const std::filesystem::path &path,
                                    const std::filesystem::path &root,
                                    std::set<std::filesystem::path> &active,
                                    std::set<std::filesystem::path> &loaded,
                                    std::string &output) {
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
    if (!runtimeDefinitionIsWithin(canonical, root))
        throw std::runtime_error("runtime definition include escapes definition root: " +
                                 canonical.string());
    if (active.contains(canonical))
        throw std::runtime_error("runtime definition include cycle: " + canonical.string());
    if (!loaded.insert(canonical).second)
        throw std::runtime_error("duplicate runtime definition include: " + canonical.string());

    std::ifstream input(canonical, std::ios::binary);
    if (!input)
        throw std::runtime_error("unable to read runtime definition: " + canonical.string());

    active.insert(canonical);
    std::string line;
    while (std::getline(input, line)) {
        const std::string include = runtimeDefinitionInclude(line);
        if (!include.empty()) {
            appendRuntimeDefinition(
                canonical.parent_path() / include, root, active, loaded, output);
            continue;
        }
        output += line;
        output.push_back('\n');
    }
    active.erase(canonical);
}

} // namespace detail

/// @brief Load the canonical runtime definition set as one textual stream.
/// @details This replaces tests that read only the root manifest while avoiding
///          a compile-time expansion of thousands of X-macro invocations.
inline std::string runtimeDefinitionText() {
    const std::filesystem::path root = std::filesystem::weakly_canonical(
        std::filesystem::path(ZANNA_SOURCE_DIR) / "src/il/runtime");
    std::set<std::filesystem::path> active;
    std::set<std::filesystem::path> loaded;
    std::string output;
    output.reserve(2U * 1024U * 1024U);
    detail::appendRuntimeDefinition(root / "runtime.def", root, active, loaded, output);
    return output;
}

} // namespace zanna::tests
