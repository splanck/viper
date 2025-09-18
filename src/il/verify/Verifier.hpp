// File: src/il/verify/Verifier.hpp
// Purpose: Declares IL verifier that checks modules.
// Key invariants: None.
// Ownership/Lifetime: Verifier does not own modules.
// Links: docs/il-spec.md
#pragma once

#include "il/core/Type.hpp"
#include "il/core/fwd.hpp"
#include <ostream>
#include <unordered_map>
#include <unordered_set>

namespace il::verify
{

class TypeInference;

/// @brief Verifies structural and type rules for a module.
class Verifier
{
  public:
    /// @brief Verify module @p m against the IL specification.
    /// @param m Module to verify.
    /// @param err Stream receiving diagnostic messages.
    /// @return True if verification succeeds; false otherwise.
    static bool verify(const il::core::Module &m, std::ostream &err);

  private:
    /// @brief Validate extern declarations for uniqueness and known signatures.
    /// @param m Module providing externs.
    /// @param err Stream receiving diagnostic messages.
    /// @param externs Map populated with externs by name for later lookups.
    /// @return True if all externs are well-formed; false otherwise.
    static bool verifyExterns(const il::core::Module &m,
                              std::ostream &err,
                              std::unordered_map<std::string, const il::core::Extern *> &externs);

    /// @brief Check global variables for duplicate definitions.
    /// @param m Module containing globals.
    /// @param err Stream receiving diagnostic messages.
    /// @param globals Map populated with globals by name for later lookups.
    /// @return True when all globals are unique; false otherwise.
    static bool verifyGlobals(const il::core::Module &m,
                              std::ostream &err,
                              std::unordered_map<std::string, const il::core::Global *> &globals);

    /// @brief Verify a function's signature and internal structure.
    /// @param fn Function to verify.
    /// @param externs Previously gathered extern signatures for calls.
    /// @param funcs Map of all functions for resolving references.
    /// @param err Stream receiving diagnostic messages.
    /// @return True if the function passes all checks; false otherwise.
    static bool verifyFunction(
        const il::core::Function &fn,
        const std::unordered_map<std::string, const il::core::Extern *> &externs,
        const std::unordered_map<std::string, const il::core::Function *> &funcs,
        std::ostream &err);

    /// @brief Validate a basic block's instructions and terminator.
    /// @param fn Enclosing function.
    /// @param bb Block under inspection.
    /// @param blockMap Map of labels to blocks for branch targets.
    /// @param externs Extern signatures for call checking.
    /// @param funcs Function map for call checking.
    /// @param temps Map of temporary ids to their inferred types.
    /// @param err Stream receiving diagnostic messages.
    /// @return True if the block is well-formed; false otherwise.
    static bool verifyBlock(
        const il::core::Function &fn,
        const il::core::BasicBlock &bb,
        const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
        const std::unordered_map<std::string, const il::core::Extern *> &externs,
        const std::unordered_map<std::string, const il::core::Function *> &funcs,
        std::unordered_map<unsigned, il::core::Type> &temps,
        std::ostream &err);

    /// @brief Validate a single instruction within a block.
    /// @param fn Enclosing function.
    /// @param bb Block containing the instruction.
    /// @param in Instruction to verify.
    /// @param blockMap Map of labels to blocks for branch targets.
    /// @param externs Extern signatures for call checking.
    /// @param funcs Function map for call checking.
    /// @param temps Map tracking temporary types.
    /// @param defined Set of temporaries defined so far.
    /// @param err Stream receiving diagnostic messages.
    /// @return True if the instruction is valid; false otherwise.
    static bool verifyInstr(
        const il::core::Function &fn,
        const il::core::BasicBlock &bb,
        const il::core::Instr &in,
        const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
        const std::unordered_map<std::string, const il::core::Extern *> &externs,
        const std::unordered_map<std::string, const il::core::Function *> &funcs,
        TypeInference &types,
        std::ostream &err);
};

} // namespace il::verify
