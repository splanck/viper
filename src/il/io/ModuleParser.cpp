// File: src/il/io/ModuleParser.cpp
// Purpose: Implements parsing of module-level IL directives.
// Key invariants: ParserState remains at module scope when invoked.
// Ownership/Lifetime: Directly mutates the module referenced by ParserState.
// License: MIT (see LICENSE for details).
// Links: docs/il-guide.md#reference

#include "il/io/ModuleParser.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Module.hpp"

#include "il/io/FunctionParser.hpp"
#include "il/io/ParserUtil.hpp"
#include "il/io/StringEscape.hpp"
#include "il/io/TypeParser.hpp"

#include "support/diag_expected.hpp"

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

/// Parses an extern declaration in the form `extern @name(param, ...) -> type`.
///
/// Whitespace around parameter tokens and the return type is normalized via
/// `trim`, and each type token is resolved with `parseType`, so failures are
/// reported when an unknown type name is encountered. When the syntax omits the
/// required `->` arrow the function returns an error diagnostic describing the
/// missing token. On success, the fully parsed signature is appended to
/// `ParserState::m.externs`.
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
    std::string paramsStr = line.substr(lp + 1, rp - lp - 1);
    std::vector<Type> params;
    std::stringstream pss(paramsStr);
    std::string p;
    while (std::getline(pss, p, ','))
    {
        p = trim(p);
        if (p.empty())
            continue;
        bool ok = true;
        Type ty = parseType(p, &ok);
        if (!ok)
        {
            std::ostringstream oss;
            oss << "line " << st.lineNo << ": unknown type '" << p << "'";
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

/// Parses a global string binding written as `global @name = "literal"`.
///
/// The helper validates that an assignment operator is present, trims the name
/// token to ignore incidental whitespace, and copies the quoted payload
/// verbatim. When the `=` delimiter is missing an error diagnostic is emitted;
/// otherwise the routine creates a UTF-8 string global and appends it to
/// `ParserState::m.globals`.
Expected<void> parseGlobal_E(const std::string &line, ParserState &st)
{
    size_t at = line.find('@');
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

/// Dispatches module-header directives such as `il`, `extern`, `global`, and
/// `func`.
///
/// An initial `il` line optionally supplies a version number; when omitted the
/// module defaults to version `0.1.2`. Extern directives are parsed via
/// `parseExtern_E`, thereby leveraging `trim` and `parseType` to normalize type
/// tokens, while globals and functions forward to their respective helpers. Any
/// unrecognized directive results in an error diagnostic that cites the current
/// line number; otherwise the appropriate portion of `ParserState` is updated.
Expected<void> parseModuleHeader_E(std::istream &is, std::string &line, ParserState &st)
{
    if (line.rfind("il", 0) == 0)
    {
        std::istringstream ls(line);
        std::string kw;
        ls >> kw;
        std::string ver;
        if (ls >> ver)
            st.m.version = ver;
        else
            st.m.version = "0.1.2";
        return {};
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
        return parseFunction(is, line, st);
    std::ostringstream oss;
    oss << "line " << st.lineNo << ": unexpected line: " << line;
    return Expected<void>{makeError({}, oss.str())};
}

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
