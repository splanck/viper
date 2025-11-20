//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/ILTypeUtils.cpp
// Purpose: Implementation of type conversion utilities for BASIC-to-IL lowering
// Key invariants: Canonical mapping from BASIC AST types to IL core types
// Ownership/Lifetime: Non-owning utilities; stateless conversions
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "ILTypeUtils.hpp"
#include "ast/NodeFwd.hpp"
#include "il/core/Type.hpp"

#include <cstddef>

namespace il::frontends::basic::type_conv
{

il::core::Type astToIlType(::il::frontends::basic::Type ty) noexcept
{
    using IlType = il::core::Type;
    switch (ty)
    {
        case ::il::frontends::basic::Type::I64:
            return IlType(IlType::Kind::I64);
        case ::il::frontends::basic::Type::F64:
            return IlType(IlType::Kind::F64);
        case ::il::frontends::basic::Type::Str:
            return IlType(IlType::Kind::Str);
        case ::il::frontends::basic::Type::Bool:
            return IlType(IlType::Kind::I1);
    }
    return IlType(IlType::Kind::I64);
}

std::size_t getFieldSize(::il::frontends::basic::Type type) noexcept
{
    constexpr std::size_t kPointerSize = sizeof(void *);

    switch (type)
    {
        case ::il::frontends::basic::Type::Str:
            return kPointerSize;
        case ::il::frontends::basic::Type::F64:
            return 8;
        case ::il::frontends::basic::Type::Bool:
            return 1;
        case ::il::frontends::basic::Type::I64:
        default:
            return 8;
    }
}

} // namespace il::frontends::basic::type_conv
