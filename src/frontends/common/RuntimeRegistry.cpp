//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/RuntimeRegistry.cpp
// Purpose: Implementation of the unified runtime API registry.
//
//===----------------------------------------------------------------------===//

#include "frontends/common/RuntimeRegistry.hpp"

#include "il/runtime/RuntimeNameMap.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

#include <algorithm>
#include <cctype>

namespace il::frontends::common
{

namespace
{

/// @brief Parse a signature string to extract return type and argument types.
/// @param sig Signature like "i64(str,i64)" or "void(obj)"
/// @return Pair of return type string and vector of arg type strings.
std::pair<std::string_view, std::vector<std::string_view>> parseSignatureString(
    std::string_view sig)
{
    std::vector<std::string_view> argTypes;

    // Find the opening paren
    auto parenPos = sig.find('(');
    if (parenPos == std::string_view::npos)
    {
        return {sig, argTypes};
    }

    std::string_view returnType = sig.substr(0, parenPos);

    // Find closing paren
    auto closePos = sig.find(')', parenPos);
    if (closePos == std::string_view::npos)
    {
        return {returnType, argTypes};
    }

    std::string_view argsStr = sig.substr(parenPos + 1, closePos - parenPos - 1);

    // Parse comma-separated args
    size_t start = 0;
    while (start < argsStr.size())
    {
        // Skip whitespace
        while (start < argsStr.size() && std::isspace(argsStr[start]))
            ++start;

        if (start >= argsStr.size())
            break;

        // Find end of this type (comma or end)
        size_t end = argsStr.find(',', start);
        if (end == std::string_view::npos)
            end = argsStr.size();

        // Trim trailing whitespace
        size_t typeEnd = end;
        while (typeEnd > start && std::isspace(argsStr[typeEnd - 1]))
            --typeEnd;

        if (typeEnd > start)
        {
            argTypes.push_back(argsStr.substr(start, typeEnd - start));
        }

        start = end + 1;
    }

    return {returnType, argTypes};
}

} // namespace

RuntimeReturnKind RuntimeRegistry::typeToReturnKind(std::string_view typeAbbrev)
{
    if (typeAbbrev == "void")
        return RuntimeReturnKind::Void;
    if (typeAbbrev == "i64" || typeAbbrev == "i32" || typeAbbrev == "i16" || typeAbbrev == "i8")
        return RuntimeReturnKind::Integer;
    if (typeAbbrev == "i1")
        return RuntimeReturnKind::Boolean;
    if (typeAbbrev == "f64" || typeAbbrev == "f32")
        return RuntimeReturnKind::Float;
    if (typeAbbrev == "str")
        return RuntimeReturnKind::String;
    if (typeAbbrev == "obj")
        return RuntimeReturnKind::Object;
    if (typeAbbrev == "ptr")
        return RuntimeReturnKind::Pointer;
    return RuntimeReturnKind::Unknown;
}

RuntimeArgKind RuntimeRegistry::typeToArgKind(std::string_view typeAbbrev)
{
    if (typeAbbrev == "i64" || typeAbbrev == "i32" || typeAbbrev == "i16" || typeAbbrev == "i8")
        return RuntimeArgKind::Integer;
    if (typeAbbrev == "i1")
        return RuntimeArgKind::Boolean;
    if (typeAbbrev == "f64" || typeAbbrev == "f32")
        return RuntimeArgKind::Float;
    if (typeAbbrev == "str")
        return RuntimeArgKind::String;
    if (typeAbbrev == "obj")
        return RuntimeArgKind::Object;
    if (typeAbbrev == "ptr")
        return RuntimeArgKind::Pointer;
    return RuntimeArgKind::Object; // Default to object for unknown
}

RuntimeReturnKind RuntimeRegistry::parseReturnKind(std::string_view signature)
{
    auto [retType, _] = parseSignatureString(signature);
    return typeToReturnKind(retType);
}

const RuntimeRegistry &RuntimeRegistry::instance()
{
    static const RuntimeRegistry registry;
    return registry;
}

RuntimeRegistry::RuntimeRegistry()
{
    buildFunctionIndex();
    buildClassIndex();
}

void RuntimeRegistry::buildFunctionIndex()
{
    // Build from RuntimeNameMap (canonical -> rt_* symbol)
    for (const auto &alias : runtime::kRuntimeNameAliases)
    {
        RuntimeFunctionInfo info;
        info.canonicalName = alias.canonical;
        info.runtimeSymbol = alias.runtime;

        // Try to find signature from RuntimeSignatures
        if (auto sig = runtime::findRuntimeSignature(alias.runtime))
        {
            // Build signature string from the parsed signature
            std::string sigStr;
            switch (sig->retType.kind)
            {
            case core::Type::Kind::Void:
                sigStr = "void";
                break;
            case core::Type::Kind::I1:
                sigStr = "i1";
                break;
            case core::Type::Kind::I16:
                sigStr = "i16";
                break;
            case core::Type::Kind::I32:
                sigStr = "i32";
                break;
            case core::Type::Kind::I64:
                sigStr = "i64";
                break;
            case core::Type::Kind::F64:
                sigStr = "f64";
                break;
            case core::Type::Kind::Ptr:
                sigStr = "obj";
                break;
            case core::Type::Kind::Str:
                sigStr = "str";
                break;
            default:
                sigStr = "obj";
                break;
            }

            info.returnKind = typeToReturnKind(sigStr);

            // Parse argument kinds
            for (const auto &paramType : sig->paramTypes)
            {
                std::string_view argStr;
                switch (paramType.kind)
                {
                case core::Type::Kind::I1:
                    argStr = "i1";
                    break;
                case core::Type::Kind::I16:
                case core::Type::Kind::I32:
                case core::Type::Kind::I64:
                    argStr = "i64";
                    break;
                case core::Type::Kind::F64:
                    argStr = "f64";
                    break;
                case core::Type::Kind::Ptr:
                    argStr = "obj";
                    break;
                case core::Type::Kind::Str:
                    argStr = "str";
                    break;
                default:
                    argStr = "obj";
                    break;
                }
                info.argKinds.push_back(typeToArgKind(argStr));
            }
        }
        else
        {
            // Default to object return for unregistered
            info.returnKind = RuntimeReturnKind::Object;
        }

        allFunctions_.push_back(info.canonicalName);
        functionIndex_.emplace(info.canonicalName, std::move(info));
    }
}

void RuntimeRegistry::buildClassIndex()
{
    // Build from RuntimeClasses catalog
    const auto &catalog = runtime::runtimeClassCatalog();

    for (const auto &cls : catalog)
    {
        RuntimeClassInfo info;
        info.name = cls.qname;
        info.constructor = cls.ctor ? cls.ctor : "";
        info.properties = cls.properties;
        info.methods = cls.methods;

        allClasses_.push_back(info.name);
        classIndex_.emplace(info.name, std::move(info));
    }
}

std::optional<RuntimeFunctionInfo> RuntimeRegistry::findFunction(
    std::string_view canonicalName) const
{
    auto it = functionIndex_.find(canonicalName);
    if (it != functionIndex_.end())
    {
        return it->second;
    }
    return std::nullopt;
}

std::optional<RuntimeClassInfo> RuntimeRegistry::findClass(std::string_view className) const
{
    auto it = classIndex_.find(className);
    if (it != classIndex_.end())
    {
        return it->second;
    }
    return std::nullopt;
}

bool RuntimeRegistry::hasFunction(std::string_view canonicalName) const
{
    return functionIndex_.find(canonicalName) != functionIndex_.end();
}

bool RuntimeRegistry::hasClass(std::string_view className) const
{
    return classIndex_.find(className) != classIndex_.end();
}

RuntimeReturnKind RuntimeRegistry::getReturnKind(std::string_view canonicalName) const
{
    auto it = functionIndex_.find(canonicalName);
    if (it != functionIndex_.end())
    {
        return it->second.returnKind;
    }
    return RuntimeReturnKind::Unknown;
}

std::optional<std::string_view> RuntimeRegistry::getRuntimeSymbol(
    std::string_view canonicalName) const
{
    return runtime::mapCanonicalRuntimeName(canonicalName);
}

const std::vector<std::string_view> &RuntimeRegistry::allFunctionNames() const
{
    return allFunctions_;
}

const std::vector<std::string_view> &RuntimeRegistry::allClassNames() const
{
    return allClasses_;
}

} // namespace il::frontends::common
