//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// File: src/frontends/basic/sem/RuntimeMethodIndex.cpp
// Purpose: Implements method index using RuntimeRegistry for lookup.
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/RuntimeMethodIndex.hpp"

namespace il::frontends::basic
{

BasicType toBasicType(il::runtime::ILScalarType t)
{
    switch (t)
    {
    case il::runtime::ILScalarType::I64:
        return BasicType::Int;
    case il::runtime::ILScalarType::F64:
        return BasicType::Float;
    case il::runtime::ILScalarType::Bool:
        return BasicType::Bool;
    case il::runtime::ILScalarType::String:
        return BasicType::String;
    case il::runtime::ILScalarType::Void:
        return BasicType::Void;
    case il::runtime::ILScalarType::Object:
        return BasicType::Object;
    case il::runtime::ILScalarType::Unknown:
    default:
        return BasicType::Unknown;
    }
}

void RuntimeMethodIndex::seed()
{
    // No-op: RuntimeRegistry handles indexing.
}

std::optional<RuntimeMethodInfo> RuntimeMethodIndex::find(std::string_view classQName,
                                                          std::string_view method,
                                                          std::size_t arity) const
{
    const auto &registry = il::runtime::RuntimeRegistry::instance();
    auto parsed = registry.findMethod(classQName, method, arity);
    if (!parsed)
        return std::nullopt;

    RuntimeMethodInfo info;
    info.target = parsed->target ? parsed->target : "";
    info.ret = toBasicType(parsed->signature.returnType);
    info.args.reserve(parsed->signature.params.size());
    for (auto p : parsed->signature.params)
        info.args.push_back(toBasicType(p));

    return info;
}

std::vector<std::string> RuntimeMethodIndex::candidates(std::string_view classQName,
                                                        std::string_view method) const
{
    const auto &registry = il::runtime::RuntimeRegistry::instance();
    return registry.methodCandidates(classQName, method);
}

RuntimeMethodIndex &runtimeMethodIndex()
{
    static RuntimeMethodIndex idx;
    return idx;
}

} // namespace il::frontends::basic
