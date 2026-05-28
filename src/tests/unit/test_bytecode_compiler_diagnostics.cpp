//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_bytecode_compiler_diagnostics.cpp
// Purpose: Ensure bytecode lowering rejects unsupported or malformed IL with diagnostics.
//
//===----------------------------------------------------------------------===//

#include "bytecode/Bytecode.hpp"
#include "bytecode/BytecodeCompiler.hpp"
#include "bytecode/BytecodeVM.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "support/source_location.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

#include <string>
#include <utility>

namespace {

viper::bytecode::BCOpcode opcodeOf(uint32_t word) {
    return viper::bytecode::decodeOpcode(word);
}

bool containsOpcode(const viper::bytecode::BytecodeFunction &fn, viper::bytecode::BCOpcode op) {
    for (uint32_t word : fn.code) {
        if (opcodeOf(word) == op)
            return true;
    }
    return false;
}

il::core::Module wrapInMain(il::core::Instr instr) {
    using namespace il::core;

    Module module;

    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";
    entry.instructions.push_back(std::move(instr));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {7, 4, 1};
    entry.instructions.push_back(ret);
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(fn));
    return module;
}

TEST(BytecodeCompilerDiagnostics, UnknownGAddrGlobalIsCompileDiagnostic) {
    using namespace il::core;

    Instr instr;
    instr.result = 0;
    instr.op = Opcode::GAddr;
    instr.type = Type(Type::Kind::Ptr);
    instr.operands.push_back(Value::global("data"));
    instr.loc = {7, 3, 5};

    viper::bytecode::BytecodeCompiler compiler;
    auto result = compiler.compileChecked(wrapInMain(std::move(instr)), nullptr, true);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "V-BC-UNKNOWN-GLOBAL");
    EXPECT_EQ(result.error().loc.line, 3u);
    EXPECT_CONTAINS(result.error().message, "bytecode compile failed in @main");
    EXPECT_CONTAINS(result.error().message, "unknown global @data");
}

TEST(BytecodeCompilerDiagnostics, UnknownStringGlobalIsCompileDiagnostic) {
    using namespace il::core;

    Instr instr;
    instr.result = 0;
    instr.op = Opcode::ConstStr;
    instr.type = Type(Type::Kind::Str);
    instr.operands.push_back(Value::global("missing_string"));
    instr.loc = {7, 8, 9};

    viper::bytecode::BytecodeCompiler compiler;
    auto result = compiler.compileChecked(wrapInMain(std::move(instr)), nullptr, true);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "V-BC-UNKNOWN-STRING-GLOBAL");
    EXPECT_EQ(result.error().loc.column, 9u);
    EXPECT_CONTAINS(result.error().message, "unknown string global");
}

TEST(BytecodeCompilerDiagnostics, MissingOperandsAreCompileDiagnostics) {
    using namespace il::core;

    Instr instr;
    instr.result = 0;
    instr.op = Opcode::Alloca;
    instr.type = Type(Type::Kind::Ptr);
    instr.loc = {7, 11, 3};

    viper::bytecode::BytecodeCompiler compiler;
    auto result = compiler.compileChecked(wrapInMain(std::move(instr)), nullptr, true);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "V-BC-MALFORMED-INSTR");
    EXPECT_CONTAINS(result.error().message, "requires at least 1 operand");
}

TEST(BytecodeCompilerDiagnostics, EmitsPerPcSourceDebugMetadata) {
    using namespace il::core;

    il::support::SourceManager sm;
    const uint32_t fileId = sm.addFile("debug_source.zia");
    ASSERT_TRUE(fileId != 0);

    Instr instr;
    instr.op = Opcode::Trap;
    instr.type = Type(Type::Kind::Void);
    instr.loc = {fileId, 12, 5};

    viper::bytecode::BytecodeCompiler compiler;
    auto result = compiler.compileChecked(wrapInMain(std::move(instr)), &sm, true);

    ASSERT_TRUE(result.hasValue());
    const auto &module = result.value();
    ASSERT_EQ(module.functions.size(), 1u);
    ASSERT_GE(module.sourceFiles.size(), 1u);
    EXPECT_EQ(module.sourceFiles[0].path, "debug_source.zia");

    const auto &fn = module.functions[0];
    ASSERT_FALSE(fn.lineTable.empty());
    ASSERT_FALSE(fn.sourceFileTable.empty());
    ASSERT_FALSE(fn.blockLabelTable.empty());
    EXPECT_EQ(fn.lineTable[0], 12u);
    EXPECT_EQ(fn.sourceFileTable[0], 1u);
    EXPECT_EQ(fn.blockLabelTable[0], "entry");
}

TEST(BytecodeCompilerDiagnostics, PlainTrapLowersToDomainError) {
    using namespace il::core;
    using namespace viper::bytecode;

    Instr instr;
    instr.op = Opcode::Trap;
    instr.type = Type(Type::Kind::Void);
    instr.loc = {7, 19, 2};

    BytecodeCompiler compiler;
    auto result = compiler.compileChecked(wrapInMain(std::move(instr)), nullptr, true);

    ASSERT_TRUE(result.hasValue());
    const auto &code = result.value().functions[0].code;
    ASSERT_FALSE(code.empty());
    EXPECT_EQ(opcodeOf(code[0]), BCOpcode::TRAP);
    EXPECT_EQ(decodeArg8_0(code[0]), static_cast<uint8_t>(TrapKind::DomainError));
}

TEST(BytecodeCompilerDiagnostics, TrapErrLowersToErrorValueNotTrap) {
    using namespace il::core;
    using namespace viper::bytecode;

    Instr instr;
    instr.result = 0;
    instr.op = Opcode::TrapErr;
    instr.type = Type(Type::Kind::Error);
    instr.operands.push_back(Value::constInt(7));
    instr.loc = {7, 20, 2};

    BytecodeCompiler compiler;
    auto result = compiler.compileChecked(wrapInMain(std::move(instr)), nullptr, true);

    ASSERT_TRUE(result.hasValue());
    const auto &fn = result.value().functions[0];
    EXPECT_TRUE(containsOpcode(fn, BCOpcode::LOAD_I8));
    EXPECT_TRUE(containsOpcode(fn, BCOpcode::STORE_LOCAL));
    EXPECT_FALSE(containsOpcode(fn, BCOpcode::TRAP));
    EXPECT_FALSE(containsOpcode(fn, BCOpcode::TRAP_FROM_ERR));
}

TEST(BytecodeCompilerDiagnostics, TrapErrWithoutResultIsCompileDiagnostic) {
    using namespace il::core;

    Instr instr;
    instr.op = Opcode::TrapErr;
    instr.type = Type(Type::Kind::Error);
    instr.operands.push_back(Value::constInt(7));
    instr.loc = {7, 21, 2};

    viper::bytecode::BytecodeCompiler compiler;
    auto result = compiler.compileChecked(wrapInMain(std::move(instr)), nullptr, true);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "V-BC-MALFORMED-INSTR");
    EXPECT_CONTAINS(result.error().message, "trap.err must produce an error value");
}

TEST(BytecodeCompilerDiagnostics, PreflightRejectsInvalidILBeforeLowering) {
    using namespace il::core;

    Instr instr;
    instr.result = 0;
    instr.op = Opcode::Add;
    instr.type = Type(Type::Kind::I64);
    instr.operands.push_back(Value::temp(99));
    instr.operands.push_back(Value::constInt(1));
    instr.loc = {7, 14, 2};

    viper::bytecode::BytecodeCompiler compiler;
    auto result = compiler.compileChecked(wrapInMain(std::move(instr)));

    ASSERT_FALSE(result.hasValue());
    EXPECT_CONTAINS(result.error().message, "bytecode preflight failed");
}

TEST(BytecodeCompilerDiagnostics, UnknownSsaIsCompileDiagnosticWhenVerifierWasBypassed) {
    using namespace il::core;

    Instr instr;
    instr.result = 0;
    instr.op = Opcode::Add;
    instr.type = Type(Type::Kind::I64);
    instr.operands.push_back(Value::temp(99));
    instr.operands.push_back(Value::constInt(1));
    instr.loc = {7, 15, 4};

    viper::bytecode::BytecodeCompiler compiler;
    auto result = compiler.compileChecked(wrapInMain(std::move(instr)), nullptr, true);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "V-BC-UNKNOWN-SSA");
    EXPECT_CONTAINS(result.error().message, "unknown SSA value %99");
}

} // namespace

int main() {
    return viper_test::run_all_tests();
}
