// File: tests/codegen/x86_64/test_call_indirect.cpp
// Purpose: Ensure the x86-64 backend can lower an indirect call target and
//          emit a `callq *...` form in assembly.
// Key invariants: The emitted assembly contains an indirect call and a return.
// Links: src/codegen/x86_64/Lowering.Mem.cpp, src/codegen/x86_64/AsmEmitter.cpp

#include "codegen/x86_64/Backend.hpp"

#include <string>
#include <string_view>

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#define VIPER_HAS_GTEST 1
#else
#include <cstdlib>
#include <iostream>
#define VIPER_HAS_GTEST 0
#endif

namespace viper::codegen::x64
{
namespace
{

[[nodiscard]] ILValue makeValue(ILValue::Kind kind, int id)
{
    ILValue v{};
    v.kind = kind;
    v.id = id;
    return v;
}

// Build a function that takes a function pointer and calls it: fnptr() -> i64
[[nodiscard]] ILModule makeCallIndirectModule()
{
    // Callee function: callee() -> i64 { ret 7 }
    ILInstr calleeRet{};
    calleeRet.opcode = "ret";
    ILValue seven{};
    seven.kind = ILValue::Kind::I64;
    seven.id = -1;
    seven.i64 = 7;
    calleeRet.ops = {seven};

    ILBlock calleeEntry{};
    calleeEntry.name = "callee";
    calleeEntry.instrs = {calleeRet};

    ILFunction calleeFn{};
    calleeFn.name = "callee";
    calleeFn.blocks = {calleeEntry};

    // Main function: main() -> i64 {
    //   call.indirect %fnptr
    //   ret %t0
    // }
    ILValue fnptr = makeValue(ILValue::Kind::PTR, 0);

    ILInstr calli{};
    calli.opcode = "call.indirect";
    calli.ops = {fnptr};
    calli.resultId = 1;
    calli.resultKind = ILValue::Kind::I64;

    ILInstr retMain{};
    retMain.opcode = "ret";
    ILValue res{};
    res.kind = ILValue::Kind::I64;
    res.id = calli.resultId;
    retMain.ops = {res};

    ILBlock mainEntry{};
    mainEntry.name = "main";
    mainEntry.paramIds = {fnptr.id};
    mainEntry.paramKinds = {fnptr.kind};
    mainEntry.instrs = {calli, retMain};

    ILFunction mainFn{};
    mainFn.name = "main";
    mainFn.blocks = {mainEntry};

    ILModule m{};
    m.funcs = {calleeFn, mainFn};
    return m;
}

[[nodiscard]] bool containsIndirectCall(const std::string &asmText)
{
    // Look for an indirect call form
    return asmText.find("callq *") != std::string::npos;
}

} // namespace
} // namespace viper::codegen::x64

#if VIPER_HAS_GTEST

TEST(CodegenX64CallIndirect, EmitsIndirectCall)
{
    using namespace viper::codegen::x64;
    const ILModule module = makeCallIndirectModule();
    const CodegenResult result = emitModuleToAssembly(module, {});
    EXPECT_TRUE(result.errors.empty());
    EXPECT_TRUE(containsIndirectCall(result.asmText)) << result.asmText;
}

#else
int main()
{
    using namespace viper::codegen::x64;
    const ILModule module = makeCallIndirectModule();
    const CodegenResult result = emitModuleToAssembly(module, {});
    if (!result.errors.empty() || !containsIndirectCall(result.asmText))
    {
        std::cerr << result.asmText;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
#endif
