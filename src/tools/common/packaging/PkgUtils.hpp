//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PkgUtils.hpp
// Purpose: Shared utility functions for all platform package builders —
//          file reading, name normalization, and common helpers.
//
// Key invariants:
//   - readFile() throws std::runtime_error on I/O failure.
//   - Name normalizers produce lowercase ASCII with no spaces.
//
// Ownership/Lifetime:
//   - Pure functions, no state.
//
// Links: MacOSPackageBuilder.cpp, LinuxPackageBuilder.cpp,
//        WindowsPackageBuilder.cpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace viper::pkg
{

/// @brief Read a file into a byte vector.
/// @throws std::runtime_error on open or read failure.
inline std::vector<uint8_t> readFile(const std::string &path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("cannot read file: " + path);
    auto size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char *>(data.data()), size);
    if (!f || f.gcount() != size)
        throw std::runtime_error("incomplete read of: " + path);
    return data;
}

/// @brief Normalize a project name to a lowercase executable name.
///
/// Spaces become underscores, all chars lowered.
/// e.g. "Viper IDE" -> "viper_ide"
inline std::string normalizeExecName(const std::string &name)
{
    std::string result;
    result.reserve(name.size());
    for (char c : name)
    {
        if (c == ' ')
            result.push_back('_');
        else
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

/// @brief Normalize a project name to a Debian-style package name.
///
/// Spaces and underscores become hyphens, all chars lowered.
/// e.g. "Viper IDE" -> "viper-ide"
inline std::string normalizeDebName(const std::string &name)
{
    std::string result;
    result.reserve(name.size());
    for (char c : name)
    {
        if (c == ' ' || c == '_')
            result.push_back('-');
        else
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

/// @brief Safely iterate a directory tree, skipping symlinks that escape the
///        project root and handling permission errors gracefully.
///
/// The callback receives each directory_entry that is either a regular file
/// or a directory (symlinks are resolved and checked).
///
/// @param root       The directory to recurse into.
/// @param projectRoot  The project root boundary — symlinks resolving outside
///                     this path are skipped with a warning.
/// @param callback   Called for each safe entry.
inline void safeDirectoryIterate(
    const std::filesystem::path &root,
    const std::filesystem::path &projectRoot,
    const std::function<void(const std::filesystem::directory_entry &)> &callback)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path canonicalRoot = fs::canonical(projectRoot, ec);
    if (ec)
        canonicalRoot = projectRoot; // Fallback if canonical fails

    for (auto it = fs::recursive_directory_iterator(
             root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator();
         it.increment(ec))
    {
        if (ec)
        {
            std::cerr << "warning: cannot access '" << it->path().string()
                      << "', skipping: " << ec.message() << "\n";
            ec.clear();
            continue;
        }

        const auto &entry = *it;

        // Check for symlinks escaping the project root
        if (entry.is_symlink())
        {
            fs::path resolved = fs::canonical(entry.path(), ec);
            if (ec)
            {
                std::cerr << "warning: cannot resolve symlink '" << entry.path().string()
                          << "', skipping\n";
                ec.clear();
                if (entry.is_directory())
                    it.disable_recursion_pending();
                continue;
            }
            // Check if resolved path is within project root
            auto mismatch =
                std::mismatch(canonicalRoot.begin(), canonicalRoot.end(), resolved.begin());
            if (mismatch.first != canonicalRoot.end())
            {
                std::cerr << "warning: symlink '" << entry.path().string()
                          << "' escapes project root, skipping\n";
                if (entry.is_directory())
                    it.disable_recursion_pending();
                continue;
            }
        }

        callback(entry);
    }
}

} // namespace viper::pkg
