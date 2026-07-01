//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PackageConfig.hpp
// Purpose: Data structures for package manifest configuration, parsed from
//          package-* directives in viper.project files.
//
// Key invariants:
//   - All paths in AssetEntry are relative to the project root directory.
//   - displayName defaults to the project name if package-name is absent.
//   - targetArchitectures defaults to host architecture if empty.
//
// Ownership/Lifetime:
//   - Value type, fully copyable/movable.
//
// Links: project_loader.hpp (embedded in ProjectConfig)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <string>
#include <vector>

namespace viper::pkg {

/// @brief A single asset entry: source file/dir -> target relative dir.
struct AssetEntry {
    std::string sourcePath; ///< Relative to project root.
    std::string targetPath; ///< Relative to install dir.
};

/// @brief A file association declaration.
struct FileAssoc {
    std::string extension;            ///< e.g. ".zia"
    std::string description;          ///< e.g. "Zia Source File"
    std::string mimeType;             ///< e.g. "text/x-zia"
    std::string openCommandArguments; ///< Optional Windows Open verb args before "%1".
};

/// @brief All package-related configuration from viper.project.
struct PackageConfig {
    std::string displayName;     ///< package-name (defaults to project name)
    std::string author;          ///< package-author
    std::string maintainerEmail; ///< package-maintainer-email (Debian `Maintainer: Name <email>`)
    std::string description;     ///< package-description
    std::string homepage;        ///< package-homepage
    std::string license;         ///< package-license (SPDX)
    std::string licenseFilePath; ///< package-license-file (relative path to full license text)
    std::string readmeFilePath;  ///< package-readme (relative path to packaged README text)
    std::string welcomeText;     ///< package-welcome (single-line installer/package summary text)
    std::string identifier;      ///< package-identifier (reverse DNS)
    std::string iconPath;        ///< package-icon (relative path to PNG)

    std::vector<AssetEntry> assets;          ///< Extra files to bundle (source -> target).
    std::vector<FileAssoc> fileAssociations; ///< File-type associations to register.

    bool shortcutDesktop{false}; ///< Create a desktop shortcut on install.
    bool shortcutMenu{true};     ///< Create a start-menu / app-menu entry on install.
    /// Permit Linux maintainer scripts to copy desktop shortcuts into existing home Desktop dirs.
    bool allowHomeDesktopShortcuts{false};

    std::string minOsWindows; ///< "10.0"
    std::string minOsMacos;   ///< "11.0"

    std::string macosSignMode;        ///< none, preserve, adhoc, or developer-id
    std::string macosSignIdentity;    ///< Developer ID Application identity
    std::string macosEntitlements;    ///< Entitlements plist path, project-relative
    bool macosHardenedRuntime{false}; ///< Enable the hardened runtime when signing.
    std::string macosNotaryProfile;   ///< notarytool keychain profile
    bool macosStaple{false};          ///< Staple the notarization ticket to the artifact.
    bool macosDisableHardenedRuntime{
        false};                       ///< Opt out of the otherwise default-on hardened runtime.
    int macosNotaryTimeoutSeconds{0}; ///< notarytool --timeout in seconds (0 = built-in 30m).
    std::string macosDmgBackground;   ///< macos-dmg-background (project-relative PNG).
    std::string macosDmgIcon;         ///< macos-dmg-icon (project-relative .icns volume icon).

    std::string windowsInstallScope;  ///< machine (default) or user
    std::string windowsInstallDir;    ///< Optional install directory override.
    std::string windowsPublisher;     ///< Optional Windows Publisher override for ARP/version info.
    std::string windowsWizardSummary; ///< Optional short wizard summary shown before install.
    std::vector<std::string> windowsDlls; ///< Extra DLL/data dependency paths to bundle beside app.
    bool windowsSign{false};              ///< Request Authenticode signing for Windows installers.
    bool windowsSignSet{false};           ///< True when windows-sign was specified.
    std::string windowsSignPfx;        ///< PFX certificate path, project-relative unless absolute.
    std::string windowsSignThumbprint; ///< Certificate store SHA-1 thumbprint for signtool /sha1.
    std::string windowsTimestampUrl;   ///< RFC3161 timestamp URL for signtool.
    std::string windowsSigntoolPath;   ///< signtool.exe path override.
    bool windowsSignNoVerify{false};   ///< Skip signtool verify after signing.

    std::vector<std::string> targetArchitectures; ///< "x64", "arm64"

    std::string category;             ///< package-category (e.g. "Game", "Development", "Utility")
    std::vector<std::string> depends; ///< package-depends (e.g. "libc6", "libx11-6")
    std::vector<std::string> rpmDepends; ///< package-rpm-depends (RPM Requires entries).
    std::string linuxStartupWmClass;     ///< linux-startup-wm-class for StartupWMClass=.
    std::string linuxKeywords;           ///< linux-keywords for freedesktop Keywords=.
    std::string appstreamId;             ///< linux-appstream-id for AppStream component metadata.

    std::string postInstallScript;  ///< Custom post-install script content
    std::string preUninstallScript; ///< Custom pre-uninstall script content
    /// Permit package lifecycle hooks to be emitted into installer maintainer scripts.
    bool allowInstallHooks{false};

    /// @brief Check if any package-* directives were specified.
    bool hasPackageConfig() const {
        return !displayName.empty() || !author.empty() || !description.empty() ||
               !homepage.empty() || !license.empty() || !licenseFilePath.empty() ||
               !readmeFilePath.empty() || !welcomeText.empty() || !identifier.empty() ||
               !iconPath.empty() || !assets.empty() || !fileAssociations.empty() ||
               shortcutDesktop || !shortcutMenu || allowHomeDesktopShortcuts ||
               !minOsWindows.empty() || !minOsMacos.empty() || !macosSignMode.empty() ||
               !macosSignIdentity.empty() || !macosEntitlements.empty() || macosHardenedRuntime ||
               !macosNotaryProfile.empty() || macosStaple || !macosDmgBackground.empty() ||
               !macosDmgIcon.empty() || !windowsInstallScope.empty() ||
               !windowsInstallDir.empty() || !windowsPublisher.empty() ||
               !windowsWizardSummary.empty() || !windowsDlls.empty() || windowsSignSet ||
               !windowsSignPfx.empty() || !windowsSignThumbprint.empty() ||
               !windowsTimestampUrl.empty() || !windowsSigntoolPath.empty() ||
               windowsSignNoVerify || !targetArchitectures.empty() || !category.empty() ||
               !depends.empty() || !rpmDepends.empty() || !linuxStartupWmClass.empty() ||
               !linuxKeywords.empty() || !appstreamId.empty() || !postInstallScript.empty() ||
               !preUninstallScript.empty() || allowInstallHooks || !maintainerEmail.empty() ||
               macosDisableHardenedRuntime || macosNotaryTimeoutSeconds != 0;
    }
};

} // namespace viper::pkg
