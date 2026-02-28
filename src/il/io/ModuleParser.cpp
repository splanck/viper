//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements parsing of module-level IL directives such as version banners,
// extern declarations, globals, and function definitions.  The helpers consume
// textual lines provided by @c ParserState and populate the owning module in
// place, returning structured diagnostics when malformed directives are
// encountered.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Textual IL module parser entry points.
/// @details The parser walks line-oriented module input, dispatching directives
///          like `il`, `target`, `extern`, `global`, and `func` to dedicated
///          helpers.  Each helper emits @ref il::support::Expected diagnostics so
///          command-line tools can forward precise messages to users.  The
///          implementation deliberately minimises allocations by reusing shared
///          parser state while still providing richly documented behaviour.

#include "il/internal/io/ModuleParser.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Module.hpp"

#include "il/internal/io/FunctionParser.hpp"
#include "il/internal/io/ParserUtil.hpp"
#include "il/internal/io/TypeParser.hpp"
#include "il/io/StringEscape.hpp"

#include "support/diag_expected.hpp"
#include "viper/parse/Cursor.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace il::io::detail
{

namespace
{

using il::core::Type;
using il::support::Expected;
using il::support::makeError;
using viper::parse::Cursor;
using viper::parse::SourcePos;

/// @brief Parse an extern declaration in the form `extern @name(param, ...) -> type`.
///
/// @details Whitespace around parameter tokens and the return type is normalised
/// with @ref trim, and each type token is resolved using @ref parseType.  When
/// the syntax omits required punctuation such as the `->` arrow, the helper
/// returns a diagnostic describing the missing token.  Successful parses append
/// the signature to @c ParserState::m.externs.
Expected<void> parseExtern_E(const std::string &line, ParserState &st)
{
    size_t at = line.find('@');
    if (at == std::string::npos)
    {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing '@'")};
    }
    size_t lp = line.find('(', at);
    if (lp == std::string::npos)
    {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing '('")};
    }
    size_t rp = line.find(')', lp);
    if (rp == std::string::npos)
    {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing ')'")};
    }
    size_t arr = line.find("->", rp);
    if (arr == std::string::npos)
    {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing '->'")};
    }
    std::string name = trim(line.substr(at + 1, lp - at - 1));
    if (name.empty())
    {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing extern name")};
    }
    std::string paramsStr = line.substr(lp + 1, rp - lp - 1);
    std::vector<Type> params;
    std::string trimmedParams = trim(paramsStr);
    if (!trimmedParams.empty())
    {
        std::stringstream pss(paramsStr);
        std::string rawParam;
        while (std::getline(pss, rawParam, ','))
        {
            std::string trimmed = trim(rawParam);
            if (trimmed.empty())
            {
                std::ostringstream oss;
                oss << "malformed extern parameter";
                if (!rawParam.empty())
                    oss << " '" << rawParam << "'";
                else
                    oss << " ''";
                oss << " (empty entry)";
                return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, oss.str())};
            }
            bool ok = true;
            Type ty = parseType(trimmed, &ok);
            if (!ok)
            {
                std::ostringstream oss;
                oss << "unknown type '" << trimmed << "'";
                return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, oss.str())};
            }
            params.push_back(ty);
        }
    }
    std::string retStr = trim(line.substr(arr + 2));
    bool retOk = true;
    Type retTy = parseType(retStr, &retOk);
    if (!retOk)
    {
        std::ostringstream oss;
        oss << "unknown type '" << retStr << "'";
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, oss.str())};
    }
    auto hasDuplicate = std::any_of(st.m.externs.begin(),
                                    st.m.externs.end(),
                                    [&](const il::core::Extern &ext) { return ext.name == name; });
    if (hasDuplicate)
    {
        std::ostringstream oss;
        oss << "duplicate extern '" << name << "'";
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, oss.str())};
    }

    st.m.externs.push_back({name, retTy, params});
    return {};
}

/// @brief Parse a global string binding written as `global @name = "literal"`.
///
/// @details Validates that an assignment operator is present, trims the name
/// token to ignore incidental whitespace, and copies the quoted payload after
/// decoding escape sequences with @ref il::io::decodeEscapedString.  Missing
/// delimiters or invalid escapes produce diagnostics; otherwise a UTF-8 string
/// global is appended to @c ParserState::m.globals.
Expected<void> parseGlobal_E(const std::string &line, ParserState &st)
{
    size_t at = line.find('@');
    if (at == std::string::npos)
    {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing '@'")};
    }

    constexpr size_t kGlobalKeywordLen = 6; // length of "global"
    const std::string qualifiers = trim(line.substr(kGlobalKeywordLen, at - kGlobalKeywordLen));
    std::istringstream qss(qualifiers);
    std::vector<std::string> tokens;
    std::string token;
    while (qss >> token)
    {
        tokens.push_back(token);
    }

    // Parse optional linkage keyword (export/import) before "const".
    il::core::Linkage globalLinkage = il::core::Linkage::Internal;
    if (!tokens.empty() && (tokens.front() == "export" || tokens.front() == "import"))
    {
        globalLinkage =
            (tokens.front() == "export") ? il::core::Linkage::Export : il::core::Linkage::Import;
        tokens.erase(tokens.begin());
    }

    if (!tokens.empty() && tokens.front() == "const")
    {
        tokens.erase(tokens.begin());
        if (tokens.empty())
        {
            return Expected<void>{
                il::io::makeLineErrorDiag({}, st.lineNo, "missing global type after 'const'")};
        }
    }

    if (tokens.empty())
    {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing global type")};
    }

    if (tokens.size() != 1)
    {
        return Expected<void>{
            il::io::makeLineErrorDiag({}, st.lineNo, "unexpected tokens before '@'")};
    }

    const std::string &typeToken = tokens.front();
    if (typeToken != "str")
    {
        std::ostringstream oss;
        oss << "unsupported global type '" << typeToken << "' (expected 'str')";
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, oss.str())};
    }

    size_t eq = line.find('=', at);
    if (eq == std::string::npos)
    {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing '='")};
    }
    size_t q1 = line.find('"', eq);
    if (q1 == std::string::npos)
    {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing opening '\"'")};
    }
    size_t q2 = line.rfind('"');
    if (q2 == std::string::npos || q2 <= q1)
    {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing closing '\"'")};
    }
    std::string name = trim(line.substr(at + 1, eq - at - 1));
    if (name.empty())
    {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing global name")};
    }
    std::string init = line.substr(q1 + 1, q2 - q1 - 1);
    auto trailingBegin = line.begin() + static_cast<std::ptrdiff_t>(q2 + 1);
    auto trailingEnd = line.end();
    auto nonWs = std::find_if(
        trailingBegin, trailingEnd, [](unsigned char ch) { return !std::isspace(ch); });
    if (nonWs != trailingEnd)
    {
        return Expected<void>{
            il::io::makeLineErrorDiag({}, st.lineNo, "unexpected characters after closing '\"'")};
    }
    std::string decoded;
    std::string errMsg;
    if (!il::io::decodeEscapedString(init, decoded, &errMsg))
    {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, errMsg)};
    }
    st.m.globals.push_back({name, Type(Type::Kind::Str), decoded, globalLinkage});
    return {};
}

} // namespace

/// @brief Dispatch module-header directives such as `il`, `extern`, `global`, and `func`.
///
/// @details Executes a fixed sequence of checks for every incoming line:
///          1. Ensure the file begins with an `il` version banner and that only
///             one appears.
///          2. Recognise the optional `target` triple and extract its quoted
///             payload.
///          3. Delegate `extern`, `global`, and `func` directives to specialised
///             helpers that populate the module state.
///          When a directive is malformed or appears out of order, a diagnostic
///          containing the current line number is returned so the caller can
///          surface precise feedback to the user.
Expected<void> parseModuleHeader_E(std::istream &is, std::string &line, ParserState &st)
{
    if (line.rfind("il", 0) == 0)
    {
        if (st.sawVersion)
        {
            return Expected<void>{
                il::io::makeLineErrorDiag({}, st.lineNo, "duplicate 'il' version directive")};
        }
        std::istringstream ls(line);
        std::string kw;
        ls >> kw;
        std::string ver;
        if (ls >> ver)
        {
            st.m.version = ver;
        }
        else
        {
            return Expected<void>{
                il::io::makeLineErrorDiag({}, st.lineNo, "missing version after 'il' directive")};
        }
        st.sawVersion = true;
        return {};
    }
    if (!st.sawVersion)
    {
        return Expected<void>{
            il::io::makeLineErrorDiag({}, st.lineNo, "missing 'il' version directive")};
    }
    if (line.rfind("target", 0) == 0)
    {
        std::istringstream ls(line);
        std::string kw;
        ls >> kw;
        ls >> std::ws;
        if (ls.peek() != '"')
        {
            return Expected<void>{
                il::io::makeLineErrorDiag({}, st.lineNo, "missing quoted target triple")};
        }

        std::string triple;
        if (ls >> std::quoted(triple))
        {
            st.m.target = triple;
            return {};
        }

        return Expected<void>{
            il::io::makeLineErrorDiag({}, st.lineNo, "missing quoted target triple")};
    }
    if (line.rfind("extern", 0) == 0)
    {
        constexpr size_t kExternKeywordLen = 6;
        if (line.size() == kExternKeywordLen)
            return parseExtern_E(line, st);
        const unsigned char next = static_cast<unsigned char>(line[kExternKeywordLen]);
        if (!std::isalnum(next) && next != '_')
            return parseExtern_E(line, st);
    }
    if (line.rfind("global", 0) == 0)
    {
        const bool atEnd = line.size() == 6;
        const bool hasWhitespace = !atEnd && std::isspace(static_cast<unsigned char>(line[6]));
        if (atEnd || hasWhitespace)
            return parseGlobal_E(line, st);
    }
    if (line.rfind("func", 0) == 0)
    {
        Cursor cursor{line, SourcePos{st.lineNo, 0}};
        if (cursor.consumeKeyword("func"))
            return parseFunction(is, line, st);
    }
    {
        std::ostringstream msg;
        msg << "unexpected line: " << line;
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, msg.str())};
    }
}

/// @brief Parse a single module header line and emit user-facing diagnostics.
///
/// @details Invokes @ref parseModuleHeader_E to produce structured diagnostics
/// and prints any failures to @p err so command-line tools can mirror the IL
/// parser's messaging.  Success leaves @p st updated and returns @c true.
///
/// @param is Stream supplying additional lines for nested parsers (for example functions).
/// @param line Current header line to interpret.
/// @param st Parser state accumulating module contents.
/// @param err Output stream receiving diagnostic text when parsing fails.
/// @return @c true on success, @c false when a diagnostic was emitted.
bool parseModuleHeader(std::istream &is, std::string &line, ParserState &st, std::ostream &err)
{
    auto result = parseModuleHeader_E(is, line, st);
    if (!result)
    {
        il::support::printDiag(result.error(), err);
        return false;
    }
    return true;
}

} // namespace il::io::detail
