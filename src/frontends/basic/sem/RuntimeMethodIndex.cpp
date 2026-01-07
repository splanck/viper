//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// File: src/frontends/basic/sem/RuntimeMethodIndex.cpp
// Purpose: Implements method index with signature parsing.
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/RuntimeMethodIndex.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

#include <cctype>

namespace il::frontends::basic
{

std::string RuntimeMethodIndex::toLower(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s)
        out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

std::string RuntimeMethodIndex::keyFor(std::string_view cls,
                                       std::string_view method,
                                       std::size_t arity)
{
    std::string key;
    key.reserve(cls.size() + method.size() + 8);
    key.append(toLower(cls));
    key.push_back('|');
    key.append(toLower(method));
    key.push_back('#');
    key.append(std::to_string(arity));
    return key;
}

BasicType RuntimeMethodIndex::mapIlToken(std::string_view tok)
{
    if (tok == "i64")
        return BasicType::Int;
    if (tok == "f64")
        return BasicType::Float;
    if (tok == "i1")
        return BasicType::Bool;
    if (tok == "str")
        return BasicType::String;
    if (tok == "void")
        return BasicType::Void;
    if (tok == "obj" || tok == "ptr")
        return BasicType::Object;
    return BasicType::Unknown;
}

bool RuntimeMethodIndex::parseSignature(std::string_view sig, RuntimeMethodInfo &out)
{
    // Expect: ret(args)
    auto lparen = sig.find('(');
    auto rparen = sig.rfind(')');
    if (lparen == std::string_view::npos || rparen == std::string_view::npos || rparen < lparen)
        return false;
    std::string_view retTok = sig.substr(0, lparen);
    // trim spaces
    while (!retTok.empty() && std::isspace(static_cast<unsigned char>(retTok.front())))
        retTok.remove_prefix(1);
    while (!retTok.empty() && std::isspace(static_cast<unsigned char>(retTok.back())))
        retTok.remove_suffix(1);
    out.ret = mapIlToken(retTok);
    std::string_view args = sig.substr(lparen + 1, rparen - lparen - 1);
    out.args.clear();
    std::size_t pos = 0;
    while (pos < args.size())
    {
        // skip spaces and commas
        while (pos < args.size() && (args[pos] == ' ' || args[pos] == ','))
            ++pos;
        if (pos >= args.size())
            break;
        std::size_t start = pos;
        while (pos < args.size() && args[pos] != ',' &&
               !std::isspace(static_cast<unsigned char>(args[pos])))
            ++pos;
        std::string_view tok = args.substr(start, pos - start);
        if (!tok.empty())
            out.args.push_back(mapIlToken(tok));
    }
    return true;
}

void RuntimeMethodIndex::seed(const std::vector<il::runtime::RuntimeClass> &classes)
{
    map_.clear();
    for (const auto &cls : classes)
    {
        for (const auto &m : cls.methods)
        {
            RuntimeMethodInfo info;
            info.target = m.target ? m.target : "";
            if (!parseSignature(m.signature ? m.signature : "", info))
                continue;
            map_[keyFor(cls.qname, m.name, info.args.size())] = std::move(info);
        }
    }
}

std::optional<RuntimeMethodInfo> RuntimeMethodIndex::find(std::string_view classQName,
                                                          std::string_view method,
                                                          std::size_t arity) const
{
    auto it = map_.find(keyFor(classQName, method, arity));
    if (it == map_.end())
        return std::nullopt;
    return it->second;
}

std::vector<std::string> RuntimeMethodIndex::candidates(std::string_view classQName,
                                                        std::string_view method) const
{
    std::vector<std::string> out;
    std::string prefix;
    prefix.reserve(classQName.size() + method.size() + 2);
    prefix.append(toLower(classQName));
    prefix.push_back('|');
    prefix.append(toLower(method));
    prefix.push_back('#');
    for (const auto &p : map_)
    {
        const std::string &k = p.first;
        if (k.rfind(prefix, 0) == 0)
        {
            // key format: <cls>|<method>#<arity>
            auto pos = k.rfind('#');
            std::string ar = pos != std::string::npos ? k.substr(pos + 1) : std::string("?");
            out.push_back(std::string(method) + "/" + ar);
        }
    }
    return out;
}

RuntimeMethodIndex &runtimeMethodIndex()
{
    static RuntimeMethodIndex idx;
    return idx;
}

} // namespace il::frontends::basic
