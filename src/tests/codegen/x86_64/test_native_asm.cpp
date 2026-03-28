//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_native_asm.cpp
// Purpose: End-to-end tests for the x86_64 native assembler path (--native-asm).
//          Uses CodegenPipeline directly with AssemblerMode::Native to verify
//          the binary encoder + object file writer pipeline produces correct
//          executables.
// Key invariants:
//   - Native asm path must produce correct execution results
//   - .o output must be a valid object file for the host platform
//   - Both native-asm and system-asm paths must produce equivalent results
// Ownership/Lifetime:
//   - Each test writes temporary IL files and cleans them up
// Links: codegen/x86_64/CodegenPipeline.hpp
//        codegen/x86_64/binenc/X64BinaryEncoder.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/CodegenPipeline.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

static int gFail = 0;
static int gPass = 0;
static const char *gCurrentTest = nullptr;

static void check(bool cond, const char *msg, int line) {
    if (!cond) {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define ASSERT(cond)                                                                               \
    do {                                                                                           \
        check((cond), #cond, __LINE__);                                                            \
        if (!(cond))                                                                               \
            return;                                                                                \
    } while (0)

#define TEST_BEGIN(name)                                                                           \
    static void test_##name() {                                                                    \
        gCurrentTest = #name;                                                                      \
        std::cerr << "[  RUN     ] NativeAsmX64." #name "\n";

#define TEST_END()                                                                                 \
    std::cerr << "[  PASSED  ] NativeAsmX64." << gCurrentTest << "\n";                             \
    ++gPass;                                                                                       \
    }

namespace fs = std::filesystem;

static std::string outPath(const std::string &name) {
    const fs::path dir{"build/test-out/x64-nativeasm"};
    fs::create_directories(dir);
    return (dir / name).string();
}

static void writeFile(const std::string &path, const std::string &text) {
    std::ofstream ofs(path);
    ofs << text;
}

using Pipeline = viper::codegen::x64::CodegenPipeline;

/// Run the CodegenPipeline with native-asm mode and return the exit code.
static int runNative(const std::string &ilPath,
                     const std::string &exePath = "",
                     bool runExe = false) {
    Pipeline::Options opts;
    opts.input_il_path = ilPath;
    opts.assembler_mode = Pipeline::AssemblerMode::Native;
    if (!exePath.empty())
        opts.output_obj_path = exePath;
    opts.run_native = runExe;

    Pipeline pipeline(opts);
    auto result = pipeline.run();
    return result.exit_code;
}

/// Run the CodegenPipeline with system-asm mode and return the exit code.
static int runSystem(const std::string &ilPath,
                     const std::string &exePath = "",
                     bool runExe = false) {
    Pipeline::Options opts;
    opts.input_il_path = ilPath;
    opts.assembler_mode = Pipeline::AssemblerMode::System;
    if (!exePath.empty())
        opts.output_obj_path = exePath;
    opts.run_native = runExe;

    Pipeline pipeline(opts);
    auto result = pipeline.run();
    return result.exit_code;
}

// ---------------------------------------------------------------------------
// 1. Basic return value — simplest possible native-asm E2E
// ---------------------------------------------------------------------------
TEST_BEGIN(BasicReturn0)
const std::string in = outPath("nasm_ret0.il");
writeFile(in,
          "il 0.1\n"
          "func @main() -> i64 {\n"
          "entry:\n"
          "  ret 0\n"
          "}\n");
CHECK(runNative(in, "", true) == 0);
fs::remove(in);
TEST_END()

TEST_BEGIN(BasicReturn42)
const std::string in = outPath("nasm_ret42.il");
writeFile(in,
          "il 0.1\n"
          "func @main() -> i64 {\n"
          "entry:\n"
          "  ret 42\n"
          "}\n");
CHECK(runNative(in, "", true) == 42);
fs::remove(in);
TEST_END()

// ---------------------------------------------------------------------------
// 2. Multi-function — cross-function call
// ---------------------------------------------------------------------------
TEST_BEGIN(MultiFunctionCall)
const std::string in = outPath("nasm_multifunc.il");
writeFile(in,
          "il 0.1\n"
          "func @add(%a:i64, %b:i64) -> i64 {\n"
          "entry(%a:i64, %b:i64):\n"
          "  %r = add %a, %b\n"
          "  ret %r\n"
          "}\n"
          "func @main() -> i64 {\n"
          "entry:\n"
          "  %v = call @add(17, 25)\n"
          "  ret %v\n"
          "}\n");
CHECK(runNative(in, "", true) == 42);
fs::remove(in);
TEST_END()

// ---------------------------------------------------------------------------
// 3. Control flow — branches, conditional, block parameters
// ---------------------------------------------------------------------------
TEST_BEGIN(ControlFlowBranch)
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
CHECK(runNative(in, "", true) == 7);
fs::remove(in);
TEST_END()

// ---------------------------------------------------------------------------
// 4. .o output only — verify object file creation
// ---------------------------------------------------------------------------
TEST_BEGIN(DotOOutput)
const std::string in = outPath("nasm_dot_o.il");
const std::string obj = outPath("nasm_dot_o.o");
writeFile(in,
          "il 0.1\n"
          "func @main() -> i64 {\n"
          "entry:\n"
          "  ret 0\n"
          "}\n");
CHECK(runNative(in, obj) == 0);
CHECK(fs::exists(obj));
// Verify it's a valid object file by checking magic bytes.
std::ifstream f(obj, std::ios::binary);
ASSERT(static_cast<bool>(f));
uint8_t magic[4]{};
f.read(reinterpret_cast<char *>(magic), 4);
ASSERT(static_cast<bool>(f));
#if defined(__APPLE__)
// Mach-O: 0xFEEDFACF (64-bit little-endian)
CHECK(magic[0] == 0xCF);
CHECK(magic[1] == 0xFA);
CHECK(magic[2] == 0xED);
CHECK(magic[3] == 0xFE);
#elif defined(_WIN32)
// COFF: Machine field at offset 0 should be 0x8664 (AMD64)
CHECK(magic[0] == 0x64);
CHECK(magic[1] == 0x86);
#else
// ELF: 0x7F 'E' 'L' 'F'
CHECK(magic[0] == 0x7F);
CHECK(magic[1] == 'E');
CHECK(magic[2] == 'L');
CHECK(magic[3] == 'F');
#endif
fs::remove(obj);
fs::remove(in);
TEST_END()

// ---------------------------------------------------------------------------
// 5. Dual-path equivalence — native-asm exit code matches system-asm
// ---------------------------------------------------------------------------
TEST_BEGIN(EquivBasicReturn)
const std::string in = outPath("nasm_equiv.il");
writeFile(in,
          "il 0.1\n"
          "func @add(%a:i64, %b:i64) -> i64 {\n"
          "entry(%a:i64, %b:i64):\n"
          "  %r = add %a, %b\n"
          "  ret %r\n"
          "}\n"
          "func @main() -> i64 {\n"
          "entry:\n"
          "  %v = call @add(10, 7)\n"
          "  ret %v\n"
          "}\n");
const int sysRc = runSystem(in, "", true);
const int natRc = runNative(in, "", true);
CHECK(sysRc == natRc);
CHECK(natRc == 17);
fs::remove(in);
TEST_END()

// ---------------------------------------------------------------------------
// 6. Overflow arithmetic
// ---------------------------------------------------------------------------
TEST_BEGIN(OverflowArithmetic)
const std::string in = outPath("nasm_ovf.il");
writeFile(in,
          "il 0.2.0\n"
          "func @main() -> i64 {\n"
          "entry:\n"
          "  %a = iadd.ovf 20, 22\n"
          "  ret %a\n"
          "}\n");
CHECK(runNative(in, "", true) == 42);
fs::remove(in);
TEST_END()

// ---------------------------------------------------------------------------
// 7. Floating-point
// ---------------------------------------------------------------------------
TEST_BEGIN(FloatingPoint)
const std::string in = outPath("nasm_fp.il");
writeFile(in,
          "il 0.2.0\n"
          "func @main() -> i64 {\n"
          "entry:\n"
          "  %a = const.f64 3.14\n"
          "  %b = const.f64 2.86\n"
          "  %c = fadd %a, %b\n"
          "  %r = fptosi %c\n"
          "  ret %r\n"
          "}\n");
CHECK(runNative(in, "", true) == 6);
fs::remove(in);
TEST_END()

int main() {
    test_BasicReturn0();
    test_BasicReturn42();
    test_MultiFunctionCall();
    test_ControlFlowBranch();
    test_DotOOutput();
    test_EquivBasicReturn();
    test_OverflowArithmetic();
    test_FloatingPoint();

    std::cerr << "\n" << gPass << " passed, " << gFail << " failed.\n";
    return gFail > 0 ? 1 : 0;
}
