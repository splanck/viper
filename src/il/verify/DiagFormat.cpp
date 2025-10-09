// File: src/il/verify/DiagFormat.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Define shared helpers for formatting IL verifier diagnostics.
// Key invariants: Functions are pure and rely solely on immutable IL inputs.
// Ownership/Lifetime: Callers retain ownership of IL structures; helpers copy strings.
// Links: docs/il-guide.md#reference

#include "il/verify/DiagFormat.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/verify/TypeInference.hpp"

#include <sstream>

namespace il::verify
{

/// @brief Format a diagnostic string scoped to a basic block.
///
/// Produces "<function>:<block>[: <message>]" so verifier callers can attribute
/// issues to the relevant IR location without repeating formatting logic.
///
/// @param fn Function containing the block.
/// @param bb Block that triggered the diagnostic.
/// @param message Optional extra text appended to the identifier.
/// @return Human-readable diagnostic prefix.
std::string formatBlockDiag(const core::Function &fn,
                            const core::BasicBlock &bb,
                            std::string_view message)
{
    std::ostringstream oss;
    oss << fn.name << ":" << bb.label;
    if (!message.empty())
        oss << ": " << message;
    return oss.str();
}

/// @brief Format a diagnostic string scoped to a specific instruction.
///
/// Prefixes the function and block labels then includes a serialized instruction
/// snippet for additional context.  Optional messages are appended for clarity.
///
/// @param fn Function containing the instruction.
/// @param bb Block owning the instruction.
/// @param instr Instruction that triggered the diagnostic.
/// @param message Optional detailed message to append.
/// @return Human-readable diagnostic string referencing the instruction.
std::string formatInstrDiag(const core::Function &fn,
                            const core::BasicBlock &bb,
                            const core::Instr &instr,
                            std::string_view message)
{
    std::ostringstream oss;
    oss << fn.name << ":" << bb.label << ": " << makeSnippet(instr);
    if (!message.empty())
        oss << ": " << message;
    return oss.str();
}

} // namespace il::verify

