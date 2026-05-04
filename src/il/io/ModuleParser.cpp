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
#include <iterator>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace il::io::detail {

namespace {

using il::core::Type;
using il::support::Expected;
using il::support::makeError;
using viper::parse::Cursor;
using viper::parse::SourcePos;

bool isIgnorableDirectiveTrailing(std::string_view text) {
    std::string trimmed = trim(std::string{text});
    return trimmed.empty() || trimmed.rfind("//", 0) == 0 || trimmed.front() == ';';
}

Expected<il::core::EffectAttrs> parseEffectAttrsSuffix(std::string_view text, unsigned lineNo) {
    il::core::EffectAttrs attrs;
    std::string trimmedText = trim(std::string{text});
    if (trimmedText.empty())
        return attrs;
    if (trimmedText.front() != '[' || trimmedText.back() != ']')
        return Expected<il::core::EffectAttrs>{
            il::io::makeLineErrorDiag({}, lineNo, "malformed extern attributes")};

    std::string body = trim(trimmedText.substr(1, trimmedText.size() - 2));
    if (body.empty()) {
        return Expected<il::core::EffectAttrs>{
            il::io::makeLineErrorDiag({}, lineNo, "empty extern attribute list")};
    }

    for (char &ch : body)
        if (ch == ',')
            ch = ' ';

    std::istringstream ss(body);
    std::string attr;
    while (ss >> attr) {
        if (attr == "nothrow") {
            attrs.nothrow = true;
        } else if (attr == "readonly") {
            attrs.readonly = true;
        } else if (attr == "pure") {
            attrs.pure = true;
        } else {
            std::ostringstream oss;
            oss << "unknown extern attribute '" << attr << "'";
            return Expected<il::core::EffectAttrs>{
                il::io::makeLineErrorDiag({}, lineNo, oss.str())};
        }
    }
    return attrs;
}

std::string stripDeclarationComment(std::string text) {
    std::size_t comment = std::string::npos;
    for (std::size_t candidate : {text.find('#'), text.find("//"), text.find(';')}) {
        if (candidate != std::string::npos &&
            (candidate == 0 ||
             std::isspace(static_cast<unsigned char>(text[candidate - 1])))) {
            comment = std::min(comment, candidate);
        }
    }
    if (comment != std::string::npos)
        text = trim(text.substr(0, comment));
    return text;
}

std::string directiveKeyword(const std::string &line) {
    std::istringstream ls(line);
    std::string kw;
    ls >> kw;
    return kw;
}

/// @brief Parse an extern declaration in the form `extern @name(param, ...) -> type`.
///
/// @details Whitespace around parameter tokens and the return type is normalised
/// with @ref trim, and each type token is resolved using @ref parseType.  When
/// the syntax omits required punctuation such as the `->` arrow, the helper
/// returns a diagnostic describing the missing token.  Successful parses append
/// the signature to @c ParserState::m.externs.
Expected<void> parseExtern_E(const std::string &line, ParserState &st) {
    size_t at = line.find('@');
    if (at == std::string::npos) {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing '@'")};
    }
    size_t lp = line.find('(', at);
    if (lp == std::string::npos) {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing '('")};
    }
    size_t rp = line.find(')', lp);
    if (rp == std::string::npos) {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing ')'")};
    }
    size_t arr = line.find("->", rp);
    if (arr == std::string::npos) {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing '->'")};
    }
    std::string name = trim(line.substr(at + 1, lp - at - 1));
    if (name.empty()) {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing extern name")};
    }
    if (!isValidILIdentifier(name)) {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "malformed extern name")};
    }
    std::string paramsStr = line.substr(lp + 1, rp - lp - 1);
    std::vector<Type> params;
    std::string trimmedParams = trim(paramsStr);
    if (!trimmedParams.empty()) {
        std::stringstream pss(paramsStr);
        std::string rawParam;
        while (std::getline(pss, rawParam, ',')) {
            std::string trimmed = trim(rawParam);
            if (trimmed.empty()) {
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
            if (!ok) {
                std::ostringstream oss;
                oss << "unknown type '" << trimmed << "'";
                return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, oss.str())};
            }
            if (ty.kind == Type::Kind::Void || ty.kind == Type::Kind::Error ||
                ty.kind == Type::Kind::ResumeTok) {
                std::ostringstream oss;
                oss << "unsupported extern parameter type '" << trimmed << "'";
                return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, oss.str())};
            }
            params.push_back(ty);
        }
    }
    std::string retAndAttrs = stripDeclarationComment(trim(line.substr(arr + 2)));
    std::string retStr = retAndAttrs;
    il::core::EffectAttrs attrs;
    if (const size_t attrStart = retAndAttrs.find('['); attrStart != std::string::npos) {
        retStr = trim(retAndAttrs.substr(0, attrStart));
        auto parsedAttrs = parseEffectAttrsSuffix(retAndAttrs.substr(attrStart), st.lineNo);
        if (!parsedAttrs)
            return Expected<void>{parsedAttrs.error()};
        attrs = parsedAttrs.value();
    }
    bool retOk = true;
    Type retTy = parseType(retStr, &retOk);
    if (!retOk) {
        std::ostringstream oss;
        oss << "unknown type '" << retStr << "'";
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, oss.str())};
    }
    auto hasDuplicate = std::any_of(st.m.externs.begin(),
                                    st.m.externs.end(),
                                    [&](const il::core::Extern &ext) { return ext.name == name; });
    if (hasDuplicate) {
        std::ostringstream oss;
        oss << "duplicate extern '" << name << "'";
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, oss.str())};
    }

    il::core::Extern ext{name, retTy, params};
    ext.attrs() = attrs;
    st.m.externs.push_back(std::move(ext));
    return {};
}

/// @brief Parse a global binding written as `global [linkage] [const] type @name [= init]`.
///
/// @details Validates that an assignment operator is present, trims the name
/// token to ignore incidental whitespace, and copies the quoted payload after
/// decoding escape sequences with @ref il::io::decodeEscapedString.  Missing
/// delimiters or invalid escapes produce diagnostics; otherwise a UTF-8 string
/// global is appended to @c ParserState::m.globals.
Expected<void> parseGlobal_E(const std::string &line, ParserState &st) {
    size_t at = line.find('@');
    if (at == std::string::npos) {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing '@'")};
    }

    constexpr size_t kGlobalKeywordLen = 6; // length of "global"
    const std::string qualifiers = trim(line.substr(kGlobalKeywordLen, at - kGlobalKeywordLen));
    std::istringstream qss(qualifiers);
    std::vector<std::string> tokens;
    std::string token;
    while (qss >> token) {
        tokens.push_back(token);
    }

    // Parse optional linkage keyword (export/import) before "const".
    il::core::Linkage globalLinkage = il::core::Linkage::Internal;
    if (!tokens.empty() && (tokens.front() == "export" || tokens.front() == "import")) {
        globalLinkage =
            (tokens.front() == "export") ? il::core::Linkage::Export : il::core::Linkage::Import;
        tokens.erase(tokens.begin());
    }

    bool isConst = false;
    if (!tokens.empty() && tokens.front() == "const") {
        isConst = true;
        tokens.erase(tokens.begin());
        if (tokens.empty()) {
            return Expected<void>{
                il::io::makeLineErrorDiag({}, st.lineNo, "missing global type after 'const'")};
        }
    }

    if (tokens.empty()) {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing global type")};
    }

    if (tokens.size() != 1) {
        return Expected<void>{
            il::io::makeLineErrorDiag({}, st.lineNo, "unexpected tokens before '@'")};
    }

    const std::string &typeToken = tokens.front();
    bool typeOk = true;
    Type globalType = parseType(typeToken, &typeOk);
    if (!typeOk || globalType.kind == Type::Kind::Void || globalType.kind == Type::Kind::Error ||
        globalType.kind == Type::Kind::ResumeTok) {
        std::ostringstream oss;
        oss << "unsupported global type '" << typeToken << "'";
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, oss.str())};
    }

    size_t eq = line.find('=', at);
    if (globalType.kind == Type::Kind::Str && eq == std::string::npos) {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing '='")};
    }
    const size_t nameEnd = eq == std::string::npos ? line.size() : eq;
    std::string name = trim(line.substr(at + 1, nameEnd - at - 1));
    if (name.empty()) {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing global name")};
    }
    if (!isValidILIdentifier(name)) {
        return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "malformed global name")};
    }

    std::string decoded;
    bool hasInitializer = eq != std::string::npos;
    if (globalType.kind == Type::Kind::Str) {
        size_t q1 = line.find('"', eq);
        if (q1 == std::string::npos) {
            return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing opening '\"'")};
        }
        size_t q2 = line.rfind('"');
        if (q2 == std::string::npos || q2 <= q1) {
            return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, "missing closing '\"'")};
        }
        std::string init = line.substr(q1 + 1, q2 - q1 - 1);
        auto trailingBegin = line.begin() + static_cast<std::ptrdiff_t>(q2 + 1);
        auto trailingEnd = line.end();
        auto nonWs = std::find_if(
            trailingBegin, trailingEnd, [](unsigned char ch) { return !std::isspace(ch); });
        const bool trailingIsComment =
            nonWs != trailingEnd &&
            (*nonWs == ';' ||
             (*nonWs == '/' && std::next(nonWs) != trailingEnd && *std::next(nonWs) == '/'));
        if (nonWs != trailingEnd && !trailingIsComment) {
            return Expected<void>{
                il::io::makeLineErrorDiag({}, st.lineNo, "unexpected characters after closing '\"'")};
        }
        std::string errMsg;
        if (!il::io::decodeEscapedString(init, decoded, &errMsg)) {
            return Expected<void>{il::io::makeLineErrorDiag({}, st.lineNo, errMsg)};
        }
        isConst = true;
    } else if (eq != std::string::npos) {
        decoded = trim(line.substr(eq + 1));
        if (decoded.empty()) {
            return Expected<void>{
                il::io::makeLineErrorDiag({}, st.lineNo, "missing global initializer")};
        }
    }

    st.m.globals.push_back({name, globalType, decoded, globalLinkage, isConst, hasInitializer});
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
Expected<void> parseModuleHeader_E(std::istream &is, std::string &line, ParserState &st) {
    const std::string kw0 = directiveKeyword(line);

    if (kw0 == "il") {
        if (st.sawVersion) {
            return Expected<void>{
                il::io::makeLineErrorDiag({}, st.lineNo, "duplicate 'il' version directive")};
        }
        std::istringstream ls(line);
        std::string kw;
        ls >> kw;
        std::string ver;
        if (ls >> ver) {
            st.m.version = ver;
        } else {
            return Expected<void>{
                il::io::makeLineErrorDiag({}, st.lineNo, "missing version after 'il' directive")};
        }
        std::string trailing;
        std::getline(ls, trailing);
        if (!isIgnorableDirectiveTrailing(trailing)) {
            return Expected<void>{
                il::io::makeLineErrorDiag({}, st.lineNo, "unexpected characters after version")};
        }
        st.sawVersion = true;
        return {};
    }
    if (!st.sawVersion) {
        return Expected<void>{
            il::io::makeLineErrorDiag({}, st.lineNo, "missing 'il' version directive")};
    }
    if (kw0 == "target") {
        if (st.m.target.has_value()) {
            return Expected<void>{
                il::io::makeLineErrorDiag({}, st.lineNo, "duplicate target directive")};
        }
        std::istringstream ls(line);
        std::string kw;
        ls >> kw;
        ls >> std::ws;
        if (ls.peek() != '"') {
            return Expected<void>{
                il::io::makeLineErrorDiag({}, st.lineNo, "missing quoted target triple")};
        }

        std::string triple;
        if (ls >> std::quoted(triple)) {
            std::string trailing;
            std::getline(ls, trailing);
            if (!isIgnorableDirectiveTrailing(trailing)) {
                return Expected<void>{il::io::makeLineErrorDiag(
                    {}, st.lineNo, "unexpected characters after target triple")};
            }
            st.m.target = triple;
            return {};
        }

        return Expected<void>{
            il::io::makeLineErrorDiag({}, st.lineNo, "missing quoted target triple")};
    }
    if (kw0 == "extern")
        return parseExtern_E(line, st);
    if (kw0 == "global")
        return parseGlobal_E(line, st);
    if (kw0 == "func") {
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
bool parseModuleHeader(std::istream &is, std::string &line, ParserState &st, std::ostream &err) {
    auto result = parseModuleHeader_E(is, line, st);
    if (!result) {
        il::support::printDiag(result.error(), err);
        return false;
    }
    return true;
}

} // namespace il::io::detail
