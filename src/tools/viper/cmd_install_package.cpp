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

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
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
    return arch == "arm64" ? "arm64" : "amd64";
}

std::string rpmArchFor(const std::string &arch) {
    return arch == "arm64" ? "aarch64" : "x86_64";
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
            if (data.size() < 2 || data[0] != 0x1F || data[1] != 0x8B) {
                err << "tarball: missing gzip header\n";
                return false;
            }
            return true;
        case InstallPackageTarget::MacOS:
            if (data.size() < 4 || data[0] != 'x' || data[1] != 'a' || data[2] != 'r' ||
                data[3] != '!') {
                err << "macos: pkg does not begin with xar header\n";
                return false;
            }
            return true;
        case InstallPackageTarget::LinuxRpm:
            if (data.size() < 4 || data[0] != 0xED || data[1] != 0xAB || data[2] != 0xEE ||
                data[3] != 0xDB) {
                err << "rpm: missing lead magic\n";
                return false;
            }
            return true;
        case InstallPackageTarget::All:
        default:
            return false;
    }
}

fs::path ensureStageDir(const InstallPackageArgs &args) {
    if (!args.stageDir.empty())
        return args.stageDir;

    const auto pid =
#if defined(_WIN32)
        static_cast<unsigned long>(_getpid());
#else
        static_cast<unsigned long>(::getpid());
#endif
    fs::path stageDir =
        args.buildDir / ("install-toolchain-stage-" + std::to_string(pid));
    fs::remove_all(stageDir);
    fs::create_directories(stageDir);

    const RunResult rr =
        run_process({"cmake", "--install", args.buildDir.string(), "--prefix", stageDir.string()});
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
        InstallPackageTarget target = InstallPackageTarget::Tarball;
        const std::string ext = args.verifyOnlyPath.extension().string();
        if (ext == ".exe")
            target = InstallPackageTarget::Windows;
        else if (ext == ".pkg")
            target = InstallPackageTarget::MacOS;
        else if (ext == ".deb")
            target = InstallPackageTarget::LinuxDeb;
        else if (ext == ".rpm")
            target = InstallPackageTarget::LinuxRpm;
        if (!verifyArtifact(args.verifyOnlyPath, target, err)) {
            std::cerr << err.str();
            return 1;
        }
        return 0;
    }

    fs::path stageDir = ensureStageDir(args);
    viper::pkg::ToolchainInstallManifest manifest =
        viper::pkg::gatherToolchainInstallManifest(stageDir);
    if (!args.archOverride.empty())
        manifest.arch = args.archOverride;

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
                return 1;
            }
        }

        std::cout << artifactPath.string() << "\n";
    }

    if (!args.keepStageDir && args.stageDir.empty())
        fs::remove_all(stageDir);

    return 0;
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
