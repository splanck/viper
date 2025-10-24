// File: src/il/verify/DiagFormat.hpp
// Purpose: Declare shared helpers for formatting IL verifier diagnostics.
// Key invariants: Formatting helpers only inspect immutable IL structures.
// Ownership/Lifetime: Non-owning references to IL structures provided by callers.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/fwd.hpp"

#include <string>
#include <string_view>

namespace il::verify
{

/// @brief Format a diagnostic string describing a basic block.
/// @param fn Function owning the block used to provide context.
/// @param bb Basic block referenced by the diagnostic.
/// @param message Optional trailing text appended to the diagnostic.
/// @return Single-line diagnostic including the function and block label.
std::string formatBlockDiag(const il::core::Function &fn,
                            const il::core::BasicBlock &bb,
                            std::string_view message = {});

/// @brief Format a diagnostic string describing an instruction.
/// @param fn Function owning the instruction.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction rendered via makeSnippet for context.
/// @param message Optional trailing text appended to the diagnostic.
/// @return Single-line diagnostic with function, block, instruction snippet and message.
std::string formatInstrDiag(const il::core::Function &fn,
                            const il::core::BasicBlock &bb,
                            const il::core::Instr &instr,
                            std::string_view message = {});

} // namespace il::verify
