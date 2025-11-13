//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares validation helpers for branch and return terminator
// instructions used by the IL verifier. These functions ensure that control
// flow transfer instructions maintain structural invariants required by the
// IL specification.
//
// The verifier must ensure that branch instructions (br, cbr, switch_i32)
// correctly reference valid basic block targets with properly typed arguments
// that match the target block's parameter signature. Similarly, return
// instructions must produce values matching the function's declared return
// type.
//
// Key Responsibilities:
// - Verify unconditional branches provide correct arguments for target blocks
// - Validate conditional branches have boolean conditions and matching branches
// - Ensure switch_i32 has i32 scrutinee with valid case/default targets
// - Confirm return instructions match function signature (void or typed value)
//
// Design Notes:
// All verification functions follow the Expected-based error reporting pattern,
// accepting a TypeInference context to resolve operand types and record results.
// The functions are stateless and operate purely on the provided verifier state
// without maintaining any internal caches or ownership of IL structures.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support/diag_expected.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace il::core
{
struct BasicBlock;
struct Function;
struct Instr;
} // namespace il::core

namespace il::verify
{
class TypeInference;

il::support::Expected<void> verifyBr_E(
    const il::core::Function &fn,
    const il::core::BasicBlock &bb,
    const il::core::Instr &instr,
    const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
    TypeInference &types);

il::support::Expected<void> verifyCBr_E(
    const il::core::Function &fn,
    const il::core::BasicBlock &bb,
    const il::core::Instr &instr,
    const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
    TypeInference &types);

il::support::Expected<void> verifySwitchI32_E(
    const il::core::Function &fn,
    const il::core::BasicBlock &bb,
    const il::core::Instr &instr,
    const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
    TypeInference &types);

il::support::Expected<void> verifyRet_E(const il::core::Function &fn,
                                        const il::core::BasicBlock &bb,
                                        const il::core::Instr &instr,
                                        TypeInference &types);

} // namespace il::verify
