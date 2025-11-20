//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/sem/OverloadResolution.cpp
// Purpose: Resolve method overloads (incl. property accessors) on a class.
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/OverloadResolution.hpp"

#include "frontends/basic/StringUtils.hpp"

namespace il::frontends::basic::sem
{

namespace
{
// Map semantic Type to AST Type used in OOP signatures for comparison
static inline bool isExactMatch(::il::frontends::basic::Type expect,
                                ::il::frontends::basic::Type got) noexcept
{
    return expect == got;
}

static inline bool isWideningAllowed(::il::frontends::basic::Type expect,
                                     ::il::frontends::basic::Type got) noexcept
{
    // Only numeric widening: int->float64; integer to integer widening already canonicalized to I64.
    // For this frontend, INT maps to I64 and SINGLE/DOUBLE to F64; allow I64->F64.
    return (expect == ::il::frontends::basic::Type::F64 && got == ::il::frontends::basic::Type::I64);
}

static inline std::string signatureText(std::string_view qclass,
                                        std::string_view name,
                                        const ClassInfo::MethodInfo &mi)
{
    std::string s;
    s.reserve(64);
    s += std::string(qclass);
    s += ".";
    s += std::string(name);
    s += "(";
    for (size_t i = 0; i < mi.sig.paramTypes.size(); ++i)
    {
        if (i)
            s += ", ";
        auto t = mi.sig.paramTypes[i];
        switch (t)
        {
            case Type::I64: s += "INTEGER"; break;
            case Type::F64: s += "DOUBLE"; break;
            case Type::Str: s += "STRING"; break;
            case Type::Bool: s += "BOOLEAN"; break;
        }
    }
    s += ")";
    return s;
}
} // namespace

std::optional<ResolvedMethod> resolveMethodOverload(const OopIndex &index,
                                                    std::string_view qualifiedClass,
                                                    std::string_view methodName,
                                                    bool isStatic,
                                                    const std::vector<Type> &argTypes,
                                                    std::string_view currentClass,
                                                    DiagnosticEmitter *de,
                                                    il::support::SourceLoc loc)
{
    // Normalize class casing using index metadata
    const ClassInfo *ci = index.findClass(std::string(qualifiedClass));
    if (!ci)
        return std::nullopt;

    // Build candidate list: methodName plus property accessors matching arity.
    struct Cand { const ClassInfo::MethodInfo *mi; std::string name; };
    std::vector<Cand> cands;
    auto addIf = [&](const std::string &name)
    {
        auto it = ci->methods.find(name);
        if (it != ci->methods.end())
            cands.push_back(Cand{&it->second, name});
    };
    addIf(std::string(methodName));
    // Properties: get_Name has 0 user params; set_Name has 1 user param.
    if (argTypes.size() == 0)
        addIf("get_" + std::string(methodName));
    if (argTypes.size() == 1)
        addIf("set_" + std::string(methodName));

    // Filter: static/instance and access control
    std::vector<Cand> filtered;
    filtered.reserve(cands.size());
    for (const auto &c : cands)
    {
        if (c.mi->isStatic != isStatic)
            continue;
        if (c.mi->sig.access == Access::Private && ci->qualifiedName != currentClass)
            continue;
        filtered.push_back(c);
    }

    if (filtered.empty())
    {
        if (de)
        {
            std::string msg = "no matching overload for '" + std::string(methodName) + "(";
            for (size_t i = 0; i < argTypes.size(); ++i)
            {
                if (i)
                    msg += ", ";
                switch (argTypes[i])
                {
                    case Type::I64: msg += "INTEGER"; break;
                    case Type::F64: msg += "DOUBLE"; break;
                    case Type::Str: msg += "STRING"; break;
                    case Type::Bool: msg += "BOOLEAN"; break;
                }
            }
            msg += ")'";
            de->emit(il::support::Severity::Error,
                     "E_OVERLOAD_NO_MATCH",
                     loc,
                     static_cast<uint32_t>(methodName.size()),
                     std::move(msg));
        }
        return std::nullopt;
    }

    // Rank: exact match wins; else allow widening numeric conversion (I64->F64) per param.
    int bestScore = -1;
    std::vector<size_t> bestIdx;
    for (size_t i = 0; i < filtered.size(); ++i)
    {
        const auto &mi = *filtered[i].mi;
        if (mi.sig.paramTypes.size() != argTypes.size())
            continue;
        int score = 0;
        bool viable = true;
        for (size_t p = 0; p < argTypes.size(); ++p)
        {
            if (isExactMatch(mi.sig.paramTypes[p], argTypes[p]))
            {
                score += 2;
                continue;
            }
            if (isWideningAllowed(mi.sig.paramTypes[p], argTypes[p]))
            {
                score += 1;
                continue;
            }
            viable = false; // narrowing or incompatible
            break;
        }
        if (!viable)
            continue;
        if (score > bestScore)
        {
            bestScore = score;
            bestIdx.clear();
            bestIdx.push_back(i);
        }
        else if (score == bestScore)
        {
            bestIdx.push_back(i);
        }
    }

    if (bestIdx.empty())
    {
        if (de)
        {
            std::string msg = "no viable overload for '" + std::string(methodName) + "'";
            de->emit(il::support::Severity::Error,
                     "E_OVERLOAD_NO_MATCH",
                     loc,
                     static_cast<uint32_t>(methodName.size()),
                     std::move(msg));
        }
        return std::nullopt;
    }
    if (bestIdx.size() > 1)
    {
        if (de)
        {
            std::string msg = "ambiguous call to '" + std::string(methodName) + "' among: ";
            bool first = true;
            for (size_t i : bestIdx)
            {
                if (!first)
                    msg += "; ";
                first = false;
                msg += signatureText(ci->qualifiedName, filtered[i].name, *filtered[i].mi);
            }
            de->emit(il::support::Severity::Error,
                     "E_OVERLOAD_AMBIGUOUS",
                     loc,
                     static_cast<uint32_t>(methodName.size()),
                     std::move(msg));
        }
        return std::nullopt;
    }

    const auto &win = filtered[bestIdx.front()];
    return ResolvedMethod{ci, win.mi, ci->qualifiedName, win.name};
}

} // namespace il::frontends::basic::sem

