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
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "PackageConfig.hpp"

namespace viper::pkg {

inline constexpr uint64_t kMaxPackageFileBytes = 0xFFFFFFFFull;

/// @brief Read a file into a byte vector.
/// @throws std::runtime_error on open or read failure.
inline std::vector<uint8_t> readFile(const std::string &path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("cannot read file: " + path);
    auto size = f.tellg();
    if (size < 0)
        throw std::runtime_error("cannot determine file size: " + path);
    const auto size64 = static_cast<uint64_t>(size);
    if (size64 > kMaxPackageFileBytes)
        throw std::runtime_error("file is too large for VAPS package formats: " + path);
    if (size64 > static_cast<uint64_t>(std::vector<uint8_t>().max_size()))
        throw std::runtime_error("file is too large to read into memory: " + path);
    f.seekg(0);
    if (!f)
        throw std::runtime_error("cannot seek file: " + path);
    std::vector<uint8_t> data(static_cast<size_t>(size64));
    if (data.empty())
        return data;
    f.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!f || f.gcount() != static_cast<std::streamsize>(data.size()))
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
    if (result.back() == '-' || result.back() == '.')
        throw std::runtime_error("Debian package name must not end with '-' or '.': '" + name +
                                 "'");
    return result;
}

/// @brief Strip leading and trailing ASCII whitespace (space, tab, CR, LF).
inline std::string trimAsciiWhitespace(std::string value) {
    const std::size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return {};
    const std::size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

/// @brief Return a copy of value with all ASCII letters converted to lowercase.
inline std::string lowerAsciiCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

/// @brief Throw if value contains any ASCII control character (0x00-0x1F or 0x7F).
/// Used to keep metadata strings safe for embedding in package control files.
inline void rejectControlChars(const std::string &value, const char *fieldName) {
    for (char c : value) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F) {
            throw std::runtime_error(std::string(fieldName) +
                                     " must not contain control characters");
        }
    }
}

/// @brief Throw if value contains CR or LF characters.
/// Single-line metadata fields (Name, Version, etc.) must not span multiple lines.
inline void rejectLineBreaks(const std::string &value, const char *fieldName) {
    if (value.find('\n') != std::string::npos || value.find('\r') != std::string::npos) {
        throw std::runtime_error(std::string(fieldName) + " must not contain line breaks");
    }
}

/// @brief Validate a string that must be a single, control-character-free line.
/// Combines rejectLineBreaks + rejectControlChars in one call.
inline void validateSingleLineField(const std::string &value, const char *fieldName) {
    rejectLineBreaks(value, fieldName);
    rejectControlChars(value, fieldName);
}

/// @brief Validate that arch is one of the two supported Viper target architectures ("x64", "arm64").
inline void validateToolchainArchitecture(const std::string &arch,
                                          const char *fieldName = "toolchain architecture") {
    validateSingleLineField(arch, fieldName);
    if (arch != "x64" && arch != "arm64")
        throw std::runtime_error(std::string(fieldName) + " must be x64 or arm64: '" + arch + "'");
}

/// @brief Validate that platform is one of "windows", "macos", or "linux".
inline void validateToolchainPlatform(const std::string &platform,
                                      const char *fieldName = "toolchain platform") {
    validateSingleLineField(platform, fieldName);
    if (platform != "windows" && platform != "macos" && platform != "linux") {
        throw std::runtime_error(std::string(fieldName) +
                                 " must be windows, macos, or linux: '" + platform + "'");
    }
}

/// @brief Validate a Debian package version string per Policy Manual §5.6.12.
/// Accepts the full [epoch:]upstream[-revision] format. Throws on any invalid
/// character, empty component, non-numeric epoch, or wrong starting digit.
inline void validateDebVersion(const std::string &version, const char *fieldName = "version") {
    const auto fail = [&](const std::string &why) {
        throw std::runtime_error(std::string(fieldName) + " is not a valid Debian version: '" +
                                 version + "' (" + why + ")");
    };
    if (version.empty())
        fail("empty");

    int colonCount = 0;
    std::size_t colon = std::string::npos;
    for (std::size_t i = 0; i < version.size(); ++i) {
        const char c = version[i];
        unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '.' && c != '+' && c != '~' && c != '-' && c != ':') {
            fail("invalid character");
        }
        if (c == ':') {
            ++colonCount;
            colon = i;
        }
    }
    if (colonCount > 1)
        fail("multiple epochs");
    std::size_t upstreamStart = 0;
    if (colon != std::string::npos) {
        if (colon == 0)
            fail("empty epoch");
        for (std::size_t i = 0; i < colon; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(version[i])))
                fail("epoch must be numeric");
        }
        upstreamStart = colon + 1;
    }
    if (upstreamStart >= version.size())
        fail("empty upstream version");
    const std::size_t dash = version.find('-', upstreamStart);
    const std::size_t upstreamEnd = dash == std::string::npos ? version.size() : dash;
    if (upstreamEnd == upstreamStart)
        fail("empty upstream version");
    if (!std::isdigit(static_cast<unsigned char>(version[upstreamStart])))
        fail("upstream version must start with a digit");
    for (std::size_t i = upstreamStart; i < upstreamEnd; ++i) {
        const char c = version[i];
        unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '.' && c != '+' && c != '~' && c != ':')
            fail("invalid upstream character");
    }
    if (dash != std::string::npos) {
        if (dash + 1 >= version.size())
            fail("empty Debian revision");
        for (std::size_t i = dash + 1; i < version.size(); ++i) {
            const char c = version[i];
            unsigned char uc = static_cast<unsigned char>(c);
            if (!std::isalnum(uc) && c != '.' && c != '+' && c != '~')
                fail("invalid Debian revision character");
        }
    }
}

/// @brief Parse a dotted-numeric version string ("1.2.3.4") into a uint32_t vector.
/// Each component is validated to be purely numeric and ≤ 65535.
/// Throws on empty input, non-numeric characters, or a trailing dot.
inline std::vector<uint32_t> parseDottedNumericVersionParts(const std::string &version,
                                                            const char *fieldName) {
    if (version.empty())
        throw std::runtime_error(std::string(fieldName) + " must not be empty");
    std::vector<uint32_t> parts;
    uint32_t current = 0;
    bool sawDigit = false;
    for (char c : version) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isdigit(uc)) {
            sawDigit = true;
            const uint32_t digit = static_cast<uint32_t>(c - '0');
            if (current > (65535u - digit) / 10u)
                throw std::runtime_error(std::string(fieldName) +
                                         " component exceeds 65535: '" + version + "'");
            current = current * 10u + digit;
            continue;
        }
        if (c == '.' && sawDigit) {
            parts.push_back(current);
            current = 0;
            sawDigit = false;
            continue;
        }
        throw std::runtime_error(std::string(fieldName) +
                                 " must be a dotted numeric version: '" + version + "'");
    }
    if (!sawDigit)
        throw std::runtime_error(std::string(fieldName) +
                                 " must be a dotted numeric version: '" + version + "'");
    parts.push_back(current);
    return parts;
}

/// @brief Validate a dotted-numeric version string with 2-4 components (e.g. "1.0" or "1.2.3.4").
inline void validateDottedNumericVersion(const std::string &version, const char *fieldName) {
    const auto parts = parseDottedNumericVersionParts(version, fieldName);
    if (parts.size() < 2 || parts.size() > 4)
        throw std::runtime_error(std::string(fieldName) +
                                 " must have 2 to 4 numeric components: '" + version + "'");
}

/// @brief Validate a Debian dependency specification per Policy Manual §7.1.
/// Accepts the full pkgname[:arch] [(op version)] [arch-list] [profile-list] | alt ...
/// syntax. Throws with a descriptive error on any violation.
inline void validateDebDependency(const std::string &dependency) {
    const std::string dep = trimAsciiWhitespace(dependency);
    if (dep.empty())
        throw std::runtime_error("package dependency must not be empty");
    validateSingleLineField(dep, "package dependency");

    auto isPkgChar = [](char c) {
        unsigned char uc = static_cast<unsigned char>(c);
        return std::islower(uc) || std::isdigit(uc) || c == '+' || c == '-' || c == '.';
    };
    auto isArchChar = [](char c) {
        unsigned char uc = static_cast<unsigned char>(c);
        return std::islower(uc) || std::isdigit(uc) || c == '-';
    };
    auto skipSpaces = [&](size_t &pos) {
        while (pos < dep.size() && (dep[pos] == ' ' || dep[pos] == '\t'))
            ++pos;
    };
    auto parseName = [&](size_t &pos) {
        const size_t start = pos;
        if (pos >= dep.size() ||
            (!std::islower(static_cast<unsigned char>(dep[pos])) &&
             !std::isdigit(static_cast<unsigned char>(dep[pos]))))
            throw std::runtime_error("package dependency has invalid package name: '" + dep + "'");
        while (pos < dep.size() && isPkgChar(dep[pos]))
            ++pos;
        if (pos - start < 2)
            throw std::runtime_error("package dependency package name is too short: '" + dep + "'");
        if (dep[pos - 1] == '-' || dep[pos - 1] == '.')
            throw std::runtime_error("package dependency package name has invalid suffix: '" + dep + "'");
    };
    auto parseArchQualifier = [&](size_t &pos) {
        if (pos >= dep.size() || dep[pos] != ':')
            return;
        ++pos;
        const size_t start = pos;
        while (pos < dep.size() && isArchChar(dep[pos]))
            ++pos;
        if (start == pos)
            throw std::runtime_error("package dependency has empty architecture qualifier: '" + dep +
                                     "'");
    };
    auto parseVersionConstraint = [&](size_t &pos) {
        skipSpaces(pos);
        if (pos >= dep.size() || dep[pos] != '(')
            return;
        ++pos;
        skipSpaces(pos);
        const char *ops[] = {"<<", "<=", "=", ">=", ">>"};
        bool matched = false;
        for (const char *op : ops) {
            const size_t len = std::char_traits<char>::length(op);
            if (dep.compare(pos, len, op) == 0) {
                pos += len;
                matched = true;
                break;
            }
        }
        if (!matched)
            throw std::runtime_error("package dependency has invalid version relation: '" + dep + "'");
        skipSpaces(pos);
        const size_t versionStart = pos;
        while (pos < dep.size() && dep[pos] != ')') {
            unsigned char uc = static_cast<unsigned char>(dep[pos]);
            if (!std::isalnum(uc) && dep[pos] != '.' && dep[pos] != '+' && dep[pos] != '~' &&
                dep[pos] != '-' && dep[pos] != ':')
                throw std::runtime_error("package dependency has invalid version: '" + dep + "'");
            ++pos;
        }
        if (versionStart == pos || pos >= dep.size() || dep[pos] != ')')
            throw std::runtime_error("package dependency has unterminated version constraint: '" +
                                     dep + "'");
        ++pos;
    };
    auto parseBracketList = [&](size_t &pos, char open, char close, const char *what) {
        skipSpaces(pos);
        if (pos >= dep.size() || dep[pos] != open)
            return;
        ++pos;
        bool sawToken = false;
        while (pos < dep.size() && dep[pos] != close) {
            unsigned char uc = static_cast<unsigned char>(dep[pos]);
            if (std::islower(uc) || std::isdigit(uc) || dep[pos] == '-' || dep[pos] == '!' ||
                dep[pos] == ' ' || dep[pos] == '\t') {
                if (std::islower(uc) || std::isdigit(uc))
                    sawToken = true;
                ++pos;
                continue;
            }
            throw std::runtime_error(std::string("package dependency has invalid ") + what +
                                     ": '" + dep + "'");
        }
        if (!sawToken || pos >= dep.size() || dep[pos] != close)
            throw std::runtime_error(std::string("package dependency has unterminated ") + what +
                                     ": '" + dep + "'");
        ++pos;
    };

    size_t pos = 0;
    bool expectAlternative = true;
    while (pos < dep.size()) {
        skipSpaces(pos);
        if (pos >= dep.size())
            break;
        if (!expectAlternative && dep[pos] == '|') {
            expectAlternative = true;
            ++pos;
            continue;
        }
        if (!expectAlternative)
            throw std::runtime_error("package dependency alternatives must be separated by '|': '" +
                                     dep + "'");
        parseName(pos);
        parseArchQualifier(pos);
        parseVersionConstraint(pos);
        parseBracketList(pos, '[', ']', "architecture restriction");
        parseBracketList(pos, '<', '>', "build profile restriction");
        expectAlternative = false;
        skipSpaces(pos);
        if (pos < dep.size() && dep[pos] != '|')
            throw std::runtime_error("package dependency contains trailing invalid syntax: '" + dep +
                                     "'");
    }
    if (expectAlternative)
        throw std::runtime_error("package dependency has empty alternative: '" + dep + "'");
}

/// @brief Return true if category is a registered freedesktop.org desktop category.
/// Used by normalizeDesktopCategories to reject unknown tokens before writing
/// the .desktop Categories= field.
inline bool isKnownDesktopCategory(std::string_view category) {
    static constexpr std::string_view known[] = {
        "AudioVideo", "Audio", "Video", "Development", "Education", "Game", "Graphics",
        "Network", "Office", "Science", "Settings", "System", "Utility", "Building",
        "Debugger", "IDE", "GUIDesigner", "Profiling", "RevisionControl", "Translation",
        "Calendar", "ContactManagement", "Database", "Dictionary", "Chart", "Email",
        "Finance", "FlowChart", "PDA", "ProjectManagement", "Presentation", "Spreadsheet",
        "WordProcessor", "2DGraphics", "VectorGraphics", "RasterGraphics", "3DGraphics",
        "Scanning", "OCR", "Photography", "Publishing", "Viewer", "TextTools",
        "DesktopSettings", "HardwareSettings", "Printing", "PackageManager", "Dialup",
        "InstantMessaging", "Chat", "IRCClient", "Feed", "FileTransfer", "HamRadio",
        "News", "P2P", "RemoteAccess", "Telephony", "TelephonyTools", "VideoConference",
        "WebBrowser", "WebDevelopment", "Midi", "Mixer", "Sequencer", "Tuner", "TV",
        "AudioVideoEditing", "Player", "Recorder", "DiscBurning", "ActionGame",
        "AdventureGame", "ArcadeGame", "BoardGame", "BlocksGame", "CardGame", "KidsGame",
        "LogicGame", "RolePlaying", "Shooter", "Simulation", "SportsGame",
        "StrategyGame", "Art", "Construction", "Music", "Languages",
        "ArtificialIntelligence", "Astronomy", "Biology", "Chemistry", "ComputerScience",
        "DataVisualization", "Economy", "Electricity", "Geography", "Geology",
        "Geoscience", "History", "Humanities", "ImageProcessing", "Literature", "Maps",
        "Math", "NumericalAnalysis", "MedicalSoftware", "Physics", "Robotics",
        "Spirituality", "Sports", "ParallelComputing", "Amusement", "Archiving",
        "Compression", "Electronics", "Emulator", "Engineering", "FileTools",
        "FileManager", "TerminalEmulator", "Filesystem", "Monitor", "Security",
        "Accessibility", "Calculator", "Clock", "TextEditor", "Documentation", "Adult",
        "Core", "KDE", "GNOME", "XFCE", "GTK", "Qt", "ConsoleOnly"};
    for (std::string_view item : known) {
        if (item == category)
            return true;
    }
    return false;
}

/// @brief Parse, validate, and normalize a semicolon-delimited freedesktop.org
/// Categories string. Each token is trimmed of whitespace, checked against the
/// known-categories list, and rejoined with semicolons. Returns "" for empty input.
inline std::string normalizeDesktopCategories(const std::string &categories) {
    if (categories.empty())
        return {};
    validateSingleLineField(categories, "desktop categories");
    std::string out;
    size_t pos = 0;
    while (pos <= categories.size()) {
        const size_t next = categories.find(';', pos);
        std::string token = trimAsciiWhitespace(
            next == std::string::npos ? categories.substr(pos) : categories.substr(pos, next - pos));
        if (!token.empty()) {
            for (char c : token) {
                unsigned char uc = static_cast<unsigned char>(c);
                if (!std::isalnum(uc) && c != '-' && c != '_' && c != '.')
                    throw std::runtime_error("desktop categories contain an invalid character: '" +
                                             categories + "'");
            }
            if (!isKnownDesktopCategory(token))
                throw std::runtime_error("unknown freedesktop desktop category: '" + token + "'");
            out += token;
            out.push_back(';');
        }
        if (next == std::string::npos)
            break;
        pos = next + 1;
    }
    return out;
}

/// @brief Validate desktop categories without returning the normalized form.
inline void validateDesktopCategories(const std::string &categories) {
    (void)normalizeDesktopCategories(categories);
}

/// @brief Validate an RPM version string.
/// RPM versions may contain alphanumerics plus '.', '_', '+', '~', '^'.
/// No epoch/revision parsing — RPM spec file handles those separately.
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

/// @brief Validate a reverse-DNS package identifier (e.g. "com.example.MyApp").
/// Rules: ≤255 chars total; each dot-separated component ≤63 chars, starts and
/// ends with alphanumeric, contains only alphanumerics and '-'; must have ≥1 dot.
/// An empty identifier is accepted (optional field).
inline void validatePackageIdentifier(const std::string &identifier,
                                      const char *fieldName = "package identifier") {
    if (identifier.empty())
        return;
    rejectLineBreaks(identifier, fieldName);
    if (identifier.size() > 255)
        throw std::runtime_error(std::string(fieldName) + " is too long: '" + identifier + "'");
    std::size_t componentStart = 0;
    bool sawDot = false;
    for (std::size_t i = 0; i <= identifier.size(); ++i) {
        if (i < identifier.size() && identifier[i] != '.')
            continue;
        const std::size_t len = i - componentStart;
        if (len == 0)
            throw std::runtime_error(std::string(fieldName) + " has an empty component: '" +
                                     identifier + "'");
        if (len > 63)
            throw std::runtime_error(std::string(fieldName) + " has a component longer than 63 "
                                     "characters: '" +
                                     identifier + "'");
        const char first = identifier[componentStart];
        const char last = identifier[i - 1];
        if (!std::isalnum(static_cast<unsigned char>(first)) ||
            !std::isalnum(static_cast<unsigned char>(last))) {
            throw std::runtime_error(std::string(fieldName) +
                                     " components must start and end with a letter or digit: '" +
                                     identifier + "'");
        }
        for (std::size_t j = componentStart; j < i; ++j) {
            const char c = identifier[j];
            unsigned char uc = static_cast<unsigned char>(c);
            if (!std::isalnum(uc) && c != '-')
                throw std::runtime_error(std::string(fieldName) +
                                         " components may contain only letters, digits, and '-': '" +
                                         identifier + "'");
        }
        if (i < identifier.size()) {
            sawDot = true;
            componentStart = i + 1;
        }
    }
    if (!sawDot)
        throw std::runtime_error(std::string(fieldName) +
                                 " must contain at least one '.' reverse-DNS separator: '" +
                                 identifier + "'");
}

/// @brief Validate a macOS bundle identifier. Delegates to validatePackageIdentifier
/// with a macOS-specific field name for clearer error messages.
inline void validateMacOSBundleIdentifier(const std::string &identifier,
                                          const char *fieldName = "macOS bundle identifier") {
    validatePackageIdentifier(identifier, fieldName);
}

/// @brief Validate a Windows ProgID base string (e.g. "Company.AppName").
/// Must contain at least one '.' separating non-empty components composed of
/// alphanumerics, '_', or '-'. Empty identifier is accepted (optional field).
inline void validateWindowsProgIdBase(const std::string &identifier,
                                      const char *fieldName = "Windows ProgID base") {
    if (identifier.empty())
        return;
    rejectLineBreaks(identifier, fieldName);
    bool sawDot = false;
    bool lastWasDot = true;
    for (char c : identifier) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '_' || c == '-') {
            lastWasDot = false;
            continue;
        }
        if (c == '.' && !lastWasDot) {
            sawDot = true;
            lastWasDot = true;
            continue;
        }
        throw std::runtime_error(std::string(fieldName) +
                                 " is not a valid Windows ProgID base: '" + identifier + "'");
    }
    if (!sawDot || lastWasDot)
        throw std::runtime_error(std::string(fieldName) +
                                 " must contain non-empty dot-separated components: '" +
                                 identifier + "'");
}

/// @brief Normalize a package lifecycle hook script (preinst, postinst, etc.).
/// Converts Windows CRLF line endings to LF, and rejects NUL bytes and other
/// non-printable control characters. Returns "" for an empty input.
inline std::string normalizePackageHookScript(std::string script, const char *fieldName) {
    if (script.empty())
        return {};
    std::string out;
    out.reserve(script.size());
    for (std::size_t i = 0; i < script.size(); ++i) {
        const unsigned char uc = static_cast<unsigned char>(script[i]);
        if (uc == 0)
            throw std::runtime_error(std::string(fieldName) + " must not contain NUL bytes");
        if (script[i] == '\r') {
            if (i + 1 < script.size() && script[i + 1] == '\n')
                continue;
            out.push_back('\n');
            continue;
        }
        if ((uc < 0x20 && script[i] != '\n' && script[i] != '\t') || uc == 0x7F) {
            throw std::runtime_error(std::string(fieldName) +
                                     " must not contain control characters");
        }
        out.push_back(script[i]);
    }
    return out;
}

/// @brief Validate a single file association entry.
/// Checks that the extension starts with '.' and contains valid characters,
/// that description and mimeType are single-line printable strings, and that
/// mimeType has exactly one '/' separating non-empty type and subtype.
inline void validateFileAssociation(const std::string &extension,
                                    const std::string &description,
                                    const std::string &mimeType) {
    if (extension.empty())
        throw std::runtime_error("file association extension must not be empty");
    if (extension.front() != '.' || extension.size() < 2 || extension.back() == '.')
        throw std::runtime_error("file association extension must be a dotted extension such as "
                                 "'.zia': '" +
                                 extension + "'");
    validateSingleLineField(description, "file association description");
    validateSingleLineField(mimeType, "file association MIME type");
    bool sawExtensionChar = false;
    for (char c : extension) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '.' && c != '_' && c != '-' && c != '+')
            throw std::runtime_error("file association extension contains an invalid character: '" +
                                     extension + "'");
        if (std::isalnum(uc) || c == '_' || c == '-' || c == '+')
            sawExtensionChar = true;
    }
    if (!sawExtensionChar)
        throw std::runtime_error("file association extension must contain at least one extension "
                                 "character: '" +
                                 extension + "'");
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

/// @brief Validate all file associations in the package config.
/// Calls validateFileAssociation for each entry and additionally checks for
/// duplicate extensions (case-insensitive comparison).
inline void validatePackageFileAssociations(const std::vector<FileAssoc> &associations) {
    std::set<std::string> seenExtensions;
    for (const auto &assoc : associations) {
        validateFileAssociation(assoc.extension, assoc.description, assoc.mimeType);
        validateSingleLineField(assoc.openCommandArguments,
                                "file association open command arguments");
        const std::string key = lowerAsciiCopy(assoc.extension);
        if (!seenExtensions.insert(key).second) {
            throw std::runtime_error("duplicate file association extension: '" + assoc.extension +
                                     "'");
        }
    }
}

/// @brief Normalize and validate a Windows certificate-store SHA-1 thumbprint.
/// Accepts optional spaces/tabs for paste-friendliness, rejects line breaks, and
/// returns the compact lowercase 40-hex-character form expected by signtool /sha1.
inline std::string normalizeWindowsCertificateThumbprint(const std::string &thumbprint,
                                                         const char *fieldName) {
    if (thumbprint.empty())
        return {};
    rejectLineBreaks(thumbprint, fieldName);
    std::string compact;
    compact.reserve(40);
    for (char c : thumbprint) {
        if (c == ' ' || c == '\t')
            continue;
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isxdigit(uc)) {
            throw std::runtime_error(std::string(fieldName) +
                                     " must contain only SHA-1 hexadecimal digits");
        }
        compact.push_back(static_cast<char>(std::tolower(uc)));
    }
    if (compact.size() != 40) {
        throw std::runtime_error(std::string(fieldName) +
                                 " must be a 40-character SHA-1 thumbprint");
    }
    return compact;
}

/// @brief Validate a Windows certificate-store SHA-1 thumbprint when present.
inline void validateWindowsCertificateThumbprint(const std::string &thumbprint,
                                                 const char *fieldName) {
    (void)normalizeWindowsCertificateThumbprint(thumbprint, fieldName);
}

/// @brief Return true if mode is one of the four supported macOS signing modes.
/// Valid values: "" (unset/default), "none", "preserve", "adhoc", "developer-id".
inline bool isValidMacOSSignModeText(const std::string &mode) {
    return mode.empty() || mode == "none" || mode == "preserve" || mode == "adhoc" ||
           mode == "developer-id";
}

/// @brief Return the effective macOS sign mode for the current host.
/// If pkg.macosSignMode is set, returns it directly. Otherwise defaults to
/// "adhoc" on Apple hosts (where codesign is available) and "preserve" elsewhere.
inline std::string resolveMacOSSignModeForHost(const PackageConfig &pkg) {
    if (!pkg.macosSignMode.empty())
        return pkg.macosSignMode;
#if defined(__APPLE__)
    return "adhoc";
#else
    return "preserve";
#endif
}

/// @brief Validate the full macOS signing/notarization/stapling configuration.
/// Enforces that developer-id mode is required for notarization and stapling,
/// that an identity string is present when developer-id is selected, and that
/// stapling requires a notary profile.
inline void validateMacOSSigningConfig(const PackageConfig &pkg) {
    if (!isValidMacOSSignModeText(pkg.macosSignMode)) {
        throw std::runtime_error("macOS sign mode must be none, preserve, adhoc, or "
                                 "developer-id: " +
                                 pkg.macosSignMode);
    }

    const std::string mode = resolveMacOSSignModeForHost(pkg);
    if (mode == "developer-id" && pkg.macosSignIdentity.empty()) {
        throw std::runtime_error("macOS Developer ID signing requires macos-sign-identity");
    }
    if (!pkg.macosNotaryProfile.empty() && mode != "developer-id") {
        throw std::runtime_error("macOS notarization requires macos-sign-mode developer-id");
    }
    if (pkg.macosStaple) {
        if (mode != "developer-id")
            throw std::runtime_error("macos-staple requires macos-sign-mode developer-id");
        if (pkg.macosNotaryProfile.empty())
            throw std::runtime_error(
                "macos-staple requires macos-notary-profile for this package build");
    }
}

/// @brief Validate a package metadata URL.
/// Checks for a valid URI scheme (letters only start), "://" separator, non-empty
/// authority (host + optional port), no userinfo, and that the port (if present)
/// is purely numeric. Accepts IPv6 bracket notation. Empty URL is accepted.
inline void validatePackageUrl(const std::string &url, const char *fieldName) {
    if (url.empty())
        return;
    validateSingleLineField(url, fieldName);
    for (char c : url) {
        if (std::isspace(static_cast<unsigned char>(c)))
            throw std::runtime_error(std::string(fieldName) +
                                     " must not contain whitespace: '" + url + "'");
    }
    const auto schemePos = url.find("://");
    if (schemePos == std::string::npos || schemePos == 0 || schemePos + 3 >= url.size())
        throw std::runtime_error(std::string(fieldName) +
                                 " must include a URL scheme and host/path: '" + url + "'");
    if (!std::isalpha(static_cast<unsigned char>(url[0])))
        throw std::runtime_error(std::string(fieldName) +
                                 " URL scheme must start with a letter: '" + url + "'");
    for (std::size_t i = 0; i < schemePos; ++i) {
        unsigned char uc = static_cast<unsigned char>(url[i]);
        if (!std::isalpha(uc) && !std::isdigit(uc) && url[i] != '+' && url[i] != '-' &&
            url[i] != '.')
            throw std::runtime_error(std::string(fieldName) +
                                     " contains an invalid URL scheme: '" + url + "'");
    }

    const std::size_t authorityStart = schemePos + 3;
    const std::size_t authorityEnd = url.find_first_of("/?#", authorityStart);
    const std::string authority =
        authorityEnd == std::string::npos
            ? url.substr(authorityStart)
            : url.substr(authorityStart, authorityEnd - authorityStart);
    if (authority.empty())
        throw std::runtime_error(std::string(fieldName) + " URL host must not be empty: '" + url +
                                 "'");
    if (authority.find('@') != std::string::npos)
        throw std::runtime_error(std::string(fieldName) +
                                 " URL userinfo is not supported: '" + url + "'");

    std::string host;
    std::string port;
    if (!authority.empty() && authority.front() == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string::npos || close == 1)
            throw std::runtime_error(std::string(fieldName) +
                                     " URL IPv6 host is malformed: '" + url + "'");
        host = authority.substr(1, close - 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':')
                throw std::runtime_error(std::string(fieldName) +
                                         " URL host has invalid suffix: '" + url + "'");
            port = authority.substr(close + 2);
        }
        bool sawHex = false;
        for (char c : host) {
            if (std::isxdigit(static_cast<unsigned char>(c))) {
                sawHex = true;
                continue;
            }
            if (c != ':')
                throw std::runtime_error(std::string(fieldName) +
                                         " URL IPv6 host contains an invalid character: '" + url +
                                         "'");
        }
        if (!sawHex)
            throw std::runtime_error(std::string(fieldName) +
                                     " URL IPv6 host must contain address digits: '" + url + "'");
    } else {
        const std::size_t colon = authority.rfind(':');
        if (colon != std::string::npos) {
            host = authority.substr(0, colon);
            port = authority.substr(colon + 1);
        } else {
            host = authority;
        }
        if (host.empty() || host.front() == '.' || host.back() == '.' || host.front() == '-' ||
            host.back() == '-')
            throw std::runtime_error(std::string(fieldName) +
                                     " URL host is malformed: '" + url + "'");
        bool sawHostChar = false;
        bool lastWasDot = false;
        for (char c : host) {
            unsigned char uc = static_cast<unsigned char>(c);
            if (std::isalnum(uc)) {
                sawHostChar = true;
                lastWasDot = false;
                continue;
            }
            if (c == '-') {
                lastWasDot = false;
                continue;
            }
            if (c == '.') {
                if (lastWasDot)
                    throw std::runtime_error(std::string(fieldName) +
                                             " URL host has an empty label: '" + url + "'");
                lastWasDot = true;
                continue;
            }
            throw std::runtime_error(std::string(fieldName) +
                                     " URL host contains an invalid character: '" + url + "'");
        }
        if (!sawHostChar)
            throw std::runtime_error(std::string(fieldName) +
                                     " URL host must contain letters or digits: '" + url + "'");
    }
    if (!port.empty()) {
        for (char c : port) {
            if (!std::isdigit(static_cast<unsigned char>(c)))
                throw std::runtime_error(std::string(fieldName) +
                                         " URL port must be numeric: '" + url + "'");
        }
    } else if (!authority.empty() && authority.back() == ':') {
        throw std::runtime_error(std::string(fieldName) + " URL port must not be empty: '" + url +
                                 "'");
    }
}

/// @brief Validate a filename for use on Windows.
/// Rejects control characters, the characters <, >, :, ", /, \, |, ?, *,
/// trailing spaces/dots, and Windows reserved device names (CON, NUL, COM1, etc.).
inline void validateWindowsFileName(const std::string &name, const char *fieldName) {
    if (name.empty())
        throw std::runtime_error(std::string(fieldName) + " must not be empty");
    if (name.back() == ' ' || name.back() == '.')
        throw std::runtime_error(std::string(fieldName) + " must not end in space or dot: '" +
                                 name + "'");
    for (char c : name) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' ||
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

/// @brief Return true if path starts with every component of root.
/// Used to guard against symlink escapes that resolve outside the project root.
/// Both paths should be canonical (absolute, no symlinks) before comparison.
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
            if (uc < 0x20 || uc == 0x7F || c == ':') {
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

/// @brief Resolve a package-relative source path to an absolute filesystem path.
/// Sanitizes raw via sanitizePackageRelativePath, then resolves against projectRoot.
/// Uses fs::canonical when the file exists; falls back to weakly_canonical otherwise
/// (so missing files can still be validated for escapes before creation).
/// Throws if the result escapes the project root.
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

/// @brief Convert a UTF-8 string to a vector of UTF-16 code units (no BOM).
/// Handles the full Unicode range including supplementary characters via
/// surrogate pairs (U+10000–U+10FFFF). Throws on overlong sequences, truncated
/// multi-byte sequences, invalid leading bytes, or lone surrogates.
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

/// @brief Return the number of UTF-16 code units that text would occupy.
/// Convenience wrapper around utf8ToUtf16CodeUnits for size-checking without
/// retaining the full code unit array.
inline size_t utf16CodeUnitCountFromUtf8(const std::string &text) {
    return utf8ToUtf16CodeUnits(text).size();
}

/// @brief Convert a UTF-8 string to a little-endian UTF-16 byte sequence.
/// @param nulTerminate If true, appends a UTF-16 NUL (two zero bytes).
/// Used for Win32 registry string fields and .lnk StringData blocks.
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

/// @brief A package-directory entry after resolving any symlink used to reach it.
///
/// logicalPath is the path that determines the archive/install-relative name.
/// resolvedPath is the filesystem path that was validated against projectRoot
/// and must be used for stat/read/copy operations to avoid post-validation
/// symlink swaps changing the packaged payload.
struct SafeDirectoryEntry {
    std::filesystem::path logicalPath;
    std::filesystem::path resolvedPath;
    bool directory{false};
    bool regularFile{false};
    bool symlink{false};
};

/// @brief Safely iterate a directory tree, following only symlinks that remain
///        inside the project root and handling permission errors gracefully.
///
/// The callback receives both the logical archive path and the resolved path to
/// use for filesystem reads. Callers should read/stat resolvedPath, not
/// logicalPath, to preserve the validation decision.
inline void safeDirectoryIterateResolved(
    const std::filesystem::path &root,
    const std::filesystem::path &projectRoot,
    const std::function<void(const SafeDirectoryEntry &)> &callback) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path canonicalRoot = fs::canonical(projectRoot, ec);
    if (ec)
        canonicalRoot = projectRoot; // Fallback if canonical fails
    ec.clear();

    fs::path canonicalIterRoot = fs::canonical(root, ec);
    if (ec)
        canonicalIterRoot = root;
    ec.clear();
    std::set<fs::path> visitedDirectories;
    visitedDirectories.insert(canonicalIterRoot);

    std::function<void(const fs::path &, const fs::path &)> walk =
        [&](const fs::path &physicalDir, const fs::path &logicalDir) {
            auto it =
                fs::directory_iterator(physicalDir, fs::directory_options::skip_permission_denied, ec);
            if (ec) {
                std::cerr << "warning: cannot access '" << logicalDir.string()
                          << "', skipping: " << ec.message() << "\n";
                ec.clear();
                return;
            }
            const auto end = fs::directory_iterator();
            while (it != end) {
                const fs::path entryPath = logicalDir / it->path().filename();
                bool skipEntry = false;
                bool isDirectory = false;
                bool isRegularFile = false;
                bool hasResolvedSymlink = false;
                fs::path resolvedSymlink;

                std::error_code entryEc;
                if (fs::is_symlink(fs::symlink_status(entryPath, entryEc))) {
                    fs::path resolved = fs::canonical(entryPath, entryEc);
                    if (entryEc) {
                        std::cerr << "warning: cannot resolve symlink '" << entryPath.string()
                                  << "', skipping\n";
                        skipEntry = true;
                    } else if (!isPathWithin(canonicalRoot, resolved)) {
                        std::cerr << "warning: symlink '" << entryPath.string()
                                  << "' escapes project root, skipping\n";
                        skipEntry = true;
                    } else {
                        resolvedSymlink = resolved;
                        hasResolvedSymlink = true;
                    }
                }

                entryEc.clear();
                const fs::path resolvedPath = hasResolvedSymlink ? resolvedSymlink : entryPath;
                if (!skipEntry) {
                    const fs::file_status status = fs::status(resolvedPath, entryEc);
                    if (!entryEc) {
                        isDirectory = fs::is_directory(status);
                        isRegularFile = fs::is_regular_file(status);
                    }
                }
                if (entryEc) {
                    std::cerr << "warning: cannot stat directory entry '" << entryPath.string()
                              << "', skipping: " << entryEc.message() << "\n";
                    skipEntry = true;
                }

                if (!skipEntry)
                    callback(SafeDirectoryEntry{
                        entryPath, resolvedPath, isDirectory, isRegularFile, hasResolvedSymlink});

                if (!skipEntry && isDirectory) {
                    fs::path resolvedDir =
                        hasResolvedSymlink ? resolvedSymlink : fs::canonical(entryPath, entryEc);
                    if (entryEc) {
                        std::cerr << "warning: cannot resolve directory '" << entryPath.string()
                                  << "', skipping recursion\n";
                    } else if (visitedDirectories.insert(resolvedDir).second) {
                        walk(resolvedDir, entryPath);
                    }
                }

                it.increment(ec);
                if (ec) {
                    std::cerr << "warning: cannot advance directory iterator from '"
                              << entryPath.string() << "': " << ec.message() << "\n";
                    ec.clear();
                }
            }
        };
    walk(canonicalIterRoot, root);
}

/// @brief Compatibility wrapper for callers that only need the logical entry.
inline void safeDirectoryIterate(
    const std::filesystem::path &root,
    const std::filesystem::path &projectRoot,
    const std::function<void(const std::filesystem::directory_entry &)> &callback) {
    safeDirectoryIterateResolved(root, projectRoot, [&](const SafeDirectoryEntry &entry) {
        callback(std::filesystem::directory_entry(entry.logicalPath));
    });
}

} // namespace viper::pkg
