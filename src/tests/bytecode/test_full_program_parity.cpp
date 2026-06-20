//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/bytecode/test_full_program_parity.cpp
// Purpose: Differentially compare full-program IL VM and bytecode VM behavior.
// Key invariants:
//   - The tree-walking VM, bytecode switch dispatch, and bytecode threaded
//     dispatch must agree on return value, stdout, and trap kind.
//   - TrapKind values 0-11 remain aligned with il::vm::TrapKind.
// Ownership/Lifetime: Loads checked-in IL corpus files and executes them within
//                     the current process except for IL VM trap cases, which are
//                     isolated because the IL VM terminates on unhandled traps.
// Links: docs/BYTECODE_VM_DESIGN.md, docs/testing.md
//
//===----------------------------------------------------------------------===//

#include "bytecode/BytecodeCompiler.hpp"
#include "bytecode/BytecodeModule.hpp"
#include "bytecode/BytecodeVM.hpp"
#include "common/ProcessIsolation.hpp"
#include "il/core/Module.hpp"
#include "il/io/Parser.hpp"
#include "il/verify/Verifier.hpp"
#include "runtime/core/rt_output.h"
#include "runtime/rt.hpp"
#include "vm/Trap.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct SuccessCase {
    const char *file;
    int64_t expectedValue;
    const char *expectedOutput;
};

struct TrapCase {
    const char *file;
    viper::bytecode::TrapKind bytecodeKind;
    const char *ilDiagnosticName;
};

struct RunResult {
    bool trapped = false;
    int64_t value = 0;
    std::string output;
    viper::bytecode::TrapKind trapKind = viper::bytecode::TrapKind::None;
    std::string trapMessage;
};

class ScopedRuntimeOutputCapture {
  public:
    ScopedRuntimeOutputCapture() : previous_(rt_output_set_capture_hook(capture, this)) {}

    ScopedRuntimeOutputCapture(const ScopedRuntimeOutputCapture &) = delete;
    ScopedRuntimeOutputCapture &operator=(const ScopedRuntimeOutputCapture &) = delete;

    ~ScopedRuntimeOutputCapture() {
        rt_output_set_capture_hook(previous_.fn, previous_.ctx);
    }

    [[nodiscard]] const std::string &output() const noexcept {
        return output_;
    }

  private:
    static void capture(const char *data, size_t len, void *ctx) noexcept {
        if (!ctx || !data || len == 0)
            return;
        static_cast<ScopedRuntimeOutputCapture *>(ctx)->output_.append(data, len);
    }

    rt_output_capture_hook previous_{};
    std::string output_;
};

[[nodiscard]] fs::path corpusRoot() {
    return fs::path(VIPER_SHARED_IL_CORPUS_DIR);
}

[[nodiscard]] il::core::Module loadModule(const fs::path &path) {
    std::ifstream input(path);
    if (!input) {
        std::cerr << "cannot open corpus IL: " << path << '\n';
        assert(false && "missing corpus IL file");
    }

    il::core::Module module;
    auto parsed = il::io::Parser::parse(input, module);
    if (!parsed) {
        std::cerr << "parse failed for " << path << ": " << parsed.error().message << '\n';
        assert(false && "shared corpus IL parse failed");
    }

    auto verified = il::verify::Verifier::verify(module);
    if (!verified) {
        std::cerr << "verify failed for " << path << ": " << verified.error().message << '\n';
        assert(false && "shared corpus IL verify failed");
    }
    return module;
}

[[nodiscard]] RunResult runIlVm(il::core::Module module) {
    ScopedRuntimeOutputCapture capture;
    il::vm::VM vm(module);
    const int64_t value = vm.run();
    rt_output_flush();

    RunResult result;
    result.value = value;
    result.output = capture.output();
    return result;
}

void registerBytecodeTerminalHandlers(viper::bytecode::BytecodeVM &vm) {
    vm.setRuntimeBridgeEnabled(false);
    vm.registerNativeHandler(
        "Viper.Terminal.PrintI64",
        [](viper::bytecode::BCSlot *args, uint32_t argc, viper::bytecode::BCSlot *) {
            assert(argc == 1);
            rt_print_i64(args[0].i64);
        });
    vm.registerNativeHandler(
        "Viper.Terminal.PrintStr",
        [](viper::bytecode::BCSlot *args, uint32_t argc, viper::bytecode::BCSlot *) {
            assert(argc == 1);
            rt_print_str(static_cast<rt_string>(args[0].ptr));
        });
}

[[nodiscard]] RunResult runBytecodeVm(const il::core::Module &module,
                                      bool threaded,
                                      uint64_t maxInstructions = 0) {
    viper::bytecode::BytecodeCompiler compiler;
    auto compiled = compiler.compileChecked(module);
    if (!compiled) {
        std::cerr << "bytecode compile failed: " << compiled.error().message << '\n';
        assert(false && "bytecode compile failed for shared corpus");
    }

    viper::bytecode::BytecodeModule bytecode = std::move(compiled.value());
    viper::bytecode::BytecodeVM vm;
    vm.setThreadedDispatch(threaded);
    vm.setMaxInstructions(maxInstructions);
    registerBytecodeTerminalHandlers(vm);
    vm.load(&bytecode);

    ScopedRuntimeOutputCapture capture;
    const viper::bytecode::BCSlot slot = vm.exec("main", {});
    rt_output_flush();

    RunResult result;
    result.trapped = vm.state() == viper::bytecode::VMState::Trapped;
    result.value = slot.i64;
    result.output = capture.output();
    result.trapKind = vm.trapKind();
    result.trapMessage = vm.trapMessage();
    return result;
}

[[nodiscard]] bool sameObservableResult(const RunResult &lhs, const RunResult &rhs) {
    if (lhs.trapped != rhs.trapped || lhs.value != rhs.value || lhs.output != rhs.output)
        return false;
    return !lhs.trapped || lhs.trapKind == rhs.trapKind;
}

void requireSuccessMatches(const SuccessCase &testCase) {
    const fs::path path = corpusRoot() / "success" / testCase.file;
    il::core::Module module = loadModule(path);
    const RunResult ilResult = runIlVm(module);

    if (ilResult.trapped || ilResult.value != testCase.expectedValue ||
        ilResult.output != testCase.expectedOutput) {
        std::cerr << "IL VM baseline mismatch for " << testCase.file << "\n"
                  << "  value=" << ilResult.value << " output='" << ilResult.output << "'\n";
        assert(false && "IL VM baseline did not match expected corpus metadata");
    }

    for (bool threaded : {false, true}) {
        const RunResult bcResult = runBytecodeVm(module, threaded);
        if (!sameObservableResult(ilResult, bcResult)) {
            std::cerr << "bytecode parity mismatch for " << testCase.file
                      << " threaded=" << threaded << "\n"
                      << "  il: trapped=" << ilResult.trapped << " value=" << ilResult.value
                      << " output='" << ilResult.output << "'\n"
                      << "  bc: trapped=" << bcResult.trapped << " value=" << bcResult.value
                      << " output='" << bcResult.output
                      << "' trap=" << static_cast<int>(bcResult.trapKind) << " "
                      << bcResult.trapMessage << '\n';
            assert(false && "bytecode full-program parity mismatch");
        }
    }
}

void requireTrapMatches(const TrapCase &testCase) {
    const fs::path path = corpusRoot() / "traps" / testCase.file;
    il::core::Module module = loadModule(path);

    il::core::Module ilTrapModule = module;
    const bool usesPendingInterrupt =
        testCase.bytecodeKind == viper::bytecode::TrapKind::Interrupt;
    const viper::tests::ChildResult ilTrap =
        usesPendingInterrupt
            ? viper::tests::runModuleWithPendingInterruptIsolated(ilTrapModule)
            : viper::tests::runModuleIsolated(ilTrapModule);
    if (!ilTrap.trapped()) {
        std::cerr << "IL VM did not trap for " << testCase.file << '\n';
        assert(false && "IL VM trap corpus case did not trap");
    }
    if (ilTrap.stderrText.find(testCase.ilDiagnosticName) == std::string::npos) {
        std::cerr << "IL VM trap name mismatch for " << testCase.file << "\n"
                  << "  expected fragment: " << testCase.ilDiagnosticName << "\n"
                  << "  stderr: " << ilTrap.stderrText << '\n';
        assert(false && "IL VM trap corpus case produced wrong diagnostic kind");
    }

    for (bool threaded : {false, true}) {
        const RunResult bcResult =
            runBytecodeVm(module, threaded, usesPendingInterrupt ? 16U : 0U);
        if (!bcResult.trapped || bcResult.trapKind != testCase.bytecodeKind) {
            std::cerr << "bytecode trap mismatch for " << testCase.file
                      << " threaded=" << threaded << "\n"
                      << "  expected=" << static_cast<int>(testCase.bytecodeKind)
                      << " actual=" << static_cast<int>(bcResult.trapKind) << "\n"
                      << "  message=" << bcResult.trapMessage << '\n';
            assert(false && "bytecode trap kind mismatch");
        }
    }
}

void testTrapKindAlignment() {
    using BC = viper::bytecode::TrapKind;
    using IL = il::vm::TrapKind;

    static_assert(static_cast<int32_t>(IL::DivideByZero) ==
                  static_cast<int32_t>(BC::DivideByZero));
    static_assert(static_cast<int32_t>(IL::Overflow) == static_cast<int32_t>(BC::Overflow));
    static_assert(static_cast<int32_t>(IL::InvalidCast) ==
                  static_cast<int32_t>(BC::InvalidCast));
    static_assert(static_cast<int32_t>(IL::DomainError) ==
                  static_cast<int32_t>(BC::DomainError));
    static_assert(static_cast<int32_t>(IL::Bounds) == static_cast<int32_t>(BC::Bounds));
    static_assert(static_cast<int32_t>(IL::FileNotFound) ==
                  static_cast<int32_t>(BC::FileNotFound));
    static_assert(static_cast<int32_t>(il::vm::trapKindFromValue(6)) ==
                  static_cast<int32_t>(BC::EndOfFile));
    static_assert(static_cast<int32_t>(IL::IOError) == static_cast<int32_t>(BC::IOError));
    static_assert(static_cast<int32_t>(IL::InvalidOperation) ==
                  static_cast<int32_t>(BC::InvalidOperation));
    static_assert(static_cast<int32_t>(IL::RuntimeError) ==
                  static_cast<int32_t>(BC::RuntimeError));
    static_assert(static_cast<int32_t>(IL::Interrupt) == static_cast<int32_t>(BC::Interrupt));
    static_assert(static_cast<int32_t>(IL::NetworkError) ==
                  static_cast<int32_t>(BC::NetworkError));
    static_assert(static_cast<int32_t>(BC::NullPointer) >= 100);
    static_assert(static_cast<int32_t>(BC::StackOverflow) >= 100);
    static_assert(static_cast<int32_t>(BC::InvalidOpcode) >= 100);
}

void testGateRejectsDivergentResults() {
    RunResult baseline;
    baseline.value = 7;
    baseline.output = "ok";

    RunResult divergent = baseline;
    divergent.output = "bad";
    assert(!sameObservableResult(baseline, divergent) && "parity gate missed output mismatch");

    divergent = baseline;
    divergent.value = 8;
    assert(!sameObservableResult(baseline, divergent) && "parity gate missed return mismatch");

    divergent = baseline;
    divergent.trapped = true;
    divergent.trapKind = viper::bytecode::TrapKind::RuntimeError;
    assert(!sameObservableResult(baseline, divergent) && "parity gate missed trap mismatch");
}

const std::vector<SuccessCase> &successCases() {
    static const std::vector<SuccessCase> cases = {
        {"ret_i64.il", 42, ""},
        {"arith_chain.il", 6, ""},
        {"block_params_sum.il", 55, ""},
        {"conditional_abs.il", 7, ""},
        {"recursive_factorial.il", 720, ""},
        {"switch_i32.il", 30, ""},
        {"alloca_store_load.il", 77, ""},
        {"global_scalar_load_store.il", 58, ""},
        {"gep_stack.il", 99, ""},
        {"bitwise_shift.il", 95, ""},
        {"unsigned_div_rem.il", 12, ""},
        {"signed_div_rem.il", -6, ""},
        {"fp_cast_round.il", 6, ""},
        {"cast_round_to_even.il", 6, ""},
        {"idxchk_normalize.il", 5, ""},
        {"i1_bool_flow.il", 1, ""},
        {"call_two_args.il", 123, ""},
        {"string_print.il", 0, "shared"},
        {"print_i64.il", 0, "123"},
        {"eh_catch_return.il", 0, ""},
        {"branch_join_value.il", 22, ""},
        {"i16_narrow_store.il", 123, ""},
        {"unsigned_compare.il", 1, ""},
    };
    return cases;
}

const std::vector<TrapCase> &trapCases() {
    using BC = viper::bytecode::TrapKind;
    static const std::vector<TrapCase> cases = {
        {"trap_kind_00_divide_by_zero.il", BC::DivideByZero, "DivideByZero"},
        {"trap_kind_01_overflow.il", BC::Overflow, "Overflow"},
        {"trap_kind_02_invalid_cast.il", BC::InvalidCast, "InvalidCast"},
        {"trap_kind_03_domain_error.il", BC::DomainError, "DomainError"},
        {"trap_kind_04_bounds.il", BC::Bounds, "Bounds"},
        {"trap_kind_05_file_not_found.il", BC::FileNotFound, "FileNotFound"},
        {"trap_kind_06_eof.il", BC::EndOfFile, "EOF"},
        {"trap_kind_07_io_error.il", BC::IOError, "IOError"},
        {"trap_kind_08_invalid_operation.il", BC::InvalidOperation, "InvalidOperation"},
        {"trap_kind_09_runtime_error.il", BC::RuntimeError, "RuntimeError"},
        {"trap_kind_10_interrupt.il", BC::Interrupt, "Interrupt"},
        {"trap_kind_11_network_error.il", BC::NetworkError, "NetworkError"},
    };
    return cases;
}

} // namespace

int main(int argc, char *argv[]) {
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    std::cout << "Running bytecode full-program parity corpus...\n";
    testTrapKindAlignment();
    testGateRejectsDivergentResults();

    for (const SuccessCase &testCase : successCases())
        requireSuccessMatches(testCase);
    for (const TrapCase &testCase : trapCases())
        requireTrapMatches(testCase);

    std::cout << "Bytecode full-program parity corpus PASSED!\n";
    return 0;
}
