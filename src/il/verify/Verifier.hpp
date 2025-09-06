// File: src/il/verify/Verifier.hpp
// Purpose: Declares IL verifier that checks modules.
// Key invariants: None.
// Ownership/Lifetime: Verifier does not own modules.
// Links: docs/il-spec.md
#pragma once

#include "il/core/Module.hpp"
#include <ostream>
#include <unordered_map>
#include <unordered_set>

namespace il::verify
{

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
    static bool verifyExterns(const il::core::Module &m,
                              std::ostream &err,
                              std::unordered_map<std::string, const il::core::Extern *> &externs);
    static bool verifyGlobals(const il::core::Module &m,
                              std::ostream &err,
                              std::unordered_map<std::string, const il::core::Global *> &globals);
    static bool verifyFunction(
        const il::core::Function &fn,
        const std::unordered_map<std::string, const il::core::Extern *> &externs,
        const std::unordered_map<std::string, const il::core::Function *> &funcs,
        std::ostream &err);
    static bool verifyBlock(
        const il::core::Function &fn,
        const il::core::BasicBlock &bb,
        const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
        const std::unordered_map<std::string, const il::core::Extern *> &externs,
        const std::unordered_map<std::string, const il::core::Function *> &funcs,
        std::unordered_map<unsigned, il::core::Type> &temps,
        std::ostream &err);
    static bool verifyInstr(
        const il::core::Function &fn,
        const il::core::BasicBlock &bb,
        const il::core::Instr &in,
        const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
        const std::unordered_map<std::string, const il::core::Extern *> &externs,
        const std::unordered_map<std::string, const il::core::Function *> &funcs,
        std::unordered_map<unsigned, il::core::Type> &temps,
        std::unordered_set<unsigned> &defined,
        std::ostream &err);
};

} // namespace il::verify
