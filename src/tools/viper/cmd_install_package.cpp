//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "cli.hpp"

#include "common/RunProcess.hpp"
#include "tools/common/packaging/LinuxPackageBuilder.hpp"
#include "tools/common/packaging/MacOSPackageBuilder.hpp"
#include "tools/common/packaging/PkgUtils.hpp"
#include "tools/common/packaging/PkgVerify.hpp"
#include "tools/common/packaging/ToolchainInstallManifest.hpp"
#include "tools/common/packaging/WindowsPackageBuilder.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

enum class InstallPackageTarget { Windows, MacOS, LinuxDeb, LinuxRpm, Tarball, All };

/// @brief Parsed command-line arguments for the `viper install-package` subcommand.
/// @details Selects the output format(s) and source (staged tree or build dir),
///          plus the macOS/Windows signing and Windows installer behavior options.
struct InstallPackageArgs {
    InstallPackageTarget target{InstallPackageTarget::All};
    std::string archOverride;
    fs::path stageDir;
    fs::path buildDir;
    std::string buildConfig;
    fs::path outputPath;
    fs::path verifyOnlyPath;
    std::string macosPackageVersion;
    std::string macosSignIdentity;
    std::string macosNotaryProfile;
    bool macosStaple{false};
    bool windowsSign{false};
    std::string windowsSignPfx;
    std::string windowsSignThumbprint;
    std::string windowsTimestampUrl;
    std::string windowsSigntoolPath;
    bool windowsSignNoVerify{false};
    std::string windowsInstallScope{"user"};
    std::string windowsInstallDir{"Viper"};
    bool windowsAddToPath{true};
    bool windowsFileAssociations{false};
    bool windowsShortcuts{true};
    bool allowDebugToolchain{false};
    bool noVerify{false};
    bool skipBuild{false};
    bool verbose{false};
    bool keepStageDir{false};
    bool stageOnly{false};
};

/// @brief Platform/architecture detected from a staged native executable.
struct NativeExecutableInfo {
    std::string platform; ///< "windows"/"macos"/"linux".
    std::string arch;     ///< "x64"/"arm64".
};

/// @brief Print usage for the `viper install-package` subcommand to stderr.
void installPackageUsage() {
    std::cerr
        << "Usage: viper install-package [options]\n"
        << "\n"
        << "  Package the Viper toolchain from a staged install tree.\n"
        << "\n"
        << "Options:\n"
        << "  --target <fmt>        windows | macos | linux-deb | linux-rpm | tarball | all\n"
        << "  --arch <arch>         x64 | arm64 (default: manifest/host)\n"
        << "  --stage-dir <dir>     Existing staged install tree\n"
        << "  --build-dir <dir>     Build tree; runs cmake --build then cmake --install\n"
        << "  --config <cfg>        Build configuration for cmake --install from a build tree\n"
        << "  --verify-only <path>  Verify an existing artifact and exit\n"
        << "  --macos-pkg-version <v> Dotted numeric package version override\n"
        << "  --macos-sign-identity <id> Developer ID Installer identity for macOS .pkg signing\n"
        << "  --macos-notary-profile <profile> notarytool keychain profile for macOS .pkg "
           "notarization\n"
        << "  --macos-staple       Staple the notarization ticket after successful submission\n"
        << "  --windows-sign        Authenticode-sign generated Windows installer\n"
        << "  --windows-sign-pfx <path> PFX certificate for Windows signing\n"
        << "  --windows-sign-thumbprint <sha1> Certificate store SHA-1 thumbprint\n"
        << "  --windows-timestamp-url <url> RFC3161 timestamp URL for Windows signing\n"
        << "  --windows-signtool <path> signtool.exe path override\n"
        << "  --windows-sign-no-verify Skip signtool verify after signing\n"
        << "  --windows-install-scope <scope> user | machine (default: user)\n"
        << "  --windows-install-dir <name> Directory name under install root (default: Viper)\n"
        << "  --windows-no-path    Do not add bin/ to PATH\n"
        << "  --windows-file-associations on|off Register .zia/.bas/.il (default: off)\n"
        << "  --windows-shortcuts on|off Create Start Menu developer shortcuts (default: on)\n"
        << "  --allow-debug-toolchain Allow Windows packages that reference MSVC debug CRTs\n"
        << "  --stage-only          Validate/gather the staged install tree and stop\n"
        << "  --skip-build          With --build-dir, run cmake --install without rebuilding "
           "first\n"
        << "  --keep-stage-dir      Preserve auto-generated stage directories\n"
        << "  -o <path>             Output file or directory\n"
        << "  --no-verify           Skip post-build verification\n"
        << "  --verbose, -v         Verbose output\n"
        << "  --help, -h            Show this help\n";
}

/// @brief Return the host platform name ("macos"/"windows"/"linux").
std::string hostPlatformName() {
#if defined(__APPLE__)
    return "macos";
#elif defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
}

/// @brief Map a Viper arch ("x64"/"arm64") to its Debian architecture name.
std::string debArchFor(const std::string &arch) {
    viper::pkg::validateToolchainArchitecture(arch);
    return arch == "arm64" ? "arm64" : "amd64";
}

/// @brief Map a Viper arch ("x64"/"arm64") to its RPM architecture name.
std::string rpmArchFor(const std::string &arch) {
    viper::pkg::validateToolchainArchitecture(arch);
    return arch == "arm64" ? "aarch64" : "x86_64";
}

/// @brief Return true if the `rpmbuild` tool is available on PATH.
bool rpmbuildAvailable() {
    const RunResult rr = run_process({"rpmbuild", "--version"});
    return rr.exit_code == 0;
}

/// @brief Read an environment variable, returning "" when it is unset.
std::string getenvOrEmpty(const char *name) {
    const char *value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
}

/// @brief Return true if the args request Windows Authenticode signing.
bool windowsSigningRequested(const InstallPackageArgs &args) {
    return args.windowsSign || !args.windowsSignPfx.empty() || !args.windowsSignThumbprint.empty();
}

/// @brief Return true if the args request macOS package signing/notarization.
bool macOSPackageSigningRequested(const InstallPackageArgs &args) {
    return !args.macosSignIdentity.empty() || !args.macosNotaryProfile.empty() || args.macosStaple;
}

/// @brief Authenticode-sign a Windows installer artifact when signing is requested.
/// @details Resolves the PFX/thumbprint (falling back to VIPER_WINDOWS_SIGN_*
///          env vars), invokes signtool, and optionally verifies the signature.
/// @return true on success or when no signing was requested; false on failure.
bool signWindowsInstallerArtifact(const InstallPackageArgs &args,
                                  const fs::path &artifactPath,
                                  std::ostream &err) {
    if (!windowsSigningRequested(args))
        return true;

    std::string pfxPath =
        args.windowsSignPfx.empty() ? getenvOrEmpty("VIPER_WINDOWS_SIGN_PFX") : args.windowsSignPfx;
    std::string thumbprint = args.windowsSignThumbprint.empty()
                                 ? getenvOrEmpty("VIPER_WINDOWS_SIGN_THUMBPRINT")
                                 : args.windowsSignThumbprint;
    try {
        thumbprint = viper::pkg::normalizeWindowsCertificateThumbprint(
            thumbprint, "Windows signing thumbprint");
    } catch (const std::exception &ex) {
        err << "error: " << ex.what() << "\n";
        return false;
    }
    if (pfxPath.empty() && thumbprint.empty()) {
        err << "error: Windows signing requested but no PFX or certificate thumbprint was "
               "provided\n";
        return false;
    }

    std::vector<std::string> signCmd;
    std::string signtool = args.windowsSigntoolPath.empty()
                               ? getenvOrEmpty("VIPER_WINDOWS_SIGNTOOL")
                               : args.windowsSigntoolPath;
    if (signtool.empty())
        signtool = "signtool.exe";
    std::string timestampUrl = args.windowsTimestampUrl.empty()
                                   ? getenvOrEmpty("VIPER_WINDOWS_TIMESTAMP_URL")
                                   : args.windowsTimestampUrl;
    if (timestampUrl.empty())
        timestampUrl = "https://timestamp.digicert.com";
    try {
        viper::pkg::validatePackageUrl(timestampUrl, "Windows timestamp URL");
    } catch (const std::exception &ex) {
        err << "error: " << ex.what() << "\n";
        return false;
    }

    signCmd = {signtool, "sign", "/fd", "SHA256", "/tr", timestampUrl, "/td", "SHA256"};
    if (!thumbprint.empty()) {
        signCmd.push_back("/sha1");
        signCmd.push_back(thumbprint);
    } else {
        if (!fs::is_regular_file(pfxPath)) {
            err << "error: Windows signing PFX not found: " << pfxPath << "\n";
            return false;
        }
        const std::string password = getenvOrEmpty("VIPER_WINDOWS_SIGN_PASSWORD");
        if (password.empty()) {
            err << "error: Windows PFX signing requires VIPER_WINDOWS_SIGN_PASSWORD\n";
            return false;
        }
        signCmd.push_back("/f");
        signCmd.push_back(pfxPath);
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
    if (!args.windowsSignNoVerify) {
        const RunResult verifyResult =
            run_process({signtool, "verify", "/pa", "/all", artifactPath.string()});
        if (verifyResult.exit_code != 0) {
            err << "error: signtool verify failed with exit code " << verifyResult.exit_code << "\n"
                << verifyResult.out << verifyResult.err;
            return false;
        }
    }
    return true;
}

/// @brief Sign (and optionally notarize/staple) a macOS .pkg when requested.
/// @details Resolves the Developer ID Installer identity and notary profile
///          (falling back to VIPER_MACOS_* env vars), runs productsign, verifies
///          with pkgutil, and — when a notary profile is set — submits via
///          notarytool and optionally staples. Only available on macOS hosts.
/// @return true on success or when no signing was requested; false on failure.
bool signMacOSPackageArtifact(const InstallPackageArgs &args,
                              const fs::path &artifactPath,
                              std::ostream &err) {
    if (!macOSPackageSigningRequested(args))
        return true;

    std::string identity = args.macosSignIdentity.empty()
                               ? getenvOrEmpty("VIPER_MACOS_SIGN_IDENTITY")
                               : args.macosSignIdentity;
    std::string notaryProfile = args.macosNotaryProfile.empty()
                                    ? getenvOrEmpty("VIPER_MACOS_NOTARY_PROFILE")
                                    : args.macosNotaryProfile;
    try {
        viper::pkg::validateSingleLineField(identity, "macOS package signing identity");
        viper::pkg::validateSingleLineField(notaryProfile, "macOS notary profile");
    } catch (const std::exception &ex) {
        err << "error: " << ex.what() << "\n";
        return false;
    }
    if (identity.empty()) {
        err << "error: macOS package signing requested but no Developer ID Installer identity "
               "was provided\n";
        return false;
    }
    if (args.macosStaple && notaryProfile.empty()) {
        err << "error: --macos-staple requires --macos-notary-profile or "
               "VIPER_MACOS_NOTARY_PROFILE\n";
        return false;
    }

#if !defined(__APPLE__)
    err << "error: macOS package signing requires running on macOS\n";
    (void)artifactPath;
    return false;
#else
    const fs::path signedPath = artifactPath.string() + ".signed.tmp";
    std::error_code ec;
    fs::remove(signedPath, ec);
    const RunResult signResult = run_process(
        {"productsign", "--sign", identity, artifactPath.string(), signedPath.string()});
    if (signResult.exit_code != 0) {
        err << "error: productsign failed with exit code " << signResult.exit_code << "\n"
            << signResult.out << signResult.err;
        fs::remove(signedPath, ec);
        return false;
    }
    fs::rename(signedPath, artifactPath, ec);
    if (ec) {
        err << "error: cannot replace unsigned macOS package with signed artifact: " << ec.message()
            << "\n";
        fs::remove(signedPath, ec);
        return false;
    }

    const RunResult verifyResult =
        run_process({"pkgutil", "--check-signature", artifactPath.string()});
    if (verifyResult.exit_code != 0) {
        err << "error: pkgutil --check-signature failed for signed macOS package\n"
            << verifyResult.out << verifyResult.err;
        return false;
    }

    if (!notaryProfile.empty()) {
        const RunResult notaryResult = run_process({"xcrun",
                                                    "notarytool",
                                                    "submit",
                                                    artifactPath.string(),
                                                    "--keychain-profile",
                                                    notaryProfile,
                                                    "--wait"});
        if (notaryResult.exit_code != 0) {
            err << "error: notarytool submit failed with exit code " << notaryResult.exit_code
                << "\n"
                << notaryResult.out << notaryResult.err;
            return false;
        }
        if (args.macosStaple) {
            const RunResult stapleResult =
                run_process({"xcrun", "stapler", "staple", artifactPath.string()});
            if (stapleResult.exit_code != 0) {
                err << "error: stapler failed with exit code " << stapleResult.exit_code << "\n"
                    << stapleResult.out << stapleResult.err;
                return false;
            }
        }
    }
    return true;
#endif
}

/// @brief Read a big-endian uint16 at @p off (caller must bounds-check).
uint16_t readBE16(const std::vector<uint8_t> &data, size_t off) {
    return static_cast<uint16_t>((data[off] << 8) | data[off + 1]);
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

/// @brief Return an ASCII-lowercased copy of @p text.
std::string lowerAscii(std::string text) {
    for (char &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/// @brief Return the lowercased filename with any trailing ".exe" stripped.
std::string binaryBaseName(std::string filename) {
    filename = lowerAscii(std::move(filename));
    if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".exe")
        filename.resize(filename.size() - 4);
    return filename;
}

/// @brief Sanitize a version string into a portable filename component.
/// @details Keeps alphanumerics and `.+~-`, replaces anything else with `_`, and
///          falls back to "0.0.0" when the result would be empty.
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
    return out.empty() ? "0.0.0" : out;
}

/// @brief Detect the platform/arch of a native executable from its header magic.
/// @details Recognises ELF, PE (MZ/PE), and Mach-O (thin and fat/universal),
///          decoding the machine/cputype field into platform+arch.
/// @return The detected info, or std::nullopt if the format is unrecognized.
std::optional<NativeExecutableInfo> detectNativeExecutableInfo(const fs::path &path) {
    const auto data = viper::pkg::readFile(path.string());
    if (data.size() < 20)
        return std::nullopt;

    if (data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
        if (data[4] != 2 || data[5] != 1)
            return std::nullopt;
        const uint16_t machine = readLE16(data, 18);
        if (machine == 62)
            return NativeExecutableInfo{"linux", "x64"};
        if (machine == 183)
            return NativeExecutableInfo{"linux", "arm64"};
        return std::nullopt;
    }

    if (data[0] == 'M' && data[1] == 'Z') {
        if (data.size() < 64)
            return std::nullopt;
        const size_t peOff = readLE32(data, 60);
        if (peOff > data.size() || data.size() - peOff < 24 || data[peOff] != 'P' ||
            data[peOff + 1] != 'E' || data[peOff + 2] != 0 || data[peOff + 3] != 0)
            return std::nullopt;
        const uint16_t machine = readLE16(data, peOff + 4);
        if (machine == 0x8664)
            return NativeExecutableInfo{"windows", "x64"};
        if (machine == 0xAA64)
            return NativeExecutableInfo{"windows", "arm64"};
        return std::nullopt;
    }

    const uint32_t magicBE = readBE32(data, 0);
    const uint32_t magicLE = readLE32(data, 0);
    if (magicLE == 0xFEEDFACF || magicBE == 0xFEEDFACF) {
        const bool little = magicLE == 0xFEEDFACF;
        const uint32_t cputype = little ? readLE32(data, 4) : readBE32(data, 4);
        if (cputype == 0x01000007u)
            return NativeExecutableInfo{"macos", "x64"};
        if (cputype == 0x0100000Cu)
            return NativeExecutableInfo{"macos", "arm64"};
        return std::nullopt;
    }
    if ((magicBE == 0xCAFEBABEu || magicBE == 0xCAFEBABFu) && data.size() >= 8) {
        const uint32_t count = readBE32(data, 4);
        const size_t archSize = magicBE == 0xCAFEBABFu ? 32u : 20u;
        if (count == 0 || count > 64 || data.size() < 8 + static_cast<size_t>(count) * archSize)
            return std::nullopt;
        bool hasX64 = false;
        bool hasArm64 = false;
        for (uint32_t i = 0; i < count; ++i) {
            const size_t off = 8 + static_cast<size_t>(i) * archSize;
            const uint32_t cputype = readBE32(data, off);
            hasX64 = hasX64 || cputype == 0x01000007u;
            hasArm64 = hasArm64 || cputype == 0x0100000Cu;
        }
        if (hasX64 && hasArm64)
            return NativeExecutableInfo{"macos", "universal"};
        if (hasX64)
            return NativeExecutableInfo{"macos", "x64"};
        if (hasArm64)
            return NativeExecutableInfo{"macos", "arm64"};
    }
    return std::nullopt;
}

/// @brief Detect the platform/arch from the manifest's staged `viper` binary.
/// @return The detected info, or std::nullopt when no usable binary is found.
std::optional<NativeExecutableInfo> detectManifestToolchainExecutableInfo(
    const viper::pkg::ToolchainInstallManifest &manifest) {
    for (const auto &file : manifest.files) {
        if (file.kind != viper::pkg::ToolchainFileKind::Binary)
            continue;
        if (binaryBaseName(file.stagedAbsolutePath.filename().string()) != "viper")
            continue;
        return detectNativeExecutableInfo(file.stagedAbsolutePath);
    }
    return std::nullopt;
}

/// @brief Find the first staged PE that imports an MSVC debug CRT, if any.
/// @details Scans each staged .exe/.dll's import table for the known debug-runtime
///          DLLs; used to reject (or warn about) non-redistributable debug builds.
/// @return A "<path> imports <dll>" description, or std::nullopt when none found.
std::optional<std::string> firstWindowsDebugRuntimeReference(
    const viper::pkg::ToolchainInstallManifest &manifest) {
    static const char *debugDlls[] = {
        "ucrtbased.dll", "vcruntime140d.dll", "vcruntime140_1d.dll", "msvcp140d.dll"};
    for (const auto &file : manifest.files) {
        if (file.symlink)
            continue;
        const std::string ext = lowerAscii(file.stagedAbsolutePath.extension().string());
        if (ext != ".exe" && ext != ".dll")
            continue;
        const auto data = viper::pkg::readFile(file.stagedAbsolutePath.string());
        const auto imports = viper::pkg::importedDllNamesFromPe(data);
        for (const char *dll : debugDlls) {
            if (std::find(imports.begin(), imports.end(), dll) != imports.end())
                return file.stagedRelativePath + " imports " + dll;
        }
    }
    return std::nullopt;
}

/// @brief Parse a --target value into an InstallPackageTarget.
/// @return true on a recognized target name; false otherwise.
bool parseTarget(const std::string &text, InstallPackageTarget &out) {
    if (text == "windows")
        out = InstallPackageTarget::Windows;
    else if (text == "macos")
        out = InstallPackageTarget::MacOS;
    else if (text == "linux-deb")
        out = InstallPackageTarget::LinuxDeb;
    else if (text == "linux-rpm")
        out = InstallPackageTarget::LinuxRpm;
    else if (text == "tarball")
        out = InstallPackageTarget::Tarball;
    else if (text == "all")
        out = InstallPackageTarget::All;
    else
        return false;
    return true;
}

/// @brief Parse an on/off-style boolean option value.
/// @return true on a recognized on/off/true/false/1/0/yes/no token; false otherwise.
bool parseOnOff(const std::string &text, bool &out) {
    if (text == "on" || text == "true" || text == "1" || text == "yes") {
        out = true;
        return true;
    }
    if (text == "off" || text == "false" || text == "0" || text == "no") {
        out = false;
        return true;
    }
    return false;
}

/// @brief Return true if @p arg is an install-package option that takes a value.
/// @details Used during argument parsing to know when to consume the next token.
bool installPackageOptionRequiresValue(const std::string &arg) {
    return arg == "--target" || arg == "--arch" || arg == "--stage-dir" || arg == "--build-dir" ||
           arg == "--config" || arg == "--verify-only" || arg == "--macos-pkg-version" ||
           arg == "--macos-sign-identity" || arg == "--macos-notary-profile" ||
           arg == "--windows-sign-pfx" || arg == "--windows-sign-thumbprint" ||
           arg == "--windows-timestamp-url" || arg == "--windows-signtool" ||
           arg == "--windows-install-scope" || arg == "--windows-install-dir" ||
           arg == "--windows-file-associations" || arg == "--windows-shortcuts" || arg == "-o";
}

/// @brief Parse the `viper install-package` command line into @p args.
/// @details Handles the target/source options and the macOS/Windows signing and
///          installer options; prints usage and returns false on a malformed or
///          missing-value argument.
/// @return true on a successful parse.
bool parseInstallPackageArgs(int argc, char **argv, InstallPackageArgs &args) {
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--help" || arg == "-h")) {
            installPackageUsage();
            return false;
        }
        if (installPackageOptionRequiresValue(arg) && i + 1 >= argc) {
            std::cerr << "error: " << arg << " requires a value\n";
            return false;
        } else if (arg == "--target" && i + 1 < argc) {
            if (!parseTarget(argv[++i], args.target)) {
                std::cerr << "error: unknown install-package target '" << argv[i] << "'\n";
                return false;
            }
        } else if (arg == "--arch" && i + 1 < argc) {
            args.archOverride = argv[++i];
            if (args.archOverride != "x64" && args.archOverride != "arm64") {
                std::cerr << "error: unknown arch '" << args.archOverride
                          << "'; expected x64 or arm64\n";
                return false;
            }
        } else if (arg == "--stage-dir" && i + 1 < argc) {
            args.stageDir = argv[++i];
        } else if (arg == "--build-dir" && i + 1 < argc) {
            args.buildDir = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            args.buildConfig = argv[++i];
        } else if (arg == "--verify-only" && i + 1 < argc) {
            args.verifyOnlyPath = argv[++i];
        } else if (arg == "--macos-pkg-version" && i + 1 < argc) {
            args.macosPackageVersion = argv[++i];
            try {
                viper::pkg::validateDottedNumericVersion(args.macosPackageVersion,
                                                         "macOS package version override");
            } catch (const std::exception &ex) {
                std::cerr << "error: " << ex.what() << "\n";
                return false;
            }
        } else if (arg == "--macos-sign-identity" && i + 1 < argc) {
            args.macosSignIdentity = argv[++i];
        } else if (arg == "--macos-notary-profile" && i + 1 < argc) {
            args.macosNotaryProfile = argv[++i];
        } else if (arg == "--macos-staple") {
            args.macosStaple = true;
        } else if (arg == "--windows-sign") {
            args.windowsSign = true;
        } else if (arg == "--windows-sign-pfx" && i + 1 < argc) {
            args.windowsSignPfx = argv[++i];
        } else if (arg == "--windows-sign-thumbprint" && i + 1 < argc) {
            args.windowsSignThumbprint = argv[++i];
        } else if (arg == "--windows-timestamp-url" && i + 1 < argc) {
            args.windowsTimestampUrl = argv[++i];
        } else if (arg == "--windows-signtool" && i + 1 < argc) {
            args.windowsSigntoolPath = argv[++i];
        } else if (arg == "--windows-sign-no-verify") {
            args.windowsSignNoVerify = true;
        } else if (arg == "--windows-install-scope" && i + 1 < argc) {
            args.windowsInstallScope = argv[++i];
            if (args.windowsInstallScope != "user" && args.windowsInstallScope != "machine") {
                std::cerr << "error: --windows-install-scope expects user or machine\n";
                return false;
            }
        } else if (arg == "--windows-install-dir" && i + 1 < argc) {
            args.windowsInstallDir = argv[++i];
        } else if (arg == "--windows-no-path") {
            args.windowsAddToPath = false;
        } else if (arg == "--windows-file-associations" && i + 1 < argc) {
            if (!parseOnOff(lowerAscii(argv[++i]), args.windowsFileAssociations)) {
                std::cerr << "error: --windows-file-associations expects on or off\n";
                return false;
            }
        } else if (arg == "--windows-shortcuts" && i + 1 < argc) {
            if (!parseOnOff(lowerAscii(argv[++i]), args.windowsShortcuts)) {
                std::cerr << "error: --windows-shortcuts expects on or off\n";
                return false;
            }
        } else if (arg == "--allow-debug-toolchain") {
            args.allowDebugToolchain = true;
        } else if (arg == "-o" && i + 1 < argc) {
            args.outputPath = argv[++i];
        } else if (arg == "--no-verify") {
            args.noVerify = true;
        } else if (arg == "--skip-build") {
            args.skipBuild = true;
        } else if (arg == "--verbose" || arg == "-v") {
            args.verbose = true;
        } else if (arg == "--keep-stage-dir") {
            args.keepStageDir = true;
        } else if (arg == "--stage-only") {
            args.stageOnly = true;
        } else {
            std::cerr << "error: unknown option '" << arg << "'\n";
            return false;
        }
    }

    int modeCount = 0;
    if (!args.stageDir.empty())
        ++modeCount;
    if (!args.buildDir.empty())
        ++modeCount;
    if (!args.verifyOnlyPath.empty())
        ++modeCount;
    if (modeCount != 1) {
        std::cerr << "error: require exactly one of --stage-dir, --build-dir, or --verify-only\n";
        return false;
    }
    if (!args.buildDir.empty() && args.buildConfig.empty() && hostPlatformName() == "windows")
        args.buildConfig = "Release";
    try {
        viper::pkg::validatePackageUrl(args.windowsTimestampUrl, "Windows timestamp URL");
        viper::pkg::validateSingleLineField(args.windowsSigntoolPath, "Windows signtool path");
        viper::pkg::validateWindowsFileName(args.windowsInstallDir, "Windows install directory");
        viper::pkg::validateWindowsCertificateThumbprint(args.windowsSignThumbprint,
                                                         "Windows signing thumbprint");
        viper::pkg::validateSingleLineField(args.macosSignIdentity,
                                            "macOS package signing identity");
        viper::pkg::validateSingleLineField(args.macosNotaryProfile, "macOS notary profile");
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return false;
    }
    return true;
}

/// @brief Build the conventional output filename for a target from the manifest.
/// @details Encodes the version/arch/platform per platform naming convention
///          (e.g. `viper_<v>_<arch>.deb`); returns "" for the All meta-target.
std::string targetFileName(InstallPackageTarget target,
                           const viper::pkg::ToolchainInstallManifest &manifest) {
    const std::string version = manifest.version.empty() ? "0.0.0" : manifest.version;
    const std::string arch = manifest.arch.empty() ? "x64" : manifest.arch;
    const std::string platform = manifest.platform.empty() ? hostPlatformName() : manifest.platform;
    switch (target) {
        case InstallPackageTarget::Windows:
            return "viper-" + version + "-win-" + arch + ".exe";
        case InstallPackageTarget::MacOS:
            return "viper-" + version + "-macos-" + arch + ".pkg";
        case InstallPackageTarget::LinuxDeb:
            return "viper_" + version + "_" + debArchFor(arch) + ".deb";
        case InstallPackageTarget::LinuxRpm:
            return "viper-" + version + "-1." + rpmArchFor(arch) + ".rpm";
        case InstallPackageTarget::Tarball:
            return "viper-" + version + "-" + platform + "-" + arch + ".tar.gz";
        case InstallPackageTarget::All:
        default:
            return {};
    }
}

/// @brief Compute the payload paths a built package of @p target must contain.
/// @details Derives the expected install-relative paths from the manifest files,
///          adding the platform-specific layout prefixes and any file-association
///          metadata (.desktop/MIME entries). Used by post-build verification.
/// @return The list of required payload paths for @p target.
std::vector<std::string> requiredPayloadPaths(
    InstallPackageTarget target, const viper::pkg::ToolchainInstallManifest &manifest) {
    std::vector<std::string> paths;
    paths.reserve(manifest.files.size() + 1);
    auto appendLinuxAssociationMetadata = [&](const std::string &prefix, bool portable) {
        if (manifest.fileAssociations.empty())
            return;
        bool hasSource = false;
        bool hasIl = false;
        for (const auto &assoc : manifest.fileAssociations) {
            std::string ext = assoc.extension;
            for (char &c : ext)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext == ".il")
                hasIl = true;
            else
                hasSource = true;
        }
        const std::string appDir = portable ? "share/applications/" : "usr/share/applications/";
        const std::string mimeDir = portable ? "share/mime/packages/" : "usr/share/mime/packages/";
        if (hasSource)
            paths.push_back(prefix + appDir + "viper-source.desktop");
        if (hasIl)
            paths.push_back(prefix + appDir + "viper-il.desktop");
        paths.push_back(prefix + mimeDir + "viper.xml");
    };
    switch (target) {
        case InstallPackageTarget::Windows:
            for (const auto &file : manifest.files) {
                paths.push_back(viper::pkg::sanitizePackageRelativePath(file.stagedRelativePath,
                                                                        "windows toolchain path"));
            }
            paths.push_back("bin/viper-dev.cmd");
            paths.push_back("bin/viper-install-vscode-extension.cmd");
            paths.push_back("share/viper/README.windows-prerequisites.txt");
            paths.push_back("uninstall.exe");
            paths.push_back(".viper-install-manifest.txt");
            break;
        case InstallPackageTarget::LinuxDeb:
        case InstallPackageTarget::LinuxRpm:
            for (const auto &file : manifest.files) {
                const std::string installPath =
                    viper::pkg::mapInstallPath(file, viper::pkg::InstallPathPolicy::LinuxUsrRoot);
                paths.push_back(viper::pkg::sanitizePackageRelativePath(
                    installPath.size() > 1 ? installPath.substr(1) : installPath,
                    "linux install path"));
            }
            appendLinuxAssociationMetadata("", false);
            break;
        case InstallPackageTarget::Tarball: {
            const std::string version = manifest.version.empty() ? "0.0.0" : manifest.version;
            const std::string packageName = "viper";
            const std::string topDir = viper::pkg::sanitizePackageRelativePath(
                packageName + "-" + portableArchiveVersionComponent(version) + "-" +
                    manifest.platform + "-" + manifest.arch,
                "toolchain tarball top-level directory");
            for (const auto &file : manifest.files) {
                paths.push_back(topDir + "/" +
                                viper::pkg::mapInstallPath(
                                    file, viper::pkg::InstallPathPolicy::PortableArchive));
            }
            if (manifest.platform == "linux") {
                appendLinuxAssociationMetadata(topDir + "/", true);
                paths.push_back(topDir + "/share/viper/install_manifest.txt");
                paths.push_back(topDir + "/install.sh");
                paths.push_back(topDir + "/uninstall.sh");
                paths.push_back(topDir + "/README.install");
            }
            break;
        }
        case InstallPackageTarget::MacOS:
            for (const auto &file : manifest.files) {
                const std::string rel = viper::pkg::sanitizePackageRelativePath(
                    file.stagedRelativePath, "macOS toolchain path");
                paths.push_back("usr/local/viper/" + rel);
                if (file.kind == viper::pkg::ToolchainFileKind::Binary ||
                    rel.rfind("bin/", 0) == 0) {
                    paths.push_back("usr/local/bin/" + fs::path(rel).filename().generic_string());
                } else if (file.kind == viper::pkg::ToolchainFileKind::ManPage ||
                           rel.rfind("share/man/", 0) == 0) {
                    static constexpr std::string_view kManPrefix = "share/man/";
                    if (rel.rfind(kManPrefix, 0) == 0) {
                        paths.push_back("usr/local/share/man/" + rel.substr(kManPrefix.size()));
                    }
                }
            }
            paths.push_back("usr/local/viper/share/viper/install_manifest.txt");
            paths.push_back("usr/local/viper/share/viper/uninstall.sh");
            paths.push_back("usr/local/lib/cmake/Viper/ViperConfig.cmake");
            paths.push_back("usr/local/lib/cmake/Viper/ViperConfigVersion.cmake");
            if (!manifest.fileAssociations.empty()) {
                paths.push_back("Applications/Viper Toolchain.app/Contents/Info.plist");
                paths.push_back("Applications/Viper Toolchain.app/Contents/PkgInfo");
                paths.push_back(
                    "Applications/Viper Toolchain.app/Contents/MacOS/viper-file-handler");
            }
            break;
        case InstallPackageTarget::All:
            break;
    }
    return paths;
}

/// @brief Verify every @p required path (leading slashes stripped) is in @p actual.
/// @param actual Set of payload paths actually present in the built artifact.
/// @param required Paths that must be present.
/// @param kind Artifact-kind label for diagnostics.
/// @param err Stream for error messages.
/// @return true when all required paths are present.
bool requireListedPayloadPaths(const std::set<std::string> &actual,
                               const std::vector<std::string> &required,
                               const char *kind,
                               std::ostream &err) {
    for (const auto &path : required) {
        std::string normalized = path;
        while (!normalized.empty() && normalized.front() == '/')
            normalized.erase(normalized.begin());
        if (actual.find(normalized) == actual.end()) {
            err << kind << ": missing required payload path " << normalized << "\n";
            return false;
        }
    }
    return true;
}

/// @brief Parse a newline-separated payload listing into a normalized path set.
/// @details Trims CR/LF, strips leading slashes and a leading "./", and skips
///          blank lines.
std::set<std::string> parsePayloadListing(const std::string &text) {
    std::set<std::string> paths;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        while (!line.empty() && line.front() == '/')
            line.erase(line.begin());
        if (line.rfind("./", 0) == 0)
            line.erase(0, 2);
        if (!line.empty())
            paths.insert(line);
    }
    return paths;
}

/// @brief One index entry in an RPM header section.
struct RpmHeaderEntry {
    uint32_t tag{0};    ///< RPM tag id.
    uint32_t type{0};   ///< Value type code.
    uint32_t offset{0}; ///< Offset into the header's data store.
    uint32_t count{0};  ///< Number of values.
};

/// @brief A parsed RPM header: its index entries and data-store bounds.
struct RpmHeaderView {
    size_t storeOffset{0};               ///< File offset of the data store.
    size_t storeSize{0};                 ///< Size of the data store in bytes.
    size_t endOffset{0};                 ///< File offset just past this header.
    std::vector<RpmHeaderEntry> entries; ///< Index entries describing the values.
};

/// @brief Parse an RPM header section at @p offset into @p header.
/// @details Validates the header magic and index/store sizes; used to read the
///          file listing out of a generated .rpm for verification.
/// @param data Whole .rpm file bytes.
/// @param offset File offset where the header begins.
/// @param name Header name for diagnostics (e.g. "signature", "main").
/// @param header Output parsed header view.
/// @param err Stream for error messages.
/// @return true when the header parses successfully.
bool parseRpmHeaderAt(const std::vector<uint8_t> &data,
                      size_t offset,
                      const char *name,
                      RpmHeaderView &header,
                      std::ostream &err) {
    if (offset + 16 > data.size()) {
        err << "rpm: missing " << name << " header\n";
        return false;
    }
    if (data[offset] != 0x8E || data[offset + 1] != 0xAD || data[offset + 2] != 0xE8 ||
        data[offset + 3] != 0x01) {
        err << "rpm: missing " << name << " header magic\n";
        return false;
    }
    const uint32_t indexCount = readBE32(data, offset + 8);
    const uint32_t storeSize = readBE32(data, offset + 12);
    const size_t entriesOffset = offset + 16;
    if (indexCount > (data.size() - entriesOffset) / 16u) {
        err << "rpm: " << name << " header index extends past end of file\n";
        return false;
    }
    const size_t storeOffset = entriesOffset + static_cast<size_t>(indexCount) * 16u;
    if (storeSize > data.size() - storeOffset) {
        err << "rpm: " << name << " header store extends past end of file\n";
        return false;
    }

    header.storeOffset = storeOffset;
    header.storeSize = storeSize;
    header.endOffset = storeOffset + storeSize;
    header.entries.clear();
    header.entries.reserve(indexCount);
    for (uint32_t i = 0; i < indexCount; ++i) {
        const size_t entryOffset = entriesOffset + static_cast<size_t>(i) * 16u;
        RpmHeaderEntry entry;
        entry.tag = readBE32(data, entryOffset);
        entry.type = readBE32(data, entryOffset + 4);
        entry.offset = readBE32(data, entryOffset + 8);
        entry.count = readBE32(data, entryOffset + 12);
        if (entry.offset > storeSize) {
            err << "rpm: " << name << " header tag " << entry.tag
                << " points past the header store\n";
            return false;
        }
        header.entries.push_back(entry);
    }
    return true;
}

/// @brief Find the header index entry with the given RPM @p tag.
/// @return Pointer to the entry, or nullptr if the tag is absent.
const RpmHeaderEntry *findRpmHeaderEntry(const RpmHeaderView &header, uint32_t tag) {
    auto it = std::find_if(header.entries.begin(), header.entries.end(), [&](const auto &entry) {
        return entry.tag == tag;
    });
    return it == header.entries.end() ? nullptr : &*it;
}

/// @brief Read a STRING/STRING_ARRAY/I18NSTRING RPM tag value into @p values.
/// @details Validates the entry type and reads NUL-terminated strings from the
///          header data store, bounds-checking against truncation.
/// @return true on success; false (with @p err set) on a type/bounds error.
bool readRpmStringArray(const std::vector<uint8_t> &data,
                        const RpmHeaderView &header,
                        const RpmHeaderEntry &entry,
                        std::vector<std::string> &values,
                        std::ostream &err) {
    static constexpr uint32_t kRpmString = 6;
    static constexpr uint32_t kRpmStringArray = 8;
    static constexpr uint32_t kRpmI18NString = 9;
    if (entry.type != kRpmString && entry.type != kRpmStringArray && entry.type != kRpmI18NString) {
        err << "rpm: header tag " << entry.tag << " is not a string array\n";
        return false;
    }
    const uint32_t expected = entry.type == kRpmString ? 1u : entry.count;
    size_t pos = header.storeOffset + entry.offset;
    const size_t end = header.storeOffset + header.storeSize;
    values.clear();
    values.reserve(expected);
    for (uint32_t i = 0; i < expected; ++i) {
        if (pos >= end) {
            err << "rpm: string array tag " << entry.tag << " is truncated\n";
            return false;
        }
        size_t nul = pos;
        while (nul < end && data[nul] != 0)
            ++nul;
        if (nul >= end) {
            err << "rpm: string array tag " << entry.tag << " is missing a terminator\n";
            return false;
        }
        values.emplace_back(reinterpret_cast<const char *>(data.data() + pos), nul - pos);
        pos = nul + 1;
    }
    return true;
}

/// @brief Read an INT32 RPM tag value array into @p values (big-endian).
/// @return true on success; false (with @p err set) on a type/bounds error.
bool readRpmInt32Array(const std::vector<uint8_t> &data,
                       const RpmHeaderView &header,
                       const RpmHeaderEntry &entry,
                       std::vector<uint32_t> &values,
                       std::ostream &err) {
    static constexpr uint32_t kRpmInt32 = 4;
    if (entry.type != kRpmInt32) {
        err << "rpm: header tag " << entry.tag << " is not an int32 array\n";
        return false;
    }
    if (entry.count > (header.storeSize - entry.offset) / 4u) {
        err << "rpm: int32 array tag " << entry.tag << " is truncated\n";
        return false;
    }
    values.clear();
    values.reserve(entry.count);
    size_t pos = header.storeOffset + entry.offset;
    for (uint32_t i = 0; i < entry.count; ++i) {
        values.push_back(readBE32(data, pos));
        pos += 4;
    }
    return true;
}

/// @brief Normalize an RPM-listed path (strip leading slashes and a leading "./").
std::string normalizeRpmListedPath(std::string path) {
    while (!path.empty() && path.front() == '/')
        path.erase(path.begin());
    if (path.rfind("./", 0) == 0)
        path.erase(0, 2);
    return path;
}

/// @brief Reconstruct the installed file paths recorded in an .rpm header.
/// @details Combines the BASENAMES, DIRINDEXES, and DIRNAMES tags into full
///          normalized paths for post-build payload verification.
/// @return true when the path tags were read and assembled successfully.
bool readRpmPayloadPaths(const std::vector<uint8_t> &data,
                         std::set<std::string> *payloadPaths,
                         std::ostream &err) {
    if (data.size() < 112 || data[0] != 0xED || data[1] != 0xAB || data[2] != 0xEE ||
        data[3] != 0xDB) {
        err << "rpm: missing lead magic\n";
        return false;
    }
    if (data[4] != 3) {
        err << "rpm: unsupported lead major version " << static_cast<int>(data[4]) << "\n";
        return false;
    }
    if (readBE16(data, 78) != 5) {
        err << "rpm: unsupported signature type\n";
        return false;
    }

    RpmHeaderView signature;
    if (!parseRpmHeaderAt(data, 96, "signature", signature, err))
        return false;
    const size_t mainOff = (signature.endOffset + 7ull) & ~7ull;
    RpmHeaderView mainHeader;
    if (!parseRpmHeaderAt(data, mainOff, "main", mainHeader, err))
        return false;
    if (mainHeader.endOffset == data.size()) {
        err << "rpm: package has no payload after main header\n";
        return false;
    }
    bool sawPayloadByte = false;
    for (size_t i = mainHeader.endOffset; i < data.size(); ++i) {
        if (data[i] != 0) {
            sawPayloadByte = true;
            break;
        }
    }
    if (!sawPayloadByte) {
        err << "rpm: package payload is empty\n";
        return false;
    }
    if (payloadPaths == nullptr)
        return true;

    static constexpr uint32_t kRpmTagOldFileNames = 1027;
    static constexpr uint32_t kRpmTagDirIndexes = 1116;
    static constexpr uint32_t kRpmTagBaseNames = 1117;
    static constexpr uint32_t kRpmTagDirNames = 1118;
    static constexpr uint32_t kRpmTagFileNames = 5000;

    payloadPaths->clear();
    std::vector<std::string> fullNames;
    if (const auto *fileNamesEntry = findRpmHeaderEntry(mainHeader, kRpmTagFileNames)) {
        if (!readRpmStringArray(data, mainHeader, *fileNamesEntry, fullNames, err))
            return false;
    } else if (const auto *oldFileNamesEntry =
                   findRpmHeaderEntry(mainHeader, kRpmTagOldFileNames)) {
        if (!readRpmStringArray(data, mainHeader, *oldFileNamesEntry, fullNames, err))
            return false;
    }
    if (!fullNames.empty()) {
        for (auto &path : fullNames) {
            path = normalizeRpmListedPath(std::move(path));
            if (!path.empty())
                payloadPaths->insert(path);
        }
        return true;
    }

    const auto *baseEntry = findRpmHeaderEntry(mainHeader, kRpmTagBaseNames);
    const auto *dirEntry = findRpmHeaderEntry(mainHeader, kRpmTagDirNames);
    const auto *indexEntry = findRpmHeaderEntry(mainHeader, kRpmTagDirIndexes);
    if (baseEntry == nullptr || dirEntry == nullptr || indexEntry == nullptr) {
        err << "rpm: main header does not contain file name tags\n";
        return false;
    }

    std::vector<std::string> basenames;
    std::vector<std::string> dirnames;
    std::vector<uint32_t> dirindexes;
    if (!readRpmStringArray(data, mainHeader, *baseEntry, basenames, err) ||
        !readRpmStringArray(data, mainHeader, *dirEntry, dirnames, err) ||
        !readRpmInt32Array(data, mainHeader, *indexEntry, dirindexes, err)) {
        return false;
    }
    if (basenames.size() != dirindexes.size()) {
        err << "rpm: basename and directory index counts differ\n";
        return false;
    }
    for (size_t i = 0; i < basenames.size(); ++i) {
        if (dirindexes[i] >= dirnames.size()) {
            err << "rpm: file directory index points past directory table\n";
            return false;
        }
        std::string path = normalizeRpmListedPath(dirnames[dirindexes[i]] + basenames[i]);
        if (!path.empty())
            payloadPaths->insert(path);
    }
    return true;
}

/// @brief Structurally verify a built package artifact for @p target.
/// @details Dispatches to the format-specific PkgVerify routine; when @p manifest
///          is provided, additionally asserts the required payload paths are
///          present (using the RPM header reader for .rpm).
/// @return true when the artifact is valid (and complete, if a manifest is given).
bool verifyArtifact(const fs::path &artifact,
                    InstallPackageTarget target,
                    std::ostream &err,
                    const viper::pkg::ToolchainInstallManifest *manifest = nullptr) {
    const auto data = viper::pkg::readFile(artifact.string());
    switch (target) {
        case InstallPackageTarget::Windows:
            if (manifest) {
                return viper::pkg::verifyPEZipOverlayNestedPayload(
                    data,
                    {"meta/payload.zip", "meta/install_manifest.next", "meta/manifest.sha256"},
                    "meta/payload.zip",
                    requiredPayloadPaths(target, *manifest),
                    err);
            }
            return viper::pkg::verifyPEZipOverlay(data, err);
        case InstallPackageTarget::LinuxDeb:
            if (manifest) {
                return viper::pkg::verifyDebPayload(
                    data, requiredPayloadPaths(target, *manifest), err);
            }
            return viper::pkg::verifyDeb(data, err);
        case InstallPackageTarget::Tarball:
            if (manifest) {
                return viper::pkg::verifyTarGzPayload(
                    data, requiredPayloadPaths(target, *manifest), err);
            }
            return viper::pkg::verifyTarGz(data, err);
        case InstallPackageTarget::MacOS:
            if (manifest)
                return viper::pkg::verifyMacOSPkgPayload(
                    data, requiredPayloadPaths(target, *manifest), err);
            return viper::pkg::verifyMacOSPkg(data, err);
        case InstallPackageTarget::LinuxRpm: {
            if (manifest) {
                std::set<std::string> payloadPaths;
                if (!readRpmPayloadPaths(data, &payloadPaths, err))
                    return false;
                return requireListedPayloadPaths(
                    payloadPaths, requiredPayloadPaths(target, *manifest), "rpm", err);
            }
            return readRpmPayloadPaths(data, nullptr, err);
        }
        case InstallPackageTarget::All:
        default:
            return false;
    }
}

/// @brief Infer the package target from an artifact's filename extension.
/// @details Maps .exe/.pkg/.deb/.rpm/.tar.gz/.tgz to their targets for
///          `--verify-only`. @return true on a recognized extension.
bool inferVerifyTargetFromPath(const fs::path &path, InstallPackageTarget &target) {
    const std::string name = lowerAscii(path.filename().string());
    if (name.size() >= 4 && name.substr(name.size() - 4) == ".exe")
        target = InstallPackageTarget::Windows;
    else if (name.size() >= 4 && name.substr(name.size() - 4) == ".pkg")
        target = InstallPackageTarget::MacOS;
    else if (name.size() >= 4 && name.substr(name.size() - 4) == ".deb")
        target = InstallPackageTarget::LinuxDeb;
    else if (name.size() >= 4 && name.substr(name.size() - 4) == ".rpm")
        target = InstallPackageTarget::LinuxRpm;
    else if ((name.size() >= 7 && name.substr(name.size() - 7) == ".tar.gz") ||
             (name.size() >= 4 && name.substr(name.size() - 4) == ".tgz"))
        target = InstallPackageTarget::Tarball;
    else
        return false;
    return true;
}

/// @brief RAII guard that removes an auto-created staging directory on scope exit.
/// @details Call dismiss() to keep the directory once staging succeeds; otherwise
///          the destructor recursively deletes it (used for auto-generated stages).
class AutoStageCleanup {
  public:
    /// @brief Construct a guard for @p path; cleanup runs only when @p enabled.
    AutoStageCleanup(fs::path path, bool enabled) : path_(std::move(path)), enabled_(enabled) {}

    /// @brief Remove the staging directory unless the guard was dismissed.
    ~AutoStageCleanup() {
        if (enabled_ && !path_.empty()) {
            std::error_code ec;
            fs::remove_all(path_, ec);
        }
    }

    /// @brief Cancel the pending cleanup so the directory is preserved.
    void dismiss() {
        enabled_ = false;
    }

  private:
    fs::path path_;       ///< Staging directory to remove.
    bool enabled_{false}; ///< Whether the destructor should delete @c path_.
};

/// @brief Return an existing staged install tree, or build/stage one on demand.
/// @details If --stage-dir was given, returns it directly. Otherwise creates a
///          unique directory under --build-dir, optionally runs `cmake --build`,
///          then `cmake --install --prefix <stage>`; the directory is removed on
///          failure (and on success unless --keep-stage-dir was set).
/// @throws std::runtime_error on directory creation or cmake failure.
fs::path ensureStageDir(const InstallPackageArgs &args) {
    if (!args.stageDir.empty())
        return args.stageDir;

    const auto pid =
#if defined(_WIN32)
        static_cast<unsigned long>(_getpid());
#else
        static_cast<unsigned long>(::getpid());
#endif
    const auto tick = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    fs::path stageDir;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        stageDir = args.buildDir / ("install-toolchain-stage-" + std::to_string(pid) + "-" +
                                    std::to_string(tick) + "-" + std::to_string(attempt));
        std::error_code ec;
        if (fs::create_directory(stageDir, ec))
            break;
        if (attempt == 99)
            throw std::runtime_error("cannot create a unique staging directory under " +
                                     args.buildDir.string());
    }
    AutoStageCleanup cleanup(stageDir, !args.keepStageDir);

    if (!args.skipBuild) {
        std::vector<std::string> buildCmd = {"cmake", "--build", args.buildDir.string()};
        if (!args.buildConfig.empty()) {
            buildCmd.push_back("--config");
            buildCmd.push_back(args.buildConfig);
        }
        const RunResult buildResult = run_process(buildCmd);
        if (buildResult.exit_code != 0) {
            throw std::runtime_error("cmake --build failed while preparing toolchain payload:\n" +
                                     buildResult.out + buildResult.err);
        }
    }

    std::vector<std::string> installCmd = {
        "cmake", "--install", args.buildDir.string(), "--prefix", stageDir.string()};
    if (!args.buildConfig.empty()) {
        installCmd.push_back("--config");
        installCmd.push_back(args.buildConfig);
    }

    const RunResult rr = run_process(installCmd);
    if (rr.exit_code != 0) {
        throw std::runtime_error("cmake --install failed while staging toolchain:\n" + rr.out +
                                 rr.err);
    }
    cleanup.dismiss();
    return stageDir;
}

/// @brief Return true if @p target is buildable for the staged @p platform.
/// @details Tarball matches any platform; native formats require the matching
///          platform string.
bool targetMatchesStagedPlatform(InstallPackageTarget target, const std::string &platform) {
    switch (target) {
        case InstallPackageTarget::Windows:
            return platform == "windows";
        case InstallPackageTarget::MacOS:
            return platform == "macos";
        case InstallPackageTarget::LinuxDeb:
        case InstallPackageTarget::LinuxRpm:
            return platform == "linux";
        case InstallPackageTarget::Tarball:
            return true;
        case InstallPackageTarget::All:
        default:
            return false;
    }
}

/// @brief Expand the requested target into the concrete formats to build.
/// @details A specific target maps to itself; the All meta-target expands to the
///          native format(s) for @p platform (deb+rpm on Linux) plus a tarball.
std::vector<InstallPackageTarget> selectedTargets(InstallPackageTarget target,
                                                  const std::string &platform) {
    if (target != InstallPackageTarget::All)
        return {target};

    std::vector<InstallPackageTarget> result;
    if (platform == "windows") {
        result.push_back(InstallPackageTarget::Windows);
    } else if (platform == "macos") {
        result.push_back(InstallPackageTarget::MacOS);
    } else if (platform == "linux") {
        result.push_back(InstallPackageTarget::LinuxDeb);
        result.push_back(InstallPackageTarget::LinuxRpm);
    }
    result.push_back(InstallPackageTarget::Tarball);
    return result;
}

} // namespace

int cmdInstallPackage(int argc, char **argv) {
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            installPackageUsage();
            return 0;
        }
    }

    InstallPackageArgs args;
    if (!parseInstallPackageArgs(argc, argv, args))
        return 1;

    try {
        if (!args.verifyOnlyPath.empty()) {
            std::ostringstream err;
            InstallPackageTarget target = args.target;
            if (target == InstallPackageTarget::All &&
                !inferVerifyTargetFromPath(args.verifyOnlyPath, target)) {
                std::cerr << "error: cannot infer install-package artifact type from extension: "
                          << args.verifyOnlyPath.string()
                          << " (use --target for supported artifact formats)\n";
                return 1;
            }
            if (!verifyArtifact(args.verifyOnlyPath, target, err)) {
                std::cerr << err.str();
                return 1;
            }
            return 0;
        }

        fs::path stageDir = ensureStageDir(args);
        AutoStageCleanup stageCleanup(stageDir, args.stageDir.empty() && !args.keepStageDir);
        std::optional<fs::path> installManifestPath;
        if (!args.buildDir.empty())
            installManifestPath = args.buildDir / "install_manifest.txt";
        viper::pkg::ToolchainInstallManifest manifest =
            viper::pkg::gatherToolchainInstallManifest(stageDir, installManifestPath);
        const auto detectedInfo = detectManifestToolchainExecutableInfo(manifest);
        if (detectedInfo) {
            manifest.platform = detectedInfo->platform;
        }
        if (!args.archOverride.empty()) {
            if (detectedInfo && detectedInfo->arch != "universal" &&
                detectedInfo->arch != args.archOverride) {
                std::cerr << "error: --arch " << args.archOverride
                          << " does not match staged viper binary architecture "
                          << detectedInfo->arch << "\n";
                return 1;
            }
            manifest.arch = args.archOverride;
        } else if (detectedInfo && detectedInfo->arch == "universal") {
            manifest.arch = "universal";
        } else if (detectedInfo) {
            manifest.arch = detectedInfo->arch;
        }
        viper::pkg::validateToolchainInstallManifest(manifest);

        if (args.verbose) {
            std::cout << "Stage: " << stageDir.string() << "\n";
            std::cout << "Version: " << manifest.version << "\n";
            std::cout << "Platform: " << manifest.platform << "\n";
            std::cout << "Arch: " << manifest.arch << "\n";
            std::cout << "Files: " << manifest.files.size() << "\n";
        }

        if (args.stageOnly)
            return 0;

        fs::path outBase = args.outputPath;
        if (outBase.empty())
            outBase = stageDir.parent_path() / "installers";
        const auto targets = selectedTargets(args.target, manifest.platform);
        std::error_code outEc;
        const bool outPathExistsAsDirectory = !outBase.empty() && fs::is_directory(outBase, outEc);
        const bool outIsDirectoryLike = args.outputPath.empty() || outPathExistsAsDirectory ||
                                        (!outBase.has_extension() && targets.size() > 1);
        if (outIsDirectoryLike)
            fs::create_directories(outBase);

        const bool buildsWindows =
            std::find(targets.begin(), targets.end(), InstallPackageTarget::Windows) !=
            targets.end();
        if (buildsWindows && manifest.platform == "windows" && !args.allowDebugToolchain) {
            if (const auto debugRuntime = firstWindowsDebugRuntimeReference(manifest)) {
                std::cerr
                    << "error: refusing to build a Windows installer from a Debug CRT payload: "
                    << *debugRuntime << "\n"
                    << "       rebuild with --config Release or RelWithDebInfo, or pass "
                       "--allow-debug-toolchain for local-only diagnostics\n";
                return 1;
            }
        }

        for (InstallPackageTarget target : targets) {
            if (!targetMatchesStagedPlatform(target, manifest.platform)) {
                std::cerr << "error: target does not match staged viper binary platform "
                          << manifest.platform << "\n";
                return 1;
            }
            if (target == InstallPackageTarget::LinuxRpm && !rpmbuildAvailable()) {
                if (args.target == InstallPackageTarget::All) {
                    std::cerr << "error: --target all for a Linux toolchain includes linux-rpm "
                                 "and requires rpmbuild; install rpm-build or choose "
                                 "--target linux-deb/tarball explicitly\n";
                } else {
                    std::cerr << "error: --target linux-rpm requires rpmbuild; install rpm-build "
                                 "or use --target linux-deb/tarball\n";
                }
                return 1;
            }
            fs::path artifactPath =
                outIsDirectoryLike ? (outBase / targetFileName(target, manifest)) : outBase;
            if (!outIsDirectoryLike && !artifactPath.parent_path().empty())
                fs::create_directories(artifactPath.parent_path());

            switch (target) {
                case InstallPackageTarget::Windows: {
                    viper::pkg::WindowsToolchainBuildParams params;
                    params.manifest = manifest;
                    params.outputPath = artifactPath.string();
                    params.archStr = manifest.arch;
                    params.installScope = args.windowsInstallScope;
                    params.installDirName = args.windowsInstallDir;
                    params.addToPath = args.windowsAddToPath;
                    params.registerFileAssociations = args.windowsFileAssociations;
                    params.createStartMenuShortcuts = args.windowsShortcuts;
                    viper::pkg::buildWindowsToolchainInstaller(params);
                    break;
                }
                case InstallPackageTarget::MacOS: {
                    viper::pkg::MacOSToolchainBuildParams params;
                    params.manifest = manifest;
                    params.outputPath = artifactPath.string();
                    params.packageVersion = args.macosPackageVersion;
                    viper::pkg::buildMacOSToolchainPackage(params);
                    break;
                }
                case InstallPackageTarget::LinuxDeb: {
                    viper::pkg::LinuxToolchainBuildParams params;
                    params.manifest = manifest;
                    params.outputPath = artifactPath.string();
                    viper::pkg::buildToolchainDebPackage(params);
                    break;
                }
                case InstallPackageTarget::LinuxRpm: {
                    viper::pkg::LinuxToolchainBuildParams params;
                    params.manifest = manifest;
                    params.outputPath = artifactPath.string();
                    viper::pkg::buildToolchainRpmPackage(params);
                    break;
                }
                case InstallPackageTarget::Tarball: {
                    viper::pkg::LinuxToolchainBuildParams params;
                    params.manifest = manifest;
                    params.outputPath = artifactPath.string();
                    viper::pkg::buildToolchainTarball(params);
                    break;
                }
                case InstallPackageTarget::All:
                    break;
            }

            if (target == InstallPackageTarget::Windows &&
                !signWindowsInstallerArtifact(args, artifactPath, std::cerr)) {
                std::error_code removeEc;
                fs::remove(artifactPath, removeEc);
                return 1;
            }
            if (target == InstallPackageTarget::MacOS &&
                !signMacOSPackageArtifact(args, artifactPath, std::cerr)) {
                std::error_code removeEc;
                fs::remove(artifactPath, removeEc);
                return 1;
            }

            if (!args.noVerify) {
                std::ostringstream err;
                if (!verifyArtifact(artifactPath, target, err, &manifest)) {
                    std::cerr << "error: verification failed for " << artifactPath.string() << "\n"
                              << err.str();
                    std::error_code removeEc;
                    fs::remove(artifactPath, removeEc);
                    return 1;
                }
            }

            std::cout << artifactPath.string() << "\n";
        }

        if (!args.keepStageDir && args.stageDir.empty())
            fs::remove_all(stageDir);
        stageCleanup.dismiss();

        return 0;
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
