//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema_Runtime.cpp
/// @brief Runtime function registration for the Zia semantic analyzer.
///
/// This file uses the RuntimeRegistry to register all runtime functions,
/// providing full signature information (return type and parameter types).
/// It also includes ZiaRuntimeExterns.inc for aliases not in the catalog.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Sema.hpp"
#include "frontends/zia/RuntimeAdapter.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

namespace il::frontends::zia
{

void Sema::initRuntimeFunctions()
{
    const auto &registry = il::runtime::RuntimeRegistry::instance();
    const auto &catalog = registry.rawCatalog();

    // Register class types first
    for (const auto &cls : catalog)
    {
        typeRegistry_[cls.qname] = types::runtimeClass(cls.qname);
    }

    // Register methods with full signatures from RuntimeRegistry
    for (const auto &cls : catalog)
    {
        for (const auto &m : cls.methods)
        {
            auto sig = il::runtime::parseRuntimeSignature(m.signature ? m.signature : "");
            if (!sig.isValid())
                continue;

            TypeRef returnType = toZiaType(sig.returnType);
            std::vector<TypeRef> paramTypes = toZiaParamTypes(sig);
            defineExternFunction(m.target ? m.target : "", returnType, paramTypes);
        }

        // Register property getters/setters
        for (const auto &p : cls.properties)
        {
            auto propType = toZiaType(il::runtime::mapILToken(p.type ? p.type : ""));

            // Getter
            if (p.getter)
            {
                defineExternFunction(p.getter, propType, {});
            }

            // Setter
            if (p.setter)
            {
                defineExternFunction(p.setter, types::voidType(), {propType});
            }
        }
    }

    // Include generated aliases and any standalone functions from runtime.def
    // This includes RT_ALIAS entries like Viper.Time.SleepMs
    // NOTE: These use the old signature (return type only), which is acceptable
    // for backwards compatibility. The class methods above already have full signatures.
#include "il/runtime/ZiaRuntimeExterns.inc"
}

} // namespace il::frontends::zia
