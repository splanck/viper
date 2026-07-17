//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/WindowsInstallerMetadata.cpp
// Purpose: Serialize and strictly parse the versioned native Windows installer
//          metadata protocol.
//
// Key invariants:
//   - Serialization order is stable and independent of host locale or time.
//   - Percent decoding accepts only complete uppercase hexadecimal escapes.
//   - Duplicate scalar fields, component ids, payload paths, and ProgIDs fail.
//   - Paths remain relative and cannot traverse out of the installation root.
//
// Ownership/Lifetime:
//   - All parsed fields are copied into owning standard-library containers.
//
// Links: WindowsInstallerMetadata.hpp, WindowsPackageBuilder.cpp
//
//===----------------------------------------------------------------------===//

#include "WindowsInstallerMetadata.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace zanna::pkg {
namespace {

constexpr std::string_view kHeader = "ZANNA-WINDOWS-INSTALLER\t3";
constexpr size_t kMaximumMetadataBytes = 16U * 1024U * 1024U;
constexpr size_t kMaximumRecords = 200000U;

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool isHexUpper(char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F');
}

uint8_t hexValue(char ch) {
    return static_cast<uint8_t>(ch <= '9' ? ch - '0' : 10 + ch - 'A');
}

std::string encodeField(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (const unsigned char ch : value) {
        const bool safe = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') || ch == ' ' || ch == '-' || ch == '_' ||
                          ch == '.' || ch == '/' || ch == '\\' || ch == ':' || ch == '@' ||
                          ch == '+' || ch == ',';
        if (safe && ch != '%') {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(kHex[ch >> 4U]);
            out.push_back(kHex[ch & 0x0FU]);
        }
    }
    return out;
}

std::string decodeField(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '%') {
            const unsigned char ch = static_cast<unsigned char>(value[i]);
            if (ch < 0x20U || ch == 0x7FU)
                throw std::runtime_error("installer metadata contains an unescaped control byte");
            out.push_back(value[i]);
            continue;
        }
        if (i + 2 >= value.size() || !isHexUpper(value[i + 1]) || !isHexUpper(value[i + 2]))
            throw std::runtime_error("installer metadata contains an invalid percent escape");
        const uint8_t decoded =
            static_cast<uint8_t>((hexValue(value[i + 1]) << 4U) | hexValue(value[i + 2]));
        if (decoded == 0)
            throw std::runtime_error("installer metadata contains a NUL byte");
        out.push_back(static_cast<char>(decoded));
        i += 2;
    }
    return out;
}

std::vector<std::string_view> splitTabs(std::string_view line) {
    std::vector<std::string_view> fields;
    size_t start = 0;
    while (true) {
        const size_t pos = line.find('\t', start);
        fields.push_back(line.substr(start, pos == std::string_view::npos ? pos : pos - start));
        if (pos == std::string_view::npos)
            break;
        start = pos + 1;
    }
    return fields;
}

uint64_t parseUint64(std::string_view value, std::string_view fieldName) {
    uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::runtime_error("invalid unsigned integer in installer metadata field '" +
                                 std::string(fieldName) + "'");
    return result;
}

int32_t parseInt32(std::string_view value, std::string_view fieldName) {
    int32_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::runtime_error("invalid signed integer in installer metadata field '" +
                                 std::string(fieldName) + "'");
    return result;
}

bool parseBool(std::string_view value, std::string_view fieldName) {
    if (value == "1")
        return true;
    if (value == "0")
        return false;
    throw std::runtime_error("invalid boolean in installer metadata field '" +
                             std::string(fieldName) + "'");
}

void validateIdentifier(std::string_view value, std::string_view fieldName) {
    if (value.empty() || value.size() > 128)
        throw std::runtime_error("invalid installer metadata " + std::string(fieldName));
    for (const unsigned char ch : value) {
        if (!(std::isalnum(ch) || ch == '.' || ch == '-' || ch == '_'))
            throw std::runtime_error("invalid installer metadata " + std::string(fieldName));
    }
}

void validateChannel(std::string_view value) {
    if (value.empty() || value.size() > 24U ||
        !std::isalnum(static_cast<unsigned char>(value.front())) ||
        !std::isalnum(static_cast<unsigned char>(value.back()))) {
        throw std::runtime_error("invalid Windows installer release channel");
    }
    for (const unsigned char ch : value) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-'))
            throw std::runtime_error("invalid Windows installer release channel");
    }
}

void validateWindowsLeafName(std::string_view value, std::string_view fieldName) {
    if (value.empty() || value.size() > 255U || value == "." || value == ".." ||
        value.back() == '.' || value.back() == ' ' ||
        value.find_first_of("<>:\"/\\|?*") != std::string_view::npos ||
        std::any_of(value.begin(), value.end(), [](unsigned char ch) { return ch < 0x20U; })) {
        throw std::runtime_error("invalid installer metadata " + std::string(fieldName));
    }
    std::string base(value.substr(0, value.find('.')));
    base = lowerAscii(std::move(base));
    const bool numberedDevice = base.size() == 4U &&
                                (base.rfind("com", 0) == 0 || base.rfind("lpt", 0) == 0) &&
                                base[3] >= '1' && base[3] <= '9';
    if (base == "con" || base == "prn" || base == "aux" || base == "nul" || numberedDevice) {
        throw std::runtime_error("reserved installer metadata " + std::string(fieldName));
    }
}

void validateDottedVersion(std::string_view value) {
    if (value.empty() || value.size() > 64U)
        throw std::runtime_error("invalid minimum Windows version in installer metadata");
    size_t start = 0;
    unsigned fields = 0;
    while (start <= value.size()) {
        const size_t dot = value.find('.', start);
        const std::string_view part =
            value.substr(start, dot == std::string_view::npos ? value.size() - start : dot - start);
        uint32_t number = 0;
        const auto parsed = std::from_chars(part.data(), part.data() + part.size(), number);
        if (part.empty() || parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size() ||
            ++fields > 3U) {
            throw std::runtime_error("invalid minimum Windows version in installer metadata");
        }
        if (dot == std::string_view::npos)
            break;
        start = dot + 1U;
    }
}

void validateRelativePath(std::string_view value, std::string_view fieldName) {
    if (value.empty() || value.size() > 32760 || value.front() == '/' || value.front() == '\\' ||
        (value.size() >= 2 && std::isalpha(static_cast<unsigned char>(value[0])) &&
         value[1] == ':')) {
        throw std::runtime_error("invalid installer metadata " + std::string(fieldName));
    }
    size_t start = 0;
    while (start <= value.size()) {
        const size_t end = value.find_first_of("/\\", start);
        const std::string_view segment =
            value.substr(start, end == std::string_view::npos ? value.size() - start : end - start);
        if (segment.empty() || segment == "." || segment == "..")
            throw std::runtime_error("unsafe installer metadata " + std::string(fieldName));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
}

void validateSha256(std::string_view value) {
    if (value.size() != 64)
        throw std::runtime_error("invalid payload SHA-256 in installer metadata");
    for (char ch : value) {
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
            throw std::runtime_error("invalid payload SHA-256 in installer metadata");
    }
}

bool isLowerHex(std::string_view value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}

void validateMetadata(const WindowsInstallerMetadata &m) {
    if (m.schemaVersion != kWindowsInstallerMetadataSchema)
        throw std::runtime_error("unsupported Windows installer metadata schema");
    if (m.packageMode != "setup" && m.packageMode != "maintenance")
        throw std::runtime_error("invalid Windows installer package mode");
    if (m.productKind != "application" && m.productKind != "toolchain")
        throw std::runtime_error("invalid Windows installer product kind");
    validateIdentifier(m.identifier, "identifier");
    if (m.displayName.empty() || m.version.empty() || m.publisher.empty())
        throw std::runtime_error("Windows installer metadata is missing product identity");
    if (m.architecture != "x64" && m.architecture != "arm64")
        throw std::runtime_error("invalid Windows installer metadata architecture");
    validateChannel(m.channel);
    if (!m.commit.empty()) {
        if (m.commit.size() < 7U || m.commit.size() > 64U ||
            !std::all_of(m.commit.begin(), m.commit.end(), [](unsigned char ch) {
                return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
            })) {
            throw std::runtime_error("invalid Windows installer source commit");
        }
    }
    if ((!m.homepage.empty() && m.homepage.rfind("https://", 0) != 0) ||
        (!m.documentationUrl.empty() && m.documentationUrl.rfind("https://", 0) != 0)) {
        throw std::runtime_error("Windows installer public URLs must use HTTPS");
    }
    const bool hasUpdateUrl = !m.updateManifestUrl.empty();
    const bool hasUpdateKey = !m.updateRsaModulus.empty() || !m.updateRsaExponent.empty();
    if (hasUpdateUrl != hasUpdateKey)
        throw std::runtime_error(
            "Windows installer update metadata requires both an HTTPS URL and RSA public key");
    if (hasUpdateUrl) {
        if (m.updateManifestUrl.rfind("https://", 0) != 0)
            throw std::runtime_error("Windows installer update manifest URL must use HTTPS");
        if (m.updateRsaModulus.size() < 512U || m.updateRsaModulus.size() > 1024U ||
            m.updateRsaModulus.size() % 2U != 0U || !isLowerHex(m.updateRsaModulus) ||
            m.updateRsaModulus.front() == '0') {
            throw std::runtime_error(
                "Windows installer update RSA modulus must be 2048-4096-bit lowercase hex");
        }
        if (m.updateRsaExponent.size() < 2U || m.updateRsaExponent.size() > 8U ||
            m.updateRsaExponent.size() % 2U != 0U || !isLowerHex(m.updateRsaExponent) ||
            m.updateRsaExponent.front() == '0') {
            throw std::runtime_error(
                "Windows installer update RSA exponent must be lowercase big-endian hex");
        }
        uint64_t exponent = 0;
        const auto parsed = std::from_chars(m.updateRsaExponent.data(),
                                            m.updateRsaExponent.data() + m.updateRsaExponent.size(),
                                            exponent,
                                            16);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != m.updateRsaExponent.data() + m.updateRsaExponent.size() ||
            exponent < 3U || exponent % 2U == 0U) {
            throw std::runtime_error(
                "Windows installer update RSA exponent must be an odd integer of at least 3");
        }
    }
    if (m.defaultScope != "user" && m.defaultScope != "machine")
        throw std::runtime_error("invalid Windows installer default scope");
    validateWindowsLeafName(m.defaultInstallDir, "install directory");
    validateRelativePath(m.executableName, "primary executable path");
    if (!m.displayIconRelativePath.empty())
        validateRelativePath(m.displayIconRelativePath, "display icon path");
    validateRelativePath(m.payloadEntry, "payload entry");
    validateRelativePath(m.cleanupEntry, "cleanup entry");
    validateSha256(m.cleanupSha256);
    validateRelativePath(m.licenseEntry, "license entry");
    validateRelativePath(m.readmeEntry, "readme entry");
    validateRelativePath(m.installedManifestRelativePath, "installed manifest path");
    validateRelativePath(m.stateRelativePath, "state path");
    validateRelativePath(m.uninstallerRelativePath, "uninstaller path");
    validateDottedVersion(m.minimumWindowsVersion);
    if (!m.associationExecutable.empty())
        validateRelativePath(m.associationExecutable, "association executable");
    if (!m.pathRelativePath.empty())
        validateRelativePath(m.pathRelativePath, "PATH relative path");

    std::set<std::string> components;
    bool hasCore = false;
    for (const auto &component : m.components) {
        validateIdentifier(component.id, "component id");
        if (component.label.empty() || !components.insert(lowerAscii(component.id)).second)
            throw std::runtime_error("duplicate or unnamed Windows installer component");
        if (lowerAscii(component.id) == "core") {
            if (!component.required || !component.defaultSelected)
                throw std::runtime_error("Windows installer core component must be required");
            hasCore = true;
        }
    }
    if (!hasCore)
        throw std::runtime_error("Windows installer metadata is missing its core component");

    std::set<std::string> paths;
    std::map<std::string, uint64_t> componentSizes;
    uint64_t summedSize = 0;
    for (const auto &file : m.payloadFiles) {
        validateRelativePath(file.path, "payload path");
        validateSha256(file.sha256);
        const std::string folded = lowerAscii(file.path);
        if (!paths.insert(folded).second)
            throw std::runtime_error("duplicate Windows installer payload path");
        if (!file.componentId.empty() &&
            components.find(lowerAscii(file.componentId)) == components.end()) {
            throw std::runtime_error("payload references an unknown Windows installer component");
        }
        if (file.sizeBytes > std::numeric_limits<uint64_t>::max() - summedSize)
            throw std::runtime_error("Windows installer payload size overflow");
        summedSize += file.sizeBytes;
        componentSizes[file.componentId.empty() ? "core" : lowerAscii(file.componentId)] +=
            file.sizeBytes;
    }
    std::set<std::string> outerEntries;
    for (const auto &file : m.outerFiles) {
        validateRelativePath(file.overlayPath, "outer-file overlay path");
        validateRelativePath(file.path, "outer-file destination path");
        validateSha256(file.sha256);
        if (!outerEntries.insert(lowerAscii(file.overlayPath)).second ||
            !paths.insert(lowerAscii(file.path)).second) {
            throw std::runtime_error("duplicate Windows installer outer-file record");
        }
        if (!file.componentId.empty() &&
            components.find(lowerAscii(file.componentId)) == components.end()) {
            throw std::runtime_error(
                "outer file references an unknown Windows installer component");
        }
        if (file.sizeBytes > std::numeric_limits<uint64_t>::max() - summedSize)
            throw std::runtime_error("Windows installer payload size overflow");
        summedSize += file.sizeBytes;
        componentSizes[file.componentId.empty() ? "core" : lowerAscii(file.componentId)] +=
            file.sizeBytes;
    }
    if (m.packageMode == "setup") {
        if (m.outerFiles.size() != 1U ||
            lowerAscii(m.outerFiles.front().overlayPath) != "meta/uninstall.exe" ||
            lowerAscii(m.outerFiles.front().path) != lowerAscii(m.uninstallerRelativePath) ||
            !m.outerFiles.front().componentId.empty()) {
            throw std::runtime_error(
                "Windows setup metadata must contain one core maintenance executable");
        }
    } else if (!m.outerFiles.empty()) {
        throw std::runtime_error(
            "Windows maintenance metadata must not recursively contain outer files");
    }
    if (summedSize != m.installedSizeBytes)
        throw std::runtime_error(
            "Windows installer installed-size metadata does not match payload");
    for (const auto &component : m.components) {
        if (component.sizeBytes != componentSizes[lowerAscii(component.id)])
            throw std::runtime_error("Windows installer component size does not match its payload");
    }

    std::set<std::string> shortcutPaths;
    for (const auto &shortcut : m.shortcuts) {
        validateRelativePath(shortcut.relativePath, "shortcut destination path");
        if (shortcut.root != "desktop" && shortcut.root != "start-menu")
            throw std::runtime_error("invalid Windows installer shortcut root");
        const std::string key = shortcut.root + ":" + lowerAscii(shortcut.relativePath);
        if (!shortcutPaths.insert(key).second)
            throw std::runtime_error("duplicate Windows installer shortcut destination");
        if (shortcut.targetRoot != "install" && shortcut.targetRoot != "windows")
            throw std::runtime_error("invalid Windows installer shortcut target root");
        validateRelativePath(shortcut.targetPath, "shortcut target path");
        if (shortcut.workingRoot != "install" && shortcut.workingRoot != "profile" &&
            shortcut.workingRoot != "windows") {
            throw std::runtime_error("invalid Windows installer shortcut working root");
        }
        if (!shortcut.workingPath.empty())
            validateRelativePath(shortcut.workingPath, "shortcut working path");
        if (!shortcut.argumentPath.empty())
            validateRelativePath(shortcut.argumentPath, "shortcut argument path");
        if (shortcut.argumentPath.empty() != shortcut.argumentPrefix.empty()) {
            throw std::runtime_error(
                "Windows installer shortcut arguments require both a prefix and a path");
        }
        for (const unsigned char ch : shortcut.argumentPrefix) {
            if (ch < 0x21U || ch > 0x7EU || ch == '"' || ch == '\\')
                throw std::runtime_error("unsafe Windows installer shortcut argument prefix");
        }
        if (shortcut.description.empty())
            throw std::runtime_error("Windows installer shortcut description is empty");
        if (shortcut.iconRoot.empty() != shortcut.iconPath.empty())
            throw std::runtime_error("incomplete Windows installer shortcut icon metadata");
        if (!shortcut.iconRoot.empty() && shortcut.iconRoot != "install" &&
            shortcut.iconRoot != "windows") {
            throw std::runtime_error("invalid Windows installer shortcut icon root");
        }
        if (!shortcut.iconPath.empty())
            validateRelativePath(shortcut.iconPath, "shortcut icon path");
        if (!shortcut.componentId.empty() &&
            components.find(lowerAscii(shortcut.componentId)) == components.end()) {
            throw std::runtime_error("shortcut references an unknown Windows installer component");
        }
    }

    std::set<std::string> extensions;
    std::set<std::string> progIds;
    for (const auto &assoc : m.associations) {
        if (assoc.extension.size() < 2 || assoc.extension.front() != '.' || assoc.progId.empty())
            throw std::runtime_error("invalid Windows installer file association");
        if (!extensions.insert(lowerAscii(assoc.extension)).second ||
            !progIds.insert(lowerAscii(assoc.progId)).second) {
            throw std::runtime_error("duplicate Windows installer file association");
        }
    }
    if (m.registerFileAssociations && (m.associationExecutable.empty() || m.associations.empty())) {
        throw std::runtime_error("file association metadata lacks an executable or association");
    }
}

void appendScalar(std::ostringstream &out, std::string_view name, std::string_view value) {
    out << name << '\t' << encodeField(value) << '\n';
}

} // namespace

std::string serializeWindowsInstallerMetadata(const WindowsInstallerMetadata &metadata) {
    validateMetadata(metadata);
    std::ostringstream out;
    out << kHeader << '\n';
    appendScalar(out, "mode", metadata.packageMode);
    appendScalar(out, "kind", metadata.productKind);
    appendScalar(out, "identifier", metadata.identifier);
    appendScalar(out, "display", metadata.displayName);
    appendScalar(out, "version", metadata.version);
    appendScalar(out, "publisher", metadata.publisher);
    appendScalar(out, "description", metadata.description);
    appendScalar(out, "contact", metadata.contact);
    appendScalar(out, "homepage", metadata.homepage);
    appendScalar(out, "documentation-url", metadata.documentationUrl);
    appendScalar(out, "update-manifest-url", metadata.updateManifestUrl);
    appendScalar(out, "update-rsa-modulus", metadata.updateRsaModulus);
    appendScalar(out, "update-rsa-exponent", metadata.updateRsaExponent);
    appendScalar(out, "architecture", metadata.architecture);
    appendScalar(out, "channel", metadata.channel);
    appendScalar(out, "commit", metadata.commit);
    appendScalar(out, "default-scope", metadata.defaultScope);
    appendScalar(out, "default-install-dir", metadata.defaultInstallDir);
    appendScalar(out, "executable", metadata.executableName);
    appendScalar(out, "association-executable", metadata.associationExecutable);
    appendScalar(out, "path-relative", metadata.pathRelativePath);
    appendScalar(out, "display-icon", metadata.displayIconRelativePath);
    appendScalar(out, "payload-entry", metadata.payloadEntry);
    appendScalar(out, "cleanup-entry", metadata.cleanupEntry);
    appendScalar(out, "cleanup-sha256", metadata.cleanupSha256);
    appendScalar(out, "license-entry", metadata.licenseEntry);
    appendScalar(out, "readme-entry", metadata.readmeEntry);
    appendScalar(out, "installed-manifest", metadata.installedManifestRelativePath);
    appendScalar(out, "state-path", metadata.stateRelativePath);
    appendScalar(out, "uninstaller", metadata.uninstallerRelativePath);
    appendScalar(out, "minimum-windows", metadata.minimumWindowsVersion);
    out << "add-to-path\t" << (metadata.addToPath ? '1' : '0') << '\n';
    out << "register-associations\t" << (metadata.registerFileAssociations ? '1' : '0') << '\n';
    out << "create-shortcuts\t" << (metadata.createShortcuts ? '1' : '0') << '\n';
    out << "installed-size\t" << metadata.installedSizeBytes << '\n';
    for (const auto &component : metadata.components) {
        out << "component\t" << encodeField(component.id) << '\t' << encodeField(component.label)
            << '\t' << encodeField(component.description) << '\t'
            << (component.required ? '1' : '0') << '\t' << (component.defaultSelected ? '1' : '0')
            << '\t' << component.sizeBytes << '\n';
    }
    for (const auto &file : metadata.payloadFiles) {
        out << "payload\t" << encodeField(file.path) << '\t' << file.sha256 << '\t'
            << file.sizeBytes << '\t' << encodeField(file.componentId) << '\n';
    }
    for (const auto &file : metadata.outerFiles) {
        out << "outer-file\t" << encodeField(file.overlayPath) << '\t' << encodeField(file.path)
            << '\t' << file.sha256 << '\t' << file.sizeBytes << '\t'
            << encodeField(file.componentId) << '\n';
    }
    for (const auto &shortcut : metadata.shortcuts) {
        out << "shortcut\t" << encodeField(shortcut.root) << '\t'
            << encodeField(shortcut.relativePath) << '\t' << encodeField(shortcut.targetRoot)
            << '\t' << encodeField(shortcut.targetPath) << '\t' << encodeField(shortcut.workingRoot)
            << '\t' << encodeField(shortcut.workingPath) << '\t'
            << encodeField(shortcut.argumentPrefix) << '\t' << encodeField(shortcut.argumentPath)
            << '\t' << encodeField(shortcut.description) << '\t' << encodeField(shortcut.iconRoot)
            << '\t' << encodeField(shortcut.iconPath) << '\t' << shortcut.iconIndex << '\t'
            << encodeField(shortcut.componentId) << '\n';
    }
    for (const auto &assoc : metadata.associations) {
        out << "association\t" << encodeField(assoc.extension) << '\t'
            << encodeField(assoc.description) << '\t' << encodeField(assoc.mimeType) << '\t'
            << encodeField(assoc.progId) << '\t' << encodeField(assoc.arguments) << '\n';
    }
    return out.str();
}

WindowsInstallerMetadata parseWindowsInstallerMetadata(std::string_view text) {
    if (text.empty() || text.size() > kMaximumMetadataBytes)
        throw std::runtime_error("Windows installer metadata is empty or too large");
    WindowsInstallerMetadata result;
    std::set<std::string> scalars;
    size_t recordCount = 0;
    size_t lineStart = 0;
    bool first = true;
    while (lineStart <= text.size()) {
        const size_t lineEnd = text.find('\n', lineStart);
        std::string_view line = text.substr(
            lineStart,
            lineEnd == std::string_view::npos ? text.size() - lineStart : lineEnd - lineStart);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (first) {
            first = false;
            if (line != kHeader)
                throw std::runtime_error(
                    "unsupported or malformed Windows installer metadata header");
        } else if (!line.empty()) {
            if (++recordCount > kMaximumRecords)
                throw std::runtime_error("Windows installer metadata contains too many records");
            const auto fields = splitTabs(line);
            const std::string key(fields.front());
            if (key == "component") {
                if (fields.size() != 7)
                    throw std::runtime_error("malformed Windows installer component record");
                result.components.push_back({decodeField(fields[1]),
                                             decodeField(fields[2]),
                                             decodeField(fields[3]),
                                             parseBool(fields[4], key),
                                             parseBool(fields[5], key),
                                             parseUint64(fields[6], key)});
            } else if (key == "payload") {
                if (fields.size() != 5)
                    throw std::runtime_error("malformed Windows installer payload record");
                result.payloadFiles.push_back({decodeField(fields[1]),
                                               std::string(fields[2]),
                                               parseUint64(fields[3], key),
                                               decodeField(fields[4])});
            } else if (key == "outer-file") {
                if (fields.size() != 6)
                    throw std::runtime_error("malformed Windows installer outer-file record");
                result.outerFiles.push_back({decodeField(fields[1]),
                                             decodeField(fields[2]),
                                             std::string(fields[3]),
                                             parseUint64(fields[4], key),
                                             decodeField(fields[5])});
            } else if (key == "shortcut") {
                if (fields.size() != 14)
                    throw std::runtime_error("malformed Windows installer shortcut record");
                result.shortcuts.push_back({decodeField(fields[1]),
                                            decodeField(fields[2]),
                                            decodeField(fields[3]),
                                            decodeField(fields[4]),
                                            decodeField(fields[5]),
                                            decodeField(fields[6]),
                                            decodeField(fields[7]),
                                            decodeField(fields[8]),
                                            decodeField(fields[9]),
                                            decodeField(fields[10]),
                                            decodeField(fields[11]),
                                            parseInt32(fields[12], key),
                                            decodeField(fields[13])});
            } else if (key == "association") {
                if (fields.size() != 6)
                    throw std::runtime_error("malformed Windows installer association record");
                result.associations.push_back({decodeField(fields[1]),
                                               decodeField(fields[2]),
                                               decodeField(fields[3]),
                                               decodeField(fields[4]),
                                               decodeField(fields[5])});
            } else {
                if (fields.size() != 2 || !scalars.insert(key).second)
                    throw std::runtime_error("malformed or duplicate Windows installer scalar '" +
                                             key + "'");
                const std::string value = decodeField(fields[1]);
                if (key == "mode")
                    result.packageMode = value;
                else if (key == "kind")
                    result.productKind = value;
                else if (key == "identifier")
                    result.identifier = value;
                else if (key == "display")
                    result.displayName = value;
                else if (key == "version")
                    result.version = value;
                else if (key == "publisher")
                    result.publisher = value;
                else if (key == "description")
                    result.description = value;
                else if (key == "contact")
                    result.contact = value;
                else if (key == "homepage")
                    result.homepage = value;
                else if (key == "documentation-url")
                    result.documentationUrl = value;
                else if (key == "update-manifest-url")
                    result.updateManifestUrl = value;
                else if (key == "update-rsa-modulus")
                    result.updateRsaModulus = value;
                else if (key == "update-rsa-exponent")
                    result.updateRsaExponent = value;
                else if (key == "architecture")
                    result.architecture = value;
                else if (key == "channel")
                    result.channel = value;
                else if (key == "commit")
                    result.commit = value;
                else if (key == "default-scope")
                    result.defaultScope = value;
                else if (key == "default-install-dir")
                    result.defaultInstallDir = value;
                else if (key == "executable")
                    result.executableName = value;
                else if (key == "association-executable")
                    result.associationExecutable = value;
                else if (key == "path-relative")
                    result.pathRelativePath = value;
                else if (key == "display-icon")
                    result.displayIconRelativePath = value;
                else if (key == "payload-entry")
                    result.payloadEntry = value;
                else if (key == "cleanup-entry")
                    result.cleanupEntry = value;
                else if (key == "cleanup-sha256")
                    result.cleanupSha256 = value;
                else if (key == "license-entry")
                    result.licenseEntry = value;
                else if (key == "readme-entry")
                    result.readmeEntry = value;
                else if (key == "installed-manifest")
                    result.installedManifestRelativePath = value;
                else if (key == "state-path")
                    result.stateRelativePath = value;
                else if (key == "uninstaller")
                    result.uninstallerRelativePath = value;
                else if (key == "minimum-windows")
                    result.minimumWindowsVersion = value;
                else if (key == "add-to-path")
                    result.addToPath = parseBool(fields[1], key);
                else if (key == "register-associations")
                    result.registerFileAssociations = parseBool(fields[1], key);
                else if (key == "create-shortcuts")
                    result.createShortcuts = parseBool(fields[1], key);
                else if (key == "installed-size")
                    result.installedSizeBytes = parseUint64(fields[1], key);
                else
                    throw std::runtime_error("unknown Windows installer metadata field '" + key +
                                             "'");
            }
        }
        if (lineEnd == std::string_view::npos)
            break;
        lineStart = lineEnd + 1;
    }
    static const std::set<std::string> kRequiredScalars = {
        "mode",
        "kind",
        "identifier",
        "display",
        "version",
        "publisher",
        "description",
        "contact",
        "homepage",
        "documentation-url",
        "update-manifest-url",
        "update-rsa-modulus",
        "update-rsa-exponent",
        "architecture",
        "channel",
        "commit",
        "default-scope",
        "default-install-dir",
        "executable",
        "association-executable",
        "path-relative",
        "display-icon",
        "payload-entry",
        "cleanup-entry",
        "cleanup-sha256",
        "license-entry",
        "readme-entry",
        "installed-manifest",
        "state-path",
        "uninstaller",
        "minimum-windows",
        "add-to-path",
        "register-associations",
        "create-shortcuts",
        "installed-size",
    };
    if (scalars != kRequiredScalars)
        throw std::runtime_error("Windows installer metadata is missing a required scalar field");
    validateMetadata(result);
    return result;
}

} // namespace zanna::pkg
