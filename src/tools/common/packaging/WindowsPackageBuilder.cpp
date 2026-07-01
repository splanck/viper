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
//   - Overlay is a structurally valid ZIP archive using stored bootstrap entries.
//     The main payload is a DEFLATE-compressed inner ZIP expanded at install time.
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
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

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

/// @brief Return a conservative environment-expanded probe for a Windows install root.
/// @details The installer resolves known folders at runtime. These probes mirror
///          the longest common expanded locations closely enough for preflight
///          buffer checks, including Desktop and Start Menu entries that are not
///          under the package install directory.
/// @param layout Package layout determining per-user versus machine roots.
/// @param root Root anchor to probe.
/// @return Representative absolute path prefix for @p root.
std::string windowsRootProbeFor(const WindowsPackageLayout &layout, WindowsInstallRoot root) {
    const std::string installDir =
        layout.installDirName.empty() ? layout.displayName : layout.installDirName;
    switch (root) {
        case WindowsInstallRoot::DesktopDir:
            return layout.perUserInstall ? "%UserProfile%\\Desktop" : "%Public%\\Desktop";
        case WindowsInstallRoot::StartMenuDir:
            return layout.perUserInstall
                       ? "%AppData%\\Microsoft\\Windows\\Start Menu\\Programs"
                       : "%ProgramData%\\Microsoft\\Windows\\Start Menu\\Programs";
        case WindowsInstallRoot::InstallDir:
        default:
            return (layout.perUserInstall ? "%LocalAppData%\\" : "%ProgramFiles%\\") + installDir;
    }
}

/// @brief Add a path to a case-insensitive collision set for a Windows root.
/// @details Windows destination paths are case-insensitive on normal NTFS
///          installs. This catches `Readme.txt` vs `README.TXT` collisions
///          before the overlay is built and one entry overwrites another.
/// @param seen Root-qualified normalized path set.
/// @param root Destination root anchor.
/// @param relativePath Root-relative path to validate and insert.
/// @param fieldName Diagnostic field name.
void addWindowsCaseFoldedPath(std::set<std::string> &seen,
                              WindowsInstallRoot root,
                              const std::string &relativePath,
                              const char *fieldName) {
    const std::string clean = sanitizePackageRelativePath(relativePath, fieldName);
    if (clean.empty())
        return;
    std::string folded = clean;
    for (char &c : folded)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::string key = std::to_string(static_cast<unsigned long long>(root)) + ":" + folded;
    if (!seen.insert(key).second)
        throw std::runtime_error(
            std::string(fieldName) +
            " collides with another path on case-insensitive Windows: " + clean);
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
    const std::string installDir =
        layout.installDirName.empty() ? layout.displayName : layout.installDirName;
    validateWindowsFileName(installDir, "Windows install directory");
    const std::string rootProbe = windowsRootProbeFor(layout, WindowsInstallRoot::InstallDir);
    validateStubPathFits(rootProbe, "Windows install directory");
    std::set<std::string> caseFoldedPaths;
    for (const auto &dir : layout.installDirectories) {
        validateWindowsRelativePath(dir.relativePath, "Windows install directory path");
        validateStubPathFits(windowsRootProbeFor(layout, dir.root) + "\\" + dir.relativePath,
                             "Windows install directory path");
        addWindowsCaseFoldedPath(
            caseFoldedPaths, dir.root, dir.relativePath, "Windows install directory path");
    }
    for (const auto &file : layout.installFiles) {
        validateWindowsRelativePath(file.relativePath, "Windows install file path");
        validateStubPathFits(windowsRootProbeFor(layout, file.root) + "\\" + file.relativePath,
                             "Windows install file path");
        addWindowsCaseFoldedPath(
            caseFoldedPaths, file.root, file.relativePath, "Windows install file path");
    }
    for (const auto &file : layout.installedFiles) {
        validateWindowsRelativePath(file.relativePath, "Windows installed file path");
        validateStubPathFits(windowsRootProbeFor(layout, file.root) + "\\" + file.relativePath,
                             "Windows installed file path");
        addWindowsCaseFoldedPath(
            caseFoldedPaths, file.root, file.relativePath, "Windows installed file path");
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
    for (const auto &file : layout.uninstallFiles) {
        validateWindowsRelativePath(file.relativePath, "Windows uninstall file path");
        validateStubPathFits(windowsRootProbeFor(layout, file.root) + "\\" + file.relativePath,
                             "Windows uninstall file path");
    }
    for (const auto &assoc : layout.fileAssociations) {
        validateSingleLineField(assoc.openCommandArguments,
                                "Windows file association command arguments");
        for (char c : assoc.openCommandArguments) {
            const unsigned char uc = static_cast<unsigned char>(c);
            if (uc < 0x20 || c == '"' || c == '&' || c == '|' || c == '<' || c == '>' || c == '^' ||
                c == '%') {
                throw std::runtime_error("Windows file association command arguments contain "
                                         "unsafe command-line syntax: " +
                                         assoc.openCommandArguments);
            }
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

/// @brief Read a little-endian uint16 at @p off (caller must bounds-check).
uint16_t rd16(const std::vector<uint8_t> &data, size_t off) {
    return static_cast<uint16_t>(data[off] | (data[off + 1] << 8));
}

/// @brief Read a little-endian uint32 at @p off (caller must bounds-check).
uint32_t rd32(const std::vector<uint8_t> &data, size_t off) {
    return static_cast<uint32_t>(data[off]) | (static_cast<uint32_t>(data[off + 1]) << 8) |
           (static_cast<uint32_t>(data[off + 2]) << 16) |
           (static_cast<uint32_t>(data[off + 3]) << 24);
}

/// @brief Return true if [off, off+len) lies entirely within @p data.
bool hasBytes(const std::vector<uint8_t> &data, size_t off, size_t len) {
    return off <= data.size() && len <= data.size() - off;
}

/// @brief Rotate a 32-bit value right by @p bits (SHA-256 round operation).
uint32_t rotr32(uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32u - bits));
}

/// @brief Compute the SHA-256 of a buffer as a lowercase hex string.
/// @details Self-contained SHA-256 used to fingerprint payload files in the
///          installer's integrity manifest; kept local to avoid a runtime dep.
std::string sha256Hex(const uint8_t *data, size_t len) {
    static constexpr std::array<uint32_t, 64> k = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
        0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
        0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
        0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
        0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
        0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
        0xc67178f2u};
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
                   (static_cast<uint32_t>(msg[p + 2]) << 8) | static_cast<uint32_t>(msg[p + 3]);
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

/// @brief One PE section's RVA/size and file-offset/size, for RVA→offset mapping.
struct PeSectionInfo {
    uint32_t rva{0};         ///< Virtual address of the section.
    uint32_t virtualSize{0}; ///< Virtual size of the section.
    uint32_t rawOffset{0};   ///< File offset of the section's raw data.
    uint32_t rawSize{0};     ///< File size of the section's raw data.
};

/// @brief Minimal identifying info about a PE image: machine type and PE32+ flag.
struct PeExecutableInfo {
    uint16_t machine{0};  ///< COFF Machine field (0x8664 = AMD64, 0xAA64 = ARM64).
    bool pe32Plus{false}; ///< True when the optional header magic is 0x020B (PE32+).
};

/// @brief Parse just enough of a PE image to extract its machine type and bitness.
/// @return PeExecutableInfo, or std::nullopt if the bytes are not a valid PE image.
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

/// @brief Map an architecture string to its PE COFF Machine value.
/// @throws std::runtime_error for anything other than "" / "x64" / "arm64".
uint16_t windowsMachineForArch(const std::string &arch) {
    if (arch.empty() || arch == "x64")
        return 0x8664;
    if (arch == "arm64")
        return 0xAA64;
    throw std::runtime_error("unsupported Windows package architecture '" + arch + "'");
}

/// @brief Validate that the payload binary at @p path is a PE32+ for @p arch.
/// @throws std::runtime_error if the file is not PE32+ or its machine type does
///         not match the requested architecture.
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

/// @brief Translate a PE relative virtual address to a file offset.
/// @details Searches @p sections for the one containing @p rva and maps it into
///          that section's raw data. Returns nullopt when no section covers the
///          RVA or the resulting offset would exceed @p fileSize.
std::optional<size_t> peRvaToFileOffset(const std::vector<PeSectionInfo> &sections,
                                        uint32_t rva,
                                        size_t fileSize) {
    for (const auto &sec : sections) {
        if (sec.rawSize == 0)
            continue;
        const uint32_t sectionSpan = std::max(sec.virtualSize, sec.rawSize);
        if (rva >= sec.rva && rva - sec.rva < sectionSpan) {
            const uint32_t delta = rva - sec.rva;
            if (delta >= sec.rawSize)
                return std::nullopt;
            const size_t off = static_cast<size_t>(sec.rawOffset) + static_cast<size_t>(delta);
            if (off < fileSize)
                return off;
        }
    }
    return std::nullopt;
}

/// @brief Read a NUL-terminated printable-ASCII string from @p data at @p off.
/// @return The string, or "" if a non-printable byte or an unterminated run is
///         encountered (used to read DLL name strings from the import table).
std::string readPeAsciiZ(const std::vector<uint8_t> &data, size_t off) {
    std::string out;
    while (off < data.size() && data[off] != 0) {
        const unsigned char ch = data[off++];
        if (ch < 0x20 || ch > 0x7e)
            return {};
        out.push_back(static_cast<char>(ch));
    }
    return off < data.size() ? out : std::string{};
}

/// @brief Parse a PE32+ import directory and return the imported DLL names.
/// @details Walks the import data directory's IMAGE_IMPORT_DESCRIPTOR array,
///          mapping each Name RVA to a file offset and reading the DLL string.
///          Returns an empty vector if the image is not a parseable PE32+.
std::vector<std::string> importedDllNamesFromPeImpl(const std::vector<uint8_t> &data) {
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
    if (!hasBytes(data, optOff, optSize) || rd16(data, optOff) != 0x020B || optSize < 128)
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

    const auto importOff = peRvaToFileOffset(sections, importRva, data.size());
    if (!importOff)
        return {};
    const uint64_t importEnd64 = static_cast<uint64_t>(*importOff) + importSize;
    if (importSize < 20 || importEnd64 > data.size())
        return {};
    const size_t importEnd = static_cast<size_t>(importEnd64);

    std::vector<std::string> names;
    bool sawTerminator = false;
    for (size_t desc = *importOff; desc <= importEnd - 20; desc += 20) {
        const uint32_t originalThunk = rd32(data, desc);
        const uint32_t nameRva = rd32(data, desc + 12);
        const uint32_t firstThunk = rd32(data, desc + 16);
        if (originalThunk == 0 && nameRva == 0 && firstThunk == 0) {
            sawTerminator = true;
            break;
        }
        const auto nameOff = peRvaToFileOffset(sections, nameRva, data.size());
        if (!nameOff)
            return {};
        std::string dll = lowerAscii(readPeAsciiZ(data, *nameOff));
        if (dll.empty())
            return {};
        names.push_back(std::move(dll));
    }
    if (!sawTerminator)
        return {};
    return names;
}

/// @brief Decide whether a DLL is a system/redistributable that need not be bundled.
/// @details Returns true for OS DLLs and the standard MSVC/UCRT redistributables
///          (release builds), plus the api-ms-win/ext-ms-win API sets; returns
///          false for debug-CRT variants (which are not redistributable) so the
///          packager can flag a debug-linked payload. Used to decide which
///          adjacent DLLs must travel with the application.
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
        "advapi32.dll",     "bcrypt.dll",         "cfgmgr32.dll",    "combase.dll",
        "comctl32.dll",     "crypt32.dll",        "d3d11.dll",       "d3d12.dll",
        "dcomp.dll",        "dwmapi.dll",         "dxgi.dll",        "gdi32.dll",
        "imm32.dll",        "iphlpapi.dll",       "kernel32.dll",    "msvcrt.dll",
        "ntdll.dll",        "ole32.dll",          "oleaut32.dll",    "propsys.dll",
        "rpcrt4.dll",       "secur32.dll",        "setupapi.dll",    "shell32.dll",
        "shlwapi.dll",      "user32.dll",         "uxtheme.dll",     "version.dll",
        "winmm.dll",        "winspool.drv",       "ws2_32.dll",      "wtsapi32.dll",
        "ucrtbase.dll",     "xinput1_4.dll",      "xinput9_1_0.dll", "d3dcompiler_47.dll",
        "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll"};
    auto hasNumericSuffix = [](std::string_view text) {
        if (text.empty())
            return false;
        bool sawDigit = false;
        for (char c : text) {
            if (c == '_')
                continue;
            if (!std::isdigit(static_cast<unsigned char>(c)))
                return false;
            sawDigit = true;
        }
        return sawDigit;
    };
    if (exact.find(dll) != exact.end())
        return true;
    static constexpr std::string_view vcruntimePrefix = "vcruntime";
    static constexpr std::string_view msvcpPrefix = "msvcp";
    if (stem.rfind("vcruntime", 0) == 0 &&
        hasNumericSuffix(std::string_view(stem).substr(vcruntimePrefix.size())))
        return true;
    if (stem.rfind("msvcp", 0) == 0 &&
        hasNumericSuffix(std::string_view(stem).substr(msvcpPrefix.size())))
        return true;
    return dll.rfind("api-ms-win-", 0) == 0 || dll.rfind("ext-ms-win-", 0) == 0;
}

/// @brief Find @p filename under @p dir, trying exact, adjacent, then bounded recursive search.
/// @details Windows applications often stage plugin or delay-load DLLs in subdirectories.
///          The recursive pass is capped so an accidental large project tree does not make
///          packaging unbounded.
/// @return The matching path, or std::nullopt when no file matches.
std::optional<fs::path> findLocalDllCaseInsensitive(const fs::path &dir,
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
    constexpr size_t kMaxRecursiveDllSearchEntries = 4096;
    size_t visited = 0;
    ec.clear();
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    while (!ec && it != end && visited++ < kMaxRecursiveDllSearchEntries) {
        const fs::path candidate = it->path();
        std::error_code statusEc;
        if (fs::is_regular_file(candidate, statusEc) &&
            lowerAscii(candidate.filename().generic_string()) == filename) {
            return candidate;
        }
        it.increment(ec);
    }
    return std::nullopt;
}

/// @brief Transitively collect non-system DLLs that ship next to @p exePath.
/// @details Breadth-first walks the import tables of the executable and each
///          discovered local DLL, skipping known redistributable/system DLLs, and
///          returns the adjacent files that must be bundled into the installer.
std::vector<fs::path> discoverAdjacentDllDependencies(const fs::path &exePath) {
    std::vector<fs::path> deps;
    const fs::path dir = exePath.parent_path();
    std::set<std::string> seen;
    std::vector<fs::path> queue{exePath};
    for (size_t index = 0; index < queue.size(); ++index) {
        const fs::path current = queue[index];
        const auto imports = importedDllNamesFromPeImpl(readFile(current.string()));
        for (const auto &dll : imports) {
            if (!seen.insert(dll).second)
                continue;
            if (isKnownWindowsRedistributableDll(dll))
                continue;
            const auto local = findLocalDllCaseInsensitive(dir, dll);
            if (local) {
                deps.push_back(*local);
                queue.push_back(*local);
                continue;
            }
            throw std::runtime_error("Windows package executable imports non-system DLL '" + dll +
                                     "' but no adjacent DLL was found next to " + exePath.string());
        }
    }
    return deps;
}

/// @brief Convert a file association extension into a ProgID-safe component.
/// @details Windows ProgIDs allow alphanumerics plus '.', '_', and '-' as
///          separators/components. File extensions can contain characters such
///          as '+', so this helper preserves the association while replacing
///          unsupported ProgID component characters with '_'.
/// @param assoc File association whose extension has already passed package validation.
/// @return Non-empty identifier component without a leading dot.
std::string windowsProgIdExtensionComponent(const FileAssoc &assoc) {
    std::string ext = assoc.extension;
    while (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    if (ext.empty())
        throw std::runtime_error("Windows file association extension must not be empty");
    for (char &c : ext) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '_' && c != '-')
            c = '_';
    }
    return ext;
}

/// @brief Build the Windows ProgID string for a file association in app packages.
/// Format: "<pkg.identifier>.<ext>" — e.g. "com.example.myapp.zia".
/// ProgIDs are registered in HKEY_CLASSES_ROOT and link the extension to the app.
std::string windowsProgIdFor(const PackageConfig &pkg,
                             const std::string &exec,
                             const FileAssoc &assoc) {
    std::string base = pkg.identifier.empty() ? ("viper." + exec) : pkg.identifier;
    validateWindowsProgIdBase(base, "Windows file association ProgID base");
    validateFileAssociation(assoc.extension, assoc.description, assoc.mimeType);
    const std::string progId = base + "." + windowsProgIdExtensionComponent(assoc);
    validateWindowsProgIdBase(progId, "Windows file association ProgID");
    return progId;
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

/// @brief Wrap @p path in double quotes for embedding in a command line.
/// @throws std::runtime_error if @p path itself contains a double quote.
std::string windowsQuotedPath(const std::string &path) {
    if (path.find('"') != std::string::npos)
        throw std::runtime_error("Windows shortcut path must not contain quotes: " + path);
    return "\"" + path + "\"";
}

/// @brief Generate the "Viper Developer Prompt" .bat that puts bin/ on PATH.
std::string toolchainDeveloperPromptScript() {
    std::ostringstream os;
    os << "@echo off\r\n"
       << "set \"VIPER_HOME=%~dp0..\"\r\n"
       << "set \"PATH=%VIPER_HOME%\\bin;%PATH%\"\r\n"
       << "if not exist \"%USERPROFILE%\" goto prompt\r\n"
       << "cd /d \"%USERPROFILE%\"\r\n"
       << ":prompt\r\n"
       << "echo Viper developer environment\r\n"
       << "echo VIPER_HOME=%VIPER_HOME%\r\n"
       << "viper --version\r\n";
    return os.str();
}

/// @brief Generate the .bat that installs the bundled VS Code (.vsix) extension.
std::string toolchainVSCodeInstallScript() {
    return "@echo off\r\n"
           "setlocal\r\n"
           "set \"VIPER_HOME=%~dp0..\"\r\n"
           "set \"VSIX_DIR=%VIPER_HOME%\\share\\viper\\vscode\"\r\n"
           "set \"VSIX=\"\r\n"
           "for %%F in (\"%VSIX_DIR%\\*.vsix\") do set \"VSIX=%%~fF\"\r\n"
           "if not defined VSIX (\r\n"
           "  echo No packaged VS Code extension was found under \"%VSIX_DIR%\".\r\n"
           "  echo Build misc\\editors\\vscode\\zia\\zia-language-*.vsix before packaging to "
           "install it here.\r\n"
           "  exit /b 2\r\n"
           ")\r\n"
           "set \"CODE_CMD=\"\r\n"
           "for %%F in (code.cmd code.exe code) do if not defined CODE_CMD (\r\n"
           "  for /f \"delims=\" %%C in ('where %%F 2^>nul') do if not defined CODE_CMD set "
           "\"CODE_CMD=%%C\"\r\n"
           ")\r\n"
           "if not defined CODE_CMD (\r\n"
           "  echo Visual Studio Code command-line launcher was not found on PATH.\r\n"
           "  exit /b 3\r\n"
           ")\r\n"
           "\"%CODE_CMD%\" --install-extension \"%VSIX%\" --force\r\n";
}

/// @brief Generate the prerequisites README text bundled with the installer.
std::string toolchainWindowsPrerequisitesReadme() {
    return "Viper Windows toolchain installer prerequisites\r\n"
           "\r\n"
           "- Windows PowerShell 5 or newer must be available under System32. The setup "
           "bootstrap uses it to expand the compressed payload.\r\n"
           "- Release builds produced with the MSVC dynamic runtime require the Microsoft "
           "Visual C++ Redistributable for Visual Studio 2015-2022 unless the staged "
           "toolchain includes the required runtime DLLs next to bin\\viper.exe or the "
           "toolchain was built with a static MSVC runtime.\r\n"
           "- Native code generation requires the normal Windows developer prerequisites "
           "for the selected backend, including a C++ compiler/linker toolchain when "
           "linking native executables.\r\n"
           "\r\n"
           "After setup, open a new terminal before relying on PATH changes. The default "
           "per-user install root is %LOCALAPPDATA%\\Viper, and machine-scope installs use "
           "%ProgramFiles%\\Viper unless the installer was built with a custom directory.\r\n"
           "\r\n"
           "Start Menu shortcuts include a Viper developer prompt, ViperIDE, and the VS Code "
           "extension installer when a .vsix was packaged. To remove the toolchain, use "
           "Settings > Apps or run uninstall.exe from the install root.\r\n";
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
    const auto &files = layout.installedFiles.empty() ? layout.installFiles : layout.installedFiles;
    for (const auto &file : files) {
        if (file.root == WindowsInstallRoot::InstallDir)
            checkedAddU64(total, file.sizeBytes, "Windows installed size");
    }
    const uint64_t kb = roundedKiB(total, "Windows installed size");
    return kb > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(kb);
}

/// @brief Pick the embedded UAC manifest: asInvoker for per-user installs,
///        admin-elevation otherwise.
std::string windowsManifestForLayout(const WindowsPackageLayout &layout,
                                     const std::string &minOsWindows) {
    return layout.perUserInstall ? generateAsInvokerManifest(minOsWindows)
                                 : generateUacManifest(minOsWindows);
}

/// @brief Parse a Windows VERSIONINFO numeric core into a {a,b,c,d} array.
/// @details Package versions may include a semver-style suffix such as
///          `1.2.3-beta`; the PE VERSIONINFO numeric fields can only store the
///          leading dotted numeric core. This parser accepts one to four numeric
///          components followed either by end-of-string or by `-`/`+` suffix
///          metadata. Other malformed versions are rejected instead of being
///          silently truncated.
std::array<uint16_t, 4> windowsVersionPartsForResource(const std::string &version) {
    std::array<uint16_t, 4> parts{0, 0, 0, 0};
    if (version.empty())
        throw std::runtime_error("Windows VERSIONINFO version must not be empty");
    std::vector<uint32_t> parsed;
    size_t pos = 0;
    while (pos < version.size()) {
        if (parsed.size() == parts.size()) {
            if (version[pos] == '-' || version[pos] == '+')
                break;
            throw std::runtime_error("Windows VERSIONINFO version must have at most 4 numeric "
                                     "components: " +
                                     version);
        }
        if (!std::isdigit(static_cast<unsigned char>(version[pos]))) {
            if (!parsed.empty() && (version[pos] == '-' || version[pos] == '+'))
                break;
            throw std::runtime_error("Windows VERSIONINFO version must start with a dotted "
                                     "numeric core: " +
                                     version);
        }
        uint32_t value = 0;
        while (pos < version.size() && std::isdigit(static_cast<unsigned char>(version[pos]))) {
            const uint32_t digit = static_cast<uint32_t>(version[pos] - '0');
            if (value > (65535u - digit) / 10u) {
                throw std::runtime_error("Windows VERSIONINFO version component exceeds 65535: " +
                                         version);
            }
            value = value * 10u + digit;
            ++pos;
        }
        parsed.push_back(value);
        if (pos >= version.size())
            break;
        if (version[pos] == '.') {
            ++pos;
            if (pos >= version.size() || !std::isdigit(static_cast<unsigned char>(version[pos]))) {
                throw std::runtime_error("Windows VERSIONINFO version has an empty numeric "
                                         "component: " +
                                         version);
            }
            continue;
        }
        if (version[pos] == '-' || version[pos] == '+')
            break;
        throw std::runtime_error("Windows VERSIONINFO version has invalid suffix: " + version);
    }
    if (parsed.empty()) {
        throw std::runtime_error("Windows VERSIONINFO version must start with a numeric core: " +
                                 version);
    }
    if (parsed.size() > parts.size()) {
        throw std::runtime_error("Windows VERSIONINFO version must have at most 4 numeric "
                                 "components: " +
                                 version);
    }
    for (size_t i = 0; i < parsed.size(); ++i)
        parts[i] = static_cast<uint16_t>(parsed[i]);
    return parts;
}

/// @brief Build a PEVersionInfo (RT_VERSION resource) from a package layout.
/// @details Populates file/product versions from the layout version and the
///          publisher/display-name strings; the description is the display name
///          optionally suffixed (e.g. " Uninstaller").
/// @param layout Package layout providing version, publisher, and display name.
/// @param filename Internal/original filename to record.
/// @param descriptionSuffix Optional suffix appended to the file description.
/// @return A populated, enabled PEVersionInfo.
PEVersionInfo windowsVersionInfoForLayout(const WindowsPackageLayout &layout,
                                          const std::string &filename,
                                          const std::string &descriptionSuffix) {
    const std::string version = layout.version.empty() ? "0.0.0" : layout.version;
    PEVersionInfo info;
    info.enabled = true;
    info.fileVersion = windowsVersionPartsForResource(version);
    info.productVersion = info.fileVersion;
    info.companyName = layout.publisher;
    info.fileDescription = descriptionSuffix.empty() ? layout.displayName
                                                     : layout.displayName + " " + descriptionSuffix;
    info.fileVersionText = version;
    info.internalName = filename;
    info.originalFilename = filename;
    info.productName = layout.displayName;
    info.productVersionText = version;
    return info;
}

std::string textFromBytes(const std::vector<uint8_t> &data) {
    if (data.empty())
        return {};
    return std::string(reinterpret_cast<const char *>(data.data()), data.size());
}

std::string appLicenseTextFor(const std::string &projectRoot, const PackageConfig &pkg) {
    const fs::path root(projectRoot);
    for (const char *name : {"LICENSE", "LICENSE.txt"}) {
        const fs::path candidate = root / name;
        if (fs::is_regular_file(candidate))
            return textFromBytes(readFile(candidate.string()));
    }
    if (!pkg.license.empty())
        return pkg.license;
    return "GPL-3.0-only";
}

/// @brief Build the Windows ProgID for a toolchain file association.
/// Equivalent to windowsProgIdFor but takes an explicit identifier string instead
/// of a full PackageConfig; used for toolchain installer builds.
std::string toolchainProgIdFor(const std::string &identifier, const FileAssoc &assoc) {
    validateWindowsProgIdBase(identifier, "Windows file association ProgID base");
    if (identifier.empty())
        throw std::runtime_error("Windows file association ProgID base must not be empty");
    validateFileAssociation(assoc.extension, assoc.description, assoc.mimeType);
    const std::string progId = identifier + "." + windowsProgIdExtensionComponent(assoc);
    validateWindowsProgIdBase(progId, "Windows file association ProgID");
    return progId;
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
                     << sanitizePackageRelativePath(overlayName, "Windows package manifest path")
                     << "\n";
}

/// @brief Record a file for removal at uninstall time, when @p deleteOnUninstall.
/// @details Appends an uninstall entry to @p layout so the generated uninstaller
///          knows to delete this path; no-op when the file should be left behind.
void registerInstalledFile(WindowsPackageLayout &layout,
                           WindowsInstallRoot root,
                           const std::string &installRelativePath,
                           uint64_t sizeBytes,
                           bool deleteOnUninstall) {
    if (deleteOnUninstall) {
        layout.uninstallFiles.push_back(
            WindowsPackageFileEntry{root, installRelativePath, 0, sizeBytes, 0});
    }
}

/// @brief Add a STORED (uncompressed) file to the overlay ZIP and register it.
/// @details Stored entries are mandatory for bootstrap files the stub reads
///          directly: it captures the entry's local-data offset and CRC-32 into
///          @p layout so the stub can locate and verify the bytes without parsing
///          the central directory. Throws if the writer compressed the entry.
///          Optionally appends a SHA-256 line to @p payloadManifest.
void addStoredOverlayFile(ZipWriter &zip,
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
    registerInstalledFile(
        layout, root, installRelativePath, entry.uncompressedSize, deleteOnUninstall);
}

/// @brief Add a file to the compressed inner payload ZIP and register it.
/// @details Unlike addStoredOverlayFile, entries here may be DEFLATE-compressed
///          because the stub extracts the whole inner ZIP before use, so no
///          stored-offset capture is needed. Records the install/uninstall entry
///          and, for InstallDir files, appends to @p installManifestPaths.
void addCompressedPayloadFile(ZipWriter &payloadZip,
                              const std::string &payloadName,
                              const uint8_t *data,
                              size_t len,
                              uint32_t unixMode,
                              WindowsPackageLayout &layout,
                              WindowsInstallRoot root,
                              const std::string &installRelativePath,
                              bool deleteOnUninstall,
                              std::vector<std::string> *installManifestPaths = nullptr) {
    payloadZip.addFile(payloadName, data, len, unixMode);
    layout.installedFiles.push_back(WindowsPackageFileEntry{root, installRelativePath, 0, len, 0});
    registerInstalledFile(layout, root, installRelativePath, len, deleteOnUninstall);
    if (installManifestPaths != nullptr && root == WindowsInstallRoot::InstallDir) {
        installManifestPaths->push_back(
            sanitizePackageRelativePath(installRelativePath, "Windows installed manifest path"));
    }
}

/// @brief Render the newline-separated, sorted, de-duplicated installed-file manifest.
/// @details Adds the manifest's own path, sorts case-insensitively, removes
///          case-insensitive duplicates, and joins with newlines. The installer
///          writes this manifest so a later upgrade/uninstall knows exactly which
///          files it placed.
/// @param paths Install-relative paths to include (taken by value; mutated).
/// @param manifestRelativePath Path of the manifest file itself (also recorded).
/// @return The manifest text.
std::string buildWindowsInstalledManifest(std::vector<std::string> paths,
                                          const std::string &manifestRelativePath) {
    paths.push_back(
        sanitizePackageRelativePath(manifestRelativePath, "Windows installed manifest path"));
    std::sort(paths.begin(), paths.end(), [](const std::string &a, const std::string &b) {
        return lowerAscii(a) < lowerAscii(b);
    });
    paths.erase(std::unique(paths.begin(),
                            paths.end(),
                            [](const std::string &a, const std::string &b) {
                                return lowerAscii(a) == lowerAscii(b);
                            }),
                paths.end());
    std::ostringstream os;
    for (const auto &path : paths)
        os << path << "\n";
    return os.str();
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

std::vector<std::string> importedDllNamesFromPe(const std::vector<uint8_t> &data) {
    return importedDllNamesFromPeImpl(data);
}

/// @brief Build a Windows self-extracting installer for a single application binary.
/// Assembly is a two-pass process: Pass 1 measures the exact overlay offset with
/// overlayFileOffset=0; Pass 2 bakes the measured offset into the stub and produces
/// the final PE. The uninstaller is built first so it can be included in the ZIP.
void buildWindowsPackage(const WindowsBuildParams &params) {
    const auto &pkg = params.pkgConfig;
    const std::string displayName = pkg.displayName.empty() ? params.projectName : pkg.displayName;
    const std::string exec = normalizeExecName(params.projectName);
    const std::string installDir =
        pkg.windowsInstallDir.empty() ? displayName : pkg.windowsInstallDir;
    const std::string version = params.version.empty() ? "0.0.0" : params.version;
    validateWindowsFileName(displayName, "Windows display name");
    validateWindowsFileName(installDir, "Windows install directory");
    validateWindowsProgIdBase(pkg.identifier, "Windows package identifier");
    validateSingleLineField(version, "Windows package version");
    if (!pkg.windowsInstallScope.empty() && pkg.windowsInstallScope != "user" &&
        pkg.windowsInstallScope != "machine") {
        throw std::runtime_error("Windows install scope must be 'user' or 'machine'");
    }
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
    layout.version = version;
    layout.identifier = pkg.identifier;
    layout.publisher = pkg.author;
    layout.description = pkg.description;
    layout.contact = pkg.maintainerEmail.empty() ? pkg.author : pkg.maintainerEmail;
    layout.licenseText = appLicenseTextFor(params.projectRoot, pkg);
    layout.executableName = exec + ".exe";
    layout.perUserInstall = pkg.windowsInstallScope == "user";
    layout.homepage = pkg.homepage;
    layout.installDate = currentInstallDate();
    layout.createDesktopShortcut = pkg.shortcutDesktop;
    layout.createStartMenuShortcut = pkg.shortcutMenu;
    layout.cleanInstallRootBeforeInstall = false;
    for (const auto &assoc : pkg.fileAssociations) {
        layout.fileAssociations.push_back({assoc.extension,
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
    ZipWriter payloadZip;
    payloadZip.setCompressionEnabled(true);
    std::vector<std::string> installedManifestPaths;
    layout.compressedPayloadRelativePath = ".viper-payload.zip";
    layout.compressedPayloadManifestRelativePath = ".viper-install-manifest.next";
    layout.installedManifestRelativePath = ".viper-install-manifest.txt";
    std::ostringstream payloadManifest;

    std::vector<uint8_t> icoData;
    if (!pkg.iconPath.empty()) {
        fs::path iconSrc =
            resolvePackageSourcePath(params.projectRoot, pkg.iconPath, "package icon");
        if (!fs::is_regular_file(iconSrc))
            throw std::runtime_error("package icon not found: " + pkg.iconPath);
        auto srcImage = pngRead(iconSrc.string());
        icoData = generateIco(srcImage);
    }

    const auto execData = readFile(params.executablePath);
    addCompressedPayloadFile(payloadZip,
                             exec + ".exe",
                             execData.data(),
                             execData.size(),
                             0100755,
                             layout,
                             WindowsInstallRoot::InstallDir,
                             exec + ".exe",
                             true,
                             &installedManifestPaths);

    for (const auto &dllPath : discoverAdjacentDllDependencies(params.executablePath)) {
        const std::string dllName = dllPath.filename().generic_string();
        validateWindowsRelativePath(dllName, "Windows DLL dependency path");
        const auto dllData = readFile(dllPath.string());
        addCompressedPayloadFile(payloadZip,
                                 dllName,
                                 dllData.data(),
                                 dllData.size(),
                                 0100644,
                                 layout,
                                 WindowsInstallRoot::InstallDir,
                                 dllName,
                                 true,
                                 &installedManifestPaths);
    }

    if (!icoData.empty()) {
        addCompressedPayloadFile(payloadZip,
                                 exec + ".ico",
                                 icoData.data(),
                                 icoData.size(),
                                 0100644,
                                 layout,
                                 WindowsInstallRoot::InstallDir,
                                 exec + ".ico",
                                 true,
                                 &installedManifestPaths);
    }
    layout.displayIconRelativePath = !icoData.empty() ? exec + ".ico" : exec + ".exe";

    for (const auto &asset : pkg.assets) {
        const std::string sourceRel =
            sanitizePackageRelativePath(asset.sourcePath, "asset source path");
        const std::string sourceLeaf = fs::path(sourceRel).filename().generic_string();
        fs::path srcPath =
            resolvePackageSourcePath(params.projectRoot, asset.sourcePath, "asset source path");
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
                    addCompressedPayloadFile(payloadZip,
                                             relInstall,
                                             data.data(),
                                             data.size(),
                                             0100644,
                                             layout,
                                             WindowsInstallRoot::InstallDir,
                                             relInstall,
                                             true,
                                             &installedManifestPaths);
                });
        } else if (fs::is_regular_file(srcPath)) {
            const std::string relInstall =
                joinPackageRelativePath(targetDir, sourceLeaf, "asset path");
            addParentDirs(layout.installDirectories,
                          installDirSet,
                          WindowsInstallRoot::InstallDir,
                          relInstall);
            const auto data = readFile(srcPath.string());
            addCompressedPayloadFile(payloadZip,
                                     relInstall,
                                     data.data(),
                                     data.size(),
                                     0100644,
                                     layout,
                                     WindowsInstallRoot::InstallDir,
                                     relInstall,
                                     true,
                                     &installedManifestPaths);
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
        addStoredOverlayFile(zip,
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
        addStoredOverlayFile(zip,
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

    registerInstalledFile(
        layout, WindowsInstallRoot::InstallDir, layout.installedManifestRelativePath, 0, true);
    finalizeUninstallDirs(layout);

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
    addCompressedPayloadFile(payloadZip,
                             "uninstall.exe",
                             uninstBytes.data(),
                             uninstBytes.size(),
                             0100755,
                             layout,
                             WindowsInstallRoot::InstallDir,
                             "uninstall.exe",
                             false,
                             &installedManifestPaths);

    const std::string installedManifestText =
        buildWindowsInstalledManifest(installedManifestPaths, layout.installedManifestRelativePath);
    addCompressedPayloadFile(payloadZip,
                             layout.installedManifestRelativePath,
                             reinterpret_cast<const uint8_t *>(installedManifestText.data()),
                             installedManifestText.size(),
                             0100644,
                             layout,
                             WindowsInstallRoot::InstallDir,
                             layout.installedManifestRelativePath,
                             false,
                             nullptr);
    const auto compressedPayload = payloadZip.finishToVector();
    addStoredOverlayFile(zip,
                         "meta/payload.zip",
                         compressedPayload.data(),
                         compressedPayload.size(),
                         0100644,
                         layout,
                         WindowsInstallRoot::InstallDir,
                         layout.compressedPayloadRelativePath,
                         true,
                         &payloadManifest);
    addStoredOverlayFile(zip,
                         "meta/install_manifest.next",
                         reinterpret_cast<const uint8_t *>(installedManifestText.data()),
                         installedManifestText.size(),
                         0100644,
                         layout,
                         WindowsInstallRoot::InstallDir,
                         layout.compressedPayloadManifestRelativePath,
                         true,
                         &payloadManifest);
    layout.estimatedSizeKb = estimatedInstalledSizeKb(layout);
    validateWindowsLayoutFitsStub(layout);
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

    std::vector<uint8_t> peBytes;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
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

        peBytes = buildPE(pe);
        const uint64_t finalOverlayOffset =
            static_cast<uint64_t>(peBytes.size() - zipPayload.size());
        if (finalOverlayOffset == layout.overlayFileOffset)
            break;
        if (attempt == 3) {
            throw std::runtime_error("Windows installer overlay offset did not converge");
        }
        layout.overlayFileOffset = finalOverlayOffset;
    }
    writePEToFile(peBytes, params.outputPath);
}

/// @brief Build a Windows toolchain installer from a pre-validated staged manifest.
/// Staged files are installed as-is, while generated developer helper scripts and
/// Start Menu shortcuts are added by the packager. Symlinks are dereferenced.
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
    validateWindowsFileName(params.installDirName, "Windows install directory");
    validatePackageUrl(params.homepage, "Windows package homepage");
    if (params.installScope != "user" && params.installScope != "machine") {
        throw std::runtime_error("Windows toolchain install scope must be 'user' or 'machine'");
    }
    if (params.manifest.version.empty())
        throw std::runtime_error("toolchain package version is required");
    validateSingleLineField(params.manifest.version, "Windows package version");
    WindowsPackageLayout layout;
    layout.displayName = params.displayName;
    layout.installDirName = params.installDirName;
    layout.version = params.manifest.version;
    layout.identifier = params.identifier;
    layout.publisher = params.publisher;
    layout.description = "Viper compiler toolchain";
    layout.contact = params.manifest.maintainerEmail.empty() ? params.publisher
                                                             : params.manifest.maintainerEmail;
    layout.licenseText = params.manifest.license;
    layout.executableName = "viper.exe";
    layout.homepage = params.homepage;
    layout.displayIconRelativePath = "share\\viper\\viper.ico";
    layout.installDate = currentInstallDate();
    layout.createDesktopShortcut = false;
    layout.createStartMenuShortcut = params.createStartMenuShortcuts;
    layout.addToPath = params.addToPath;
    layout.cleanInstallRootBeforeInstall = false;
    layout.pathRelativePath = "bin";
    layout.fileAssociationExecutableRelativePath = "bin\\viper.exe";
    layout.perUserInstall = params.installScope == "user";
    if (params.registerFileAssociations) {
        if (params.identifier.empty())
            throw std::runtime_error(
                "Windows file associations require a non-empty package identifier");
        for (const auto &assoc : params.manifest.fileAssociations) {
            layout.fileAssociations.push_back({assoc.extension,
                                               assoc.description,
                                               assoc.mimeType,
                                               toolchainProgIdFor(params.identifier, assoc),
                                               toolchainOpenCommandArgsFor(assoc)});
        }
    }

    std::set<std::string> installDirSet;
    ZipWriter zip;
    zip.setCompressionEnabled(false);
    zip.addDirectory("app/");
    zip.addDirectory("meta/");
    ZipWriter payloadZip;
    payloadZip.setCompressionEnabled(true);
    std::vector<std::string> installedManifestPaths;
    layout.compressedPayloadRelativePath = ".viper-payload.zip";
    layout.compressedPayloadManifestRelativePath = ".viper-install-manifest.next";
    layout.installedManifestRelativePath = ".viper-install-manifest.txt";
    std::ostringstream payloadManifest;
    std::string stagedLicenseText;
    bool hasPackagedVSIX = false;
    const std::vector<uint8_t> toolchainIcon = generateIco(defaultViperToolchainIconImage());

    for (const auto &file : params.manifest.files) {
        const std::string relInstall =
            sanitizePackageRelativePath(file.stagedRelativePath, "windows toolchain path");
        validateWindowsRelativePath(relInstall, "windows toolchain path");
        if (file.symlink && !fs::is_regular_file(file.stagedAbsolutePath)) {
            throw std::runtime_error("Windows toolchain installers can only dereference "
                                     "symlinks to regular files: " +
                                     file.stagedRelativePath);
        }
        addParentDirs(
            layout.installDirectories, installDirSet, WindowsInstallRoot::InstallDir, relInstall);
        const auto data = readFile(file.stagedAbsolutePath.string());
        addCompressedPayloadFile(payloadZip,
                                 relInstall,
                                 data.data(),
                                 data.size(),
                                 file.executable ? 0100755 : 0100644,
                                 layout,
                                 WindowsInstallRoot::InstallDir,
                                 relInstall,
                                 true,
                                 &installedManifestPaths);

        const std::string lowerName = lowerAscii(fs::path(relInstall).filename().generic_string());
        const std::string lowerRel = lowerAscii(relInstall);
        if (lowerRel.rfind("share/viper/vscode/", 0) == 0 && lowerName.size() >= 5 &&
            lowerName.substr(lowerName.size() - 5) == ".vsix") {
            hasPackagedVSIX = true;
        }
        if (lowerName == "license" || lowerName == "readme.md") {
            const std::string overlayName =
                lowerName == "license" ? "meta/license.txt" : "meta/readme.txt";
            zip.addFile(overlayName, data.data(), data.size(), 0100644);
            appendPayloadManifestEntry(&payloadManifest, overlayName, data.data(), data.size());
            if (lowerName == "license")
                stagedLicenseText = textFromBytes(data);
        }
    }
    if (!stagedLicenseText.empty())
        layout.licenseText = stagedLicenseText;

    addUniqueDir(layout.installDirectories, installDirSet, WindowsInstallRoot::InstallDir, "bin");
    {
        const std::string script = toolchainDeveloperPromptScript();
        addCompressedPayloadFile(payloadZip,
                                 "bin/viper-dev.cmd",
                                 reinterpret_cast<const uint8_t *>(script.data()),
                                 script.size(),
                                 0100644,
                                 layout,
                                 WindowsInstallRoot::InstallDir,
                                 "bin/viper-dev.cmd",
                                 true,
                                 &installedManifestPaths);
    }
    {
        const std::string script = toolchainVSCodeInstallScript();
        addCompressedPayloadFile(payloadZip,
                                 "bin/viper-install-vscode-extension.cmd",
                                 reinterpret_cast<const uint8_t *>(script.data()),
                                 script.size(),
                                 0100644,
                                 layout,
                                 WindowsInstallRoot::InstallDir,
                                 "bin/viper-install-vscode-extension.cmd",
                                 true,
                                 &installedManifestPaths);
    }
    {
        addUniqueDir(layout.installDirectories,
                     installDirSet,
                     WindowsInstallRoot::InstallDir,
                     "share/viper");
        addCompressedPayloadFile(payloadZip,
                                 "share/viper/viper.ico",
                                 toolchainIcon.data(),
                                 toolchainIcon.size(),
                                 0100644,
                                 layout,
                                 WindowsInstallRoot::InstallDir,
                                 "share/viper/viper.ico",
                                 true,
                                 &installedManifestPaths);
        const std::string readme = toolchainWindowsPrerequisitesReadme();
        addCompressedPayloadFile(payloadZip,
                                 "share/viper/README.windows-prerequisites.txt",
                                 reinterpret_cast<const uint8_t *>(readme.data()),
                                 readme.size(),
                                 0100644,
                                 layout,
                                 WindowsInstallRoot::InstallDir,
                                 "share/viper/README.windows-prerequisites.txt",
                                 true,
                                 &installedManifestPaths);
    }

    if (params.createStartMenuShortcuts) {
        LnkParams promptLnk;
        promptLnk.targetPath = "%SystemRoot%\\System32\\cmd.exe";
        promptLnk.workingDir = "%USERPROFILE%";
        promptLnk.arguments =
            "/k " + windowsQuotedPath(windowsInstallEnvPath(
                        params.installDirName, layout.perUserInstall, "bin\\viper-dev.cmd"));
        promptLnk.description = params.displayName + " Developer Prompt";
        promptLnk.iconPath = windowsInstallEnvPath(
            params.installDirName, layout.perUserInstall, layout.displayIconRelativePath);
        const auto promptData = generateLnk(promptLnk);
        addStoredOverlayFile(zip,
                             "meta/viper_developer_prompt.lnk",
                             promptData.data(),
                             promptData.size(),
                             0100644,
                             layout,
                             WindowsInstallRoot::StartMenuDir,
                             "Viper Developer Prompt.lnk",
                             true,
                             &payloadManifest);

        if (hasPackagedVSIX) {
            LnkParams vscodeLnk;
            vscodeLnk.targetPath = "%SystemRoot%\\System32\\cmd.exe";
            vscodeLnk.workingDir = "%USERPROFILE%";
            vscodeLnk.arguments =
                "/c " +
                windowsQuotedPath(windowsInstallEnvPath(params.installDirName,
                                                        layout.perUserInstall,
                                                        "bin\\viper-install-vscode-extension.cmd"));
            vscodeLnk.description = "Install " + params.displayName + " VS Code extension";
            vscodeLnk.iconPath = windowsInstallEnvPath(
                params.installDirName, layout.perUserInstall, layout.displayIconRelativePath);
            const auto vscodeData = generateLnk(vscodeLnk);
            addStoredOverlayFile(zip,
                                 "meta/viper_vscode_extension.lnk",
                                 vscodeData.data(),
                                 vscodeData.size(),
                                 0100644,
                                 layout,
                                 WindowsInstallRoot::StartMenuDir,
                                 "Install VS Code Extension.lnk",
                                 true,
                                 &payloadManifest);
        }

        LnkParams ideLnk;
        ideLnk.targetPath = windowsInstallEnvPath(
            params.installDirName, layout.perUserInstall, "bin\\viperide.exe");
        ideLnk.workingDir = "%USERPROFILE%";
        ideLnk.description = "ViperIDE";
        ideLnk.iconPath = windowsInstallEnvPath(
            params.installDirName, layout.perUserInstall, layout.displayIconRelativePath);
        const auto ideData = generateLnk(ideLnk);
        addStoredOverlayFile(zip,
                             "meta/viperide.lnk",
                             ideData.data(),
                             ideData.size(),
                             0100644,
                             layout,
                             WindowsInstallRoot::StartMenuDir,
                             "ViperIDE.lnk",
                             true,
                             &payloadManifest);
    }

    registerInstalledFile(
        layout, WindowsInstallRoot::InstallDir, layout.installedManifestRelativePath, 0, true);
    finalizeUninstallDirs(layout);

    auto uninstStub = buildUninstallerStub(layout, params.archStr);
    PEBuildParams uninstPe;
    uninstPe.arch = uninstStub.peArch;
    uninstPe.textSection = uninstStub.textSection;
    uninstPe.rdataSection = uninstStub.stubData;
    uninstPe.imports = uninstStub.imports;
    uninstPe.manifest = windowsManifestForLayout(layout, {});
    uninstPe.iconData = toolchainIcon;
    uninstPe.versionInfo = windowsVersionInfoForLayout(layout, "uninstall.exe", "Uninstaller");
    configureInstallerStack(uninstPe);
    const auto uninstBytes = buildPE(uninstPe);
    addCompressedPayloadFile(payloadZip,
                             "uninstall.exe",
                             uninstBytes.data(),
                             uninstBytes.size(),
                             0100755,
                             layout,
                             WindowsInstallRoot::InstallDir,
                             "uninstall.exe",
                             false,
                             &installedManifestPaths);
    const std::string installedManifestText =
        buildWindowsInstalledManifest(installedManifestPaths, layout.installedManifestRelativePath);
    addCompressedPayloadFile(payloadZip,
                             layout.installedManifestRelativePath,
                             reinterpret_cast<const uint8_t *>(installedManifestText.data()),
                             installedManifestText.size(),
                             0100644,
                             layout,
                             WindowsInstallRoot::InstallDir,
                             layout.installedManifestRelativePath,
                             false,
                             nullptr);
    const auto compressedPayload = payloadZip.finishToVector();
    addStoredOverlayFile(zip,
                         "meta/payload.zip",
                         compressedPayload.data(),
                         compressedPayload.size(),
                         0100644,
                         layout,
                         WindowsInstallRoot::InstallDir,
                         layout.compressedPayloadRelativePath,
                         true,
                         &payloadManifest);
    addStoredOverlayFile(zip,
                         "meta/install_manifest.next",
                         reinterpret_cast<const uint8_t *>(installedManifestText.data()),
                         installedManifestText.size(),
                         0100644,
                         layout,
                         WindowsInstallRoot::InstallDir,
                         layout.compressedPayloadManifestRelativePath,
                         true,
                         &payloadManifest);
    layout.estimatedSizeKb = estimatedInstalledSizeKb(layout);
    validateWindowsLayoutFitsStub(layout);
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
    provisionalPe.manifest = windowsManifestForLayout(layout, {});
    provisionalPe.iconData = toolchainIcon;
    provisionalPe.overlay = zipPayload;
    provisionalPe.versionInfo = windowsVersionInfoForLayout(
        layout, fs::path(params.outputPath).filename().generic_string(), "Setup");
    configureInstallerStack(provisionalPe);
    const auto provisionalBytes = buildPE(provisionalPe);
    layout.overlayFileOffset = static_cast<uint64_t>(provisionalBytes.size() - zipPayload.size());

    std::vector<uint8_t> peBytes;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        auto instStub = buildInstallerStub(layout, params.archStr);
        PEBuildParams pe;
        pe.arch = instStub.peArch;
        pe.textSection = instStub.textSection;
        pe.rdataSection = instStub.stubData;
        pe.imports = instStub.imports;
        pe.manifest = windowsManifestForLayout(layout, {});
        pe.iconData = toolchainIcon;
        pe.overlay = zipPayload;
        pe.versionInfo = windowsVersionInfoForLayout(
            layout, fs::path(params.outputPath).filename().generic_string(), "Setup");
        configureInstallerStack(pe);
        peBytes = buildPE(pe);
        const uint64_t finalOverlayOffset =
            static_cast<uint64_t>(peBytes.size() - zipPayload.size());
        if (finalOverlayOffset == layout.overlayFileOffset)
            break;
        if (attempt == 3) {
            throw std::runtime_error("Windows installer overlay offset did not converge");
        }
        layout.overlayFileOffset = finalOverlayOffset;
    }
    writePEToFile(peBytes, params.outputPath);
}

} // namespace viper::pkg
