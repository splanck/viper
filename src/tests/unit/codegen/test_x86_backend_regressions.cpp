//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_x86_backend_regressions.cpp
// Purpose: Regression tests for x86-64 backend bugs found during review.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/x86_64/Backend.hpp"

#include <array>
#include <cstdio>
#include <iostream>
#include <regex>
#include <string>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

using namespace viper::codegen::x64;

namespace {

ILValue val(ILValue::Kind kind, int id) {
    ILValue value{};
    value.kind = kind;
    value.id = id;
    return value;
}

ILValue imm(int64_t value) {
    ILValue operand{};
    operand.kind = ILValue::Kind::I64;
    operand.id = -1;
    operand.i64 = value;
    return operand;
}

ILValue label(const char *name) {
    ILValue operand{};
    operand.kind = ILValue::Kind::LABEL;
    operand.id = -1;
    operand.label = name;
    return operand;
}

ILInstr op(const char *opcode,
           std::vector<ILValue> ops,
           int resultId = -1,
           ILValue::Kind resultKind = ILValue::Kind::I64) {
    ILInstr instr{};
    instr.opcode = opcode;
    instr.ops = std::move(ops);
    instr.resultId = resultId;
    instr.resultKind = resultKind;
    return instr;
}

CodegenResult compile(const ILFunction &fn) {
    ILModule module{};
    module.funcs = {fn};
    return emitModuleToAssembly(module, {});
}

bool containsRegex(const std::string &text, const std::string &pattern) {
    return std::regex_search(text, std::regex(pattern));
}

int dupFd(int fd) {
#if defined(_WIN32)
    return _dup(fd);
#else
    return dup(fd);
#endif
}

int dup2Fd(int src, int dst) {
#if defined(_WIN32)
    return _dup2(src, dst);
#else
    return dup2(src, dst);
#endif
}

int closeFd(int fd) {
#if defined(_WIN32)
    return _close(fd);
#else
    return close(fd);
#endif
}

int fileNumber(FILE *file) {
#if defined(_WIN32)
    return _fileno(file);
#else
    return fileno(file);
#endif
}

template <typename Fn>
std::string captureStderr(Fn &&fn) {
    std::fflush(stderr);
    std::cerr.flush();

    FILE *capture = std::tmpfile();
    if (capture == nullptr)
        return {};

    const int stderrFd = fileNumber(stderr);
    const int captureFd = fileNumber(capture);
    const int savedFd = dupFd(stderrFd);
    if (savedFd < 0) {
        std::fclose(capture);
        return {};
    }

    if (dup2Fd(captureFd, stderrFd) < 0) {
        closeFd(savedFd);
        std::fclose(capture);
        return {};
    }

    fn();

    std::fflush(stderr);
    std::cerr.flush();
    dup2Fd(savedFd, stderrFd);
    closeFd(savedFd);

    std::rewind(capture);
    std::string out;
    std::array<char, 256> buffer{};
    while (true) {
        const std::size_t n = std::fread(buffer.data(), 1, buffer.size(), capture);
        if (n == 0)
            break;
        out.append(buffer.data(), n);
    }
    std::fclose(capture);
    return out;
}

} // namespace

TEST(X86BackendRegressions, ConstNullLoadMaterializesBaseAndEmitsMemoryRead) {
    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {
        op("const_null", {}, 0, ILValue::Kind::PTR),
        op("load", {val(ILValue::Kind::PTR, 0)}, 1, ILValue::Kind::I64),
        op("ret", {val(ILValue::Kind::I64, 1)})};

    ILFunction fn{};
    fn.name = "null_load";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    if (!result.errors.empty()) {
        std::cerr << result.errors << '\n';
    }
    ASSERT_TRUE(result.errors.empty());
    EXPECT_TRUE(result.asmText.find("xorl ") != std::string::npos ||
                containsRegex(result.asmText, R"(movq\s+\$0,\s*%r[a-z0-9]+)"));
    EXPECT_TRUE(containsRegex(result.asmText, R"(movq\s+(?:0)?\(%r[a-z0-9]+\),\s*%r[a-z0-9]+)"));
}

TEST(X86BackendRegressions, ConstNullStoreMaterializesBaseAndEmitsMemoryWrite) {
    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {
        op("const_null", {}, 0, ILValue::Kind::PTR),
        op("store", {val(ILValue::Kind::PTR, 0), imm(7)}),
        op("ret", {imm(0)})};

    ILFunction fn{};
    fn.name = "null_store";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    if (!result.errors.empty()) {
        std::cerr << result.errors << '\n';
    }
    ASSERT_TRUE(result.errors.empty());
    EXPECT_TRUE(result.asmText.find("xorl ") != std::string::npos ||
                containsRegex(result.asmText, R"(movq\s+\$0,\s*%r[a-z0-9]+)"));
    EXPECT_TRUE(containsRegex(result.asmText, R"(movq\s+%r[a-z0-9]+,\s*(?:0)?\(%r[a-z0-9]+\))"));
}

TEST(X86BackendRegressions, IdxChkNormalizesNonZeroLowerBound) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    entry.instrs = {
        op("idx_chk", {val(ILValue::Kind::I64, 0), imm(10), imm(20)}, 1, ILValue::Kind::I64),
        op("ret", {val(ILValue::Kind::I64, 1)})};

    ILFunction fn{};
    fn.name = "idxchk_norm";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    if (!result.errors.empty()) {
        std::cerr << result.errors << '\n';
    }
    ASSERT_TRUE(result.errors.empty());
    EXPECT_TRUE(result.asmText.find("addq $-10") != std::string::npos ||
                result.asmText.find("subq $10") != std::string::npos);
    EXPECT_TRUE(result.asmText.find("jl ") != std::string::npos);
    EXPECT_TRUE(result.asmText.find("jge ") != std::string::npos);
}

TEST(X86BackendRegressions, IdxChkUsesSignedChecksForNegativeLowerBound) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    entry.instrs = {
        op("idx_chk", {val(ILValue::Kind::I64, 0), imm(-5), imm(5)}, 1, ILValue::Kind::I64),
        op("ret", {val(ILValue::Kind::I64, 1)})};

    ILFunction fn{};
    fn.name = "idxchk_signed";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    if (!result.errors.empty()) {
        std::cerr << result.errors << '\n';
    }
    ASSERT_TRUE(result.errors.empty());
    EXPECT_TRUE(result.asmText.find("addq $5") != std::string::npos);
    EXPECT_TRUE(result.asmText.find("jl ") != std::string::npos);
    EXPECT_TRUE(result.asmText.find("jge ") != std::string::npos);
    EXPECT_TRUE(result.asmText.find("jb ") == std::string::npos);
    EXPECT_TRUE(result.asmText.find("jae ") == std::string::npos);
}

TEST(X86BackendRegressions, CompareBranchUsesFlagsDirectly) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1};
    entry.paramKinds = {ILValue::Kind::I64, ILValue::Kind::I64};
    entry.instrs = {
        op("icmp_ne", {val(ILValue::Kind::I64, 0), val(ILValue::Kind::I64, 1)}, 2,
           ILValue::Kind::I1),
        op("cbr", {val(ILValue::Kind::I1, 2), label("yes"), label("no")})};

    ILBlock yes{};
    yes.name = "yes";
    yes.instrs = {op("ret", {imm(7)})};

    ILBlock no{};
    no.name = "no";
    no.instrs = {op("ret", {imm(13)})};

    ILFunction fn{};
    fn.name = "cmp_branch";
    fn.blocks = {entry, yes, no};

    const CodegenResult result = compile(fn);
    if (!result.errors.empty()) {
        std::cerr << result.errors << '\n';
    }
    ASSERT_TRUE(result.errors.empty());
    EXPECT_TRUE(result.asmText.find("cmpq") != std::string::npos);
    EXPECT_TRUE(result.asmText.find("jne ") != std::string::npos);
    EXPECT_TRUE(result.asmText.find("setne ") == std::string::npos);
    EXPECT_TRUE(result.asmText.find("movzbq ") == std::string::npos);
    EXPECT_TRUE(result.asmText.find("testq ") == std::string::npos);
}

TEST(X86BackendRegressions, SuccessfulAssemblyEmissionIsSilentOnStderr) {
    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("ret", {imm(7)})};

    ILFunction fn{};
    fn.name = "quiet_return";
    fn.blocks = {entry};

    CodegenResult result{};
    const std::string stderrText = captureStderr([&] { result = compile(fn); });

    ASSERT_TRUE(result.errors.empty());
    EXPECT_FALSE(result.asmText.empty());
    EXPECT_TRUE(stderrText.empty());
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
