// File: src/il/verify/InstructionChecker.hpp
// Purpose: Declares helpers that validate IL instructions during verification.
// Key invariants: Operates on instructions within a single basic block context.
// Ownership/Lifetime: Stateless functions relying on caller-managed storage.
// Links: docs/il-spec.md
#pragma once

#include "il/core/fwd.hpp"
#include "il/verify/TypeInference.hpp"
#include <ostream>
#include <unordered_map>

namespace il::core
{
struct Extern;
}

namespace il::verify
{

/// @brief Validate a single IL instruction that is not control-flow specific.
/// @param fn Enclosing function for diagnostics.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction to validate.
/// @param externs Map of available extern declarations keyed by name.
/// @param funcs Map of known functions keyed by name.
/// @param types Type inference helper for operand queries and result recording.
/// @param err Stream receiving diagnostics on failure.
/// @return True when the instruction satisfies operand and type rules.
bool verifyInstruction(const il::core::Function &fn,
                       const il::core::BasicBlock &bb,
                       const il::core::Instr &instr,
                       const std::unordered_map<std::string, const il::core::Extern *> &externs,
                       const std::unordered_map<std::string, const il::core::Function *> &funcs,
                       TypeInference &types,
                       std::ostream &err);

/// @brief Validate opcode-independent structural properties of @p instr.
/// @param fn Function used for diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction to verify.
/// @param err Stream receiving diagnostics on failure.
/// @return True if the instruction matches the metadata signature.
bool verifyOpcodeSignature(const il::core::Function &fn,
                           const il::core::BasicBlock &bb,
                           const il::core::Instr &instr,
                           std::ostream &err);

} // namespace il::verify
