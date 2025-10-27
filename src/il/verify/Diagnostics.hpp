//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/verify/Diagnostics.hpp
//
// Summary:
//   Provides lightweight helpers shared by the verifier components for
//   constructing consistent diagnostic messages.  Centralising the formatting
//   logic keeps the verification passes focussed on semantic checks while
//   ensuring a uniform presentation for all reported issues.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Shared diagnostic helpers for the IL verifier.
/// @details Exposes inline utilities that assemble rule-oriented diagnostic
///          messages.  The helpers guarantee a consistent "[RULE:<name>]"
///          prefix so downstream tooling can classify failures deterministically.

#pragma once

#include <string>

/// @brief Format a rule-based diagnostic message with location information.
/// @param rule Name of the rule that triggered the diagnostic.
/// @param msg Descriptive message explaining the failure.
/// @param fn Name of the function containing the violation.
/// @param block Index of the basic block within the function.
/// @param insn Index of the instruction inside the block.
/// @return Formatted diagnostic string following the verifier convention.
inline std::string diag_rule_msg(const char *rule,
                                 const std::string &msg,
                                 const std::string &fn,
                                 int block,
                                 int insn)
{
    return "[RULE:" + std::string(rule) + "] " + msg + " at " + fn + ":" +
           std::to_string(block) + ":" + std::to_string(insn);
}

