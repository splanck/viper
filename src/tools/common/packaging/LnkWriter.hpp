//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

namespace viper::pkg {

/// @brief Parameters for generating a .lnk shortcut file.
struct LnkParams {
    std::string targetPath;  ///< Target executable path (e.g. "C:\\Program Files\\App\\app.exe")
    std::string workingDir;  ///< Working directory for the target.
    std::string description; ///< Shortcut description/comment.
    std::string iconPath;    ///< Icon file path (empty = use target).
    int32_t iconIndex{0};    ///< Icon index within icon file.
};

/// @brief Generate a Windows .lnk shortcut file.
///
/// Produces a minimal but valid .lnk file with:
///   - ShellLinkHeader (76 bytes) with HasRelativePath | IsUnicode
///   - StringData: NAME_STRING, RELATIVE_PATH, WORKING_DIR
///   - No LinkTargetIDList or LinkInfo (simplifies generation).
///
/// @param params Shortcut parameters.
/// @return .lnk file bytes.
std::vector<uint8_t> generateLnk(const LnkParams &params);

} // namespace viper::pkg
