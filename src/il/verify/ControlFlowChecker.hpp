//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/verify/ControlFlowChecker.hpp
// Purpose: Core control-flow verification infrastructure -- validates basic
//          block structure, terminator placement, and CFG integrity. Provides
//          the iteration and dispatch framework (VerifyInstrFn callback) that
//          orchestrates per-instruction verification within a function.
// Key invariants:
//   - Every basic block must end with exactly one terminator.
//   - Control-flow transfers must respect block parameter signatures.
//   - Structural checks are separate from opcode-specific semantic validation.
// Ownership/Lifetime: Stateless free functions operating on caller-owned IL
//          structures and a caller-provided ostream for diagnostics.
// Links: il/verify/BlockMap.hpp, il/verify/TypeInference.hpp,
//        il/core/Opcode.hpp
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
using VerifyInstrFn =
    bool (*)(const il::core::Function &fn,
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
bool iterateBlockInstructions(
    VerifyInstrFn verifyInstrFn,
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
