// File: src/il/io/FunctionParser.cpp
// Purpose: Implements helpers for parsing IL function definitions (MIT License; see
//          LICENSE).
// Key invariants: ParserState maintains current function and block context.
// Ownership/Lifetime: Populates functions directly within the supplied module.
// Links: docs/il-spec.md

#include "il/io/FunctionParser.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "il/core/Param.hpp"

#include "il/io/InstrParser.hpp"
#include "il/io/ParserUtil.hpp"
#include "il/io/TypeParser.hpp"
#include "support/diag_expected.hpp"

#include <cstdint>
#include <sstream>
#include <string_view>
#include <utility>

namespace il::io::detail
{

using il::core::Param;
using il::core::Type;

namespace
{

using il::support::Expected;
using il::support::makeError;

/// @brief Normalises diagnostics captured from instruction parsing.
///
/// The instruction parser reports errors prefixed with "error: " and terminated by
/// trailing newlines. This helper strips that prefix and trailing newline/carriage
/// returns so that downstream diagnostics emitted through @ref
/// il::support::printDiag are consistent across call sites.
///
/// @param text Raw diagnostic text captured from a stream buffer.
/// @return The diagnostic message without redundant prefix/terminators.
std::string stripCapturedDiagMessage(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();
    constexpr std::string_view kPrefix = "error: ";
    if (text.rfind(kPrefix, 0) == 0)
        text.erase(0, kPrefix.size());
    return text;
}

/// @brief Parses a single IL instruction line and forwards diagnostics.
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

} // namespace

/// @brief Parses a function header and initialises the parser state for a new
/// function.
///
/// The expected format matches the IL textual form, e.g.
/// `func @name(i32 %arg0, ptr %arg1) -> i1 {`. Parameter identifiers must be
/// prefixed with `%`, which is stripped when recording names; the parser assumes
/// this convention for mapping temporaries. On success, the supplied @p st is
/// updated with a new function appended to the current module, the argument
/// temporaries seeded (including `st.tempIds` and `st.nextTemp`), and block state
/// cleared. Errors report malformed headers or unknown types and reference the
/// current line number stored in the parser state.
///
/// @param header Canonical IL function header text.
/// @param st Parser state receiving the new function and reset block context.
/// @return Empty on success; otherwise, an error diagnostic describing the malformed
/// header.
Expected<void> parseFunctionHeader(const std::string &header, ParserState &st)
{
    size_t at = header.find('@');
    size_t lp = header.find('(', at);
    size_t rp = header.find(')', lp);
    size_t arr = header.find("->", rp);
    size_t lb = header.find('{', arr);
    if (arr == std::string::npos || lb == std::string::npos)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": malformed function header";
        return Expected<void>{makeError({}, oss.str())};
    }
    std::string name = header.substr(at + 1, lp - at - 1);
    std::string paramsStr = header.substr(lp + 1, rp - lp - 1);
    std::vector<Param> params;
    std::stringstream pss(paramsStr);
    std::string p;
    while (std::getline(pss, p, ','))
    {
        p = trim(p);
        if (p.empty())
            continue;
        std::stringstream ps(p);
        std::string ty, nm;
        ps >> ty >> nm;
        if (!ty.empty() && !nm.empty() && nm[0] == '%')
            params.push_back({nm.substr(1), parseType(ty)});
    }
    std::string retStr = trim(header.substr(arr + 2, lb - arr - 2));
    st.tempIds.clear();
    unsigned idx = 0;
    for (auto &param : params)
    {
        param.id = idx;
        st.tempIds[param.name] = idx;
        ++idx;
    }
    st.m.functions.push_back({name, parseType(retStr), params, {}, {}});
    st.curFn = &st.m.functions.back();
    st.curBB = nullptr;
    st.nextTemp = idx;
    st.curFn->valueNames.resize(st.nextTemp);
    for (auto &param : params)
        st.curFn->valueNames[param.id] = param.name;
    st.blockParamCount.clear();
    st.pendingBrs.clear();
    return {};
}

/// @brief Parses a basic-block header and opens a new block in the current
/// function.
///
/// The header should contain a label optionally followed by parameter
/// declarations, e.g. `bb0(%x: ptr, %y: i32)`. Parameters follow the `%name :
/// type` syntax; `%` prefixes are assumed (and removed) when populating block
/// temporaries. Successful parses append a new block, populate
/// `ParserState::tempIds`, extend `valueNames`, and increment `st.nextTemp` for
/// each parameter. Failures arise from mismatched parentheses, missing types, or
/// other malformed parameter definitions, and they report using the state's line
/// counter.
///
/// @param header Text of the block label (without the trailing colon).
/// @param st Parser state mutated with the newly opened block and updated
/// temporary mappings.
/// @return Empty on success; otherwise, a diagnostic capturing the malformed
/// header information.
Expected<void> parseBlockHeader(const std::string &header, ParserState &st)
{
    size_t lp = header.find('(');
    std::vector<Param> bparams;
    std::string label = trim(header);
    if (lp != std::string::npos)
    {
        size_t rp = header.find(')', lp);
        if (rp == std::string::npos)
        {
            std::ostringstream oss;
            oss << "line " << st.lineNo << ": mismatched ')'";
            return Expected<void>{makeError({}, oss.str())};
        }
        label = trim(header.substr(0, lp));
        std::string paramsStr = header.substr(lp + 1, rp - lp - 1);
        std::stringstream pss(paramsStr);
        std::string q;
        while (std::getline(pss, q, ','))
        {
            q = trim(q);
            if (q.empty())
                continue;
            size_t col = q.find(':');
            if (col == std::string::npos)
            {
                std::ostringstream oss;
                oss << "line " << st.lineNo << ": bad param";
                return Expected<void>{makeError({}, oss.str())};
            }
            std::string nm = trim(q.substr(0, col));
            if (!nm.empty() && nm[0] == '%')
                nm = nm.substr(1);
            std::string tyStr = trim(q.substr(col + 1));
            bool ok = true;
            Type ty = parseType(tyStr, &ok);
            if (!ok || ty.kind == Type::Kind::Void)
            {
                std::ostringstream oss;
                oss << "line " << st.lineNo << ": unknown param type";
                return Expected<void>{makeError({}, oss.str())};
            }
            bparams.push_back({nm, ty, st.nextTemp});
            st.tempIds[nm] = st.nextTemp;
            if (st.curFn->valueNames.size() <= st.nextTemp)
                st.curFn->valueNames.resize(st.nextTemp + 1);
            st.curFn->valueNames[st.nextTemp] = nm;
            ++st.nextTemp;
        }
    }
    st.curFn->blocks.push_back({label, bparams, {}, false});
    st.curBB = &st.curFn->blocks.back();
    st.blockParamCount[label] = bparams.size();
    for (auto it = st.pendingBrs.begin(); it != st.pendingBrs.end();)
    {
        if (it->label == label)
        {
            if (it->args != bparams.size())
            {
                std::ostringstream oss;
                oss << "line " << it->line << ": bad arg count";
                return Expected<void>{makeError({}, oss.str())};
            }
            it = st.pendingBrs.erase(it);
        }
        else
            ++it;
    }
    return {};
}

/// @brief Parses an entire function body following an already-read header.
///
/// The stream @p is should provide the body lines after the function header and
/// opening brace. The parser recognises block labels terminated by `:`, `.loc`
/// directives, blank/comment lines, and individual instruction lines formatted as
/// by the serializer. It mutates @p st to track the active function, block,
/// source locations, and pending branch resolution. Errors return diagnostics for
/// malformed blocks, instructions outside blocks, or instruction parsing failures.
///
/// @param is Input stream positioned on the first body line after the header.
/// @param header Original header string already consumed from the stream.
/// @param st Parser state receiving the fully parsed function definition.
/// @return Empty on success; otherwise, a diagnostic describing the parsing issue.
Expected<void> parseFunction(std::istream &is, std::string &header, ParserState &st)
{
    auto headerResult = parseFunctionHeader(header, st);
    if (!headerResult)
        return headerResult;

    std::string line;
    while (std::getline(is, line))
    {
        ++st.lineNo;
        line = trim(line);
        if (line.empty() || line.rfind("//", 0) == 0)
            continue;
        if (line[0] == '}')
        {
            st.curFn = nullptr;
            st.curBB = nullptr;
            return {};
        }
        if (line.back() == ':')
        {
            auto blockResult = parseBlockHeader(line.substr(0, line.size() - 1), st);
            if (!blockResult)
                return blockResult;
            continue;
        }
        if (!st.curBB)
        {
            std::ostringstream oss;
            oss << "line " << st.lineNo << ": instruction outside block";
            return Expected<void>{makeError({}, oss.str())};
        }
        if (line.rfind(".loc", 0) == 0)
        {
            std::istringstream ls(line.substr(4));
            uint32_t fid = 0, ln = 0, col = 0;
            ls >> fid >> ln >> col;
            st.curLoc = {fid, ln, col};
            continue;
        }
        auto instr = parseInstructionShim_E(line, st);
        if (!instr)
            return instr;
    }
    return {};
}

} // namespace il::io::detail
