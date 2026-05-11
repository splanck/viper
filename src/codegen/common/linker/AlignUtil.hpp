//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/AlignUtil.hpp
// Purpose: Shared alignment utility for the native linker subsystem.
// Key invariants:
//   - align must be 0 or a power of two
//   - align=0 is treated as no-op (returns val unchanged)
// Links: codegen/common/linker/SectionMerger.cpp,
//        codegen/common/linker/ElfExeWriter.cpp,
//        codegen/common/linker/MachOExeWriter.cpp,
//        codegen/common/linker/PeExeWriter.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cassert>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace viper::codegen::linker {

/// Round \p val up to the next multiple of \p align.
/// \p align must be 0 (no-op) or a power of two.
inline size_t alignUp(size_t val, size_t align) {
    if (align == 0)
        return val;
    if ((align & (align - 1)) != 0)
        throw std::invalid_argument("alignUp: alignment must be a power of two");
    if (val > std::numeric_limits<size_t>::max() - (align - 1))
        throw std::length_error("alignUp: aligned value exceeds addressable size");
    assert((align & (align - 1)) == 0 && "alignUp: alignment must be a power of two");
    return (val + align - 1) & ~(align - 1);
}

} // namespace viper::codegen::linker
