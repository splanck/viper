//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/conformance/VMBytecodeScalarSemanticsTests.cpp
// Purpose: Verify that IL VM and bytecode VM scalar instruction semantics match.
// Key invariants: The same IL module must produce the same value or trap kind
//                 under the tree-walking VM, bytecode switch dispatch, and
//                 bytecode threaded dispatch.
// Ownership/Lifetime: Builds ephemeral modules and executes them immediately.
// Links: il/semantics/ScalarOps.hpp, bytecode/BytecodeSemantics.hpp,
//        docs/arithmetic-semantics.md
//
//===----------------------------------------------------------------------===//

#include "bytecode/BytecodeCompiler.hpp"
#include "bytecode/BytecodeModule.hpp"
#include "bytecode/BytecodeVM.hpp"
#include "common/ProcessIsolation.hpp"
#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

using namespace il::core;

namespace {

/// @brief Captured result from executing a module on the bytecode VM.
/// @details Successful runs carry the returned slot value.  Trapping runs carry
///          the bytecode trap kind and formatted diagnostic message so tests can
///          compare trap classes without depending on exact source formatting.
struct BytecodeRunResult {
    bool trapped = false;                                      ///< True when BytecodeVM trapped.
    viper::bytecode::TrapKind trapKind =                      ///< Bytecode trap kind on failure.
        viper::bytecode::TrapKind::None;
    int64_t value = 0;                                         ///< Returned i64 value on success.
    std::string message;                                       ///< Bytecode trap diagnostic text.
};

/// @brief Append a return instruction for the provided value to a basic block.
/// @param block Basic block receiving the terminator.
/// @param value Value returned by the generated `main` function.
void appendReturn(BasicBlock &block, Value value) {
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(std::move(value));
    ret.loc = {1, 1, 1};
    block.instructions.push_back(ret);
}

/// @brief Compile and execute a module on one bytecode dispatch mode.
/// @param module Source IL module to compile.
/// @param threaded True to use computed-goto threaded dispatch when available.
/// @return Captured value or trap details from BytecodeVM.
BytecodeRunResult runBytecode(const Module &module, bool threaded) {
    viper::bytecode::BytecodeCompiler compiler;
    auto compiled = compiler.compileChecked(module, nullptr, true);
    if (!compiled) {
        std::cerr << "bytecode scalar conformance compile failed: "
                  << compiled.error().message << "\n";
        std::abort();
    }

    viper::bytecode::BytecodeModule bytecode = std::move(compiled.value());
    viper::bytecode::BytecodeVM vm;
    vm.setThreadedDispatch(threaded);
    vm.load(&bytecode);
    const viper::bytecode::BCSlot result = vm.exec("main", {});

    BytecodeRunResult captured;
    captured.trapped = vm.state() == viper::bytecode::VMState::Trapped;
    captured.trapKind = vm.trapKind();
    captured.value = result.i64;
    captured.message = vm.trapMessage();
    return captured;
}

/// @brief Execute a module on both bytecode dispatch modes and require success.
/// @param module Source IL module to compile and run.
/// @param expected Expected i64 return value for both bytecode engines.
void expectBytecodeValue(const Module &module, int64_t expected) {
    for (bool threaded : {false, true}) {
        const BytecodeRunResult result = runBytecode(module, threaded);
        if (result.trapped) {
            std::cerr << "BytecodeVM trapped unexpectedly in threaded=" << threaded
                      << ": " << result.message << "\n";
            std::abort();
        }
        if (result.value != expected) {
            std::cerr << "BytecodeVM produced " << result.value
                      << " in threaded=" << threaded << ", expected " << expected
                      << "\n";
            std::abort();
        }
    }
}

/// @brief Execute a module on both bytecode dispatch modes and require a trap.
/// @param module Source IL module to compile and run.
/// @param expected Expected bytecode trap kind in both dispatch engines.
void expectBytecodeTrap(const Module &module, viper::bytecode::TrapKind expected) {
    for (bool threaded : {false, true}) {
        const BytecodeRunResult result = runBytecode(module, threaded);
        if (!result.trapped) {
            std::cerr << "expected bytecode trap in threaded=" << threaded
                      << " but returned " << result.value << "\n";
            std::abort();
        }
        if (result.trapKind != expected) {
            std::cerr << "BytecodeVM trapped with kind "
                      << static_cast<int>(result.trapKind)
                      << " in threaded=" << threaded << ", expected "
                      << static_cast<int>(expected) << ": " << result.message
                      << "\n";
            std::abort();
        }
    }
}

/// @brief Build a `main` module that returns the result of `idx.chk`.
/// @param type Integer width used by the bounds-check instruction.
/// @param idx Index operand.
/// @param lo Inclusive lower bound operand.
/// @param hi Exclusive upper bound operand.
/// @return Module containing `main`.
Module buildIdxChkModule(Type::Kind type, int64_t idx, int64_t lo, int64_t hi) {
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &entry = builder.addBlock(fn, "entry");
    builder.setInsertPoint(entry);

    Instr idxChk;
    idxChk.result = builder.reserveTempId();
    idxChk.op = Opcode::IdxChk;
    idxChk.type = Type(type);
    idxChk.operands = {Value::constInt(idx), Value::constInt(lo), Value::constInt(hi)};
    idxChk.loc = {1, 1, 1};
    entry.instructions.push_back(idxChk);

    appendReturn(entry, Value::temp(*idxChk.result));
    return module;
}

/// @brief Build a two-operand integer module for arithmetic trap tests.
/// @param op Arithmetic opcode to emit.
/// @param type Result and operation width.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @return Module containing `main`.
Module buildBinaryIntegerModule(Opcode op, Type::Kind type, int64_t lhs, int64_t rhs) {
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(type), {});
    auto &entry = builder.addBlock(fn, "entry");
    builder.setInsertPoint(entry);

    Instr instr;
    instr.result = builder.reserveTempId();
    instr.op = op;
    instr.type = Type(type);
    instr.operands = {Value::constInt(lhs), Value::constInt(rhs)};
    instr.loc = {1, 1, 1};
    entry.instructions.push_back(instr);

    appendReturn(entry, Value::temp(*instr.result));
    return module;
}

/// @brief Build a checked f64-to-integer conversion module.
/// @param op Conversion opcode to emit.
/// @param type Integer result width.
/// @param value Floating-point source value.
/// @return Module containing `main`.
Module buildCheckedFpCastModule(Opcode op, Type::Kind type, double value) {
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(type), {});
    auto &entry = builder.addBlock(fn, "entry");
    builder.setInsertPoint(entry);

    Instr instr;
    instr.result = builder.reserveTempId();
    instr.op = op;
    instr.type = Type(type);
    instr.operands = {Value::constFloat(value)};
    instr.loc = {1, 1, 1};
    entry.instructions.push_back(instr);

    appendReturn(entry, Value::temp(*instr.result));
    return module;
}

/// @brief Build a checked signed narrowing conversion module.
/// @param type Integer target width.
/// @param value Source signed value.
/// @return Module containing `main`.
Module buildSignedNarrowModule(Type::Kind type, int64_t value) {
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(type), {});
    auto &entry = builder.addBlock(fn, "entry");
    builder.setInsertPoint(entry);

    Instr instr;
    instr.result = builder.reserveTempId();
    instr.op = Opcode::CastSiNarrowChk;
    instr.type = Type(type);
    instr.operands = {Value::constInt(value)};
    instr.loc = {1, 1, 1};
    entry.instructions.push_back(instr);

    appendReturn(entry, Value::temp(*instr.result));
    return module;
}

/// @brief Require the IL VM to trap with diagnostic text containing a fragment.
/// @param module Source IL module to execute in an isolated child process.
/// @param fragment Substring expected in the captured trap diagnostic.
void expectIlTrapContains(Module &module, const char *fragment) {
    viper::tests::VmFixture fixture;
    const std::string trapText = fixture.captureTrap(module);
    if (trapText.find(fragment) == std::string::npos) {
        std::cerr << "IL VM trap text did not contain '" << fragment
                  << "': " << trapText << "\n";
        std::abort();
    }
}

/// @brief Test that bytecode preserves IL `idx.chk` normalization.
/// @details The known divergence was that bytecode checked the range but kept
///          the raw index.  IL semantics return `idx - lo` on success.
void testIdxChkNormalizesLikeIlVm() {
    Module module = buildIdxChkModule(Type::Kind::I64, 15, 10, 20);
    viper::tests::VmFixture fixture;
    const int64_t ilResult = fixture.run(module);
    if (ilResult != 5) {
        std::cerr << "IL VM should normalize idx.chk to idx - lo, got "
                  << ilResult << "\n";
        std::abort();
    }
    expectBytecodeValue(module, 5);
}

/// @brief Test that I16 checked division overflow traps in every VM.
/// @details This covers bytecode width metadata for `sdiv.chk0`; without the
///          metadata, bytecode performed an I64 division and missed I16 MIN/-1.
void testI16CheckedDivOverflowMatchesIlVm() {
    Module module =
        buildBinaryIntegerModule(Opcode::SDivChk0, Type::Kind::I16, INT64_C(-32768), -1);
    expectIlTrapContains(module, "Overflow");
    expectBytecodeTrap(module, viper::bytecode::TrapKind::Overflow);
}

/// @brief Test that checked f64-to-i16 casts use the target width in bytecode.
/// @details 32768.0 is valid for i64 but overflows i16, so this catches loss of
///          conversion result-width metadata during bytecode lowering.
void testF64ToI16CheckedCastOverflowMatchesIlVm() {
    Module module = buildCheckedFpCastModule(
        Opcode::CastFpToSiRteChk, Type::Kind::I16, 32768.0);
    expectIlTrapContains(module, "Overflow");
    expectBytecodeTrap(module, viper::bytecode::TrapKind::Overflow);
}

/// @brief Test that checked signed narrowing uses InvalidCast in every VM.
/// @details The IL VM classifies out-of-range narrowing as InvalidCast.  This
///          verifies bytecode no longer reports the same semantic failure as
///          integer Overflow.
void testSignedNarrowTrapKindMatchesIlVm() {
    Module module = buildSignedNarrowModule(Type::Kind::I16, 32768);
    expectIlTrapContains(module, "InvalidCast");
    expectBytecodeTrap(module, viper::bytecode::TrapKind::InvalidCast);
}

} // namespace

/// @brief Entry point for VM/bytecode scalar semantic conformance tests.
/// @param argc Process argument count.
/// @param argv Process argument vector; child-mode arguments are consumed by
///             the process isolation helper.
/// @return Zero on success.
int main(int argc, char *argv[]) {
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    std::cout << "Running VM/Bytecode scalar semantic conformance tests...\n";
    testIdxChkNormalizesLikeIlVm();
    testI16CheckedDivOverflowMatchesIlVm();
    testF64ToI16CheckedCastOverflowMatchesIlVm();
    testSignedNarrowTrapKindMatchesIlVm();
    std::cout << "All VM/Bytecode scalar semantic conformance tests PASSED!\n";
    return 0;
}
