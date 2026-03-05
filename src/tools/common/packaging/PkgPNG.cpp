//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PkgPNG.cpp
// Purpose: PNG read/write and bilinear image resize. Ported from
//          src/runtime/graphics/rt_pixels.c with GC dependencies removed.
//
// Key invariants:
//   - Reader supports 8-bit RGB (color_type=2) and RGBA (color_type=6).
//   - Reader handles all 5 PNG filter types (None, Sub, Up, Average, Paeth).
//   - Writer always uses RGBA (color_type=6) with filter=0.
//   - Zlib framing: CMF=0x78, FLG=0x01, + DEFLATE + Adler-32.
//   - CRC-32 per chunk uses rt_crc32_compute.
//
// Ownership/Lifetime:
//   - All returned images own their pixel data.
//
// Links: rt_pixels.c (original), PkgDeflate.hpp, rt_crc32.h
//
//===----------------------------------------------------------------------===//

#include "PkgPNG.hpp"
#include "PkgDeflate.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>

extern "C"
{
    uint32_t rt_crc32_compute(const uint8_t *data, size_t len);
}

namespace viper::pkg
{

//=============================================================================
// PNG Constants
//=============================================================================

static const uint8_t kPNGSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};

//=============================================================================
// Helpers
//=============================================================================

static uint32_t readBE32(const uint8_t *p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

static void writeBE32(uint8_t *p, uint32_t v)
{
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}

/// @brief PNG CRC-32 over type + data bytes
static uint32_t pngCRC(const uint8_t *data, size_t len)
{
    return rt_crc32_compute(data, len);
}

/// @brief Paeth predictor (RFC 2083)
static uint8_t paethPredict(uint8_t a, uint8_t b, uint8_t c)
{
    int p = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
    int pa = p > static_cast<int>(a) ? p - static_cast<int>(a) : static_cast<int>(a) - p;
    int pb = p > static_cast<int>(b) ? p - static_cast<int>(b) : static_cast<int>(b) - p;
    int pc = p > static_cast<int>(c) ? p - static_cast<int>(c) : static_cast<int>(c) - p;
    if (pa <= pb && pa <= pc)
        return a;
    if (pb <= pc)
        return b;
    return c;
}

/// @brief Compute Adler-32 checksum
static uint32_t adler32(const uint8_t *data, size_t len)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++)
    {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

//=============================================================================
// PNG Reader
//=============================================================================

PkgImage pngReadMemory(const uint8_t *data, size_t len)
{
    if (len < 8 || std::memcmp(data, kPNGSignature, 8) != 0)
        throw PNGError("PNG: invalid signature");

    uint32_t width = 0, height = 0;
    uint8_t colorType = 0;
    std::vector<uint8_t> idatBuf;
    size_t pos = 8;

    while (pos + 12 <= len)
    {
        uint32_t chunkLen = readBE32(data + pos);
        const uint8_t *chunkType = data + pos + 4;
        const uint8_t *chunkData = data + pos + 8;

        if (pos + 12 + chunkLen > len)
            break;

        if (std::memcmp(chunkType, "IHDR", 4) == 0 && chunkLen >= 13)
        {
            width = readBE32(chunkData);
            height = readBE32(chunkData + 4);
            uint8_t bitDepth = chunkData[8];
            colorType = chunkData[9];
            if (bitDepth != 8 || (colorType != 2 && colorType != 6))
                throw PNGError("PNG: unsupported format (need 8-bit RGB or RGBA)");
        }
        else if (std::memcmp(chunkType, "IDAT", 4) == 0)
        {
            idatBuf.insert(idatBuf.end(), chunkData, chunkData + chunkLen);
        }
        else if (std::memcmp(chunkType, "IEND", 4) == 0)
        {
            break;
        }

        pos += 12 + chunkLen;
    }

    if (width == 0 || height == 0 || idatBuf.size() < 6)
        throw PNGError("PNG: missing IHDR or IDAT");

    // IDAT is a zlib stream: 2-byte header + DEFLATE + 4-byte Adler-32
    size_t deflateLen = idatBuf.size() - 2 - 4;
    auto raw = inflate(idatBuf.data() + 2, deflateLen);

    int channels = (colorType == 6) ? 4 : 3;
    size_t stride = static_cast<size_t>(width) * channels;
    size_t expected = (stride + 1) * height;
    if (raw.size() < expected)
        throw PNGError("PNG: decompressed data too short");

    // Apply scanline filters
    std::vector<uint8_t> img(stride * height);
    for (uint32_t y = 0; y < height; y++)
    {
        uint8_t filter = raw[y * (stride + 1)];
        const uint8_t *src = raw.data() + y * (stride + 1) + 1;
        uint8_t *dst = img.data() + y * stride;
        const uint8_t *prev = (y > 0) ? img.data() + (y - 1) * stride : nullptr;

        for (size_t i = 0; i < stride; i++)
        {
            uint8_t rawByte = src[i];
            uint8_t a = (i >= static_cast<size_t>(channels)) ? dst[i - channels] : 0;
            uint8_t b = prev ? prev[i] : 0;
            uint8_t c = (prev && i >= static_cast<size_t>(channels)) ? prev[i - channels] : 0;

            switch (filter)
            {
                case 0:
                    dst[i] = rawByte;
                    break;
                case 1:
                    dst[i] = rawByte + a;
                    break;
                case 2:
                    dst[i] = rawByte + b;
                    break;
                case 3:
                    dst[i] = rawByte +
                             static_cast<uint8_t>((static_cast<int>(a) + static_cast<int>(b)) / 2);
                    break;
                case 4:
                    dst[i] = rawByte + paethPredict(a, b, c);
                    break;
                default:
                    throw PNGError("PNG: unknown filter type");
            }
        }
    }

    // Convert to RGBA
    PkgImage result;
    result.width = width;
    result.height = height;
    result.pixels.resize(width * height * 4);

    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            const uint8_t *px = img.data() + (y * stride) + x * channels;
            uint8_t *dst = result.pixels.data() + (y * width + x) * 4;
            dst[0] = px[0];                          // R
            dst[1] = px[1];                          // G
            dst[2] = px[2];                          // B
            dst[3] = (channels == 4) ? px[3] : 0xFF; // A
        }
    }

    return result;
}

PkgImage pngRead(const std::string &path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw PNGError("PNG: cannot open " + path);

    auto size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char *>(data.data()), size);
    if (!f)
        throw PNGError("PNG: failed to read " + path);

    return pngReadMemory(data.data(), data.size());
}

//=============================================================================
// PNG Writer
//=============================================================================

/// @brief Write a PNG chunk to a buffer.
static void writeChunk(std::vector<uint8_t> &buf, const char *type, const uint8_t *data, size_t len)
{
    // Length (big-endian)
    uint8_t lenBuf[4];
    writeBE32(lenBuf, static_cast<uint32_t>(len));
    buf.insert(buf.end(), lenBuf, lenBuf + 4);

    // Type + data (used for CRC calculation)
    size_t tdStart = buf.size();
    buf.insert(buf.end(),
               reinterpret_cast<const uint8_t *>(type),
               reinterpret_cast<const uint8_t *>(type) + 4);
    if (len > 0)
        buf.insert(buf.end(), data, data + len);

    // CRC over type + data
    uint32_t crc = pngCRC(buf.data() + tdStart, 4 + len);
    uint8_t crcBuf[4];
    writeBE32(crcBuf, crc);
    buf.insert(buf.end(), crcBuf, crcBuf + 4);
}

std::vector<uint8_t> pngEncode(const PkgImage &img)
{
    if (img.width == 0 || img.height == 0)
        throw PNGError("PNG: empty image");

    std::vector<uint8_t> result;
    result.reserve(img.width * img.height * 4 + 1024);

    // PNG signature
    result.insert(result.end(), kPNGSignature, kPNGSignature + 8);

    // IHDR
    uint8_t ihdr[13];
    writeBE32(ihdr, img.width);
    writeBE32(ihdr + 4, img.height);
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 6;  // color type RGBA
    ihdr[10] = 0; // compression
    ihdr[11] = 0; // filter
    ihdr[12] = 0; // interlace
    writeChunk(result, "IHDR", ihdr, 13);

    // Build raw scanlines with filter=0
    size_t stride = static_cast<size_t>(img.width) * 4;
    size_t rawLen = (stride + 1) * img.height;
    std::vector<uint8_t> raw(rawLen);

    for (uint32_t y = 0; y < img.height; y++)
    {
        raw[y * (stride + 1)] = 0; // Filter: None
        std::memcpy(raw.data() + y * (stride + 1) + 1, img.pixels.data() + y * stride, stride);
    }

    // Compress with DEFLATE
    auto deflated = deflate(raw.data(), rawLen);

    // Wrap in zlib stream: CMF + FLG + DEFLATE + Adler-32
    size_t zlibLen = 2 + deflated.size() + 4;
    std::vector<uint8_t> zlibData(zlibLen);
    zlibData[0] = 0x78; // CMF: deflate, window=32K
    zlibData[1] = 0x01; // FLG: no dict, check bits
    std::memcpy(zlibData.data() + 2, deflated.data(), deflated.size());

    uint32_t adler = adler32(raw.data(), rawLen);
    writeBE32(zlibData.data() + 2 + deflated.size(), adler);

    // IDAT
    writeChunk(result, "IDAT", zlibData.data(), zlibLen);

    // IEND
    writeChunk(result, "IEND", nullptr, 0);

    return result;
}

void pngWrite(const std::string &path, const PkgImage &img)
{
    auto data = pngEncode(img);

    std::ofstream out(path, std::ios::binary);
    if (!out)
        throw PNGError("PNG: cannot create " + path);

    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
    if (!out)
        throw PNGError("PNG: failed to write " + path);
}

//=============================================================================
// Bilinear Image Resize
//=============================================================================

PkgImage imageResize(const PkgImage &src, uint32_t newWidth, uint32_t newHeight)
{
    if (newWidth == 0)
        newWidth = 1;
    if (newHeight == 0)
        newHeight = 1;

    PkgImage result;
    result.width = newWidth;
    result.height = newHeight;
    result.pixels.resize(newWidth * newHeight * 4);

    if (src.width == 0 || src.height == 0)
    {
        std::memset(result.pixels.data(), 0, result.pixels.size());
        return result;
    }

    for (uint32_t y = 0; y < newHeight; y++)
    {
        // Map dest y to source y with 8-bit fractional part
        int64_t srcY256 = (static_cast<int64_t>(y) * src.height * 256) / newHeight;
        int64_t srcY = srcY256 >> 8;
        int64_t fracY = srcY256 & 0xFF;

        if (srcY >= src.height)
            srcY = src.height - 1;
        if (srcY < 0)
            srcY = 0;
        int64_t sy1 = (srcY + 1 < src.height) ? srcY + 1 : srcY;
        if (srcY >= static_cast<int64_t>(src.height) - 1)
            fracY = 255;

        for (uint32_t x = 0; x < newWidth; x++)
        {
            int64_t srcX256 = (static_cast<int64_t>(x) * src.width * 256) / newWidth;
            int64_t srcX = srcX256 >> 8;
            int64_t fracX = srcX256 & 0xFF;

            if (srcX >= src.width)
                srcX = src.width - 1;
            if (srcX < 0)
                srcX = 0;
            int64_t sx1 = (srcX + 1 < src.width) ? srcX + 1 : srcX;
            if (srcX >= static_cast<int64_t>(src.width) - 1)
                fracX = 255;

            // Four neighboring pixels (RGBA bytes)
            const uint8_t *p00 = src.at(srcX, srcY);
            const uint8_t *p10 = src.at(sx1, srcY);
            const uint8_t *p01 = src.at(srcX, sy1);
            const uint8_t *p11 = src.at(sx1, sy1);

            int64_t invFracX = 256 - fracX;
            int64_t invFracY = 256 - fracY;

            uint8_t *dst = result.at(x, y);
            for (int ch = 0; ch < 4; ch++)
            {
                int64_t v = (p00[ch] * invFracX * invFracY + p10[ch] * fracX * invFracY +
                             p01[ch] * invFracX * fracY + p11[ch] * fracX * fracY) >>
                            16;
                dst[ch] = static_cast<uint8_t>(v & 0xFF);
            }
        }
    }

    return result;
}

} // namespace viper::pkg
