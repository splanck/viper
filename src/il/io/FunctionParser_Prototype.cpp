//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/io/FunctionParser_Prototype.cpp
// Purpose: Implementation of function prototype parsing. Handles the "func @name(...) -> type {"
//          syntax including parameter parsing, calling convention, and attributes.
// Key invariants: Creates new function entries in the module with proper temp ID setup.
// Ownership/Lifetime: Populates functions directly within the supplied module.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/internal/io/FunctionParser.hpp"
#include "il/internal/io/FunctionParser_Internal.hpp"
#include "il/internal/io/TypeParser.hpp"

#include <array>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace il::io::detail
{

namespace
{

/// @brief Parse a single parameter from "type %name" or "%name: type" syntax.
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

/// @brief Parse the function symbol name from "@name(" syntax.
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

/// @brief Parse the function prototype: "(params) -> rettype".
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

/// @brief Parse an optional calling convention specifier.
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

/// @brief Parse function attributes before the opening brace.
Expected<Attrs> parseAttributes(Cursor &cur)
{
    cur.skipWs();
    if (cur.atEnd())
        return Expected<Attrs>{makeSyntaxError(cursorPos(cur), "unexpected end of header", {})};
    if (!cur.consume('{'))
        return Expected<Attrs>{makeSyntaxError(cursorPos(cur), "malformed function header", {})};
    return Attrs{};
}

/// @brief Parse an optional source location directive.
Expected<il::support::SourceLoc> parseOptionalLoc(Cursor &cur)
{
    cur.skipWs();
    return il::support::SourceLoc{};
}

} // namespace

// ============================================================================
// Public API
// ============================================================================

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

} // namespace il::io::detail
