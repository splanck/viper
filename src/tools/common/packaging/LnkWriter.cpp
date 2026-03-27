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

namespace viper::pkg
{

namespace
{

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
    for (char c : str)
    {
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
    linkFlags |= 0x00000002; // HasLinkInfo
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

    // ─── LinkInfo (§2.3) ──────────────────────────────────────────────
    // Provides VolumeID + LocalBasePath for reliable shortcut resolution.
    // Structure: LinkInfoSize(4) + LinkInfoHeaderSize(4) + LinkInfoFlags(4)
    //          + VolumeIDOffset(4) + LocalBasePathOffset(4)
    //          + CommonNetworkRelativeLinkOffset(4) + CommonPathSuffixOffset(4)
    //          + VolumeID + LocalBasePath(NUL) + CommonPathSuffix(NUL)
    {
        // Build LinkInfo in a temporary buffer, then append
        std::vector<uint8_t> li;

        // LinkInfoHeaderSize = 0x1C (28 bytes, minimum for §2.3)
        uint32_t liHeaderSize = 0x1C;

        // VolumeID structure: VolumeIDSize(4) + DriveType(4) + DriveSerialNumber(4)
        //                   + VolumeLabelOffset(4) + VolumeLabel("C:\0")
        // DriveType = DRIVE_FIXED (3)
        std::vector<uint8_t> volumeId;
        appendLE32(volumeId, 0);  // VolumeIDSize placeholder
        appendLE32(volumeId, 3);  // DriveType = DRIVE_FIXED
        appendLE32(volumeId, 0);  // DriveSerialNumber = 0
        appendLE32(volumeId, 16); // VolumeLabelOffset = 16 (after this header)
        volumeId.push_back(0);    // Volume label = "" (NUL terminated)
        // Fill in VolumeIDSize
        uint32_t volIdSize = static_cast<uint32_t>(volumeId.size());
        volumeId[0] = static_cast<uint8_t>(volIdSize & 0xFF);
        volumeId[1] = static_cast<uint8_t>((volIdSize >> 8) & 0xFF);
        volumeId[2] = static_cast<uint8_t>((volIdSize >> 16) & 0xFF);
        volumeId[3] = static_cast<uint8_t>((volIdSize >> 24) & 0xFF);

        // LocalBasePath: the target path as ANSI (NUL terminated)
        std::string localBasePath = params.targetPath;
        // Ensure NUL terminator
        std::vector<uint8_t> lbp(localBasePath.begin(), localBasePath.end());
        lbp.push_back(0);

        // CommonPathSuffix: empty (NUL terminated)
        std::vector<uint8_t> cps = {0};

        // Calculate offsets
        uint32_t volumeIdOffset = liHeaderSize;
        uint32_t localBasePathOffset = volumeIdOffset + volIdSize;
        uint32_t commonNetworkOffset = 0; // Not present
        uint32_t commonPathSuffixOffset = localBasePathOffset + static_cast<uint32_t>(lbp.size());
        uint32_t linkInfoSize = commonPathSuffixOffset + static_cast<uint32_t>(cps.size());

        // Write LinkInfo header
        appendLE32(li, linkInfoSize);
        appendLE32(li, liHeaderSize);
        appendLE32(li, 0x00000001); // LinkInfoFlags: VolumeIDAndLocalBasePath
        appendLE32(li, volumeIdOffset);
        appendLE32(li, localBasePathOffset);
        appendLE32(li, commonNetworkOffset);
        appendLE32(li, commonPathSuffixOffset);

        // VolumeID
        li.insert(li.end(), volumeId.begin(), volumeId.end());

        // LocalBasePath
        li.insert(li.end(), lbp.begin(), lbp.end());

        // CommonPathSuffix
        li.insert(li.end(), cps.begin(), cps.end());

        buf.insert(buf.end(), li.begin(), li.end());
    }

    // ─── StringData ────────────────────────────────────────────────────
    // Order (when present): NAME_STRING, RELATIVE_PATH, WORKING_DIR, ICON_LOCATION

    // NAME_STRING (description)
    std::string name = params.description.empty() ? params.targetPath : params.description;
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
