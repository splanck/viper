//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/common/Filesystem.hpp
// Purpose: Convert between native filesystem paths and Zanna's UTF-8 path strings.
// Key invariants:
//   - Narrow tool/runtime path strings are UTF-8, never the Windows active code page.
//   - Native path conversion preserves every encoded code point without lossy fallback.
// Ownership/Lifetime: Returned strings and paths own their storage.
// Links: src/tools/common/native_compiler.cpp, src/codegen/common/LinkerSupport.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

namespace zanna::filesystem {

/// @brief Decode a Zanna UTF-8 path into the host filesystem's native representation.
inline std::filesystem::path pathFromUtf8(std::string_view path) {
    std::u8string encoded(path.size(), u8'\0');
    if (!path.empty())
        std::memcpy(encoded.data(), path.data(), path.size());
    return std::filesystem::path(encoded);
}

/// @brief Encode a native filesystem path using Zanna's UTF-8 path convention.
inline std::string pathToUtf8(const std::filesystem::path &path) {
    const std::u8string encoded = path.u8string();
    return std::string(reinterpret_cast<const char *>(encoded.data()), encoded.size());
}

/// @brief Encode a native path as UTF-8 with portable forward-slash separators.
inline std::string genericPathToUtf8(const std::filesystem::path &path) {
    const std::u8string encoded = path.generic_u8string();
    return std::string(reinterpret_cast<const char *>(encoded.data()), encoded.size());
}

} // namespace zanna::filesystem
