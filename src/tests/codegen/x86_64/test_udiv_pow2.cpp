//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_udiv_pow2.cpp
// Purpose: Verify unsigned division by power-of-2 is strength-reduced to SHR,
//          and unsigned remainder by power-of-2 is strength-reduced to AND.
// Links: src/codegen/x86_64/LowerDiv.cpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

namespace viper::codegen::x64 {
namespace {
[[nodiscard]] ILValue makeI64Param(int id) noexcept {
    ILValue value{};
    value.kind = ILValue::Kind::I64;
    value.id = id;
    return value;
}

[[nodiscard]] ILValue makeI64Imm(int64_t val) noexcept {
    ILValue value{};
    value.kind = ILValue::Kind::I64;
    value.id = -1; // -1 means immediate
    value.i64 = val;
    return value;
}

[[nodiscard]] ILValue makeValueRef(int id) noexcept {
    ILValue ref{};
    ref.kind = ILValue::Kind::I64;
    ref.id = id;
    return ref;
}

/// Build a module with: udiv %0, 8 (unsigned divide parameter by constant 8).
[[nodiscard]] ILModule makeUDivPow2Module() {
    ILValue dividend = makeI64Param(0);

    ILInstr divInstr{};
    divInstr.opcode = "udiv";
    divInstr.resultId = 1;
    divInstr.resultKind = ILValue::Kind::I64;
    divInstr.ops = {dividend, makeI64Imm(8)};

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    retInstr.ops = {makeValueRef(1)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {dividend.id};
    entry.paramKinds = {dividend.kind};
    entry.instrs = {divInstr, retInstr};

    ILFunction func{};
    func.name = "udiv_pow2";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

/// Build a module with: urem %0, 16 (unsigned remainder by constant 16).
[[nodiscard]] ILModule makeURemPow2Module() {
    ILValue dividend = makeI64Param(0);

    ILInstr remInstr{};
    remInstr.opcode = "urem";
    remInstr.resultId = 1;
    remInstr.resultKind = ILValue::Kind::I64;
    remInstr.ops = {dividend, makeI64Imm(16)};

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    retInstr.ops = {makeValueRef(1)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {dividend.id};
    entry.paramKinds = {dividend.kind};
    entry.instrs = {remInstr, retInstr};

    ILFunction func{};
    func.name = "urem_pow2";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

[[nodiscard]] ILModule makeUnsignedDivModule(std::string opcode, int64_t divisor, std::string name) {
    ILValue dividend = makeI64Param(0);

    ILInstr divInstr{};
    divInstr.opcode = std::move(opcode);
    divInstr.resultId = 1;
    divInstr.resultKind = ILValue::Kind::I64;
    divInstr.ops = {dividend, makeI64Imm(divisor)};

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    retInstr.ops = {makeValueRef(1)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {dividend.id};
    entry.paramKinds = {dividend.kind};
    entry.instrs = {divInstr, retInstr};

    ILFunction func{};
    func.name = std::move(name);
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

} // namespace
} // namespace viper::codegen::x64

int main() {
    using namespace viper::codegen::x64;

    // Test 1: udiv by power-of-2 should use SHR instead of IDIV
    {
        const ILModule module = makeUDivPow2Module();
        const CodegenResult result = emitModuleToAssembly(module, {});

        if (!result.errors.empty()) {
            std::cerr << "udiv_pow2 codegen error: " << result.errors;
            return EXIT_FAILURE;
        }

        // After strength reduction, we expect shrq $3 instead of divq
        const bool hasShr = result.asmText.find("shrq") != std::string::npos ||
                            result.asmText.find("shr") != std::string::npos;
        const bool hasDiv = result.asmText.find("divq") != std::string::npos;

        if (hasShr && !hasDiv) {
            std::cout << "PASS: udiv by pow2 uses SHR\n";
        } else {
            std::cerr << "Expected udiv by pow2 to use SHR without divq:\n" << result.asmText;
            return EXIT_FAILURE;
        }
    }

    // Test 2: urem by power-of-2 should use AND instead of IDIV
    {
        const ILModule module = makeURemPow2Module();
        const CodegenResult result = emitModuleToAssembly(module, {});

        if (!result.errors.empty()) {
            std::cerr << "urem_pow2 codegen error: " << result.errors;
            return EXIT_FAILURE;
        }

        const bool hasAnd = result.asmText.find("andq") != std::string::npos ||
                            result.asmText.find("and") != std::string::npos;
        const bool hasDiv = result.asmText.find("divq") != std::string::npos;

        if (hasAnd && !hasDiv) {
            std::cout << "PASS: urem by pow2 uses AND\n";
        } else {
            std::cerr << "Expected urem by pow2 to use AND without divq:\n" << result.asmText;
            return EXIT_FAILURE;
        }
    }

    // Test 3: plain udiv with a proven non-zero non-power-of-two divisor must not keep a trap.
    {
        const ILModule module = makeUnsignedDivModule("udiv", 10, "udiv_plain_nonpow2");
        const CodegenResult result = emitModuleToAssembly(module, {});

        if (!result.errors.empty()) {
            std::cerr << "udiv plain codegen error: " << result.errors;
            return EXIT_FAILURE;
        }

        if (result.asmText.find("rt_trap_div0") != std::string::npos) {
            std::cerr << "Plain udiv should not emit a div0 trap:\n" << result.asmText;
            return EXIT_FAILURE;
        }
        if (result.asmText.find("divq") == std::string::npos) {
            std::cerr << "Plain non-pow2 udiv should still lower to divq:\n" << result.asmText;
            return EXIT_FAILURE;
        }
    }

    // Test 4: checked udiv with the same divisor still keeps the runtime guard.
    {
        const ILModule module = makeUnsignedDivModule("udiv.chk0", 10, "udiv_checked_nonpow2");
        const CodegenResult result = emitModuleToAssembly(module, {});

        if (!result.errors.empty()) {
            std::cerr << "udiv checked codegen error: " << result.errors;
            return EXIT_FAILURE;
        }

        if (result.asmText.find("rt_trap_div0") == std::string::npos ||
            result.asmText.find("divq") == std::string::npos) {
            std::cerr << "Checked non-pow2 udiv should emit divq with div0 trap:\n"
                      << result.asmText;
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
