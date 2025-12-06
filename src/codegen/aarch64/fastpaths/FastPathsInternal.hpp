//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: codegen/aarch64/fastpaths/FastPathsInternal.hpp
// Purpose: Internal shared declarations for fast-path pattern matching.
//
// Summary:
//   This header contains shared helper functions, constants, and types used
//   across the fast-path translation units. It is NOT part of the public API
//   and should only be included by FastPaths_*.cpp files.
//
// Fast-path invariants:
//   - Fast paths match simple, common IL patterns for optimized lowering
//   - Each fast-path returns the lowered MFunction if matched, nullopt otherwise
//   - Fast paths must produce correct code identical to generic lowering
//   - Parameter registers are accessed via ABI-defined argument order
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
