//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_varargs_al.cpp
// Purpose: Verify SysV variadic call lowerer sets %al to #XMM-args.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: src/codegen/x86_64/CallLowering.cpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"

#include <string>

using namespace viper::codegen::x64;

namespace
{

[[nodiscard]] ILValue makeParam(int id, ILValue::Kind kind) noexcept
{
    ILValue v{};
    v.kind = kind;
    v.id = id;
    return v;
}

[[nodiscard]] ILValue makeValueRef(int id, ILValue::Kind kind) noexcept
{
    ILValue v{};
    v.kind = kind;
    v.id = id;
    return v;
}

[[nodiscard]] ILValue makeLabel(const char *name) noexcept
{
    ILValue v{};
    v.kind = ILValue::Kind::LABEL;
    v.id = -1;
    v.label = name;
    return v;
}

static std::string buildAsmWithCallee(const char *callee)
{
    // Pretend signature: int rt_snprintf(char*, size_t, double, double, ...)
    // IL: call "rt_snprintf"(buf, size, f0, f1) -> i64
    ILValue buf = makeParam(0, ILValue::Kind::PTR);
    ILValue size = makeParam(1, ILValue::Kind::I64);
    ILValue f0 = makeParam(2, ILValue::Kind::F64);
    ILValue f1 = makeParam(3, ILValue::Kind::F64);

    ILInstr call{};
    call.opcode = "call";
    call.resultId = 5;
    call.resultKind = ILValue::Kind::I64;
    call.ops = {makeLabel(callee), buf, size, f0, f1};

    ILInstr ret{};
    ret.opcode = "ret";
    ret.ops = {makeValueRef(5, ILValue::Kind::I64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {buf.id, size.id, f0.id, f1.id};
    entry.paramKinds = {buf.kind, size.kind, f0.kind, f1.kind};
    entry.instrs = {call, ret};

    ILFunction fn{};
    fn.name = "v";
    fn.blocks = {entry};

    ILModule m{};
    m.funcs = {fn};

    const CodegenResult res = emitModuleToAssembly(m, {});
    return res.asmText;
}

} // namespace

int main()
{
#ifdef _WIN32
    // Windows x64 ABI doesn't use %al for varargs XMM count.
    // This test is SysV ABI specific.
    return 0;
#endif
    {
        const auto text = buildAsmWithCallee("rt_snprintf");
        const auto callPos = text.find("call");
        if (callPos == std::string::npos)
            return 1;
        const auto mov2 = text.rfind("$2, %rax", callPos);
        if (mov2 == std::string::npos)
            return 2;
        const auto prefix = text.substr(0, callPos);
        if (prefix.find("%xmm0") == std::string::npos)
            return 3;
        if (prefix.find("%xmm1") == std::string::npos)
            return 4;
    }
    {
        const auto text = buildAsmWithCallee("rt_sb_printf");
        const auto callPos = text.find("call");
        if (callPos == std::string::npos)
            return 5;
        const auto mov2 = text.rfind("movq $2, %rax", callPos);
        if (mov2 == std::string::npos)
            return 6;
        const auto prefix = text.substr(0, callPos);
        if (prefix.find("%xmm0") == std::string::npos)
            return 7;
        if (prefix.find("%xmm1") == std::string::npos)
            return 8;
    }
    // 0 f64s
    {
        ILValue buf = makeParam(0, ILValue::Kind::PTR);
        ILValue size = makeParam(1, ILValue::Kind::I64);
        ILInstr call{};
        call.opcode = "call";
        call.resultId = 3;
        call.resultKind = ILValue::Kind::I64;
        call.ops = {makeLabel("rt_snprintf"), buf, size};
        ILInstr ret{};
        ret.opcode = "ret";
        ret.ops = {makeValueRef(3, ILValue::Kind::I64)};
        ILBlock entry{};
        entry.name = "entry";
        entry.paramIds = {buf.id, size.id};
        entry.paramKinds = {buf.kind, size.kind};
        entry.instrs = {call, ret};
        ILFunction fn{};
        fn.name = "v0";
        fn.blocks = {entry};
        ILModule m{};
        m.funcs = {fn};
        const auto text = emitModuleToAssembly(m, {}).asmText;
        const auto callPos = text.find("call");
        if (callPos == std::string::npos)
            return 9;
        const auto mov0 = text.rfind("$0, %rax", callPos);
        if (mov0 == std::string::npos)
            return 10;
    }
    // 1 f64
    {
        ILValue buf = makeParam(0, ILValue::Kind::PTR);
        ILValue size = makeParam(1, ILValue::Kind::I64);
        ILValue f0 = makeParam(2, ILValue::Kind::F64);
        ILInstr call{};
        call.opcode = "call";
        call.resultId = 4;
        call.resultKind = ILValue::Kind::I64;
        call.ops = {makeLabel("rt_snprintf"), buf, size, f0};
        ILInstr ret{};
        ret.opcode = "ret";
        ret.ops = {makeValueRef(4, ILValue::Kind::I64)};
        ILBlock entry{};
        entry.name = "entry";
        entry.paramIds = {buf.id, size.id, f0.id};
        entry.paramKinds = {buf.kind, size.kind, f0.kind};
        entry.instrs = {call, ret};
        ILFunction fn{};
        fn.name = "v1";
        fn.blocks = {entry};
        ILModule m{};
        m.funcs = {fn};
        const auto text = emitModuleToAssembly(m, {}).asmText;
        const auto callPos = text.find("call");
        if (callPos == std::string::npos)
            return 11;
        const auto mov1 = text.rfind("$1, %rax", callPos);
        if (mov1 == std::string::npos)
            return 12;
    }
    // Non-varargs should not set %al.
    {
        ILValue x = makeParam(0, ILValue::Kind::F64);
        ILInstr call{};
        call.opcode = "call";
        call.resultId = 1;
        call.resultKind = ILValue::Kind::I64;
        call.ops = {makeLabel("rt_print_f64"), x};
        ILInstr ret{};
        ret.opcode = "ret";
        ret.ops = {makeValueRef(1, ILValue::Kind::I64)};
        ILBlock entry{};
        entry.name = "entry";
        entry.paramIds = {x.id};
        entry.paramKinds = {x.kind};
        entry.instrs = {call, ret};
        ILFunction fn{};
        fn.name = "nv";
        fn.blocks = {entry};
        ILModule m{};
        m.funcs = {fn};
        const auto text = emitModuleToAssembly(m, {}).asmText;
        const auto callPos = text.find("call");
        if (callPos == std::string::npos)
            return 13;
        const auto movAny = text.rfind("movq $", callPos);
        if (movAny != std::string::npos && text.find("%rax", movAny) != std::string::npos)
            return 14;
    }
    return 0;
}
