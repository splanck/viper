// File: tests/unit/test_basic_il_qualified_names.cpp
// Purpose: Validate IL emission preserves fully-qualified function names.
// Key invariants: Lowering uses decl.qualifiedName; serializer prints names verbatim.
// Ownership/Lifetime: Local parser/emitter; no persistent state.

#include "viper/il/IO.hpp"
#include "viper/il/IRBuilder.hpp"

#include <cassert>
#include <sstream>
#include <string>

using namespace il::support;

int main()
{
    // Build a tiny module with a function named with a qualified identifier.
    il::core::Module m;
    il::build::IRBuilder b(m);
    auto &fn = b.startFunction("a.b.f", il::core::Type(il::core::Type::Kind::I64), {});
    auto &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);
    b.emitRet(il::core::Value::constInt(0), {});

    std::ostringstream oss;
    il::io::Serializer::write(m, oss);
    const std::string text = oss.str();
    // Disassembled text must retain the qualified name verbatim.
    assert(text.find("func @a.b.f(") != std::string::npos);
    return 0;
}
