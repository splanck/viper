//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/InstallerStub.hpp
// Purpose: Generate complete Windows installer and uninstaller stub code
//          using InstallerStubGen. Returns .text bytes + import list for
//          PEBuilder integration.
//
// Key invariants:
//   - Installer uses precomputed layout metadata and stored-overlay offsets to
//     extract files directly from the packaged ZIP overlay.
//   - Uninstaller uses the same layout metadata to delete installed files,
//     remove shortcuts, and unregister the app.
//   - Both stubs use Win32 APIs via IAT (no dynamic LoadLibrary).
//   - Bootstrap code is emitted as x86-64 machine code. ARM64 payload packages
//     currently use the same x86-64 bootstrap so the installer can run under
//     Windows-on-ARM emulation while still deploying an ARM64 application.
//
// Ownership/Lifetime:
//   - Pure functions returning result structs.
//
// Links: InstallerStubGen.hpp, PEBuilder.hpp, WindowsPackageBuilder.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "PEBuilder.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace viper::pkg {

enum class WindowsInstallRoot : uint64_t {
    InstallDir = 0,
    DesktopDir = 1,
    StartMenuDir = 2,
};

struct WindowsPackageDirEntry {
    WindowsInstallRoot root{WindowsInstallRoot::InstallDir};
    std::string relativePath;
};

struct WindowsPackageFileEntry {
    WindowsInstallRoot root{WindowsInstallRoot::InstallDir};
    std::string relativePath;
    uint64_t overlayDataOffset{0};
    uint64_t sizeBytes{0};
};

struct WindowsPackageLayout {
    std::string displayName;
    std::string installDirName;
    std::string version;
    std::string identifier;
    std::string publisher;
    std::string executableName;
    uint32_t overlayFileOffset{0};
    bool createDesktopShortcut{false};
    bool createStartMenuShortcut{false};
    std::vector<WindowsPackageDirEntry> installDirectories;
    std::vector<WindowsPackageDirEntry> uninstallDirectories;
    std::vector<WindowsPackageFileEntry> installFiles;
    std::vector<WindowsPackageFileEntry> uninstallFiles;
};

/// @brief Result of building an installer/uninstaller stub.
struct StubResult {
    std::vector<uint8_t> textSection; ///< Machine code for .text
    std::vector<uint8_t> stubData;    ///< Embedded string data (appended to .rdata)
    std::vector<PEImport> imports;    ///< DLL imports needed
    std::string peArch{"x64"};        ///< PE machine type to use for the bootstrap executable.
    uint32_t stubDataRVAOffset{0};    ///< Offset within .rdata where stubData starts
};

/// @brief Build the installer stub machine code.
///
/// @param layout Package layout and extraction metadata.
/// @param arch   Payload architecture ("x64" or "arm64").
/// @return StubResult with .text bytes, data, and import list.
StubResult buildInstallerStub(const WindowsPackageLayout &layout, const std::string &arch);

/// @brief Build the uninstaller stub machine code.
///
/// @param layout Package layout and uninstall metadata.
/// @param arch   Payload architecture ("x64" or "arm64").
/// @return StubResult with .text bytes, data, and import list.
StubResult buildUninstallerStub(const WindowsPackageLayout &layout, const std::string &arch);

} // namespace viper::pkg
