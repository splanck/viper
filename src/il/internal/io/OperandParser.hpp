//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/internal/io/OperandParser.hpp
// Purpose: Declares a helper for parsing textual IL operands.
// Key invariants: Requires ParserState to supply SSA mappings and diagnostics.
// Ownership/Lifetime: Operates on instructions owned by the parser caller.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/fwd.hpp"
#include "il/internal/io/ParserState.hpp"
#include "support/diag_expected.hpp"

#include <string>
#include <vector>

namespace il::io::detail
{

/// @brief Parses operand tokens for an instruction currently under construction.
class OperandParser
{
  public:
    /// @brief Construct an operand parser bound to the current parser state and instruction.
    /// @param state Parser state supplying temporary mappings and diagnostics.
    /// @param instr Instruction receiving parsed operands.
    OperandParser(ParserState &state, il::core::Instr &instr);

    /// @brief Parse a single value token (temporary, global, literal, etc.).
    /// @param token Token extracted from the instruction text.
    /// @return Parsed IL value or a diagnostic on failure.
    il::support::Expected<il::core::Value> parseValueToken(const std::string &token) const;

    /// @brief Parse the call operand syntax and append results to the instruction.
    /// @param text Remainder of the instruction line starting at the callee token.
    /// @return Empty on success; otherwise, a diagnostic describing the malformed call.
    il::support::Expected<void> parseCallOperands(const std::string &text);

    /// @brief Parse branch target lists of the form `label(args), label(args)`.
    /// @param text Textual segment containing the branch targets.
    /// @param expectedTargets Number of targets dictated by the opcode metadata.
    /// @return Empty on success; otherwise, a diagnostic describing the malformed targets.
    il::support::Expected<void> parseBranchTargets(const std::string &text, size_t expectedTargets);

    /// @brief Parse switch operand payloads consisting of a default target followed by cases.
    /// @param text Textual segment beginning with the default target.
    /// @return Empty on success; otherwise, a diagnostic describing the malformed switch payload.
    il::support::Expected<void> parseSwitchTargets(const std::string &text);

  private:
    il::support::Expected<std::vector<std::string>> splitCommaSeparated(const std::string &text,
                                                                        const char *context) const;

    il::support::Expected<void> parseBranchTarget(const std::string &segment,
                                                  std::string &label,
                                                  std::vector<il::core::Value> &args) const;

    il::support::Expected<void> checkBranchArgCount(const std::string &label,
                                                    size_t argCount) const;

    il::support::Expected<void> parseDefaultTarget(const std::string &segment);

    il::support::Expected<void> parseCaseSegment(const std::string &segment, const char *mnemonic);

    il::support::Expected<void> validateCaseArity(std::string label,
                                                  std::vector<il::core::Value> args);

    ParserState &state_;
    il::core::Instr &instr_;
};

} // namespace il::io::detail
