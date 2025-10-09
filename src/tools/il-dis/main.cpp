//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//
// Defines a tiny command-line utility that demonstrates how to build a module
// using the IRBuilder façade and serialize it to textual IL.  The tool is used
// during development as a smoke test for the builder and serializer pipelines.
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/io/Serializer.hpp"
#include <iostream>

/// @brief Emit IL for a fixed "hello world" style program.
///
/// The utility does not inspect command-line arguments.  Instead it builds a
/// module in-memory using IRBuilder, declaring the runtime print routine,
/// materializing a string literal, and emitting the entry function with its
/// associated block and instructions.  Finally it serializes the finished
/// module to stdout so the caller can observe the produced IL.
///
/// @return Zero on success after printing the serialized module.
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
