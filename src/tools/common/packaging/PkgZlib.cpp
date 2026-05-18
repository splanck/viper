//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PkgZlib.cpp
// Purpose: zlib framing for native XAR TOCs and per-file compression.
//
//===----------------------------------------------------------------------===//

#include "PkgZlib.hpp"

#include "PkgDeflate.hpp"

#include <stdexcept>

namespace viper::pkg {
namespace {

uint32_t adler32(const uint8_t *data, size_t len) {
    uint32_t a = 1;
    uint32_t b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

uint32_t readBE32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

} // namespace

std::vector<uint8_t> zlibCompress(const uint8_t *data, size_t len, int level) {
    if (len != 0 && data == nullptr)
        throw std::runtime_error("zlib: null data pointer for non-empty input");

    auto deflated = deflate(data, len, level);
    std::vector<uint8_t> out;
    out.reserve(2 + deflated.size() + 4);
    out.push_back(0x78);
    out.push_back(0x9c);
    out.insert(out.end(), deflated.begin(), deflated.end());
    const uint32_t sum = adler32(data, len);
    out.push_back(static_cast<uint8_t>((sum >> 24) & 0xffu));
    out.push_back(static_cast<uint8_t>((sum >> 16) & 0xffu));
    out.push_back(static_cast<uint8_t>((sum >> 8) & 0xffu));
    out.push_back(static_cast<uint8_t>(sum & 0xffu));
    return out;
}

std::vector<uint8_t> zlibDecompress(const uint8_t *data, size_t len, size_t expectedSize) {
    if (data == nullptr || len < 6)
        throw std::runtime_error("zlib: stream too small");
    const uint8_t cmf = data[0];
    const uint8_t flg = data[1];
    if ((cmf & 0x0fu) != 8u || ((static_cast<uint16_t>(cmf) << 8) + flg) % 31u != 0)
        throw std::runtime_error("zlib: invalid header");
    if ((flg & 0x20u) != 0)
        throw std::runtime_error("zlib: preset dictionaries are not supported");

    auto out = inflate(data + 2, len - 6, expectedSize);
    if (out.size() != expectedSize)
        throw std::runtime_error("zlib: uncompressed size mismatch");
    const uint32_t expectedAdler = readBE32(data + len - 4);
    const uint32_t actualAdler = adler32(out.data(), out.size());
    if (expectedAdler != actualAdler)
        throw std::runtime_error("zlib: Adler-32 mismatch");
    return out;
}

} // namespace viper::pkg
