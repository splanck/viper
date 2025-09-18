// File: src/il/io/FunctionParser.cpp
// Purpose: Implements helpers for parsing IL function definitions.
// Key invariants: ParserState maintains current function and block context.
// Ownership/Lifetime: Populates functions directly within the supplied module.
// Links: docs/il-spec.md

#include "il/io/FunctionParser.hpp"

#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "il/io/InstrParser.hpp"
#include "il/io/ParserUtil.hpp"
#include "il/io/TypeParser.hpp"

#include <cstdint>
#include <sstream>

namespace il::io::detail
{

using il::core::Param;
using il::core::Type;

bool parseFunctionHeader(const std::string &header, ParserState &st, std::ostream &err)
{
    size_t at = header.find('@');
    size_t lp = header.find('(', at);
    size_t rp = header.find(')', lp);
    size_t arr = header.find("->", rp);
    size_t lb = header.find('{', arr);
    if (arr == std::string::npos || lb == std::string::npos)
    {
        err << "line " << st.lineNo << ": malformed function header\n";
        st.hasError = true;
        return false;
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
    return true;
}

bool parseBlockHeader(const std::string &header, ParserState &st, std::ostream &err)
{
    size_t lp = header.find('(');
    std::vector<Param> bparams;
    std::string label = trim(header);
    if (lp != std::string::npos)
    {
        size_t rp = header.find(')', lp);
        if (rp == std::string::npos)
        {
            err << "line " << st.lineNo << ": mismatched ')\n";
            return false;
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
                err << "line " << st.lineNo << ": bad param\n";
                return false;
            }
            std::string nm = trim(q.substr(0, col));
            if (!nm.empty() && nm[0] == '%')
                nm = nm.substr(1);
            std::string tyStr = trim(q.substr(col + 1));
            bool ok = true;
            Type ty = parseType(tyStr, &ok);
            if (!ok || ty.kind == Type::Kind::Void)
            {
                err << "line " << st.lineNo << ": unknown param type\n";
                return false;
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
                err << "line " << it->line << ": bad arg count\n";
                return false;
            }
            it = st.pendingBrs.erase(it);
        }
        else
            ++it;
    }
    return true;
}

bool parseFunction(std::istream &is, std::string &header, ParserState &st, std::ostream &err)
{
    if (!parseFunctionHeader(header, st, err))
        return false;

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
            return true;
        }
        if (line.back() == ':')
        {
            if (!parseBlockHeader(line.substr(0, line.size() - 1), st, err))
                return false;
            continue;
        }
        if (!st.curBB)
        {
            err << "line " << st.lineNo << ": instruction outside block\n";
            return false;
        }
        if (line.rfind(".loc", 0) == 0)
        {
            std::istringstream ls(line.substr(4));
            uint32_t fid = 0, ln = 0, col = 0;
            ls >> fid >> ln >> col;
            st.curLoc = {fid, ln, col};
            continue;
        }
        if (!parseInstruction(line, st, err))
            return false;
    }
    return true;
}

} // namespace il::io::detail
