//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/windows_installer/WindowsInstallerUpdate.hpp
// Purpose: Declare authenticated, opt-in Windows installer update discovery.
//
// Key invariants:
//   - Update manifests are HTTPS-only, same-origin, bounded, and RSA-SHA256 signed.
//   - Checking never downloads or executes an installer.
//   - The user explicitly chooses whether to open an authenticated release URL.
//
// Ownership/Lifetime: Results own every parsed string and retain no network handles.
//
// Links: WindowsInstallerUpdate.cpp, WindowsInstallerHost.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "WindowsInstallerHost.hpp"

#include <string>
#include <string_view>

namespace viper::installer {

enum class UpdateStatus { Unconfigured, Current, Available };

struct UpdateCheckResult {
    UpdateStatus status{UpdateStatus::Unconfigured};
    std::string currentVersion;
    std::string availableVersion;
    std::string channel;
    std::string architecture;
    std::string downloadUrl;
    std::string downloadSha256;
    std::string releaseNotesUrl;
};

/// @brief Parse and authenticate an already downloaded canonical update manifest.
UpdateCheckResult verifyUpdateManifest(const HostPackage &package, std::string_view manifestText);

/// @brief Download, bound, parse, and authenticate the configured update manifest.
UpdateCheckResult checkForUpdates(const HostPackage &package);

/// @brief Return deterministic UTF-16 JSON for CLI/automation output.
std::wstring updateResultJson(const UpdateCheckResult &result);

/// @brief Show the update result and optionally open its authenticated HTTPS URL.
void showUpdateResult(HINSTANCE instance,
                      const HostPackage &package,
                      const UpdateCheckResult &result);

} // namespace viper::installer
