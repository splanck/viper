//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_call_indirect.cpp
// Purpose: Ensure the x86-64 backend can lower an indirect call target and
// Key invariants: The emitted assembly contains an indirect call and a return.
// Ownership/Lifetime: To be documented.
// Links: src/codegen/x86_64/Lowering.Mem.cpp, src/codegen/x86_64/AsmEmitter.cpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

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
