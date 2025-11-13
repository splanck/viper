//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares diagnostic formatting utilities for the IL verifier. These
// helpers generate human-readable error messages that provide context about where
// verification failures occurred within the IL module hierarchy.
//
// Effective error reporting requires identifying not just what failed, but where
// the failure occurred. IL programs are organized as modules containing functions
// containing basic blocks containing instructions. The formatting utilities in
// this file construct diagnostic messages that include this hierarchical context,
// making verification errors easier to locate and fix.
//
// Key Responsibilities:
// - Format block-level diagnostics with function and block label context
// - Format instruction-level diagnostics with function, block, and instruction
// - Generate compact instruction snippets for error message inclusion
// - Provide consistent diagnostic formatting across all verifier components
//
// Design Notes:
// All formatters are stateless pure functions accepting const references to IL
// structures. They never modify the IL or take ownership. The formatted strings
// are designed for command-line output and follow a consistent pattern:
// "function 'name' block 'label': instruction 'snippet': message".
//
//===----------------------------------------------------------------------===//

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
