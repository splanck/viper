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
//   - Installer reads ZIP overlay from own PE, extracts to Program Files.
//   - Uninstaller reads install.ini, deletes files, removes registry keys.
//   - Both stubs use Win32 APIs via IAT (no dynamic LoadLibrary).
//   - Generated code is x86-64 only (ARM64 support planned).
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

namespace viper::pkg
{

/// @brief Result of building an installer/uninstaller stub.
struct StubResult
{
    std::vector<uint8_t> textSection; ///< Machine code for .text
    std::vector<uint8_t> stubData;    ///< Embedded string data (appended to .rdata)
    std::vector<PEImport> imports;    ///< DLL imports needed
    uint32_t stubDataRVAOffset{0};    ///< Offset within .rdata where stubData starts
};

/// @brief Build the installer stub machine code.
///
/// The installer:
///   1. Finds own .exe path via GetModuleFileNameW
///   2. Opens self and reads the ZIP overlay (stored entries only)
///   3. Parses install.ini for display name, install dir, shortcut flags
///   4. Creates install directory under %ProgramFiles%
///   5. Extracts app/ files to the install directory
///   6. Copies .lnk shortcuts to Start Menu / Desktop
///   7. Writes Add/Remove Programs registry entry
///   8. Shows completion MessageBox
///
/// @param displayName  Application display name (for MessageBox, registry).
/// @param installDir   Subdirectory under Program Files.
/// @param arch         "x64" or "arm64".
/// @return StubResult with .text bytes, data, and import list.
StubResult buildInstallerStub(const std::string &displayName,
                              const std::string &installDir,
                              const std::string &arch);

/// @brief Build the uninstaller stub machine code.
///
/// The uninstaller:
///   1. Finds own directory via GetModuleFileNameW
///   2. Reads install.ini from same directory
///   3. Deletes all files in the install directory
///   4. Removes the install directory
///   5. Deletes Start Menu / Desktop shortcuts
///   6. Removes Add/Remove Programs registry entry
///   7. Schedules self-deletion
///   8. Shows completion MessageBox
///
/// @param displayName  Application display name.
/// @param arch         "x64" or "arm64".
/// @return StubResult with .text bytes, data, and import list.
StubResult buildUninstallerStub(const std::string &displayName, const std::string &arch);

} // namespace viper::pkg
