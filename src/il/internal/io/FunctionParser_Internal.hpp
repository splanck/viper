//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/internal/io/FunctionParser_Internal.hpp
// Purpose: Internal declarations shared between FunctionParser implementation
//          files. Contains the TokenStream class for line-based tokenization,
//          parser state wrappers, and common utility functions.
// Key invariants: Used only by FunctionParser_*.cpp files; not part of public API.
// Ownership/Lifetime: Header-only utilities with no ownership semantics.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "il/core/Param.hpp"
#include "il/internal/io/ParserState.hpp"
#include "il/internal/io/ParserUtil.hpp"
#include "support/diag_expected.hpp"
#include "viper/parse/Cursor.h"

#include <cctype>
#include <istream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::io::detail
{

using il::core::Param;
using il::core::Type;
using il::support::Diag;
using il::support::Expected;
using il::support::makeError;
using viper::parse::Cursor;
using viper::parse::SourcePos;

// Alias for the public ParserState to distinguish from parser_impl::ParserState
using LegacyParserState = ::il::io::detail::ParserState;
using Error = Diag;

// ============================================================================
// Token types for function body parsing
// ============================================================================

/// @brief Classifies lines encountered while parsing an IL function body.
enum class TokenKind
{
    Skip,         ///< Line was blank or a comment; should be skipped.
    CloseBrace,   ///< Closing brace '}' marking the end of the function body.
    BlockLabel,   ///< A basic block label line (ending with ':').
    LocDirective, ///< A `.loc` source location directive.
    Instruction,  ///< An IL instruction line to parse.
    End,          ///< End of input reached before a closing brace.
};

// ============================================================================
// TokenStream - line-based tokenization for function bodies
// ============================================================================

/// @brief Line-based tokenizer for function body parsing.
/// @details Reads lines from the input stream, skipping comments and blank lines,
///          and classifies each line as a block label, instruction, directive, etc.
class TokenStream
{
  public:
    TokenStream(std::istream &stream, LegacyParserState &legacy)
        : stream_(&stream), legacy_(&legacy)
    {
    }

    [[nodiscard]] TokenKind kind() const noexcept
    {
        return token_;
    }

    [[nodiscard]] const std::string &line() const noexcept
    {
        return line_;
    }

    [[nodiscard]] LegacyParserState &legacy() noexcept
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
    LegacyParserState *legacy_ = nullptr;
    std::string line_;
    TokenKind token_ = TokenKind::Skip;
};

// ============================================================================
// Internal parser state wrapper
// ============================================================================

namespace parser_impl
{

/// @brief Internal state wrapper that bridges TokenStream with LegacyParserState.
struct ParserState
{
    il::core::Module *mod = nullptr;
    il::core::Function *fn = nullptr;
    il::core::BasicBlock *cur = nullptr;
    il::support::SourceLoc loc{};
    il::support::DiagnosticEngine *diags = nullptr;
    TokenStream *ts = nullptr;
    LegacyParserState *legacy = nullptr;

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

// ============================================================================
// Data structures for prototype parsing
// ============================================================================

/// @brief Parsed function prototype: return type and parameter list.
struct Prototype
{
    Type retType;              ///< Declared return type of the function.
    std::vector<Param> params; ///< Ordered parameter list with types and names.
};

/// @brief Result of parsing a function prototype header line.
/// @details Contains the parsed prototype and any trailing calling convention
///          segment that follows the parameter list.
struct PrototypeParseResult
{
    Prototype proto;                     ///< Parsed return type and parameters.
    std::string_view callingConvSegment; ///< Trailing text after the parameter list.
};

/// @brief Calling convention annotation parsed from function headers.
/// @details Currently only the default calling convention is supported.
///          Future extensions may add fastcall, stdcall, etc.
enum class CallingConv
{
    Default, ///< Standard platform calling convention.
};

/// @brief Parsed function attributes (currently empty).
/// @details Placeholder for future attribute parsing (nothrow, readonly, etc.).
struct Attrs
{
};

/// @brief Complete parsed function header including name, prototype, and metadata.
struct FunctionHeader
{
    std::string name;           ///< Function identifier.
    Prototype proto;            ///< Return type and parameter list.
    CallingConv cc;             ///< Calling convention annotation.
    Attrs attrs;                ///< Parsed function attributes.
    il::support::SourceLoc loc; ///< Source location of the function declaration.
};

// ============================================================================
// Snapshot for parser state rollback on error
// ============================================================================

/// @brief Captures parser state for transactional rollback on parse failure.
/// @details On construction, saves all mutable parser state (function context,
///          SSA mappings, pending branches, function count). If the parse
///          succeeds, the caller calls discard() to commit. On destruction
///          without discard(), the snapshot restores the saved state and removes
///          any functions that were added during the failed parse.
struct ParserSnapshot
{
    LegacyParserState &state;      ///< Reference to the parser state being snapshotted.
    il::core::Function *curFn;     ///< Saved current function pointer.
    il::core::BasicBlock *curBB;   ///< Saved current basic block pointer.
    il::support::SourceLoc curLoc; ///< Saved source location.
    std::unordered_map<std::string, unsigned> tempIds;       ///< Saved SSA name-to-id mappings.
    unsigned nextTemp;                                       ///< Saved next temporary ID counter.
    std::unordered_map<std::string, size_t> blockParamCount; ///< Saved block parameter counts.
    std::vector<LegacyParserState::PendingBr> pendingBrs;    ///< Saved pending branch targets.
    size_t functionCount; ///< Number of functions at snapshot time.
    bool active = true;   ///< True if rollback should occur on destruction.

    explicit ParserSnapshot(LegacyParserState &st)
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

// ============================================================================
// Utility functions
// ============================================================================

/// @brief Trim whitespace from a string_view.
inline std::string_view trimView(std::string_view text)
{
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;
    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return text.substr(begin, end - begin);
}

/// @brief Create a line-prefixed error diagnostic.
template <class T> Expected<T> lineError(unsigned lineNo, const std::string &message)
{
    std::ostringstream oss;
    oss << "line " << lineNo << ": " << message;
    return Expected<T>{makeError({}, oss.str())};
}

/// @brief Get source position from cursor.
inline SourcePos cursorPos(const Cursor &cur)
{
    return cur.pos();
}

/// @brief Create a syntax error with optional context.
inline Error makeSyntaxError(SourcePos pos, std::string_view msg, std::string_view near)
{
    std::ostringstream body;
    body << msg;
    if (!near.empty())
        body << " '" << near << "'";
    return lineError<void>(pos.line, body.str()).error();
}

/// @brief Normalises diagnostics captured from instruction parsing.
///
/// The instruction parser reports errors prefixed with "error: " and terminated by
/// trailing newlines. This helper strips that prefix and trailing newline/carriage
/// returns so that downstream diagnostics emitted through @ref
/// il::support::printDiag are consistent across call sites.
inline std::string stripCapturedDiagMessage(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();
    constexpr std::string_view kPrefix = "error: ";
    if (text.rfind(kPrefix, 0) == 0)
        text.erase(0, kPrefix.size());
    return text;
}

/// @brief Human-readable description of a token kind.
inline std::string_view describeTokenKind(TokenKind token)
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

/// @brief Extract the text that caused a parse error.
inline std::string describeOffendingToken(const parser_impl::ParserState &state)
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

} // namespace il::io::detail
