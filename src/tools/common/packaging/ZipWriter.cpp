//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/ZipWriter.cpp
// Purpose: ZIP archive writer with Unix permissions. Produces valid archives
//          that preserve file mode bits when extracted on macOS/Linux.
//
// Key invariants:
//   - version_made_by byte layout: high byte = host OS (3=Unix), low = spec
//     version (20 = 2.0). So version_made_by = 0x031E = (3<<8)|30.
//     Actually PKWARE uses 20 for 2.0; we use 20.
//   - external_file_attributes: upper 16 bits = Unix mode, lower 16 = MS-DOS.
//   - Compression: DEFLATE (method 8) for entries > 64 bytes when it saves space.
//   - Symlinks: stored with typeflag in external attributes (0120000 prefix).
//
// Ownership/Lifetime:
//   - Internal buffer freed on destruction or after finish().
//
// Links: ZipWriter.hpp, PkgDeflate.hpp, rt_crc32.h
//
//===----------------------------------------------------------------------===//

#include "ZipWriter.hpp"
#include "PkgDeflate.hpp"

#include <cstring>
#include <ctime>
#include <fstream>
#include <stdexcept>

extern "C" {
uint32_t rt_crc32_compute(const uint8_t *data, size_t len);
}

namespace viper::pkg {

//=============================================================================
// ZIP Constants
//=============================================================================

static constexpr uint32_t kLocalHeaderSig = 0x04034B50;
static constexpr uint32_t kCentralHeaderSig = 0x02014B50;
static constexpr uint32_t kEndRecordSig = 0x06054B50;

static constexpr uint16_t kMethodStored = 0;
static constexpr uint16_t kMethodDeflate = 8;

static constexpr uint16_t kVersionNeeded = 20;
// Unix(3) in high byte, version 2.0 (20) in low byte
static constexpr uint16_t kVersionMadeBy = (3 << 8) | 20;

static constexpr int kLocalHeaderSize = 30;
static constexpr int kCentralHeaderSize = 46;
static constexpr int kEndRecordSize = 22;

//=============================================================================
// Helpers
//=============================================================================

static inline void putU16(uint8_t *p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

static inline void putU32(uint8_t *p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

//=============================================================================
// ZipWriter Implementation
//=============================================================================

ZipWriter::ZipWriter()
{
    buffer_.reserve(65536);
}

ZipWriter::~ZipWriter() = default;

void ZipWriter::writeBytes(const uint8_t *data, size_t len)
{
    buffer_.insert(buffer_.end(), data, data + len);
}

void ZipWriter::writeU16(uint16_t v)
{
    uint8_t buf[2];
    putU16(buf, v);
    writeBytes(buf, 2);
}

void ZipWriter::writeU32(uint32_t v)
{
    uint8_t buf[4];
    putU32(buf, v);
    writeBytes(buf, 4);
}

void ZipWriter::getDosTime(uint16_t &time, uint16_t &date)
{
    std::time_t now = std::time(nullptr);
    struct tm *t = std::localtime(&now);
    if (!t) {
        time = 0;
        date = 0x0021; // 1980-01-01
        return;
    }
    time = static_cast<uint16_t>(
        (t->tm_sec / 2) | (t->tm_min << 5) | (t->tm_hour << 11));
    date = static_cast<uint16_t>(
        t->tm_mday | ((t->tm_mon + 1) << 5) | ((t->tm_year - 80) << 9));
}

void ZipWriter::addFile(const std::string &name, const uint8_t *data,
                        size_t len, uint32_t unixMode)
{
    uint32_t crc = rt_crc32_compute(data, len);

    // Decide compression
    uint16_t method = kMethodStored;
    const uint8_t *writeData = data;
    size_t writeLen = len;
    std::vector<uint8_t> compressed;

    if (len > 64) {
        compressed = deflate(data, len);
        if (compressed.size() < len) {
            method = kMethodDeflate;
            writeData = compressed.data();
            writeLen = compressed.size();
        }
    }

    // Record entry
    Entry e;
    e.name = name;
    e.crc32 = crc;
    e.compressedSize = static_cast<uint32_t>(writeLen);
    e.uncompressedSize = static_cast<uint32_t>(len);
    e.method = method;
    getDosTime(e.modTime, e.modDate);
    e.localOffset = static_cast<uint32_t>(buffer_.size());
    // Unix mode in upper 16 bits, MS-DOS archive attribute in lower
    e.externalAttrs = (unixMode << 16);

    // Write local file header
    uint8_t lh[kLocalHeaderSize];
    putU32(lh + 0, kLocalHeaderSig);
    putU16(lh + 4, kVersionNeeded);
    putU16(lh + 6, 0); // General purpose flags
    putU16(lh + 8, method);
    putU16(lh + 10, e.modTime);
    putU16(lh + 12, e.modDate);
    putU32(lh + 14, crc);
    putU32(lh + 18, static_cast<uint32_t>(writeLen));
    putU32(lh + 22, static_cast<uint32_t>(len));
    putU16(lh + 26, static_cast<uint16_t>(name.size()));
    putU16(lh + 28, 0); // Extra field length

    writeBytes(lh, kLocalHeaderSize);
    writeBytes(reinterpret_cast<const uint8_t *>(name.data()), name.size());
    writeBytes(writeData, writeLen);

    entries_.push_back(std::move(e));
}

void ZipWriter::addFileString(const std::string &name,
                              const std::string &content, uint32_t unixMode)
{
    addFile(name, reinterpret_cast<const uint8_t *>(content.data()),
            content.size(), unixMode);
}

void ZipWriter::addDirectory(const std::string &name, uint32_t unixMode)
{
    std::string dirName = name;
    if (dirName.empty() || dirName.back() != '/')
        dirName += '/';

    Entry e;
    e.name = dirName;
    e.crc32 = 0;
    e.compressedSize = 0;
    e.uncompressedSize = 0;
    e.method = kMethodStored;
    getDosTime(e.modTime, e.modDate);
    e.localOffset = static_cast<uint32_t>(buffer_.size());
    // Unix dir mode in upper 16 bits, MS-DOS directory attribute (0x10) in lower
    e.externalAttrs = (unixMode << 16) | 0x10;

    uint8_t lh[kLocalHeaderSize];
    putU32(lh + 0, kLocalHeaderSig);
    putU16(lh + 4, kVersionNeeded);
    putU16(lh + 6, 0);
    putU16(lh + 8, kMethodStored);
    putU16(lh + 10, e.modTime);
    putU16(lh + 12, e.modDate);
    putU32(lh + 14, 0);
    putU32(lh + 18, 0);
    putU32(lh + 22, 0);
    putU16(lh + 26, static_cast<uint16_t>(dirName.size()));
    putU16(lh + 28, 0);

    writeBytes(lh, kLocalHeaderSize);
    writeBytes(reinterpret_cast<const uint8_t *>(dirName.data()),
               dirName.size());

    entries_.push_back(std::move(e));
}

void ZipWriter::addSymlink(const std::string &name, const std::string &target)
{
    // Symlinks store the target path as file data
    auto *data = reinterpret_cast<const uint8_t *>(target.data());
    size_t len = target.size();
    uint32_t crc = rt_crc32_compute(data, len);

    Entry e;
    e.name = name;
    e.crc32 = crc;
    e.compressedSize = static_cast<uint32_t>(len);
    e.uncompressedSize = static_cast<uint32_t>(len);
    e.method = kMethodStored;
    getDosTime(e.modTime, e.modDate);
    e.localOffset = static_cast<uint32_t>(buffer_.size());
    // Symlink: Unix mode 0120777 in upper 16 bits
    e.externalAttrs = (0120777U << 16);

    uint8_t lh[kLocalHeaderSize];
    putU32(lh + 0, kLocalHeaderSig);
    putU16(lh + 4, kVersionNeeded);
    putU16(lh + 6, 0);
    putU16(lh + 8, kMethodStored);
    putU16(lh + 10, e.modTime);
    putU16(lh + 12, e.modDate);
    putU32(lh + 14, crc);
    putU32(lh + 18, static_cast<uint32_t>(len));
    putU32(lh + 22, static_cast<uint32_t>(len));
    putU16(lh + 26, static_cast<uint16_t>(name.size()));
    putU16(lh + 28, 0);

    writeBytes(lh, kLocalHeaderSize);
    writeBytes(reinterpret_cast<const uint8_t *>(name.data()), name.size());
    writeBytes(data, len);

    entries_.push_back(std::move(e));
}

void ZipWriter::writeCentralDirectory()
{
    uint32_t cdOffset = static_cast<uint32_t>(buffer_.size());

    for (const auto &e : entries_) {
        uint8_t ch[kCentralHeaderSize];
        putU32(ch + 0, kCentralHeaderSig);
        putU16(ch + 4, kVersionMadeBy);
        putU16(ch + 6, kVersionNeeded);
        putU16(ch + 8, 0); // Flags
        putU16(ch + 10, e.method);
        putU16(ch + 12, e.modTime);
        putU16(ch + 14, e.modDate);
        putU32(ch + 16, e.crc32);
        putU32(ch + 20, e.compressedSize);
        putU32(ch + 24, e.uncompressedSize);
        putU16(ch + 28, static_cast<uint16_t>(e.name.size()));
        putU16(ch + 30, 0); // Extra field length
        putU16(ch + 32, 0); // Comment length
        putU16(ch + 34, 0); // Disk number start
        putU16(ch + 36, 0); // Internal file attributes
        putU32(ch + 38, e.externalAttrs);
        putU32(ch + 42, e.localOffset);

        writeBytes(ch, kCentralHeaderSize);
        writeBytes(reinterpret_cast<const uint8_t *>(e.name.data()),
                   e.name.size());
    }

    uint32_t cdSize = static_cast<uint32_t>(buffer_.size()) - cdOffset;

    // End of central directory record
    uint8_t eocd[kEndRecordSize];
    putU32(eocd + 0, kEndRecordSig);
    putU16(eocd + 4, 0); // Disk number
    putU16(eocd + 6, 0); // Disk with central directory
    putU16(eocd + 8, static_cast<uint16_t>(entries_.size()));
    putU16(eocd + 10, static_cast<uint16_t>(entries_.size()));
    putU32(eocd + 12, cdSize);
    putU32(eocd + 16, cdOffset);
    putU16(eocd + 20, 0); // Comment length

    writeBytes(eocd, kEndRecordSize);
}

void ZipWriter::finish(const std::string &path)
{
    writeCentralDirectory();

    std::ofstream out(path, std::ios::binary);
    if (!out)
        throw std::runtime_error("ZipWriter: failed to create " + path);

    out.write(reinterpret_cast<const char *>(buffer_.data()),
              static_cast<std::streamsize>(buffer_.size()));
    if (!out)
        throw std::runtime_error("ZipWriter: failed to write " + path);
}

std::vector<uint8_t> ZipWriter::finishToVector()
{
    writeCentralDirectory();
    return std::move(buffer_);
}

} // namespace viper::pkg
