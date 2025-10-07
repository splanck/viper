// File: tests/unit/test_vm_const_string_bytes.cpp
// Purpose: Ensure VM marshals constant strings with embedded null bytes without truncation.
// Key invariants: Runtime receives the full byte payload and reports the correct length.
// Ownership: Test builds a throwaway module and relies on VM teardown to release globals.
// Links: docs/codemap.md

#include "il/build/IRBuilder.hpp"
#include "support/source_location.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <optional>
#include <string>

int main()
{
    using il::build::IRBuilder;
    using il::core::Module;
    using il::core::Type;
    using il::core::Value;
    using il::support::SourceLoc;

    Module module;
    IRBuilder builder(module);

    builder.addExtern("rt_len", Type(Type::Kind::I64), {Type(Type::Kind::Str)});
    builder.addExtern("rt_str_release_maybe", Type(Type::Kind::Void), {Type(Type::Kind::Str)});

    const std::string globalName = "g_payload";
    const std::string payload{"A\0B", 3};
    builder.addGlobalStr(globalName, payload);

    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    const SourceLoc loc{1, 1, 1};

    Value strVal = builder.emitConstStr(globalName, loc);
    unsigned lenId = builder.reserveTempId();
    Value lenVal = Value::temp(lenId);
    std::optional<Value> lenDst = lenVal;
    builder.emitCall("rt_len", {strVal}, lenDst, loc);
    builder.emitCall("rt_str_release_maybe", {strVal}, std::nullopt, loc);
    builder.emitRet(lenVal, loc);

    il::vm::VM vm(module);
    const int64_t exitCode = vm.run();
    assert(exitCode == static_cast<int64_t>(payload.size()));
    return 0;
}
