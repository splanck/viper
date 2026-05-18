//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PkgHash.hpp
// Purpose: Small SHA helpers used by native package writers.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace viper::pkg {

std::array<uint8_t, 20> sha1Bytes(const uint8_t *data, size_t len);
std::string sha1Hex(const uint8_t *data, size_t len);

std::array<uint8_t, 32> sha256Bytes(const uint8_t *data, size_t len);
std::string sha256Hex(const uint8_t *data, size_t len);

} // namespace viper::pkg
