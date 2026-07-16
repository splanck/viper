//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/windows_installer/WindowsInstallerWizard.hpp
// Purpose: Declare the accessible native Windows setup configuration,
//          progress, and completion surfaces.
//
// Key invariants:
//   - Quiet mode never creates a window.
//   - Passive mode creates progress UI but never asks a question.
//   - Full mode returns all user choices through HostOptions before mutation.
//   - Controls use native system colors, fonts, keyboard navigation, and DPI.
//
// Ownership/Lifetime:
//   - Dialog functions retain no references after returning.
//
// Links: WindowsInstallerWizard.cpp, WindowsInstallerLifecycle.cpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "WindowsInstallerHost.hpp"

#include <functional>
#include <set>

namespace viper::installer {

/// @brief Collect full-UI install or maintenance choices.
/// @return false when the user cancels before mutation.
bool configureInstallerWizard(HINSTANCE instance,
                              const HostPackage &package,
                              const std::filesystem::path &initialDestination,
                              InstallScope initialScope,
                              const std::set<std::string> &initialComponents,
                              bool installationPresent,
                              HostOptions &options);

/// @brief Execute a lifecycle operation behind a cooperatively cancellable native progress dialog.
int runInstallerProgress(HINSTANCE instance,
                         const HostPackage &package,
                         Operation operation,
                         UiLevel uiLevel,
                         Logger &logger,
                         const std::function<int()> &work);

/// @brief Show the successful completion surface and collect one launch action.
void showInstallerFinish(HINSTANCE instance,
                         const HostPackage &package,
                         const std::filesystem::path &installRoot,
                         const std::set<std::string> &selectedComponents,
                         HostOptions &options);

} // namespace viper::installer
