//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/verify/SpecTables.cpp
// Purpose: Provide accessors for opcode verification specs emitted from schema.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/verify/SpecTables.hpp"

#include <array>
#include <cassert>

namespace il::verify
{
namespace
{
#include "il/verify/generated/SpecTables.inc"
} // namespace

const OpcodeSpec &getOpcodeSpec(il::core::Opcode opcode)
{
    const size_t index = static_cast<size_t>(opcode);
    assert(index < kOpcodeSpecs.size());
    return kOpcodeSpecs[index];
}

const VerifyRule &getVerifyRule(il::core::Opcode opcode)
{
    const size_t index = static_cast<size_t>(opcode);
    assert(index < kVerifyRules.size());
    return kVerifyRules[index];
}

std::optional<il::core::Type::Kind> kindFromTypeClass(TypeClass typeClass)
{
    using il::core::Type;
    switch (typeClass)
    {
        case TypeClass::Void:
            return Type::Kind::Void;
        case TypeClass::I1:
            return Type::Kind::I1;
        case TypeClass::I16:
            return Type::Kind::I16;
        case TypeClass::I32:
            return Type::Kind::I32;
        case TypeClass::I64:
            return Type::Kind::I64;
        case TypeClass::F64:
            return Type::Kind::F64;
        case TypeClass::Ptr:
            return Type::Kind::Ptr;
        case TypeClass::Str:
            return Type::Kind::Str;
        case TypeClass::Error:
            return Type::Kind::Error;
        case TypeClass::ResumeTok:
            return Type::Kind::ResumeTok;
        case TypeClass::None:
        case TypeClass::InstrType:
            return std::nullopt;
    }
    return std::nullopt;
}

std::optional<il::core::Type> typeFromTypeClass(TypeClass typeClass)
{
    if (typeClass == TypeClass::InstrType)
        return std::nullopt;
    if (auto kind = kindFromTypeClass(typeClass))
        return il::core::Type(*kind);
    return std::nullopt;
}

} // namespace il::verify

