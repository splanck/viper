//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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

#include "il/io/ModuleParser.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Module.hpp"

#include "il/io/FunctionParser.hpp"
#include "il/io/ParserUtil.hpp"
#include "il/io/StringEscape.hpp"
#include "il/io/TypeParser.hpp"

#include "support/diag_expected.hpp"
#include "viper/parse/Cursor.h"

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
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": missing '@'";
        return Expected<void>{makeError({}, oss.str())};
    }
    size_t lp = line.find('(', at);
    if (lp == std::string::npos)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": missing '('";
        return Expected<void>{makeError({}, oss.str())};
    }
    size_t rp = line.find(')', lp);
    if (rp == std::string::npos)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": missing ')'";
        return Expected<void>{makeError({}, oss.str())};
    }
    size_t arr = line.find("->", rp);
    if (arr == std::string::npos)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": missing '->'";
        return Expected<void>{makeError({}, oss.str())};
    }
    std::string name = trim(line.substr(at + 1, lp - at - 1));
    if (name.empty())
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": missing extern name";
        return Expected<void>{makeError({}, oss.str())};
    }
    std::string paramsStr = line.substr(lp + 1, rp - lp - 1);
    std::vector<Type> params;
    std::stringstream pss(paramsStr);
    std::string rawParam;
    while (std::getline(pss, rawParam, ','))
    {
        std::string trimmed = trim(rawParam);
        if (trimmed.empty())
        {
            std::ostringstream oss;
            oss << "line " << st.lineNo << ": malformed extern parameter";
            if (!rawParam.empty())
                oss << " '" << rawParam << "'";
            else
                oss << " ''";
            oss << " (empty entry)";
            return Expected<void>{makeError({}, oss.str())};
        }
        bool ok = true;
        Type ty = parseType(trimmed, &ok);
        if (!ok)
        {
            std::ostringstream oss;
            oss << "line " << st.lineNo << ": unknown type '" << trimmed << "'";
            return Expected<void>{makeError({}, oss.str())};
        }
        params.push_back(ty);
    }
    std::string retStr = trim(line.substr(arr + 2));
    bool retOk = true;
    Type retTy = parseType(retStr, &retOk);
    if (!retOk)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": unknown type '" << retStr << "'";
        return Expected<void>{makeError({}, oss.str())};
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
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": missing '@'";
        return Expected<void>{makeError({}, oss.str())};
    }

    size_t eq = line.find('=', at);
    if (eq == std::string::npos)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": missing '='";
        return Expected<void>{makeError({}, oss.str())};
    }
    size_t q1 = line.find('"', eq);
    if (q1 == std::string::npos)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": missing opening '\"'";
        return Expected<void>{makeError({}, oss.str())};
    }
    size_t q2 = line.rfind('"');
    if (q2 == std::string::npos || q2 <= q1)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": missing closing '\"'";
        return Expected<void>{makeError({}, oss.str())};
    }
    std::string name = trim(line.substr(at + 1, eq - at - 1));
    if (name.empty())
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": missing global name";
        return Expected<void>{makeError({}, oss.str())};
    }
    std::string init = line.substr(q1 + 1, q2 - q1 - 1);
    std::string decoded;
    std::string errMsg;
    if (!il::io::decodeEscapedString(init, decoded, &errMsg))
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": " << errMsg;
        return Expected<void>{makeError({}, oss.str())};
    }
    st.m.globals.push_back({name, Type(Type::Kind::Str), decoded});
    return {};
}

} // namespace

/// @brief Dispatch module-header directives such as `il`, `extern`, `global`, and `func`.
///
/// @details Handles the leading `il` version directive, parses target triples,
/// and forwards externs/globals/functions
/// to their specialised helpers.  Unrecognised directives return diagnostics
/// that include the current line number so the caller can surface precise
/// feedback.
Expected<void> parseModuleHeader_E(std::istream &is, std::string &line, ParserState &st)
{
    if (line.rfind("il", 0) == 0)
    {
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
            std::ostringstream oss;
            oss << "line " << st.lineNo << ": missing version after 'il' directive";
            return Expected<void>{makeError({}, oss.str())};
        }
        st.sawVersion = true;
        return {};
    }
    if (!st.sawVersion)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": missing 'il' version directive";
        return Expected<void>{il::support::makeError({}, oss.str())};
    }
    if (line.rfind("target", 0) == 0)
    {
        std::istringstream ls(line);
        std::string kw;
        ls >> kw;
        ls >> std::ws;
        if (ls.peek() != '"')
        {
            std::ostringstream oss;
            oss << "line " << st.lineNo << ": missing quoted target triple";
            return Expected<void>{makeError({}, oss.str())};
        }

        std::string triple;
        if (ls >> std::quoted(triple))
        {
            st.m.target = triple;
            return {};
        }

        std::ostringstream oss;
        oss << "line " << st.lineNo << ": missing quoted target triple";
        return Expected<void>{makeError({}, oss.str())};
    }
    if (line.rfind("extern", 0) == 0)
        return parseExtern_E(line, st);
    if (line.rfind("global", 0) == 0)
        return parseGlobal_E(line, st);
    if (line.rfind("func", 0) == 0)
    {
        Cursor cursor{line, SourcePos{st.lineNo, 0}};
        if (cursor.consumeKeyword("func"))
            return parseFunction(is, line, st);
    }
    std::ostringstream oss;
    oss << "line " << st.lineNo << ": unexpected line: " << line;
    return Expected<void>{makeError({}, oss.str())};
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
