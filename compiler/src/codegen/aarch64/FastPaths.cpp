//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: codegen/aarch64/FastPaths.cpp
// Purpose: Fast-path pattern matching dispatcher for common IL patterns.
//
// Summary:
//   This file contains the main entry point for fast-path pattern matching.
//   It delegates to specialized fast-path handlers organized by category:
//   - FastPaths_Memory.cpp: Memory load/store patterns
//   - FastPaths_Return.cpp: Simple return patterns
//   - FastPaths_Arithmetic.cpp: Integer/FP arithmetic operations
//   - FastPaths_Cast.cpp: Type conversion operations
//   - FastPaths_Call.cpp: Call instruction lowering
//
// Fast-path invariants:
//   - Fast paths are tried in order; first match wins
//   - Each fast-path returns the lowered MFunction if matched, nullopt otherwise
//   - The order of fast-path attempts affects which patterns match first
//   - More specific patterns should be tried before more general ones
//
//===----------------------------------------------------------------------===//

#include "FastPaths.hpp"
#include "fastpaths/FastPathsInternal.hpp"

namespace viper::codegen::aarch64
{

std::optional<MFunction> tryFastPaths(const il::core::Function &fn,
                                      const TargetInfo &ti,
                                      FrameBuilder &fb,
                                      MFunction &mf)
{
    if (fn.blocks.empty())
        return std::nullopt;

    // Create fast-path context for all handlers
    fastpaths::FastPathContext ctx(fn, ti, fb, mf);

    // =========================================================================
    // Try fast-paths in order of specificity
    // =========================================================================
    // More specific patterns (memory, casts) are tried first, followed by
    // more general patterns (arithmetic, calls, returns).

    // Memory operations: alloca/store/load/ret pattern
    if (auto result = fastpaths::tryMemoryFastPaths(ctx))
        return result;

    // Type conversions: zext1/trunc1, narrowing casts, FP conversions
    if (auto result = fastpaths::tryCastFastPaths(ctx))
        return result;

    // Integer arithmetic: add/sub/mul/and/or/xor, comparisons, shifts
    if (auto result = fastpaths::tryIntArithmeticFastPaths(ctx))
        return result;

    // Floating-point arithmetic: fadd/fsub/fmul/fdiv
    if (auto result = fastpaths::tryFPArithmeticFastPaths(ctx))
        return result;

    // Call lowering: call @callee(args...) feeding ret
    if (auto result = fastpaths::tryCallFastPaths(ctx))
        return result;

    // Simple returns: ret %param, ret const, ret const_str/addr_of
    if (auto result = fastpaths::tryReturnFastPaths(ctx))
        return result;

    // No fast-path matched
    return std::nullopt;
}

} // namespace viper::codegen::aarch64
