//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PkgPNG.hpp
// Purpose: PNG reading and writing for the packaging library. Used to read
//          source icon images and generate resized versions for ICNS/ICO.
//          Ported from src/runtime/graphics/rt_pixels.c with GC removed.
//
// Key invariants:
//   - Supports 8-bit RGB and RGBA PNG files only.
//   - Pixel data stored as RGBA bytes: [R, G, B, A, R, G, B, A, ...].
//   - Writer uses filter=0 (None) for simplicity.
//   - Reader handles all 5 PNG filter types.
//
// Ownership/Lifetime:
//   - PkgImage owns its pixel buffer (std::vector<uint8_t>).
//
// Links: src/runtime/graphics/rt_pixels.c (original), PkgDeflate.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace viper::pkg {

/// @brief Error thrown on PNG read/write failure.
class PNGError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// @brief Simple RGBA image buffer.
struct PkgImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels; // RGBA, 4 bytes per pixel, row-major

    /// @brief Get pixel pointer at (x, y).
    uint8_t *at(uint32_t x, uint32_t y) {
        return pixels.data() + (y * width + x) * 4;
    }

    const uint8_t *at(uint32_t x, uint32_t y) const {
        return pixels.data() + (y * width + x) * 4;
    }
};

/// @brief Read a PNG file into a PkgImage.
/// @param path File path.
/// @return Decoded image with RGBA pixel data.
/// @throws PNGError on invalid format or I/O failure.
PkgImage pngRead(const std::string &path);

/// @brief Read a PNG from memory.
/// @param data PNG file bytes.
/// @param len Length of data.
/// @return Decoded image.
/// @throws PNGError on invalid format.
PkgImage pngReadMemory(const uint8_t *data, size_t len);

/// @brief Write a PkgImage as PNG to a file.
/// @param path Output file path.
/// @param img Image to write.
/// @throws PNGError on I/O failure.
void pngWrite(const std::string &path, const PkgImage &img);

/// @brief Encode a PkgImage as PNG to a byte vector.
/// @param img Image to encode.
/// @return PNG file bytes.
std::vector<uint8_t> pngEncode(const PkgImage &img);

/// @brief Resize an image using bilinear interpolation.
/// @param src Source image.
/// @param newWidth Target width.
/// @param newHeight Target height.
/// @return Resized image.
PkgImage imageResize(const PkgImage &src, uint32_t newWidth, uint32_t newHeight);

} // namespace viper::pkg
