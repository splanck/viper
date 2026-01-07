//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/io/FunctionParser_Body.cpp
// Purpose: Implementation of function body and basic block parsing. Handles
//          block labels, parameters, instructions, and .loc directives.
// Key invariants: Maintains SSA identifier uniqueness across blocks.
// Ownership/Lifetime: Populates blocks directly within the current function.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/internal/io/FunctionParser.hpp"
#include "il/internal/io/FunctionParser_Internal.hpp"
#include "il/internal/io/InstrParser.hpp"
#include "il/internal/io/TypeParser.hpp"

#include <array>
#include <cstdint>
#include <sstream>
#include <unordered_set>

namespace il::io::detail
{

namespace
{

/// @brief Normalises diagnostics captured from instruction parsing.
///
/// The instruction parser reports errors prefixed with "error: " and terminated by
/// trailing newlines. This helper strips that prefix and trailing newline/carriage
/// returns so that downstream diagnostics emitted through @ref
/// il::support::printDiag are consistent across call sites.
///
/// @param line Text of one instruction, in the same format emitted by the IL
/// serializer (including optional `%temp =` leading assignments).
/// @param st Parser state mutated for each decoded instruction; the helper
/// forwards to parseInstruction(), which may extend temporary mappings, update
/// pending branch bookkeeping, and capture diagnostic locations.
/// @return Empty on success; otherwise, a diagnostic normalised via
/// stripCapturedDiagMessage().
Expected<void> parseInstructionShim_E(const std::string &line, ParserState &st)
{
    std::ostringstream capture;
    if (parseInstruction(line, st, capture))
        return {};
    auto message = stripCapturedDiagMessage(capture.str());
    return Expected<void>{makeError(st.curLoc, std::move(message))};
}

Expected<void> expect(parser_impl::ParserState &state, TokenKind want, std::string_view what)
{
    if (state.ts && state.ts->kind() == want)
        return {};

    std::ostringstream oss;
    oss << "unexpected " << describeTokenKind(state.ts ? state.ts->kind() : TokenKind::Skip);
    std::string offending = describeOffendingToken(state);
    if (!offending.empty())
        oss << " '" << offending << "'";
    oss << " (expected " << what << ")";
    return lineError<void>(state.lineNo(), oss.str());
}

bool peekIs(const parser_impl::ParserState &state, TokenKind kind)
{
    return state.ts && state.ts->kind() == kind;
}

bool consumeIf(parser_impl::ParserState &state, TokenKind kind)
{
    if (!peekIs(state, kind))
        return false;
    state.ts->advance();
    state.refresh();
    return true;
}

void recoverTo(parser_impl::ParserState &state, TokenKind boundary)
{
    if (!state.ts)
        return;
    while (state.ts->kind() != TokenKind::End && state.ts->kind() != boundary)
    {
        if (!state.ts->advance())
            break;
    }
    state.refresh();
}

Expected<void> parseLocDirective(parser_impl::ParserState &state)
{
    if (!state.ts)
        return lineError<void>(state.lineNo(), "malformed .loc directive");

    std::istringstream ls(state.ts->line().substr(4));
    uint32_t file = 0;
    uint32_t line = 0;
    uint32_t column = 0;
    ls >> file >> line >> column;
    if (!ls)
        return lineError<void>(state.lineNo(), "malformed .loc directive");
    ls >> std::ws;
    if (ls.peek() != std::char_traits<char>::eof())
        return lineError<void>(state.lineNo(), "malformed .loc directive");
    state.loc = {file, line, column};
    state.commit();
    return {};
}

Expected<void> parseBlock(parser_impl::ParserState &state)
{
    if (!state.ts)
        return lineError<void>(state.lineNo(), "missing block label");
    std::string blockHeader = state.ts->line();
    if (!blockHeader.empty())
        blockHeader.pop_back();
    auto result = parseBlockHeader(blockHeader, *state.legacy);
    state.refresh();
    return result;
}

std::string_view extractOpcode(std::string_view line)
{
    line = trimView(line);
    if (line.empty())
        return line;
    size_t eq = line.find('=');
    if (eq != std::string_view::npos)
    {
        line.remove_prefix(eq + 1);
        line = trimView(line);
    }
    size_t space = line.find_first_of(" \t");
    if (space == std::string_view::npos)
        return line;
    return line.substr(0, space);
}

Expected<void> parseGenericInstr(parser_impl::ParserState &state, std::string_view)
{
    if (!state.ts || !state.legacy)
        return lineError<void>(state.lineNo(), "unexpected instruction context");
    return parseInstructionShim_E(state.ts->line(), *state.legacy);
}

Expected<void> parseInstr(parser_impl::ParserState &state)
{
    using Handler = Expected<void> (*)(parser_impl::ParserState &, std::string_view);

    struct Dispatch
    {
        std::string_view opcode;
        Handler handler;
    };

    static constexpr std::array<Dispatch, 3> kDispatchTable = {{
        Dispatch{"br", &parseGenericInstr},
        Dispatch{"ret", &parseGenericInstr},
        Dispatch{"", &parseGenericInstr},
    }};

    std::string_view opcode = state.ts ? extractOpcode(state.ts->line()) : std::string_view{};
    for (const auto &entry : kDispatchTable)
    {
        if (entry.opcode.empty() || entry.opcode == opcode)
            return entry.handler(state, opcode);
    }
    return kDispatchTable.back().handler(state, opcode);
}

Expected<void> parseBody(TokenStream &stream, parser_impl::ParserState &state)
{
    state.ts = &stream;
    state.refresh();

    while (stream.advance())
    {
        state.refresh();

        if (stream.kind() == TokenKind::CloseBrace)
        {
            state.fn = nullptr;
            state.cur = nullptr;
            state.loc = {};
            state.commit();
            break;
        }

        if (stream.kind() == TokenKind::BlockLabel)
        {
            auto blockResult = parseBlock(state);
            if (!blockResult)
            {
                recoverTo(state, TokenKind::BlockLabel);
                return blockResult;
            }
            continue;
        }

        if (!state.cur)
            return expect(state, TokenKind::BlockLabel, "block label before instructions");

        if (stream.kind() == TokenKind::LocDirective)
        {
            auto locResult = parseLocDirective(state);
            if (!locResult)
            {
                recoverTo(state, TokenKind::BlockLabel);
                return locResult;
            }
            continue;
        }

        auto instrResult = parseInstr(state);
        if (!instrResult)
        {
            recoverTo(state, TokenKind::BlockLabel);
            return instrResult;
        }
        state.refresh();
    }

    if (state.fn)
    {
        state.fn = nullptr;
        state.cur = nullptr;
        state.loc = {};
        state.commit();
        return lineError<void>(state.lineNo(), "unexpected end of file; missing '}'");
    }

    if (!state.legacy->pendingBrs.empty())
    {
        const auto &unresolved = state.legacy->pendingBrs.front();
        std::ostringstream oss;
        oss << "unknown block '" << unresolved.label << "'";
        return lineError<void>(unresolved.line, oss.str());
    }

    return {};
}

} // namespace

// ============================================================================
// Block parameter parsing
// ============================================================================

Expected<Param> parseBlockParam(const std::string &paramText,
                                ParserState &st,
                                std::unordered_set<std::string> &localNames)
{
    std::string q = trim(paramText);
    if (q.empty())
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": bad param";
        if (!paramText.empty())
            oss << " '" << paramText << "'";
        else
            oss << " ''";
        oss << " (empty entry)";
        return Expected<Param>{makeError({}, oss.str())};
    }

    size_t col = q.find(':');
    if (col == std::string::npos)
        return lineError<Param>(st.lineNo, "bad param");

    std::string rawName = trim(q.substr(0, col));
    if (!rawName.empty() && rawName[0] != '%')
        return lineError<Param>(st.lineNo, "parameter name must start with '%'");

    std::string nm = rawName;
    if (!nm.empty() && nm[0] == '%')
        nm = nm.substr(1);
    if (nm.empty())
        return lineError<Param>(st.lineNo, "missing parameter name");

    std::string tyStr = trim(q.substr(col + 1));
    bool ok = true;
    Type ty = parseType(tyStr, &ok);
    if (!ok || ty.kind == Type::Kind::Void)
        return lineError<Param>(st.lineNo, "unknown param type");

    if (!localNames.insert(nm).second)
    {
        std::ostringstream oss;
        oss << "duplicate parameter name '%" << nm << "'";
        return lineError<Param>(st.lineNo, oss.str());
    }

    Param param{nm, ty, st.nextTemp};
    st.tempIds[nm] = st.nextTemp;
    if (st.curFn->valueNames.size() <= st.nextTemp)
        st.curFn->valueNames.resize(st.nextTemp + 1);
    st.curFn->valueNames[st.nextTemp] = nm;
    ++st.nextTemp;

    return param;
}

Expected<void> parseBlockParamList(const std::string &work,
                                   size_t lp,
                                   ParserState &st,
                                   std::vector<Param> &bparams)
{
    size_t rp = work.find(')', lp);
    if (rp == std::string::npos)
        return lineError<void>(st.lineNo, "mismatched ')'");

    std::string paramsStr = work.substr(lp + 1, rp - lp - 1);
    std::stringstream pss(paramsStr);
    std::string piece;
    std::unordered_set<std::string> localNames;

    while (std::getline(pss, piece, ','))
    {
        auto param = parseBlockParam(piece, st, localNames);
        if (!param)
            return Expected<void>{param.error()};
        bparams.push_back(std::move(param.value()));
    }

    return {};
}

Expected<void> resolvePendingBranches(const std::string &label, size_t paramCount, ParserState &st)
{
    for (auto it = st.pendingBrs.begin(); it != st.pendingBrs.end();)
    {
        if (it->label == label)
        {
            if (it->args != paramCount)
                return lineError<void>(it->line, "bad arg count");
            it = st.pendingBrs.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return {};
}

// ============================================================================
// Public API
// ============================================================================

Expected<void> parseBlockHeader(const std::string &header, ParserState &st)
{
    std::string work = trim(header);
    if (work.rfind("handler ", 0) == 0)
        work = trim(work.substr(8));

    size_t lp = work.find('(');
    std::string label = lp != std::string::npos ? trim(work.substr(0, lp)) : trim(work);
    if (!label.empty() && label[0] == '^')
        label = label.substr(1);

    if (label.empty())
        return lineError<void>(st.lineNo, "missing block label");

    if (st.blockParamCount.find(label) != st.blockParamCount.end())
    {
        std::ostringstream oss;
        oss << "duplicate block '" << label << "'";
        return lineError<void>(st.lineNo, oss.str());
    }

    std::vector<Param> bparams;
    if (lp != std::string::npos)
    {
        auto paramsResult = parseBlockParamList(work, lp, st, bparams);
        if (!paramsResult)
            return paramsResult;
    }

    st.curFn->blocks.push_back({label, bparams, {}, false});
    st.curBB = &st.curFn->blocks.back();
    st.blockParamCount[label] = bparams.size();

    return resolvePendingBranches(label, bparams.size(), st);
}

Expected<void> parseFunction(std::istream &is, std::string &header, ParserState &st)
{
    auto headerResult = parseFunctionHeader(header, st);
    if (!headerResult)
        return headerResult;

    TokenStream tokens(is, st);
    parser_impl::ParserState local{};
    local.legacy = &st;
    local.ts = &tokens;
    local.refresh();

    auto bodyResult = parseBody(tokens, local);
    if (!bodyResult)
        return bodyResult;

    return {};
}

} // namespace il::io::detail
