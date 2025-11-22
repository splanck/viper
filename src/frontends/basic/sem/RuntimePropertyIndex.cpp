//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// File: src/frontends/basic/sem/RuntimePropertyIndex.cpp
// Purpose: Implements property index built from runtime class catalog.
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/RuntimePropertyIndex.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

#include <cctype>

namespace il::frontends::basic
{

std::string RuntimePropertyIndex::toLower(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s)
        out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

std::string RuntimePropertyIndex::keyFor(std::string_view cls, std::string_view prop)
{
    std::string key;
    key.reserve(cls.size() + prop.size() + 1);
    key.append(toLower(cls));
    key.push_back('|');
    key.append(toLower(prop));
    return key;
}

void RuntimePropertyIndex::seed(const std::vector<il::runtime::RuntimeClass> &classes)
{
    map_.clear();
    for (const auto &cls : classes)
    {
        for (const auto &p : cls.properties)
        {
            RuntimePropertyInfo info;
            info.type = p.type ? p.type : "";
            info.getter = p.getter ? p.getter : "";
            info.setter = p.setter ? p.setter : "";
            info.readonly = p.readonly || info.setter.empty();
            map_[keyFor(cls.qname, p.name)] = std::move(info);
        }
    }
}

std::optional<RuntimePropertyInfo> RuntimePropertyIndex::find(std::string_view classQName,
                                                              std::string_view propName) const
{
    auto it = map_.find(keyFor(classQName, propName));
    if (it == map_.end())
        return std::nullopt;
    return it->second;
}

RuntimePropertyIndex &runtimePropertyIndex()
{
    static RuntimePropertyIndex idx;
    return idx;
}

} // namespace il::frontends::basic

