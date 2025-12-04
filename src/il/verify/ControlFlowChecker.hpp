//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the core control-flow verification infrastructure for the
// IL verifier. It provides the fundamental building blocks for validating basic
// block structure, terminator placement, and control flow graph integrity within
// IL functions.
//
// The IL specification requires that every basic block ends with exactly one
// terminator instruction and that control flow transfers respect basic block
// parameter signatures. This file provides the iteration and dispatch framework
// that orchestrates verification of these invariants across all instructions
// within a function.
//
// Key Responsibilities:
// - Validate basic block parameter declarations and populate type environment
// - Check that terminators appear only at block ends (no dead code after)
// - Dispatch instruction verification to appropriate strategy handlers
// - Verify branch/conditional branch/return terminator semantics
// - Ensure control flow graph is well-formed with valid successor edges
//
// Design Rationale:
// The verification architecture separates control-flow checks from opcode-specific
// validation. This file focuses on structural properties (block parameters,
// terminator placement, successor validity) while delegating instruction semantics
// to specialized strategies. The VerifyInstrFn callback pattern enables flexible
// composition of verification passes.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Opcode.hpp"
#include "il/core/fwd.hpp"
#include "il/verify/BlockMap.hpp"
#include "il/verify/TypeInference.hpp"

#include <ostream>
#include <unordered_map>
#include <vector>

namespace il::core
{
struct Extern;
}

namespace il::verify
{

/// @brief Function pointer signature used by instruction iterators.
using VerifyInstrFn = bool (*)(const il::core::Function &fn,
                               const il::core::BasicBlock &bb,
                               const il::core::Instr &instr,
                               const BlockMap &blockMap,
                               const std::unordered_map<std::string, const il::core::Extern *> &externs,
                               const std::unordered_map<std::string, const il::core::Function *> &funcs,
                               TypeInference &types,
                               std::ostream &err);

/// @brief Determine whether an opcode terminates a basic block.
bool isTerminator(il::core::Opcode op);

/// @brief Validate incoming block parameters and populate the type environment.
bool validateBlockParams(const il::core::Function &fn,
                         const il::core::BasicBlock &bb,
                         TypeInference &types,
                         std::vector<unsigned> &paramIds,
                         std::ostream &err);

/// @brief Iterate instructions in a block and dispatch verification callbacks.
bool iterateBlockInstructions(VerifyInstrFn verifyInstrFn,
                              const il::core::Function &fn,
                              const il::core::BasicBlock &bb,
                              const BlockMap &blockMap,
                              const std::unordered_map<std::string, const il::core::Extern *> &externs,
                              const std::unordered_map<std::string, const il::core::Function *> &funcs,
                              TypeInference &types,
                              std::ostream &err);

/// @brief Validate that terminator placement within the block is well-formed.
bool checkBlockTerminators(const il::core::Function &fn,
                           const il::core::BasicBlock &bb,
                           std::ostream &err);

/// @brief Verify branch instruction argument structure.
bool verifyBr(const il::core::Function &fn,
              const il::core::BasicBlock &bb,
              const il::core::Instr &instr,
              const BlockMap &blockMap,
              TypeInference &types,
              std::ostream &err);

/// @brief Verify conditional branch argument structure.
bool verifyCBr(const il::core::Function &fn,
               const il::core::BasicBlock &bb,
               const il::core::Instr &instr,
               const BlockMap &blockMap,
               TypeInference &types,
               std::ostream &err);

/// @brief Verify return instruction matches function signature.
bool verifyRet(const il::core::Function &fn,
               const il::core::BasicBlock &bb,
               const il::core::Instr &instr,
               TypeInference &types,
               std::ostream &err);

} // namespace il::verify
