// File: src/il/io/TypeParser.cpp
// Purpose: Implements parsing for IL textual type specifiers.
// Key invariants: Supported types mirror docs/il-spec.md definitions.
// Ownership/Lifetime: Stateless utilities returning value objects.
// Links: docs/il-spec.md

#include "il/io/TypeParser.hpp"

namespace il::io
{

il::core::Type parseType(const std::string &token, bool *ok)
{
    auto makeType = [ok](il::core::Type::Kind kind) {
        if (ok)
            *ok = true;
        return il::core::Type(kind);
    };

    if (token == "i64")
        return makeType(il::core::Type::Kind::I64);
    if (token == "i1")
        return makeType(il::core::Type::Kind::I1);
    if (token == "f64")
        return makeType(il::core::Type::Kind::F64);
    if (token == "ptr")
        return makeType(il::core::Type::Kind::Ptr);
    if (token == "str")
        return makeType(il::core::Type::Kind::Str);
    if (token == "void")
        return makeType(il::core::Type::Kind::Void);

    if (ok)
        *ok = false;
    return il::core::Type();
}

} // namespace il::io
