//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_bugfix.cpp
// Purpose: Regression tests for ARM64 codegen bug fixes #1, #2, #3, #4.
//
//===----------------------------------------------------------------------===//
#include "tests/TestHarness.hpp"
#include <filesystem>
#include <fstream>
#include <string>

#include "tools/viper/cmd_codegen_arm64.hpp"

using namespace viper::tools::ilc;

static std::string outPath(const std::string &name)
{
    namespace fs = std::filesystem;
    const fs::path dir{"build/test-out/arm64"};
    fs::create_directories(dir);
    return (dir / name).string();
}

static void writeFile(const std::string &path, const std::string &text)
{
    std::ofstream ofs(path);
    ASSERT_TRUE(static_cast<bool>(ofs));
    ofs << text;
}

/// Bug #3: void main should exit with code 0, not whatever was in x0.
TEST(Arm64Bugfix, VoidMainExitZero)
{
    const std::string in = outPath("arm64_bugfix_void_main.il");
    // A void main that calls a runtime function leaving a non-zero value in x0.
    // Before the fix, this would exit with whatever rt_term_say left in x0.
    const std::string il = "il 0.2.0\n"
                           "extern @Viper.Terminal.Say(str) -> void\n"
                           "global const str @.msg = \"hello\"\n"
                           "func @main() -> void {\n"
                           "entry_0:\n"
                           "  %t0 = const_str @.msg\n"
                           "  call @Viper.Terminal.Say(%t0)\n"
                           "  ret\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-run-native"};
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);
}

/// Bug #1: Boolean return values should be masked to i1 (0 or 1).
/// Tests that a runtime function returning bool is correctly captured.
TEST(Arm64Bugfix, BoolReturnMasked)
{
    const std::string in = outPath("arm64_bugfix_bool_return.il");
    // Calls rt_str_eq which returns bool (i1). If the masking works,
    // the comparison and conditional branch should function correctly.
    const std::string il = "il 0.2.0\n"
                           "extern @Viper.String.Equals(str, str) -> i1\n"
                           "global const str @.a = \"hello\"\n"
                           "global const str @.b = \"hello\"\n"
                           "func @main() -> i64 {\n"
                           "entry_0:\n"
                           "  %t0 = const_str @.a\n"
                           "  %t1 = const_str @.b\n"
                           "  %t2 = call @Viper.String.Equals(%t0, %t1)\n"
                           "  cbr %t2, yes_0, no_0\n"
                           "yes_0:\n"
                           "  ret 0\n"
                           "no_0:\n"
                           "  ret 1\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-run-native"};
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    // Equal strings should return exit code 0 (took the yes branch)
    ASSERT_EQ(rc, 0);
}

/// Bug #4: AddFpImm operand must be classified as DEF-only (not USE-only) in
/// RegAllocLinear::operandRoles so that the dirty flag is set after alloca
/// address materialisation.
///
/// Without the fix, when register pressure forced the eviction of the AddFpImm
/// result vreg before it was consumed by the following AddRI (GEP offset add),
/// the dirty flag remained false and no spill store was emitted.  The
/// subsequent reload then read an uninitialised frame slot, producing a garbage
/// address that caused a store to crash (EXC_BAD_ACCESS in the chess demo).
///
/// The test uses 22 live temps (> 19 available GPRs) so that the pool is full
/// when the critical field-3 GEP (offset 24) is processed.  All 22 temps have
/// future uses AFTER the critical GEP, so they all have finite next-use
/// distances and are not selected as spill victims — only the AddFpImm result
/// has UINT_MAX distance (no use after AddRI), making the eviction deterministic.
TEST(Arm64Bugfix, AddFpImmDirtyFlagUnderPressure)
{
    // Build IL with 22 live temporaries + alloca + GEP store at offset 24.
    // The 22 temps all have future uses (stores after field 3), so only the
    // AddFpImm result vreg (no future use past the AddRI) has UINT_MAX distance
    // and is selected as the eviction victim.
    std::string il = "il 0.1\n"
                     "func @main() -> i64 {\n"
                     "entry:\n";

    // 22 live temps — enough to exceed the 19-register GPR pool
    for (int i = 0; i < 22; ++i)
        il += "  %v" + std::to_string(i) + " = add 0, " + std::to_string(i + 1) + "\n";

    // 192-byte alloca (enough for 24 i64 fields)
    il += "  %base = alloca 192\n";

    // CRITICAL: GEP at offset 24 (field 3) — triggers the AddFpImm dirty-flag bug.
    // Without fix: AddFpImm result evicted with dirty=false → reload from
    // uninitialised slot → garbage address → SIGSEGV.
    // With fix:    AddFpImm result evicted with dirty=true  → slot written →
    // reload correct → store 42 to correct address.
    il += "  %p24 = gep %base, 24\n";
    il += "  store i64, %p24, 42\n";

    // Use all 22 temps in stores AFTER field 3 so they have finite future-use
    // distances during field-3 GEP processing (ensures they are not evicted
    // instead of the AddFpImm result).
    // Offsets: 0,8,16,32,40,...,176 — skipping 24 which holds the sentinel.
    int offset = 0;
    for (int i = 0; i < 22; ++i)
    {
        if (offset == 24)
            offset += 8; // skip offset 24 (sentinel slot)
        il += "  %q" + std::to_string(i) + " = gep %base, " + std::to_string(offset) + "\n";
        il += "  store i64, %q" + std::to_string(i) + ", %v" + std::to_string(i) + "\n";
        offset += 8;
    }

    // Load sentinel from field 3 and return — must be 42
    il += "  %result = load i64, %p24\n";
    il += "  ret %result\n";
    il += "}\n";

    const std::string in = outPath("arm64_bugfix_addfpimm_dirty.il");
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-run-native"};
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    ASSERT_EQ(rc, 42);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
