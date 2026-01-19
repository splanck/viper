//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_addressing_modes.cpp
// Purpose: Ensure x86-64 codegen emits SIB addressing for base+index*scale+disp and folds LEA.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: src/codegen/x86_64/Lowering.EmitCommon.cpp, ISel.cpp (foldLeaIntoMem), AsmEmitter.cpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"

#include <iostream>
#include <string>

using namespace viper::codegen::x64;

namespace
{

[[nodiscard]] static ILValue makeParam(int id, ILValue::Kind kind) noexcept
{
    ILValue v{};
    v.kind = kind;
    v.id = id;
    return v;
}

[[nodiscard]] static ILValue makeImmI64(long long val) noexcept
{
    ILValue v{};
    v.kind = ILValue::Kind::I64;
    v.id = -1;
    v.i64 = val;
    return v;
}

[[nodiscard]] static ILValue makeValueRef(int id, ILValue::Kind kind) noexcept
{
    ILValue v{};
    v.kind = kind;
    v.id = id;
    return v;
}

// IL scaffold: v = load [p + (i << 3) + 16]
[[nodiscard]] static std::string buildAsm()
{
    ILValue p = makeParam(0, ILValue::Kind::PTR);
    ILValue i = makeParam(1, ILValue::Kind::I64);

    ILInstr shl{};
    shl.opcode = "shl";
    shl.resultId = 3;
    shl.resultKind = ILValue::Kind::I64;
    shl.ops = {i, makeImmI64(3)};

    ILInstr add{};
    add.opcode = "add";
    add.resultId = 4;
    add.resultKind = ILValue::Kind::PTR;
    add.ops = {p, makeValueRef(3, ILValue::Kind::I64)};

    ILInstr ld{};
    ld.opcode = "load";
    ld.resultId = 5;
    ld.resultKind = ILValue::Kind::I64;
    ld.ops = {makeValueRef(4, ILValue::Kind::PTR), makeImmI64(16)};

    ILInstr ret{};
    ret.opcode = "ret";
    ret.ops = {makeValueRef(5, ILValue::Kind::I64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {p.id, i.id};
    entry.paramKinds = {p.kind, i.kind};
    entry.instrs = {shl, add, ld, ret};

    ILFunction fn{};
    fn.name = "sib";
    fn.blocks = {entry};

    ILModule m{};
    m.funcs = {fn};

    const CodegenResult res = emitModuleToAssembly(m, {});
    return res.asmText;
}

} // namespace

int main()
{
    const auto text = buildAsm();
    // Check for SIB addressing mode pattern: (base,index,scale)
    // The specific registers may vary based on register allocation
    const bool hasSib8 = text.find(",8)") != std::string::npos;
    if (!hasSib8)
    {
        std::cerr << "Expected SIB addressing mode with scale 8:\n" << text;
        return 1;
    }
    if (text.find("16(") == std::string::npos)
    {
        std::cerr << "Expected displacement +16:\n" << text;
        return 2;
    }
    if (text.find("leaq") != std::string::npos)
    {
        std::cerr << "Did not expect folded LEA:\n" << text;
        return 3;
    }
    return 0;
}
