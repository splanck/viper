//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_i32_checked_ovf.cpp
// Purpose: Verify 32-bit overflow-checked arithmetic lowers to the native
//          32-bit fast path (op32 + jo + movslq) instead of the
//          widen/compute/narrow/compare sequence.
// Links: src/codegen/x86_64/Lowering.Arith.cpp (emit32BitCheckedOp)
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace zanna::codegen::x64 {
namespace {

[[nodiscard]] ILValue makeI32Param(int id) noexcept {
    ILValue value{};
    value.kind = ILValue::Kind::I64;
    value.id = id;
    return value;
}

[[nodiscard]] ILValue makeImm(int64_t val) noexcept {
    ILValue value{};
    value.kind = ILValue::Kind::I64;
    value.id = -1;
    value.i64 = val;
    return value;
}

/// Build a module computing a sub-width checked op over two parameters.
[[nodiscard]] ILModule makeCheckedModule(std::string opcode,
                                         bool immRhs,
                                         std::uint8_t bits,
                                         std::string name) {
    ILValue lhs = makeI32Param(0);
    ILValue rhs = makeI32Param(1);

    ILInstr op{};
    op.opcode = std::move(opcode);
    op.resultId = 2;
    op.resultKind = ILValue::Kind::I64;
    op.resultBits = bits;
    op.ops = {lhs, immRhs ? makeImm(7) : rhs};

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    ILValue res{};
    res.kind = ILValue::Kind::I64;
    res.id = 2;
    retInstr.ops = {res};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {lhs.id, rhs.id};
    entry.paramKinds = {lhs.kind, rhs.kind};
    entry.instrs = {op, retInstr};

    ILFunction func{};
    func.name = std::move(name);
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

[[nodiscard]] bool contains(const std::string &text, const std::string &needle) {
    return text.find(needle) != std::string::npos;
}

} // namespace
} // namespace zanna::codegen::x64

int main() {
    using namespace zanna::codegen::x64;

    struct Case {
        const char *opcode;
        bool immRhs;
        std::uint8_t bits;
        const char *mnemonic;
        const char *movsx;
    };
    const Case cases[] = {
        {"iadd.ovf", false, 32, "addl", "movslq"},
        {"iadd.ovf", true, 32, "addl $7,", "movslq"},
        {"isub.ovf", false, 32, "subl", "movslq"},
        {"imul.ovf", false, 32, "imull", "movslq"},
        {"iadd.ovf", false, 16, "addw", "movswq"},
        {"iadd.ovf", true, 16, "addw $7,", "movswq"},
        {"isub.ovf", false, 16, "subw", "movswq"},
        {"imul.ovf", false, 16, "imulw", "movswq"},
    };

    for (const auto &c : cases) {
        const ILModule module = makeCheckedModule(c.opcode,
                                                  c.immRhs,
                                                  c.bits,
                                                  std::string("chk_") + std::to_string(c.bits) +
                                                      "_" + c.mnemonic[0] +
                                                      (c.immRhs ? "_imm" : "_reg"));
        const CodegenResult result = emitModuleToAssembly(module, {});
        if (!result.errors.empty()) {
            std::cerr << c.opcode << " codegen error: " << result.errors;
            return EXIT_FAILURE;
        }

        // Fast path: narrow op + jo + sign-extending re-widen.
        if (!contains(result.asmText, c.mnemonic) || !contains(result.asmText, "jo ") ||
            !contains(result.asmText, c.movsx)) {
            std::cerr << c.opcode << " (" << int(c.bits) << "-bit): expected fast path ("
                      << c.mnemonic << " + jo + " << c.movsx << "):\n"
                      << result.asmText;
            return EXIT_FAILURE;
        }

        // The widen/narrow shift dance must be gone.
        if (contains(result.asmText, "shlq $32") || contains(result.asmText, "sarq $32") ||
            contains(result.asmText, "shlq $48") || contains(result.asmText, "sarq $48")) {
            std::cerr << c.opcode << " (" << int(c.bits)
                      << "-bit): slow-path shift sequence still present:\n"
                      << result.asmText;
            return EXIT_FAILURE;
        }
    }

    std::cout << "PASS: sub-width checked ops use the narrow fast path\n";
    return EXIT_SUCCESS;
}
