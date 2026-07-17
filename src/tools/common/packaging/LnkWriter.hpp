//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/LnkWriter.hpp
// Purpose: Generate Windows .lnk (shell link) shortcut files from scratch,
//          following the [MS-SHLLINK] binary format specification.
//
// Key invariants:
//   - ShellLinkHeader is always 76 bytes.
//   - LinkCLSID = {00021401-0000-0000-C000-000000000046}.
//   - HasLinkInfo + HasRelativePath + IsUnicode flags set.
//   - LinkInfo provides VolumeID + LocalBasePath for reliable resolution.
//   - String data uses UTF-16LE with 2-byte length prefix (character count).
//   - No LinkTargetIDList — LinkInfo + StringData provide reliable resolution.
//
// Ownership/Lifetime:
//   - Pure function returning byte vector.
//
// Links: WindowsPackageBuilder.hpp, [MS-SHLLINK] specification
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zanna::pkg {

/// @brief Parameters for generating a .lnk shortcut file.
struct LnkParams {
    std::string targetPath;  ///< Target executable path (e.g. "C:\\Program Files\\App\\app.exe")
    std::string workingDir;  ///< Working directory for the target.
    std::string arguments;   ///< Command-line arguments passed to the target executable.
    std::string description; ///< Shortcut description/comment.
    std::string iconPath;    ///< Icon file path (empty = use target).
    int32_t iconIndex{0};    ///< Icon index within icon file.
};

/// @brief Generate a Windows .lnk shortcut file.
///
/// Produces a valid [MS-SHLLINK] shell link with:
///   - ShellLinkHeader (76 bytes) with HasLinkInfo | HasName | HasRelativePath |
///     IsUnicode (and HasWorkingDir/HasArguments/HasIconLocation/HasExpString as
///     applicable).
///   - LinkInfo (VolumeID + LocalBasePath, ANSI and Unicode) for reliable
///     resolution.
///   - StringData: NAME_STRING, RELATIVE_PATH, and — when set — WORKING_DIR,
///     COMMAND_LINE_ARGUMENTS, and ICON_LOCATION.
///   - An EnvironmentVariableDataBlock when the target contains %VAR% references.
///   - No LinkTargetIDList (LinkInfo + StringData are sufficient to resolve).
///
/// @param params Shortcut parameters.
/// @return .lnk file bytes.
std::vector<uint8_t> generateLnk(const LnkParams &params);

} // namespace zanna::pkg
