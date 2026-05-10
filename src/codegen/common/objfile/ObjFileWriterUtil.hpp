//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/ObjFileWriterUtil.hpp
// Purpose: Shared encoding and padding utilities for ELF, Mach-O, and COFF
//          object file writers. Eliminates triplicated appendLE/alignUp/padTo
//          definitions across the three writer implementations.
// Key invariants:
//   - appendLE* functions append in little-endian byte order
//   - alignUp requires align == 0 or power of two
//   - padTo is a no-op if the buffer already meets or exceeds the target
// Links: ElfWriter.cpp, MachOWriter.cpp, CoffWriter.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <vector>

namespace viper::codegen::objfile {

/// Append a 16-bit value in little-endian byte order.
inline void appendLE16(std::vector<uint8_t> &out, uint16_t val) {
    out.push_back(static_cast<uint8_t>(val));
    out.push_back(static_cast<uint8_t>(val >> 8));
}

/// Append a 32-bit value in little-endian byte order.
inline void appendLE32(std::vector<uint8_t> &out, uint32_t val) {
    out.push_back(static_cast<uint8_t>(val));
    out.push_back(static_cast<uint8_t>(val >> 8));
    out.push_back(static_cast<uint8_t>(val >> 16));
    out.push_back(static_cast<uint8_t>(val >> 24));
}

/// Append a 64-bit value in little-endian byte order.
inline void appendLE64(std::vector<uint8_t> &out, uint64_t val) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<uint8_t>(val >> (i * 8)));
}

/// Round \p val up to the next multiple of \p align.
/// \p align must be 0 (no-op) or a power of two.
inline size_t alignUp(size_t val, size_t align) {
    if (align == 0)
        return val;
    if ((align & (align - 1)) != 0)
        throw std::invalid_argument("alignUp: alignment must be a power of two");
    if (val > std::numeric_limits<size_t>::max() - (align - 1))
        throw std::length_error("alignUp: aligned value exceeds addressable size");
    return (val + align - 1) & ~(align - 1);
}

inline bool checkedAddSize(size_t a,
                           size_t b,
                           const char *writerName,
                           const char *what,
                           std::ostream &err,
                           size_t &out) {
    if (a > std::numeric_limits<size_t>::max() - b) {
        err << writerName << ": " << what << " exceeds addressable size\n";
        return false;
    }
    out = a + b;
    return true;
}

inline bool checkedMulSize(size_t a,
                           size_t b,
                           const char *writerName,
                           const char *what,
                           std::ostream &err,
                           size_t &out) {
    if (a != 0 && b > std::numeric_limits<size_t>::max() / a) {
        err << writerName << ": " << what << " exceeds addressable size\n";
        return false;
    }
    out = a * b;
    return true;
}

inline bool checkedAlignUpSize(size_t val,
                               size_t align,
                               const char *writerName,
                               const char *what,
                               std::ostream &err,
                               size_t &out) {
    if (align == 0) {
        out = val;
        return true;
    }
    if ((align & (align - 1)) != 0) {
        err << writerName << ": " << what << " alignment is not a power of two\n";
        return false;
    }
    if (val > std::numeric_limits<size_t>::max() - (align - 1)) {
        err << writerName << ": " << what << " exceeds addressable size after alignment\n";
        return false;
    }
    out = (val + align - 1) & ~(align - 1);
    return true;
}

inline bool checkedAddU64(uint64_t a,
                          uint64_t b,
                          const char *writerName,
                          const char *what,
                          std::ostream &err,
                          uint64_t &out) {
    if (a > std::numeric_limits<uint64_t>::max() - b) {
        err << writerName << ": " << what << " exceeds 64-bit object-file range\n";
        return false;
    }
    out = a + b;
    return true;
}

inline bool checkedMulU64(uint64_t a,
                          uint64_t b,
                          const char *writerName,
                          const char *what,
                          std::ostream &err,
                          uint64_t &out) {
    if (a != 0 && b > std::numeric_limits<uint64_t>::max() / a) {
        err << writerName << ": " << what << " exceeds 64-bit object-file range\n";
        return false;
    }
    out = a * b;
    return true;
}

inline bool checkedAlignUpU64(uint64_t val,
                              uint64_t align,
                              const char *writerName,
                              const char *what,
                              std::ostream &err,
                              uint64_t &out) {
    if (align == 0) {
        out = val;
        return true;
    }
    if ((align & (align - 1)) != 0) {
        err << writerName << ": " << what << " alignment is not a power of two\n";
        return false;
    }
    if (val > std::numeric_limits<uint64_t>::max() - (align - 1)) {
        err << writerName << ": " << what << " exceeds 64-bit range after alignment\n";
        return false;
    }
    out = (val + align - 1) & ~(align - 1);
    return true;
}

inline bool checkedSizeTFromU64(uint64_t value,
                                const char *writerName,
                                const char *what,
                                std::ostream &err,
                                size_t &out) {
    if (value > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        err << writerName << ": " << what << " exceeds addressable size\n";
        return false;
    }
    out = static_cast<size_t>(value);
    return true;
}

/// Pad \p out with zeros until it reaches \p target bytes.
/// No-op if the buffer is already at or beyond the target.
inline void padTo(std::vector<uint8_t> &out, size_t target) {
    if (out.size() < target)
        out.resize(target, 0);
}

} // namespace viper::codegen::objfile
