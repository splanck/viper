// File: src/il/io/ModuleParser.cpp
// Purpose: Implements parsing of module-level IL directives.
// Key invariants: ParserState remains at module scope when invoked.
// Ownership/Lifetime: Directly mutates the module referenced by ParserState.
// Links: docs/il-spec.md

#include "il/io/ModuleParser.hpp"

#include "il/io/FunctionParser.hpp"
#include "il/io/ParserUtil.hpp"
#include "il/io/TypeParser.hpp"

#include <sstream>
#include <vector>

namespace il::io::detail
{

using il::core::Type;

bool parseModuleHeader(std::istream &is, std::string &line, ParserState &st, std::ostream &err)
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
        return true;
    }
    if (line.rfind("extern", 0) == 0)
    {
        size_t at = line.find('@');
        size_t lp = line.find('(', at);
        size_t rp = line.find(')', lp);
        size_t arr = line.find("->", rp);
        if (arr == std::string::npos)
        {
            err << "line " << st.lineNo << ": missing '->'\n";
            st.hasError = true;
            return false;
        }
        std::string name = line.substr(at + 1, lp - at - 1);
        std::string paramsStr = line.substr(lp + 1, rp - lp - 1);
        std::vector<Type> params;
        std::stringstream pss(paramsStr);
        std::string p;
        bool typesOk = true;
        while (std::getline(pss, p, ','))
        {
            p = trim(p);
            if (p.empty())
                continue;
            bool ok = true;
            Type ty = parseType(p, &ok);
            if (!ok)
            {
                err << "line " << st.lineNo << ": unknown type '" << p << "'\n";
                st.hasError = true;
                typesOk = false;
            }
            else
                params.push_back(ty);
        }
        std::string retStr = trim(line.substr(arr + 2));
        bool retOk = true;
        Type retTy = parseType(retStr, &retOk);
        if (!retOk)
        {
            err << "line " << st.lineNo << ": unknown type '" << retStr << "'\n";
            st.hasError = true;
            typesOk = false;
        }
        if (!typesOk)
            return false;
        st.m.externs.push_back({name, retTy, params});
        return true;
    }
    if (line.rfind("global", 0) == 0)
    {
        size_t at = line.find('@');
        size_t eq = line.find('=', at);
        if (eq == std::string::npos)
        {
            err << "line " << st.lineNo << ": missing '='\n";
            st.hasError = true;
            return false;
        }
        size_t q1 = line.find('"', eq);
        size_t q2 = line.rfind('"');
        std::string name = trim(line.substr(at + 1, eq - at - 1));
        std::string init = line.substr(q1 + 1, q2 - q1 - 1);
        st.m.globals.push_back({name, Type(Type::Kind::Str), init});
        return true;
    }
    if (line.rfind("func", 0) == 0)
        return parseFunction(is, line, st, err);
    err << "line " << st.lineNo << ": unexpected line: " << line << "\n";
    return false;
}

} // namespace il::io::detail
