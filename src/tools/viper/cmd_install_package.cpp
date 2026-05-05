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

#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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
    bool noVerify{false};
    bool verbose{false};
    bool keepStageDir{false};
    bool stageOnly{false};
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

uint16_t readBE16(const std::vector<uint8_t> &data, size_t off) {
    return static_cast<uint16_t>((data[off] << 8) | data[off + 1]);
}

uint32_t readBE32(const std::vector<uint8_t> &data, size_t off) {
    return (static_cast<uint32_t>(data[off]) << 24) |
           (static_cast<uint32_t>(data[off + 1]) << 16) |
           (static_cast<uint32_t>(data[off + 2]) << 8) | static_cast<uint32_t>(data[off + 3]);
}

uint64_t readBE64(const std::vector<uint8_t> &data, size_t off) {
    return (static_cast<uint64_t>(readBE32(data, off)) << 32) | readBE32(data, off + 4);
}

std::string lowerAscii(std::string text) {
    for (char &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
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
    return true;
}

std::string targetFileName(InstallPackageTarget target,
                           const viper::pkg::ToolchainInstallManifest &manifest) {
    const std::string version = manifest.version.empty() ? "0.0.0" : manifest.version;
    const std::string arch = manifest.arch.empty() ? "x64" : manifest.arch;
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
            return "viper-" + version + "-" + hostPlatformName() + "-" + arch + ".tar.gz";
        case InstallPackageTarget::All:
        default:
            return {};
    }
}

bool verifyArtifact(const fs::path &artifact, InstallPackageTarget target, std::ostream &err) {
    const auto data = viper::pkg::readFile(artifact.string());
    switch (target) {
        case InstallPackageTarget::Windows:
            return viper::pkg::verifyPEZipOverlay(data, err);
        case InstallPackageTarget::LinuxDeb:
            return viper::pkg::verifyDeb(data, err);
        case InstallPackageTarget::Tarball:
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
    return stageDir;
}

std::vector<InstallPackageTarget> selectedTargets(InstallPackageTarget target) {
    if (target != InstallPackageTarget::All)
        return {target};

    std::vector<InstallPackageTarget> result = {
        InstallPackageTarget::Windows,
        InstallPackageTarget::LinuxDeb,
        InstallPackageTarget::Tarball,
    };
#if defined(__APPLE__)
    result.push_back(InstallPackageTarget::MacOS);
#endif
#if defined(__linux__)
    result.push_back(InstallPackageTarget::LinuxRpm);
#endif
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
    if (!args.archOverride.empty())
        manifest.arch = args.archOverride;
    viper::pkg::validateToolchainInstallManifest(manifest);

    if (args.verbose) {
        std::cout << "Stage: " << stageDir.string() << "\n";
        std::cout << "Version: " << manifest.version << "\n";
        std::cout << "Arch: " << manifest.arch << "\n";
        std::cout << "Files: " << manifest.files.size() << "\n";
    }

    if (args.stageOnly)
        return 0;

    fs::path outBase = args.outputPath;
    if (outBase.empty())
        outBase = stageDir.parent_path() / "installers";
    const bool outIsDirectoryLike =
        args.outputPath.empty() || (!outBase.has_extension() && selectedTargets(args.target).size() > 1);
    if (outIsDirectoryLike)
        fs::create_directories(outBase);

    for (InstallPackageTarget target : selectedTargets(args.target)) {
        fs::path artifactPath =
            outIsDirectoryLike ? (outBase / targetFileName(target, manifest)) : outBase;

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

        if (!args.noVerify) {
            std::ostringstream err;
            if (!verifyArtifact(artifactPath, target, err)) {
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
