//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/basic/BuiltinRegistryTests.cpp
// Purpose: Verify dynamic BASIC builtin handler registration stores stable keys.
// Key invariants: Handler lookups must succeed even when registered with temporary strings.
// Ownership/Lifetime: Registry owns string keys; tests clean up installed handlers.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BuiltinRegistry.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <cassert>
#include <string>
#include <string_view>

using namespace il::frontends::basic;

namespace
{

Lowerer::RVal dummy_handler(lower::BuiltinLowerContext &)
{
    return Lowerer::RVal{il::core::Value::null(), il::core::Type{il::core::Type::Kind::Void}};
}

void register_with_temporary_key()
{
    register_builtin(std::string("__TEMP_BUILTIN_HANDLER__"), dummy_handler);
}

} // namespace

int main()
{
    register_with_temporary_key();

    constexpr std::string_view kName = "__TEMP_BUILTIN_HANDLER__";
    BuiltinHandler handler = find_builtin(kName);
    assert(handler == dummy_handler);

    register_builtin(kName, nullptr);
    assert(find_builtin(kName) == nullptr);

    return 0;
}
