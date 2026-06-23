//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/viper/cmd_package.cpp
// Purpose: Handle `viper package` subcommand — compiles a project to a native
//          binary and packages it into a platform-specific installer.
//
// Key invariants:
//   - Resolves project, compiles to native, then packages.
//   - Default target is the host platform.
//   - Non-host package formats can package a caller-supplied executable even
//     when the current host cannot build the target-native payload itself.
//
// Ownership/Lifetime:
//   - Temporary files (IL, native binary) are cleaned up after packaging.
//
// Links: cmd_run.cpp (compile pattern), MacOSPackageBuilder.hpp,
//        native_compiler.hpp, project_loader.hpp
//
//===----------------------------------------------------------------------===//

#include "cli.hpp"
#include "common/PlatformCapabilities.hpp"
#include "common/RunProcess.hpp"
#include "tools/common/native_compiler.hpp"
#include "tools/common/packaging/LinuxPackageBuilder.hpp"
#include "tools/common/packaging/LinuxRuntimeStubGen.hpp"
#include "tools/common/packaging/MacOSPackageBuilder.hpp"
#include "tools/common/packaging/PkgUtils.hpp"
#include "tools/common/packaging/PkgVerify.hpp"
#include "tools/common/packaging/WindowsPackageBuilder.hpp"
#include "tools/common/project_loader.hpp"

#include "support/diag_expected.hpp"
#include "support/source_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace il::tools::common;
using namespace il::support;
namespace fs = std::filesystem;

enum class PackageTarget { MacOS, Linux, Windows, Tarball, AppImage, Rpm, Dmg, Auto };
enum class ExecutableFormat { Unknown, MachO, ELF, PE };

/// @brief Identifying details of a native executable detected from its header.
struct ExecutableInfo {
    ExecutableFormat format{ExecutableFormat::Unknown}; ///< Detected container format.
    std::string arch;                                   ///< Architecture string ("x64"/"arm64").
};

/// @brief Print usage information for `viper package`.
void packageUsage(std::ostream &out = std::cerr) {
    out << "Usage: viper package [project] [options]\n"
        << "\n"
        << "  Build a Viper project and package it for a target platform.\n"
        << "\n"
        << "  [project]  Path to a directory or viper.project file (default: .)\n"
        << "\n"
        << "Options:\n"
        << "  --target <platform>       macos, linux, windows, appimage, rpm, dmg, or tarball "
           "(default: host)\n"
        << "  --target=<platform>       Inline form of --target\n"
        << "  --arch <arch>             Target architecture: x64 or arm64 (default: host)\n"
        << "  --arch=<arch>             Inline form of --arch\n"
        << "  --executable <path>       Package a prebuilt native executable instead of compiling\n"
        << "  --executable=<path>       Inline form of --executable\n"
        << "  -o, --output <path>       Output file path\n"
        << "  --output=<path>           Inline form of --output\n"
        << "  --dry-run                 List package contents without building or signing\n"
        << "  --keep-failed-artifact    Preserve generated artifacts after a failed package step\n"
        << "  --verbose, -v             Show detailed packaging output\n"
        << "  --help, -h                Show this help\n"
        << "\n"
        << "Target-specific install/signing options are documented in docs/tools.md.\n"
        << "\n"
        << "Examples:\n"
        << "  viper package                       Package current dir for host platform\n"
        << "  viper package myapp/ --target linux  Build .deb for Linux\n"
        << "  viper package . --target windows -o myapp-setup.exe\n"
        << "\n"
        << "Output formats:\n"
        << "  macOS:    .app bundle in .zip (Finder-native, drag to /Applications)\n"
        << "  DMG:      .app bundle in a .dmg with an /Applications drag-target (macOS host)\n"
        << "  Linux:    .deb package (dpkg -i), includes .desktop + MIME types\n"
        << "  AppImage: self-extracting Linux .AppImage (portable, no install required)\n"
        << "  RPM:      .rpm package (dnf/yum) — requires rpmbuild on the build host\n"
        << "  Windows:  PE32+ .exe with embedded ZIP (assets, shortcuts, uninstaller)\n"
        << "  Tarball:  .tar.gz portable archive\n"
        << "\n"
        << "macOS code-signing/notarization run only on a macOS host; Windows Authenticode\n"
        << "signing runs only where signtool is available. .dmg builds on a macOS host and\n"
        << ".rpm on an rpmbuild-capable host.\n"
        << "Manifest directives and signing options are documented in docs/tools.md.\n";
}

/// @brief Parsed command-line arguments for the `viper package` subcommand.
/// @details Covers the target/output selection plus the macOS signing and Windows
///          installer/signing option families; *Set companion flags record whether
///          an option was explicitly provided so manifest defaults can apply.
struct PackageArgs {
    std::string target;
    PackageTarget platformTarget{PackageTarget::Auto};
    std::string outputPath;
    std::string archOverride; // "x64" or "arm64"
    std::string executablePath;
    std::string macosSignMode;
    bool macosSignModeSet{false};
    std::string macosSignIdentity;
    bool macosSignIdentitySet{false};
    std::string macosEntitlements;
    bool macosEntitlementsSet{false};
    std::string macosNotaryProfile;
    bool macosNotaryProfileSet{false};
    bool macosHardenedRuntime{false};
    bool macosHardenedRuntimeSet{false};
    bool macosStaple{false};
    bool macosStapleSet{false};
    std::string windowsInstallScope;
    bool windowsInstallScopeSet{false};
    std::string windowsInstallDir;
    bool windowsInstallDirSet{false};
    bool windowsSign{false};
    bool windowsSignSet{false};
    std::string windowsSignPfx;
    bool windowsSignPfxSet{false};
    std::string windowsSignThumbprint;
    bool windowsSignThumbprintSet{false};
    std::string windowsTimestampUrl;
    bool windowsTimestampUrlSet{false};
    std::string windowsSigntoolPath;
    bool windowsSigntoolPathSet{false};
    bool windowsSignNoVerify{false};
    bool windowsSignNoVerifySet{false};
    std::string linuxSignKey;
    bool linuxSignKeySet{false};
    bool dryRun{false};
    bool keepFailedArtifact{false};
    bool verbose{false};
    bool help{false};
};

/// @brief Determine the packaging target matching the host build platform.
PackageTarget detectHostPlatform() {
#if VIPER_HOST_MACOS
    return PackageTarget::MacOS;
#elif VIPER_HOST_LINUX
    return PackageTarget::Linux;
#elif VIPER_HOST_WINDOWS
    return PackageTarget::Windows;
#else
    return PackageTarget::Tarball;
#endif
}

/// @brief Parse a package target name from the command line.
/// @param value User supplied target value.
/// @param out Receives the parsed target on success.
/// @return True when @p value is one of macos, linux, windows, or tarball.
bool parsePackageTargetValue(std::string_view value, PackageTarget &out) {
    if (value == "macos")
        out = PackageTarget::MacOS;
    else if (value == "linux")
        out = PackageTarget::Linux;
    else if (value == "windows")
        out = PackageTarget::Windows;
    else if (value == "tarball")
        out = PackageTarget::Tarball;
    else if (value == "appimage")
        out = PackageTarget::AppImage;
    else if (value == "rpm")
        out = PackageTarget::Rpm;
    else if (value == "dmg")
        out = PackageTarget::Dmg;
    else
        return false;
    return true;
}

/// @brief Return true when @p value is a supported package architecture name.
bool isPackageArchName(std::string_view value) {
    return value == "x64" || value == "arm64";
}

/// @brief Remove a generated artifact unless the user requested failure preservation.
/// @details Package failures are often easier to diagnose by inspecting the
///          partially generated executable or installer. This helper centralizes
///          the `--keep-failed-artifact` policy so success cleanup remains
///          unchanged while failure paths are consistent.
/// @param path Artifact path to remove.
/// @param keep True when `--keep-failed-artifact` was supplied.
/// @param ec Receives any best-effort filesystem removal error.
void removeFailedArtifactUnlessKept(const fs::path &path, bool keep, std::error_code &ec) {
    if (keep || path.empty())
        return;
    fs::remove(path, ec);
}

/// @brief Return the lowercase platform name for a target (e.g. "macos").
std::string platformName(PackageTarget t) {
    switch (t) {
        case PackageTarget::MacOS:
            return "macos";
        case PackageTarget::Linux:
            return "linux";
        case PackageTarget::Windows:
            return "windows";
        case PackageTarget::Tarball:
            return "tarball";
        case PackageTarget::AppImage:
            return "linux";
        case PackageTarget::Rpm:
            return "linux";
        case PackageTarget::Dmg:
            return "macos";
        default:
            return "unknown";
    }
}

/// @brief Return the output file extension for a target (e.g. ".deb").
std::string platformExtension(PackageTarget t) {
    switch (t) {
        case PackageTarget::MacOS:
            return ".zip";
        case PackageTarget::Linux:
            return ".deb";
        case PackageTarget::Windows:
            return ".exe";
        case PackageTarget::Tarball:
            return ".tar.gz";
        case PackageTarget::AppImage:
            return ".AppImage";
        case PackageTarget::Rpm:
            return ".rpm";
        case PackageTarget::Dmg:
            return ".dmg";
        default:
            return ".zip";
    }
}

/// @brief Sanitize a string for use as a filename component.
/// @details Keeps alphanumerics and `._-+~`, replaces other characters with `_`,
///          strips leading `.`/`-`, and returns @p fallback if the result is empty
///          or "."/"..".
std::string sanitizeOutputFileComponent(const std::string &text, const std::string &fallback) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '.' || c == '_' || c == '-' || c == '+' || c == '~')
            out.push_back(c);
        else
            out.push_back('_');
    }
    while (!out.empty() && (out.front() == '.' || out.front() == '-'))
        out.erase(out.begin());
    if (out.empty() || out == "." || out == "..")
        return fallback;
    return out;
}

/// @brief Build the default output filename `<name>-<version>-<platform>-<arch><ext>`.
/// @details Each component is sanitized; used when the user does not pass `-o`.
std::string defaultPackageOutputPath(const ProjectConfig &proj,
                                     const std::string &version,
                                     PackageTarget target,
                                     const std::string &archStr) {
    return sanitizeOutputFileComponent(proj.name, "package") + "-" +
           sanitizeOutputFileComponent(version, "0.0.0") + "-" + platformName(target) + "-" +
           sanitizeOutputFileComponent(archStr, "native") + platformExtension(target);
}

/// @brief Sanitize a version string into a portable filename component (keeps
///        alphanumerics and `.+~-`, replaces anything else with `_`).
std::string portableArchiveVersionComponent(const std::string &version) {
    std::string out;
    out.reserve(version.size());
    for (char c : version) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '.' || c == '+' || c == '~' || c == '-')
            out.push_back(c);
        else
            out.push_back('_');
    }
    return out;
}

/// @brief Read a little-endian uint16 at @p off (caller must bounds-check).
uint16_t readLE16(const std::vector<uint8_t> &data, size_t off) {
    return static_cast<uint16_t>(data[off] | (data[off + 1] << 8));
}

/// @brief Read a little-endian uint32 at @p off (caller must bounds-check).
uint32_t readLE32(const std::vector<uint8_t> &data, size_t off) {
    return static_cast<uint32_t>(data[off]) | (static_cast<uint32_t>(data[off + 1]) << 8) |
           (static_cast<uint32_t>(data[off + 2]) << 16) |
           (static_cast<uint32_t>(data[off + 3]) << 24);
}

/// @brief Read a big-endian uint32 at @p off (caller must bounds-check).
uint32_t readBE32(const std::vector<uint8_t> &data, size_t off) {
    return (static_cast<uint32_t>(data[off]) << 24) | (static_cast<uint32_t>(data[off + 1]) << 16) |
           (static_cast<uint32_t>(data[off + 2]) << 8) | static_cast<uint32_t>(data[off + 3]);
}

/// @brief Read up to 64 KiB of an executable's leading bytes for format detection.
/// @throws std::runtime_error on open/size/seek/read failure.
std::vector<uint8_t> readExecutableHeader(const std::string &path) {
    constexpr std::streamoff kMaxHeaderBytes = 64 * 1024;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("cannot read executable: " + path);
    const std::streamoff fileSize = f.tellg();
    if (fileSize < 0)
        throw std::runtime_error("cannot determine executable size: " + path);
    const std::streamoff readSize = std::min(fileSize, kMaxHeaderBytes);
    f.seekg(0);
    if (!f)
        throw std::runtime_error("cannot seek executable: " + path);
    std::vector<uint8_t> data(static_cast<size_t>(readSize));
    if (!data.empty()) {
        f.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!f || f.gcount() != static_cast<std::streamsize>(data.size()))
            throw std::runtime_error("incomplete read of executable header: " + path);
    }
    return data;
}

/// @brief Return a human-readable name for an executable format (e.g. "Mach-O").
std::string formatName(ExecutableFormat format) {
    switch (format) {
        case ExecutableFormat::Unknown:
            return "unknown";
        case ExecutableFormat::MachO:
            return "Mach-O";
        case ExecutableFormat::ELF:
            return "ELF";
        case ExecutableFormat::PE:
            return "PE";
    }
    return "unknown";
}

/// @brief Format a uint16 as a hexadecimal string (used in error messages).
std::string hexU16(uint16_t value) {
    std::ostringstream os;
    os << std::hex << value;
    return os.str();
}

/// @brief Return the native executable format expected for a package target.
/// @throws std::runtime_error for the tarball target (no executable format).
ExecutableFormat expectedExecutableFormat(PackageTarget target) {
    switch (target) {
        case PackageTarget::MacOS:
            return ExecutableFormat::MachO;
        case PackageTarget::Linux:
            return ExecutableFormat::ELF;
        case PackageTarget::Windows:
            return ExecutableFormat::PE;
        case PackageTarget::Tarball:
            throw std::runtime_error(
                "tarball packages do not require a platform executable format");
        case PackageTarget::AppImage:
            return ExecutableFormat::ELF;
        case PackageTarget::Rpm:
            return ExecutableFormat::ELF;
        case PackageTarget::Dmg:
            return ExecutableFormat::MachO;
        case PackageTarget::Auto:
        default:
            return ExecutableFormat::ELF;
    }
}

/// @brief Detect the format and architecture of a native executable.
/// @details Inspects the leading header bytes for Mach-O, ELF, or PE magic and
///          decodes the machine field into an "x64"/"arm64" arch string.
/// @throws std::runtime_error when the file is too small or unrecognized.
ExecutableInfo inspectExecutable(const std::string &path) {
    const auto data = readExecutableHeader(path);
    if (data.size() < 20)
        throw std::runtime_error("executable is too small to identify: " + path);

    if (data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
        if (data[4] != 2 || data[5] != 1)
            throw std::runtime_error("ELF executable must be 64-bit little-endian: " + path);
        const uint16_t machine = readLE16(data, 18);
        if (machine == 62)
            return {ExecutableFormat::ELF, "x64"};
        if (machine == 183)
            return {ExecutableFormat::ELF, "arm64"};
        throw std::runtime_error("ELF executable has unsupported machine type: " +
                                 std::to_string(machine));
    }

    if (data[0] == 'M' && data[1] == 'Z') {
        if (data.size() < 64)
            throw std::runtime_error("PE executable is truncated: " + path);
        const size_t peOff = readLE32(data, 60);
        if (peOff >= data.size() || peOff > data.size() - 24 || data[peOff] != 'P' ||
            data[peOff + 1] != 'E' || data[peOff + 2] != 0 || data[peOff + 3] != 0) {
            throw std::runtime_error("PE executable has an invalid header: " + path);
        }
        const uint16_t machine = readLE16(data, peOff + 4);
        if (machine == 0x8664)
            return {ExecutableFormat::PE, "x64"};
        if (machine == 0xAA64)
            return {ExecutableFormat::PE, "arm64"};
        throw std::runtime_error("PE executable has unsupported machine type: 0x" +
                                 hexU16(machine));
    }

    const uint32_t magicBE = readBE32(data, 0);
    const uint32_t magicLE = readLE32(data, 0);
    if (magicLE == 0xFEEDFACF || magicBE == 0xFEEDFACF) {
        const bool little = magicLE == 0xFEEDFACF;
        const uint32_t cputype = little ? readLE32(data, 4) : readBE32(data, 4);
        if (cputype == 0x01000007u)
            return {ExecutableFormat::MachO, "x64"};
        if (cputype == 0x0100000Cu)
            return {ExecutableFormat::MachO, "arm64"};
        throw std::runtime_error("Mach-O executable has unsupported CPU type: " +
                                 std::to_string(cputype));
    }
    if ((magicBE == 0xCAFEBABEu || magicBE == 0xCAFEBABFu) && data.size() >= 8) {
        const uint32_t count = readBE32(data, 4);
        const size_t archSize = magicBE == 0xCAFEBABFu ? 32u : 20u;
        if (count == 0 || count > 64 || data.size() < 8 + static_cast<size_t>(count) * archSize)
            throw std::runtime_error("Mach-O universal binary has an invalid fat header: " + path);
        bool hasX64 = false;
        bool hasArm64 = false;
        for (uint32_t i = 0; i < count; ++i) {
            const size_t off = 8 + static_cast<size_t>(i) * archSize;
            const uint32_t cputype = readBE32(data, off);
            if (cputype == 0x01000007u)
                hasX64 = true;
            else if (cputype == 0x0100000Cu)
                hasArm64 = true;
        }
        if (hasX64 && hasArm64)
            return {ExecutableFormat::MachO, "universal"};
        if (hasX64)
            return {ExecutableFormat::MachO, "x64"};
        if (hasArm64)
            return {ExecutableFormat::MachO, "arm64"};
        throw std::runtime_error("Mach-O universal binary does not contain x64 or arm64: " + path);
    }

    throw std::runtime_error("executable format is not Mach-O, ELF, or PE: " + path);
}

/// @brief Verify a prebuilt executable matches the requested target and arch.
/// @details Checks the container format against the target and the architecture
///          against @p archStr (universal binaries match any). Tarball packages
///          are portable archives, so prebuilt payloads are only required to be
///          readable by the tarball builder and are not inspected as native
///          Mach-O/ELF/PE executables here.
/// @throws std::runtime_error describing the mismatch.
void validateExecutableForPackageTarget(const std::string &path,
                                        PackageTarget target,
                                        const std::string &archStr) {
    if (target == PackageTarget::Tarball)
        return;

    const ExecutableInfo info = inspectExecutable(path);
    const ExecutableFormat expected = expectedExecutableFormat(target);
    if (info.format != expected) {
        throw std::runtime_error("executable format " + formatName(info.format) +
                                 " does not match package target " + platformName(target) +
                                 " (expected " + formatName(expected) + ")");
    }
    if (info.arch != "universal" && info.arch != archStr) {
        throw std::runtime_error("executable architecture '" + info.arch +
                                 "' does not match selected package architecture '" + archStr +
                                 "'");
    }
}

/// @brief Read an environment variable, returning "" when it is unset.
std::string getenvOrEmpty(const char *name) {
    const char *value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
}

/// @brief Resolve @p pathText relative to @p projectRoot (absolute paths kept as-is).
fs::path resolveOptionalProjectPath(const std::string &projectRoot, const std::string &pathText) {
    fs::path p(pathText);
    if (p.is_absolute())
        return p.lexically_normal();
    return (fs::path(projectRoot) / p).lexically_normal();
}

/// @brief Return true if the package config requests Windows Authenticode signing.
bool windowsSigningRequested(const viper::pkg::PackageConfig &pkg) {
    return pkg.windowsSign || !pkg.windowsSignPfx.empty() || !pkg.windowsSignThumbprint.empty();
}

/// @brief Sign a Windows installer artifact when signing is requested.
/// @details Resolves the PFX path / certificate thumbprint (falling back to the
///          VIPER_WINDOWS_SIGN_* environment variables), then invokes signtool;
///          a no-op success when no signing was requested.
/// @return true on success or when signing was not requested; false on failure.
bool signWindowsInstallerArtifact(const ProjectConfig &proj,
                                  const fs::path &artifactPath,
                                  bool verbose,
                                  std::ostream &err) {
    const auto &pkg = proj.packageConfig;
    if (!windowsSigningRequested(pkg))
        return true;

    std::string pfxPath = pkg.windowsSignPfx;
    if (pfxPath.empty())
        pfxPath = getenvOrEmpty("VIPER_WINDOWS_SIGN_PFX");
    std::string thumbprint = pkg.windowsSignThumbprint;
    if (thumbprint.empty())
        thumbprint = getenvOrEmpty("VIPER_WINDOWS_SIGN_THUMBPRINT");
    try {
        thumbprint = viper::pkg::normalizeWindowsCertificateThumbprint(
            thumbprint, "Windows signing thumbprint");
    } catch (const std::exception &ex) {
        err << "error: " << ex.what() << "\n";
        return false;
    }
    if (pfxPath.empty() && thumbprint.empty()) {
        err << "error: Windows signing requested but no PFX was provided "
               "and no certificate thumbprint was provided (use --windows-sign-pfx, "
               "windows-sign-pfx, VIPER_WINDOWS_SIGN_PFX, --windows-sign-thumbprint, "
               "windows-sign-thumbprint, or VIPER_WINDOWS_SIGN_THUMBPRINT)\n";
        return false;
    }
    fs::path resolvedPfx;
    std::string password;
    if (!pfxPath.empty()) {
        resolvedPfx = resolveOptionalProjectPath(proj.rootDir, pfxPath);
        if (!fs::is_regular_file(resolvedPfx)) {
            err << "error: Windows signing PFX not found: " << resolvedPfx.string() << "\n";
            return false;
        }
        password = getenvOrEmpty("VIPER_WINDOWS_SIGN_PASSWORD");
        if (password.empty()) {
            err << "error: Windows PFX signing requires VIPER_WINDOWS_SIGN_PASSWORD\n";
            return false;
        }
        const std::string allowPasswordArgv = getenvOrEmpty("VIPER_WINDOWS_SIGN_PASSWORD_ARGV_OK");
        if (allowPasswordArgv != "1" && allowPasswordArgv != "true" &&
            allowPasswordArgv != "TRUE") {
            err << "error: PFX password signing passes the password to signtool argv; use "
                   "certificate-store thumbprint signing or set "
                   "VIPER_WINDOWS_SIGN_PASSWORD_ARGV_OK=1 to acknowledge the exposure\n";
            return false;
        }
    }

    std::string timestampUrl = pkg.windowsTimestampUrl;
    if (timestampUrl.empty())
        timestampUrl = getenvOrEmpty("VIPER_WINDOWS_TIMESTAMP_URL");
    if (timestampUrl.empty())
        timestampUrl = "https://timestamp.digicert.com";
    try {
        viper::pkg::validatePackageUrl(timestampUrl, "Windows timestamp URL");
    } catch (const std::exception &ex) {
        err << "error: " << ex.what() << "\n";
        return false;
    }

    std::string signtool = pkg.windowsSigntoolPath;
    if (signtool.empty())
        signtool = getenvOrEmpty("VIPER_WINDOWS_SIGNTOOL");
    if (signtool.empty())
        signtool = "signtool.exe";

    std::vector<std::string> signCmd = {
        signtool, "sign", "/fd", "SHA256", "/tr", timestampUrl, "/td", "SHA256"};
    if (!thumbprint.empty()) {
        signCmd.push_back("/sha1");
        signCmd.push_back(thumbprint);
    } else {
        signCmd.push_back("/f");
        signCmd.push_back(resolvedPfx.string());
        signCmd.push_back("/p");
        signCmd.push_back(password);
    }
    signCmd.push_back(artifactPath.string());
    const RunResult signResult = run_process(signCmd);
    if (signResult.exit_code != 0) {
        err << "error: signtool sign failed with exit code " << signResult.exit_code << "\n"
            << signResult.out << signResult.err;
        return false;
    }

    if (!pkg.windowsSignNoVerify) {
        const RunResult verifyResult =
            run_process({signtool, "verify", "/pa", "/all", artifactPath.string()});
        if (verifyResult.exit_code != 0) {
            err << "error: signtool verify failed with exit code " << verifyResult.exit_code << "\n"
                << verifyResult.out << verifyResult.err;
            return false;
        }
    }

    if (verbose)
        err << "  Windows signing: passed\n";
    return true;
}

/// @brief Consume a package option value from inline or following-argument syntax.
/// @details Matches either `--name=value` or `--name value`, advances @p index
///          only for the separated form, and optionally rejects an empty value.
///          Keeping this in one place makes target-specific package options
///          behave like the top-level target/output options.
/// @param arg Current argument token.
/// @param optionName Option name including leading dashes.
/// @param index Current argv index, advanced when the value is the next token.
/// @param argc Total argument count.
/// @param argv Argument vector.
/// @param value Receives the parsed value when the option matched.
/// @param description User-facing noun for missing-value diagnostics.
/// @param allowEmpty Whether `--name=` or `--name ""` is accepted.
/// @param matched Receives whether @p arg matched @p optionName at all.
/// @return True when @p arg matched @p optionName and @p value was populated.
bool consumePackageOptionValue(std::string_view arg,
                               std::string_view optionName,
                               int &index,
                               int argc,
                               char **argv,
                               std::string &value,
                               std::string_view description,
                               bool allowEmpty,
                               bool &matched) {
    matched = false;
    std::string_view inlineValue;
    if (ilc::splitInlineOptionValue(arg, optionName, inlineValue)) {
        matched = true;
        if (!allowEmpty && inlineValue.empty()) {
            std::cerr << "error: " << optionName << " requires " << description << "\n";
            return false;
        }
        value = std::string(inlineValue);
        return true;
    }
    if (arg != optionName)
        return false;
    matched = true;
    if (index + 1 >= argc) {
        std::cerr << "error: " << optionName << " requires " << description << "\n";
        return false;
    }
    value = argv[++index];
    if (!allowEmpty && value.empty()) {
        std::cerr << "error: " << optionName << " requires " << description << "\n";
        return false;
    }
    return true;
}

/// @brief Parse the `viper package` command-line arguments into @p args.
/// @details Handles the target/output/arch/executable options plus the macOS and
///          Windows signing option families; prints usage and returns false on a
///          malformed or missing-value argument.
/// @return true on a successful parse.
bool parsePackageArgs(int argc, char **argv, PackageArgs &args) {
    for (int i = 0; i < argc; i++) {
        std::string arg = argv[i];
        bool matched = false;
        std::string val;
        if (consumePackageOptionValue(
                arg, "--target", i, argc, argv, val, "a value", false, matched)) {
            if (!parsePackageTargetValue(val, args.platformTarget)) {
                std::cerr << "error: unknown target '" << val
                          << "'; expected macos, linux, windows, or tarball\n";
                return false;
            }
        } else if (matched) {
            return false;
        } else if (consumePackageOptionValue(arg,
                                             "--arch",
                                             i,
                                             argc,
                                             argv,
                                             args.archOverride,
                                             "a value",
                                             false,
                                             matched)) {
            if (!isPackageArchName(args.archOverride)) {
                std::cerr << "error: unknown arch '" << args.archOverride
                          << "'; expected x64 or arm64\n";
                return false;
            }
        } else if (matched) {
            return false;
        } else if (consumePackageOptionValue(arg,
                                             "--executable",
                                             i,
                                             argc,
                                             argv,
                                             args.executablePath,
                                             "a path",
                                             false,
                                             matched)) {
            // Parsed above.
        } else if (matched) {
            return false;
        } else if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "error: -o requires a path\n";
                return false;
            }
            args.outputPath = argv[++i];
            if (args.outputPath.empty()) {
                std::cerr << "error: -o requires a path\n";
                return false;
            }
        } else if (consumePackageOptionValue(
                       arg, "--output", i, argc, argv, args.outputPath, "a path", false, matched)) {
            // Parsed above.
        } else if (matched) {
            return false;
        } else if (consumePackageOptionValue(arg,
                                             "--macos-sign-mode",
                                             i,
                                             argc,
                                             argv,
                                             args.macosSignMode,
                                             "a value",
                                             true,
                                             matched)) {
            args.macosSignModeSet = true;
        } else if (matched) {
            return false;
        } else if (consumePackageOptionValue(arg,
                                             "--macos-sign-identity",
                                             i,
                                             argc,
                                             argv,
                                             args.macosSignIdentity,
                                             "a value",
                                             true,
                                             matched)) {
            args.macosSignIdentitySet = true;
        } else if (matched) {
            return false;
        } else if (consumePackageOptionValue(arg,
                                             "--macos-entitlements",
                                             i,
                                             argc,
                                             argv,
                                             args.macosEntitlements,
                                             "a path",
                                             true,
                                             matched)) {
            args.macosEntitlementsSet = true;
        } else if (matched) {
            return false;
        } else if (consumePackageOptionValue(arg,
                                             "--macos-notary-profile",
                                             i,
                                             argc,
                                             argv,
                                             args.macosNotaryProfile,
                                             "a value",
                                             true,
                                             matched)) {
            args.macosNotaryProfileSet = true;
        } else if (matched) {
            return false;
        } else if (arg == "--macos-hardened-runtime") {
            args.macosHardenedRuntime = true;
            args.macosHardenedRuntimeSet = true;
        } else if (arg == "--macos-staple") {
            args.macosStaple = true;
            args.macosStapleSet = true;
        } else if (consumePackageOptionValue(arg,
                                             "--windows-install-scope",
                                             i,
                                             argc,
                                             argv,
                                             args.windowsInstallScope,
                                             "a value",
                                             true,
                                             matched)) {
            args.windowsInstallScopeSet = true;
            if (!args.windowsInstallScope.empty() && args.windowsInstallScope != "machine" &&
                args.windowsInstallScope != "user") {
                std::cerr << "error: unknown Windows install scope '" << args.windowsInstallScope
                          << "'; expected machine or user\n";
                return false;
            }
        } else if (matched) {
            return false;
        } else if (consumePackageOptionValue(arg,
                                             "--windows-install-dir",
                                             i,
                                             argc,
                                             argv,
                                             args.windowsInstallDir,
                                             "a value",
                                             true,
                                             matched)) {
            args.windowsInstallDirSet = true;
        } else if (matched) {
            return false;
        } else if (arg == "--windows-sign") {
            args.windowsSign = true;
            args.windowsSignSet = true;
        } else if (consumePackageOptionValue(arg,
                                             "--linux-sign-key",
                                             i,
                                             argc,
                                             argv,
                                             args.linuxSignKey,
                                             "a value",
                                             true,
                                             matched)) {
            args.linuxSignKeySet = true;
        } else if (matched) {
            return false;
        } else if (consumePackageOptionValue(arg,
                                             "--windows-sign-pfx",
                                             i,
                                             argc,
                                             argv,
                                             args.windowsSignPfx,
                                             "a path",
                                             true,
                                             matched)) {
            args.windowsSignPfxSet = true;
        } else if (matched) {
            return false;
        } else if (consumePackageOptionValue(arg,
                                             "--windows-sign-thumbprint",
                                             i,
                                             argc,
                                             argv,
                                             args.windowsSignThumbprint,
                                             "a SHA-1 thumbprint",
                                             true,
                                             matched)) {
            args.windowsSignThumbprintSet = true;
        } else if (matched) {
            return false;
        } else if (consumePackageOptionValue(arg,
                                             "--windows-timestamp-url",
                                             i,
                                             argc,
                                             argv,
                                             args.windowsTimestampUrl,
                                             "a URL",
                                             true,
                                             matched)) {
            args.windowsTimestampUrlSet = true;
        } else if (matched) {
            return false;
        } else if (consumePackageOptionValue(arg,
                                             "--windows-signtool",
                                             i,
                                             argc,
                                             argv,
                                             args.windowsSigntoolPath,
                                             "a path",
                                             true,
                                             matched)) {
            args.windowsSigntoolPathSet = true;
        } else if (matched) {
            return false;
        } else if (arg == "--windows-sign-no-verify") {
            args.windowsSignNoVerify = true;
            args.windowsSignNoVerifySet = true;
        } else if (arg == "--dry-run") {
            args.dryRun = true;
        } else if (arg == "--keep-failed-artifact") {
            args.keepFailedArtifact = true;
        } else if (arg == "--verbose" || arg == "-v") {
            args.verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            packageUsage(std::cout);
            args.help = true;
            return true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "error: unknown option '" << arg << "'\n";
            packageUsage();
            return false;
        } else if (args.target.empty()) {
            args.target = arg;
        } else {
            std::cerr << "error: unexpected argument '" << arg << "'\n";
            return false;
        }
    }

    if (args.target.empty())
        args.target = ".";

    if (args.platformTarget == PackageTarget::Auto)
        args.platformTarget = detectHostPlatform();

    return true;
}

/// @brief Verify a project-relative package source path exists on disk.
/// @details Resolves @p path against the project root and checks it is a regular
///          file (or directory when @p allowDirectory); prints an error otherwise.
/// @return true when the path exists and is of an acceptable type.
bool validatePackageSourcePathExists(const ProjectConfig &proj,
                                     const std::string &path,
                                     const char *fieldName,
                                     bool allowDirectory = true) {
    fs::path resolved;
    try {
        resolved = viper::pkg::resolvePackageSourcePath(proj.rootDir, path, fieldName);
    } catch (const std::exception &ex) {
        std::cerr << "error: invalid " << fieldName << " '" << path << "': " << ex.what() << "\n";
        return false;
    }
    std::error_code ec;
    if (!fs::exists(resolved, ec)) {
        if (ec)
            std::cerr << "error: cannot inspect " << fieldName << " '" << path
                      << "': " << ec.message() << "\n";
        else
            std::cerr << "error: " << fieldName << " not found: " << path << "\n";
        return false;
    }
    ec.clear();
    const bool regular = fs::is_regular_file(resolved, ec);
    if (ec) {
        std::cerr << "error: cannot inspect " << fieldName << " '" << path << "': " << ec.message()
                  << "\n";
        return false;
    }
    ec.clear();
    const bool directory = fs::is_directory(resolved, ec);
    if (ec) {
        std::cerr << "error: cannot inspect " << fieldName << " '" << path << "': " << ec.message()
                  << "\n";
        return false;
    }
    if (!regular && !(allowDirectory && directory)) {
        std::cerr << "error: " << fieldName
                  << (allowDirectory ? " is not a regular file or directory: "
                                     : " is not a regular file: ")
                  << path << "\n";
        return false;
    }
    return true;
}

/// @brief Validate the package config (icon, assets, signing) for a given target.
/// @details Checks that referenced source paths exist and that target-specific
///          signing/installer settings are well-formed before building, printing
///          errors to @p err.
/// @param requireSigningCredentials When false, dry-run validation keeps structural
///        signing checks but skips credential/material presence checks that are only
///        needed for an actual package build.
/// @return true when the configuration is valid for @p target / @p archStr.
bool validatePackageConfigForTarget(const ProjectConfig &proj,
                                    PackageTarget target,
                                    const std::string &archStr,
                                    std::ostream &err,
                                    bool requireSigningCredentials = true) {
    const auto &pkg = proj.packageConfig;
    const std::string displayName = pkg.displayName.empty() ? proj.name : pkg.displayName;
    const std::string version = proj.version.empty() ? "0.0.0" : proj.version;

    try {
        viper::pkg::validateSingleLineField(displayName, "package display name");
        viper::pkg::validateSingleLineField(pkg.author, "package author");
        viper::pkg::validateSingleLineField(pkg.description, "package description");
        viper::pkg::validateSingleLineField(pkg.license, "package license");
        viper::pkg::validatePackageUrl(pkg.homepage, "package homepage");
        viper::pkg::validatePackageFileAssociations(pkg.fileAssociations);
        for (const auto &asset : pkg.assets) {
            (void)viper::pkg::sanitizePackageRelativePath(asset.targetPath, "asset target path");
            if (!validatePackageSourcePathExists(proj, asset.sourcePath, "asset source path"))
                return false;
        }
        if (!pkg.iconPath.empty() &&
            !validatePackageSourcePathExists(proj, pkg.iconPath, "package icon", false))
            return false;

        switch (target) {
            case PackageTarget::MacOS:
            case PackageTarget::Dmg:
                if (displayName.empty() || displayName.find('/') != std::string::npos ||
                    displayName.find('\\') != std::string::npos ||
                    displayName.find(':') != std::string::npos) {
                    throw std::runtime_error(
                        "macOS bundle display name must not be empty or contain path separators");
                }
                viper::pkg::validateWindowsFileName(displayName, "macOS bundle display name");
                viper::pkg::validateMacOSBundleIdentifier(pkg.identifier,
                                                          "macOS bundle identifier");
                viper::pkg::validateDottedNumericVersion(version, "macOS package version");
                if (!pkg.minOsMacos.empty())
                    viper::pkg::validateDottedNumericVersion(pkg.minOsMacos,
                                                             "minimum macOS version");
                if (requireSigningCredentials)
                    viper::pkg::validateMacOSSigningConfig(pkg);
                else if (!viper::pkg::isValidMacOSSignModeText(pkg.macosSignMode)) {
                    throw std::runtime_error("macOS sign mode must be none, preserve, adhoc, or "
                                             "developer-id: " +
                                             pkg.macosSignMode);
                } else {
                    const std::string mode = viper::pkg::resolveMacOSSignModeForHost(pkg);
                    if (!pkg.macosNotaryProfile.empty() && mode != "developer-id") {
                        throw std::runtime_error(
                            "macOS notarization requires macos-sign-mode developer-id");
                    }
                    if (pkg.macosStaple) {
                        if (mode != "developer-id")
                            throw std::runtime_error(
                                "macos-staple requires macos-sign-mode developer-id");
                        if (pkg.macosNotaryProfile.empty())
                            throw std::runtime_error(
                                "macos-staple requires macos-notary-profile for this package "
                                "build");
                    }
                }
                if (!pkg.macosEntitlements.empty() &&
                    !validatePackageSourcePathExists(
                        proj, pkg.macosEntitlements, "macOS entitlements", false)) {
                    return false;
                }
                break;
            case PackageTarget::Linux: {
                const std::string debArch = archStr == "x64" ? "amd64" : archStr;
                if (debArch != "amd64" && debArch != "arm64" && debArch != "all") {
                    throw std::runtime_error(
                        "Debian package architecture must be amd64, arm64, or all: " + debArch);
                }
                viper::pkg::validateDebVersion(version, "package version");
                viper::pkg::validateDesktopCategories(pkg.category);
                for (const auto &dep : pkg.depends)
                    viper::pkg::validateDebDependency(dep);
                viper::pkg::validatePackageHooksAllowed(pkg);
                (void)viper::pkg::normalizePackageHookScript(pkg.postInstallScript,
                                                             "post-install script");
                (void)viper::pkg::normalizePackageHookScript(pkg.preUninstallScript,
                                                             "pre-uninstall script");
                (void)viper::pkg::normalizeDebName(proj.name);
                (void)viper::pkg::normalizeExecName(proj.name);
                break;
            }
            case PackageTarget::Windows:
                viper::pkg::validateWindowsFileName(displayName, "Windows display name");
                viper::pkg::validateWindowsProgIdBase(pkg.identifier, "Windows package identifier");
                viper::pkg::validateSingleLineField(version, "Windows package version");
                if (!pkg.windowsInstallScope.empty() && pkg.windowsInstallScope != "machine" &&
                    pkg.windowsInstallScope != "user") {
                    throw std::runtime_error("Windows install scope must be machine or user: " +
                                             pkg.windowsInstallScope);
                }
                if (!pkg.windowsInstallDir.empty())
                    viper::pkg::validateWindowsFileName(pkg.windowsInstallDir,
                                                        "Windows install directory");
                if (!pkg.minOsWindows.empty())
                    viper::pkg::validateDottedNumericVersion(pkg.minOsWindows,
                                                             "minimum Windows version");
                viper::pkg::validatePackageUrl(pkg.windowsTimestampUrl, "Windows timestamp URL");
                viper::pkg::validateSingleLineField(pkg.windowsSigntoolPath,
                                                    "Windows signtool path");
                viper::pkg::validateWindowsCertificateThumbprint(pkg.windowsSignThumbprint,
                                                                 "Windows signing thumbprint");
                if (requireSigningCredentials && !pkg.windowsSignPfx.empty()) {
                    const fs::path pfx =
                        resolveOptionalProjectPath(proj.rootDir, pkg.windowsSignPfx);
                    if (!fs::is_regular_file(pfx)) {
                        throw std::runtime_error("Windows signing PFX not found: " + pfx.string());
                    }
                }
                (void)viper::pkg::normalizeExecName(proj.name);
                break;
            case PackageTarget::Tarball:
                viper::pkg::validateDebVersion(version, "package version");
                (void)viper::pkg::sanitizePackageRelativePath(
                    viper::pkg::normalizeDebName(proj.name) + "-" +
                        portableArchiveVersionComponent(version),
                    "tarball top-level directory");
                (void)viper::pkg::normalizeExecName(proj.name);
                break;
            case PackageTarget::AppImage: {
                if (archStr != "x64" && archStr != "arm64") {
                    throw std::runtime_error("AppImage architecture must be x64 or arm64: " +
                                             archStr);
                }
                viper::pkg::validateDebVersion(version, "package version");
                viper::pkg::validateDesktopCategories(pkg.category);
                (void)viper::pkg::normalizeDebName(proj.name);
                (void)viper::pkg::normalizeExecName(proj.name);
                break;
            }
            case PackageTarget::Rpm: {
                if (archStr != "x64" && archStr != "arm64") {
                    throw std::runtime_error("RPM architecture must be x64 or arm64: " + archStr);
                }
                viper::pkg::validateDebVersion(version, "package version");
                viper::pkg::validateDesktopCategories(pkg.category);
                (void)viper::pkg::normalizeDebName(proj.name);
                (void)viper::pkg::normalizeExecName(proj.name);
                break;
            }
            case PackageTarget::Auto:
                break;
        }
    } catch (const std::exception &ex) {
        err << "error: package configuration invalid: " << ex.what() << "\n";
        return false;
    }
    return true;
}

/// @brief Overlay command-line `--macos-*` / `--windows-*` signing & install
///        options onto the project's package config (CLI flags win over the
///        manifest). Only set options override; unset ones leave the manifest
///        value intact.
void applyPackageCliOverrides(ProjectConfig &proj, const PackageArgs &args) {
    if (args.macosSignModeSet)
        proj.packageConfig.macosSignMode = args.macosSignMode;
    if (args.macosSignIdentitySet)
        proj.packageConfig.macosSignIdentity = args.macosSignIdentity;
    if (args.macosEntitlementsSet)
        proj.packageConfig.macosEntitlements = args.macosEntitlements;
    if (args.macosNotaryProfileSet)
        proj.packageConfig.macosNotaryProfile = args.macosNotaryProfile;
    if (args.macosHardenedRuntimeSet)
        proj.packageConfig.macosHardenedRuntime = args.macosHardenedRuntime;
    if (args.macosStapleSet)
        proj.packageConfig.macosStaple = args.macosStaple;
    if (args.windowsInstallScopeSet)
        proj.packageConfig.windowsInstallScope = args.windowsInstallScope;
    if (args.windowsInstallDirSet)
        proj.packageConfig.windowsInstallDir = args.windowsInstallDir;
    if (args.windowsSignSet) {
        proj.packageConfig.windowsSign = args.windowsSign;
        proj.packageConfig.windowsSignSet = true;
    }
    if (args.windowsSignPfxSet)
        proj.packageConfig.windowsSignPfx = args.windowsSignPfx;
    if (args.windowsSignThumbprintSet)
        proj.packageConfig.windowsSignThumbprint = args.windowsSignThumbprint;
    if (args.windowsTimestampUrlSet)
        proj.packageConfig.windowsTimestampUrl = args.windowsTimestampUrl;
    if (args.windowsSigntoolPathSet)
        proj.packageConfig.windowsSigntoolPath = args.windowsSigntoolPath;
    if (args.windowsSignNoVerifySet)
        proj.packageConfig.windowsSignNoVerify = args.windowsSignNoVerify;
}

/// @brief Append one required payload path for package verification.
/// @details Joins @p payloadPrefix, @p targetDir, and @p leaf through the shared
///          packaging path sanitizer so verification expects the same normalized
///          layout that the builders emit.
void appendRequiredPayloadPath(std::vector<std::string> &paths,
                               const std::string &payloadPrefix,
                               const std::string &targetDir,
                               const std::string &leaf) {
    const std::string underTarget =
        viper::pkg::joinPackageRelativePath(targetDir, leaf, "asset path");
    paths.push_back(
        viper::pkg::joinPackageRelativePath(payloadPrefix, underTarget, "asset payload path"));
}

/// @brief Return required payload paths for configured package assets.
/// @details Mirrors the package builders' asset layout: regular-file assets are
///          installed under their target directory using the source leaf name,
///          and directory assets install each regular file at its relative path
///          beneath the configured target directory. Directories themselves are
///          not required so empty-directory handling remains platform-specific.
std::vector<std::string> requiredAssetPayloadPaths(const ProjectConfig &proj,
                                                   const std::string &payloadPrefix) {
    std::vector<std::string> paths;
    for (const auto &asset : proj.packageConfig.assets) {
        const std::string sourceRel =
            viper::pkg::sanitizePackageRelativePath(asset.sourcePath, "asset source path");
        const std::string sourceLeaf = fs::path(sourceRel).filename().generic_string();
        const std::string targetDir =
            viper::pkg::sanitizePackageRelativePath(asset.targetPath, "asset target path");
        const fs::path srcPath = viper::pkg::resolvePackageSourcePath(
            proj.rootDir, asset.sourcePath, "asset source path");

        std::error_code ec;
        if (fs::is_directory(srcPath, ec)) {
            if (ec)
                throw std::runtime_error("cannot inspect asset source path '" + asset.sourcePath +
                                         "': " + ec.message());
            viper::pkg::safeDirectoryIterateResolved(
                srcPath, proj.rootDir, [&](const viper::pkg::SafeDirectoryEntry &entry) {
                    if (!entry.regularFile)
                        return;
                    const std::string relPath = viper::pkg::sanitizePackageRelativePath(
                        entry.logicalPath.lexically_relative(srcPath).generic_string(),
                        "asset path");
                    appendRequiredPayloadPath(paths, payloadPrefix, targetDir, relPath);
                });
            continue;
        }
        if (ec)
            throw std::runtime_error("cannot inspect asset source path '" + asset.sourcePath +
                                     "': " + ec.message());

        ec.clear();
        if (fs::is_regular_file(srcPath, ec)) {
            if (ec)
                throw std::runtime_error("cannot inspect asset source path '" + asset.sourcePath +
                                         "': " + ec.message());
            appendRequiredPayloadPath(paths, payloadPrefix, targetDir, sourceLeaf);
            continue;
        }
        if (ec)
            throw std::runtime_error("cannot inspect asset source path '" + asset.sourcePath +
                                     "': " + ec.message());
        throw std::runtime_error("asset is not a regular file or directory: " + asset.sourcePath);
    }
    return paths;
}

} // namespace

int cmdPackage(int argc, char **argv) {
    using namespace il::tools::common;
    namespace fs = std::filesystem;

    PackageArgs args;
    if (!parsePackageArgs(argc, argv, args))
        return 1;
    if (args.help)
        return 0;

    // Resolve project
    auto project = resolveProject(args.target);
    if (!project) {
        SourceManager sm;
        il::support::printDiag(project.error(), std::cerr, &sm);
        return 1;
    }

    ProjectConfig &proj = project.value();

    applyPackageCliOverrides(proj, args);

    // Check that package config is present
    if (!proj.packageConfig.hasPackageConfig()) {
        std::cerr << "warning: no package-* directives in viper.project; "
                  << "using defaults\n";
    }

    // Determine display name and version
    std::string displayName =
        proj.packageConfig.displayName.empty() ? proj.name : proj.packageConfig.displayName;
    const std::string resolvedVersion = proj.version.empty() ? "0.0.0" : proj.version;

    // Determine architecture
    viper::tools::TargetArch arch = viper::tools::detectHostArch();
    std::string archStr;
    if (!args.archOverride.empty()) {
        arch = (args.archOverride == "arm64") ? viper::tools::TargetArch::ARM64
                                              : viper::tools::TargetArch::X64;
        archStr = args.archOverride;
    } else if (proj.packageConfig.targetArchitectures.size() == 1) {
        archStr = proj.packageConfig.targetArchitectures.front();
        arch =
            (archStr == "arm64") ? viper::tools::TargetArch::ARM64 : viper::tools::TargetArch::X64;
    } else {
        archStr = (arch == viper::tools::TargetArch::ARM64) ? "arm64" : "x64";
    }
    if (!proj.packageConfig.targetArchitectures.empty()) {
        auto it = std::find(proj.packageConfig.targetArchitectures.begin(),
                            proj.packageConfig.targetArchitectures.end(),
                            archStr);
        if (it == proj.packageConfig.targetArchitectures.end()) {
            std::cerr << "error: selected package architecture '" << archStr
                      << "' is not listed by target-arch in viper.project\n";
            return 1;
        }
    }

    if (args.outputPath.empty()) {
        args.outputPath =
            defaultPackageOutputPath(proj, resolvedVersion, args.platformTarget, archStr);
    }

    if (!validatePackageConfigForTarget(
            proj, args.platformTarget, archStr, std::cerr, !args.dryRun))
        return 1;

    // Dry-run mode: list what would be packaged, then exit
    if (args.dryRun) {
        std::ostream &dryOut = std::cout;
        dryOut << "Dry run: " << displayName << " for " << platformName(args.platformTarget) << " ("
               << archStr << ")\n";
        dryOut << "  Output: " << args.outputPath << "\n";
        if (!args.executablePath.empty())
            dryOut << "  Executable: " << args.executablePath << " (prebuilt)\n";
        else
            dryOut << "  Executable: " << proj.name << " (build)\n";
        if (!proj.packageConfig.iconPath.empty()) {
            fs::path iconPath;
            dryOut << "  Icon: " << proj.packageConfig.iconPath;
            try {
                iconPath = viper::pkg::resolvePackageSourcePath(
                    proj.rootDir, proj.packageConfig.iconPath, "package icon");
            } catch (const std::exception &ex) {
                dryOut << " [INVALID: " << ex.what() << "]";
            }
            std::error_code iconEc;
            if (!iconPath.empty() && !fs::exists(iconPath, iconEc))
                dryOut << " [NOT FOUND]";
            dryOut << "\n";
        }
        if (args.platformTarget == PackageTarget::MacOS) {
            const std::string signMode =
                viper::pkg::resolveMacOSSignModeForHost(proj.packageConfig);
            dryOut << "  macOS signing: " << signMode << "\n";
            if (!proj.packageConfig.macosSignIdentity.empty())
                dryOut << "  macOS signing identity: " << proj.packageConfig.macosSignIdentity
                       << "\n";
            if (!proj.packageConfig.macosEntitlements.empty())
                dryOut << "  macOS entitlements: " << proj.packageConfig.macosEntitlements << "\n";
            if (proj.packageConfig.macosHardenedRuntime)
                dryOut << "  macOS hardened runtime: on\n";
            if (!proj.packageConfig.macosNotaryProfile.empty())
                dryOut << "  macOS notary profile: " << proj.packageConfig.macosNotaryProfile
                       << "\n";
            if (proj.packageConfig.macosStaple)
                dryOut << "  macOS staple: on\n";
        }
        if (args.platformTarget == PackageTarget::Windows) {
            const std::string scope = proj.packageConfig.windowsInstallScope.empty()
                                          ? "machine"
                                          : proj.packageConfig.windowsInstallScope;
            dryOut << "  Windows install scope: " << scope << "\n";
            if (!proj.packageConfig.windowsInstallDir.empty())
                dryOut << "  Windows install directory: " << proj.packageConfig.windowsInstallDir
                       << "\n";
            if (windowsSigningRequested(proj.packageConfig)) {
                dryOut << "  Windows signing: on\n";
                if (!proj.packageConfig.windowsSignPfx.empty())
                    dryOut << "  Windows signing PFX: " << proj.packageConfig.windowsSignPfx
                           << "\n";
                if (!proj.packageConfig.windowsSignThumbprint.empty())
                    dryOut << "  Windows signing thumbprint: "
                           << proj.packageConfig.windowsSignThumbprint << "\n";
                if (!proj.packageConfig.windowsTimestampUrl.empty())
                    dryOut << "  Windows timestamp URL: " << proj.packageConfig.windowsTimestampUrl
                           << "\n";
            }
        }
        for (const auto &asset : proj.packageConfig.assets) {
            fs::path assetPath;
            dryOut << "  Asset: " << asset.sourcePath << " -> " << asset.targetPath;
            try {
                assetPath = viper::pkg::resolvePackageSourcePath(
                    proj.rootDir, asset.sourcePath, "asset source path");
            } catch (const std::exception &ex) {
                dryOut << " [INVALID: " << ex.what() << "]";
            }
            std::error_code assetEc;
            if (!assetPath.empty() && !fs::exists(assetPath, assetEc))
                dryOut << " [NOT FOUND]";
            else if (!assetPath.empty() && fs::is_directory(assetPath, assetEc)) {
                size_t count = 0;
                try {
                    viper::pkg::safeDirectoryIterateResolved(
                        assetPath, proj.rootDir, [&](const viper::pkg::SafeDirectoryEntry &e) {
                            if (e.regularFile)
                                ++count;
                        });
                    dryOut << " (" << count << " files)";
                } catch (const std::exception &ex) {
                    dryOut << " [INVALID: " << ex.what() << "]";
                }
            }
            dryOut << "\n";
        }
        for (const auto &assoc : proj.packageConfig.fileAssociations) {
            dryOut << "  File assoc: " << assoc.extension << " (" << assoc.description << ")";
            if (!assoc.openCommandArguments.empty())
                dryOut << " [Windows open args: " << assoc.openCommandArguments << "]";
            dryOut << "\n";
        }
        if (!proj.packageConfig.category.empty())
            dryOut << "  Category: " << proj.packageConfig.category << "\n";
        if (!proj.packageConfig.depends.empty()) {
            dryOut << "  Depends:";
            for (const auto &d : proj.packageConfig.depends)
                dryOut << " " << d;
            dryOut << "\n";
        }
        return 0;
    }

    const PackageTarget hostPlatform = detectHostPlatform();
    if (args.executablePath.empty() && args.platformTarget != PackageTarget::Tarball &&
        args.platformTarget != hostPlatform) {
        std::cerr << "error: packaging for '" << platformName(args.platformTarget)
                  << "' from this host requires --executable because the built-in compile path "
                     "still targets the host platform\n";
        return 1;
    }
    if (args.executablePath.empty() && args.platformTarget == PackageTarget::Tarball &&
        arch != viper::tools::detectHostArch()) {
        std::cerr << "error: tarball packaging for architecture '" << archStr
                  << "' requires --executable because the built-in compile path still targets "
                     "the host architecture\n";
        return 1;
    }

    std::string packageBinaryPath;
    bool cleanupPackagedBinary = false;

    if (!args.executablePath.empty()) {
        fs::path exePath(args.executablePath);
        if (!exePath.is_absolute()) {
            std::error_code cwdEc;
            auto cwd = fs::current_path(cwdEc);
            if (cwdEc) {
                std::cerr << "error: cannot resolve current directory: " << cwdEc.message() << "\n";
                return 1;
            }
            exePath = cwd / exePath;
        }
        std::error_code exeEc;
        const fs::path canonicalExe = fs::weakly_canonical(exePath, exeEc);
        packageBinaryPath = (exeEc ? exePath.lexically_normal() : canonicalExe).string();
        exeEc.clear();
        if (!fs::exists(packageBinaryPath, exeEc)) {
            if (exeEc) {
                std::cerr << "error: cannot inspect prebuilt executable at " << packageBinaryPath
                          << ": " << exeEc.message() << "\n";
                return 1;
            }
            std::cerr << "error: prebuilt executable not found at " << packageBinaryPath << "\n";
            return 1;
        }
        exeEc.clear();
        if (!fs::is_regular_file(packageBinaryPath, exeEc)) {
            if (exeEc) {
                std::cerr << "error: cannot inspect prebuilt executable at " << packageBinaryPath
                          << ": " << exeEc.message() << "\n";
                return 1;
            }
            std::cerr << "error: prebuilt executable is not a regular file at " << packageBinaryPath
                      << "\n";
            return 1;
        }
        try {
            validateExecutableForPackageTarget(packageBinaryPath, args.platformTarget, archStr);
        } catch (const std::exception &ex) {
            std::cerr << "error: prebuilt executable is not valid for this package: " << ex.what()
                      << "\n";
            return 1;
        }
    } else {
        // Step 1: Compile to native binary (using build pipeline)
        std::cerr << "Compiling " << proj.name << "...\n";

        // Build through the same in-process path as `viper build` so packaging
        // observes project manifests, frontend options, assets, and native
        // backend flags consistently.
        std::string tempBinaryExt;
#if VIPER_HOST_WINDOWS
        tempBinaryExt = ".exe";
#endif
        std::string tempBinaryPath =
            viper::tools::generateTempFilePath("viper_package", tempBinaryExt.c_str());
        packageBinaryPath = tempBinaryPath;
        cleanupPackagedBinary = true;

        const int buildRc = buildProjectToNativeForPackage(
            args.target, tempBinaryPath, archStr, args.platformTarget == PackageTarget::Windows);
        if (buildRc != 0) {
            std::cerr << "error: compilation failed\n";
            std::error_code ec;
            removeFailedArtifactUnlessKept(packageBinaryPath, args.keepFailedArtifact, ec);
            return 1;
        }

        std::error_code compiledEc;
        if (!fs::exists(packageBinaryPath, compiledEc)) {
            if (compiledEc) {
                std::cerr << "error: cannot inspect compiled binary at " << packageBinaryPath
                          << ": " << compiledEc.message() << "\n";
                std::error_code cleanupEc;
                removeFailedArtifactUnlessKept(
                    packageBinaryPath, args.keepFailedArtifact, cleanupEc);
                return 1;
            }
            std::cerr << "error: compiled binary not found at " << packageBinaryPath << "\n";
            std::error_code ec;
            removeFailedArtifactUnlessKept(packageBinaryPath, args.keepFailedArtifact, ec);
            return 1;
        }
        try {
            validateExecutableForPackageTarget(packageBinaryPath, args.platformTarget, archStr);
        } catch (const std::exception &ex) {
            std::cerr << "error: compiled executable is not valid for this package: " << ex.what()
                      << "\n";
            std::error_code ec;
            removeFailedArtifactUnlessKept(packageBinaryPath, args.keepFailedArtifact, ec);
            return 1;
        }
    }

    // Step 3: Package
    std::cerr << "Packaging " << displayName << " for " << platformName(args.platformTarget) << " ("
              << archStr << ")...\n";

    if (args.verbose) {
        std::error_code sizeEc;
        auto binSize = fs::file_size(packageBinaryPath, sizeEc);
        std::cerr << "  Binary: " << packageBinaryPath;
        if (!sizeEc)
            std::cerr << " (" << binSize << " bytes)";
        else
            std::cerr << " (size unavailable: " << sizeEc.message() << ")";
        std::cerr << "\n";
        std::cerr << "  Output: " << args.outputPath << "\n";
        if (!proj.packageConfig.iconPath.empty())
            std::cerr << "  Icon: " << proj.packageConfig.iconPath << "\n";
        for (const auto &asset : proj.packageConfig.assets)
            std::cerr << "  Asset: " << asset.sourcePath << " -> " << asset.targetPath << "\n";
    }

    try {
        if (!args.linuxSignKey.empty() && args.platformTarget != PackageTarget::Linux &&
            args.platformTarget != PackageTarget::Rpm) {
            throw std::runtime_error(
                "--linux-sign-key applies only to --target linux or --target rpm");
        }
        const fs::path outputParent = fs::path(args.outputPath).parent_path();
        if (!outputParent.empty()) {
            std::error_code mkdirEc;
            fs::create_directories(outputParent, mkdirEc);
            if (mkdirEc) {
                throw std::runtime_error("cannot create output directory '" +
                                         outputParent.string() + "': " + mkdirEc.message());
            }
        }
        switch (args.platformTarget) {
            case PackageTarget::MacOS: {
                viper::pkg::MacOSBuildParams params;
                params.projectName = proj.name;
                params.version = resolvedVersion;
                params.executablePath = packageBinaryPath;
                params.projectRoot = proj.rootDir;
                params.pkgConfig = proj.packageConfig;
                params.outputPath = args.outputPath;
                viper::pkg::buildMacOSPackage(params);
                break;
            }
            case PackageTarget::Linux: {
                viper::pkg::LinuxBuildParams lparams;
                lparams.projectName = proj.name;
                lparams.version = resolvedVersion;
                lparams.executablePath = packageBinaryPath;
                lparams.projectRoot = proj.rootDir;
                lparams.pkgConfig = proj.packageConfig;
                lparams.outputPath = args.outputPath;
                // Map architecture: Debian uses "amd64" not "x64"
                lparams.archStr = (archStr == "x64") ? "amd64" : archStr;
                viper::pkg::buildDebPackage(lparams);
                if (!args.linuxSignKey.empty())
                    viper::pkg::signLinuxPackage(args.outputPath,
                                                 args.linuxSignKey,
                                                 /*isRpm=*/false);
                break;
            }
            case PackageTarget::Windows: {
                viper::pkg::WindowsBuildParams wparams;
                wparams.projectName = proj.name;
                wparams.version = resolvedVersion;
                wparams.executablePath = packageBinaryPath;
                wparams.projectRoot = proj.rootDir;
                wparams.pkgConfig = proj.packageConfig;
                wparams.outputPath = args.outputPath;
                wparams.archStr = archStr;
                viper::pkg::buildWindowsPackage(wparams);
                break;
            }
            case PackageTarget::Tarball: {
                viper::pkg::LinuxBuildParams tparams;
                tparams.projectName = proj.name;
                tparams.version = resolvedVersion;
                tparams.executablePath = packageBinaryPath;
                tparams.projectRoot = proj.rootDir;
                tparams.pkgConfig = proj.packageConfig;
                tparams.outputPath = args.outputPath;
                tparams.archStr = archStr;
                viper::pkg::buildTarball(tparams);
                break;
            }
            case PackageTarget::AppImage: {
                viper::pkg::LinuxBuildParams aparams;
                aparams.projectName = proj.name;
                aparams.version = resolvedVersion;
                aparams.executablePath = packageBinaryPath;
                aparams.projectRoot = proj.rootDir;
                aparams.pkgConfig = proj.packageConfig;
                aparams.outputPath = args.outputPath;
                aparams.archStr = archStr; // AppImage uses the portable x64/arm64 form
                viper::pkg::buildAppImage(aparams);
                break;
            }
            case PackageTarget::Rpm: {
                viper::pkg::LinuxBuildParams rparams;
                rparams.projectName = proj.name;
                rparams.version = resolvedVersion;
                rparams.executablePath = packageBinaryPath;
                rparams.projectRoot = proj.rootDir;
                rparams.pkgConfig = proj.packageConfig;
                rparams.outputPath = args.outputPath;
                rparams.archStr = archStr; // RPM uses the portable x64/arm64 form
                viper::pkg::buildRpmPackage(rparams);
                if (!args.linuxSignKey.empty())
                    viper::pkg::signLinuxPackage(args.outputPath,
                                                 args.linuxSignKey,
                                                 /*isRpm=*/true);
                break;
            }
            case PackageTarget::Dmg: {
                viper::pkg::MacOSBuildParams params;
                params.projectName = proj.name;
                params.version = resolvedVersion;
                params.executablePath = packageBinaryPath;
                params.projectRoot = proj.rootDir;
                params.pkgConfig = proj.packageConfig;
                params.outputPath = args.outputPath;
                viper::pkg::buildMacOSAppDmg(params);
                break;
            }
            default:
                break;
        }
    } catch (const std::exception &e) {
        std::cerr << "error: packaging failed: " << e.what() << "\n";
        std::error_code ec;
        if (cleanupPackagedBinary)
            removeFailedArtifactUnlessKept(packageBinaryPath, args.keepFailedArtifact, ec);
        removeFailedArtifactUnlessKept(args.outputPath, args.keepFailedArtifact, ec);
        return 1;
    }

    // Step 4: Verify the generated package
    std::error_code ec;
    if (!fs::exists(args.outputPath, ec)) {
        if (ec)
            std::cerr << "error: cannot inspect generated package: " << ec.message() << "\n";
        else
            std::cerr << "error: package builder did not create output file: " << args.outputPath
                      << "\n";
        if (cleanupPackagedBinary)
            removeFailedArtifactUnlessKept(packageBinaryPath, args.keepFailedArtifact, ec);
        return 1;
    }

    if (args.platformTarget == PackageTarget::Windows &&
        !signWindowsInstallerArtifact(proj, args.outputPath, args.verbose, std::cerr)) {
        removeFailedArtifactUnlessKept(args.outputPath, args.keepFailedArtifact, ec);
        if (cleanupPackagedBinary)
            removeFailedArtifactUnlessKept(packageBinaryPath, args.keepFailedArtifact, ec);
        return 1;
    }

    std::vector<uint8_t> pkgData;
    try {
        pkgData = viper::pkg::readFile(args.outputPath);
    } catch (const std::exception &ex) {
        std::cerr << "error: cannot read generated package for verification: " << ex.what() << "\n";
        if (cleanupPackagedBinary)
            removeFailedArtifactUnlessKept(packageBinaryPath, args.keepFailedArtifact, ec);
        return 1;
    }

    std::ostringstream verifyErr;
    bool valid = true;
    const std::string execName = viper::pkg::normalizeExecName(proj.name);
    try {
        switch (args.platformTarget) {
            case PackageTarget::MacOS: {
                const auto requiredResources = requiredAssetPayloadPaths(proj, "");
                valid = viper::pkg::verifyMacOSAppZipPayload(
                    pkgData, displayName + ".app", execName, requiredResources, verifyErr);
                break;
            }
            case PackageTarget::Linux: {
                std::vector<std::string> required = {"usr/bin/" + execName};
                auto assetPaths = requiredAssetPayloadPaths(
                    proj, "usr/share/" + viper::pkg::normalizeDebName(proj.name));
                required.insert(required.end(), assetPaths.begin(), assetPaths.end());
                valid = viper::pkg::verifyDebPayload(pkgData, required, verifyErr);
                break;
            }
            case PackageTarget::Windows: {
                std::vector<std::string> requiredInner = {
                    execName + ".exe", "uninstall.exe", ".viper-install-manifest.txt"};
                auto assetPaths = requiredAssetPayloadPaths(proj, "");
                requiredInner.insert(requiredInner.end(), assetPaths.begin(), assetPaths.end());
                valid = viper::pkg::verifyPEZipOverlayNestedPayload(
                    pkgData,
                    {"meta/payload.zip", "meta/install_manifest.next", "meta/manifest.sha256"},
                    "meta/payload.zip",
                    requiredInner,
                    verifyErr);
                break;
            }
            case PackageTarget::Tarball: {
                const std::string topDir = viper::pkg::normalizeDebName(proj.name) + "-" +
                                           portableArchiveVersionComponent(resolvedVersion);
                std::vector<std::string> required = {
                    topDir + "/" + execName, topDir + "/README.install", topDir + "/LICENSE"};
                auto assetPaths = requiredAssetPayloadPaths(proj, topDir);
                required.insert(required.end(), assetPaths.begin(), assetPaths.end());
                valid = viper::pkg::verifyTarGzPayload(pkgData, required, verifyErr);
                break;
            }
            case PackageTarget::AppImage: {
                std::string appImageErr;
                valid = viper::pkg::verifyLinuxAppImage(pkgData, &appImageErr);
                if (!valid)
                    verifyErr << appImageErr;
                break;
            }
            case PackageTarget::Rpm: {
                // rpmbuild produces the artifact; confirm the RPM lead magic is present.
                valid = pkgData.size() >= 4 && pkgData[0] == static_cast<uint8_t>(0xED) &&
                        pkgData[1] == static_cast<uint8_t>(0xAB) &&
                        pkgData[2] == static_cast<uint8_t>(0xEE) &&
                        pkgData[3] == static_cast<uint8_t>(0xDB);
                if (!valid)
                    verifyErr << "rpm: missing RPM lead magic\n";
                break;
            }
            case PackageTarget::Dmg: {
                // hdiutil produces a UDIF image; confirm the trailing "koly" block.
                const size_t n = pkgData.size();
                valid = n >= 512 && pkgData[n - 512] == 'k' && pkgData[n - 511] == 'o' &&
                        pkgData[n - 510] == 'l' && pkgData[n - 509] == 'y';
                if (!valid)
                    verifyErr << "dmg: missing UDIF koly trailer\n";
                break;
            }
            default:
                break;
        }
    } catch (const std::exception &ex) {
        std::cerr << "error: cannot prepare package verification: " << ex.what() << "\n";
        removeFailedArtifactUnlessKept(args.outputPath, args.keepFailedArtifact, ec);
        if (cleanupPackagedBinary)
            removeFailedArtifactUnlessKept(packageBinaryPath, args.keepFailedArtifact, ec);
        return 1;
    }
    if (!valid) {
        std::cerr << "error: package verification failed:\n" << verifyErr.str();
        removeFailedArtifactUnlessKept(args.outputPath, args.keepFailedArtifact, ec);
        if (cleanupPackagedBinary)
            removeFailedArtifactUnlessKept(packageBinaryPath, args.keepFailedArtifact, ec);
        return 1;
    }
    if (args.verbose)
        std::cerr << "  Verification: passed\n";

    // Cleanup temp binary
    if (cleanupPackagedBinary) {
        ec.clear();
        fs::remove(packageBinaryPath, ec);
        if (ec && args.verbose) {
            std::cerr << "warning: failed to remove temporary packaged binary '"
                      << packageBinaryPath << "': " << ec.message() << "\n";
        }
    }

    std::cerr << "Package created: " << args.outputPath;
    ec.clear();
    if (args.verbose && fs::exists(args.outputPath, ec)) {
        ec.clear();
        auto finalPackageSize = fs::file_size(args.outputPath, ec);
        if (!ec)
            std::cerr << " (" << finalPackageSize << " bytes)";
    }
    std::cerr << "\n";
    return 0;
}
