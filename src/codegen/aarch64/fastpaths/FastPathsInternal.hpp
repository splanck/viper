//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: codegen/aarch64/fastpaths/FastPathsInternal.hpp
// Purpose: Internal shared declarations for fast-path pattern matching.
// Key invariants: Fast paths match simple, common IL patterns for optimized
//                 lowering; each fast-path returns the lowered MFunction if
//                 matched, nullopt otherwise; output must be semantically
//                 identical to generic lowering; parameter registers are
//                 accessed via ABI-defined argument order.
// Ownership/Lifetime: FastPathContext borrows references to externally-owned
//                     state for the duration of a single fast-path attempt;
//                     it does not retain or manage any lifetimes.
// Links: codegen/aarch64/FastPaths.hpp, codegen/aarch64/LoweringContext.hpp,
//        docs/architecture.md
//
// This header contains shared helper functions, constants, and types used
// across the fast-path translation units. It is NOT part of the public API
// and should only be included by FastPaths_*.cpp files.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/FastPaths.hpp"
#include "codegen/aarch64/FrameBuilder.hpp"
#include "codegen/aarch64/InstrLowering.hpp"
#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/OpcodeMappings.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper::codegen::aarch64::fastpaths
{

//===----------------------------------------------------------------------===//
// Common Constants
//===----------------------------------------------------------------------===//

/// @brief Counter for generating unique trap labels.
/// @details Thread-local to avoid races during parallel compilation.
extern thread_local unsigned trapLabelCounter;

//===----------------------------------------------------------------------===//
// Context Structure
//===----------------------------------------------------------------------===//

/// @brief Context for fast-path lowering operations.
/// @details Groups commonly-used references and lambdas for convenient access.
struct FastPathContext
{
    const il::core::Function &fn;
    const TargetInfo &ti;
    FrameBuilder &fb;
    MFunction &mf;
    const std::array<PhysReg, kMaxGPRArgs> &argOrder;

    /// @brief Construct a fast-path context from the function being lowered.
    /// @param fn The IL function to lower.
    /// @param ti ABI and register information for the AArch64 target.
    /// @param fb Frame builder for stack slot allocation.
    /// @param mf Output MIR function being constructed.
    FastPathContext(const il::core::Function &fn,
                    const TargetInfo &ti,
                    FrameBuilder &fb,
                    MFunction &mf)
        : fn(fn), ti(ti), fb(fb), mf(mf), argOrder(ti.intArgOrder)
    {
    }

    /// @brief Get the MIR output block at the given index.
    MBasicBlock &bbOut(std::size_t idx)
    {
        return mf.blocks[idx];
    }

    /// @brief Get the register holding a value if it's a parameter.
    /// @param bb The basic block whose parameter list is checked.
    /// @param val The IL value to look up.
    /// @return The physical register if @p val is a parameter within GPR arg limits, nullopt otherwise.
    [[nodiscard]] std::optional<PhysReg> getValueReg(const il::core::BasicBlock &bb,
                                                     const il::core::Value &val) const
    {
        if (val.kind == il::core::Value::Kind::Temp)
        {
            int pIdx = indexOfParam(bb, val.id);
            if (pIdx >= 0 && static_cast<std::size_t>(pIdx) < kMaxGPRArgs)
            {
                return argOrder[static_cast<size_t>(pIdx)];
            }
        }
        return std::nullopt;
    }
};

//===----------------------------------------------------------------------===//
// Fast-Path Entry Points
//===----------------------------------------------------------------------===//

/// @brief Try fast-path for simple return patterns.
/// @details Handles: ret %paramN, ret const i64, ret const_str/addr_of
std::optional<MFunction> tryReturnFastPaths(FastPathContext &ctx);

/// @brief Try fast-path for memory operations.
/// @details Handles: alloca/store/load/ret pattern
std::optional<MFunction> tryMemoryFastPaths(FastPathContext &ctx);

/// @brief Try fast-path for integer arithmetic operations.
/// @details Handles: add/sub/mul/and/or/xor RR ops, RI ops, comparisons
std::optional<MFunction> tryIntArithmeticFastPaths(FastPathContext &ctx);

/// @brief Try fast-path for floating-point arithmetic operations.
/// @details Handles: fadd/fsub/fmul/fdiv RR ops
std::optional<MFunction> tryFPArithmeticFastPaths(FastPathContext &ctx);

/// @brief Try fast-path for type conversion operations.
/// @details Handles: zext1/trunc1, cast.si_narrow.chk, cast.fp_to_si.rte.chk
std::optional<MFunction> tryCastFastPaths(FastPathContext &ctx);

/// @brief Try fast-path for call operations.
/// @details Handles: call @callee(args...) feeding ret
std::optional<MFunction> tryCallFastPaths(FastPathContext &ctx);

//===----------------------------------------------------------------------===//
// Utility Functions
//===----------------------------------------------------------------------===//

/// @brief Result of single-block fast-path validation.
/// @details Provides references to the front block and output MIR block if
///          validation succeeds, allowing callers to avoid repeated lookups.
struct SingleBlockFastPathSetup
{
    const il::core::BasicBlock &bb; ///< Reference to the single block.
    MBasicBlock &bbMir;             ///< Reference to the output MIR block.
};

/// @brief Validate context for single-block fast-path patterns.
/// @details Checks that the function has exactly one block with at least
///          minInstrs instructions. Returns references to avoid repeated
///          lookups in calling code.
/// @param ctx Fast-path context to validate.
/// @param minInstrs Minimum required instruction count (default: 2).
/// @param requireParams If true, block must have at least one parameter.
/// @return Setup struct if valid, nullopt otherwise.
[[nodiscard]] inline std::optional<SingleBlockFastPathSetup> validateSingleBlockFastPath(
    FastPathContext &ctx, std::size_t minInstrs = 2, bool requireParams = true)
{
    if (ctx.fn.blocks.empty())
        return std::nullopt;
    if (ctx.fn.blocks.size() != 1)
        return std::nullopt;

    const auto &bb = ctx.fn.blocks.front();
    if (bb.instructions.size() < minInstrs)
        return std::nullopt;
    if (requireParams && bb.params.empty())
        return std::nullopt;

    return SingleBlockFastPathSetup{bb, ctx.bbOut(0)};
}

/// @brief Check if a basic block has side effects that prevent fast-path.
[[nodiscard]] inline bool hasSideEffects(const il::core::BasicBlock &bb)
{
    for (const auto &instr : bb.instructions)
    {
        switch (instr.op)
        {
            case il::core::Opcode::Ret:
            case il::core::Opcode::Br:
            case il::core::Opcode::CBr:
                // Control flow terminators don't count as side effects
                continue;
            case il::core::Opcode::Call:
            case il::core::Opcode::Store:
            case il::core::Opcode::Trap:
            case il::core::Opcode::TrapFromErr:
                return true;
            default:
                continue;
        }
    }
    return false;
}

} // namespace viper::codegen::aarch64::fastpaths
