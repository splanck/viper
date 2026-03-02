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
//   - Cross-packaging is supported (byte-level format generation).
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
#include "tools/common/packaging/WindowsPackageBuilder.hpp"
#include "tools/common/project_loader.hpp"

#include "support/diag_expected.hpp"
#include "support/source_manager.hpp"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

// Forward declarations of compile functions (defined in cmd_run.cpp)
namespace
{

using namespace il::tools::common;
using namespace il::support;
namespace fs = std::filesystem;

enum class PackageTarget
{
    MacOS,
    Linux,
    Windows,
    Tarball,
    Auto
};

/// @brief Print usage information for `viper package`.
void packageUsage()
{
    std::cerr
        << "Usage: viper package [project] [options]\n"
        << "\n"
        << "  Compiles a Viper project to a native binary and packages it into a\n"
        << "  platform-specific installer. Cross-packaging is fully supported.\n"
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
        << "  -o <path>             Output file path\n"
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

struct PackageArgs
{
    std::string target;
    PackageTarget platformTarget{PackageTarget::Auto};
    std::string outputPath;
    std::string archOverride; // "x64" or "arm64"
};

PackageTarget detectHostPlatform()
{
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

std::string platformName(PackageTarget t)
{
    switch (t)
    {
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

std::string platformExtension(PackageTarget t)
{
    switch (t)
    {
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

bool parsePackageArgs(int argc, char **argv, PackageArgs &args)
{
    for (int i = 0; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--target" && i + 1 < argc)
        {
            std::string val = argv[++i];
            if (val == "macos")
                args.platformTarget = PackageTarget::MacOS;
            else if (val == "linux")
                args.platformTarget = PackageTarget::Linux;
            else if (val == "windows")
                args.platformTarget = PackageTarget::Windows;
            else if (val == "tarball")
                args.platformTarget = PackageTarget::Tarball;
            else
            {
                std::cerr << "error: unknown target '" << val
                          << "'; expected macos, linux, windows, or tarball\n";
                return false;
            }
        }
        else if (arg == "--arch" && i + 1 < argc)
        {
            args.archOverride = argv[++i];
            if (args.archOverride != "x64" && args.archOverride != "arm64")
            {
                std::cerr << "error: unknown arch '" << args.archOverride
                          << "'; expected x64 or arm64\n";
                return false;
            }
        }
        else if (arg == "-o" && i + 1 < argc)
        {
            args.outputPath = argv[++i];
        }
        else if (arg == "--help" || arg == "-h")
        {
            packageUsage();
            return false;
        }
        else if (arg[0] == '-')
        {
            std::cerr << "error: unknown option '" << arg << "'\n";
            packageUsage();
            return false;
        }
        else if (args.target.empty())
        {
            args.target = arg;
        }
        else
        {
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

int cmdPackage(int argc, char **argv)
{
    using namespace il::tools::common;
    namespace fs = std::filesystem;

    PackageArgs args;
    if (!parsePackageArgs(argc, argv, args))
        return 1;

    // Resolve project
    auto project = resolveProject(args.target);
    if (!project)
    {
        SourceManager sm;
        il::support::printDiag(project.error(), std::cerr, &sm);
        return 1;
    }

    ProjectConfig &proj = project.value();

    // Check that package config is present
    if (!proj.packageConfig.hasPackageConfig())
    {
        std::cerr << "warning: no package-* directives in viper.project; "
                  << "using defaults\n";
    }

    // Determine display name and version
    std::string displayName = proj.packageConfig.displayName.empty()
                                  ? proj.name
                                  : proj.packageConfig.displayName;

    // Determine architecture
    viper::tools::TargetArch arch = viper::tools::detectHostArch();
    std::string archStr;
    if (!args.archOverride.empty())
    {
        arch = (args.archOverride == "arm64") ? viper::tools::TargetArch::ARM64
                                              : viper::tools::TargetArch::X64;
        archStr = args.archOverride;
    }
    else
    {
        archStr = (arch == viper::tools::TargetArch::ARM64) ? "arm64" : "x64";
    }

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
        std::vector<char *> buildArgv;
        std::string targetArg = args.target;
        std::string dashO = "-o";
        buildArgv.push_back(targetArg.data());
        buildArgv.push_back(dashO.data());
        buildArgv.push_back(tempBinaryPath.data());
        int rc = cmdBuild(static_cast<int>(buildArgv.size()), buildArgv.data());
        if (rc != 0)
        {
            std::cerr << "error: compilation failed\n";
            return 1;
        }
    }

    if (!fs::exists(tempBinaryPath))
    {
        std::cerr << "error: compiled binary not found at " << tempBinaryPath << "\n";
        return 1;
    }

    // Step 2: Determine output path
    if (args.outputPath.empty())
    {
        args.outputPath = proj.name + "-" + proj.version + "-" + platformName(args.platformTarget)
                          + "-" + archStr + platformExtension(args.platformTarget);
    }

    // Step 3: Package
    std::cerr << "Packaging " << displayName << " for " << platformName(args.platformTarget)
              << " (" << archStr << ")...\n";

    try
    {
        switch (args.platformTarget)
        {
        case PackageTarget::MacOS: {
            viper::pkg::MacOSBuildParams params;
            params.projectName = proj.name;
            params.version = proj.version;
            params.executablePath = tempBinaryPath;
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
            lparams.executablePath = tempBinaryPath;
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
            wparams.executablePath = tempBinaryPath;
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
            tparams.executablePath = tempBinaryPath;
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
    }
    catch (const std::exception &e)
    {
        std::cerr << "error: packaging failed: " << e.what() << "\n";
        std::error_code ec;
        fs::remove(tempBinaryPath, ec);
        return 1;
    }

    // Cleanup temp binary
    std::error_code ec;
    fs::remove(tempBinaryPath, ec);

    std::cerr << "Package created: " << args.outputPath << "\n";
    return 0;
}
