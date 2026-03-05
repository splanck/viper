//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/IconGenerator.hpp
// Purpose: Generate platform-specific icon formats from a source PNG image.
//          - macOS: ICNS container with embedded PNGs at multiple sizes.
//          - Windows: ICO container with embedded PNGs at multiple sizes.
//          - Linux: Individual PNG files at standard sizes.
//
// Key invariants:
//   - Source image should be at least 256x256 (ideally 1024x1024).
//   - ICNS uses big-endian fields; embeds PNG data directly (modern format).
//   - ICO uses little-endian fields; embeds PNG data for sizes >= 48px.
//   - Output is returned as byte vectors — no file I/O in the generator.
//
// Ownership/Lifetime:
//   - All output returned as std::vector<uint8_t> (caller-owned).
//
// Links: PkgPNG.hpp (resize + encode), MacOSPackageBuilder.hpp,
//        LinuxPackageBuilder.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "PkgPNG.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace viper::pkg
{

/// @brief Generate macOS ICNS icon data from a source image.
///
/// Produces entries for standard sizes (32, 64, 128, 256, 512, 1024) that
/// don't exceed the source image dimensions. Each entry contains a PNG.
///
/// ICNS format: "icns" magic(4) + total_size(4, big-endian)
///   + entries: type(4) + entry_size(4, big-endian) + PNG data
///
/// @param srcImage Source RGBA image.
/// @return ICNS file data.
std::vector<uint8_t> generateIcns(const PkgImage &srcImage);

/// @brief Generate Windows ICO icon data from a source image.
///
/// Produces entries for standard sizes (16, 24, 32, 48, 64, 128, 256) that
/// don't exceed the source image dimensions. Each entry embeds a PNG.
///
/// ICO format: ICONDIR(6) + ICONDIRENTRY[N](16 each) + PNG data blocks.
///
/// @param srcImage Source RGBA image.
/// @return ICO file data.
std::vector<uint8_t> generateIco(const PkgImage &srcImage);

/// @brief Generate multi-size PNG icons from a source image.
///
/// Returns a map of size -> PNG data for standard Linux icon sizes
/// (16, 32, 48, 128, 256) that don't exceed the source image dimensions.
///
/// @param srcImage Source RGBA image.
/// @return Map of pixel size to PNG file data.
std::map<uint32_t, std::vector<uint8_t>> generateMultiSizePngs(const PkgImage &srcImage);

} // namespace viper::pkg
