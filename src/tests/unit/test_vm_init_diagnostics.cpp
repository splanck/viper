//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_init_diagnostics.cpp
// Purpose: Verify VM initialization failures are reported as exceptions.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "il/core/Module.hpp"
#include "vm/VM.hpp"

#include <stdexcept>
#include <string>

TEST(VMInitDiagnostics, SharedProgramStateModuleMismatchThrowsRuntimeError) {
    il::core::Module first;
    il::core::Module second;

    il::vm::VM owner(first);
    auto state = owner.programState();
    ASSERT_TRUE(state != nullptr);

    bool threw = false;
    std::string message;
    try {
        il::vm::VM mismatched(second, state);
    } catch (const std::runtime_error &ex) {
        threw = true;
        message = ex.what();
    }

    EXPECT_TRUE(threw);
    EXPECT_CONTAINS(message, "shared ProgramState belongs to a different module");
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
