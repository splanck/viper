//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/LnkWriter.cpp
// Purpose: Generate Windows .lnk (shell link) shortcut files.
//
// Key invariants:
//   - ShellLinkHeader: 76 bytes, HeaderSize=0x4C, CLSID at offset 4.
//   - LinkFlags at offset 20:
//       HasName          = 0x00000004
//       HasRelativePath  = 0x00000008
//       HasWorkingDir    = 0x00000010
//       HasIconLocation  = 0x00000040
//       IsUnicode        = 0x00000080
//   - StringData entries: 2-byte character count (uint16_t) + UTF-16LE chars.
//   - All numeric fields are little-endian.
//
// Ownership/Lifetime:
//   - Pure function, returns byte vector.
//
// Links: LnkWriter.hpp, [MS-SHLLINK] §2.1-2.4
//
//===----------------------------------------------------------------------===//

#include "LnkWriter.hpp"

#include <cstring>

namespace viper::pkg {

namespace {

void appendLE16(std::vector<uint8_t> &buf, uint16_t val)
{
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
}

void appendLE32(std::vector<uint8_t> &buf, uint32_t val)
{
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

/// @brief Append a UTF-16LE StringData entry: charCount(2) + UTF-16LE chars.
/// @note Simple ASCII-to-UTF-16LE conversion (sufficient for file paths).
void appendStringData(std::vector<uint8_t> &buf, const std::string &str)
{
    uint16_t charCount = static_cast<uint16_t>(str.size());
    appendLE16(buf, charCount);
    for (char c : str) {
        buf.push_back(static_cast<uint8_t>(c));
        buf.push_back(0); // High byte = 0 for ASCII
    }
}

} // namespace

std::vector<uint8_t> generateLnk(const LnkParams &params)
{
    std::vector<uint8_t> buf;
    buf.reserve(512);

    // ─── ShellLinkHeader (76 bytes) ────────────────────────────────────

    // HeaderSize (4 bytes) = 0x0000004C
    appendLE32(buf, 0x4C);

    // LinkCLSID (16 bytes) = {00021401-0000-0000-C000-000000000046}
    // Stored as: Data1(LE32) Data2(LE16) Data3(LE16) Data4(8 bytes, big-endian)
    appendLE32(buf, 0x00021401);
    appendLE16(buf, 0x0000);
    appendLE16(buf, 0x0000);
    buf.push_back(0xC0);
    buf.push_back(0x00);
    buf.push_back(0x00);
    buf.push_back(0x00);
    buf.push_back(0x00);
    buf.push_back(0x00);
    buf.push_back(0x00);
    buf.push_back(0x46);

    // LinkFlags (4 bytes)
    uint32_t linkFlags = 0;
    linkFlags |= 0x00000004; // HasName (NAME_STRING = description)
    linkFlags |= 0x00000008; // HasRelativePath
    linkFlags |= 0x00000080; // IsUnicode

    bool hasWorkDir = !params.workingDir.empty();
    bool hasIcon = !params.iconPath.empty();

    if (hasWorkDir)
        linkFlags |= 0x00000010; // HasWorkingDir
    if (hasIcon)
        linkFlags |= 0x00000040; // HasIconLocation

    appendLE32(buf, linkFlags);

    // FileAttributes (4 bytes) = FILE_ATTRIBUTE_NORMAL (0x80)
    appendLE32(buf, 0x00000080);

    // CreationTime (8 bytes) = 0
    appendLE32(buf, 0);
    appendLE32(buf, 0);

    // AccessTime (8 bytes) = 0
    appendLE32(buf, 0);
    appendLE32(buf, 0);

    // WriteTime (8 bytes) = 0
    appendLE32(buf, 0);
    appendLE32(buf, 0);

    // FileSize (4 bytes) = 0
    appendLE32(buf, 0);

    // IconIndex (4 bytes)
    appendLE32(buf, static_cast<uint32_t>(params.iconIndex));

    // ShowCommand (4 bytes) = SW_SHOWNORMAL (1)
    appendLE32(buf, 1);

    // HotKey (2 bytes) = 0
    appendLE16(buf, 0);

    // Reserved1 (2 bytes)
    appendLE16(buf, 0);

    // Reserved2 (4 bytes)
    appendLE32(buf, 0);

    // Reserved3 (4 bytes)
    appendLE32(buf, 0);

    // Header should be exactly 76 bytes at this point

    // ─── StringData ────────────────────────────────────────────────────
    // Order (when present): NAME_STRING, RELATIVE_PATH, WORKING_DIR, ICON_LOCATION

    // NAME_STRING (description)
    std::string name = params.description.empty() ? params.targetPath
                                                   : params.description;
    appendStringData(buf, name);

    // RELATIVE_PATH (the target path)
    appendStringData(buf, params.targetPath);

    // WORKING_DIR
    if (hasWorkDir)
        appendStringData(buf, params.workingDir);

    // ICON_LOCATION
    if (hasIcon)
        appendStringData(buf, params.iconPath);

    return buf;
}

} // namespace viper::pkg
