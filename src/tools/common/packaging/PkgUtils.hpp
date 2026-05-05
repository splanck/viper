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

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace viper::pkg {

/// @brief Read a file into a byte vector.
/// @throws std::runtime_error on open or read failure.
inline std::vector<uint8_t> readFile(const std::string &path) {
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
inline std::string normalizeExecName(const std::string &name) {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (c == ' ')
            result.push_back('_');
        else if (std::isalnum(uc) || c == '_' || c == '-' || c == '.')
            result.push_back(static_cast<char>(std::tolower(uc)));
        else
            throw std::runtime_error("executable name contains an invalid character: '" + name +
                                     "'");
    }
    if (result.empty() || result == "." || result == "..")
        throw std::runtime_error("executable name must not be empty or special: '" + name + "'");
    return result;
}

/// @brief Normalize a project name to a Debian-style package name.
///
/// Spaces and underscores become hyphens, all chars lowered.
/// e.g. "Viper IDE" -> "viper-ide"
inline std::string normalizeDebName(const std::string &name) {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (c == ' ' || c == '_')
            result.push_back('-');
        else if (std::isalnum(uc) || c == '+' || c == '-' || c == '.')
            result.push_back(static_cast<char>(std::tolower(uc)));
        else
            throw std::runtime_error("Debian package name contains an invalid character: '" +
                                     name + "'");
    }
    if (result.size() < 2 || !std::isalnum(static_cast<unsigned char>(result.front())))
        throw std::runtime_error("Debian package name must start with an alphanumeric character "
                                 "and contain at least two characters: '" +
                                 name + "'");
    return result;
}

inline void rejectControlChars(const std::string &value, const char *fieldName) {
    for (char c : value) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F) {
            throw std::runtime_error(std::string(fieldName) +
                                     " must not contain control characters");
        }
    }
}

inline void rejectLineBreaks(const std::string &value, const char *fieldName) {
    if (value.find('\n') != std::string::npos || value.find('\r') != std::string::npos) {
        throw std::runtime_error(std::string(fieldName) + " must not contain line breaks");
    }
}

inline void validateDebVersion(const std::string &version, const char *fieldName = "version") {
    if (version.empty())
        throw std::runtime_error(std::string(fieldName) + " must not be empty");
    for (char c : version) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '.' && c != '+' && c != '~' && c != '-' && c != ':') {
            throw std::runtime_error(std::string(fieldName) +
                                     " contains a character invalid for Debian versions: '" +
                                     version + "'");
        }
    }
}

inline void validateRpmVersion(const std::string &version, const char *fieldName = "version") {
    if (version.empty())
        throw std::runtime_error(std::string(fieldName) + " must not be empty");
    for (char c : version) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '.' && c != '_' && c != '+' && c != '~' && c != '^') {
            throw std::runtime_error(std::string(fieldName) +
                                     " contains a character invalid for RPM versions: '" +
                                     version + "'");
        }
    }
}

inline void validateSingleLineField(const std::string &value, const char *fieldName) {
    rejectLineBreaks(value, fieldName);
    rejectControlChars(value, fieldName);
}

inline void validatePackageIdentifier(const std::string &identifier,
                                      const char *fieldName = "package identifier") {
    if (identifier.empty())
        return;
    rejectLineBreaks(identifier, fieldName);
    bool lastWasDot = true;
    for (char c : identifier) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '-' || c == '_') {
            lastWasDot = false;
            continue;
        }
        if (c == '.' && !lastWasDot) {
            lastWasDot = true;
            continue;
        }
        throw std::runtime_error(std::string(fieldName) + " is not a valid identifier: '" +
                                 identifier + "'");
    }
    if (lastWasDot)
        throw std::runtime_error(std::string(fieldName) + " must not end with '.': '" +
                                 identifier + "'");
}

inline void validateFileAssociation(const std::string &extension,
                                    const std::string &description,
                                    const std::string &mimeType) {
    if (extension.empty())
        throw std::runtime_error("file association extension must not be empty");
    validateSingleLineField(description, "file association description");
    validateSingleLineField(mimeType, "file association MIME type");
    for (char c : extension) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '.' && c != '_' && c != '-' && c != '+')
            throw std::runtime_error("file association extension contains an invalid character: '" +
                                     extension + "'");
    }
    const std::size_t slash = mimeType.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= mimeType.size())
        throw std::runtime_error("file association MIME type must be type/subtype: '" +
                                 mimeType + "'");
    if (mimeType.find('/', slash + 1) != std::string::npos)
        throw std::runtime_error("file association MIME type must contain only one '/': '" +
                                 mimeType + "'");
    for (char c : mimeType) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '/' && c != '-' && c != '+' && c != '.' && c != '_' &&
            c != '!' && c != '#' && c != '$' && c != '&' && c != '^') {
            throw std::runtime_error("file association MIME type contains an invalid character: '" +
                                     mimeType + "'");
        }
    }
}

inline void validateWindowsFileName(const std::string &name, const char *fieldName) {
    if (name.empty())
        throw std::runtime_error(std::string(fieldName) + " must not be empty");
    if (name.back() == ' ' || name.back() == '.')
        throw std::runtime_error(std::string(fieldName) + " must not end in space or dot: '" +
                                 name + "'");
    for (char c : name) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' ||
            c == '\\' || c == '|' || c == '?' || c == '*') {
            throw std::runtime_error(std::string(fieldName) +
                                     " contains a character invalid on Windows: '" + name + "'");
        }
    }

    std::string lower;
    lower.reserve(name.size());
    for (char c : name)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    const std::string stem = lower.substr(0, lower.find('.'));
    for (std::string_view reserved :
         {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6",
          "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6",
          "lpt7", "lpt8", "lpt9"}) {
        if (stem == reserved)
            throw std::runtime_error(std::string(fieldName) +
                                     " must not be a Windows reserved device name: '" + name +
                                     "'");
    }
}

inline bool isPathWithin(const std::filesystem::path &root, const std::filesystem::path &path) {
    auto rootIt = root.begin();
    auto pathIt = path.begin();
    for (; rootIt != root.end(); ++rootIt, ++pathIt) {
        if (pathIt == path.end() || *rootIt != *pathIt)
            return false;
    }
    return true;
}

/// @brief Normalize a package-relative path and reject archive escapes.
///
/// Converts backslashes to forward slashes and rejects absolute paths, parent
/// traversal, empty segments, drive-qualified paths, and control characters.
/// Returns an empty string for "." or an empty input.
inline std::string sanitizePackageRelativePath(const std::string &raw,
                                               const char *fieldName = "package path") {
    std::string normalized = raw;
    for (char &c : normalized) {
        if (c == '\\')
            c = '/';
    }

    std::string collapsed;
    collapsed.reserve(normalized.size());
    bool lastWasSlash = false;
    for (char c : normalized) {
        if (c == '/') {
            if (lastWasSlash)
                continue;
            lastWasSlash = true;
        } else {
            lastWasSlash = false;
        }
        collapsed.push_back(c);
    }
    normalized.swap(collapsed);

    while (!normalized.empty() && normalized.back() == '/')
        normalized.pop_back();

    if (normalized.empty() || normalized == ".")
        return "";

    if (normalized.front() == '/')
        throw std::runtime_error(std::string(fieldName) + " must be relative: '" + raw + "'");

    if (normalized.size() >= 2 && std::isalpha(static_cast<unsigned char>(normalized[0])) &&
        normalized[1] == ':') {
        throw std::runtime_error(std::string(fieldName) + " must not use a drive prefix: '" + raw +
                                 "'");
    }

    std::string out;
    std::size_t pos = 0;
    while (pos < normalized.size()) {
        std::size_t next = normalized.find('/', pos);
        std::string_view segment = next == std::string::npos
                                       ? std::string_view(normalized).substr(pos)
                                       : std::string_view(normalized).substr(pos, next - pos);

        if (segment.empty())
            throw std::runtime_error(std::string(fieldName) + " contains an empty path segment: '" +
                                     raw + "'");
        if (segment == "." || segment == "..")
            throw std::runtime_error(std::string(fieldName) +
                                     " must not contain '.' or '..' segments: '" + raw + "'");

        for (char c : segment) {
            unsigned char uc = static_cast<unsigned char>(c);
            if (uc < 0x20 || c == ':') {
                throw std::runtime_error(std::string(fieldName) +
                                         " contains an invalid character: '" + raw + "'");
            }
        }

        if (!out.empty())
            out.push_back('/');
        out.append(segment.data(), segment.size());

        if (next == std::string::npos)
            break;
        pos = next + 1;
    }

    return out;
}

/// @brief Join two package-relative paths and sanitize the result.
inline std::string joinPackageRelativePath(const std::string &base,
                                           const std::string &leaf,
                                           const char *fieldName = "package path") {
    const std::string cleanBase = sanitizePackageRelativePath(base, fieldName);
    const std::string cleanLeaf = sanitizePackageRelativePath(leaf, fieldName);
    if (cleanBase.empty())
        return cleanLeaf;
    if (cleanLeaf.empty())
        return cleanBase;
    return sanitizePackageRelativePath(cleanBase + "/" + cleanLeaf, fieldName);
}

inline std::filesystem::path resolvePackageSourcePath(const std::filesystem::path &projectRoot,
                                                      const std::string &raw,
                                                      const char *fieldName) {
    namespace fs = std::filesystem;
    const std::string clean = sanitizePackageRelativePath(raw, fieldName);
    if (clean.empty())
        throw std::runtime_error(std::string(fieldName) + " must not be empty");

    std::error_code ec;
    const fs::path canonicalRoot = fs::canonical(projectRoot, ec);
    if (ec)
        throw std::runtime_error("cannot resolve project root for packaging: " +
                                 projectRoot.string());

    fs::path candidate = (canonicalRoot / fs::path(clean)).lexically_normal();
    const fs::path weakCandidate = fs::weakly_canonical(candidate, ec);
    if (!ec && !isPathWithin(canonicalRoot, weakCandidate)) {
        throw std::runtime_error(std::string(fieldName) + " escapes the project root: '" + raw +
                                 "'");
    }
    ec.clear();
    if (!fs::exists(candidate, ec))
        return ec ? candidate : weakCandidate;

    const fs::path resolved = fs::canonical(candidate, ec);
    if (ec)
        throw std::runtime_error(std::string("cannot resolve ") + fieldName + ": " + raw);
    if (!isPathWithin(canonicalRoot, resolved)) {
        throw std::runtime_error(std::string(fieldName) + " escapes the project root: '" + raw +
                                 "'");
    }
    return resolved;
}

inline std::vector<uint16_t> utf8ToUtf16CodeUnits(const std::string &text) {
    std::vector<uint16_t> out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t cp = 0;
        size_t extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
            if (cp == 0)
                throw std::runtime_error("invalid overlong UTF-8 sequence");
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            throw std::runtime_error("invalid UTF-8 leading byte");
        }
        if (i + extra >= text.size())
            throw std::runtime_error("truncated UTF-8 sequence");
        for (size_t j = 1; j <= extra; ++j) {
            unsigned char cc = static_cast<unsigned char>(text[i + j]);
            if ((cc & 0xC0) != 0x80)
                throw std::runtime_error("invalid UTF-8 continuation byte");
            cp = (cp << 6) | (cc & 0x3F);
        }
        if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
            (extra == 3 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF)) {
            throw std::runtime_error("invalid UTF-8 code point");
        }
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<uint16_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<uint16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
        }
        i += extra + 1;
    }
    return out;
}

inline size_t utf16CodeUnitCountFromUtf8(const std::string &text) {
    return utf8ToUtf16CodeUnits(text).size();
}

inline std::vector<uint8_t> utf8ToUtf16LEBytes(const std::string &text, bool nulTerminate = true) {
    const auto units = utf8ToUtf16CodeUnits(text);
    if (units.size() > static_cast<size_t>(std::numeric_limits<uint16_t>::max()))
        throw std::runtime_error("UTF-16 string is too long");
    std::vector<uint8_t> bytes;
    bytes.reserve((units.size() + (nulTerminate ? 1 : 0)) * 2);
    for (uint16_t unit : units) {
        bytes.push_back(static_cast<uint8_t>(unit & 0xFF));
        bytes.push_back(static_cast<uint8_t>((unit >> 8) & 0xFF));
    }
    if (nulTerminate) {
        bytes.push_back(0);
        bytes.push_back(0);
    }
    return bytes;
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
    const std::function<void(const std::filesystem::directory_entry &)> &callback) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path canonicalRoot = fs::canonical(projectRoot, ec);
    if (ec)
        canonicalRoot = projectRoot; // Fallback if canonical fails

    for (auto it = fs::recursive_directory_iterator(
             root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec) {
            std::cerr << "warning: cannot access '" << it->path().string()
                      << "', skipping: " << ec.message() << "\n";
            ec.clear();
            continue;
        }

        const auto &entry = *it;

        // Check for symlinks escaping the project root
        if (entry.is_symlink()) {
            fs::path resolved = fs::canonical(entry.path(), ec);
            if (ec) {
                std::cerr << "warning: cannot resolve symlink '" << entry.path().string()
                          << "', skipping\n";
                ec.clear();
                if (entry.is_directory())
                    it.disable_recursion_pending();
                continue;
            }
            // Check if resolved path is within project root.
            if (!isPathWithin(canonicalRoot, resolved)) {
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
