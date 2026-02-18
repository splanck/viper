//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_aarch64_linux_abi.cpp
// Purpose: Verify the Linux ELF ABI output mode for the AArch64 backend (3H).
//
// Background:
//   The AArch64 code generator previously only supported Darwin (macOS) output:
//   - Global symbols are prefixed with an underscore (_func)
//   - No ELF-specific directives (.type, .size)
//
//   Priority 3H extends the AsmEmitter to support Linux ELF output:
//   - Global symbols have NO underscore prefix (func, not _func)
//   - .type sym, @function emitted before the symbol definition
//   - .size sym, .-sym emitted after the function body
//
//   The ABI register convention is identical between Darwin AArch64 and Linux
//   AArch64 (both follow AAPCS64), so only assembly syntax changes.
//
// Tests:
//   1. LinuxSymbolNoUnderscore  — function label does not have '_' prefix
//   2. LinuxTypeDirective       — .type func, @function emitted before label
//   3. LinuxSizeDirective       — .size func, .-func emitted after body
//   4. DarwinRegressionPrefix   — Darwin output still uses '_' prefix (no regression)
//   5. LinuxCallSite            — bl calls also use un-prefixed symbols on Linux
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"
#include <sstream>
#include <string>

#include "codegen/aarch64/TargetAArch64.hpp"
#include "codegen/aarch64/passes/EmitPass.hpp"
#include "codegen/aarch64/passes/LoweringPass.hpp"
#include "codegen/aarch64/passes/PassManager.hpp"
#include "codegen/aarch64/passes/PeepholePass.hpp"
#include "codegen/aarch64/passes/RegAllocPass.hpp"
#include "il/io/Parser.hpp"

using namespace viper::codegen::aarch64;
using namespace viper::codegen::aarch64::passes;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

static il::core::Module parseIL(const std::string &src)
{
    std::istringstream ss(src);
    il::core::Module mod;
    if (!il::io::Parser::parse(ss, mod))
        return {};
    return mod;
}

/// Build the standard emit pipeline for the given target.
static std::string compileToAsm(const std::string &il, const TargetInfo &ti)
{
    il::core::Module mod = parseIL(il);
    if (mod.functions.empty())
        return {};
    AArch64Module m;
    m.ilMod = &mod;
    m.ti = &ti;
    PassManager pm;
    pm.addPass(std::make_unique<LoweringPass>());
    pm.addPass(std::make_unique<RegAllocPass>());
    pm.addPass(std::make_unique<PeepholePass>());
    pm.addPass(std::make_unique<EmitPass>());
    Diagnostics diags;
    pm.run(m, diags);
    return m.assembly;
}

// A minimal function to compile for output inspection.
const char *kSimpleIL = "il 0.1\n"
                        "func @hello_linux() -> i64 {\n"
                        "entry:\n"
                        "  ret 42\n"
                        "}\n";

} // namespace

// ---------------------------------------------------------------------------
// Test 1: Linux output must NOT have underscore prefix on function label.
// ---------------------------------------------------------------------------
TEST(AArch64LinuxABI, LinuxSymbolNoUnderscore)
{
    const std::string asm_ = compileToAsm(kSimpleIL, linuxTarget());
    EXPECT_FALSE(asm_.empty());

    // Must contain the unmangled function name.
    EXPECT_NE(asm_.find("hello_linux"), std::string::npos);

    // Must NOT contain '_hello_linux' (the Darwin-mangled name).
    if (asm_.find("_hello_linux") != std::string::npos)
    {
        std::cerr << "Assembly contains Darwin-style underscore prefix on Linux target.\n"
                  << "Assembly:\n"
                  << asm_ << "\n";
        EXPECT_TRUE(false);
    }
}

// ---------------------------------------------------------------------------
// Test 2: Linux output must have .type sym, @function directive.
// ---------------------------------------------------------------------------
TEST(AArch64LinuxABI, LinuxTypeDirective)
{
    const std::string asm_ = compileToAsm(kSimpleIL, linuxTarget());
    EXPECT_FALSE(asm_.empty());

    // ELF requires .type to mark symbol as a function for the linker.
    const bool hasType = asm_.find(".type hello_linux, @function") != std::string::npos;
    if (!hasType)
    {
        std::cerr << "Missing '.type hello_linux, @function' directive.\n"
                  << "Assembly:\n"
                  << asm_ << "\n";
    }
    EXPECT_TRUE(hasType);
}

// ---------------------------------------------------------------------------
// Test 3: Linux output must have .size sym, .-sym directive after body.
// ---------------------------------------------------------------------------
TEST(AArch64LinuxABI, LinuxSizeDirective)
{
    const std::string asm_ = compileToAsm(kSimpleIL, linuxTarget());
    EXPECT_FALSE(asm_.empty());

    // ELF requires .size for debuggers and profilers to know function extents.
    const bool hasSize = asm_.find(".size hello_linux, .-hello_linux") != std::string::npos;
    if (!hasSize)
    {
        std::cerr << "Missing '.size hello_linux, .-hello_linux' directive.\n"
                  << "Assembly:\n"
                  << asm_ << "\n";
    }
    EXPECT_TRUE(hasSize);
}

// ---------------------------------------------------------------------------
// Test 4: Darwin output must still use '_' prefix (regression guard).
// ---------------------------------------------------------------------------
TEST(AArch64LinuxABI, DarwinRegressionPrefix)
{
    const std::string asm_ = compileToAsm(kSimpleIL, darwinTarget());
    EXPECT_FALSE(asm_.empty());

    // Darwin: symbol must be prefixed with '_'.
    const bool hasPrefixed = asm_.find("_hello_linux") != std::string::npos;
    if (!hasPrefixed)
    {
        std::cerr << "Darwin assembly is missing '_hello_linux' prefix.\n"
                  << "Assembly:\n"
                  << asm_ << "\n";
    }
    EXPECT_TRUE(hasPrefixed);

    // Darwin must NOT have .type / .size directives.
    EXPECT_TRUE(asm_.find(".type") == std::string::npos);
    EXPECT_TRUE(asm_.find(".size") == std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 5: bl call sites on Linux must not have underscore prefix.
// ---------------------------------------------------------------------------
//
// A function that calls another function should emit 'bl callee' (no '_')
// when using the Linux target.
//
TEST(AArch64LinuxABI, LinuxCallSite)
{
    const std::string il = "il 0.1\n"
                           "func @callee() -> i64 {\n"
                           "entry:\n"
                           "  ret 1\n"
                           "}\n"
                           "func @caller() -> i64 {\n"
                           "entry:\n"
                           "  %r = call @callee()\n"
                           "  ret %r\n"
                           "}\n";

    const std::string asm_ = compileToAsm(il, linuxTarget());
    EXPECT_FALSE(asm_.empty());

    // The bl instruction for @callee must use the unmangled name.
    const bool hasBlCallee = asm_.find("bl callee") != std::string::npos;
    if (!hasBlCallee)
    {
        std::cerr << "Expected 'bl callee' (no underscore) in Linux assembly.\n"
                  << "Assembly:\n"
                  << asm_ << "\n";
    }
    EXPECT_TRUE(hasBlCallee);

    // Must NOT have 'bl _callee' (Darwin mangling).
    EXPECT_TRUE(asm_.find("bl _callee") == std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
