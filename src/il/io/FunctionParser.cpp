//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/io/FunctionParser.cpp
// Purpose: Parse IL textual function definitions into in-memory IR structures.
// Key invariants: ParserState maintains current function, block, and location
//                 context while enforcing SSA identifier uniqueness.
// Ownership/Lifetime: Populates functions directly within the supplied module.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the streaming parser that materialises IL functions.
/// @details The helpers in this file cooperate with instruction and operand
///          parsers to translate the textual IL syntax into the core IR data
///          structures.  The functions actively validate naming, type, and
///          structural constraints while producing precise diagnostics that
///          mirror the textual form understood by developers.

#include "il/io/FunctionParser.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "il/core/Param.hpp"

#include "il/io/InstrParser.hpp"
#include "il/io/ParserUtil.hpp"
#include "il/io/TypeParser.hpp"
#include "support/diag_expected.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace il::io::detail
{

using il::core::Param;
using il::core::Type;

namespace
{

using il::support::Expected;
using il::support::makeError;

namespace
{

enum class TokenKind
{
    Skip,
    CloseBrace,
    BlockLabel,
    LocDirective,
    Instruction,
    End,
};

enum class DiagKind
{
    CurrentLine,
    PendingBranch,
};

struct ParseState
{
    std::istream &stream;
    std::string &header;
    ParserState &ctx;
    std::string line;
    TokenKind token = TokenKind::Skip;
};

} // namespace

bool tokenIs(TokenKind tok, TokenKind kind)
{
    return tok == kind;
}

template <class T = void>
Expected<T> expect(ParseState &state, DiagKind kind, std::string message)
{
    unsigned line = state.ctx.lineNo;
    if (kind == DiagKind::PendingBranch && !state.ctx.pendingBrs.empty())
        line = state.ctx.pendingBrs.front().line;
    std::ostringstream oss;
    oss << "line " << line << ": " << message;
    return Expected<T>{makeError({}, oss.str())};
}

#define TRY(expr)                   \
    do                              \
    {                               \
        auto _result = (expr);      \
        if (!_result)               \
            return _result;         \
    } while (false)

struct Cursor
{
    std::string_view text;
    size_t index = 0;
    unsigned lineNo = 0;
    size_t gapBegin = 0;
    size_t gapEnd = 0;
    bool hasGap = false;

    Cursor(std::string_view src, unsigned line) : text(src), lineNo(line) {}

    void skipWs()
    {
        while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])))
            ++index;
    }

    char peek() const
    {
        return index < text.size() ? text[index] : '\0';
    }

    bool consume(char c)
    {
        if (peek() != c)
            return false;
        ++index;
        return true;
    }

    bool consumeIf(char c)
    {
        if (peek() == c)
        {
            ++index;
            return true;
        }
        return false;
    }

    bool consumeIdent(std::string_view &out)
    {
        skipWs();
        if (index >= text.size())
            return false;
        auto isStart = [](unsigned char ch) {
            return std::isalpha(ch) || ch == '_' || ch == '.';
        };
        auto isBody = [](unsigned char ch) {
            return std::isalnum(ch) || ch == '_' || ch == '.' || ch == '$';
        };
        unsigned char first = static_cast<unsigned char>(text[index]);
        if (!isStart(first))
            return false;
        size_t begin = index++;
        while (index < text.size() && isBody(static_cast<unsigned char>(text[index])))
            ++index;
        out = text.substr(begin, index - begin);
        return true;
    }

    bool consumeKeyword(std::string_view kw)
    {
        skipWs();
        if (!kw.empty() && text.substr(index, kw.size()) == kw)
        {
            index += kw.size();
            return true;
        }
        return false;
    }

    bool atEnd() const
    {
        return index >= text.size();
    }

    size_t pos() const
    {
        return index;
    }

    void setGap(size_t begin, size_t end)
    {
        gapBegin = begin;
        gapEnd = end;
        hasGap = end > begin;
    }

    std::string_view gap() const
    {
        if (!hasGap)
            return {};
        return text.substr(gapBegin, gapEnd - gapBegin);
    }

    void clearGap()
    {
        hasGap = false;
    }
};

struct Prototype
{
    Type retType;
    std::vector<Param> params;
};

enum class CallingConv
{
    Default,
};

struct Attrs
{
};

struct FunctionHeader
{
    std::string name;
    Prototype proto;
    CallingConv cc;
    Attrs attrs;
    il::support::SourceLoc loc;
};

struct ParserSnapshot
{
    ParserState &state;
    il::core::Function *curFn;
    il::core::BasicBlock *curBB;
    il::support::SourceLoc curLoc;
    std::unordered_map<std::string, unsigned> tempIds;
    unsigned nextTemp;
    std::unordered_map<std::string, size_t> blockParamCount;
    std::vector<ParserState::PendingBr> pendingBrs;
    size_t functionCount;
    bool active = true;

    explicit ParserSnapshot(ParserState &st)
        : state(st),
          curFn(st.curFn),
          curBB(st.curBB),
          curLoc(st.curLoc),
          tempIds(st.tempIds),
          nextTemp(st.nextTemp),
          blockParamCount(st.blockParamCount),
          pendingBrs(st.pendingBrs),
          functionCount(st.m.functions.size())
    {
    }

    void restore()
    {
        state.curFn = curFn;
        state.curBB = curBB;
        state.curLoc = curLoc;
        state.tempIds = tempIds;
        state.nextTemp = nextTemp;
        state.blockParamCount = blockParamCount;
        state.pendingBrs = pendingBrs;
        if (state.m.functions.size() > functionCount)
            state.m.functions.resize(functionCount);
    }

    void discard()
    {
        active = false;
    }

    ~ParserSnapshot()
    {
        if (active)
            restore();
    }
};

std::string_view trimView(std::string_view text)
{
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;
    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return text.substr(begin, end - begin);
}

template <class T>
Expected<T> malformedFunctionHeader(unsigned lineNo)
{
    std::ostringstream oss;
    oss << "line " << lineNo << ": malformed function header";
    return Expected<T>{makeError({}, oss.str())};
}

template <class T>
Expected<T> lineError(unsigned lineNo, const std::string &message)
{
    std::ostringstream oss;
    oss << "line " << lineNo << ": " << message;
    return Expected<T>{makeError({}, oss.str())};
}

Expected<Param> parseParameterToken(const std::string &rawParam, unsigned lineNo)
{
    std::string trimmed = trim(rawParam);
    if (trimmed.empty())
    {
        std::ostringstream oss;
        oss << "malformed parameter";
        if (!rawParam.empty())
            oss << " '" << rawParam << "'";
        else
            oss << " ''";
        oss << " (empty entry)";
        return lineError<Param>(lineNo, oss.str());
    }

    std::string ty;
    std::string nm;
    size_t colon = trimmed.find(':');
    if (colon != std::string::npos)
    {
        std::string left = trim(trimmed.substr(0, colon));
        std::string right = trim(trimmed.substr(colon + 1));
        if (left.empty() || right.empty())
            return lineError<Param>(lineNo, "malformed parameter");
        nm = std::move(left);
        ty = std::move(right);
    }
    else
    {
        std::stringstream ps(trimmed);
        ps >> ty >> nm;
    }

    if (ty.empty() || nm.empty())
        return lineError<Param>(lineNo, "malformed parameter");
    if (nm[0] != '%')
        return lineError<Param>(lineNo, "parameter name must start with '%'");
    if (nm.size() == 1)
        return lineError<Param>(lineNo, "missing parameter name");

    bool ok = true;
    Type parsedTy = parseType(ty, &ok);
    if (!ok || parsedTy.kind == Type::Kind::Void)
        return lineError<Param>(lineNo, "unknown param type");

    return Param{nm.substr(1), parsedTy};
}

Expected<std::string> parseSymbolName(Cursor &cur)
{
    size_t at = cur.text.find('@');
    if (at == std::string_view::npos)
        return malformedFunctionHeader<std::string>(cur.lineNo);
    size_t lp = cur.text.find('(', at);
    if (lp == std::string_view::npos)
        return malformedFunctionHeader<std::string>(cur.lineNo);
    std::string name = trim(std::string(cur.text.substr(at + 1, lp - at - 1)));
    if (name.empty())
        return malformedFunctionHeader<std::string>(cur.lineNo);
    cur.index = lp;
    return name;
}

Expected<Prototype> parsePrototype(Cursor &cur)
{
    if (!cur.consume('('))
        return malformedFunctionHeader<Prototype>(cur.lineNo);
    size_t paramsBegin = cur.pos();
    size_t rp = cur.text.find(')', paramsBegin);
    if (rp == std::string_view::npos)
        return malformedFunctionHeader<Prototype>(cur.lineNo);
    std::string paramsStr(cur.text.substr(paramsBegin, rp - paramsBegin));
    cur.index = rp + 1;

    std::vector<Param> params;
    if (!paramsStr.empty())
    {
        std::stringstream pss(paramsStr);
        std::string piece;
        while (std::getline(pss, piece, ','))
        {
            auto param = parseParameterToken(piece, cur.lineNo);
            if (!param)
                return Expected<Prototype>{param.error()};
            params.push_back(std::move(param.value()));
        }
    }

    size_t gapStart = cur.pos();
    size_t arrow = cur.text.find("->", gapStart);
    if (arrow == std::string_view::npos)
        return malformedFunctionHeader<Prototype>(cur.lineNo);
    cur.setGap(gapStart, arrow);
    cur.index = arrow + 2;
    cur.skipWs();

    size_t brace = cur.text.find('{', cur.pos());
    if (brace == std::string_view::npos)
        return malformedFunctionHeader<Prototype>(cur.lineNo);
    std::string retRaw(cur.text.substr(cur.pos(), brace - cur.pos()));
    std::string retStr = trim(retRaw);
    bool retOk = true;
    Type retTy = parseType(retStr, &retOk);
    if (!retOk)
        return lineError<Prototype>(cur.lineNo, "unknown return type");

    cur.index = brace;
    return Prototype{retTy, std::move(params)};
}

Expected<CallingConv> parseCallingConv(Cursor &cur)
{
    std::string_view segment = trimView(cur.gap());
    cur.clearGap();
    if (segment.empty())
        return CallingConv::Default;

    static constexpr std::array<std::pair<std::string_view, CallingConv>, 1> kCallingConvs = {{
        {"default", CallingConv::Default},
    }};

    for (const auto &entry : kCallingConvs)
    {
        if (segment == entry.first)
            return entry.second;
    }

    return CallingConv::Default;
}

Expected<Attrs> parseAttributes(Cursor &cur)
{
    cur.skipWs();
    if (!cur.consume('{'))
        return malformedFunctionHeader<Attrs>(cur.lineNo);
    return Attrs{};
}

Expected<il::support::SourceLoc> parseOptionalLoc(Cursor &cur)
{
    cur.skipWs();
    return il::support::SourceLoc{};
}

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

bool advance(ParseState &state)
{
    while (std::getline(state.stream, state.line))
    {
        ++state.ctx.lineNo;
        state.line = trim(state.line);
        if (state.line.empty() || state.line.rfind("//", 0) == 0)
            continue;
        if (!state.line.empty() && state.line.front() == '#')
            continue;
        if (!state.line.empty() && state.line.front() == '}')
        {
            state.token = TokenKind::CloseBrace;
            return true;
        }
        if (!state.line.empty() && state.line.back() == ':')
        {
            state.token = TokenKind::BlockLabel;
            return true;
        }
        if (state.line.rfind(".loc", 0) == 0)
        {
            state.token = TokenKind::LocDirective;
            return true;
        }
        state.token = TokenKind::Instruction;
        return true;
    }
    state.token = TokenKind::End;
    return false;
}

Expected<void> parseLocDirective(ParseState &state)
{
    std::istringstream ls(state.line.substr(4));
    uint32_t file = 0;
    uint32_t line = 0;
    uint32_t column = 0;
    ls >> file >> line >> column;
    if (!ls)
        return expect(state, DiagKind::CurrentLine, "malformed .loc directive");
    ls >> std::ws;
    if (ls.peek() != std::char_traits<char>::eof())
        return expect(state, DiagKind::CurrentLine, "malformed .loc directive");
    state.ctx.curLoc = {file, line, column};
    return {};
}

Expected<void> parseHeader(ParseState &state)
{
    return parseFunctionHeader(state.header, state.ctx);
}

Expected<void> parseSignature(ParseState &state)
{
    (void)state;
    return {};
}

Expected<void> parseAttributes(ParseState &state)
{
    (void)state;
    return {};
}

Expected<void> parseBody(ParseState &state)
{
    while (advance(state))
    {
        if (tokenIs(state.token, TokenKind::CloseBrace))
        {
            state.ctx.curFn = nullptr;
            state.ctx.curBB = nullptr;
            state.ctx.curLoc = {};
            break;
        }

        if (tokenIs(state.token, TokenKind::BlockLabel))
        {
            std::string blockHeader = state.line.substr(0, state.line.size() - 1);
            TRY(parseBlockHeader(blockHeader, state.ctx));
            continue;
        }

        if (!state.ctx.curBB)
            return expect(state, DiagKind::CurrentLine, "instruction outside block");

        if (tokenIs(state.token, TokenKind::LocDirective))
        {
            TRY(parseLocDirective(state));
            continue;
        }

        TRY(parseInstructionShim_E(state.line, state.ctx));
    }

    if (state.ctx.curFn)
    {
        state.ctx.curFn = nullptr;
        state.ctx.curBB = nullptr;
        state.ctx.curLoc = {};
        return expect(state, DiagKind::CurrentLine, "unexpected end of file; missing '}'");
    }

    if (!state.ctx.pendingBrs.empty())
    {
        const auto &unresolved = state.ctx.pendingBrs.front();
        std::string message = "unknown block '" + unresolved.label + "'";
        return expect(state, DiagKind::PendingBranch, std::move(message));
    }

    return {};
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
    ParserSnapshot snapshot{st};
    Cursor cursor{header, st.lineNo};

    FunctionHeader fh;
    {
        auto name = parseSymbolName(cursor);
        if (!name)
            return Expected<void>{name.error()};
        fh.name = std::move(name.value());
    }
    {
        auto proto = parsePrototype(cursor);
        if (!proto)
            return Expected<void>{proto.error()};
        fh.proto = std::move(proto.value());
    }
    {
        auto cc = parseCallingConv(cursor);
        if (!cc)
            return Expected<void>{cc.error()};
        fh.cc = cc.value();
    }
    {
        auto attrs = parseAttributes(cursor);
        if (!attrs)
            return Expected<void>{attrs.error()};
        fh.attrs = attrs.value();
    }
    {
        auto loc = parseOptionalLoc(cursor);
        if (!loc)
            return Expected<void>{loc.error()};
        fh.loc = loc.value();
    }

    for (const auto &fn : st.m.functions)
    {
        if (fn.name == fh.name)
        {
            std::ostringstream oss;
            oss << "duplicate function '@" << fh.name << "'";
            return lineError<void>(st.lineNo, oss.str());
        }
    }

    std::unordered_set<std::string> seenParams;
    for (const auto &param : fh.proto.params)
    {
        if (!seenParams.insert(param.name).second)
        {
            std::ostringstream oss;
            oss << "duplicate parameter name '%" << param.name << "'";
            return lineError<void>(st.lineNo, oss.str());
        }
    }

    st.curLoc = fh.loc;
    st.tempIds.clear();
    unsigned nextId = 0;
    for (auto &param : fh.proto.params)
    {
        param.id = nextId;
        st.tempIds[param.name] = nextId;
        ++nextId;
    }
    st.nextTemp = nextId;

    il::core::Function fn{fh.name, fh.proto.retType, std::move(fh.proto.params), {}, {}};
    st.m.functions.push_back(std::move(fn));
    st.curFn = &st.m.functions.back();
    st.curBB = nullptr;
    st.curFn->valueNames.resize(st.nextTemp);
    for (const auto &param : st.curFn->params)
        st.curFn->valueNames[param.id] = param.name;
    st.blockParamCount.clear();
    st.pendingBrs.clear();

    snapshot.discard();
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
    std::string work = trim(header);
    if (work.rfind("handler ", 0) == 0)
        work = trim(work.substr(8));
    size_t lp = work.find('(');
    std::vector<Param> bparams;
    std::string label = lp != std::string::npos ? trim(work.substr(0, lp)) : trim(work);
    if (!label.empty() && label[0] == '^')
        label = label.substr(1);
    if (label.empty())
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": missing block label";
        return Expected<void>{makeError({}, oss.str())};
    }
    if (st.blockParamCount.find(label) != st.blockParamCount.end())
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": duplicate block '" << label << "'";
        return Expected<void>{makeError({}, oss.str())};
    }
    std::unordered_set<std::string> localNames;
    if (lp != std::string::npos)
    {
        size_t rp = work.find(')', lp);
        if (rp == std::string::npos)
        {
            std::ostringstream oss;
            oss << "line " << st.lineNo << ": mismatched ')'";
            return Expected<void>{makeError({}, oss.str())};
        }
        std::string paramsStr = work.substr(lp + 1, rp - lp - 1);
        std::stringstream pss(paramsStr);
        std::string q;
        while (std::getline(pss, q, ','))
        {
            std::string rawParam = q;
            q = trim(q);
            if (q.empty())
            {
                std::ostringstream oss;
                oss << "line " << st.lineNo << ": bad param";
                if (!rawParam.empty())
                    oss << " '" << rawParam << "'";
                else
                    oss << " ''";
                oss << " (empty entry)";
                return Expected<void>{makeError({}, oss.str())};
            }
            size_t col = q.find(':');
            if (col == std::string::npos)
            {
                std::ostringstream oss;
                oss << "line " << st.lineNo << ": bad param";
                return Expected<void>{makeError({}, oss.str())};
            }
            std::string rawName = trim(q.substr(0, col));
            if (!rawName.empty() && rawName[0] != '%')
            {
                std::ostringstream oss;
                oss << "line " << st.lineNo << ": parameter name must start with '%'";
                return Expected<void>{makeError({}, oss.str())};
            }
            std::string nm = rawName;
            if (!nm.empty() && nm[0] == '%')
                nm = nm.substr(1);
            if (nm.empty())
            {
                std::ostringstream oss;
                oss << "line " << st.lineNo << ": missing parameter name";
                return Expected<void>{makeError({}, oss.str())};
            }
            std::string tyStr = trim(q.substr(col + 1));
            bool ok = true;
            Type ty = parseType(tyStr, &ok);
            if (!ok || ty.kind == Type::Kind::Void)
            {
                std::ostringstream oss;
                oss << "line " << st.lineNo << ": unknown param type";
                return Expected<void>{makeError({}, oss.str())};
            }
            if (!localNames.insert(nm).second)
            {
                std::ostringstream oss;
                oss << "line " << st.lineNo
                    << ": duplicate parameter name '%" << nm << "'";
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
    ParseState state{is, header, st};
    TRY(parseHeader(state));
    TRY(parseSignature(state));
    TRY(parseAttributes(state));
    TRY(parseBody(state));
    return {};
}

#undef TRY

} // namespace il::io::detail
