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
#include "tools/common/packaging/PkgDeflate.hpp"
#include "tools/common/packaging/PkgUtils.hpp"
#include "tools/common/packaging/PkgVerify.hpp"
#include "tools/common/packaging/ToolchainInstallManifest.hpp"
#include "tools/common/packaging/WindowsPackageBuilder.hpp"

#include <chrono>
#include <cctype>
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

struct InstallPackageArgs {
    InstallPackageTarget target{InstallPackageTarget::All};
    std::string archOverride;
    fs::path stageDir;
    fs::path buildDir;
    std::string buildConfig;
    fs::path outputPath;
    fs::path verifyOnlyPath;
    std::string macosPackageVersion;
    bool windowsSign{false};
    std::string windowsSignPfx;
    std::string windowsSignThumbprint;
    std::string windowsTimestampUrl;
    std::string windowsSigntoolPath;
    bool windowsSignNoVerify{false};
    bool noVerify{false};
    bool verbose{false};
    bool keepStageDir{false};
    bool stageOnly{false};
};

struct NativeExecutableInfo {
    std::string platform;
    std::string arch;
};

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
        << "  --build-dir <dir>     Build tree; runs cmake --install into a staging dir\n"
        << "  --config <cfg>        Build configuration for cmake --install from a build tree\n"
        << "  --verify-only <path>  Verify an existing artifact and exit\n"
        << "  --macos-pkg-version <v> Dotted numeric pkgbuild version override\n"
        << "  --windows-sign        Authenticode-sign generated Windows installer\n"
        << "  --windows-sign-pfx <path> PFX certificate for Windows signing\n"
        << "  --windows-sign-thumbprint <sha1> Certificate store SHA-1 thumbprint\n"
        << "  --windows-timestamp-url <url> RFC3161 timestamp URL for Windows signing\n"
        << "  --windows-signtool <path> signtool.exe path override\n"
        << "  --windows-sign-no-verify Skip signtool verify after signing\n"
        << "  --stage-only          Validate/gather the staged install tree and stop\n"
        << "  --keep-stage-dir      Preserve auto-generated stage directories\n"
        << "  -o <path>             Output file or directory\n"
        << "  --no-verify           Skip post-build verification\n"
        << "  --verbose, -v         Verbose output\n"
        << "  --help, -h            Show this help\n";
}

std::string hostPlatformName() {
#if defined(__APPLE__)
    return "macos";
#elif defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
}

std::string debArchFor(const std::string &arch) {
    viper::pkg::validateToolchainArchitecture(arch);
    return arch == "arm64" ? "arm64" : "amd64";
}

std::string rpmArchFor(const std::string &arch) {
    viper::pkg::validateToolchainArchitecture(arch);
    return arch == "arm64" ? "aarch64" : "x86_64";
}

bool rpmbuildAvailable() {
    const RunResult rr = run_process({"rpmbuild", "--version"});
    return rr.exit_code == 0;
}

std::string getenvOrEmpty(const char *name) {
    const char *value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
}

bool windowsSigningRequested(const InstallPackageArgs &args) {
    return args.windowsSign || !args.windowsSignPfx.empty() ||
           !args.windowsSignThumbprint.empty();
}

bool signWindowsInstallerArtifact(const InstallPackageArgs &args,
                                  const fs::path &artifactPath,
                                  std::ostream &err) {
    if (!windowsSigningRequested(args))
        return true;

    std::string pfxPath = args.windowsSignPfx.empty() ? getenvOrEmpty("VIPER_WINDOWS_SIGN_PFX")
                                                      : args.windowsSignPfx;
    std::string thumbprint = args.windowsSignThumbprint.empty()
                                 ? getenvOrEmpty("VIPER_WINDOWS_SIGN_THUMBPRINT")
                                 : args.windowsSignThumbprint;
    try {
        thumbprint =
            viper::pkg::normalizeWindowsCertificateThumbprint(thumbprint,
                                                              "Windows signing thumbprint");
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
        timestampUrl = "http://timestamp.digicert.com";

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
            err << "error: signtool verify failed with exit code " << verifyResult.exit_code
                << "\n"
                << verifyResult.out << verifyResult.err;
            return false;
        }
    }
    return true;
}

uint16_t readBE16(const std::vector<uint8_t> &data, size_t off) {
    return static_cast<uint16_t>((data[off] << 8) | data[off + 1]);
}

uint16_t readLE16(const std::vector<uint8_t> &data, size_t off) {
    return static_cast<uint16_t>(data[off] | (data[off + 1] << 8));
}

uint32_t readLE32(const std::vector<uint8_t> &data, size_t off) {
    return static_cast<uint32_t>(data[off]) | (static_cast<uint32_t>(data[off + 1]) << 8) |
           (static_cast<uint32_t>(data[off + 2]) << 16) |
           (static_cast<uint32_t>(data[off + 3]) << 24);
}

uint32_t readBE32(const std::vector<uint8_t> &data, size_t off) {
    return (static_cast<uint32_t>(data[off]) << 24) |
           (static_cast<uint32_t>(data[off + 1]) << 16) |
           (static_cast<uint32_t>(data[off + 2]) << 8) | static_cast<uint32_t>(data[off + 3]);
}

uint64_t readBE64(const std::vector<uint8_t> &data, size_t off) {
    return (static_cast<uint64_t>(readBE32(data, off)) << 32) | readBE32(data, off + 4);
}

uint32_t readBE32Ptr(const uint8_t *data) {
    return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
}

uint32_t adler32(const uint8_t *data, size_t len) {
    uint32_t a = 1;
    uint32_t b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

std::vector<uint8_t> inflateZlibPayload(const uint8_t *data, size_t len, size_t expectedSize) {
    if (len < 6)
        throw std::runtime_error("zlib stream is too small");
    const uint8_t cmf = data[0];
    const uint8_t flg = data[1];
    if ((cmf & 0x0Fu) != 8u || ((static_cast<uint16_t>(cmf) << 8) + flg) % 31u != 0)
        throw std::runtime_error("zlib header is invalid");
    if ((flg & 0x20u) != 0)
        throw std::runtime_error("zlib preset dictionaries are not supported");
    const size_t deflateLen = len - 6;
    auto out = viper::pkg::inflate(data + 2, deflateLen, expectedSize);
    const uint32_t expectedAdler = readBE32Ptr(data + len - 4);
    const uint32_t actualAdler = adler32(out.data(), out.size());
    if (expectedAdler != actualAdler)
        throw std::runtime_error("zlib Adler-32 mismatch");
    if (out.size() != expectedSize)
        throw std::runtime_error("zlib uncompressed size mismatch");
    return out;
}

std::string lowerAscii(std::string text) {
    for (char &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string binaryBaseName(std::string filename) {
    filename = lowerAscii(std::move(filename));
    if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".exe")
        filename.resize(filename.size() - 4);
    return filename;
}

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
    if (magicBE == 0xCAFEBABEu && data.size() >= 8) {
        const uint32_t count = readBE32(data, 4);
        if (count == 0 || count > 64 || data.size() < 8 + static_cast<size_t>(count) * 20)
            return std::nullopt;
        bool hasX64 = false;
        bool hasArm64 = false;
        for (uint32_t i = 0; i < count; ++i) {
            const size_t off = 8 + static_cast<size_t>(i) * 20;
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

bool parseInstallPackageArgs(int argc, char **argv, InstallPackageArgs &args) {
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--help" || arg == "-h")) {
            installPackageUsage();
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
                viper::pkg::validateDottedNumericVersion(
                    args.macosPackageVersion, "macOS package version override");
            } catch (const std::exception &ex) {
                std::cerr << "error: " << ex.what() << "\n";
                return false;
            }
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
        } else if (arg == "-o" && i + 1 < argc) {
            args.outputPath = argv[++i];
        } else if (arg == "--no-verify") {
            args.noVerify = true;
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
    try {
        viper::pkg::validatePackageUrl(args.windowsTimestampUrl, "Windows timestamp URL");
        viper::pkg::validateSingleLineField(args.windowsSigntoolPath,
                                            "Windows signtool path");
        viper::pkg::validateWindowsCertificateThumbprint(
            args.windowsSignThumbprint, "Windows signing thumbprint");
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return false;
    }
    return true;
}

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

std::vector<std::string> requiredPayloadPaths(
    InstallPackageTarget target,
    const viper::pkg::ToolchainInstallManifest &manifest) {
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
                paths.push_back("app/" + viper::pkg::sanitizePackageRelativePath(
                                             file.stagedRelativePath, "windows toolchain path"));
            }
            paths.push_back("app/uninstall.exe");
            paths.push_back("meta/manifest.sha256");
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
            const std::string topDir =
                viper::pkg::sanitizePackageRelativePath(packageName + "-" + version + "-" +
                                                            manifest.platform + "-" + manifest.arch,
                                                        "toolchain tarball top-level directory");
            for (const auto &file : manifest.files) {
                paths.push_back(topDir + "/" +
                                viper::pkg::mapInstallPath(
                                    file, viper::pkg::InstallPathPolicy::PortableArchive));
            }
            if (manifest.platform == "linux")
                appendLinuxAssociationMetadata(topDir + "/", true);
            break;
        }
        case InstallPackageTarget::MacOS:
            for (const auto &file : manifest.files) {
                paths.push_back("usr/local/viper/" +
                                viper::pkg::sanitizePackageRelativePath(
                                    file.stagedRelativePath, "macOS toolchain path"));
                if (file.kind == viper::pkg::ToolchainFileKind::Binary) {
                    paths.push_back("usr/local/bin/" +
                                    fs::path(file.stagedRelativePath).filename().generic_string());
                }
            }
            break;
        case InstallPackageTarget::All:
            break;
    }
    return paths;
}

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

bool verifyArtifact(const fs::path &artifact,
                    InstallPackageTarget target,
                    std::ostream &err,
                    const viper::pkg::ToolchainInstallManifest *manifest = nullptr) {
    const auto data = viper::pkg::readFile(artifact.string());
    switch (target) {
        case InstallPackageTarget::Windows:
            if (manifest) {
                return viper::pkg::verifyPEZipOverlayPayload(
                    data, requiredPayloadPaths(target, *manifest), err);
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
            if (data.size() < 28 || data[0] != 'x' || data[1] != 'a' || data[2] != 'r' ||
                data[3] != '!') {
                err << "macos: pkg does not begin with xar header\n";
                return false;
            }
            {
                const uint16_t headerSize = readBE16(data, 4);
                const uint16_t version = readBE16(data, 6);
                const uint64_t tocCompressed = readBE64(data, 8);
                const uint64_t tocUncompressed = readBE64(data, 16);
                if (headerSize < 28 || headerSize > data.size()) {
                    err << "macos: pkg has invalid xar header size\n";
                    return false;
                }
                if (version != 1) {
                    err << "macos: unsupported xar version " << version << "\n";
                    return false;
                }
                if (tocCompressed == 0 || tocUncompressed == 0 ||
                    tocCompressed > data.size() - headerSize) {
                    err << "macos: xar TOC length is invalid\n";
                    return false;
                }
                if (tocUncompressed > 64ull * 1024ull * 1024ull) {
                    err << "macos: xar TOC is unreasonably large\n";
                    return false;
                }
                try {
                    const auto toc = inflateZlibPayload(data.data() + headerSize,
                                                        static_cast<size_t>(tocCompressed),
                                                        static_cast<size_t>(tocUncompressed));
                    const std::string tocText(toc.begin(), toc.end());
                    if (tocText.find("<xar") == std::string::npos ||
                        tocText.find("</xar>") == std::string::npos ||
                        tocText.find("<name>Payload</name>") == std::string::npos ||
                        tocText.find("<name>PackageInfo</name>") == std::string::npos ||
                        tocText.find("<name>Bom</name>") == std::string::npos) {
                        err << "macos: xar TOC does not describe a package payload\n";
                        return false;
                    }
                } catch (const std::exception &ex) {
                    err << "macos: cannot inflate xar TOC: " << ex.what() << "\n";
                    return false;
                }
            }
            if (manifest) {
                const RunResult rr = run_process({"pkgutil", "--payload-files", artifact.string()});
                if (rr.exit_code != 0) {
                    err << "macos: pkgutil could not list package payload:\n" << rr.out
                        << rr.err;
                    return false;
                }
                return requireListedPayloadPaths(parsePayloadListing(rr.out),
                                                 requiredPayloadPaths(target, *manifest),
                                                 "macos",
                                                 err);
            }
            return true;
        case InstallPackageTarget::LinuxRpm:
            if (data.size() < 112 || data[0] != 0xED || data[1] != 0xAB || data[2] != 0xEE ||
                data[3] != 0xDB) {
                err << "rpm: missing lead magic\n";
                return false;
            }
            if (data[4] != 3) {
                err << "rpm: unsupported lead major version " << static_cast<int>(data[4])
                    << "\n";
                return false;
            }
            if (readBE16(data, 78) != 5) {
                err << "rpm: unsupported signature type\n";
                return false;
            }
            if (data[96] != 0x8E || data[97] != 0xAD || data[98] != 0xE8 || data[99] != 0x01) {
                err << "rpm: missing signature header magic\n";
                return false;
            }
            {
                const uint32_t sigIndexCount = readBE32(data, 104);
                const uint32_t sigStoreSize = readBE32(data, 108);
                const uint64_t sigEnd = 96ull + 16ull + static_cast<uint64_t>(sigIndexCount) * 16ull +
                                        sigStoreSize;
                if (sigEnd > data.size()) {
                    err << "rpm: signature header extends past end of file\n";
                    return false;
                }
                const size_t mainOff = static_cast<size_t>((sigEnd + 7ull) & ~7ull);
                if (mainOff + 16 > data.size()) {
                    err << "rpm: missing main header\n";
                    return false;
                }
                if (data[mainOff] != 0x8E || data[mainOff + 1] != 0xAD ||
                    data[mainOff + 2] != 0xE8 || data[mainOff + 3] != 0x01) {
                    err << "rpm: missing main header magic\n";
                    return false;
                }
                const uint32_t mainIndexCount = readBE32(data, mainOff + 8);
                const uint32_t mainStoreSize = readBE32(data, mainOff + 12);
                const uint64_t mainEnd = static_cast<uint64_t>(mainOff) + 16ull +
                                         static_cast<uint64_t>(mainIndexCount) * 16ull +
                                         mainStoreSize;
                if (mainEnd > data.size()) {
                    err << "rpm: main header extends past end of file\n";
                    return false;
                }
                if (mainEnd == data.size()) {
                    err << "rpm: package has no payload after main header\n";
                    return false;
                }
                bool sawPayloadByte = false;
                for (size_t i = static_cast<size_t>(mainEnd); i < data.size(); ++i) {
                    if (data[i] != 0) {
                        sawPayloadByte = true;
                        break;
                    }
                }
                if (!sawPayloadByte) {
                    err << "rpm: package payload is empty\n";
                    return false;
                }
            }
            if (manifest) {
                const RunResult rr = run_process({"rpm", "-qpl", artifact.string()});
                if (rr.exit_code != 0) {
                    err << "rpm: rpm could not list package payload:\n" << rr.out << rr.err;
                    return false;
                }
                return requireListedPayloadPaths(parsePayloadListing(rr.out),
                                                 requiredPayloadPaths(target, *manifest),
                                                 "rpm",
                                                 err);
            }
            return true;
        case InstallPackageTarget::All:
        default:
            return false;
    }
}

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

class AutoStageCleanup {
  public:
    AutoStageCleanup(fs::path path, bool enabled) : path_(std::move(path)), enabled_(enabled) {}
    ~AutoStageCleanup() {
        if (enabled_ && !path_.empty()) {
            std::error_code ec;
            fs::remove_all(path_, ec);
        }
    }
    void dismiss() {
        enabled_ = false;
    }

  private:
    fs::path path_;
    bool enabled_{false};
};

fs::path ensureStageDir(const InstallPackageArgs &args) {
    if (!args.stageDir.empty())
        return args.stageDir;

    const auto pid =
#if defined(_WIN32)
        static_cast<unsigned long>(_getpid());
#else
        static_cast<unsigned long>(::getpid());
#endif
    const auto tick =
        static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
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

    {
        std::error_code ec;
        fs::remove(args.buildDir / "install_manifest.txt", ec);
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
        if (rpmbuildAvailable())
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
    viper::pkg::ToolchainInstallManifest manifest =
        viper::pkg::gatherToolchainInstallManifest(stageDir);
    const auto detectedInfo = detectManifestToolchainExecutableInfo(manifest);
    if (detectedInfo) {
        manifest.platform = detectedInfo->platform;
    }
    if (!args.archOverride.empty()) {
        if (detectedInfo && detectedInfo->arch != "universal" &&
            detectedInfo->arch != args.archOverride) {
            std::cerr << "error: --arch " << args.archOverride
                      << " does not match staged viper binary architecture " << detectedInfo->arch
                      << "\n";
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
    const bool outIsDirectoryLike =
        args.outputPath.empty() || outPathExistsAsDirectory ||
        (!outBase.has_extension() && targets.size() > 1);
    if (outIsDirectoryLike)
        fs::create_directories(outBase);

    for (InstallPackageTarget target : targets) {
        if (!targetMatchesStagedPlatform(target, manifest.platform)) {
            std::cerr << "error: target does not match staged viper binary platform "
                      << manifest.platform << "\n";
            return 1;
        }
        if (target == InstallPackageTarget::LinuxRpm && !rpmbuildAvailable()) {
            std::cerr << "error: --target linux-rpm requires rpmbuild; install rpm-build "
                         "or use --target linux-deb/tarball\n";
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
