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
#include "codegen/x86_64/LowerILToMIR.hpp"
#include "codegen/x86_64/TargetX64.hpp"

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

ILValue immF64(double value) {
    ILValue operand{};
    operand.kind = ILValue::Kind::F64;
    operand.id = -1;
    operand.f64 = value;
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

CodegenResult compile(const ILFunction &fn, const CodegenOptions &options = {}) {
    ILModule module{};
    module.funcs = {fn};
    return emitModuleToAssembly(module, options);
}

BinaryEmitResult compileBinary(const ILFunction &fn, const CodegenOptions &options = {}) {
    ILModule module{};
    module.funcs = {fn};
    return emitModuleToBinary(module, options);
}

bool containsRegex(const std::string &text, const std::string &pattern) {
    return std::regex_search(text, std::regex(pattern));
}

bool blockContainsOpcode(const MBasicBlock &block, MOpcode opcode) {
    return std::any_of(block.instructions.begin(), block.instructions.end(), [&](const MInstr &instr) {
        return instr.opcode == opcode;
    });
}

const OpLabel *jumpTarget(const MInstr &instr) {
    if (instr.operands.empty())
        return nullptr;
    return std::get_if<OpLabel>(&instr.operands.back());
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

TEST(X86BackendRegressions, ConditionalBlockArgsUseDedicatedEdgeBlocks) {
    AsmEmitter::RoDataPool roData;
    LowerILToMIR lowering(sysvTarget(), roData);

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    entry.instrs = {
        op("icmp_ne", {val(ILValue::Kind::I64, 0), imm(0)}, 1, ILValue::Kind::I1),
        op("cbr", {val(ILValue::Kind::I1, 1), label("yes"), label("no")})};
    entry.terminatorEdges = {
        ILBlock::EdgeArg{.to = "yes", .argIds = {-1}, .argValues = {imm(7)}},
        ILBlock::EdgeArg{.to = "no", .argIds = {-1}, .argValues = {imm(13)}}};

    ILBlock yes{};
    yes.name = "yes";
    yes.paramIds = {2};
    yes.paramKinds = {ILValue::Kind::I64};
    yes.instrs = {op("ret", {val(ILValue::Kind::I64, 2)})};

    ILBlock no{};
    no.name = "no";
    no.paramIds = {3};
    no.paramKinds = {ILValue::Kind::I64};
    no.instrs = {op("ret", {val(ILValue::Kind::I64, 3)})};

    ILFunction fn{};
    fn.name = "cbr_edges";
    fn.blocks = {entry, yes, no};

    const MFunction mir = lowering.lower(fn);
    ASSERT_EQ(mir.blocks.size(), 5u);

    const auto entryIt = std::find_if(mir.blocks.begin(), mir.blocks.end(), [](const MBasicBlock &bb) {
        return bb.label == ".L_cbr_edges_entry";
    });
    ASSERT_TRUE(entryIt != mir.blocks.end());

    std::size_t edgeBlockCount = 0;
    for (const auto &bb : mir.blocks) {
        if (bb.label.find(".edge") == std::string::npos)
            continue;
        ++edgeBlockCount;
        EXPECT_TRUE(blockContainsOpcode(bb, MOpcode::PX_COPY));
        ASSERT_FALSE(bb.instructions.empty());
        EXPECT_EQ(bb.instructions.back().opcode, MOpcode::JMP);
        const OpLabel *target = jumpTarget(bb.instructions.back());
        ASSERT_TRUE(target != nullptr);
        EXPECT_TRUE(target->name == ".L_cbr_edges_yes" || target->name == ".L_cbr_edges_no");
    }
    EXPECT_EQ(edgeBlockCount, 2u);

    bool branchesViaHelper = false;
    for (const auto &instr : entryIt->instructions) {
        if (instr.opcode != MOpcode::JCC && instr.opcode != MOpcode::JMP)
            continue;
        const OpLabel *target = jumpTarget(instr);
        if (target && target->name.find(".edge") != std::string::npos)
            branchesViaHelper = true;
    }
    EXPECT_TRUE(branchesViaHelper);
}

TEST(X86BackendRegressions, SwitchBlockArgsUseDedicatedEdgeBlocks) {
    AsmEmitter::RoDataPool roData;
    LowerILToMIR lowering(sysvTarget(), roData);

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    entry.instrs = {op("switch_i32",
                       {val(ILValue::Kind::I64, 0),
                        imm(1),
                        label("case1"),
                        imm(2),
                        label("case2"),
                        label("def")})};
    entry.terminatorEdges = {
        ILBlock::EdgeArg{.to = "case1", .argIds = {-1}, .argValues = {imm(11)}},
        ILBlock::EdgeArg{.to = "case2", .argIds = {-1}, .argValues = {imm(22)}},
        ILBlock::EdgeArg{.to = "def", .argIds = {-1}, .argValues = {imm(33)}}};

    ILBlock case1{};
    case1.name = "case1";
    case1.paramIds = {1};
    case1.paramKinds = {ILValue::Kind::I64};
    case1.instrs = {op("ret", {val(ILValue::Kind::I64, 1)})};

    ILBlock case2{};
    case2.name = "case2";
    case2.paramIds = {2};
    case2.paramKinds = {ILValue::Kind::I64};
    case2.instrs = {op("ret", {val(ILValue::Kind::I64, 2)})};

    ILBlock def{};
    def.name = "def";
    def.paramIds = {3};
    def.paramKinds = {ILValue::Kind::I64};
    def.instrs = {op("ret", {val(ILValue::Kind::I64, 3)})};

    ILFunction fn{};
    fn.name = "switch_edges";
    fn.blocks = {entry, case1, case2, def};

    const MFunction mir = lowering.lower(fn);
    ASSERT_EQ(mir.blocks.size(), 7u);

    const auto entryIt =
        std::find_if(mir.blocks.begin(), mir.blocks.end(), [](const MBasicBlock &bb) {
            return bb.label == ".L_switch_edges_entry";
        });
    ASSERT_TRUE(entryIt != mir.blocks.end());

    std::size_t edgeBlockCount = 0;
    for (const auto &bb : mir.blocks) {
        if (bb.label.find(".edge") == std::string::npos)
            continue;
        ++edgeBlockCount;
        EXPECT_TRUE(blockContainsOpcode(bb, MOpcode::PX_COPY));
        ASSERT_FALSE(bb.instructions.empty());
        EXPECT_EQ(bb.instructions.back().opcode, MOpcode::JMP);
    }
    EXPECT_EQ(edgeBlockCount, 3u);
}

TEST(X86BackendRegressions, CheckedSignedDivisionIncludesOverflowTrap) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1};
    entry.paramKinds = {ILValue::Kind::I64, ILValue::Kind::I64};
    entry.instrs = {
        op("sdiv.chk0", {val(ILValue::Kind::I64, 0), val(ILValue::Kind::I64, 1)}, 2,
           ILValue::Kind::I64),
        op("ret", {val(ILValue::Kind::I64, 2)})};

    ILFunction fn{};
    fn.name = "checked_div";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find("rt_trap_ovf"), std::string::npos);
    EXPECT_NE(result.asmText.find("cmpq $-1"), std::string::npos);
}

TEST(X86BackendRegressions, CheckedSignedRemainderZerosMinOverflowPath) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1};
    entry.paramKinds = {ILValue::Kind::I64, ILValue::Kind::I64};
    entry.instrs = {
        op("srem.chk0", {val(ILValue::Kind::I64, 0), val(ILValue::Kind::I64, 1)}, 2,
           ILValue::Kind::I64),
        op("ret", {val(ILValue::Kind::I64, 2)})};

    ILFunction fn{};
    fn.name = "checked_rem";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    ASSERT_TRUE(result.errors.empty());
    EXPECT_EQ(result.asmText.find("rt_trap_ovf"), std::string::npos);
    EXPECT_TRUE(result.asmText.find("movq $0") != std::string::npos ||
                containsRegex(result.asmText, R"(xor[lq]\s+%r[a-z0-9]+,\s*%r[a-z0-9]+)"));
}

TEST(X86BackendRegressions, AssemblyHonorsTargetPlatformSections) {
    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {
        op("const_f64", {immF64(3.25)}, 0, ILValue::Kind::F64),
        op("ret", {val(ILValue::Kind::F64, 0)}, -1, ILValue::Kind::F64)};

    ILFunction fn{};
    fn.name = "const_f64_target";
    fn.blocks = {entry};

    CodegenOptions linuxOpts{};
    linuxOpts.targetPlatform = CodegenOptions::TargetPlatform::Linux;
    const CodegenResult linux = compile(fn, linuxOpts);
    ASSERT_TRUE(linux.errors.empty());
    EXPECT_NE(linux.asmText.find(".section .rodata"), std::string::npos);
    EXPECT_NE(linux.asmText.find(".type"), std::string::npos);
    EXPECT_NE(linux.asmText.find(".note.GNU-stack"), std::string::npos);

    CodegenOptions darwinOpts{};
    darwinOpts.targetPlatform = CodegenOptions::TargetPlatform::Darwin;
    const CodegenResult darwin = compile(fn, darwinOpts);
    ASSERT_TRUE(darwin.errors.empty());
    EXPECT_NE(darwin.asmText.find(".section __TEXT,__const"), std::string::npos);
    EXPECT_EQ(darwin.asmText.find(".type"), std::string::npos);
    EXPECT_EQ(darwin.asmText.find(".note.GNU-stack"), std::string::npos);

    CodegenOptions windowsOpts{};
    windowsOpts.targetPlatform = CodegenOptions::TargetPlatform::Windows;
    const CodegenResult windows = compile(fn, windowsOpts);
    ASSERT_TRUE(windows.errors.empty());
    EXPECT_NE(windows.asmText.find(".section .rdata,\"dr\""), std::string::npos);
    EXPECT_EQ(windows.asmText.find(".type"), std::string::npos);
}

TEST(X86BackendRegressions, BinaryEmissionHonorsTargetPlatformSymbolMangling) {
    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("ret", {imm(0)})};

    ILFunction fn{};
    fn.name = "main";
    fn.blocks = {entry};

    CodegenOptions linuxOpts{};
    linuxOpts.targetPlatform = CodegenOptions::TargetPlatform::Linux;
    const BinaryEmitResult linux = compileBinary(fn, linuxOpts);
    ASSERT_TRUE(linux.errors.empty());
    EXPECT_NE(linux.text.symbols().find("main"), 0U);
    EXPECT_EQ(linux.text.symbols().find("_main"), 0U);

    CodegenOptions darwinOpts{};
    darwinOpts.targetPlatform = CodegenOptions::TargetPlatform::Darwin;
    const BinaryEmitResult darwin = compileBinary(fn, darwinOpts);
    ASSERT_TRUE(darwin.errors.empty());
    EXPECT_NE(darwin.text.symbols().find("_main"), 0U);
    EXPECT_EQ(darwin.text.symbols().find("main"), 0U);
}

TEST(X86BackendRegressions, UnknownOpcodeFailsAssemblyEmission) {
    MFunction fn{};
    fn.name = "bad_opcode";
    MBasicBlock entry{};
    entry.label = ".L_bad_opcode_entry";
    entry.instructions.push_back(MInstr::make(static_cast<MOpcode>(9999), {}));
    fn.blocks.push_back(std::move(entry));

    AsmEmitter::RoDataPool roData{};
    const CodegenResult result = emitMIRToAssembly({fn}, roData, sysvTarget(), {});
    EXPECT_NE(result.errors.find("unknown opcode"), std::string::npos);
    EXPECT_EQ(result.asmText.find("# unknown"), std::string::npos);
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
