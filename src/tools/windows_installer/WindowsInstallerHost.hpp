//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/windows_installer/WindowsInstallerHost.hpp
// Purpose: Declare the native Windows installer host's package, command-line,
//          logging, and lifecycle interfaces.
//
// Key invariants:
//   - All user-visible and filesystem strings are UTF-16 after package parsing.
//   - The complete package overlay and metadata are verified before mutation.
//   - Lifecycle functions return documented Windows Installer-compatible codes.
//   - Logging never records secret-bearing command-line values.
//
// Ownership/Lifetime:
//   - HostPackage owns its executable and extracted archive buffers.
//   - Logger owns its file handle and is movable but not copyable.
//
// Links: WindowsInstallerHost.cpp, WindowsInstallerLifecycle.cpp,
//        WindowsInstallerWizard.cpp, WindowsInstallerMetadata.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "WindowsInstallerMetadata.hpp"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zanna::installer {

inline constexpr int kExitSuccess = 0;
inline constexpr int kExitInvalidCommandLine = 87;
inline constexpr int kExitUserCancelled = 1602;
inline constexpr int kExitFatalError = 1603;
inline constexpr int kExitAnotherInstallRunning = 1618;
inline constexpr int kExitNewerVersionInstalled = 1638;
inline constexpr int kExitRebootRequired = 3010;

/// @brief Lifecycle failure carrying a Windows Installer-compatible process code.
class InstallerError final : public std::runtime_error {
  public:
    InstallerError(int exitCode, std::string message)
        : std::runtime_error(std::move(message)), exitCode_(exitCode) {}

    int exitCode() const noexcept {
        return exitCode_;
    }

  private:
    int exitCode_;
};

enum class Operation {
    Auto,
    Install,
    Modify,
    Repair,
    Uninstall,
    Inspect,
    CheckUpdates,
    Help,
    SelfTest,
};

enum class UiLevel { Full, Passive, Quiet };
enum class InstallScope { User, Machine };
enum class ComponentPreset { Unspecified, Minimal, Typical, SDK, Complete };

struct HostOptions {
    Operation operation{Operation::Auto};
    UiLevel uiLevel{UiLevel::Full};
    std::optional<InstallScope> scope;
    std::filesystem::path destination;
    std::filesystem::path logPath;
    std::filesystem::path outputPath;
    std::set<std::string> selectedComponents;
    ComponentPreset componentPreset{ComponentPreset::Unspecified};
    bool componentsSpecified{false};
    std::optional<bool> addToPath;
    std::optional<bool> registerAssociations;
    std::optional<bool> createShortcuts;
    bool allowDowngrade{false};
    bool noRestart{false};
    bool closeApplications{false};
    bool elevatedWorker{false};
    bool uninstallWorker{false};
    DWORD handoffParentId{0};
    bool launchIDE{false};
    bool launchPrompt{false};
    bool openQuickstart{false};
    bool openSamples{false};
};

struct HostPackage {
    std::filesystem::path executablePath;
    std::vector<uint8_t> executableBytes;
    std::string executableSha256;
    size_t overlayOffset{0};
    size_t overlayLength{0};
    zanna::pkg::WindowsInstallerMetadata metadata;
    std::vector<uint8_t> payloadZip;
    std::vector<uint8_t> cleanupBytes;
    std::string licenseText;
    std::string readmeText;
    std::map<std::string, std::vector<uint8_t>, std::less<>> outerFileBytes;
};

class Logger {
  public:
    Logger() = default;
    ~Logger();
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger(Logger &&other) noexcept;
    Logger &operator=(Logger &&other) noexcept;

    void open(const std::filesystem::path &path);
    void info(std::wstring_view message);
    void warning(std::wstring_view message);
    void error(std::wstring_view message);
    void setProgressCallback(std::function<void(std::wstring_view)> callback);
    void setCancellationCallback(std::function<bool()> callback);
    bool cancellationRequested() const;

    const std::filesystem::path &path() const {
        return path_;
    }

  private:
    void write(std::wstring_view level, std::wstring_view message);
    HANDLE handle_{INVALID_HANDLE_VALUE};
    std::filesystem::path path_;
    std::function<void(std::wstring_view)> progressCallback_;
    std::function<bool()> cancellationCallback_;
};

std::wstring utf8ToWide(std::string_view text);
std::string wideToUtf8(std::wstring_view text);
std::wstring formatWindowsError(DWORD error);
std::wstring quoteCommandLineArgument(std::wstring_view argument);
std::filesystem::path currentExecutablePath();
std::filesystem::path defaultLogPath(std::string_view identifier);

HostOptions parseCommandLine(int argc, wchar_t **argv);
std::wstring commandLineHelp();
HostPackage loadHostPackage(const std::filesystem::path &executablePath);
std::wstring inspectPackageJson(const HostPackage &package);

/// @brief Compare supported dotted semantic versions using SemVer prerelease precedence.
int compareInstallerVersions(std::string_view left, std::string_view right);

int runLifecycle(HINSTANCE instance,
                 const HostPackage &package,
                 const HostOptions &options,
                 Logger &logger);

} // namespace zanna::installer
