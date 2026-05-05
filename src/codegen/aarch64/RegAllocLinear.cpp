//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/RegAllocLinear.cpp
// Purpose: Thin adapter that drives the AArch64 linear-scan register allocator.
//          Delegates all work to ra::LinearAllocator.
//
// Key invariants:
//   - After allocation all MReg operands have isPhys=true.
//   - Callee-saved register usage is recorded in fn.savedGPRs/savedFPRs.
//
// Ownership/Lifetime:
//   - Modifies MFunction in place; caller retains ownership.
//
// Links: codegen/aarch64/RegAllocLinear.hpp,
//        codegen/aarch64/ra/Allocator.hpp
//
//===----------------------------------------------------------------------===//

#include "RegAllocLinear.hpp"

#include "ra/Allocator.hpp"

namespace viper::codegen::aarch64 {

AllocationResult allocate(MFunction &fn, const TargetInfo &ti) {
    ra::LinearAllocator allocator(fn, ti);
    return allocator.run();
}

} // namespace viper::codegen::aarch64
