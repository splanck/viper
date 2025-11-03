//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/io/OperandParse_Type.cpp
// Purpose: Provide the per-kind parser for IL type immediates.
// Key invariants: Emits diagnostics consistent with the legacy OperandParser
//                 implementation while updating the instruction type in place.
// Ownership/Lifetime: Operates on parser-managed state without owning data.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the helper that parses type literal operands.
/// @details The helper mirrors the historical type parsing logic used by the
///          instruction parser, translating canonical textual spellings into
///          il::core::Type values and attaching them to the active instruction.

#include "viper/il/io/OperandParse.hpp"

#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/internal/io/ParserState.hpp"
#include "il/internal/io/ParserUtil.hpp"
#include "il/internal/io/TypeParser.hpp"

#include "support/diag_expected.hpp"

#include <cctype>
#include <sstream>
#include <string>
#include <string_view>

namespace viper::il::io
{
namespace
{

ParseResult syntaxError(Context &ctx, std::string message)
{
    ParseResult result;
    result.status = ::il::support::Expected<void>{::il::support::makeError(
        ctx.state.curLoc, ::il::io::formatLineDiag(ctx.state.lineNo, std::move(message)))};
    return result;
}

} // namespace

ParseResult parseTypeOperand(viper::parse::Cursor &cur, Context &ctx)
{
    cur.skipWs();
    const std::size_t beginOffset = cur.offset();
    const std::string_view rawToken =
        cur.consumeWhile([](char ch) { return !std::isspace(static_cast<unsigned char>(ch)); });

    if (rawToken.empty())
        return syntaxError(ctx, "missing type");

    std::string token(rawToken);
    if (!token.empty() && token.back() == ',')
        token.pop_back();

    token = ::il::io::trim(token);
    if (token.empty())
        return syntaxError(ctx, "missing type");

    bool ok = false;
    ::il::core::Type type = ::il::io::parseType(token, &ok);
    if (!ok)
    {
        std::ostringstream oss;
        oss << "unknown type '" << token << "'";
        return syntaxError(ctx, oss.str());
    }

    ctx.instr.type = type;

    // Position the cursor after the consumed token.
    cur.seek(beginOffset + rawToken.size());

    ParseResult result;
    return result;
}

} // namespace viper::il::io

