//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/WindowsPackageBuilder.cpp
// Purpose: Assemble a Windows self-extracting installer .exe.
//
// Key invariants:
//   - Overlay is a structurally valid ZIP archive using stored entries only.
//   - Installer/uninstaller behavior is driven by an explicit package layout,
//     not by parsing installer metadata at runtime.
//   - All package construction happens inside Viper; no external tools are used.
//
// Ownership/Lifetime:
//   - Single-use builder.
//
// Links: WindowsPackageBuilder.hpp, InstallerStub.hpp, PEBuilder.hpp, ZipWriter.hpp
//
//===----------------------------------------------------------------------===//

#include "WindowsPackageBuilder.hpp"
#include "IconGenerator.hpp"
#include "InstallerStub.hpp"
#include "LnkWriter.hpp"
#include "PEBuilder.hpp"
#include "PkgUtils.hpp"
#include "ZipWriter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace viper::pkg {

namespace {

constexpr size_t kInstallerStubPathCharLimit = 32768;
constexpr uint64_t kInstallerStackReserve = 0x200000;
constexpr uint64_t kInstallerStackCommit = 0x100000;

/// @brief Apply a larger-than-default stack to all installer/uninstaller PEs.
/// The installer recursively creates directory trees and copies large files;
/// the default 1 MB reserve is too small for deeply nested install paths.
void configureInstallerStack(PEBuildParams &pe) {
    pe.stackReserve = kInstallerStackReserve;
    pe.stackCommit = kInstallerStackCommit;
}

/// @brief Append a directory entry to out only if it has not already been seen.
/// The seen set keys on root+path so the same relative path under different
/// roots (e.g. InstallDir vs StartMenuDir) is treated as two distinct entries.
void addUniqueDir(std::vector<WindowsPackageDirEntry> &out,
                  std::set<std::string> &seen,
                  WindowsInstallRoot root,
                  const std::string &relativePath) {
    const std::string clean = sanitizePackageRelativePath(relativePath, "windows package path");
    if (clean.empty())
        return;
    const std::string key = std::to_string(static_cast<unsigned long long>(root)) + ":" + clean;
    if (!seen.insert(key).second)
        return;
    out.push_back(WindowsPackageDirEntry{root, clean});
}

/// @brief Validate every path segment in a slash-separated relative Windows path.
/// Each segment must pass validateWindowsFileName to reject reserved device names
/// (CON, NUL, COM1…), illegal characters (<, >, :, |, ?, *), and empty components.
void validateWindowsRelativePath(const std::string &relativePath, const char *fieldName) {
    const std::string clean = sanitizePackageRelativePath(relativePath, fieldName);
    size_t pos = 0;
    while (pos < clean.size()) {
        const size_t next = clean.find('/', pos);
        const std::string segment =
            next == std::string::npos ? clean.substr(pos) : clean.substr(pos, next - pos);
        validateWindowsFileName(segment, fieldName);
        if (next == std::string::npos)
            break;
        pos = next + 1;
    }
}

/// @brief Ensure that an absolute-expanded Windows path (e.g. %ProgramFiles%\App\bin\viper.exe)
/// fits within the installer stub's fixed-size WCHAR path buffer (32768 code units).
/// The check uses UTF-16 unit count so multi-byte UTF-8 characters are counted correctly.
void validateStubPathFits(const std::string &path, const char *fieldName) {
    if (utf16CodeUnitCountFromUtf8(path) + 1 > kInstallerStubPathCharLimit)
        throw std::runtime_error(std::string(fieldName) +
                                 " exceeds the Windows installer long-path stub limit: " + path);
}

/// @brief Pre-flight every path that the installer stub will write at runtime to
/// ensure none exceed the stub's WCHAR buffer limit. Checks: install root,
/// all directories, all files, the optional PATH entry, the file association
/// executable path, and all file association ProgID command arguments.
void validateWindowsLayoutFitsStub(const WindowsPackageLayout &layout) {
    const std::string installDir = layout.installDirName.empty() ? layout.displayName
                                                                 : layout.installDirName;
    validateWindowsFileName(installDir, "Windows install directory");
    const std::string rootProbe =
        (layout.perUserInstall ? "%LocalAppData%\\" : "%ProgramFiles%\\") + installDir;
    validateStubPathFits(rootProbe, "Windows install directory");
    for (const auto &dir : layout.installDirectories) {
        validateWindowsRelativePath(dir.relativePath, "Windows install directory path");
        validateStubPathFits(rootProbe + "\\" + dir.relativePath, "Windows install directory path");
    }
    for (const auto &file : layout.installFiles) {
        validateWindowsRelativePath(file.relativePath, "Windows install file path");
        validateStubPathFits(rootProbe + "\\" + file.relativePath, "Windows install file path");
    }
    if (layout.addToPath && !layout.pathRelativePath.empty()) {
        validateWindowsRelativePath(layout.pathRelativePath, "Windows PATH entry path");
        validateStubPathFits(rootProbe + "\\" + layout.pathRelativePath, "Windows PATH entry path");
    }
    if (!layout.fileAssociationExecutableRelativePath.empty()) {
        validateWindowsRelativePath(layout.fileAssociationExecutableRelativePath,
                                    "Windows file association executable path");
        validateStubPathFits(rootProbe + "\\" + layout.fileAssociationExecutableRelativePath,
                             "Windows file association executable path");
    }
    if (!layout.displayIconRelativePath.empty()) {
        validateWindowsRelativePath(layout.displayIconRelativePath, "Windows display icon path");
        validateStubPathFits(rootProbe + "\\" + layout.displayIconRelativePath,
                             "Windows display icon path");
    }
    for (const auto &file : layout.uninstallFiles)
        validateWindowsRelativePath(file.relativePath, "Windows uninstall file path");
    for (const auto &assoc : layout.fileAssociations) {
        validateSingleLineField(assoc.openCommandArguments,
                                "Windows file association command arguments");
        if (assoc.openCommandArguments.find('"') != std::string::npos) {
            throw std::runtime_error("Windows file association command arguments must not "
                                     "contain quotes: " +
                                     assoc.openCommandArguments);
        }
    }
}

/// @brief Return text lowercased for case-insensitive filename comparisons (LICENSE, README.MD).
/// Only transforms ASCII letters so it is safe for arbitrary UTF-8 filenames.
std::string lowerAscii(std::string text) {
    for (char &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

uint16_t rd16(const std::vector<uint8_t> &data, size_t off) {
    return static_cast<uint16_t>(data[off] | (data[off + 1] << 8));
}

uint32_t rd32(const std::vector<uint8_t> &data, size_t off) {
    return static_cast<uint32_t>(data[off]) | (static_cast<uint32_t>(data[off + 1]) << 8) |
           (static_cast<uint32_t>(data[off + 2]) << 16) |
           (static_cast<uint32_t>(data[off + 3]) << 24);
}

bool hasBytes(const std::vector<uint8_t> &data, size_t off, size_t len) {
    return off <= data.size() && len <= data.size() - off;
}

uint32_t rotr32(uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32u - bits));
}

std::string sha256Hex(const uint8_t *data, size_t len) {
    static constexpr std::array<uint32_t, 64> k = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
        0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
        0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
        0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
    uint32_t h[8] = {0x6a09e667u,
                     0xbb67ae85u,
                     0x3c6ef372u,
                     0xa54ff53au,
                     0x510e527fu,
                     0x9b05688cu,
                     0x1f83d9abu,
                     0x5be0cd19u};

    std::vector<uint8_t> msg;
    if (len != 0)
        msg.assign(data, data + len);
    const uint64_t bitLen = static_cast<uint64_t>(len) * 8ull;
    msg.push_back(0x80);
    while ((msg.size() % 64u) != 56u)
        msg.push_back(0);
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<uint8_t>((bitLen >> (i * 8)) & 0xffu));

    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[64] = {};
        for (size_t i = 0; i < 16; ++i) {
            const size_t p = off + i * 4u;
            w[i] = (static_cast<uint32_t>(msg[p]) << 24) |
                   (static_cast<uint32_t>(msg[p + 1]) << 16) |
                   (static_cast<uint32_t>(msg[p + 2]) << 8) |
                   static_cast<uint32_t>(msg[p + 3]);
        }
        for (size_t i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (size_t i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = hh + s1 + ch + k[i] + w[i];
            const uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (uint32_t word : h)
        os << std::setw(8) << word;
    return os.str();
}

struct PeSectionInfo {
    uint32_t rva{0};
    uint32_t virtualSize{0};
    uint32_t rawOffset{0};
    uint32_t rawSize{0};
};

struct PeExecutableInfo {
    uint16_t machine{0};
    bool pe32Plus{false};
};

std::optional<PeExecutableInfo> inspectPeExecutable(const std::vector<uint8_t> &data) {
    if (!hasBytes(data, 0, 64) || data[0] != 'M' || data[1] != 'Z')
        return std::nullopt;
    const size_t peOff = rd32(data, 60);
    if (!hasBytes(data, peOff, 24) || data[peOff] != 'P' || data[peOff + 1] != 'E' ||
        data[peOff + 2] != 0 || data[peOff + 3] != 0)
        return std::nullopt;
    const size_t coffOff = peOff + 4;
    const uint16_t optSize = rd16(data, coffOff + 16);
    const size_t optOff = coffOff + 20;
    if (!hasBytes(data, optOff, optSize) || optSize < 2)
        return std::nullopt;
    return PeExecutableInfo{rd16(data, coffOff), rd16(data, optOff) == 0x020B};
}

uint16_t windowsMachineForArch(const std::string &arch) {
    if (arch.empty() || arch == "x64")
        return 0x8664;
    if (arch == "arm64")
        return 0xAA64;
    throw std::runtime_error("unsupported Windows package architecture '" + arch + "'");
}

void validateWindowsPayloadExecutable(const fs::path &path, const std::string &arch) {
    const auto data = readFile(path.string());
    const auto info = inspectPeExecutable(data);
    if (!info || !info->pe32Plus)
        throw std::runtime_error("Windows package executable must be a PE32+ executable: " +
                                 path.string());
    const uint16_t expected = windowsMachineForArch(arch);
    if (info->machine != expected) {
        std::ostringstream os;
        os << "Windows package executable architecture does not match selected architecture '"
           << (arch.empty() ? "x64" : arch) << "': " << path.string();
        throw std::runtime_error(os.str());
    }
}

std::optional<size_t> peRvaToFileOffset(const std::vector<PeSectionInfo> &sections,
                                        uint32_t rva) {
    for (const auto &sec : sections) {
        const uint32_t extent = std::max(sec.virtualSize, sec.rawSize);
        if (extent == 0)
            continue;
        if (rva >= sec.rva && rva - sec.rva < extent)
            return static_cast<size_t>(sec.rawOffset) + static_cast<size_t>(rva - sec.rva);
    }
    return std::nullopt;
}

std::string readPeAsciiZ(const std::vector<uint8_t> &data, size_t off) {
    std::string out;
    while (off < data.size() && data[off] != 0) {
        const unsigned char ch = data[off++];
        if (ch < 0x20 || ch > 0x7e)
            return {};
        out.push_back(static_cast<char>(ch));
    }
    return out;
}

std::vector<std::string> importedDllNamesFromPe(const std::vector<uint8_t> &data) {
    if (!hasBytes(data, 0, 64) || data[0] != 'M' || data[1] != 'Z')
        return {};
    const size_t peOff = rd32(data, 60);
    if (!hasBytes(data, peOff, 24) || data[peOff] != 'P' || data[peOff + 1] != 'E' ||
        data[peOff + 2] != 0 || data[peOff + 3] != 0)
        return {};

    const size_t coffOff = peOff + 4;
    const uint16_t numSections = rd16(data, coffOff + 2);
    const uint16_t optSize = rd16(data, coffOff + 16);
    const size_t optOff = coffOff + 20;
    if (!hasBytes(data, optOff, optSize) || rd16(data, optOff) != 0x020B || optSize < 120)
        return {};
    const uint32_t importRva = rd32(data, optOff + 112 + 8);
    const uint32_t importSize = rd32(data, optOff + 112 + 12);
    if (importRva == 0 || importSize == 0)
        return {};

    const size_t secOff = optOff + optSize;
    if (!hasBytes(data, secOff, static_cast<size_t>(numSections) * 40u))
        return {};
    std::vector<PeSectionInfo> sections;
    sections.reserve(numSections);
    for (uint16_t i = 0; i < numSections; ++i) {
        const size_t cur = secOff + static_cast<size_t>(i) * 40u;
        sections.push_back({rd32(data, cur + 12),
                            rd32(data, cur + 8),
                            rd32(data, cur + 20),
                            rd32(data, cur + 16)});
    }

    const auto importOff = peRvaToFileOffset(sections, importRva);
    if (!importOff)
        return {};

    std::vector<std::string> names;
    for (size_t desc = *importOff; hasBytes(data, desc, 20); desc += 20) {
        const uint32_t originalThunk = rd32(data, desc);
        const uint32_t nameRva = rd32(data, desc + 12);
        const uint32_t firstThunk = rd32(data, desc + 16);
        if (originalThunk == 0 && nameRva == 0 && firstThunk == 0)
            break;
        const auto nameOff = peRvaToFileOffset(sections, nameRva);
        if (!nameOff)
            return {};
        std::string dll = lowerAscii(readPeAsciiZ(data, *nameOff));
        if (!dll.empty())
            names.push_back(std::move(dll));
    }
    return names;
}

bool isKnownWindowsRedistributableDll(const std::string &dll) {
    const std::string stem = dll.size() > 4 && dll.substr(dll.size() - 4) == ".dll"
                                 ? dll.substr(0, dll.size() - 4)
                                 : dll;
    if (stem == "ucrtbased" ||
        (stem.rfind("vcruntime", 0) == 0 && !stem.empty() && stem.back() == 'd') ||
        (stem.rfind("msvcp", 0) == 0 && !stem.empty() && stem.back() == 'd')) {
        return false;
    }
    static const std::set<std::string> exact = {
        "advapi32.dll",    "bcrypt.dll",       "cfgmgr32.dll",   "combase.dll",
        "comctl32.dll",    "crypt32.dll",      "d3d11.dll",      "d3d12.dll",
        "dcomp.dll",       "dwmapi.dll",       "dxgi.dll",       "gdi32.dll",
        "imm32.dll",       "iphlpapi.dll",     "kernel32.dll",   "msvcrt.dll",
        "ntdll.dll",
        "ole32.dll",       "oleaut32.dll",     "propsys.dll",    "rpcrt4.dll",
        "secur32.dll",     "setupapi.dll",     "shell32.dll",    "shlwapi.dll",
        "user32.dll",      "uxtheme.dll",      "version.dll",    "winmm.dll",
        "winspool.drv",    "ws2_32.dll",       "wtsapi32.dll",   "ucrtbase.dll",
        "xinput1_4.dll",   "xinput9_1_0.dll",  "d3dcompiler_47.dll",
        "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll"};
    if (exact.find(dll) != exact.end())
        return true;
    return dll.rfind("api-ms-win-", 0) == 0 || dll.rfind("ext-ms-win-", 0) == 0 ||
           dll.rfind("vcruntime", 0) == 0 || dll.rfind("msvcp", 0) == 0;
}

std::optional<fs::path> findAdjacentFileCaseInsensitive(const fs::path &dir,
                                                        const std::string &filename) {
    const fs::path direct = dir / filename;
    std::error_code directEc;
    if (fs::is_regular_file(direct, directEc))
        return direct;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(dir, ec)) {
        if (ec)
            break;
        std::error_code statusEc;
        if (!fs::is_regular_file(entry.path(), statusEc))
            continue;
        if (lowerAscii(entry.path().filename().generic_string()) == filename)
            return entry.path();
    }
    return std::nullopt;
}

std::vector<fs::path> discoverAdjacentDllDependencies(const fs::path &exePath) {
    std::vector<fs::path> deps;
    const fs::path dir = exePath.parent_path();
    std::set<std::string> seen;
    std::vector<fs::path> queue{exePath};
    for (size_t index = 0; index < queue.size(); ++index) {
        const fs::path current = queue[index];
        const auto imports = importedDllNamesFromPe(readFile(current.string()));
        for (const auto &dll : imports) {
            if (!seen.insert(dll).second)
                continue;
            if (isKnownWindowsRedistributableDll(dll))
                continue;
            const auto local = findAdjacentFileCaseInsensitive(dir, dll);
            if (local) {
                deps.push_back(*local);
                queue.push_back(*local);
                continue;
            }
            throw std::runtime_error("Windows package executable imports non-system DLL '" + dll +
                                     "' but no adjacent DLL was found next to " +
                                     exePath.string());
        }
    }
    return deps;
}

/// @brief Build the Windows ProgID string for a file association in app packages.
/// Format: "<pkg.identifier>.<ext>" — e.g. "com.example.myapp.zia".
/// ProgIDs are registered in HKEY_CLASSES_ROOT and link the extension to the app.
std::string windowsProgIdFor(const PackageConfig &pkg,
                             const std::string &exec,
                             const FileAssoc &assoc) {
    std::string ext = assoc.extension;
    if (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    std::string base = pkg.identifier.empty() ? ("viper." + exec) : pkg.identifier;
    validateWindowsProgIdBase(base, "Windows file association ProgID base");
    validateFileAssociation(assoc.extension, assoc.description, assoc.mimeType);
    return base + "." + ext;
}

/// @brief Build a %ProgramFiles%\<installDir>[\<leaf>] path string for use in .lnk
/// shortcut files and installer metadata. The %ProgramFiles% token is expanded
/// by Windows at shortcut resolution time, so the path is machine-independent.
std::string windowsInstallEnvPath(const std::string &installDir,
                                  bool perUserInstall,
                                  const std::string &leaf = {}) {
    std::string path = perUserInstall ? "%LocalAppData%\\" : "%ProgramFiles%\\";
    path += installDir;
    if (!leaf.empty())
        path += "\\" + leaf;
    return path;
}

/// @brief Return a YYYYMMDD date for Add/Remove Programs InstallDate metadata.
std::string currentInstallDate() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%Y%m%d");
    return os.str();
}

/// @brief Estimate Add/Remove Programs installed size from files installed on disk.
uint32_t estimatedInstalledSizeKb(const WindowsPackageLayout &layout) {
    uint64_t total = 0;
    for (const auto &file : layout.installFiles) {
        if (file.root == WindowsInstallRoot::InstallDir)
            total += file.sizeBytes;
    }
    const uint64_t kb = (total + 1023u) / 1024u;
    return kb > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(kb);
}

std::string windowsManifestForLayout(const WindowsPackageLayout &layout,
                                     const std::string &minOsWindows) {
    return layout.perUserInstall ? generateAsInvokerManifest(minOsWindows)
                                 : generateUacManifest(minOsWindows);
}

std::array<uint16_t, 4> windowsVersionPartsForResource(const std::string &version) {
    std::array<uint16_t, 4> parts{0, 0, 0, 0};
    size_t partIndex = 0;
    size_t pos = 0;
    while (pos < version.size() && partIndex < parts.size()) {
        if (!std::isdigit(static_cast<unsigned char>(version[pos])))
            break;
        uint32_t value = 0;
        while (pos < version.size() &&
               std::isdigit(static_cast<unsigned char>(version[pos]))) {
            value = value * 10u + static_cast<uint32_t>(version[pos] - '0');
            if (value > 65535u)
                throw std::runtime_error("Windows VERSIONINFO numeric version component "
                                         "exceeds 65535: " +
                                         version);
            ++pos;
        }
        parts[partIndex++] = static_cast<uint16_t>(value);
        if (pos >= version.size() || version[pos] != '.')
            break;
        ++pos;
    }
    return parts;
}

PEVersionInfo windowsVersionInfoForLayout(const WindowsPackageLayout &layout,
                                          const std::string &filename,
                                          const std::string &descriptionSuffix) {
    const std::string version = layout.version.empty() ? "0.0.0" : layout.version;
    PEVersionInfo info;
    info.enabled = true;
    info.fileVersion = windowsVersionPartsForResource(version);
    info.productVersion = info.fileVersion;
    info.companyName = layout.publisher;
    info.fileDescription =
        descriptionSuffix.empty() ? layout.displayName : layout.displayName + " " + descriptionSuffix;
    info.fileVersionText = version;
    info.internalName = filename;
    info.originalFilename = filename;
    info.productName = layout.displayName;
    info.productVersionText = version;
    return info;
}

/// @brief Build the Windows ProgID for a toolchain file association.
/// Equivalent to windowsProgIdFor but takes an explicit identifier string instead
/// of a full PackageConfig; used for toolchain installer builds.
std::string toolchainProgIdFor(const std::string &identifier, const FileAssoc &assoc) {
    validateWindowsProgIdBase(identifier, "Windows file association ProgID base");
    validateFileAssociation(assoc.extension, assoc.description, assoc.mimeType);
    std::string ext = assoc.extension;
    if (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    return identifier + "." + ext;
}

/// @brief Return the command-line arguments the viper binary uses to open files of
/// this extension: "-run" for pre-compiled .il modules, "run" for source files.
std::string toolchainOpenCommandArgsFor(const FileAssoc &assoc) {
    std::string ext = assoc.extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == ".il" ? "-run" : "run";
}

/// @brief Walk every parent directory segment of relativeFilePath and ensure each one
/// is present in out. Directories are added shallowest-first so the installer
/// creates parent directories before their children. Duplicate entries are
/// suppressed via the seen set.
void addParentDirs(std::vector<WindowsPackageDirEntry> &out,
                   std::set<std::string> &seen,
                   WindowsInstallRoot root,
                   const std::string &relativeFilePath) {
    fs::path rel(relativeFilePath);
    fs::path cur = rel.parent_path();
    if (cur.empty())
        return;

    std::vector<std::string> dirs;
    while (!cur.empty() && cur != ".") {
        dirs.push_back(sanitizePackageRelativePath(cur.generic_string(), "windows package path"));
        cur = cur.parent_path();
    }
    std::reverse(dirs.begin(), dirs.end());
    for (const auto &dir : dirs)
        addUniqueDir(out, seen, root, dir);
}

/// @brief Add a file to the ZIP overlay and register corresponding install/uninstall
/// entries in layout. The layout entry captures the local-data offset and CRC-32
/// from the freshly-written ZIP entry so the installer stub can locate and verify
/// each file without parsing the central directory at runtime.
void appendPayloadManifestEntry(std::ostringstream *payloadManifest,
                                const std::string &overlayName,
                                const uint8_t *data,
                                size_t len) {
    if (payloadManifest == nullptr)
        return;
    *payloadManifest << sha256Hex(data, len) << "  "
                     << sanitizePackageRelativePath(overlayName,
                                                    "Windows package manifest path")
                     << "\n";
}

void addOverlayFile(ZipWriter &zip,
                    const std::string &overlayName,
                    const uint8_t *data,
                    size_t len,
                    uint32_t unixMode,
                    WindowsPackageLayout &layout,
                    WindowsInstallRoot root,
                    const std::string &installRelativePath,
                    bool deleteOnUninstall,
                    std::ostringstream *payloadManifest = nullptr) {
    zip.addFile(overlayName, data, len, unixMode);
    const auto &entry = zip.layoutEntries().back();
    if (entry.method != 0 || entry.compressedSize != entry.uncompressedSize) {
        throw std::runtime_error("Windows installer overlay entries must be stored without "
                                 "compression: " +
                                 overlayName);
    }
    layout.installFiles.push_back(WindowsPackageFileEntry{
        root, installRelativePath, entry.localDataOffset, entry.uncompressedSize, entry.crc32});
    appendPayloadManifestEntry(payloadManifest, overlayName, data, len);
    if (deleteOnUninstall) {
        layout.uninstallFiles.push_back(
            WindowsPackageFileEntry{root, installRelativePath, 0, entry.uncompressedSize, 0});
    }
}

/// @brief Populate uninstallDirectories from installDirectories, then sort deepest-first
/// so the uninstaller removes leaf directories before their parents. Sorting by
/// depth first, then path length, ensures correct order when depths are equal.
void finalizeUninstallDirs(WindowsPackageLayout &layout) {
    layout.uninstallDirectories = layout.installDirectories;
    std::stable_sort(layout.uninstallDirectories.begin(),
                     layout.uninstallDirectories.end(),
                     [](const WindowsPackageDirEntry &a, const WindowsPackageDirEntry &b) {
                         auto depth = [](const std::string &path) {
                             return static_cast<int>(std::count(path.begin(), path.end(), '/'));
                         };
                         const int da = depth(a.relativePath);
                         const int db = depth(b.relativePath);
                         if (da != db)
                             return da > db;
                         return a.relativePath.size() > b.relativePath.size();
                     });
}

} // namespace

/// @brief Build a Windows self-extracting installer for a single application binary.
/// Assembly is a two-pass process: Pass 1 measures the exact overlay offset with
/// overlayFileOffset=0; Pass 2 bakes the measured offset into the stub and produces
/// the final PE. The uninstaller is built first so it can be included in the ZIP.
void buildWindowsPackage(const WindowsBuildParams &params) {
    const auto &pkg = params.pkgConfig;
    const std::string displayName = pkg.displayName.empty() ? params.projectName : pkg.displayName;
    const std::string exec = normalizeExecName(params.projectName);
    const std::string installDir = pkg.windowsInstallDir.empty() ? displayName : pkg.windowsInstallDir;
    validateWindowsFileName(displayName, "Windows display name");
    validateWindowsFileName(installDir, "Windows install directory");
    validateWindowsProgIdBase(pkg.identifier, "Windows package identifier");
    validateSingleLineField(params.version.empty() ? "0.0.0" : params.version,
                            "Windows package version");
    if (!pkg.minOsWindows.empty())
        validateDottedNumericVersion(pkg.minOsWindows, "minimum Windows version");
    validateSingleLineField(pkg.author, "Windows package author");
    validateSingleLineField(pkg.description, "Windows package description");
    validatePackageUrl(pkg.homepage, "package homepage");
    validatePackageFileAssociations(pkg.fileAssociations);
    if (!fs::is_regular_file(params.executablePath))
        throw std::runtime_error("Windows package executable is not a regular file: " +
                                 params.executablePath);
    validateWindowsPayloadExecutable(params.executablePath, params.archStr);

    WindowsPackageLayout layout;
    layout.displayName = displayName;
    layout.installDirName = installDir;
    layout.version = params.version;
    layout.identifier = pkg.identifier;
    layout.publisher = pkg.author;
    layout.executableName = exec + ".exe";
    layout.perUserInstall = pkg.windowsInstallScope == "user";
    layout.homepage = pkg.homepage;
    layout.installDate = currentInstallDate();
    layout.createDesktopShortcut = pkg.shortcutDesktop;
    layout.createStartMenuShortcut = pkg.shortcutMenu;
    layout.cleanInstallRootBeforeInstall = false;
    for (const auto &assoc : pkg.fileAssociations) {
        layout.fileAssociations.push_back(
            {assoc.extension,
             assoc.description,
             assoc.mimeType,
             windowsProgIdFor(pkg, exec, assoc),
             assoc.openCommandArguments});
    }

    std::set<std::string> installDirSet;

    ZipWriter zip;
    zip.setCompressionEnabled(false);
    zip.addDirectory("app/");
    zip.addDirectory("meta/");
    std::ostringstream payloadManifest;

    std::vector<uint8_t> icoData;
    if (!pkg.iconPath.empty()) {
        fs::path iconSrc = resolvePackageSourcePath(params.projectRoot, pkg.iconPath, "package icon");
        if (!fs::is_regular_file(iconSrc))
            throw std::runtime_error("package icon not found: " + pkg.iconPath);
        auto srcImage = pngRead(iconSrc.string());
        icoData = generateIco(srcImage);
    }

    const auto execData = readFile(params.executablePath);
    addOverlayFile(zip,
                   "app/" + exec + ".exe",
                   execData.data(),
                   execData.size(),
                   0100755,
                   layout,
                   WindowsInstallRoot::InstallDir,
                   exec + ".exe",
                   true,
                   &payloadManifest);

    for (const auto &dllPath : discoverAdjacentDllDependencies(params.executablePath)) {
        const std::string dllName = dllPath.filename().generic_string();
        validateWindowsRelativePath(dllName, "Windows DLL dependency path");
        const auto dllData = readFile(dllPath.string());
        addOverlayFile(zip,
                       "app/" + dllName,
                       dllData.data(),
                       dllData.size(),
                       0100644,
                       layout,
                       WindowsInstallRoot::InstallDir,
                       dllName,
                       true,
                       &payloadManifest);
    }

    if (!icoData.empty()) {
        addOverlayFile(zip,
                       "app/" + exec + ".ico",
                       icoData.data(),
                       icoData.size(),
                       0100644,
                       layout,
                       WindowsInstallRoot::InstallDir,
                       exec + ".ico",
                       true,
                       &payloadManifest);
    }
    layout.displayIconRelativePath = !icoData.empty() ? exec + ".ico" : exec + ".exe";

    for (const auto &asset : pkg.assets) {
        fs::path srcPath = resolvePackageSourcePath(params.projectRoot, asset.sourcePath, "asset source path");
        const std::string targetDir =
            sanitizePackageRelativePath(asset.targetPath, "asset target path");
        validateWindowsRelativePath(targetDir, "asset target path");

        if (!fs::exists(srcPath))
            throw std::runtime_error("asset not found: " + asset.sourcePath);

        if (fs::is_directory(srcPath)) {
            if (!targetDir.empty()) {
                addUniqueDir(layout.installDirectories,
                             installDirSet,
                             WindowsInstallRoot::InstallDir,
                             targetDir);
            }
            safeDirectoryIterateResolved(
                srcPath, params.projectRoot, [&](const SafeDirectoryEntry &entry) {
                    const auto relPath = sanitizePackageRelativePath(
                        entry.logicalPath.lexically_relative(srcPath).generic_string(),
                        "asset path");
                    if (entry.directory) {
                        const std::string relInstall =
                            joinPackageRelativePath(targetDir, relPath, "asset path");
                        addUniqueDir(layout.installDirectories,
                                     installDirSet,
                                     WindowsInstallRoot::InstallDir,
                                     relInstall);
                        return;
                    }
                    if (!entry.regularFile)
                        return;

                    const std::string relInstall =
                        joinPackageRelativePath(targetDir, relPath, "asset path");
                    addParentDirs(layout.installDirectories,
                                  installDirSet,
                                  WindowsInstallRoot::InstallDir,
                                  relInstall);
                    const auto data = readFile(entry.resolvedPath.string());
                    addOverlayFile(zip,
                                   "app/" + relInstall,
                                   data.data(),
                                   data.size(),
                                   0100644,
                                   layout,
                                   WindowsInstallRoot::InstallDir,
                                   relInstall,
                                   true,
                                   &payloadManifest);
                });
        } else if (fs::is_regular_file(srcPath)) {
            const std::string relInstall = joinPackageRelativePath(
                targetDir, srcPath.filename().generic_string(), "asset path");
            addParentDirs(layout.installDirectories,
                          installDirSet,
                          WindowsInstallRoot::InstallDir,
                          relInstall);
            const auto data = readFile(srcPath.string());
            addOverlayFile(zip,
                           "app/" + relInstall,
                           data.data(),
                           data.size(),
                           0100644,
                           layout,
                           WindowsInstallRoot::InstallDir,
                           relInstall,
                           true,
                           &payloadManifest);
        } else {
            throw std::runtime_error("asset is not a regular file or directory: " +
                                     asset.sourcePath);
        }
    }

    if (pkg.shortcutMenu) {
        LnkParams lnk;
        lnk.targetPath = windowsInstallEnvPath(installDir, layout.perUserInstall, exec + ".exe");
        lnk.workingDir = windowsInstallEnvPath(installDir, layout.perUserInstall);
        lnk.description = displayName;
        if (!icoData.empty())
            lnk.iconPath = windowsInstallEnvPath(installDir, layout.perUserInstall, exec + ".ico");
        const auto lnkData = generateLnk(lnk);
        addOverlayFile(zip,
                       "meta/start_menu.lnk",
                       lnkData.data(),
                       lnkData.size(),
                       0100644,
                       layout,
                       WindowsInstallRoot::StartMenuDir,
                       displayName + ".lnk",
                       true,
                       &payloadManifest);
    }

    if (pkg.shortcutDesktop) {
        LnkParams lnk;
        lnk.targetPath = windowsInstallEnvPath(installDir, layout.perUserInstall, exec + ".exe");
        lnk.workingDir = windowsInstallEnvPath(installDir, layout.perUserInstall);
        lnk.description = displayName;
        if (!icoData.empty())
            lnk.iconPath = windowsInstallEnvPath(installDir, layout.perUserInstall, exec + ".ico");
        const auto lnkData = generateLnk(lnk);
        addOverlayFile(zip,
                       "meta/desktop.lnk",
                       lnkData.data(),
                       lnkData.size(),
                       0100644,
                       layout,
                       WindowsInstallRoot::DesktopDir,
                       displayName + ".lnk",
                       true,
                       &payloadManifest);
    }

    finalizeUninstallDirs(layout);
    validateWindowsLayoutFitsStub(layout);

    auto uninstStub = buildUninstallerStub(layout, params.archStr);
    PEBuildParams uninstPe;
    uninstPe.arch = uninstStub.peArch;
    uninstPe.textSection = uninstStub.textSection;
    uninstPe.rdataSection = uninstStub.stubData;
    uninstPe.imports = uninstStub.imports;
    uninstPe.manifest = windowsManifestForLayout(layout, pkg.minOsWindows);
    uninstPe.iconData = icoData;
    uninstPe.versionInfo = windowsVersionInfoForLayout(layout, "uninstall.exe", "Uninstaller");
    configureInstallerStack(uninstPe);
    auto uninstBytes = buildPE(uninstPe);
    addOverlayFile(zip,
                   "app/uninstall.exe",
                   uninstBytes.data(),
                   uninstBytes.size(),
                   0100755,
                   layout,
                   WindowsInstallRoot::InstallDir,
                   "uninstall.exe",
                   false,
                   &payloadManifest);
    layout.estimatedSizeKb = estimatedInstalledSizeKb(layout);
    const std::string manifestText = payloadManifest.str();
    zip.addFile("meta/manifest.sha256",
                reinterpret_cast<const uint8_t *>(manifestText.data()),
                manifestText.size(),
                0100644);

    const auto zipPayload = zip.finishToVector();

    layout.overlayFileOffset = 0;
    auto provisionalStub = buildInstallerStub(layout, params.archStr);
    PEBuildParams provisionalPe;
    provisionalPe.arch = provisionalStub.peArch;
    provisionalPe.textSection = provisionalStub.textSection;
    provisionalPe.rdataSection = provisionalStub.stubData;
    provisionalPe.imports = provisionalStub.imports;
    provisionalPe.manifest = windowsManifestForLayout(layout, pkg.minOsWindows);
    provisionalPe.iconData = icoData;
    provisionalPe.overlay = zipPayload;
    provisionalPe.versionInfo = windowsVersionInfoForLayout(
        layout, fs::path(params.outputPath).filename().generic_string(), "Setup");
    configureInstallerStack(provisionalPe);
    const auto provisionalBytes = buildPE(provisionalPe);
    layout.overlayFileOffset = static_cast<uint64_t>(provisionalBytes.size() - zipPayload.size());

    auto instStub = buildInstallerStub(layout, params.archStr);
    PEBuildParams pe;
    pe.arch = instStub.peArch;
    pe.textSection = instStub.textSection;
    pe.rdataSection = instStub.stubData;
    pe.imports = instStub.imports;
    pe.manifest = windowsManifestForLayout(layout, pkg.minOsWindows);
    pe.iconData = icoData;
    pe.overlay = zipPayload;
    pe.versionInfo = windowsVersionInfoForLayout(
        layout, fs::path(params.outputPath).filename().generic_string(), "Setup");
    configureInstallerStack(pe);

    const auto peBytes = buildPE(pe);
    writePEToFile(peBytes, params.outputPath);
}

/// @brief Build a Windows toolchain installer from a pre-validated staged manifest.
/// Unlike buildWindowsPackage, all files come from the manifest (no asset globs, no
/// PNG icon conversion, no desktop/Start Menu shortcuts). PATH registration and file
/// associations for .zia/.bas/.il are always enabled. Symlinks are dereferenced.
void buildWindowsToolchainInstaller(const WindowsToolchainBuildParams &params) {
    validateToolchainInstallManifest(params.manifest);
    if (params.manifest.platform != "windows") {
        throw std::runtime_error("Windows toolchain installer requires a Windows staged "
                                 "toolchain manifest, got '" +
                                 params.manifest.platform + "'");
    }
    validateWindowsFileName(params.displayName, "Windows display name");
    validateWindowsProgIdBase(params.identifier, "Windows package identifier");
    validateSingleLineField(params.publisher, "Windows package publisher");
    if (params.manifest.version.empty())
        throw std::runtime_error("toolchain package version is required");
    validateSingleLineField(params.manifest.version, "Windows package version");
    WindowsPackageLayout layout;
    layout.displayName = params.displayName;
    layout.installDirName = "Viper";
    layout.version = params.manifest.version;
    layout.identifier = params.identifier;
    layout.publisher = params.publisher;
    layout.executableName = "viper.exe";
    layout.homepage = "https://github.com/viper-org/viper";
    layout.displayIconRelativePath = "bin\\viper.exe";
    layout.installDate = currentInstallDate();
    layout.createDesktopShortcut = false;
    layout.createStartMenuShortcut = false;
    layout.addToPath = true;
    layout.cleanInstallRootBeforeInstall = true;
    layout.pathRelativePath = "bin";
    layout.fileAssociationExecutableRelativePath = "bin\\viper.exe";
    for (const auto &assoc : params.manifest.fileAssociations) {
        layout.fileAssociations.push_back(
            {assoc.extension,
             assoc.description,
             assoc.mimeType,
             toolchainProgIdFor(params.identifier, assoc),
             toolchainOpenCommandArgsFor(assoc)});
    }

    std::set<std::string> installDirSet;

    ZipWriter zip;
    zip.setCompressionEnabled(false);
    zip.addDirectory("app/");
    zip.addDirectory("meta/");
    std::ostringstream payloadManifest;

    for (const auto &file : params.manifest.files) {
        const std::string relInstall =
            sanitizePackageRelativePath(file.stagedRelativePath, "windows toolchain path");
        validateWindowsRelativePath(relInstall, "windows toolchain path");
        if (file.symlink && !fs::is_regular_file(file.stagedAbsolutePath)) {
            throw std::runtime_error("Windows toolchain installers can only dereference "
                                     "symlinks to regular files: " +
                                     file.stagedRelativePath);
        }
        addParentDirs(layout.installDirectories,
                      installDirSet,
                      WindowsInstallRoot::InstallDir,
                      relInstall);
        const auto data = readFile(file.stagedAbsolutePath.string());
        addOverlayFile(zip,
                       "app/" + relInstall,
                       data.data(),
                       data.size(),
                       file.executable ? 0100755 : 0100644,
                       layout,
                       WindowsInstallRoot::InstallDir,
                       relInstall,
                       true,
                       &payloadManifest);

        const std::string lowerName = lowerAscii(fs::path(relInstall).filename().generic_string());
        if (lowerName == "license" || lowerName == "readme.md") {
            const std::string overlayName =
                lowerName == "license" ? "meta/license.txt" : "meta/readme.txt";
            zip.addFile(overlayName, data.data(), data.size(), 0100644);
            appendPayloadManifestEntry(&payloadManifest, overlayName, data.data(), data.size());
        }
    }

    finalizeUninstallDirs(layout);
    validateWindowsLayoutFitsStub(layout);

    auto uninstStub = buildUninstallerStub(layout, params.archStr);
    PEBuildParams uninstPe;
    uninstPe.arch = uninstStub.peArch;
    uninstPe.textSection = uninstStub.textSection;
    uninstPe.rdataSection = uninstStub.stubData;
    uninstPe.imports = uninstStub.imports;
    uninstPe.manifest = generateUacManifest();
    uninstPe.versionInfo = windowsVersionInfoForLayout(layout, "uninstall.exe", "Uninstaller");
    configureInstallerStack(uninstPe);
    const auto uninstBytes = buildPE(uninstPe);
    addOverlayFile(zip,
                   "app/uninstall.exe",
                   uninstBytes.data(),
                   uninstBytes.size(),
                   0100755,
                   layout,
                   WindowsInstallRoot::InstallDir,
                   "uninstall.exe",
                   false,
                   &payloadManifest);
    layout.estimatedSizeKb = estimatedInstalledSizeKb(layout);
    const std::string manifestText = payloadManifest.str();
    zip.addFile("meta/manifest.sha256",
                reinterpret_cast<const uint8_t *>(manifestText.data()),
                manifestText.size(),
                0100644);

    const auto zipPayload = zip.finishToVector();

    layout.overlayFileOffset = 0;
    auto provisionalStub = buildInstallerStub(layout, params.archStr);
    PEBuildParams provisionalPe;
    provisionalPe.arch = provisionalStub.peArch;
    provisionalPe.textSection = provisionalStub.textSection;
    provisionalPe.rdataSection = provisionalStub.stubData;
    provisionalPe.imports = provisionalStub.imports;
    provisionalPe.manifest = generateUacManifest();
    provisionalPe.overlay = zipPayload;
    provisionalPe.versionInfo = windowsVersionInfoForLayout(
        layout, fs::path(params.outputPath).filename().generic_string(), "Setup");
    configureInstallerStack(provisionalPe);
    const auto provisionalBytes = buildPE(provisionalPe);
    layout.overlayFileOffset = static_cast<uint64_t>(provisionalBytes.size() - zipPayload.size());

    auto instStub = buildInstallerStub(layout, params.archStr);
    PEBuildParams pe;
    pe.arch = instStub.peArch;
    pe.textSection = instStub.textSection;
    pe.rdataSection = instStub.stubData;
    pe.imports = instStub.imports;
    pe.manifest = generateUacManifest();
    pe.overlay = zipPayload;
    pe.versionInfo = windowsVersionInfoForLayout(
        layout, fs::path(params.outputPath).filename().generic_string(), "Setup");
    configureInstallerStack(pe);
    const auto peBytes = buildPE(pe);
    writePEToFile(peBytes, params.outputPath);
}

} // namespace viper::pkg
