//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/TarWriter.cpp
// Purpose: Write USTAR tar archives for .deb data/control tarballs.
//
// Key invariants:
//   - Header is exactly 512 bytes (struct posix_header layout).
//   - Octal fields are NUL-terminated ASCII strings.
//   - Checksum computed with checksum field treated as 8 spaces (0x20).
//   - Data blocks padded with NUL bytes to 512-byte boundary.
//   - Archive ends with 2 x 512 zero blocks (1024 bytes of NUL).
//
// Ownership/Lifetime:
//   - Single-use writer, accumulates entries then outputs.
//
// Links: TarWriter.hpp, viperdos/user/libc/include/tar.h
//
//===----------------------------------------------------------------------===//

#include "TarWriter.hpp"

#include <cstring>
#include <stdexcept>

namespace viper::pkg
{

void TarWriter::addFile(
    const std::string &path, const uint8_t *data, size_t size, uint32_t mode, uint32_t mtime)
{
    Entry e;
    e.path = path;
    e.data.assign(data, data + size);
    e.mode = mode;
    e.mtime = mtime;
    e.typeflag = '0';
    entries_.push_back(std::move(e));
}

void TarWriter::addFileString(const std::string &path,
                              const std::string &content,
                              uint32_t mode,
                              uint32_t mtime)
{
    addFile(path, reinterpret_cast<const uint8_t *>(content.data()), content.size(), mode, mtime);
}

void TarWriter::addFileVec(const std::string &path,
                           const std::vector<uint8_t> &data,
                           uint32_t mode,
                           uint32_t mtime)
{
    addFile(path, data.data(), data.size(), mode, mtime);
}

void TarWriter::addDirectory(const std::string &path, uint32_t mode, uint32_t mtime)
{
    Entry e;
    e.path = path;
    if (!e.path.empty() && e.path.back() != '/')
        e.path.push_back('/');
    e.mode = mode;
    e.mtime = mtime;
    e.typeflag = '5';
    entries_.push_back(std::move(e));
}

void TarWriter::addSymlink(const std::string &path, const std::string &target, uint32_t mtime)
{
    Entry e;
    e.path = path;
    e.linkTarget = target;
    e.mode = 0777;
    e.mtime = mtime;
    e.typeflag = '2';
    entries_.push_back(std::move(e));
}

namespace
{

// Write an octal value as NUL-terminated ASCII into a fixed-width field.
// Format: (width-1) octal digits + NUL byte.
void writeOctal(uint8_t *field, size_t width, uint64_t value)
{
    // Fill with zeros first
    std::memset(field, '0', width - 1);
    field[width - 1] = '\0';

    // Write digits from right to left
    size_t pos = width - 2;
    if (value == 0)
    {
        field[pos] = '0';
        return;
    }
    while (value > 0 && pos < width)
    {
        field[pos] = static_cast<uint8_t>('0' + (value & 7));
        value >>= 3;
        if (pos == 0)
            break;
        pos--;
    }
}

// Write a NUL-terminated string into a fixed-width field.
void writeString(uint8_t *field, size_t width, const std::string &s)
{
    size_t len = std::min(s.size(), width);
    std::memcpy(field, s.data(), len);
    if (len < width)
        field[len] = '\0';
}

// Compute the USTAR checksum for a 512-byte header.
// The checksum field (offset 148, 8 bytes) is treated as spaces.
uint32_t computeChecksum(const uint8_t header[512])
{
    uint32_t sum = 0;
    for (int i = 0; i < 512; i++)
    {
        // Checksum field is at offset 148..155 — treat as spaces
        if (i >= 148 && i < 156)
            sum += ' ';
        else
            sum += header[i];
    }
    return sum;
}

} // namespace

std::vector<uint8_t> TarWriter::finish() const
{
    std::vector<uint8_t> out;

    // Estimate: per entry (512 header + data padded to 512) + 1024 end
    size_t est = 1024;
    for (const auto &e : entries_)
    {
        est += 512;
        if (!e.data.empty())
            est += ((e.data.size() + 511) / 512) * 512;
    }
    out.reserve(est);

    for (const auto &e : entries_)
    {
        // Build 512-byte USTAR header
        uint8_t hdr[512];
        std::memset(hdr, 0, 512);

        // Split path into prefix + name if > 100 chars
        std::string name = e.path;
        std::string prefix;
        if (name.size() > 100)
        {
            // Try to split at last '/' within first 155 chars
            size_t splitAt = name.rfind('/', 154);
            if (splitAt != std::string::npos && splitAt > 0)
            {
                prefix = name.substr(0, splitAt);
                name = name.substr(splitAt + 1);
            }
            if (name.size() > 100)
                throw std::runtime_error("tar path too long: " + e.path);
        }

        // name[100]
        writeString(hdr + 0, 100, name);

        // mode[8]
        writeOctal(hdr + 100, 8, e.mode);

        // uid[8], gid[8] — both 0
        writeOctal(hdr + 108, 8, 0);
        writeOctal(hdr + 116, 8, 0);

        // size[12]
        writeOctal(hdr + 124, 12, (e.typeflag == '0') ? e.data.size() : 0);

        // mtime[12]
        writeOctal(hdr + 136, 12, e.mtime);

        // chksum[8] — filled after all other fields
        std::memset(hdr + 148, ' ', 8);

        // typeflag
        hdr[156] = static_cast<uint8_t>(e.typeflag);

        // linkname[100]
        if (e.typeflag == '2')
            writeString(hdr + 157, 100, e.linkTarget);

        // magic[6] = "ustar\0"
        std::memcpy(hdr + 257, "ustar", 6);

        // version[2] = "00"
        hdr[263] = '0';
        hdr[264] = '0';

        // uname[32] = "root"
        writeString(hdr + 265, 32, "root");

        // gname[32] = "root"
        writeString(hdr + 297, 32, "root");

        // devmajor[8], devminor[8] — zero
        writeOctal(hdr + 329, 8, 0);
        writeOctal(hdr + 337, 8, 0);

        // prefix[155]
        if (!prefix.empty())
            writeString(hdr + 345, 155, prefix);

        // Compute and write checksum
        uint32_t cksum = computeChecksum(hdr);
        // Format: 6 octal digits + NUL + space
        char cksumStr[8];
        std::snprintf(cksumStr, sizeof(cksumStr), "%06o", cksum);
        std::memcpy(hdr + 148, cksumStr, 6);
        hdr[154] = '\0';
        hdr[155] = ' ';

        out.insert(out.end(), hdr, hdr + 512);

        // Write data blocks (padded to 512-byte boundary)
        if (e.typeflag == '0' && !e.data.empty())
        {
            out.insert(out.end(), e.data.begin(), e.data.end());
            // Pad to 512-byte boundary
            size_t remainder = e.data.size() % 512;
            if (remainder != 0)
            {
                size_t padBytes = 512 - remainder;
                out.resize(out.size() + padBytes, 0);
            }
        }
    }

    // Two zero-filled 512-byte end-of-archive blocks
    out.resize(out.size() + 1024, 0);

    return out;
}

} // namespace viper::pkg
