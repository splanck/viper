// File: tests/codegen/x86_64/test_addressing_modes.cpp
// Purpose: Ensure x86-64 codegen emits SIB addressing for base+index*scale+disp and folds LEA.
// Links: src/codegen/x86_64/Lowering.EmitCommon.cpp, ISel.cpp (foldLeaIntoMem), AsmEmitter.cpp

#include "codegen/x86_64/Backend.hpp"

#include <string>

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#define VIPER_HAS_GTEST 1
#else
#define VIPER_HAS_GTEST 0
#endif

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

#if VIPER_HAS_GTEST
TEST(CodegenX64AddrTest, EmitsSIB)
{
    const auto text = buildAsm();
    // Expect SIB form with scale 8 and displacement +16. Base/index order may vary.
    const bool sibA = text.find("(%rdi,%rsi,8)") != std::string::npos;
    const bool sibB = text.find("(%rsi,%rdi,8)") != std::string::npos;
    EXPECT_TRUE(sibA || sibB) << text;
    EXPECT_NE(text.find("16("), std::string::npos) << text;
    EXPECT_EQ(text.find("leaq"), std::string::npos) << text;
}
#else
int main()
{
    const auto text = buildAsm();
    if (text.find("(%rdi,%rsi,8)") == std::string::npos)
        return 1;
    if (text.find("16(") == std::string::npos)
        return 2;
    if (text.find("leaq") != std::string::npos)
        return 3;
    return 0;
}
#endif
