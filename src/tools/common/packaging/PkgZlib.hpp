//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PkgZlib.hpp
// Purpose: Minimal RFC 1950 zlib framing around the packaging DEFLATE engine.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace viper::pkg {

std::vector<uint8_t> zlibCompress(const uint8_t *data, size_t len, int level = 6);
std::vector<uint8_t> zlibDecompress(const uint8_t *data, size_t len, size_t expectedSize);

} // namespace viper::pkg
