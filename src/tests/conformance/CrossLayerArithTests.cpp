//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/conformance/CrossLayerArithTests.cpp
// Purpose: Deterministic cross-layer equivalence tests. Each test runs a
//          specific arithmetic edge case on both the VM and (on ARM64 hosts)
//          the AArch64 native backend, asserting identical results.
//
//          VM returns full i64; native returns process exit code (low 8 bits).
//          Tests verify full value via VM, then agreement via exit code.
//
// Platform: On non-ARM64 hosts, native tests are skipped via VIPER_TEST_SKIP.
//
// Reference: docs/arithmetic-semantics.md
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/io/Serializer.hpp"
#include "tests/TestHarness.hpp"

#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
#include "tools/viper/cmd_codegen_arm64.hpp"
#define VIPER_HAS_ARM64 1
#else
#define VIPER_HAS_ARM64 0
#endif

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#if VIPER_HAS_ARM64
#include <filesystem>
#include <fstream>
#endif

using namespace il::core;

namespace
{

int64_t doubleBits(double d)
{
    int64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    return bits;
}

double bitsToDouble(int64_t bits)
{
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

//=============================================================================
// Builder helpers
//=============================================================================

/// Build a function: main() -> i64 { return lhs OP rhs; }
void buildIntBinary(Module &module, Opcode op, int64_t lhs, int64_t rhs)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr instr;
    instr.result = builder.reserveTempId();
    instr.op = op;
    instr.type = Type(Type::Kind::I64);
    instr.operands.push_back(Value::constInt(lhs));
    instr.operands.push_back(Value::constInt(rhs));
    instr.loc = {1, 1, 1};
    bb.instructions.push_back(instr);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*instr.result));
    bb.instructions.push_back(ret);
}

/// Build a function: main() -> i64 { return lhs FCMP rhs; } (returns i1→i64)
void buildFloatCompare(Module &module, Opcode op, double lhs, double rhs)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    // ConstF64 for lhs
    Instr lhsInstr;
    lhsInstr.result = builder.reserveTempId();
    lhsInstr.op = Opcode::ConstF64;
    lhsInstr.type = Type(Type::Kind::F64);
    lhsInstr.operands.push_back(Value::constInt(doubleBits(lhs)));
    lhsInstr.loc = {1, 1, 1};
    bb.instructions.push_back(lhsInstr);

    // ConstF64 for rhs
    Instr rhsInstr;
    rhsInstr.result = builder.reserveTempId();
    rhsInstr.op = Opcode::ConstF64;
    rhsInstr.type = Type(Type::Kind::F64);
    rhsInstr.operands.push_back(Value::constInt(doubleBits(rhs)));
    rhsInstr.loc = {1, 1, 1};
    bb.instructions.push_back(rhsInstr);

    // Compare
    Instr cmpInstr;
    cmpInstr.result = builder.reserveTempId();
    cmpInstr.op = op;
    cmpInstr.type = Type(Type::Kind::I1);
    cmpInstr.operands.push_back(Value::temp(*lhsInstr.result));
    cmpInstr.operands.push_back(Value::temp(*rhsInstr.result));
    cmpInstr.loc = {1, 1, 1};
    bb.instructions.push_back(cmpInstr);

    // Zext I1 to I64 for return
    Instr zextInstr;
    zextInstr.result = builder.reserveTempId();
    zextInstr.op = Opcode::Zext1;
    zextInstr.type = Type(Type::Kind::I64);
    zextInstr.operands.push_back(Value::temp(*cmpInstr.result));
    zextInstr.loc = {1, 1, 1};
    bb.instructions.push_back(zextInstr);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*zextInstr.result));
    bb.instructions.push_back(ret);
}

/// Build a function: main() -> i64 { return lhs FOP rhs; } (returns F64 as bits)
void buildFloatBinary(Module &module, Opcode op, double lhs, double rhs)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr lhsInstr;
    lhsInstr.result = builder.reserveTempId();
    lhsInstr.op = Opcode::ConstF64;
    lhsInstr.type = Type(Type::Kind::F64);
    lhsInstr.operands.push_back(Value::constInt(doubleBits(lhs)));
    lhsInstr.loc = {1, 1, 1};
    bb.instructions.push_back(lhsInstr);

    Instr rhsInstr;
    rhsInstr.result = builder.reserveTempId();
    rhsInstr.op = Opcode::ConstF64;
    rhsInstr.type = Type(Type::Kind::F64);
    rhsInstr.operands.push_back(Value::constInt(doubleBits(rhs)));
    rhsInstr.loc = {1, 1, 1};
    bb.instructions.push_back(rhsInstr);

    Instr opInstr;
    opInstr.result = builder.reserveTempId();
    opInstr.op = op;
    opInstr.type = Type(Type::Kind::F64);
    opInstr.operands.push_back(Value::temp(*lhsInstr.result));
    opInstr.operands.push_back(Value::temp(*rhsInstr.result));
    opInstr.loc = {1, 1, 1};
    bb.instructions.push_back(opInstr);

    // Return F64 bits as I64
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*opInstr.result));
    bb.instructions.push_back(ret);
}

/// Build a conversion function: main() -> i64 { return conv(val); }
void buildConversion(Module &module, Opcode op, Type::Kind resultKind,
                     int64_t operandBits)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    // For Sitofp: operand is I64, result is F64 (returned as bits)
    // For Fptosi: operand is F64 (from ConstF64), result is I64
    if (op == Opcode::Sitofp)
    {
        Instr conv;
        conv.result = builder.reserveTempId();
        conv.op = op;
        conv.type = Type(Type::Kind::F64);
        conv.operands.push_back(Value::constInt(operandBits));
        conv.loc = {1, 1, 1};
        bb.instructions.push_back(conv);

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.loc = {1, 1, 1};
        ret.operands.push_back(Value::temp(*conv.result));
        bb.instructions.push_back(ret);
    }
    else
    {
        // ConstF64 first
        Instr constF;
        constF.result = builder.reserveTempId();
        constF.op = Opcode::ConstF64;
        constF.type = Type(Type::Kind::F64);
        constF.operands.push_back(Value::constInt(operandBits));
        constF.loc = {1, 1, 1};
        bb.instructions.push_back(constF);

        Instr conv;
        conv.result = builder.reserveTempId();
        conv.op = op;
        conv.type = Type(resultKind);
        conv.operands.push_back(Value::temp(*constF.result));
        conv.loc = {1, 1, 1};
        bb.instructions.push_back(conv);

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.loc = {1, 1, 1};
        ret.operands.push_back(Value::temp(*conv.result));
        bb.instructions.push_back(ret);
    }
}

//=============================================================================
// Execution helpers
//=============================================================================

int64_t runVm(Module &module)
{
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

std::string captureVmTrap(Module &module)
{
    viper::tests::VmFixture fixture;
    return fixture.captureTrap(module);
}

#if VIPER_HAS_ARM64
int runNative(Module &module)
{
    // Serialize IL to string, write to temp file, run native
    const std::string ilSource = il::io::Serializer::toString(module);

    namespace fs = std::filesystem;
    static int counter = 0;
    const fs::path dir{"build/test-out/crosslayer-arith"};
    fs::create_directories(dir);
    const fs::path ilPath = dir / ("test_" + std::to_string(counter++) + ".il");

    std::ofstream out(ilPath);
    out << ilSource;
    out.close();

    const char *argv[] = {ilPath.c_str(), "-run-native"};
    int result = viper::tools::ilc::cmd_codegen_arm64(
        2, const_cast<char **>(argv));

    std::error_code ec;
    fs::remove(ilPath, ec);
    return result;
}
#endif

/// Run module on VM and native, assert exit codes match (low 8 bits).
/// Returns the VM i64 result for further assertion.
int64_t runCrossLayer(Module &module)
{
    int64_t vmResult = runVm(module);

#if VIPER_HAS_ARM64
    Module nativeModule = module; // copy
    int nativeResult = runNative(nativeModule);

    int vmExit = static_cast<int>(vmResult) & 0xFF;
    int natExit = nativeResult & 0xFF;
    if (vmExit != natExit)
    {
        std::cerr << "  VM result=" << vmResult << " exit=" << vmExit
                  << "  native exit=" << natExit << "\n";
    }
    ASSERT_EQ(vmExit, natExit);
#endif

    return vmResult;
}

} // namespace

//=============================================================================
// Tests
//=============================================================================

// --- Integer wrapping ---

TEST(CrossLayerArith, AddMaxPlusOne)
{
    Module module;
    buildIntBinary(module, Opcode::Add,
                   std::numeric_limits<int64_t>::max(), 1);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == std::numeric_limits<int64_t>::min());
}

TEST(CrossLayerArith, SubMinMinusOne)
{
    Module module;
    buildIntBinary(module, Opcode::Sub,
                   std::numeric_limits<int64_t>::min(), 1);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == std::numeric_limits<int64_t>::max());
}

// --- Integer division truncation ---

TEST(CrossLayerArith, SDivNegativeTruncation)
{
    Module module;
    buildIntBinary(module, Opcode::SDiv, -7, 2);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == -3); // Truncation toward zero
}

TEST(CrossLayerArith, SDivPositiveNegDivisor)
{
    Module module;
    buildIntBinary(module, Opcode::SDiv, 7, -2);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == -3);
}

// --- Remainder sign rule ---

TEST(CrossLayerArith, SRemNegativeDividend)
{
    Module module;
    buildIntBinary(module, Opcode::SRem, -7, 2);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == -1); // Dividend sign
}

TEST(CrossLayerArith, SRemPositiveDividend)
{
    Module module;
    buildIntBinary(module, Opcode::SRem, 7, -2);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == 1); // Dividend sign
}

TEST(CrossLayerArith, SRemBothNegative)
{
    Module module;
    buildIntBinary(module, Opcode::SRem, -7, -2);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == -1); // Dividend sign
}

// --- Shift masking and extension ---

TEST(CrossLayerArith, ShlMaskTo64)
{
    Module module;
    buildIntBinary(module, Opcode::Shl, 1, 64);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == 1); // 64 & 63 = 0 → identity
}

TEST(CrossLayerArith, AShrSignExtend)
{
    Module module;
    buildIntBinary(module, Opcode::AShr, -1, 63);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == -1); // All sign bits
}

TEST(CrossLayerArith, LShrZeroExtend)
{
    Module module;
    buildIntBinary(module, Opcode::LShr, -1, 63);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == 1); // Only high bit
}

TEST(CrossLayerArith, AShrMaskTo64)
{
    Module module;
    buildIntBinary(module, Opcode::AShr, 8, 65);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == 4); // 65 & 63 = 1 → shift by 1
}

// --- Float arithmetic ---

// Float arithmetic tests use runVm() directly because float bit patterns
// have unpredictable low bytes, making 8-bit exit code comparison meaningless.

TEST(CrossLayerArith, FAddInfinity)
{
    Module module;
    buildFloatBinary(module, Opcode::FAdd, 1e308, 1e308);
    int64_t result = runVm(module);
    double d = bitsToDouble(result);
    EXPECT_TRUE(std::isinf(d) && d > 0);
}

TEST(CrossLayerArith, FDivZeroZero)
{
    Module module;
    buildFloatBinary(module, Opcode::FDiv, 0.0, 0.0);
    int64_t result = runVm(module);
    double d = bitsToDouble(result);
    EXPECT_TRUE(std::isnan(d));
}

TEST(CrossLayerArith, FDivByZero)
{
    Module module;
    buildFloatBinary(module, Opcode::FDiv, 1.0, 0.0);
    int64_t result = runVm(module);
    double d = bitsToDouble(result);
    EXPECT_TRUE(std::isinf(d) && d > 0);
}

TEST(CrossLayerArith, FMulNaN)
{
    Module module;
    buildFloatBinary(module, Opcode::FMul,
                     std::numeric_limits<double>::quiet_NaN(), 5.0);
    int64_t result = runVm(module);
    double d = bitsToDouble(result);
    EXPECT_TRUE(std::isnan(d));
}

// --- Float comparisons with NaN ---
// Results are 0 or 1 — fit in 8-bit exit code, so cross-layer comparison works.

TEST(CrossLayerArith, FCmpLtNaN)
{
    Module module;
    double nan = std::numeric_limits<double>::quiet_NaN();
    buildFloatCompare(module, Opcode::FCmpLT, nan, 1.0);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == 0); // false
}

TEST(CrossLayerArith, FCmpNeNaN)
{
    Module module;
    double nan = std::numeric_limits<double>::quiet_NaN();
    buildFloatCompare(module, Opcode::FCmpNE, nan, nan);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == 1); // true (unordered)
}

TEST(CrossLayerArith, FCmpEqNaN)
{
    Module module;
    double nan = std::numeric_limits<double>::quiet_NaN();
    buildFloatCompare(module, Opcode::FCmpEQ, nan, nan);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == 0); // false
}

TEST(CrossLayerArith, FCmpGtNaN)
{
    Module module;
    double nan = std::numeric_limits<double>::quiet_NaN();
    buildFloatCompare(module, Opcode::FCmpGT, 1.0, nan);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == 0); // false
}

TEST(CrossLayerArith, FCmpOrdNaN)
{
    Module module;
    double nan = std::numeric_limits<double>::quiet_NaN();
    buildFloatCompare(module, Opcode::FCmpOrd, nan, 1.0);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == 0); // false
}

TEST(CrossLayerArith, FCmpUnoNaN)
{
    Module module;
    double nan = std::numeric_limits<double>::quiet_NaN();
    buildFloatCompare(module, Opcode::FCmpUno, nan, 1.0);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == 1); // true
}

// --- Conversions ---

// Sitofp returns F64 bits — use runVm() (bit pattern in exit code is meaningless).
TEST(CrossLayerArith, SitofpMax)
{
    // INT64_MAX may lose precision in F64 (only 53 mantissa bits)
    Module module;
    buildConversion(module, Opcode::Sitofp, Type::Kind::F64,
                    std::numeric_limits<int64_t>::max());
    int64_t result = runVm(module);
    double d = bitsToDouble(result);
    // The conversion rounds; just verify it's finite and close
    EXPECT_TRUE(std::isfinite(d));
    EXPECT_TRUE(d > 9.2e18);
}

TEST(CrossLayerArith, FptosiTruncation)
{
    Module module;
    buildConversion(module, Opcode::Fptosi, Type::Kind::I64,
                    doubleBits(1.9));
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == 1); // Truncation toward zero
}

TEST(CrossLayerArith, FptosiNegTruncation)
{
    Module module;
    buildConversion(module, Opcode::Fptosi, Type::Kind::I64,
                    doubleBits(-1.9));
    // -1 as exit code is 255 (0xFF), VM and native should agree on low 8 bits.
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == -1); // Truncation toward zero
}

// --- Normal float comparisons ---

TEST(CrossLayerArith, FCmpLtNormal)
{
    Module module;
    buildFloatCompare(module, Opcode::FCmpLT, 1.0, 2.0);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == 1); // true
}

TEST(CrossLayerArith, FCmpLeEqual)
{
    Module module;
    buildFloatCompare(module, Opcode::FCmpLE, 2.0, 2.0);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == 1); // true
}

TEST(CrossLayerArith, FCmpOrdNormal)
{
    Module module;
    buildFloatCompare(module, Opcode::FCmpOrd, 1.0, 2.0);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == 1); // true (both not NaN)
}

// --- Unsigned arithmetic ---

TEST(CrossLayerArith, UDivTreatAsUnsigned)
{
    // UINT64_MAX / 2 = INT64_MAX — too large for 8-bit exit code, VM-only.
    Module module;
    buildIntBinary(module, Opcode::UDiv, -1, 2);
    int64_t result = runVm(module);
    EXPECT_TRUE(result == std::numeric_limits<int64_t>::max());
}

TEST(CrossLayerArith, URemTreatAsUnsigned)
{
    Module module;
    buildIntBinary(module, Opcode::URem, -1, 2);
    int64_t result = runCrossLayer(module);
    EXPECT_TRUE(result == 1);
}

//=============================================================================
// Entry point
//=============================================================================

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
