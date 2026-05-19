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
#include <ios>
#include <ostream>
#include <stdexcept>
#include <string>
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

/// @brief Add @p a + @p b into @p out, writing a writer-prefixed error on overflow.
/// @details The convention is shared by every writer so a "size_t overflow"
///          message identifies which writer (ELF/Mach-O/COFF) caught it and
///          which conceptual quantity (@p what) overflowed.
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

/// @brief Multiply @p a * @p b into @p out with the writer-prefixed overflow error.
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

/// @brief Align-up @p val to @p align with the writer-prefixed error on overflow
///        or non-power-of-two alignment.
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

/// @brief 64-bit overflow-checked addition for object-file fields like Mach-O VAs.
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

/// @brief Add a signed relocation addend to an unsigned section offset.
inline bool checkedSectionOffsetAddend(int64_t addend,
                                       size_t targetOffset,
                                       const char *writerName,
                                       const char *sectionName,
                                       size_t relocOffset,
                                       std::ostream &err,
                                       int64_t &out) {
    if (targetOffset > static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
        err << writerName << ": relocation in " << sectionName << " at offset "
            << relocOffset << " has a section-offset addend outside int64 range\n";
        return false;
    }
    const int64_t signedOffset = static_cast<int64_t>(targetOffset);
    if ((addend > 0 && signedOffset > std::numeric_limits<int64_t>::max() - addend) ||
        (addend < 0 && signedOffset < std::numeric_limits<int64_t>::min() - addend)) {
        err << writerName << ": relocation in " << sectionName << " at offset "
            << relocOffset << " has a section-offset addend outside int64 range\n";
        return false;
    }
    out = signedOffset + addend;
    return true;
}

/// @brief Write a complete byte buffer after checking streamsize can represent it.
inline bool checkedWriteAll(std::ostream &os,
                            const std::vector<uint8_t> &data,
                            const char *writerName,
                            const std::string &path,
                            std::ostream &err) {
    if (data.size() >
        static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
        err << writerName << ": output file '" << path
            << "' exceeds stream write size limit\n";
        return false;
    }
    if (!data.empty())
        os.write(reinterpret_cast<const char *>(data.data()),
                 static_cast<std::streamsize>(data.size()));
    if (!os) {
        err << writerName << ": write failed for " << path << "\n";
        return false;
    }
    return true;
}

/// Pad \p out with zeros until it reaches \p target bytes.
/// No-op if the buffer is already at or beyond the target.
inline void padTo(std::vector<uint8_t> &out, size_t target) {
    if (out.size() < target)
        out.resize(target, 0);
}

} // namespace viper::codegen::objfile
