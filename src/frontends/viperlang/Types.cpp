//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/viperlang/Types.cpp
// Purpose: Implementation of ViperLang semantic types.
//
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Types.hpp"
#include <sstream>

namespace il::frontends::viperlang
{

//=============================================================================
// ViperType Implementation
//=============================================================================

bool ViperType::equals(const ViperType &other) const
{
    if (kind != other.kind)
        return false;
    if (name != other.name)
        return false;
    if (typeArgs.size() != other.typeArgs.size())
        return false;
    for (size_t i = 0; i < typeArgs.size(); ++i)
    {
        if (!typeArgs[i] || !other.typeArgs[i])
            return typeArgs[i] == other.typeArgs[i];
        if (!typeArgs[i]->equals(*other.typeArgs[i]))
            return false;
    }
    return true;
}

bool ViperType::isAssignableFrom(const ViperType &source) const
{
    // Exact match
    if (equals(source))
        return true;

    // Any accepts everything
    if (kind == TypeKindSem::Any)
        return true;

    // Nothing is assignable from Never
    if (source.kind == TypeKindSem::Never)
        return true;

    // Optional accepts its inner type and null
    if (kind == TypeKindSem::Optional)
    {
        if (source.kind == TypeKindSem::Unit)  // null
            return true;
        if (typeArgs.empty())
            return false;
        if (source.kind == TypeKindSem::Optional)
        {
            // Optional[T] from Optional[T]
            return typeArgs[0]->isAssignableFrom(*source.typeArgs[0]);
        }
        // Optional[T] from T
        return typeArgs[0]->isAssignableFrom(source);
    }

    // Numeric promotions
    if (kind == TypeKindSem::Number && source.kind == TypeKindSem::Integer)
        return true;  // Integer -> Number
    if (kind == TypeKindSem::Integer && source.kind == TypeKindSem::Byte)
        return true;  // Byte -> Integer
    if (kind == TypeKindSem::Number && source.kind == TypeKindSem::Byte)
        return true;  // Byte -> Number

    // Interface assignment (TODO: check actual implementation)
    if (kind == TypeKindSem::Interface &&
        (source.kind == TypeKindSem::Entity || source.kind == TypeKindSem::Value))
        return true;  // Needs actual interface checking

    return false;
}

bool ViperType::isConvertibleTo(const ViperType &target) const
{
    // Assignment is conversion
    if (target.isAssignableFrom(*this))
        return true;

    // Explicit conversions
    // Integer <-> Number
    if ((kind == TypeKindSem::Integer && target.kind == TypeKindSem::Number) ||
        (kind == TypeKindSem::Number && target.kind == TypeKindSem::Integer))
        return true;

    // Integer <-> String (via toString/parse)
    if ((kind == TypeKindSem::Integer && target.kind == TypeKindSem::String) ||
        (kind == TypeKindSem::String && target.kind == TypeKindSem::Integer))
        return true;

    // Number <-> String
    if ((kind == TypeKindSem::Number && target.kind == TypeKindSem::String) ||
        (kind == TypeKindSem::String && target.kind == TypeKindSem::Number))
        return true;

    // Boolean <-> String
    if ((kind == TypeKindSem::Boolean && target.kind == TypeKindSem::String) ||
        (kind == TypeKindSem::String && target.kind == TypeKindSem::Boolean))
        return true;

    // Byte <-> Integer
    if ((kind == TypeKindSem::Byte && target.kind == TypeKindSem::Integer) ||
        (kind == TypeKindSem::Integer && target.kind == TypeKindSem::Byte))
        return true;

    return false;
}

std::string ViperType::toString() const
{
    std::ostringstream ss;

    switch (kind)
    {
        case TypeKindSem::Integer:
            return "Integer";
        case TypeKindSem::Number:
            return "Number";
        case TypeKindSem::Boolean:
            return "Boolean";
        case TypeKindSem::String:
            return "String";
        case TypeKindSem::Byte:
            return "Byte";
        case TypeKindSem::Unit:
            return "Unit";
        case TypeKindSem::Void:
            return "Void";
        case TypeKindSem::Error:
            return "Error";
        case TypeKindSem::Ptr:
            return "Ptr";
        case TypeKindSem::Unknown:
            return "?";
        case TypeKindSem::Never:
            return "Never";
        case TypeKindSem::Any:
            return "Any";

        case TypeKindSem::Optional:
            if (!typeArgs.empty())
                ss << typeArgs[0]->toString() << "?";
            else
                ss << "?";
            return ss.str();

        case TypeKindSem::Result:
            ss << "Result[";
            if (!typeArgs.empty())
                ss << typeArgs[0]->toString();
            ss << "]";
            return ss.str();

        case TypeKindSem::List:
            ss << "List[";
            if (!typeArgs.empty())
                ss << typeArgs[0]->toString();
            ss << "]";
            return ss.str();

        case TypeKindSem::Set:
            ss << "Set[";
            if (!typeArgs.empty())
                ss << typeArgs[0]->toString();
            ss << "]";
            return ss.str();

        case TypeKindSem::Map:
            ss << "Map[";
            if (typeArgs.size() >= 2)
                ss << typeArgs[0]->toString() << ", " << typeArgs[1]->toString();
            ss << "]";
            return ss.str();

        case TypeKindSem::Function:
            ss << "(";
            for (size_t i = 0; i + 1 < typeArgs.size(); ++i)
            {
                if (i > 0)
                    ss << ", ";
                ss << typeArgs[i]->toString();
            }
            ss << ") -> ";
            if (!typeArgs.empty())
                ss << typeArgs.back()->toString();
            else
                ss << "Void";
            return ss.str();

        case TypeKindSem::Value:
        case TypeKindSem::Entity:
        case TypeKindSem::Interface:
            ss << name;
            if (!typeArgs.empty())
            {
                ss << "[";
                for (size_t i = 0; i < typeArgs.size(); ++i)
                {
                    if (i > 0)
                        ss << ", ";
                    ss << typeArgs[i]->toString();
                }
                ss << "]";
            }
            return ss.str();

        case TypeKindSem::TypeParam:
            return name;
    }

    return "?";
}

//=============================================================================
// Type Factory Implementation
//=============================================================================

namespace types
{

namespace
{
    // Singleton cache for primitive types
    struct TypeCache
    {
        TypeRef integerType;
        TypeRef numberType;
        TypeRef booleanType;
        TypeRef stringType;
        TypeRef byteType;
        TypeRef unitType;
        TypeRef voidType;
        TypeRef errorType;
        TypeRef ptrType;
        TypeRef unknownType;
        TypeRef neverType;
        TypeRef anyType;

        static TypeCache &instance()
        {
            static TypeCache cache;
            return cache;
        }

    private:
        TypeCache()
        {
            integerType = std::make_shared<ViperType>(TypeKindSem::Integer);
            numberType = std::make_shared<ViperType>(TypeKindSem::Number);
            booleanType = std::make_shared<ViperType>(TypeKindSem::Boolean);
            stringType = std::make_shared<ViperType>(TypeKindSem::String);
            byteType = std::make_shared<ViperType>(TypeKindSem::Byte);
            unitType = std::make_shared<ViperType>(TypeKindSem::Unit);
            voidType = std::make_shared<ViperType>(TypeKindSem::Void);
            errorType = std::make_shared<ViperType>(TypeKindSem::Error);
            ptrType = std::make_shared<ViperType>(TypeKindSem::Ptr);
            unknownType = std::make_shared<ViperType>(TypeKindSem::Unknown);
            neverType = std::make_shared<ViperType>(TypeKindSem::Never);
            anyType = std::make_shared<ViperType>(TypeKindSem::Any);
        }
    };
} // anonymous namespace

TypeRef integer() { return TypeCache::instance().integerType; }
TypeRef number() { return TypeCache::instance().numberType; }
TypeRef boolean() { return TypeCache::instance().booleanType; }
TypeRef string() { return TypeCache::instance().stringType; }
TypeRef byte() { return TypeCache::instance().byteType; }
TypeRef unit() { return TypeCache::instance().unitType; }
TypeRef voidType() { return TypeCache::instance().voidType; }
TypeRef error() { return TypeCache::instance().errorType; }
TypeRef ptr() { return TypeCache::instance().ptrType; }
TypeRef unknown() { return TypeCache::instance().unknownType; }
TypeRef never() { return TypeCache::instance().neverType; }
TypeRef any() { return TypeCache::instance().anyType; }

TypeRef optional(TypeRef inner)
{
    return std::make_shared<ViperType>(TypeKindSem::Optional, std::vector<TypeRef>{inner});
}

TypeRef result(TypeRef successType)
{
    return std::make_shared<ViperType>(TypeKindSem::Result, std::vector<TypeRef>{successType});
}

TypeRef list(TypeRef element)
{
    return std::make_shared<ViperType>(TypeKindSem::List, std::vector<TypeRef>{element});
}

TypeRef set(TypeRef element)
{
    return std::make_shared<ViperType>(TypeKindSem::Set, std::vector<TypeRef>{element});
}

TypeRef map(TypeRef key, TypeRef value)
{
    return std::make_shared<ViperType>(TypeKindSem::Map, std::vector<TypeRef>{key, value});
}

TypeRef function(std::vector<TypeRef> params, TypeRef ret)
{
    params.push_back(ret);  // Store return type at the end
    return std::make_shared<ViperType>(TypeKindSem::Function, std::move(params));
}

TypeRef value(const std::string &name, std::vector<TypeRef> typeParams)
{
    return std::make_shared<ViperType>(TypeKindSem::Value, name, std::move(typeParams));
}

TypeRef entity(const std::string &name, std::vector<TypeRef> typeParams)
{
    return std::make_shared<ViperType>(TypeKindSem::Entity, name, std::move(typeParams));
}

TypeRef interface(const std::string &name, std::vector<TypeRef> typeParams)
{
    return std::make_shared<ViperType>(TypeKindSem::Interface, name, std::move(typeParams));
}

TypeRef typeParam(const std::string &name)
{
    return std::make_shared<ViperType>(TypeKindSem::TypeParam, name);
}

} // namespace types

//=============================================================================
// IL Type Mapping
//=============================================================================

il::core::Type::Kind toILType(const ViperType &type)
{
    switch (type.kind)
    {
        case TypeKindSem::Integer:
            return il::core::Type::Kind::I64;

        case TypeKindSem::Number:
            return il::core::Type::Kind::F64;

        case TypeKindSem::Boolean:
            return il::core::Type::Kind::I1;

        case TypeKindSem::String:
            return il::core::Type::Kind::Str;

        case TypeKindSem::Byte:
            return il::core::Type::Kind::I32;  // IL has no i8

        case TypeKindSem::Unit:
        case TypeKindSem::Void:
            return il::core::Type::Kind::Void;

        case TypeKindSem::Error:
            return il::core::Type::Kind::Error;

        case TypeKindSem::Ptr:
        case TypeKindSem::Entity:
        case TypeKindSem::Interface:
        case TypeKindSem::List:
        case TypeKindSem::Map:
        case TypeKindSem::Set:
            return il::core::Type::Kind::Ptr;

        // Value types need special handling at lowering time
        // (passed as ptr to stack slot)
        case TypeKindSem::Value:
            return il::core::Type::Kind::Ptr;

        // Optional values need special handling
        // (in-memory representation: flag + value)
        case TypeKindSem::Optional:
            return il::core::Type::Kind::Ptr;

        // Result needs special handling
        // (in-memory representation: tag + payload)
        case TypeKindSem::Result:
            return il::core::Type::Kind::Ptr;

        // Functions are function pointers or closure objects
        case TypeKindSem::Function:
            return il::core::Type::Kind::Ptr;

        // Unknown types (inference placeholder)
        case TypeKindSem::Unknown:
        case TypeKindSem::TypeParam:
        case TypeKindSem::Any:
            return il::core::Type::Kind::Ptr;

        // Never type doesn't produce values
        case TypeKindSem::Never:
            return il::core::Type::Kind::Void;
    }

    return il::core::Type::Kind::Void;
}

size_t typeSize(const ViperType &type)
{
    switch (type.kind)
    {
        case TypeKindSem::Integer:
            return 8;
        case TypeKindSem::Number:
            return 8;
        case TypeKindSem::Boolean:
            return 8;  // Stored as i64
        case TypeKindSem::String:
            return 8;  // Pointer
        case TypeKindSem::Byte:
            return 4;  // i32
        case TypeKindSem::Unit:
        case TypeKindSem::Void:
            return 0;
        case TypeKindSem::Error:
            return 8;  // Pointer to error object
        case TypeKindSem::Ptr:
            return 8;
        case TypeKindSem::Entity:
        case TypeKindSem::Interface:
        case TypeKindSem::List:
        case TypeKindSem::Map:
        case TypeKindSem::Set:
        case TypeKindSem::Function:
            return 8;  // Pointer
        case TypeKindSem::Optional:
            // flag (8) + value size
            if (!type.typeArgs.empty())
                return 8 + typeSize(*type.typeArgs[0]);
            return 16;  // Default
        case TypeKindSem::Result:
            // tag (8) + max(value size, error size)
            // Simplified: assume 16 bytes
            return 16;
        case TypeKindSem::Value:
            // User-defined value size determined by fields
            return 0;  // Must be computed from type definition
        case TypeKindSem::Unknown:
        case TypeKindSem::Never:
        case TypeKindSem::Any:
        case TypeKindSem::TypeParam:
            return 0;
    }
    return 0;
}

size_t typeAlignment(const ViperType &type)
{
    switch (type.kind)
    {
        case TypeKindSem::Integer:
        case TypeKindSem::Number:
        case TypeKindSem::Boolean:
        case TypeKindSem::String:
        case TypeKindSem::Ptr:
        case TypeKindSem::Entity:
        case TypeKindSem::Interface:
        case TypeKindSem::List:
        case TypeKindSem::Map:
        case TypeKindSem::Set:
        case TypeKindSem::Function:
        case TypeKindSem::Error:
        case TypeKindSem::Optional:
        case TypeKindSem::Result:
            return 8;
        case TypeKindSem::Byte:
            return 4;
        case TypeKindSem::Unit:
        case TypeKindSem::Void:
        case TypeKindSem::Unknown:
        case TypeKindSem::Never:
        case TypeKindSem::Any:
        case TypeKindSem::TypeParam:
            return 1;
        case TypeKindSem::Value:
            return 8;  // Default alignment
    }
    return 1;
}

const char *kindToString(TypeKindSem kind)
{
    switch (kind)
    {
        case TypeKindSem::Integer:   return "Integer";
        case TypeKindSem::Number:    return "Number";
        case TypeKindSem::Boolean:   return "Boolean";
        case TypeKindSem::String:    return "String";
        case TypeKindSem::Byte:      return "Byte";
        case TypeKindSem::Unit:      return "Unit";
        case TypeKindSem::Void:      return "Void";
        case TypeKindSem::Optional:  return "Optional";
        case TypeKindSem::Result:    return "Result";
        case TypeKindSem::List:      return "List";
        case TypeKindSem::Map:       return "Map";
        case TypeKindSem::Set:       return "Set";
        case TypeKindSem::Function:  return "Function";
        case TypeKindSem::Value:     return "Value";
        case TypeKindSem::Entity:    return "Entity";
        case TypeKindSem::Interface: return "Interface";
        case TypeKindSem::Error:     return "Error";
        case TypeKindSem::Ptr:       return "Ptr";
        case TypeKindSem::Unknown:   return "Unknown";
        case TypeKindSem::Never:     return "Never";
        case TypeKindSem::Any:       return "Any";
        case TypeKindSem::TypeParam: return "TypeParam";
    }
    return "?";
}

} // namespace il::frontends::viperlang
