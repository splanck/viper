//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_native_asm.cpp
// Purpose: End-to-end tests for the AArch64 native assembler path (--native-asm).
//          Verifies that the binary encoder + Mach-O/ELF writer pipeline produces
//          correct executables that match the system assembler path behavior.
// Key invariants:
//   - Native asm path must produce identical execution results to text asm path
//   - .o output must be a valid Mach-O/ELF object file
//   - String rodata, runtime calls, and control flow must all work correctly
// Ownership/Lifetime:
//   - Each test writes temporary files and cleans them up
// Links: codegen/aarch64/passes/BinaryEmitPass.hpp
//        tools/viper/cmd_codegen_arm64.cpp
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "tools/viper/cmd_codegen_arm64.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace viper::tools::ilc;

namespace {

std::string outPath(const std::string &name) {
    namespace fs = std::filesystem;
    const fs::path dir{"build/test-out/arm64-nativeasm"};
    fs::create_directories(dir);
    return (dir / name).string();
}

void writeFile(const std::string &path, const std::string &text) {
    std::ofstream ofs(path);
    ASSERT_TRUE(static_cast<bool>(ofs));
    ofs << text;
}

/// Run cmd_codegen_arm64 with the given arguments and return the exit code.
int runArm64(std::initializer_list<const char *> args) {
    std::vector<char *> argv;
    for (const char *a : args)
        argv.push_back(const_cast<char *>(a));
    return cmd_codegen_arm64(static_cast<int>(argv.size()), argv.data());
}

} // namespace

// ---------------------------------------------------------------------------
// 1. Basic return value — simplest possible native-asm E2E
// ---------------------------------------------------------------------------
TEST(NativeAsmArm64, BasicReturn0) {
    const std::string in = outPath("nasm_ret0.il");
    writeFile(in,
              "il 0.1\n"
              "func @main() -> i64 {\n"
              "entry:\n"
              "  ret 0\n"
              "}\n");
    ASSERT_EQ(runArm64({in.c_str(), "--native-asm", "-run-native"}), 0);
}

TEST(NativeAsmArm64, BasicReturn42) {
    const std::string in = outPath("nasm_ret42.il");
    writeFile(in,
              "il 0.1\n"
              "func @main() -> i64 {\n"
              "entry:\n"
              "  ret 42\n"
              "}\n");
    ASSERT_EQ(runArm64({in.c_str(), "--native-asm", "-run-native"}), 42);
}

// ---------------------------------------------------------------------------
// 2. Runtime call — tests external symbol relocation
// ---------------------------------------------------------------------------
TEST(NativeAsmArm64, RuntimeCallPrintI64) {
    const std::string in = outPath("nasm_print_i64.il");
    const std::string exe = outPath("nasm_print_i64_exe");
    writeFile(in,
              "il 0.2.0\n"
              "extern @rt_print_i64(i64) -> void\n"
              "func @main() -> i64 {\n"
              "entry:\n"
              "  call @rt_print_i64(99)\n"
              "  ret 0\n"
              "}\n");
    ASSERT_EQ(runArm64({in.c_str(), "--native-asm", "-o", exe.c_str()}), 0);
    ASSERT_TRUE(std::filesystem::exists(exe));
    std::filesystem::remove(exe);
}

// ---------------------------------------------------------------------------
// 3. String rodata — tests rodata pool + ADRP/ADD relocations
// ---------------------------------------------------------------------------
TEST(NativeAsmArm64, StringRodata) {
    const std::string in = outPath("nasm_str.il");
    const std::string exe = outPath("nasm_str_exe");
    writeFile(in,
              "il 0.2.0\n"
              "extern @rt_print_str(str) -> void\n"
              "global const str @.L0 = \"HELLO\"\n"
              "func @main() -> i64 {\n"
              "entry:\n"
              "  %t0 = const_str @.L0\n"
              "  call @rt_print_str(%t0)\n"
              "  ret 0\n"
              "}\n");
    ASSERT_EQ(runArm64({in.c_str(), "--native-asm", "-o", exe.c_str()}), 0);
    ASSERT_TRUE(std::filesystem::exists(exe));
    std::filesystem::remove(exe);
}

// ---------------------------------------------------------------------------
// 4. Control flow — branches, conditional, block parameters
// ---------------------------------------------------------------------------
TEST(NativeAsmArm64, ControlFlowBranch) {
    const std::string in = outPath("nasm_branch.il");
    writeFile(in,
              "il 0.1\n"
              "func @main() -> i64 {\n"
              "entry:\n"
              "  %cond = scmp_gt 5, 3\n"
              "  cbr %cond, yes(7), no(13)\n"
              "yes(%a:i64):\n"
              "  br done(%a)\n"
              "no(%b:i64):\n"
              "  br done(%b)\n"
              "done(%r:i64):\n"
              "  ret %r\n"
              "}\n");
    ASSERT_EQ(runArm64({in.c_str(), "--native-asm", "-run-native"}), 7);
}

// ---------------------------------------------------------------------------
// 5. Multi-function — cross-function call
// ---------------------------------------------------------------------------
TEST(NativeAsmArm64, MultiFunctionCall) {
    const std::string in = outPath("nasm_multifunc.il");
    writeFile(in,
              "il 0.1\n"
              "func @add(%a:i64, %b:i64) -> i64 {\n"
              "entry(%a:i64, %b:i64):\n"
              "  %r = iadd.ovf %a, %b\n"
              "  ret %r\n"
              "}\n"
              "func @main() -> i64 {\n"
              "entry:\n"
              "  %v = call @add(17, 25)\n"
              "  ret %v\n"
              "}\n");
    ASSERT_EQ(runArm64({in.c_str(), "--native-asm", "-run-native"}), 42);
}

// ---------------------------------------------------------------------------
// 6. .o output only — verify object file creation
// ---------------------------------------------------------------------------
TEST(NativeAsmArm64, DotOOutput) {
    const std::string in = outPath("nasm_dot_o.il");
    const std::string obj = outPath("nasm_dot_o.o");
    writeFile(in,
              "il 0.1\n"
              "func @main() -> i64 {\n"
              "entry:\n"
              "  ret 0\n"
              "}\n");
    ASSERT_EQ(runArm64({in.c_str(), "--native-asm", "-o", obj.c_str()}), 0);
    ASSERT_TRUE(std::filesystem::exists(obj));
    // Verify it's a valid Mach-O or ELF file by checking magic bytes.
    std::ifstream f(obj, std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(f));
    uint8_t magic[4]{};
    f.read(reinterpret_cast<char *>(magic), 4);
    ASSERT_TRUE(static_cast<bool>(f));
#if defined(__APPLE__)
    // Mach-O: 0xFEEDFACF (64-bit little-endian)
    EXPECT_EQ(magic[0], 0xCF);
    EXPECT_EQ(magic[1], 0xFA);
    EXPECT_EQ(magic[2], 0xED);
    EXPECT_EQ(magic[3], 0xFE);
#else
    // ELF: 0x7F 'E' 'L' 'F'
    EXPECT_EQ(magic[0], 0x7F);
    EXPECT_EQ(magic[1], 'E');
    EXPECT_EQ(magic[2], 'L');
    EXPECT_EQ(magic[3], 'F');
#endif
    std::filesystem::remove(obj);
}

// ---------------------------------------------------------------------------
// 7. Dual-path equivalence — native-asm exit code matches system-asm
// ---------------------------------------------------------------------------
TEST(NativeAsmArm64, EquivBasicReturn) {
    const std::string in = outPath("nasm_equiv.il");
    const std::string sysExe = outPath("nasm_equiv_sys");
    const std::string natExe = outPath("nasm_equiv_nat");
    writeFile(in,
              "il 0.1\n"
              "func @add(%a:i64, %b:i64) -> i64 {\n"
              "entry(%a:i64, %b:i64):\n"
              "  %r = iadd.ovf %a, %b\n"
              "  ret %r\n"
              "}\n"
              "func @main() -> i64 {\n"
              "entry:\n"
              "  %v = call @add(10, 7)\n"
              "  ret %v\n"
              "}\n");
    // System assembler path
    const int sysRc = runArm64({in.c_str(), "-o", sysExe.c_str(), "-run-native"});
    // Native assembler path
    const int natRc = runArm64({in.c_str(), "--native-asm", "-o", natExe.c_str(), "-run-native"});
    EXPECT_EQ(sysRc, natRc);
    // Both should return 17 (10 + 7)
    EXPECT_EQ(natRc, 17);

    std::filesystem::remove(sysExe);
    std::filesystem::remove(natExe);
}

// ---------------------------------------------------------------------------
// 8. Arithmetic with overflow checks — tests IAddOvf lowering
// ---------------------------------------------------------------------------
TEST(NativeAsmArm64, OverflowArithmetic) {
    const std::string in = outPath("nasm_ovf.il");
    writeFile(in,
              "il 0.2.0\n"
              "func @main() -> i64 {\n"
              "entry:\n"
              "  %a = iadd.ovf 20, 22\n"
              "  ret %a\n"
              "}\n");
    ASSERT_EQ(runArm64({in.c_str(), "--native-asm", "-run-native"}), 42);
}

// ---------------------------------------------------------------------------
// 9. Floating-point — tests FP register encoding and ABI
// ---------------------------------------------------------------------------
TEST(NativeAsmArm64, FloatingPoint) {
    const std::string in = outPath("nasm_fp.il");
    writeFile(in,
              "il 0.2.0\n"
              "func @main() -> i64 {\n"
              "entry:\n"
              "  %a = const.f64 3.14\n"
              "  %b = const.f64 2.86\n"
              "  %c = fadd %a, %b\n"
              "  %r = cast.fp_to_si.rte.chk %c\n"
              "  ret %r\n"
              "}\n");
    // 3.14 + 2.86 = 6.0, truncated to 6
    ASSERT_EQ(runArm64({in.c_str(), "--native-asm", "-run-native"}), 6);
}

// ---------------------------------------------------------------------------
// 10. Assembly output (-S) still works alongside --native-asm
// ---------------------------------------------------------------------------
TEST(NativeAsmArm64, AsmOutputStillWorks) {
    const std::string in = outPath("nasm_asm_out.il");
    const std::string asmOut = outPath("nasm_asm_out.s");
    writeFile(in,
              "il 0.1\n"
              "func @main() -> i64 {\n"
              "entry:\n"
              "  ret 0\n"
              "}\n");
    // -S should produce assembly even with --native-asm
    ASSERT_EQ(runArm64({in.c_str(), "--native-asm", "-S", asmOut.c_str()}), 0);
    ASSERT_TRUE(std::filesystem::exists(asmOut));
    // Verify it contains assembly text
    std::ifstream f(asmOut);
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_TRUE(content.find("main") != std::string::npos);
    std::filesystem::remove(asmOut);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
