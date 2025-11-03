//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/il/io/OperandParse.hpp
// Purpose: Declare operand-level parsing helpers shared by IL readers.
// Key invariants: Helpers preserve existing OperandParser diagnostics and
//                 operand population behaviour.
// Ownership/Lifetime: Operate on parser-managed instruction/state without
//                     taking ownership.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Declares reusable operand parsers keyed by operand parse kinds.
/// @details The helpers translate textual operand fragments referenced by
///          @ref il::core::Opcode metadata into Value or label payloads while
///          preserving legacy diagnostic messaging. They form the basis of a
///          gradual extraction away from the monolithic OperandParser
///          implementation.

#pragma once

#include "il/core/Value.hpp"
#include "support/diag_expected.hpp"
#include "viper/parse/Cursor.h"

#include <optional>
#include <string>

namespace il::core
{
struct Instr;
} // namespace il::core

namespace il::io::detail
{
struct ParserState;
} // namespace il::io::detail

namespace viper::il::io
{

/// @brief Shared parser context for operand helpers.
struct Context
{
    ::il::io::detail::ParserState &state; ///< Legacy parser state providing SSA maps and diagnostics.
    ::il::core::Instr &instr;             ///< Instruction under construction receiving parsed operands.
};

/// @brief Result bundle returned by operand-specific parsers.
struct ParseResult
{
    ::il::support::Expected<void> status{};              ///< Success/failure diagnostic container.
    std::optional<::il::core::Value> value{};            ///< Parsed Value operand when applicable.
    std::optional<std::string> label{};                ///< Parsed label text when applicable.

    /// @brief Query whether parsing succeeded.
    [[nodiscard]] bool ok() const
    {
        return static_cast<bool>(status);
    }

    /// @brief Query whether a Value operand was produced.
    [[nodiscard]] bool hasValue() const
    {
        return value.has_value();
    }

    /// @brief Query whether a label operand was produced.
    [[nodiscard]] bool hasLabel() const
    {
        return label.has_value();
    }

    /// @brief Report whether any operand payload was consumed.
    [[nodiscard]] bool consumed() const
    {
        return hasValue() || hasLabel();
    }
};

/// @brief Parse a general Value operand from the supplied cursor segment.
/// @param cur Cursor positioned at the start of the operand token.
/// @param ctx Shared parser context providing diagnostics and SSA mappings.
/// @return Result describing success and, on success, the parsed Value payload.
ParseResult parseValueOperand(viper::parse::Cursor &cur, Context &ctx);

/// @brief Parse a branch label operand from the supplied cursor segment.
/// @param cur Cursor positioned at the beginning of the label text.
/// @param ctx Shared parser context providing diagnostics and SSA mappings.
/// @return Result describing success and, on success, the parsed label string.
ParseResult parseLabelOperand(viper::parse::Cursor &cur, Context &ctx);

/// @brief Parse a type literal operand and attach it to the instruction context.
/// @param cur Cursor positioned at the beginning of the type token.
/// @param ctx Shared parser context providing diagnostics and instruction sink.
/// @return Result describing success or failure of the parse. No additional payloads
///         are populated beyond the status indicator.
ParseResult parseTypeOperand(viper::parse::Cursor &cur, Context &ctx);

/// @brief Parse a constant literal operand, producing a Value payload.
/// @param cur Cursor positioned at the start of the literal token.
/// @param ctx Shared parser context providing diagnostics and SSA mappings.
/// @return Result describing success and, on success, the parsed Value payload.
ParseResult parseConstOperand(viper::parse::Cursor &cur, Context &ctx);

} // namespace viper::il::io

