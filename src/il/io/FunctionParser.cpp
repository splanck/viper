//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

#include "il/internal/io/FunctionParser.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "il/core/Param.hpp"

#include "il/internal/io/InstrParser.hpp"
#include "il/internal/io/ParserUtil.hpp"
#include "il/internal/io/TypeParser.hpp"
#include "support/diag_expected.hpp"
#include "viper/parse/Cursor.h"

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
using viper::parse::Cursor;
using viper::parse::SourcePos;

namespace
{

using il::support::Diag;
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

class TokenStream
{
  public:
    TokenStream(std::istream &stream, ParserState &legacy) : stream_(&stream), legacy_(&legacy) {}

    [[nodiscard]] TokenKind kind() const noexcept
    {
        return token_;
    }

    [[nodiscard]] const std::string &line() const noexcept
    {
        return line_;
    }

    [[nodiscard]] ParserState &legacy() noexcept
    {
        return *legacy_;
    }

    bool advance()
    {
        while (std::getline(*stream_, line_))
        {
            ++legacy_->lineNo;
            line_ = trim(line_);
            if (line_.empty() || line_.rfind("//", 0) == 0)
                continue;
            if (!line_.empty() && line_.front() == '#')
                continue;
            if (!line_.empty() && line_.front() == '}')
            {
                token_ = TokenKind::CloseBrace;
                return true;
            }
            if (!line_.empty() && line_.back() == ':')
            {
                token_ = TokenKind::BlockLabel;
                return true;
            }
            if (line_.rfind(".loc", 0) == 0)
            {
                token_ = TokenKind::LocDirective;
                return true;
            }
            token_ = TokenKind::Instruction;
            return true;
        }
        token_ = TokenKind::End;
        line_.clear();
        return false;
    }

  private:
    std::istream *stream_ = nullptr;
    ParserState *legacy_ = nullptr;
    std::string line_;
    TokenKind token_ = TokenKind::Skip;
};

namespace parser_impl
{

struct ParserState
{
    il::core::Module *mod = nullptr;
    il::core::Function *fn = nullptr;
    il::core::BasicBlock *cur = nullptr;
    il::support::SourceLoc loc{};
    il::support::DiagnosticEngine *diags = nullptr;
    TokenStream *ts = nullptr;
    il::io::detail::ParserState *legacy = nullptr;

    void refresh()
    {
        if (!legacy)
            return;
        mod = &legacy->m;
        fn = legacy->curFn;
        cur = legacy->curBB;
        loc = legacy->curLoc;
    }

    void commit()
    {
        if (!legacy)
            return;
        legacy->curFn = fn;
        legacy->curBB = cur;
        legacy->curLoc = loc;
    }

    [[nodiscard]] unsigned lineNo() const noexcept
    {
        return legacy ? legacy->lineNo : 0;
    }
};

} // namespace parser_impl

} // namespace

using LegacyParserState = ::il::io::detail::ParserState;

#define TRY(expr)                                                                                  \
    do                                                                                             \
    {                                                                                              \
        auto _result = (expr);                                                                     \
        if (!_result)                                                                              \
            return _result;                                                                        \
    } while (false)

using Error = Diag;

SourcePos cursorPos(const Cursor &cur)
{
    return cur.pos();
}

struct Prototype
{
    Type retType;
    std::vector<Param> params;
};

struct PrototypeParseResult
{
    Prototype proto;
    std::string_view callingConvSegment;
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
        : state(st), curFn(st.curFn), curBB(st.curBB), curLoc(st.curLoc), tempIds(st.tempIds),
          nextTemp(st.nextTemp), blockParamCount(st.blockParamCount), pendingBrs(st.pendingBrs),
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

template <class T> Expected<T> lineError(unsigned lineNo, const std::string &message)
{
    std::ostringstream oss;
    oss << "line " << lineNo << ": " << message;
    return Expected<T>{makeError({}, oss.str())};
}

Error makeSyntaxError(SourcePos pos, std::string_view msg, std::string_view near)
{
    std::ostringstream body;
    body << msg;
    if (!near.empty())
        body << " '" << near << "'";
    return lineError<void>(pos.line, body.str()).error();
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
    cur.skipWs();
    if (cur.atEnd())
        return Expected<std::string>{
            makeSyntaxError(cursorPos(cur), "unexpected end of header", {})};

    const std::size_t searchStart = cur.offset();
    size_t at = cur.view().find('@', searchStart);
    if (at == std::string_view::npos)
        return Expected<std::string>{
            makeSyntaxError(cursorPos(cur), "malformed function header", {})};
    size_t lp = cur.view().find('(', at);
    if (lp == std::string_view::npos)
        return Expected<std::string>{
            makeSyntaxError(cursorPos(cur), "malformed function header", {})};
    std::string name = trim(std::string(cur.view().substr(at + 1, lp - at - 1)));
    if (name.empty())
        return Expected<std::string>{
            makeSyntaxError(cursorPos(cur), "malformed function header", {})};
    cur.seek(lp);
    return name;
}

Expected<PrototypeParseResult> parsePrototype(Cursor &cur)
{
    cur.skipWs();
    if (cur.atEnd())
        return Expected<PrototypeParseResult>{
            makeSyntaxError(cursorPos(cur), "unexpected end of header", {})};
    if (!cur.consume('('))
        return Expected<PrototypeParseResult>{
            makeSyntaxError(cursorPos(cur), "malformed function header", {})};
    size_t paramsBegin = cur.offset();
    size_t rp = cur.view().find(')', paramsBegin);
    if (rp == std::string_view::npos)
        return Expected<PrototypeParseResult>{
            makeSyntaxError(cursorPos(cur), "malformed function header", {})};
    std::string paramsStr(cur.view().substr(paramsBegin, rp - paramsBegin));
    cur.seek(rp + 1);

    std::vector<Param> params;
    if (!paramsStr.empty())
    {
        std::stringstream pss(paramsStr);
        std::string piece;
        while (std::getline(pss, piece, ','))
        {
            auto param = parseParameterToken(piece, cur.line());
            if (!param)
                return Expected<PrototypeParseResult>{param.error()};
            params.push_back(std::move(param.value()));
        }
    }

    size_t gapStart = cur.offset();
    size_t arrow = cur.view().find("->", gapStart);
    if (arrow == std::string_view::npos)
    {
        if (trimView(cur.view().substr(gapStart)).empty())
            return Expected<PrototypeParseResult>{
                makeSyntaxError(cursorPos(cur), "unexpected end of header", {})};
        return Expected<PrototypeParseResult>{
            makeSyntaxError(cursorPos(cur), "malformed function header", {})};
    }
    std::string_view ccSegment = cur.view().substr(gapStart, arrow - gapStart);
    cur.seek(arrow + 2);
    cur.skipWs();
    if (cur.atEnd())
        return Expected<PrototypeParseResult>{
            makeSyntaxError(cursorPos(cur), "unexpected end of header", {})};

    size_t brace = cur.view().find('{', cur.offset());
    if (brace == std::string_view::npos)
    {
        if (trimView(cur.view().substr(cur.offset())).empty())
            return Expected<PrototypeParseResult>{
                makeSyntaxError(cursorPos(cur), "unexpected end of header", {})};
        return Expected<PrototypeParseResult>{
            makeSyntaxError(cursorPos(cur), "malformed function header", {})};
    }
    std::string retRaw(cur.view().substr(cur.offset(), brace - cur.offset()));
    std::string retStr = trim(retRaw);
    bool retOk = true;
    Type retTy = parseType(retStr, &retOk);
    if (!retOk)
        return Expected<PrototypeParseResult>{
            makeSyntaxError(cursorPos(cur), "unknown return type", {})};

    cur.seek(brace);
    return PrototypeParseResult{Prototype{retTy, std::move(params)}, ccSegment};
}

Expected<CallingConv> parseCallingConv(std::string_view segment, unsigned lineNo)
{
    segment = trimView(segment);
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

    std::ostringstream oss;
    oss << "unknown calling convention '" << segment << "'";
    return lineError<CallingConv>(lineNo, oss.str());
}

Expected<Attrs> parseAttributes(Cursor &cur)
{
    cur.skipWs();
    if (cur.atEnd())
        return Expected<Attrs>{makeSyntaxError(cursorPos(cur), "unexpected end of header", {})};
    if (!cur.consume('{'))
        return Expected<Attrs>{makeSyntaxError(cursorPos(cur), "malformed function header", {})};
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

std::string_view describeTokenKind(TokenKind token)
{
    switch (token)
    {
        case TokenKind::CloseBrace:
            return "'}'";
        case TokenKind::BlockLabel:
            return "block label";
        case TokenKind::LocDirective:
            return "'.loc' directive";
        case TokenKind::Instruction:
            return "instruction";
        case TokenKind::End:
            return "end of function";
        case TokenKind::Skip:
            break;
    }
    return "token";
}

std::string describeOffendingToken(const parser_impl::ParserState &state)
{
    if (!state.ts)
        return "";
    switch (state.ts->kind())
    {
        case TokenKind::CloseBrace:
            return "}";
        case TokenKind::BlockLabel:
        case TokenKind::LocDirective:
        case TokenKind::Instruction:
            return state.ts->line();
        case TokenKind::End:
            return "<eof>";
        case TokenKind::Skip:
            break;
    }
    return "";
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

/// @brief Parses a single IL instruction line and forwards diagnostics.
///
/// @param line Text of one instruction, in the same format emitted by the IL
/// serializer (including optional `%temp =` leading assignments).
/// @param st Parser state mutated for each decoded instruction; the helper
/// forwards to parseInstruction(), which may extend temporary mappings, update
/// pending branch bookkeeping, and capture diagnostic locations.
/// @return Empty on success; otherwise, a diagnostic normalised via
/// stripCapturedDiagMessage().
Expected<void> parseInstructionShim_E(const std::string &line, LegacyParserState &st)
{
    std::ostringstream capture;
    if (parseInstruction(line, st, capture))
        return {};
    auto message = stripCapturedDiagMessage(capture.str());
    return Expected<void>{makeError(st.curLoc, std::move(message))};
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
    Cursor cursor{header, SourcePos{st.lineNo, 0}};

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
        auto parsedProto = std::move(proto.value());
        fh.proto = std::move(parsedProto.proto);
        auto cc = parseCallingConv(parsedProto.callingConvSegment, st.lineNo);
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
                oss << "line " << st.lineNo << ": duplicate parameter name '%" << nm << "'";
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
    TRY(parseFunctionHeader(header, st));

    TokenStream tokens(is, st);
    parser_impl::ParserState local{};
    local.legacy = &st;
    local.ts = &tokens;
    local.refresh();

    TRY(parseBody(tokens, local));
    return {};
}

#undef TRY

} // namespace il::io::detail
