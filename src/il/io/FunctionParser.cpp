// File: src/il/io/FunctionParser.cpp
// Purpose: Implements helpers for parsing IL function definitions.
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

std::string stripCapturedDiagMessage(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();
    constexpr std::string_view kPrefix = "error: ";
    if (text.rfind(kPrefix, 0) == 0)
        text.erase(0, kPrefix.size());
    return text;
}

Expected<void> parseInstructionShim_E(const std::string &line, ParserState &st)
{
    std::ostringstream capture;
    if (parseInstruction(line, st, capture))
        return {};
    auto message = stripCapturedDiagMessage(capture.str());
    return Expected<void>{makeError(st.curLoc, std::move(message))};
}

Expected<void> parseFunctionHeader_E(const std::string &header, ParserState &st)
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

Expected<void> parseBlockHeader_E(const std::string &header, ParserState &st)
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

Expected<void> parseFunction_E(std::istream &is, std::string &header, ParserState &st)
{
    auto headerResult = parseFunctionHeader_E(header, st);
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
            auto blockResult = parseBlockHeader_E(line.substr(0, line.size() - 1), st);
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

} // namespace

bool parseFunctionHeader(const std::string &header, ParserState &st, std::ostream &err)
{
    auto result = parseFunctionHeader_E(header, st);
    if (!result)
    {
        il::support::printDiag(result.error(), err);
        st.hasError = true;
        return false;
    }
    return true;
}

bool parseBlockHeader(const std::string &header, ParserState &st, std::ostream &err)
{
    auto result = parseBlockHeader_E(header, st);
    if (!result)
    {
        il::support::printDiag(result.error(), err);
        st.hasError = true;
        return false;
    }
    return true;
}

bool parseFunction(std::istream &is, std::string &header, ParserState &st, std::ostream &err)
{
    auto result = parseFunction_E(is, header, st);
    if (!result)
    {
        il::support::printDiag(result.error(), err);
        st.hasError = true;
        return false;
    }
    return true;
}

} // namespace il::io::detail
