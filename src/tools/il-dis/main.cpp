//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the miniature `il-dis` sample. The executable constructs a module
// programmatically using the IRBuilder façade, populates it with a canonical
// "hello world" style program, and serializes the result to stdout. Besides
// acting as a developer smoke test for the builder and serializer pipelines, it
// also documents the minimum amount of plumbing required to generate IL from
// scratch.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Demonstrates constructing and serializing IL programmatically.
/// @details The sample intentionally avoids file I/O to focus purely on the
///          IL-building workflow.  It serves as both developer documentation and
///          a smoke test for the builder/serializer stack.

#include "viper/il/IRBuilder.hpp"
#include "viper/il/IO.hpp"
#include <iostream>

/// @brief Emit IL for a fixed "hello world" style program.
///
/// @details Control flow proceeds as follows:
///          1. Instantiate a module and `IRBuilder`.
///          2. Declare the runtime print intrinsic and a global string literal.
///          3. Create @c main, append an entry block, and populate it with
///             call/return instructions.
///          4. Serialize the resulting module to stdout via
///             @ref il::io::Serializer.
///          No arguments are consumed; the function always emits the same
///          program and returns zero unless serialization throws (which it does
///          not in the current API).
///
/// @return Zero after printing the serialized module to stdout.
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
