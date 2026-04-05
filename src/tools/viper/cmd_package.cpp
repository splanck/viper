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
#include "tools/common/native_compiler.hpp"
#include "tools/common/packaging/LinuxPackageBuilder.hpp"
#include "tools/common/packaging/MacOSPackageBuilder.hpp"
#include "tools/common/packaging/PkgVerify.hpp"
#include "tools/common/packaging/WindowsPackageBuilder.hpp"
#include "tools/common/project_loader.hpp"

#include "support/diag_expected.hpp"
#include "support/source_manager.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// Forward declarations of compile functions (defined in cmd_run.cpp)
namespace {

using namespace il::tools::common;
using namespace il::support;
namespace fs = std::filesystem;

enum class PackageTarget { MacOS, Linux, Windows, Tarball, Auto };

/// @brief Print usage information for `viper package`.
void packageUsage() {
    std::cerr
        << "Usage: viper package [project] [options]\n"
        << "\n"
        << "  Compiles a Viper project to a native binary and packages it into a\n"
        << "  platform-specific installer using only Viper's built-in tooling.\n"
        << "\n"
        << "  [project]  Path to a directory or viper.project file (default: .)\n"
        << "\n"
        << "Options:\n"
        << "  --target <platform>   Target platform (default: host)\n"
        << "                        macos    .app bundle in .zip\n"
        << "                        linux    .deb package\n"
        << "                        windows  Self-extracting .exe installer\n"
        << "                        tarball  Portable .tar.gz archive\n"
        << "  --arch <arch>         Target architecture: x64 or arm64 (default: host)\n"
        << "  --executable <path>   Package a prebuilt native executable instead of compiling\n"
        << "  -o <path>             Output file path\n"
        << "  --dry-run             List package contents without building\n"
        << "  --verbose, -v         Show detailed packaging output\n"
        << "  --help, -h            Show this help\n"
        << "\n"
        << "Package manifest directives (in viper.project):\n"
        << "  package-name <name>                 Display name\n"
        << "  package-author <name>               Author / maintainer\n"
        << "  package-description <text>          Short description\n"
        << "  package-homepage <url>              Project homepage URL\n"
        << "  package-license <spdx>              License identifier (SPDX)\n"
        << "  package-identifier <id>             Reverse-DNS identifier\n"
        << "  package-icon <path>                 Source PNG for icons (512x512+)\n"
        << "  asset <source> <target>             Include asset files\n"
        << "  file-assoc <ext> <desc> <mime>      Register file type association\n"
        << "  shortcut-desktop on|off             Create desktop shortcut (Windows/Linux)\n"
        << "  shortcut-menu on|off                Create menu entry (default: on)\n"
        << "  min-os-macos <ver>                  Minimum macOS version (default: 10.13)\n"
        << "  min-os-windows <ver>                Minimum Windows version\n"
        << "  package-category <category>          Application category (e.g. Game, Utility)\n"
        << "  package-depends <pkg1>, <pkg2>      Package dependencies (Linux .deb)\n"
        << "  target-arch x64|arm64               Target architecture\n"
        << "  post-install <script>               Post-install hook (Linux .deb)\n"
        << "  pre-uninstall <script>              Pre-uninstall hook (Linux .deb)\n"
        << "\n"
        << "Examples:\n"
        << "  viper package                       Package current dir for host platform\n"
        << "  viper package myapp/ --target linux  Build .deb for Linux\n"
        << "  viper package . --target windows -o myapp-setup.exe\n"
        << "\n"
        << "Output formats:\n"
        << "  macOS:    .app bundle in .zip (Finder-native, drag to /Applications)\n"
        << "  Linux:    .deb package (dpkg -i), includes .desktop + MIME types\n"
        << "  Windows:  PE32+ .exe with embedded ZIP (assets, shortcuts, uninstaller)\n"
        << "  Tarball:  .tar.gz portable archive\n";
}

struct PackageArgs {
    std::string target;
    PackageTarget platformTarget{PackageTarget::Auto};
    std::string outputPath;
    std::string archOverride; // "x64" or "arm64"
    std::string executablePath;
    bool dryRun{false};
    bool verbose{false};
};

PackageTarget detectHostPlatform() {
#if defined(__APPLE__)
    return PackageTarget::MacOS;
#elif defined(__linux__)
    return PackageTarget::Linux;
#elif defined(_WIN32)
    return PackageTarget::Windows;
#else
    return PackageTarget::Tarball;
#endif
}

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
        default:
            return "unknown";
    }
}

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
        default:
            return ".zip";
    }
}

bool parsePackageArgs(int argc, char **argv, PackageArgs &args) {
    for (int i = 0; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--target" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "macos")
                args.platformTarget = PackageTarget::MacOS;
            else if (val == "linux")
                args.platformTarget = PackageTarget::Linux;
            else if (val == "windows")
                args.platformTarget = PackageTarget::Windows;
            else if (val == "tarball")
                args.platformTarget = PackageTarget::Tarball;
            else {
                std::cerr << "error: unknown target '" << val
                          << "'; expected macos, linux, windows, or tarball\n";
                return false;
            }
        } else if (arg == "--arch" && i + 1 < argc) {
            args.archOverride = argv[++i];
            if (args.archOverride != "x64" && args.archOverride != "arm64") {
                std::cerr << "error: unknown arch '" << args.archOverride
                          << "'; expected x64 or arm64\n";
                return false;
            }
        } else if (arg == "--executable" && i + 1 < argc) {
            args.executablePath = argv[++i];
        } else if (arg == "-o" && i + 1 < argc) {
            args.outputPath = argv[++i];
        } else if (arg == "--dry-run") {
            args.dryRun = true;
        } else if (arg == "--verbose" || arg == "-v") {
            args.verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            packageUsage();
            return false;
        } else if (arg[0] == '-') {
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

} // namespace

// The compile functions are in cmd_run.cpp — we need to expose them or
// duplicate the pattern. For now, we use the same resolveProject + compileToNative
// pipeline that cmdBuild uses.

int cmdPackage(int argc, char **argv) {
    using namespace il::tools::common;
    namespace fs = std::filesystem;

    PackageArgs args;
    if (!parsePackageArgs(argc, argv, args))
        return 1;

    // Resolve project
    auto project = resolveProject(args.target);
    if (!project) {
        SourceManager sm;
        il::support::printDiag(project.error(), std::cerr, &sm);
        return 1;
    }

    ProjectConfig &proj = project.value();

    // Check that package config is present
    if (!proj.packageConfig.hasPackageConfig()) {
        std::cerr << "warning: no package-* directives in viper.project; "
                  << "using defaults\n";
    }

    // Determine display name and version
    std::string displayName =
        proj.packageConfig.displayName.empty() ? proj.name : proj.packageConfig.displayName;

    // Validate version string (warn on clearly invalid formats)
    {
        bool validVersion = !proj.version.empty();
        if (validVersion) {
            for (char c : proj.version) {
                if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.' && c != '-' &&
                    c != '+' && !std::isalpha(static_cast<unsigned char>(c))) {
                    validVersion = false;
                    break;
                }
            }
        }
        if (!validVersion) {
            std::cerr << "warning: version '" << proj.version
                      << "' may be invalid; expected format like '1.0.0' or '1.0.0-beta'\n";
        }
    }

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

    // Dry-run mode: list what would be packaged, then exit
    if (args.dryRun) {
        std::cerr << "Dry run: " << displayName << " for " << platformName(args.platformTarget)
                  << " (" << archStr << ")\n";
        std::cerr << "  Output: " << args.outputPath << "\n";
        if (!args.executablePath.empty())
            std::cerr << "  Executable: " << args.executablePath << " (prebuilt)\n";
        else
            std::cerr << "  Executable: " << proj.name << " (build)\n";
        if (!proj.packageConfig.iconPath.empty()) {
            fs::path iconPath = fs::path(proj.rootDir) / proj.packageConfig.iconPath;
            std::cerr << "  Icon: " << proj.packageConfig.iconPath;
            if (!fs::exists(iconPath))
                std::cerr << " [NOT FOUND]";
            std::cerr << "\n";
        }
        for (const auto &asset : proj.packageConfig.assets) {
            fs::path assetPath = fs::path(proj.rootDir) / asset.sourcePath;
            std::cerr << "  Asset: " << asset.sourcePath << " -> " << asset.targetPath;
            if (!fs::exists(assetPath))
                std::cerr << " [NOT FOUND]";
            else if (fs::is_directory(assetPath)) {
                size_t count = 0;
                for (auto &e : fs::recursive_directory_iterator(assetPath))
                    if (e.is_regular_file())
                        count++;
                std::cerr << " (" << count << " files)";
            }
            std::cerr << "\n";
        }
        for (const auto &assoc : proj.packageConfig.fileAssociations)
            std::cerr << "  File assoc: " << assoc.extension << " (" << assoc.description << ")\n";
        if (!proj.packageConfig.category.empty())
            std::cerr << "  Category: " << proj.packageConfig.category << "\n";
        if (!proj.packageConfig.depends.empty()) {
            std::cerr << "  Depends:";
            for (const auto &d : proj.packageConfig.depends)
                std::cerr << " " << d;
            std::cerr << "\n";
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

    std::string packageBinaryPath;
    bool cleanupPackagedBinary = false;

    if (!args.executablePath.empty()) {
        fs::path exePath(args.executablePath);
        if (!exePath.is_absolute())
            exePath = fs::path(proj.rootDir) / exePath;
        packageBinaryPath = exePath.lexically_normal().string();
        if (!fs::exists(packageBinaryPath)) {
            std::cerr << "error: prebuilt executable not found at " << packageBinaryPath << "\n";
            return 1;
        }
    } else {
        // Step 1: Compile to native binary (using build pipeline)
        std::cerr << "Compiling " << proj.name << "...\n";

        // We need to compile the project to IL first, then to native.
        // Reuse the same approach as cmdBuild: compile → serialize IL → compileToNative
        SourceManager sm;

        // We need to include the compilation infrastructure. For now, use the
        // external compileToNative pathway: serialize IL to temp, then codegen.
        // This requires the compile pipeline. Since cmd_run.cpp has internal
        // compile functions, we instead build the native binary via a temp IL file
        // by calling the run/build pipeline.

        // The simplest approach: use viper build -o <tempBinary> <target>
        // But that requires forking ourselves. Instead, compile IL and use
        // compileToNative directly.

        // For the initial implementation, we compile using the existing build
        // pathway by writing a temporary binary.
        std::string tempBinaryPath = viper::tools::generateTempIlPath();
        // Change extension to the native binary
        tempBinaryPath = tempBinaryPath.substr(0, tempBinaryPath.rfind('.'));
#ifdef _WIN32
        tempBinaryPath += ".exe";
#endif

        // Build the native binary using cmdBuild directly (same binary)
        {
            // Construct argv for cmdBuild: <target> -o <tempBinaryPath>
            std::vector<std::string> buildStorage = {
                args.target, "-o", tempBinaryPath, "--arch", archStr};
            std::vector<char *> buildArgv;
            buildArgv.reserve(buildStorage.size());
            for (auto &arg : buildStorage)
                buildArgv.push_back(arg.data());
            int rc = cmdBuild(static_cast<int>(buildArgv.size()), buildArgv.data());
            if (rc != 0) {
                std::cerr << "error: compilation failed\n";
                return 1;
            }
        }

        if (!fs::exists(tempBinaryPath)) {
            std::cerr << "error: compiled binary not found at " << tempBinaryPath << "\n";
            return 1;
        }

        packageBinaryPath = std::move(tempBinaryPath);
        cleanupPackagedBinary = true;
    }

    // Step 2: Determine output path
    if (args.outputPath.empty()) {
        args.outputPath = proj.name + "-" + proj.version + "-" + platformName(args.platformTarget) +
                          "-" + archStr + platformExtension(args.platformTarget);
    }

    // Step 3: Package
    std::cerr << "Packaging " << displayName << " for " << platformName(args.platformTarget) << " ("
              << archStr << ")...\n";

    if (args.verbose) {
        auto binSize = fs::file_size(packageBinaryPath);
        std::cerr << "  Binary: " << packageBinaryPath << " (" << binSize << " bytes)\n";
        std::cerr << "  Output: " << args.outputPath << "\n";
        if (!proj.packageConfig.iconPath.empty())
            std::cerr << "  Icon: " << proj.packageConfig.iconPath << "\n";
        for (const auto &asset : proj.packageConfig.assets)
            std::cerr << "  Asset: " << asset.sourcePath << " -> " << asset.targetPath << "\n";
    }

    try {
        switch (args.platformTarget) {
            case PackageTarget::MacOS: {
                viper::pkg::MacOSBuildParams params;
                params.projectName = proj.name;
                params.version = proj.version;
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
                lparams.version = proj.version;
                lparams.executablePath = packageBinaryPath;
                lparams.projectRoot = proj.rootDir;
                lparams.pkgConfig = proj.packageConfig;
                lparams.outputPath = args.outputPath;
                // Map architecture: Debian uses "amd64" not "x64"
                lparams.archStr = (archStr == "x64") ? "amd64" : archStr;
                viper::pkg::buildDebPackage(lparams);
                break;
            }
            case PackageTarget::Windows: {
                viper::pkg::WindowsBuildParams wparams;
                wparams.projectName = proj.name;
                wparams.version = proj.version;
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
                tparams.version = proj.version;
                tparams.executablePath = packageBinaryPath;
                tparams.projectRoot = proj.rootDir;
                tparams.pkgConfig = proj.packageConfig;
                tparams.outputPath = args.outputPath;
                tparams.archStr = archStr;
                viper::pkg::buildTarball(tparams);
                break;
            }
            default:
                break;
        }
    } catch (const std::exception &e) {
        std::cerr << "error: packaging failed: " << e.what() << "\n";
        std::error_code ec;
        if (cleanupPackagedBinary)
            fs::remove(packageBinaryPath, ec);
        return 1;
    }

    // Step 4: Verify the generated package
    std::error_code ec;
    if (fs::exists(args.outputPath)) {
        std::ifstream pkgFile(args.outputPath, std::ios::binary | std::ios::ate);
        if (pkgFile) {
            auto pkgSize = pkgFile.tellg();
            pkgFile.seekg(0);
            std::vector<uint8_t> pkgData(static_cast<size_t>(pkgSize));
            pkgFile.read(reinterpret_cast<char *>(pkgData.data()), pkgSize);
            pkgFile.close();

            std::ostringstream verifyErr;
            bool valid = true;
            switch (args.platformTarget) {
                case PackageTarget::MacOS:
                    valid = viper::pkg::verifyZip(pkgData, verifyErr);
                    break;
                case PackageTarget::Linux:
                    valid = viper::pkg::verifyDeb(pkgData, verifyErr);
                    break;
                case PackageTarget::Windows:
                    valid = viper::pkg::verifyPEZipOverlay(pkgData, verifyErr);
                    break;
                default:
                    break; // Tarball: no structural verification needed
            }
            if (!valid) {
                std::cerr << "error: package verification failed:\n" << verifyErr.str();
                fs::remove(args.outputPath, ec);
                if (cleanupPackagedBinary)
                    fs::remove(packageBinaryPath, ec);
                return 1;
            }
            if (args.verbose)
                std::cerr << "  Verification: passed\n";
        }
    }

    // Cleanup temp binary
    if (cleanupPackagedBinary)
        fs::remove(packageBinaryPath, ec);

    std::cerr << "Package created: " << args.outputPath;
    if (args.verbose && fs::exists(args.outputPath)) {
        auto pkgSize = fs::file_size(args.outputPath);
        std::cerr << " (" << pkgSize << " bytes)";
    }
    std::cerr << "\n";
    return 0;
}
