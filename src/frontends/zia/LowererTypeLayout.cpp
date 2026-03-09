//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/zia/LowererTypeLayout.cpp
// Purpose: Implementation of LowererTypeLayout registration methods.
//
// Key invariants:
//   - Type registration is idempotent (duplicate names overwrite)
//
// Ownership/Lifetime:
//   - Owned by Lowerer, persists for module lowering
//
// Links: frontends/zia/LowererTypeLayout.hpp
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/LowererTypeLayout.hpp"

namespace il::frontends::zia
{

void LowererTypeLayout::registerValueType(const std::string &name, ValueTypeInfo info)
{
    valueTypes_[name] = std::move(info);
}

void LowererTypeLayout::registerEntityType(const std::string &name, EntityTypeInfo info)
{
    entityTypes_[name] = std::move(info);
}

void LowererTypeLayout::registerInterfaceType(const std::string &name, InterfaceTypeInfo info)
{
    interfaceTypes_[name] = std::move(info);
}

} // namespace il::frontends::zia
