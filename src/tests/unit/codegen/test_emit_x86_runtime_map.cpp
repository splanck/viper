//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_emit_x86_runtime_map.cpp
// Purpose: Ensure x86-64 call emission rewrites canonical Zanna.* runtime
//          names using the shared runtime alias map.
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/AsmEmitter.hpp"
#include "codegen/x86_64/MachineIR.hpp"
#include "codegen/x86_64/TargetX64.hpp"
#include "tests/TestHarness.hpp"
#include <sstream>

using namespace zanna::codegen::x64;

TEST(Codegen_X64_RuntimeNameMap, CanonicalNamesMapToRuntimeSymbols) {
    AsmEmitter::RoDataPool pool;
    AsmEmitter emitter(pool, zanna::codegen::objfile::ObjFormat::ELF);

    MFunction fn{};
    fn.name = "main";
    MBasicBlock entry{};
    entry.label = fn.name;
    entry.append(MInstr::make(MOpcode::CALL, {makeLabelOperand("Zanna.Terminal.PrintI64")}));
    entry.append(MInstr::make(MOpcode::RET));
    fn.blocks.push_back(entry);

    std::ostringstream os;
    emitter.emitFunction(os, fn, sysvTarget());
    const std::string asmText = os.str();

    EXPECT_NE(asmText.find("rt_print_i64"), std::string::npos);
    EXPECT_EQ(asmText.find("Zanna.Terminal.PrintI64"), std::string::npos);
}

TEST(Codegen_X64_RuntimeNameMap, MachODirectSymbolsUseDarwinPrefix) {
    AsmEmitter::RoDataPool pool;
    AsmEmitter emitter(pool, zanna::codegen::objfile::ObjFormat::MachO);

    MFunction fn{};
    fn.name = "main";
    MBasicBlock entry{};
    entry.label = fn.name;
    entry.append(MInstr::make(MOpcode::CALL, {makeLabelOperand("Zanna.Terminal.PrintI64")}));
    entry.append(
        MInstr::make(MOpcode::LEA,
                     {makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RAX)),
                      makeRipLabelOperand("global_data")}));
    entry.append(MInstr::make(MOpcode::JMP, {makeLabelOperand(".Ldone")}));
    entry.append(MInstr::make(MOpcode::LABEL, {makeLabelOperand(".Ldone")}));
    entry.append(MInstr::make(MOpcode::RET));
    fn.blocks.push_back(entry);

    std::ostringstream os;
    emitter.emitFunction(os, fn, sysvTarget());
    const std::string asmText = os.str();

    EXPECT_NE(asmText.find(".globl _main"), std::string::npos);
    EXPECT_NE(asmText.find("_main:"), std::string::npos);
    EXPECT_NE(asmText.find("callq _rt_print_i64"), std::string::npos);
    EXPECT_NE(asmText.find("_global_data(%rip)"), std::string::npos);
    EXPECT_NE(asmText.find("jmp .Ldone"), std::string::npos);
    EXPECT_EQ(asmText.find("jmp _.Ldone"), std::string::npos);
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, &argv);
    return zanna_test::run_all_tests();
}
