// File: src/tools/il-dis/main.cpp
// Purpose: Example tool emitting IL for a simple program.
// License: MIT License (see LICENSE).
// Key invariants: None.
// Ownership/Lifetime: Tool owns constructed module.
// Links: docs/class-catalog.md

#include "il/build/IRBuilder.hpp"
#include "il/io/Serializer.hpp"
#include <iostream>

/// @brief Emit IL for a sample program.
///
/// Accepts no command-line arguments. Constructs an in-memory module using the
/// following steps:
/// 1. Creates an IRBuilder bound to a fresh module instance.
/// 2. Adds the extern `rt_print_str` and a global string literal used by the
///    program.
/// 3. Emits the `main` function, its entry block, and the call/return
///    instructions needed to print and exit.
/// 4. Serializes the populated module to standard output.
///
/// @return 0 on success.
/// @note Side effect: writes serialized IL text to stdout.
int main()
{
    il::core::Module m;
    il::build::IRBuilder builder(m);
    builder.addExtern("rt_print_str",
                      il::core::Type(il::core::Type::Kind::Void),
                      {il::core::Type(il::core::Type::Kind::Str)});
    builder.addGlobalStr(".L0", "HELLO");
    auto &fn = builder.startFunction("main", il::core::Type(il::core::Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);
    il::core::Value s0 = builder.emitConstStr(".L0", {});
    builder.emitCall("rt_print_str", {s0}, std::optional<il::core::Value>{}, {});
    builder.emitRet(il::core::Value::constInt(0), {});
    il::io::Serializer::write(m, std::cout);
    return 0;
}
