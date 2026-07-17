//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_div_trap.cpp
// Purpose: Verify that signed 64-bit division emits a guarded trap sequence
// Key invariants: Generated assembly must test the divisor for zero, branch to
// Ownership/Lifetime: The test builds an IL module locally, requests assembly
// Links: src/codegen/x86_64/LowerDiv.cpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"
#include "common/CodegenFixture.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#ifndef _WIN32
#include "tests/common/WaitCompat.hpp"
#else
#include <process.h>
#endif

namespace zanna::codegen::x64 {
namespace {
#ifndef ZANNA_ILC_PATH
#define ZANNA_ILC_PATH "ilc"
#endif

[[nodiscard]] ILValue makeParam(int id) noexcept {
    ILValue value{};
    value.kind = ILValue::Kind::I64;
    value.id = id;
    return value;
}

[[nodiscard]] ILModule makeDivModule() {
    ILValue dividend = makeParam(0);
    ILValue divisor = makeParam(1);

    ILInstr divInstr{};
    divInstr.opcode = "div";
    divInstr.ops = {dividend, divisor};
    divInstr.resultId = 2;
    divInstr.resultKind = ILValue::Kind::I64;

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    ILValue quotient{};
    quotient.kind = ILValue::Kind::I64;
    quotient.id = divInstr.resultId;
    retInstr.ops = {quotient};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {dividend.id, divisor.id};
    entry.paramKinds = {dividend.kind, divisor.kind};
    entry.instrs = {divInstr, retInstr};

    ILFunction func{};
    func.name = "div_guard";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

[[nodiscard]] bool isSelfTest(const std::string &line) {
    const auto testPos = line.find("testq");
    if (testPos == std::string::npos) {
        return false;
    }

    const auto firstPercent = line.find('%', testPos);
    if (firstPercent == std::string::npos) {
        return false;
    }
    const auto commaPos = line.find(',', firstPercent);
    if (commaPos == std::string::npos) {
        return false;
    }

    const std::string firstReg = line.substr(firstPercent, commaPos - firstPercent);

    const auto secondPercent = line.find('%', commaPos);
    if (secondPercent == std::string::npos) {
        return false;
    }
    const auto secondEnd = line.find_first_of(", \t", secondPercent);
    const std::string secondReg = secondEnd == std::string::npos
                                      ? line.substr(secondPercent)
                                      : line.substr(secondPercent, secondEnd - secondPercent);

    return firstReg == secondReg;
}

struct DivTrapSequence {
    bool hasSelfTest{false};
    bool hasTrapBranch{false};
    bool hasCqto{false};
    bool hasIdiv{false};
    bool hasIdivScratch{false};
    bool hasScratchDivisorMove{false};
    bool hasTrapCall{false};
};

[[nodiscard]] DivTrapSequence analyseDivTrapSequence(const std::string &asmText) {
    DivTrapSequence sequence{};
    std::istringstream stream{asmText};
    std::string line{};
    while (std::getline(stream, line)) {
        if (!sequence.hasSelfTest && isSelfTest(line)) {
            sequence.hasSelfTest = true;
        }
        if (!sequence.hasTrapBranch && line.find("je ") != std::string::npos &&
            line.find(".Ltrap_div0") != std::string::npos) {
            sequence.hasTrapBranch = true;
        }
        if (!sequence.hasCqto && line.find("cqto") != std::string::npos) {
            sequence.hasCqto = true;
        }
        if (!sequence.hasIdiv && line.find("idivq") != std::string::npos) {
            sequence.hasIdiv = true;
        }
        if (!sequence.hasIdivScratch && line.find("idivq %r10") != std::string::npos) {
            sequence.hasIdivScratch = true;
        }
        if (!sequence.hasScratchDivisorMove && line.find("movq") != std::string::npos &&
            line.find("%r10") != std::string::npos) {
            sequence.hasScratchDivisorMove = true;
        }
        if (!sequence.hasTrapCall && line.find("callq") != std::string::npos &&
            line.find("rt_trap_div0") != std::string::npos) {
            sequence.hasTrapCall = true;
        }
    }
    return sequence;
}

[[nodiscard]] bool envFlagEnabled(const char *name) noexcept {
    if (const char *value = std::getenv(name)) {
        std::string_view view(value);
        if (view.empty()) {
            return true;
        }
        if (view == "0" || view == "false" || view == "FALSE" || view == "False") {
            return false;
        }
        return true;
    }
    return false;
}

[[nodiscard]] std::optional<std::string> nativeExecDisabledReason() {
    if (envFlagEnabled("ZANNA_TESTS_DISABLE_NATIVE_EXEC")) {
        return std::string("Native execution disabled via ZANNA_TESTS_DISABLE_NATIVE_EXEC");
    }
    if (envFlagEnabled("ZANNA_TESTS_DISABLE_SUBPROCESS")) {
        return std::string("Native execution disabled via ZANNA_TESTS_DISABLE_SUBPROCESS");
    }
    return std::nullopt;
}

[[nodiscard]] int decodeExitCode(const int rawStatus) {
#ifdef _WIN32
    return rawStatus;
#else
    if (rawStatus == -1) {
        return -1;
    }
    if (WIFEXITED(rawStatus)) {
        return WEXITSTATUS(rawStatus);
    }
    if (WIFSIGNALED(rawStatus)) {
        return 128 + WTERMSIG(rawStatus);
    }
    return rawStatus;
#endif
}

[[nodiscard]] std::string makeRunNativeCommand(const std::filesystem::path &ilPath) {
    return zanna::tests::quoteForShell(std::filesystem::path(ZANNA_ILC_PATH)) + " codegen x64 " +
           zanna::tests::quoteForShell(ilPath) + " -run-native";
}

[[nodiscard]] int runNativeCommand(const std::filesystem::path &ilPath,
                                   const std::string &command) {
#ifdef _WIN32
    const std::string exePath = std::filesystem::path(ZANNA_ILC_PATH).string();
    const std::string ilPathString = ilPath.string();
    const char *const argv[] = {
        exePath.c_str(), "codegen", "x64", ilPathString.c_str(), "-run-native", nullptr};
    const intptr_t status = _spawnv(_P_WAIT, exePath.c_str(), argv);
    if (status == -1) {
        return -1;
    }
    return static_cast<int>(status);
#else
    return decodeExitCode(std::system(command.c_str()));
#endif
}

[[nodiscard]] bool writeTextFile(const std::filesystem::path &path, std::string_view contents) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file << contents;
    return file.good();
}

class TempDirGuard {
  public:
    explicit TempDirGuard(std::filesystem::path path) : path_(std::move(path)) {}

    TempDirGuard(const TempDirGuard &) = delete;
    TempDirGuard &operator=(const TempDirGuard &) = delete;
    TempDirGuard(TempDirGuard &&) = delete;
    TempDirGuard &operator=(TempDirGuard &&) = delete;

    ~TempDirGuard() {
        if (path_.empty()) {
            return;
        }
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    [[nodiscard]] const std::filesystem::path &path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_{};
};

struct NativeRunResult {
    bool skipped{false};
    bool launchFailed{false};
    int exitCode{0};
    std::string command{};
    std::string message{};
};

[[nodiscard]] NativeRunResult runNativeIl(std::string_view tempPrefix,
                                          std::string_view ilFileName,
                                          std::string_view programText) {
    NativeRunResult result{};

    if (auto reason = nativeExecDisabledReason()) {
        result.skipped = true;
        result.message = *reason;
        return result;
    }

    std::error_code tempEc;
    const std::filesystem::path baseTemp = std::filesystem::temp_directory_path(tempEc);
    if (tempEc) {
        result.launchFailed = true;
        result.message = std::string("Failed to resolve temporary directory: ") + tempEc.message();
        return result;
    }

    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path tempDir = baseTemp / (std::string(tempPrefix) + suffix);

    std::error_code createEc;
    std::filesystem::create_directories(tempDir, createEc);
    if (createEc) {
        result.launchFailed = true;
        result.message = std::string("Failed to create temporary directory '") + tempDir.string() +
                         "': " + createEc.message();
        return result;
    }

    TempDirGuard guard(tempDir);

    const std::filesystem::path ilPath = tempDir / std::string(ilFileName);
    if (!writeTextFile(ilPath, programText)) {
        result.launchFailed = true;
        result.message = std::string("Failed to write IL program to '") + ilPath.string() + "'";
        return result;
    }

    const std::string command = makeRunNativeCommand(ilPath);
    result.command = command;

    const int exitCode = runNativeCommand(ilPath, command);
    if (exitCode == -1) {
        result.launchFailed = true;
        result.message = std::string("Failed to execute '") + command + "'";
        return result;
    }

    result.exitCode = exitCode;
    result.message =
        std::string("Command '") + command + "' exited with code " + std::to_string(exitCode);
    return result;
}

[[nodiscard]] NativeRunResult runDivTrapNative() {
    constexpr std::string_view kDivTrapProgram = R"(il 0.3.0

func @main() -> i64 {
entry:
  %q:i64 = sdiv.chk0 1, 0
  ret %q
}
)";

    return runNativeIl("zanna_div_trap_", "div_trap.il", kDivTrapProgram);
}

[[nodiscard]] NativeRunResult runCheckedRemainderNative() {
    constexpr std::string_view kRemainderProgram = R"(il 0.3.0

func @main() -> i64 {
entry:
  %seed:i64 = iadd.ovf 400, 7
  %px:i64 = isub.ovf %seed, 388
  %py:i64 = isub.ovf %seed, 396
  %a:i64 = srem.chk0 %px, 8
  %b:i64 = srem.chk0 %py, 8
  %c:i64 = iadd.ovf %a, %b
  ret %c
}
)";

    return runNativeIl("zanna_checked_rem_", "checked_rem.il", kRemainderProgram);
}

} // namespace
} // namespace zanna::codegen::x64

int main() {
    using namespace zanna::codegen::x64;

    const ILModule module = makeDivModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    if (!result.errors.empty()) {
        std::cerr << result.errors;
        return EXIT_FAILURE;
    }

    const DivTrapSequence sequence = analyseDivTrapSequence(result.asmText);
    if (!sequence.hasSelfTest || !sequence.hasTrapBranch || !sequence.hasCqto ||
        !sequence.hasIdiv || !sequence.hasIdivScratch || !sequence.hasScratchDivisorMove ||
        !sequence.hasTrapCall) {
        std::cerr << "Missing guarded division pattern in assembly:\n" << result.asmText;
        return EXIT_FAILURE;
    }

    const NativeRunResult native = runDivTrapNative();
    if (!native.skipped) {
        if (native.launchFailed) {
            std::cerr << native.message << '\n';
            return EXIT_FAILURE;
        }
        if (native.exitCode == 0) {
            std::cerr
                << "Expected ilc run-native to exit with a non-zero status when dividing by zero.\n"
                << native.message << '\n';
            return EXIT_FAILURE;
        }
    }

    const NativeRunResult remainderNative = runCheckedRemainderNative();
    if (!remainderNative.skipped) {
        if (remainderNative.launchFailed) {
            std::cerr << remainderNative.message << '\n';
            return EXIT_FAILURE;
        }
        if (remainderNative.exitCode != 6) {
            std::cerr << "Expected checked signed remainder native run to exit with 6.\n"
                      << remainderNative.message << '\n';
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
