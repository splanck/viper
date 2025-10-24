//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the shared formatting helpers used when emitting verifier
// diagnostics.  Keeping the string assembly logic here allows the verification
// passes to remain focused on semantic checks while still providing rich,
// contextual error messages.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Formatting helpers for verifier diagnostics.
/// @details Exposes functions that build human-readable diagnostic prefixes for
///          blocks and instructions.  The helpers pull identifying information
///          (function name, block label, instruction snippet) and stitch it
///          together in a deterministic format consumed by the CLI tools.

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
