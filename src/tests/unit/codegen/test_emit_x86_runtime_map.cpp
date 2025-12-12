//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_emit_x86_runtime_map.cpp
// Purpose: Ensure x86-64 call emission rewrites canonical Viper.* runtime
//          names using the shared runtime alias map.
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/AsmEmitter.hpp"
#include "codegen/x86_64/MachineIR.hpp"
#include "codegen/x86_64/TargetX64.hpp"
#include "tests/unit/GTestStub.hpp"

#include <sstream>

using namespace viper::codegen::x64;

TEST(Codegen_X64_RuntimeNameMap, CanonicalNamesMapToRuntimeSymbols)
{
    AsmEmitter::RoDataPool pool;
    AsmEmitter emitter(pool);

    MFunction fn{};
    fn.name = "main";
    MBasicBlock entry{};
    entry.label = fn.name;
    entry.append(MInstr::make(MOpcode::CALL, {makeLabelOperand("Viper.Terminal.PrintI64")}));
    entry.append(MInstr::make(MOpcode::RET));
    fn.blocks.push_back(entry);

    std::ostringstream os;
    emitter.emitFunction(os, fn, sysvTarget());
    const std::string asmText = os.str();

    EXPECT_NE(asmText.find("rt_print_i64"), std::string::npos);
    EXPECT_EQ(asmText.find("Viper.Terminal.PrintI64"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
