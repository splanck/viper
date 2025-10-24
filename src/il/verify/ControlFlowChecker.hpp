// File: src/il/verify/ControlFlowChecker.hpp
// Purpose: Declares helpers focused on IL control-flow verification.
// Key invariants: Terminators and block parameters follow IL structural rules.
// Ownership/Lifetime: Operates on caller-managed verifier context.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/Opcode.hpp"
#include "il/core/fwd.hpp"
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
             const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
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
    const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
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
              const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
              TypeInference &types,
              std::ostream &err);

/// @brief Verify conditional branch argument structure.
bool verifyCBr(const il::core::Function &fn,
               const il::core::BasicBlock &bb,
               const il::core::Instr &instr,
               const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
               TypeInference &types,
               std::ostream &err);

/// @brief Verify return instruction matches function signature.
bool verifyRet(const il::core::Function &fn,
               const il::core::BasicBlock &bb,
               const il::core::Instr &instr,
               TypeInference &types,
               std::ostream &err);

} // namespace il::verify
