//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/MacOSPackageBuilder.cpp
// Purpose: Assemble a macOS .app bundle inside a ZIP with proper Unix
//          permissions for executable binaries.
//
// Key invariants:
//   - .app/Contents/MacOS/<name> has mode 0100755.
//   - All other regular files have 0100644.
//   - Directories have 040755.
//   - ICNS icon generated from source PNG with multiple resolutions.
//
// Ownership/Lifetime:
//   - Single-use builder, writes output ZIP file.
//
// Links: MacOSPackageBuilder.hpp, ZipWriter.hpp, PlistGenerator.hpp,
//        IconGenerator.hpp
//
//===----------------------------------------------------------------------===//

#include "MacOSPackageBuilder.hpp"
#include "IconGenerator.hpp"
#include "PkgUtils.hpp"
#include "PlistGenerator.hpp"
#include "ZipWriter.hpp"
#include "common/RunProcess.hpp"

#include <filesystem>
#include <fstream>
#include <string_view>
#include <chrono>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace viper::pkg {
namespace {

/// @brief Generate a unique temp directory path combining `stem`, PID, and steady-clock tick.
/// Avoids collisions between concurrent packaging invocations.
fs::path uniqueTempPackagingDir(std::string_view stem) {
    const auto tick =
        static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto pid =
#if defined(_WIN32)
        static_cast<unsigned long long>(_getpid());
#else
        static_cast<unsigned long long>(::getpid());
#endif
    return fs::temp_directory_path() /
           (std::string(stem) + "-" + std::to_string(pid) + "-" + std::to_string(tick));
}

/// @brief RAII guard that removes the directory tree at `path_` on destruction.
class TempDirGuard {
  public:
    explicit TempDirGuard(fs::path path) : path_(std::move(path)) {}
    ~TempDirGuard() {
        if (!path_.empty()) {
            std::error_code ec;
            fs::remove_all(path_, ec);
        }
    }

  private:
    fs::path path_;
};

/// @brief Validate that `name` is a legal macOS bundle display name.
/// Must be non-empty, single-line, free of path separators (`/`, `\`, `:`), and pass Windows filename checks.
void validateBundleDisplayName(const std::string &name) {
    if (name.empty())
        throw std::runtime_error("macOS bundle display name must not be empty");
    validateSingleLineField(name, "macOS bundle display name");
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos)
        throw std::runtime_error("macOS bundle display name must not contain path separators: " +
                                 name);
    validateWindowsFileName(name, "macOS bundle display name");
}

/// @brief Write `data` to `path`, creating parent directories as needed, and apply `perms`.
void writeFileBytes(const fs::path &path, const std::vector<uint8_t> &data, fs::perms perms) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot write macOS package file: " + path.string());
    out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out)
        throw std::runtime_error("failed while writing macOS package file: " + path.string());
    out.close();
    fs::permissions(path, perms, fs::perm_options::replace);
}

/// @brief Write `text` to `path` as UTF-8 bytes with the given Unix permissions.
void writeFileString(const fs::path &path, const std::string &text, fs::perms perms) {
    const auto *bytes = reinterpret_cast<const uint8_t *>(text.data());
    writeFileBytes(path, std::vector<uint8_t>(bytes, bytes + text.size()), perms);
}

/// @brief Copy a package asset (file or directory tree) from `srcPath` into the .app `Resources` dir.
/// All copied regular files get mode 0644; directories are created as needed.
void copyPackageAssetToResources(const fs::path &srcPath,
                                 const fs::path &projectRoot,
                                 const fs::path &resourcesDir,
                                 const std::string &targetDir,
                                 const std::string &sourceText) {
    const fs::path targetRoot = resourcesDir / fs::path(targetDir);
    if (!fs::exists(srcPath))
        throw std::runtime_error("asset not found: " + sourceText);

    if (fs::is_directory(srcPath)) {
        if (!targetDir.empty())
            fs::create_directories(targetRoot);
        safeDirectoryIterate(
            srcPath, projectRoot, [&](const fs::directory_entry &entry) {
                const auto relPath = sanitizePackageRelativePath(
                    entry.path().lexically_relative(srcPath).generic_string(), "asset path");
                const fs::path dst = targetRoot / fs::path(relPath);
                if (fs::is_directory(entry.path())) {
                    fs::create_directories(dst);
                } else if (fs::is_regular_file(entry.path())) {
                    writeFileBytes(dst,
                                   readFile(entry.path().string()),
                                   fs::perms::owner_read | fs::perms::owner_write |
                                       fs::perms::group_read | fs::perms::others_read);
                }
            });
    } else if (fs::is_regular_file(srcPath)) {
        writeFileBytes(targetRoot / srcPath.filename(),
                       readFile(srcPath.string()),
                       fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                           fs::perms::others_read);
    } else {
        throw std::runtime_error("asset is not a regular file or directory: " + sourceText);
    }
}

/// @brief Run a command and throw `std::runtime_error` if it exits non-zero.
/// The error message includes `what` plus the captured stdout and stderr.
void runChecked(const std::vector<std::string> &args, const std::string &what) {
    RunResult rr = run_process(args);
    if (rr.exit_code != 0)
        throw std::runtime_error(what + " failed:\n" + rr.out + rr.err);
}

/// @brief Resolve the pkgbuild version string for a macOS toolchain package.
/// Returns the validated override if non-empty; otherwise validates and returns the manifest version.
std::string resolveMacOSToolchainPackageVersion(const std::string &manifestVersion,
                                                const std::string &packageVersionOverride) {
    if (!packageVersionOverride.empty()) {
        validateDottedNumericVersion(packageVersionOverride,
                                     "macOS toolchain package version override");
        return packageVersionOverride;
    }
    validateDottedNumericVersion(manifestVersion, "macOS toolchain package version");
    return manifestVersion;
}

/// @brief Walk the staged .app bundle and write all entries to a ZIP at `outputPath`.
/// The file at `execPath` gets Unix mode 0100755; all other files get 0100644.
void addStagedAppToZip(const fs::path &stageRoot,
                       const fs::path &appPath,
                       const fs::path &execPath,
                       const std::string &outputPath) {
    ZipWriter zip;
    const std::string appEntry = fs::relative(appPath, stageRoot).generic_string();
    zip.addDirectory(appEntry);

    for (const auto &entry : fs::recursive_directory_iterator(appPath)) {
        const std::string rel = fs::relative(entry.path(), stageRoot).generic_string();
        if (entry.is_directory()) {
            zip.addDirectory(rel);
        } else if (entry.is_regular_file()) {
            const auto data = readFile(entry.path().string());
            const uint32_t mode = fs::equivalent(entry.path(), execPath) ? 0100755 : 0100644;
            zip.addFile(rel, data.data(), data.size(), mode);
        }
    }
    zip.finish(outputPath);
}

/// @brief Verify the codesign signature of `appPath` using `codesign --verify --deep --strict`.
/// Only runs on Apple hosts; no-op on all other platforms.
void verifyMacOSBundleSignatureIfAvailable(const fs::path &appPath) {
#if defined(__APPLE__)
    runChecked({"codesign", "--verify", "--deep", "--strict", "--verbose=2", appPath.string()},
               "macOS code signature verification");
#else
    (void)appPath;
#endif
}

/// @brief Sign the .app bundle using `codesign` with the mode resolved from `pkg`.
/// Supports Developer ID (with optional notarization/stapling), ad-hoc, and no-op modes.
/// "ad-hoc" and "developer-id" modes throw on non-Apple hosts.
void signMacOSBundle(const fs::path &stageRoot,
                     const fs::path &appPath,
                     const fs::path &execPath,
                     const fs::path &projectRoot,
                     const PackageConfig &pkg) {
    validateMacOSSigningConfig(pkg);
    const std::string mode = resolveMacOSSignModeForHost(pkg);
    if (mode == "none" || mode == "preserve")
        return;

#if !defined(__APPLE__)
    (void)stageRoot;
    (void)appPath;
    (void)execPath;
    (void)projectRoot;
    (void)pkg;
    throw std::runtime_error("macOS signing mode '" + mode + "' requires running on macOS");
#else
    const bool developerId = mode == "developer-id";
    if (developerId && pkg.macosSignIdentity.empty())
        throw std::runtime_error("macOS Developer ID signing requires macos-sign-identity");
    if (!pkg.macosNotaryProfile.empty() && !developerId)
        throw std::runtime_error("macOS notarization requires macos-sign-mode developer-id");

    std::vector<std::string> args = {"codesign", "--force", "--sign",
                                     developerId ? pkg.macosSignIdentity : std::string("-")};
    if (developerId)
        args.push_back("--timestamp");
    if (pkg.macosHardenedRuntime || !pkg.macosNotaryProfile.empty()) {
        args.push_back("--options");
        args.push_back("runtime");
    }
    if (!pkg.macosEntitlements.empty()) {
        const fs::path entitlements =
            resolvePackageSourcePath(projectRoot, pkg.macosEntitlements, "macOS entitlements");
        args.push_back("--entitlements");
        args.push_back(entitlements.string());
    }
    args.push_back(appPath.string());
    runChecked(args, "macOS code signing");
    verifyMacOSBundleSignatureIfAvailable(appPath);

    if (!pkg.macosNotaryProfile.empty()) {
        const fs::path notaryZip = stageRoot / "notary-submit.zip";
        addStagedAppToZip(stageRoot, appPath, execPath, notaryZip.string());
        runChecked({"xcrun",
                    "notarytool",
                    "submit",
                    notaryZip.string(),
                    "--keychain-profile",
                    pkg.macosNotaryProfile,
                    "--wait"},
                   "macOS notarization");
        if (pkg.macosStaple) {
            runChecked({"xcrun", "stapler", "staple", appPath.string()},
                       "macOS notarization stapling");
            verifyMacOSBundleSignatureIfAvailable(appPath);
        }
    }
#endif
}

} // namespace

//=============================================================================
// MacOS Package Builder
//=============================================================================

/// @brief Build a macOS .app bundle inside a ZIP archive from the given build parameters.
/// Stages the bundle in a temp directory (exec, Resources, PkgInfo, Info.plist, ICNS),
/// optionally signs it with codesign, then packs the result into a ZIP at `params.outputPath`.
void buildMacOSPackage(const MacOSBuildParams &params) {
    const auto &pkg = params.pkgConfig;
    std::string displayName = pkg.displayName.empty() ? params.projectName : pkg.displayName;
    const std::string version = params.version.empty() ? "0.0.0" : params.version;
    validateBundleDisplayName(displayName);
    validateMacOSBundleIdentifier(pkg.identifier, "macOS bundle identifier");
    validateDottedNumericVersion(version, "macOS package version");
    if (!pkg.minOsMacos.empty())
        validateDottedNumericVersion(pkg.minOsMacos, "minimum macOS version");
    validatePackageFileAssociations(pkg.fileAssociations);
    validateMacOSSigningConfig(pkg);
    if (!fs::is_regular_file(params.executablePath))
        throw std::runtime_error("macOS package executable is not a regular file: " +
                                 params.executablePath);

    // Determine executable name (lowercase, no spaces)
    std::string execName = normalizeExecName(params.projectName);

    std::string appName = displayName + ".app";
    const fs::path stageRoot = uniqueTempPackagingDir("viper-macos-app-" + execName);
    TempDirGuard cleanup(stageRoot);
    fs::remove_all(stageRoot);

    const fs::path appPath = stageRoot / appName;
    const fs::path contentsDir = appPath / "Contents";
    const fs::path macosDir = contentsDir / "MacOS";
    const fs::path resourcesDir = contentsDir / "Resources";
    fs::create_directories(macosDir);
    fs::create_directories(resourcesDir);

    writeFileString(contentsDir / "PkgInfo",
                    generatePkgInfo(),
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                        fs::perms::others_read);

    const fs::path stagedExec = macosDir / execName;
    writeFileBytes(stagedExec,
                   readFile(params.executablePath),
                   fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                       fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read |
                       fs::perms::others_exec);

    std::string iconFileName;
    if (!pkg.iconPath.empty()) {
        fs::path iconSrc = resolvePackageSourcePath(params.projectRoot, pkg.iconPath, "package icon");
        if (!fs::is_regular_file(iconSrc))
            throw std::runtime_error("package icon not found: " + pkg.iconPath);
        auto srcImage = pngRead(iconSrc.string());
        auto icnsData = generateIcns(srcImage);
        iconFileName = execName;
        writeFileBytes(resourcesDir / (execName + ".icns"),
                       icnsData,
                       fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                           fs::perms::others_read);
    }

    PlistParams plistParams;
    plistParams.executableName = execName;
    plistParams.bundleId = pkg.identifier.empty() ? ("com.viper." + execName) : pkg.identifier;
    plistParams.bundleName = displayName;
    plistParams.version = version;
    plistParams.iconFile = iconFileName;
    plistParams.minOsVersion = pkg.minOsMacos;
    plistParams.fileAssociations = pkg.fileAssociations;
    writeFileString(contentsDir / "Info.plist",
                    generatePlist(plistParams),
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                        fs::perms::others_read);

    for (const auto &asset : pkg.assets) {
        fs::path srcPath = resolvePackageSourcePath(params.projectRoot, asset.sourcePath, "asset source path");
        std::string targetDir = sanitizePackageRelativePath(asset.targetPath, "asset target path");
        copyPackageAssetToResources(srcPath, params.projectRoot, resourcesDir, targetDir, asset.sourcePath);
    }

    signMacOSBundle(stageRoot, appPath, stagedExec, params.projectRoot, pkg);
    addStagedAppToZip(stageRoot, appPath, stagedExec, params.outputPath);
}

/// @brief Build a macOS `.pkg` installer for the staged toolchain using `pkgbuild`.
/// Stages files under `/usr/local/viper/`, creates symlinks in `/usr/local/bin/`,
/// then invokes `pkgbuild` to produce the final installer package.
void buildMacOSToolchainPackage(const MacOSToolchainBuildParams &params) {
    namespace fs = std::filesystem;
    validateToolchainInstallManifest(params.manifest);
    if (params.manifest.platform != "macos") {
        throw std::runtime_error("macOS toolchain package requires a macOS staged toolchain "
                                 "manifest, got '" +
                                 params.manifest.platform + "'");
    }
    if (params.manifest.version.empty())
        throw std::runtime_error("toolchain package version is required");
    const std::string version = params.manifest.version;
    const std::string pkgVersion =
        resolveMacOSToolchainPackageVersion(version, params.packageVersion);
    validateMacOSBundleIdentifier(params.identifier, "macOS package identifier");
    validateDebVersion(version, "macOS toolchain manifest version");

    const fs::path tmpRoot = uniqueTempPackagingDir("viper-macos-toolchain-" + version);
    TempDirGuard cleanup(tmpRoot);
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot / "root" / "usr" / "local" / "viper");
    fs::create_directories(tmpRoot / "root" / "usr" / "local" / "bin");

    const fs::path installRoot = tmpRoot / "root" / "usr" / "local" / "viper";
    for (const auto &file : params.manifest.files) {
        const fs::path dst = installRoot / fs::path(file.stagedRelativePath);
        fs::create_directories(dst.parent_path());
        if (file.symlink) {
            std::error_code ec;
            fs::remove(dst, ec);
            ec.clear();
            fs::create_symlink(fs::path(file.symlinkTarget), dst, ec);
            if (ec)
                throw std::runtime_error("cannot create package symlink '" + dst.string() +
                                         "' -> '" + file.symlinkTarget + "': " + ec.message());
            continue;
        }
        fs::copy_file(file.stagedAbsolutePath, dst, fs::copy_options::overwrite_existing);
        if (file.executable) {
            fs::permissions(dst,
                            fs::perms::owner_read | fs::perms::owner_write |
                                fs::perms::owner_exec | fs::perms::group_read |
                                fs::perms::group_exec | fs::perms::others_read |
                                fs::perms::others_exec,
                            fs::perm_options::replace);
        }
    }

    for (const auto &file : params.manifest.files) {
        if (file.kind != ToolchainFileKind::Binary)
            continue;
        const std::string name = fs::path(file.stagedRelativePath).filename().string();
        const fs::path linkPath = tmpRoot / "root" / "usr" / "local" / "bin" / name;
        std::error_code ec;
        fs::remove(linkPath, ec);
        ec.clear();
        fs::create_symlink(fs::path("../viper/bin") / name, linkPath, ec);
        if (ec) {
            throw std::runtime_error("cannot create package symlink '" + linkPath.string() +
                                     "': " + ec.message());
        }
    }

    const RunResult rr = run_process(
        {"pkgbuild",
         "--root",
         (tmpRoot / "root").string(),
         "--identifier",
         params.identifier,
         "--version",
         pkgVersion,
         params.outputPath});
    if (rr.exit_code != 0) {
        throw std::runtime_error("pkgbuild failed while generating macOS toolchain package:\n" +
                                 rr.out + rr.err);
    }
}

} // namespace viper::pkg
