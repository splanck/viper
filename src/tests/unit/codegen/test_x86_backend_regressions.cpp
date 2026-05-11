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

#include "codegen/x86_64/AsmEmitter.hpp"
#include "codegen/x86_64/Backend.hpp"
#include "codegen/x86_64/FrameLowering.hpp"
#include "codegen/x86_64/ISel.hpp"
#include "codegen/x86_64/LowerILToMIR.hpp"
#include "codegen/x86_64/Lowering.EmitCommon.hpp"
#include "codegen/x86_64/OperandRoles.hpp"
#include "codegen/x86_64/RegAllocLinear.hpp"
#include "codegen/x86_64/TargetX64.hpp"
#include "codegen/x86_64/peephole/BranchOpt.hpp"
#include "codegen/x86_64/peephole/MemoryOpt.hpp"
#include "codegen/x86_64/peephole/PeepholeCommon.hpp"
#include "codegen/x86_64/ra/Liveness.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

using namespace viper::codegen::x64;

namespace viper::codegen::x64 {
void lowerOverflowOps(MFunction &fn);
}

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

ILValue immI1(int64_t value) {
    ILValue operand{};
    operand.kind = ILValue::Kind::I1;
    operand.id = -1;
    operand.i64 = value;
    return operand;
}

ILValue immPtr(int64_t value) {
    ILValue operand{};
    operand.kind = ILValue::Kind::PTR;
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

ILValue strLit(std::string bytes, std::uint64_t len) {
    ILValue operand{};
    operand.kind = ILValue::Kind::STR;
    operand.id = -1;
    operand.str = std::move(bytes);
    operand.strLen = len;
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

ILInstr opBits(const char *opcode,
               std::vector<ILValue> ops,
               int resultId,
               ILValue::Kind resultKind,
               std::uint8_t resultBits) {
    ILInstr instr = op(opcode, std::move(ops), resultId, resultKind);
    instr.resultBits = resultBits;
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
    return std::any_of(block.instructions.begin(),
                       block.instructions.end(),
                       [&](const MInstr &instr) { return instr.opcode == opcode; });
}

const OpLabel *jumpTarget(const MInstr &instr) {
    if (instr.operands.empty())
        return nullptr;
    return std::get_if<OpLabel>(&instr.operands.back());
}

OpMem baseMem(const Operand &base, int32_t disp = 0) {
    OpMem mem{};
    mem.base = std::get<OpReg>(base);
    mem.index = {};
    mem.scale = 1;
    mem.disp = disp;
    mem.hasIndex = false;
    return mem;
}

OpMem indexedMem(const Operand &base, const Operand &index, uint8_t scale, int32_t disp = 0) {
    OpMem mem = baseMem(base, disp);
    mem.index = std::get<OpReg>(index);
    mem.scale = scale;
    mem.hasIndex = true;
    return mem;
}

const OpMem *firstMemOperand(const MInstr &instr) {
    for (const auto &operand : instr.operands) {
        if (const auto *mem = std::get_if<OpMem>(&operand)) {
            return mem;
        }
    }
    return nullptr;
}

bool isControlTerminator(MOpcode opcode) {
    return opcode == MOpcode::JMP || opcode == MOpcode::JCC || opcode == MOpcode::RET ||
           opcode == MOpcode::UD2;
}

bool blockHasNonTerminatorAfterControlTransfer(const MBasicBlock &block) {
    bool seenControlTransfer = false;
    for (const auto &instr : block.instructions) {
        if (seenControlTransfer && !isControlTerminator(instr.opcode)) {
            return true;
        }
        if (isControlTerminator(instr.opcode)) {
            seenControlTransfer = true;
        }
    }
    return false;
}

bool blockHasExplicitConditionalFallthrough(const MBasicBlock &block) {
    if (block.instructions.size() < 2) {
        return false;
    }
    for (std::size_t i = 0; i + 1 < block.instructions.size(); ++i) {
        if (block.instructions[i].opcode == MOpcode::JCC &&
            block.instructions[i + 1].opcode == MOpcode::JMP) {
            return true;
        }
    }
    return false;
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

template <typename Fn> std::string captureStderr(Fn &&fn) {
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

TEST(X86BackendRegressions, VoidReturnClearsIntegerReturnRegister) {
    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("ret", {})};

    ILFunction fn{};
    fn.name = "main";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    if (!result.errors.empty()) {
        std::cerr << result.errors << '\n';
    }
    ASSERT_TRUE(result.errors.empty());
    EXPECT_TRUE(containsRegex(result.asmText, R"(xorl\s+%eax,\s*%eax)"));
}

TEST(X86BackendRegressions, ConstNullLoadMaterializesBaseAndEmitsMemoryRead) {
    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("const_null", {}, 0, ILValue::Kind::PTR),
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
    entry.instrs = {op("const_null", {}, 0, ILValue::Kind::PTR),
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
    EXPECT_NE(result.asmText.find("rt_trap_raise_error"), std::string::npos);
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
    EXPECT_NE(result.asmText.find("rt_trap_raise_error"), std::string::npos);
}

TEST(X86BackendRegressions, CheckedFpToSiUsesRoundEvenAndStructuredTrap) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::F64};
    entry.instrs = {op("fptosi_chk", {val(ILValue::Kind::F64, 0)}, 1, ILValue::Kind::I64),
                    op("ret", {val(ILValue::Kind::I64, 1)})};

    ILFunction fn{};
    fn.name = "checked_fptosi";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find("rt_round_even"), std::string::npos);
    EXPECT_NE(result.asmText.find("rt_trap_raise_error"), std::string::npos);
    EXPECT_NE(result.asmText.find(".Lfptosi_chk_invalid_"), std::string::npos);
    EXPECT_NE(result.asmText.find(".Lfptosi_chk_ovf_"), std::string::npos);
}

TEST(X86BackendRegressions, CheckedFpToUiUsesUpperBoundCheckAndStructuredTrap) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::F64};
    entry.instrs = {op("fptoui", {val(ILValue::Kind::F64, 0)}, 1, ILValue::Kind::I64),
                    op("ret", {val(ILValue::Kind::I64, 1)})};

    ILFunction fn{};
    fn.name = "checked_fptoui";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find("rt_round_even"), std::string::npos);
    EXPECT_NE(result.asmText.find("rt_trap_raise_error"), std::string::npos);
    EXPECT_NE(result.asmText.find(".Lfptoui_invalid_"), std::string::npos);
    EXPECT_NE(result.asmText.find(".Lfptoui_ovf_"), std::string::npos);
}

TEST(X86BackendRegressions, FptosiTruncatingConversionChecksNaNAndRange) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::F64};
    entry.instrs = {op("fptosi", {val(ILValue::Kind::F64, 0)}, 1, ILValue::Kind::I64),
                    op("ret", {val(ILValue::Kind::I64, 1)})};

    ILFunction fn{};
    fn.name = "fptosi_checked";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find(".Lfptosi_invalid_"), std::string::npos);
    EXPECT_NE(result.asmText.find(".Lfptosi_ovf_"), std::string::npos);
    EXPECT_NE(result.asmText.find("rt_trap_raise_error"), std::string::npos);
    EXPECT_NE(result.asmText.find("cvttsd2siq"), std::string::npos);
}

TEST(X86BackendRegressions, CheckedSignedNarrowEmitsWidthCheck) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    entry.instrs = {
        opBits("si_narrow_chk", {val(ILValue::Kind::I64, 0)}, 1, ILValue::Kind::I64, 16),
        op("ret", {val(ILValue::Kind::I64, 1)})};

    ILFunction fn{};
    fn.name = "narrow_i16";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find("shlq $48"), std::string::npos);
    EXPECT_NE(result.asmText.find("sarq $48"), std::string::npos);
    EXPECT_NE(result.asmText.find(".Lnarrow_chk_trap_"), std::string::npos);
    EXPECT_NE(result.asmText.find("rt_trap_raise_error"), std::string::npos);
}

TEST(X86BackendRegressions, CheckedNarrowLabelsAreSplitBeforeRegisterAllocation) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    entry.instrs = {
        opBits("si_narrow_chk", {val(ILValue::Kind::I64, 0)}, 1, ILValue::Kind::I64, 16),
        op("ret", {val(ILValue::Kind::I64, 1)})};

    ILFunction fn{};
    fn.name = "narrow_cfg";
    fn.blocks = {entry};

    ILModule module{};
    module.funcs = {fn};

    AsmEmitter::RoDataPool roData;
    std::vector<MFunction> mir;
    std::vector<FrameInfo> frames;
    std::string errors;
    CodegenOptions options;
    ASSERT_TRUE(legalizeModuleToMIR(module, win64Target(), options, roData, mir, frames, errors));
    if (!errors.empty()) {
        std::cerr << errors << '\n';
    }
    ASSERT_TRUE(errors.empty());
    ASSERT_EQ(mir.size(), 1u);

    bool foundInlineLabel = false;
    bool foundTrapBlock = false;
    bool foundDoneBlock = false;
    bool trapEndsInUd2 = false;
    for (const auto &block : mir.front().blocks) {
        foundTrapBlock =
            foundTrapBlock || block.label.find(".Lnarrow_chk_trap_") != std::string::npos;
        foundDoneBlock =
            foundDoneBlock || block.label.find(".Lnarrow_chk_done_") != std::string::npos;
        foundInlineLabel = foundInlineLabel || blockContainsOpcode(block, MOpcode::LABEL);
        if (block.label.find(".Lnarrow_chk_trap_") != std::string::npos &&
            !block.instructions.empty()) {
            trapEndsInUd2 = block.instructions.back().opcode == MOpcode::UD2;
        }
    }

    EXPECT_TRUE(foundTrapBlock);
    EXPECT_TRUE(foundDoneBlock);
    EXPECT_TRUE(trapEndsInUd2);
    EXPECT_FALSE(foundInlineLabel);
}

TEST(X86BackendRegressions, SubWidthCheckedAddEmitsRangeCheck) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1};
    entry.paramKinds = {ILValue::Kind::I64, ILValue::Kind::I64};
    entry.instrs = {opBits("iadd.ovf",
                           {val(ILValue::Kind::I64, 0), val(ILValue::Kind::I64, 1)},
                           2,
                           ILValue::Kind::I64,
                           16),
                    op("ret", {val(ILValue::Kind::I64, 2)})};

    ILFunction fn{};
    fn.name = "iadd_i16";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find("addq"), std::string::npos);
    EXPECT_NE(result.asmText.find("shlq $48"), std::string::npos);
    EXPECT_NE(result.asmText.find("sarq $48"), std::string::npos);
    EXPECT_NE(result.asmText.find(".Liadd_ovf_trap_"), std::string::npos);
    EXPECT_NE(result.asmText.find("rt_trap_raise_error"), std::string::npos);
}

TEST(X86BackendRegressions, CompareBranchUsesFlagsDirectly) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1};
    entry.paramKinds = {ILValue::Kind::I64, ILValue::Kind::I64};
    entry.instrs = {op("icmp_ne",
                       {val(ILValue::Kind::I64, 0), val(ILValue::Kind::I64, 1)},
                       2,
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
    entry.instrs = {op("icmp_ne", {val(ILValue::Kind::I64, 0), imm(0)}, 1, ILValue::Kind::I1),
                    op("cbr", {val(ILValue::Kind::I1, 1), label("yes"), label("no")})};
    entry.terminatorEdges = {ILBlock::EdgeArg{.to = "yes", .argIds = {-1}, .argValues = {imm(7)}},
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

    const auto entryIt =
        std::find_if(mir.blocks.begin(), mir.blocks.end(), [](const MBasicBlock &bb) {
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
        ILBlock::EdgeArg{.to = "def", .argIds = {-1}, .argValues = {imm(33)}},
        ILBlock::EdgeArg{.to = "case1", .argIds = {-1}, .argValues = {imm(11)}},
        ILBlock::EdgeArg{.to = "case2", .argIds = {-1}, .argValues = {imm(22)}}};

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

    struct EdgeBlockInfo {
        std::string label;
        std::string target;
        int64_t immediate = 0;
    };

    std::vector<EdgeBlockInfo> edgeBlocks;
    for (const auto &bb : mir.blocks) {
        if (bb.label.find(".edge") == std::string::npos)
            continue;
        ++edgeBlockCount;
        EXPECT_TRUE(blockContainsOpcode(bb, MOpcode::PX_COPY));
        ASSERT_FALSE(bb.instructions.empty());
        EXPECT_EQ(bb.instructions.back().opcode, MOpcode::JMP);
        const OpLabel *target = jumpTarget(bb.instructions.back());
        ASSERT_TRUE(target != nullptr);

        int64_t immediate = 0;
        bool foundImmediate = false;
        for (const auto &instr : bb.instructions) {
            if (instr.opcode != MOpcode::MOVri || instr.operands.size() < 2)
                continue;
            const auto *immOp = std::get_if<OpImm>(&instr.operands[1]);
            if (immOp) {
                immediate = immOp->val;
                foundImmediate = true;
            }
        }
        ASSERT_TRUE(foundImmediate);
        edgeBlocks.push_back({bb.label, target->name, immediate});
    }
    EXPECT_EQ(edgeBlockCount, 3u);

    auto assertEdge = [&](const std::string &helperLabel,
                          const std::string &expectedTarget,
                          int64_t expectedImmediate) {
        const auto edgeIt =
            std::find_if(edgeBlocks.begin(), edgeBlocks.end(), [&](const EdgeBlockInfo &info) {
                return info.label == helperLabel;
            });
        ASSERT_TRUE(edgeIt != edgeBlocks.end());
        EXPECT_EQ(edgeIt->target, expectedTarget);
        EXPECT_EQ(edgeIt->immediate, expectedImmediate);
    };

    std::string case1Edge;
    std::string case2Edge;
    std::string defaultEdge;
    for (std::size_t i = 0; i < entryIt->instructions.size(); ++i) {
        const auto &instr = entryIt->instructions[i];
        if (instr.opcode == MOpcode::CMPri && instr.operands.size() >= 2) {
            const auto *caseValue = std::get_if<OpImm>(&instr.operands[1]);
            ASSERT_TRUE(caseValue != nullptr);
            ASSERT_LT(i + 1, entryIt->instructions.size());
            const OpLabel *target = jumpTarget(entryIt->instructions[i + 1]);
            ASSERT_TRUE(target != nullptr);
            if (caseValue->val == 1) {
                case1Edge = target->name;
            } else if (caseValue->val == 2) {
                case2Edge = target->name;
            }
        } else if (instr.opcode == MOpcode::JMP) {
            const OpLabel *target = jumpTarget(instr);
            ASSERT_TRUE(target != nullptr);
            defaultEdge = target->name;
        }
    }

    ASSERT_FALSE(case1Edge.empty());
    ASSERT_FALSE(case2Edge.empty());
    ASSERT_FALSE(defaultEdge.empty());
    assertEdge(case1Edge, ".L_switch_edges_case1", 11);
    assertEdge(case2Edge, ".L_switch_edges_case2", 22);
    assertEdge(defaultEdge, ".L_switch_edges_def", 33);
}

TEST(X86BackendRegressions, LargerSwitchUsesBalancedDecisionLabels) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    entry.instrs = {op("switch_i32",
                       {val(ILValue::Kind::I64, 0),
                        imm(0),
                        label("c0"),
                        imm(1),
                        label("c1"),
                        imm(2),
                        label("c2"),
                        imm(3),
                        label("c3"),
                        imm(4),
                        label("c4"),
                        imm(5),
                        label("c5"),
                        imm(6),
                        label("c6"),
                        imm(7),
                        label("c7"),
                        label("def")})};

    std::vector<ILBlock> blocks;
    blocks.push_back(entry);
    for (int i = 0; i < 8; ++i) {
        ILBlock caseBlock{};
        caseBlock.name = "c" + std::to_string(i);
        caseBlock.instrs = {op("ret", {imm(100 + i)})};
        blocks.push_back(std::move(caseBlock));
    }
    ILBlock def{};
    def.name = "def";
    def.instrs = {op("ret", {imm(0)})};
    blocks.push_back(std::move(def));

    ILFunction fn{};
    fn.name = "switch_tree";
    fn.blocks = std::move(blocks);

    const CodegenResult result = compile(fn);
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find(".Lswitch_left_"), std::string::npos);
    EXPECT_NE(result.asmText.find(".Lswitch_right_"), std::string::npos);
}

TEST(X86BackendRegressions, CheckedSignedDivisionIncludesOverflowTrap) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1};
    entry.paramKinds = {ILValue::Kind::I64, ILValue::Kind::I64};
    entry.instrs = {op("sdiv.chk0",
                       {val(ILValue::Kind::I64, 0), val(ILValue::Kind::I64, 1)},
                       2,
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
    entry.instrs = {op("srem.chk0",
                       {val(ILValue::Kind::I64, 0), val(ILValue::Kind::I64, 1)},
                       2,
                       ILValue::Kind::I64),
                    op("ret", {val(ILValue::Kind::I64, 2)})};

    ILFunction fn{};
    fn.name = "checked_rem";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    ASSERT_TRUE(result.errors.empty());
    EXPECT_EQ(result.asmText.find("rt_trap_ovf"), std::string::npos);
    EXPECT_TRUE(
        result.asmText.find("movq $0") != std::string::npos ||
        containsRegex(result.asmText,
                      R"(xor[lq]\s+%(?:e[a-z]+|r[a-z0-9]+d?),\s*%(?:e[a-z]+|r[a-z0-9]+d?))"));
}

TEST(X86BackendRegressions, AssemblyHonorsTargetPlatformSections) {
    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("const_f64", {immF64(3.25)}, 0, ILValue::Kind::F64),
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
    // Binary emission records canonical symbol names. Mach-O ABI underscores
    // are applied by the object writer, not stored in CodeSection.
    EXPECT_NE(darwin.text.symbols().find("main"), 0U);
    EXPECT_EQ(darwin.text.symbols().find("_main"), 0U);
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

TEST(X86BackendRegressions, TrapPayloadIsForwardedToRuntime) {
    AsmEmitter::RoDataPool roData;
    LowerILToMIR lowering(sysvTarget(), roData);

    ILValue message{};
    message.kind = ILValue::Kind::STR;
    message.id = -1;
    message.str = "boom";
    message.strLen = 4;

    ILInstr trap{};
    trap.opcode = "trap";
    trap.ops = {message};

    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {trap};

    ILFunction fn{};
    fn.name = "trap_payload";
    fn.blocks = {entry};

    (void)lowering.lower(fn);
    const auto trapIt =
        std::find_if(lowering.callPlans().begin(),
                     lowering.callPlans().end(),
                     [](const CallLoweringPlan &plan) { return plan.callee == "rt_trap_string"; });
    ASSERT_TRUE(trapIt != lowering.callPlans().end());
    ASSERT_EQ(trapIt->args.size(), 1u);
    EXPECT_FALSE(trapIt->args[0].isImm);
}

TEST(X86BackendRegressions, LargeGepImmediateIsMaterializedInsteadOfTruncated) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::PTR};
    entry.instrs = {
        op("gep", {val(ILValue::Kind::PTR, 0), imm(2147483648LL)}, 1, ILValue::Kind::PTR),
        op("ret", {val(ILValue::Kind::PTR, 1)})};

    ILFunction fn{};
    fn.name = "large_gep";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find("$2147483648"), std::string::npos);
    EXPECT_NE(result.asmText.find("addq"), std::string::npos);
    EXPECT_EQ(result.asmText.find("-2147483648("), std::string::npos);
}

TEST(X86BackendRegressions, LargeLoadDisplacementIsMaterializedInsteadOfTruncated) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::PTR};
    entry.instrs = {
        op("load", {val(ILValue::Kind::PTR, 0), imm(2147483648LL)}, 1, ILValue::Kind::I64),
        op("ret", {val(ILValue::Kind::I64, 1)})};

    ILFunction fn{};
    fn.name = "large_load_disp";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find("$2147483648"), std::string::npos);
    EXPECT_NE(result.asmText.find("addq"), std::string::npos);
    EXPECT_EQ(result.asmText.find("-2147483648("), std::string::npos);
}

TEST(X86BackendRegressions, InvalidAllocaSizeIsRejected) {
    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("alloca", {imm(0)}, 0, ILValue::Kind::PTR), op("ret", {imm(0)})};

    ILFunction fn{};
    fn.name = "bad_alloca";
    fn.blocks = {entry};

    EXPECT_THROWS(compile(fn), std::runtime_error);
}

TEST(X86BackendRegressions, SelectPseudoIsLoweredBeforeEmission) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I1};
    entry.instrs = {
        op("select", {val(ILValue::Kind::I1, 0), imm(7), imm(13)}, 1, ILValue::Kind::I64),
        op("ret", {val(ILValue::Kind::I64, 1)})};

    ILFunction fn{};
    fn.name = "select_i64";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find("cmovne"), std::string::npos);
    EXPECT_EQ(result.asmText.find("SELECT_GPR"), std::string::npos);
    EXPECT_EQ(result.asmText.find("select pseudo survived"), std::string::npos);
}

TEST(X86BackendRegressions, CompareBranchFoldPreservesLiveBooleanResult) {
    MFunction fn{};
    fn.name = "cmp_bool_live";
    MBasicBlock entry{};
    entry.label = ".L_cmp_bool_live_entry";
    const Operand a = makeVRegOperand(RegClass::GPR, 0);
    const Operand b = makeVRegOperand(RegClass::GPR, 1);
    const Operand flag = makeVRegOperand(RegClass::GPR, 2);
    const Operand copy = makeVRegOperand(RegClass::GPR, 3);
    entry.instructions = {
        MInstr::make(MOpcode::CMPrr, {a, b}),
        MInstr::make(MOpcode::SETcc, {makeImmOperand(1), flag}),
        MInstr::make(MOpcode::MOVZXrr8, {flag, flag}),
        MInstr::make(MOpcode::TESTrr, {flag, flag}),
        MInstr::make(MOpcode::JCC, {makeImmOperand(1), makeLabelOperand(".L_true")}),
        MInstr::make(MOpcode::MOVrr, {copy, flag}),
        MInstr::make(MOpcode::JMP, {makeLabelOperand(".L_done")})};
    fn.blocks.push_back(std::move(entry));

    ISel isel(sysvTarget());
    isel.lowerCompareAndBranch(fn);

    const auto &instructions = fn.blocks.front().instructions;
    EXPECT_TRUE(std::any_of(instructions.begin(), instructions.end(), [](const MInstr &instr) {
        return instr.opcode == MOpcode::SETcc;
    }));
}

TEST(X86BackendRegressions, RegAllocSpillsLiveValueBeforePhysicalRdxClobber) {
    MFunction fn{};
    fn.name = "phys_rdx_clobber";

    MBasicBlock entry{};
    entry.label = ".L_phys_rdx_clobber_entry";
    const Operand v1 = makeVRegOperand(RegClass::GPR, 1);
    const Operand v2 = makeVRegOperand(RegClass::GPR, 2);
    const Operand v3 = makeVRegOperand(RegClass::GPR, 3);
    const Operand v4 = makeVRegOperand(RegClass::GPR, 4);
    entry.instructions = {
        MInstr::make(MOpcode::MOVri, {v1, makeImmOperand(1)}),
        MInstr::make(MOpcode::MOVri, {v2, makeImmOperand(2)}),
        MInstr::make(MOpcode::MOVri, {v3, makeImmOperand(3)}),
        MInstr::make(MOpcode::MOVri, {v4, makeImmOperand(4)}),
        MInstr::make(MOpcode::XORrr32,
                     {makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RDX)),
                      makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RDX))}),
        MInstr::make(MOpcode::ADDrr, {v1, v2}),
        MInstr::make(MOpcode::ADDrr, {v1, v3}),
        MInstr::make(MOpcode::ADDrr, {v1, v4}),
        MInstr::make(MOpcode::RET)};
    fn.blocks.push_back(std::move(entry));

    (void)allocate(fn, sysvTarget());

    const auto &instructions = fn.blocks.front().instructions;
    const auto xorIt =
        std::find_if(instructions.begin(), instructions.end(), [](const MInstr &instr) {
            if (instr.opcode != MOpcode::XORrr32 || instr.operands.empty())
                return false;
            const auto *dst = std::get_if<OpReg>(&instr.operands[0]);
            return dst && dst->isPhys && static_cast<PhysReg>(dst->idOrPhys) == PhysReg::RDX;
        });
    ASSERT_TRUE(xorIt != instructions.end());

    bool spilledRdxBeforeClobber = false;
    for (auto it = instructions.begin(); it != xorIt; ++it) {
        if (it->opcode != MOpcode::MOVrm || it->operands.size() < 2)
            continue;
        const auto *src = std::get_if<OpReg>(&it->operands[1]);
        if (src && src->isPhys && static_cast<PhysReg>(src->idOrPhys) == PhysReg::RDX) {
            spilledRdxBeforeClobber = true;
        }
    }
    EXPECT_TRUE(spilledRdxBeforeClobber);
}

TEST(X86BackendRegressions, RegAllocSpillsLiveValuesBeforeImplicitDivClobber) {
    MFunction fn{};
    fn.name = "implicit_div_clobber";

    MBasicBlock entry{};
    entry.label = ".L_implicit_div_clobber_entry";
    const Operand v1 = makeVRegOperand(RegClass::GPR, 1);
    const Operand v2 = makeVRegOperand(RegClass::GPR, 2);
    const Operand v3 = makeVRegOperand(RegClass::GPR, 3);
    const Operand v4 = makeVRegOperand(RegClass::GPR, 4);
    entry.instructions = {
        MInstr::make(MOpcode::MOVri, {v1, makeImmOperand(1)}),
        MInstr::make(MOpcode::MOVri, {v2, makeImmOperand(2)}),
        MInstr::make(MOpcode::MOVri, {v3, makeImmOperand(3)}),
        MInstr::make(MOpcode::MOVri, {v4, makeImmOperand(4)}),
        MInstr::make(MOpcode::IDIVrm,
                     {makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::R10))}),
        MInstr::make(MOpcode::ADDrr, {v2, v3}),
        MInstr::make(MOpcode::ADDrr, {v1, v4}),
        MInstr::make(MOpcode::RET)};
    fn.blocks.push_back(std::move(entry));

    (void)allocate(fn, sysvTarget());

    const auto &instructions = fn.blocks.front().instructions;
    const auto divIt =
        std::find_if(instructions.begin(), instructions.end(), [](const MInstr &instr) {
            return instr.opcode == MOpcode::IDIVrm;
        });
    ASSERT_TRUE(divIt != instructions.end());

    bool spilledRax = false;
    bool spilledRdx = false;
    for (auto it = instructions.begin(); it != divIt; ++it) {
        if (it->opcode != MOpcode::MOVrm || it->operands.size() < 2)
            continue;
        const auto *src = std::get_if<OpReg>(&it->operands[1]);
        if (!src || !src->isPhys)
            continue;
        spilledRax = spilledRax || static_cast<PhysReg>(src->idOrPhys) == PhysReg::RAX;
        spilledRdx = spilledRdx || static_cast<PhysReg>(src->idOrPhys) == PhysReg::RDX;
    }
    EXPECT_TRUE(spilledRax);
    EXPECT_TRUE(spilledRdx);
}

TEST(X86BackendRegressions, RegAllocDoesNotAllocateFixedScratchRegisters) {
    MFunction fn{};
    fn.name = "reserved_scratch_regs";

    MBasicBlock entry{};
    entry.label = ".L_reserved_scratch_regs_entry";
    std::vector<Operand> regs;
    for (uint16_t id = 1; id <= 12; ++id) {
        regs.push_back(makeVRegOperand(RegClass::GPR, id));
        entry.instructions.push_back(
            MInstr::make(MOpcode::MOVri, {regs.back(), makeImmOperand(id)}));
    }
    for (std::size_t idx = 1; idx < regs.size(); ++idx) {
        entry.instructions.push_back(MInstr::make(MOpcode::ADDrr, {regs[0], regs[idx]}));
    }
    entry.instructions.push_back(MInstr::make(MOpcode::RET));
    fn.blocks.push_back(std::move(entry));

    (void)allocate(fn, win64Target());

    for (const auto &block : fn.blocks) {
        for (const auto &instr : block.instructions) {
            for (const auto &operand : instr.operands) {
                const auto *reg = std::get_if<OpReg>(&operand);
                if (!reg || !reg->isPhys || reg->cls != RegClass::GPR)
                    continue;
                EXPECT_NE(static_cast<PhysReg>(reg->idOrPhys), PhysReg::R10);
                EXPECT_NE(static_cast<PhysReg>(reg->idOrPhys), PhysReg::R11);
            }
        }
    }
}

TEST(X86BackendRegressions, RegAllocPreservesCallerSavedLiveOutAcrossCall) {
    MFunction fn{};
    fn.name = "call_liveout";

    MBasicBlock entry{};
    entry.label = ".L_call_liveout_entry";
    const Operand live = makeVRegOperand(RegClass::GPR, 1);
    entry.instructions = {MInstr::make(MOpcode::MOVri, {live, makeImmOperand(42)}),
                          MInstr::make(MOpcode::CALL, {makeLabelOperand("opaque_call")}),
                          MInstr::make(MOpcode::JMP, {makeLabelOperand(".L_call_liveout_join")})};

    MBasicBlock join{};
    join.label = ".L_call_liveout_join";
    const Operand tmp = makeVRegOperand(RegClass::GPR, 2);
    join.instructions = {MInstr::make(MOpcode::MOVrr, {tmp, live}), MInstr::make(MOpcode::RET)};

    fn.blocks = {entry, join};

    (void)allocate(fn, sysvTarget());

    const auto &instructions = fn.blocks.front().instructions;
    const auto callIt =
        std::find_if(instructions.begin(), instructions.end(), [](const MInstr &instr) {
            return instr.opcode == MOpcode::CALL;
        });
    ASSERT_TRUE(callIt != instructions.end());

    const auto isCallerSaved = [](PhysReg reg) {
        return reg == PhysReg::RAX || reg == PhysReg::RDI || reg == PhysReg::RSI ||
               reg == PhysReg::RDX || reg == PhysReg::RCX || reg == PhysReg::R8 ||
               reg == PhysReg::R9 || reg == PhysReg::R10 || reg == PhysReg::R11;
    };

    bool preservedBeforeCall = false;
    for (auto it = instructions.begin(); it != callIt; ++it) {
        if (it->opcode == MOpcode::MOVrr && it->operands.size() >= 2) {
            const auto *dst = std::get_if<OpReg>(&it->operands[0]);
            const auto *src = std::get_if<OpReg>(&it->operands[1]);
            if (dst && src && dst->isPhys && src->isPhys) {
                const auto dstReg = static_cast<PhysReg>(dst->idOrPhys);
                const auto srcReg = static_cast<PhysReg>(src->idOrPhys);
                preservedBeforeCall =
                    preservedBeforeCall || (isCallerSaved(srcReg) && !isCallerSaved(dstReg));
            }
        }
        if (it->opcode == MOpcode::MOVrm && it->operands.size() >= 2) {
            const auto *src = std::get_if<OpReg>(&it->operands[1]);
            if (src && src->isPhys) {
                preservedBeforeCall =
                    preservedBeforeCall || isCallerSaved(static_cast<PhysReg>(src->idOrPhys));
            }
        }
    }
    EXPECT_TRUE(preservedBeforeCall);
}

TEST(X86BackendRegressions, LivenessCfgIgnoresInternalSelectLabels) {
    MFunction fn{};
    fn.name = "local_branch_cfg";

    MBasicBlock entry{};
    entry.label = ".L_local_branch_cfg_entry";
    entry.instructions = {
        MInstr::make(MOpcode::JCC, {makeImmOperand(1), makeLabelOperand(".Llocal_false")}),
        MInstr::make(MOpcode::LABEL, {makeLabelOperand(".Llocal_false")}),
        MInstr::make(MOpcode::JMP, {makeLabelOperand(".L_local_branch_cfg_done")})};

    MBasicBlock accidental{};
    accidental.label = ".L_local_branch_cfg_accidental";
    accidental.instructions = {MInstr::make(MOpcode::RET)};

    MBasicBlock done{};
    done.label = ".L_local_branch_cfg_done";
    done.instructions = {MInstr::make(MOpcode::RET)};

    fn.blocks = {entry, accidental, done};

    ra::LivenessAnalysis liveness;
    liveness.run(fn);
    const auto &succs = liveness.successors(0);
    ASSERT_EQ(succs.size(), 1u);
    EXPECT_EQ(succs.front(), 2u);
}

TEST(X86BackendRegressions, InvalidConditionCodeThrowsDiagnosticException) {
    MFunction fn{};
    fn.name = "bad_cc";

    MBasicBlock entry{};
    entry.label = "bad_cc";
    entry.instructions = {
        MInstr::make(MOpcode::JCC, {makeImmOperand(99), makeLabelOperand(".Lbad_cc_done")})};

    MBasicBlock done{};
    done.label = ".Lbad_cc_done";
    done.instructions = {MInstr::make(MOpcode::RET)};

    fn.blocks = {entry, done};

    AsmEmitter::RoDataPool pool;
    AsmEmitter emitter(pool);
    std::ostringstream out;

    EXPECT_THROWS(emitter.emitFunction(out, fn, sysvTarget()), std::runtime_error);
}

TEST(X86BackendRegressions, Win64XmmCalleeSaveUnwindOffsetsAre16ByteAligned) {
    MFunction fn{};
    fn.name = "xmm_callee_save_align";

    MBasicBlock entry{};
    entry.label = ".L_xmm_callee_save_align_entry";
    const Operand rax = makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RAX));
    const Operand rbx = makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RBX));
    const Operand xmm0 = makePhysRegOperand(RegClass::XMM, static_cast<uint16_t>(PhysReg::XMM0));
    const Operand xmm6 = makePhysRegOperand(RegClass::XMM, static_cast<uint16_t>(PhysReg::XMM6));
    entry.instructions = {MInstr::make(MOpcode::MOVrr, {rbx, rax}),
                          MInstr::make(MOpcode::MOVSDrr, {xmm6, xmm0}),
                          MInstr::make(MOpcode::RET)};
    fn.blocks.push_back(std::move(entry));

    FrameInfo frame{};
    assignSpillSlots(fn, win64Target(), frame);
    insertPrologueEpilogue(fn, win64Target(), frame);

    bool sawXmmSave = false;
    for (const auto &op : frame.win64UnwindOps) {
        if (op.kind != Win64UnwindOpKind::SaveXmm128) {
            continue;
        }
        sawXmmSave = true;
        EXPECT_EQ(op.stackOffset % 16u, 0u);
    }
    EXPECT_TRUE(sawXmmSave);
}

TEST(X86BackendRegressions, XorImmediateZeroRemainsIdentityDuringISel) {
    MFunction fn{};
    fn.name = "xor_zero_identity";
    MBasicBlock entry{};
    entry.label = ".L_xor_zero_identity_entry";
    const Operand value = makeVRegOperand(RegClass::GPR, 1);
    entry.instructions = {MInstr::make(MOpcode::XORrr, {value, makeImmOperand(0)})};
    fn.blocks.push_back(std::move(entry));

    ISel isel(sysvTarget());
    isel.lowerArithmetic(fn);

    ASSERT_EQ(fn.blocks.front().instructions.size(), 1u);
    EXPECT_EQ(fn.blocks.front().instructions.front().opcode, MOpcode::XORri);
}

TEST(X86BackendRegressions, SelectOperandRolesIncludeTrueArm) {
    const Operand dst = makeVRegOperand(RegClass::GPR, 1);
    const Operand cond = makeVRegOperand(RegClass::GPR, 2);
    const Operand falseVal = makeVRegOperand(RegClass::GPR, 3);
    const Operand trueVal = makeVRegOperand(RegClass::GPR, 4);
    const MInstr select = MInstr::make(MOpcode::SELECT_GPR, {dst, cond, falseVal, trueVal});

    const auto [isUse, isDef] = operandRoles(select, 3);
    EXPECT_TRUE(isUse);
    EXPECT_FALSE(isDef);
}

TEST(X86BackendRegressions, GprSelectWithImmediateTrueValueLowersToBranch) {
    MFunction fn{};
    fn.name = "select_true_imm";
    MBasicBlock entry{};
    entry.label = ".L_select_true_imm_entry";
    const Operand dst = makeVRegOperand(RegClass::GPR, 1);
    const Operand cond = makeVRegOperand(RegClass::GPR, 2);
    entry.instructions = {
        MInstr::make(MOpcode::SELECT_GPR, {dst, cond, makeImmOperand(11), makeImmOperand(22)})};
    fn.blocks.push_back(std::move(entry));

    ISel isel(sysvTarget());
    isel.lowerSelect(fn);
    isel.validateSelectLowering(fn);

    const auto &instructions = fn.blocks.front().instructions;
    ASSERT_EQ(instructions.size(), 5u);
    EXPECT_EQ(instructions[0].opcode, MOpcode::TESTrr);
    EXPECT_EQ(instructions[1].opcode, MOpcode::MOVri);
    EXPECT_EQ(instructions[2].opcode, MOpcode::JCC);
    EXPECT_EQ(instructions[3].opcode, MOpcode::MOVri);
    EXPECT_EQ(instructions[4].opcode, MOpcode::LABEL);
    const auto *trueImm = std::get_if<OpImm>(&instructions[3].operands[1]);
    ASSERT_TRUE(trueImm != nullptr);
    EXPECT_EQ(trueImm->val, 22);
}

TEST(X86BackendRegressions, MulToLeaRequiresConstantDefinitionBeforeUse) {
    MFunction fn{};
    fn.name = "mul_future_const";
    MBasicBlock entry{};
    entry.label = ".L_mul_future_const_entry";
    const Operand dst = makeVRegOperand(RegClass::GPR, 1);
    const Operand factor = makeVRegOperand(RegClass::GPR, 2);
    entry.instructions = {MInstr::make(MOpcode::IMULrr, {dst, factor}),
                          MInstr::make(MOpcode::MOVri, {factor, makeImmOperand(3)})};
    fn.blocks.push_back(std::move(entry));

    ISel isel(sysvTarget());
    isel.lowerArithmetic(fn);

    const auto &block = fn.blocks.front();
    EXPECT_TRUE(blockContainsOpcode(block, MOpcode::IMULrr));
    EXPECT_TRUE(blockContainsOpcode(block, MOpcode::MOVri));
}

TEST(X86BackendRegressions, MulToLeaCountsMemoryBaseUsesOfConstantRegister) {
    MFunction fn{};
    fn.name = "mul_const_mem_use";
    MBasicBlock entry{};
    entry.label = ".L_mul_const_mem_use_entry";
    const Operand dst = makeVRegOperand(RegClass::GPR, 1);
    const Operand factor = makeVRegOperand(RegClass::GPR, 2);
    const Operand loaded = makeVRegOperand(RegClass::GPR, 3);
    entry.instructions = {MInstr::make(MOpcode::MOVri, {factor, makeImmOperand(3)}),
                          MInstr::make(MOpcode::IMULrr, {dst, factor}),
                          MInstr::make(MOpcode::MOVmr, {loaded, Operand{baseMem(factor)}})};
    fn.blocks.push_back(std::move(entry));

    ISel isel(sysvTarget());
    isel.lowerArithmetic(fn);

    const auto &block = fn.blocks.front();
    EXPECT_TRUE(blockContainsOpcode(block, MOpcode::IMULrr));
    EXPECT_TRUE(blockContainsOpcode(block, MOpcode::MOVri));
}

TEST(X86BackendRegressions, MulToLeaRejectsSelfConstantMultiply) {
    MFunction fn{};
    fn.name = "mul_self_const";
    MBasicBlock entry{};
    entry.label = ".L_mul_self_const_entry";
    const Operand value = makeVRegOperand(RegClass::GPR, 1);
    entry.instructions = {MInstr::make(MOpcode::MOVri, {value, makeImmOperand(3)}),
                          MInstr::make(MOpcode::IMULrr, {value, value})};
    fn.blocks.push_back(std::move(entry));

    ISel isel(sysvTarget());
    isel.lowerArithmetic(fn);

    const auto &block = fn.blocks.front();
    EXPECT_TRUE(blockContainsOpcode(block, MOpcode::IMULrr));
    EXPECT_TRUE(blockContainsOpcode(block, MOpcode::MOVri));
}

TEST(X86BackendRegressions, MulToLeaFoldsPriorSingleUseConstant) {
    MFunction fn{};
    fn.name = "mul_const_fold";
    MBasicBlock entry{};
    entry.label = ".L_mul_const_fold_entry";
    const Operand value = makeVRegOperand(RegClass::GPR, 1);
    const Operand factor = makeVRegOperand(RegClass::GPR, 2);
    entry.instructions = {MInstr::make(MOpcode::MOVri, {factor, makeImmOperand(5)}),
                          MInstr::make(MOpcode::IMULrr, {value, factor})};
    fn.blocks.push_back(std::move(entry));

    ISel isel(sysvTarget());
    isel.lowerArithmetic(fn);

    const auto &block = fn.blocks.front();
    EXPECT_FALSE(blockContainsOpcode(block, MOpcode::MOVri));
    EXPECT_FALSE(blockContainsOpcode(block, MOpcode::IMULrr));
    ASSERT_EQ(block.instructions.size(), 1u);
    EXPECT_EQ(block.instructions.front().opcode, MOpcode::LEA);
}

TEST(X86BackendRegressions, SibFoldRequiresAddressDefsBeforeMemoryUse) {
    MFunction fn{};
    fn.name = "sib_future_defs";
    MBasicBlock entry{};
    entry.label = ".L_sib_future_defs_entry";
    const Operand base = makeVRegOperand(RegClass::GPR, 1);
    const Operand index = makeVRegOperand(RegClass::GPR, 2);
    const Operand scaled = makeVRegOperand(RegClass::GPR, 3);
    const Operand addr = makeVRegOperand(RegClass::GPR, 4);
    const Operand loaded = makeVRegOperand(RegClass::GPR, 5);
    entry.instructions = {MInstr::make(MOpcode::MOVmr, {loaded, Operand{baseMem(addr)}}),
                          MInstr::make(MOpcode::MOVrr, {scaled, index}),
                          MInstr::make(MOpcode::SHLri, {scaled, makeImmOperand(3)}),
                          MInstr::make(MOpcode::MOVrr, {addr, base}),
                          MInstr::make(MOpcode::ADDrr, {addr, scaled})};
    fn.blocks.push_back(std::move(entry));

    ISel isel(sysvTarget());
    isel.lowerArithmetic(fn);

    const auto &block = fn.blocks.front();
    const OpMem *mem = firstMemOperand(block.instructions.front());
    ASSERT_TRUE(mem != nullptr);
    EXPECT_FALSE(mem->hasIndex);
    EXPECT_TRUE(blockContainsOpcode(block, MOpcode::SHLri));
    EXPECT_TRUE(blockContainsOpcode(block, MOpcode::ADDrr));
}

TEST(X86BackendRegressions, SibFoldUsesPostDefReadCounts) {
    MFunction fn{};
    fn.name = "sib_fold";
    MBasicBlock entry{};
    entry.label = ".L_sib_fold_entry";
    const Operand base = makeVRegOperand(RegClass::GPR, 1);
    const Operand index = makeVRegOperand(RegClass::GPR, 2);
    const Operand scaled = makeVRegOperand(RegClass::GPR, 3);
    const Operand addr = makeVRegOperand(RegClass::GPR, 4);
    const Operand loaded = makeVRegOperand(RegClass::GPR, 5);
    entry.instructions = {MInstr::make(MOpcode::MOVrr, {scaled, index}),
                          MInstr::make(MOpcode::SHLri, {scaled, makeImmOperand(3)}),
                          MInstr::make(MOpcode::MOVrr, {addr, base}),
                          MInstr::make(MOpcode::ADDrr, {addr, scaled}),
                          MInstr::make(MOpcode::MOVmr, {loaded, Operand{baseMem(addr)}})};
    fn.blocks.push_back(std::move(entry));

    ISel isel(sysvTarget());
    isel.lowerArithmetic(fn);

    const auto &block = fn.blocks.front();
    ASSERT_EQ(block.instructions.size(), 1u);
    EXPECT_EQ(block.instructions.front().opcode, MOpcode::MOVmr);
    const OpMem *mem = firstMemOperand(block.instructions.front());
    ASSERT_TRUE(mem != nullptr);
    EXPECT_TRUE(mem->hasIndex);
    EXPECT_EQ(mem->scale, 8u);
    EXPECT_EQ(mem->base.idOrPhys, 1u);
    EXPECT_EQ(mem->index.idOrPhys, 2u);
}

TEST(X86BackendRegressions, LeaFoldRequiresDefinitionBeforeMemoryUse) {
    MFunction fn{};
    fn.name = "lea_future_def";
    MBasicBlock entry{};
    entry.label = ".L_lea_future_def_entry";
    const Operand base = makeVRegOperand(RegClass::GPR, 1);
    const Operand index = makeVRegOperand(RegClass::GPR, 2);
    const Operand addr = makeVRegOperand(RegClass::GPR, 3);
    const Operand loaded = makeVRegOperand(RegClass::GPR, 4);
    entry.instructions = {MInstr::make(MOpcode::MOVmr, {loaded, Operand{baseMem(addr)}}),
                          MInstr::make(MOpcode::LEA, {addr, Operand{indexedMem(base, index, 4)}})};
    fn.blocks.push_back(std::move(entry));

    ISel isel(sysvTarget());
    isel.lowerArithmetic(fn);

    const auto &block = fn.blocks.front();
    const OpMem *mem = firstMemOperand(block.instructions.front());
    ASSERT_TRUE(mem != nullptr);
    EXPECT_FALSE(mem->hasIndex);
    EXPECT_TRUE(blockContainsOpcode(block, MOpcode::LEA));
}

TEST(X86BackendRegressions, LeaFoldUsesRoleAwareCountsAndDeferredErase) {
    MFunction fn{};
    fn.name = "lea_fold";
    MBasicBlock entry{};
    entry.label = ".L_lea_fold_entry";
    const Operand base = makeVRegOperand(RegClass::GPR, 1);
    const Operand index = makeVRegOperand(RegClass::GPR, 2);
    const Operand addr = makeVRegOperand(RegClass::GPR, 3);
    const Operand loaded = makeVRegOperand(RegClass::GPR, 4);
    entry.instructions = {
        MInstr::make(MOpcode::LEA, {addr, Operand{indexedMem(base, index, 4, 16)}}),
        MInstr::make(MOpcode::MOVmr, {loaded, Operand{baseMem(addr)}})};
    fn.blocks.push_back(std::move(entry));

    ISel isel(sysvTarget());
    isel.lowerArithmetic(fn);

    const auto &block = fn.blocks.front();
    ASSERT_EQ(block.instructions.size(), 1u);
    EXPECT_EQ(block.instructions.front().opcode, MOpcode::MOVmr);
    const OpMem *mem = firstMemOperand(block.instructions.front());
    ASSERT_TRUE(mem != nullptr);
    EXPECT_TRUE(mem->hasIndex);
    EXPECT_EQ(mem->scale, 4u);
    EXPECT_EQ(mem->disp, 16);
    EXPECT_EQ(mem->base.idOrPhys, 1u);
    EXPECT_EQ(mem->index.idOrPhys, 2u);
}

TEST(X86BackendRegressions, FlagScanStopsAtSharedFlagDefiners) {
    const Operand dst = makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RAX));
    const Operand xmm0 = makePhysRegOperand(RegClass::XMM, static_cast<uint16_t>(PhysReg::XMM0));
    const Operand xmm1 = makePhysRegOperand(RegClass::XMM, static_cast<uint16_t>(PhysReg::XMM1));
    const std::vector<MInstr> instructions = {
        MInstr::make(MOpcode::MOVri, {dst, makeImmOperand(0)}),
        MInstr::make(MOpcode::UCOMIS, {xmm0, xmm1}),
        MInstr::make(MOpcode::JCC, {makeImmOperand(0), makeLabelOperand(".L_done")})};

    EXPECT_FALSE(peephole::nextInstrReadsFlags(instructions, 0));
}

TEST(X86BackendRegressions, ImmediateShiftCountsAreMaskedModulo64) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    entry.instrs = {op("shl", {val(ILValue::Kind::I64, 0), imm(65)}, 1, ILValue::Kind::I64),
                    op("ashr", {val(ILValue::Kind::I64, 1), imm(-1)}, 2, ILValue::Kind::I64),
                    op("ret", {val(ILValue::Kind::I64, 2)})};

    ILFunction fn{};
    fn.name = "shift_mask";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find("shlq $1"), std::string::npos);
    EXPECT_NE(result.asmText.find("sarq $63"), std::string::npos);
}

TEST(X86BackendRegressions, IndexedStoreUsesStoreDisplacementOperand) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1};
    entry.paramKinds = {ILValue::Kind::PTR, ILValue::Kind::I64};
    entry.instrs = {
        op("shl", {val(ILValue::Kind::I64, 1), imm(3)}, 2, ILValue::Kind::I64),
        op("add", {val(ILValue::Kind::PTR, 0), val(ILValue::Kind::I64, 2)}, 3, ILValue::Kind::PTR),
        op("store", {val(ILValue::Kind::PTR, 3), imm(42), imm(16)}),
        op("ret", {imm(0)})};

    ILFunction fn{};
    fn.name = "indexed_store_disp";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    ASSERT_TRUE(result.errors.empty());
    EXPECT_TRUE(containsRegex(result.asmText, R"(16\(%r[a-z0-9]+,%r[a-z0-9]+,8\))"));
    EXPECT_FALSE(containsRegex(result.asmText, R"(42\(%r[a-z0-9]+,%r[a-z0-9]+,8\))"));
}

TEST(X86BackendRegressions, GepMaterializesImmediateBase) {
    ILValue nullBase{};
    nullBase.kind = ILValue::Kind::PTR;
    nullBase.id = -1;
    nullBase.i64 = 0;

    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("gep", {nullBase, imm(8)}, 1, ILValue::Kind::PTR),
                    op("ret", {val(ILValue::Kind::PTR, 1)})};

    ILFunction fn{};
    fn.name = "gep_null";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    ASSERT_TRUE(result.errors.empty());
    EXPECT_TRUE(containsRegex(result.asmText, R"(leaq\s+8\(%r[a-z0-9]+\),\s*%r[a-z0-9]+)"));
}

TEST(X86BackendRegressions, LeaFoldCombinesOuterDisplacement) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::PTR};
    entry.instrs = {op("gep", {val(ILValue::Kind::PTR, 0), imm(8)}, 1, ILValue::Kind::PTR),
                    op("load", {val(ILValue::Kind::PTR, 1), imm(4)}, 2, ILValue::Kind::I64),
                    op("ret", {val(ILValue::Kind::I64, 2)})};

    ILFunction fn{};
    fn.name = "lea_fold_disp";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    ASSERT_TRUE(result.errors.empty());
    EXPECT_TRUE(containsRegex(result.asmText, R"(movq\s+12\(%r[a-z0-9]+\),\s*%r[a-z0-9]+)"));
}

TEST(X86BackendRegressions, LeaFoldPreservesOuterIndex) {
    MFunction fn{};
    fn.name = "lea_fold_index";

    MBasicBlock entry{};
    entry.label = ".L_lea_fold_index_entry";
    const Operand base = makeVRegOperand(RegClass::GPR, 1);
    const Operand index = makeVRegOperand(RegClass::GPR, 2);
    const Operand addr = makeVRegOperand(RegClass::GPR, 3);
    const Operand loaded = makeVRegOperand(RegClass::GPR, 4);
    entry.instructions = {
        MInstr::make(MOpcode::LEA, {addr, Operand{baseMem(base, 8)}}),
        MInstr::make(MOpcode::MOVmr, {loaded, Operand{indexedMem(addr, index, 2, 4)}})};
    fn.blocks.push_back(std::move(entry));

    ISel isel(sysvTarget());
    isel.lowerArithmetic(fn);

    const auto &block = fn.blocks.front();
    ASSERT_EQ(block.instructions.size(), 1u);
    const OpMem *mem = firstMemOperand(block.instructions.front());
    ASSERT_TRUE(mem != nullptr);
    EXPECT_TRUE(mem->hasIndex);
    EXPECT_EQ(mem->disp, 12);
    EXPECT_EQ(mem->scale, 2u);
    EXPECT_EQ(mem->base.idOrPhys, 1u);
    EXPECT_EQ(mem->index.idOrPhys, 2u);
}

TEST(X86BackendRegressions, LeaFoldDoesNotEraseAddressUsedInLaterBlock) {
    MFunction fn{};
    fn.name = "lea_cross_block";

    MBasicBlock entry{};
    entry.label = ".L_lea_cross_block_entry";
    const Operand base = makeVRegOperand(RegClass::GPR, 1);
    const Operand addr = makeVRegOperand(RegClass::GPR, 2);
    const Operand value = makeVRegOperand(RegClass::GPR, 3);
    const Operand loaded = makeVRegOperand(RegClass::GPR, 4);
    entry.instructions = {
        MInstr::make(MOpcode::LEA, {addr, Operand{baseMem(base, 8)}}),
        MInstr::make(MOpcode::MOVrm, {Operand{baseMem(addr)}, value}),
        MInstr::make(MOpcode::JMP, {makeLabelOperand(".L_lea_cross_block_next")})};

    MBasicBlock next{};
    next.label = ".L_lea_cross_block_next";
    next.instructions = {MInstr::make(MOpcode::MOVmr, {loaded, Operand{baseMem(addr)}})};

    fn.blocks = {std::move(entry), std::move(next)};

    ISel isel(sysvTarget());
    isel.lowerArithmetic(fn);

    ASSERT_EQ(fn.blocks.front().instructions.size(), 3u);
    EXPECT_EQ(fn.blocks.front().instructions.front().opcode, MOpcode::LEA);
    const OpMem *entryMem = firstMemOperand(fn.blocks.front().instructions[1]);
    ASSERT_TRUE(entryMem != nullptr);
    EXPECT_EQ(entryMem->base.idOrPhys, 2u);
    const OpMem *nextMem = firstMemOperand(fn.blocks.back().instructions.front());
    ASSERT_TRUE(nextMem != nullptr);
    EXPECT_EQ(nextMem->base.idOrPhys, 2u);
}

TEST(X86BackendRegressions, UremLargePowerOfTwoMaskMaterializesBeforeBinaryEmission) {
    constexpr int64_t kLargePowerOfTwo = 1LL << 40;

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    entry.instrs = {
        op("urem", {val(ILValue::Kind::I64, 0), imm(kLargePowerOfTwo)}, 1, ILValue::Kind::I64),
        op("ret", {val(ILValue::Kind::I64, 1)})};

    ILFunction fn{};
    fn.name = "urem_large_mask";
    fn.blocks = {entry};

    const CodegenResult asmResult = compile(fn);
    ASSERT_TRUE(asmResult.errors.empty());
    EXPECT_EQ(asmResult.asmText.find("andq $1099511627775"), std::string::npos);
    EXPECT_NE(asmResult.asmText.find("andq %r11"), std::string::npos);

    const BinaryEmitResult binaryResult = compileBinary(fn);
    EXPECT_TRUE(binaryResult.errors.empty());
}

TEST(X86BackendRegressions, ExistingOverflowTrapBlockGetsRuntimeCall) {
    MFunction fn{};
    fn.name = "ovf_reuse";

    MBasicBlock entry{};
    entry.label = ".L_ovf_reuse_entry";
    const Operand lhs = makeVRegOperand(RegClass::GPR, 1);
    const Operand rhs = makeVRegOperand(RegClass::GPR, 2);
    entry.instructions = {MInstr::make(MOpcode::ADDOvfrr, {lhs, rhs})};

    MBasicBlock trap{};
    trap.label = ".Ltrap_ovf_ovf_reuse";
    trap.instructions = {MInstr::make(MOpcode::UD2)};

    fn.blocks = {entry, trap};
    lowerOverflowOps(fn);

    const auto trapIt = std::find_if(fn.blocks.begin(), fn.blocks.end(), [](const MBasicBlock &bb) {
        return bb.label == ".Ltrap_ovf_ovf_reuse";
    });
    ASSERT_TRUE(trapIt != fn.blocks.end());

    const auto callIt = std::find_if(
        trapIt->instructions.begin(), trapIt->instructions.end(), [](const MInstr &instr) {
            if (instr.opcode != MOpcode::CALL || instr.operands.empty())
                return false;
            const auto *label = std::get_if<OpLabel>(&instr.operands.front());
            return label && label->name == "rt_trap_ovf";
        });
    const auto ud2It =
        std::find_if(trapIt->instructions.begin(),
                     trapIt->instructions.end(),
                     [](const MInstr &instr) { return instr.opcode == MOpcode::UD2; });
    ASSERT_TRUE(callIt != trapIt->instructions.end());
    ASSERT_TRUE(ud2It != trapIt->instructions.end());
    EXPECT_LT(std::distance(trapIt->instructions.begin(), callIt),
              std::distance(trapIt->instructions.begin(), ud2It));
}

TEST(X86BackendRegressions, LabelLiteralEdgeArgsMaterializeAsPointers) {
    AsmEmitter::RoDataPool roData;
    LowerILToMIR lowering(sysvTarget(), roData);

    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("br", {label("target")})};
    entry.terminatorEdges = {
        ILBlock::EdgeArg{.to = "target", .argIds = {-1}, .argValues = {label("global_target")}}};

    ILBlock target{};
    target.name = "target";
    target.paramIds = {0};
    target.paramKinds = {ILValue::Kind::PTR};
    target.instrs = {op("ret", {val(ILValue::Kind::PTR, 0)})};

    ILFunction fn{};
    fn.name = "label_edge";
    fn.blocks = {entry, target};

    const MFunction mir = lowering.lower(fn);

    bool foundLabelLea = false;
    bool foundBadZeroMove = false;
    for (const auto &block : mir.blocks) {
        if (block.label.find(".edge") == std::string::npos)
            continue;
        for (const auto &instr : block.instructions) {
            if (instr.opcode == MOpcode::LEA && instr.operands.size() >= 2) {
                const auto *rip = std::get_if<OpRipLabel>(&instr.operands[1]);
                foundLabelLea = foundLabelLea || (rip && rip->name == "global_target");
            }
            if (instr.opcode == MOpcode::MOVri && instr.operands.size() >= 2) {
                const auto *immediate = std::get_if<OpImm>(&instr.operands[1]);
                foundBadZeroMove = foundBadZeroMove || (immediate && immediate->val == 0);
            }
        }
    }

    EXPECT_TRUE(foundLabelLea);
    EXPECT_FALSE(foundBadZeroMove);
}

TEST(X86BackendRegressions, XmmSelectBranchArmIsSplitBeforeRegAlloc) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I1};
    entry.instrs = {
        op("const_f64", {immF64(1.5)}, 1, ILValue::Kind::F64),
        op("const_f64", {immF64(2.5)}, 2, ILValue::Kind::F64),
        op("select",
           {val(ILValue::Kind::I1, 0), val(ILValue::Kind::F64, 1), val(ILValue::Kind::F64, 2)},
           3,
           ILValue::Kind::F64),
        op("ret", {val(ILValue::Kind::F64, 3)}, -1, ILValue::Kind::F64)};

    ILFunction fn{};
    fn.name = "xmm_select_split";
    fn.blocks = {entry};

    ILModule module{};
    module.funcs = {fn};

    AsmEmitter::RoDataPool roData;
    std::vector<MFunction> mir;
    std::vector<FrameInfo> frames;
    std::string errors;
    CodegenOptions options;
    ASSERT_TRUE(legalizeModuleToMIR(module, sysvTarget(), options, roData, mir, frames, errors));
    ASSERT_TRUE(errors.empty());
    ASSERT_EQ(mir.size(), 1u);

    bool foundExplicitFalseEdge = false;
    for (const auto &block : mir.front().blocks) {
        EXPECT_FALSE(blockHasNonTerminatorAfterControlTransfer(block));
        foundExplicitFalseEdge =
            foundExplicitFalseEdge || blockHasExplicitConditionalFallthrough(block);
    }
    EXPECT_TRUE(foundExplicitFalseEdge);
}

TEST(X86BackendRegressions, LivenessCfgTracksNonAdjacentJccBeforeFinalJump) {
    MFunction fn{};
    fn.name = "non_adjacent_jcc";

    MBasicBlock entry{};
    entry.label = ".L_non_adjacent_jcc_entry";
    const Operand dst = makeVRegOperand(RegClass::GPR, 1);
    const Operand src = makeVRegOperand(RegClass::GPR, 2);
    entry.instructions = {
        MInstr::make(MOpcode::JCC, {makeImmOperand(1), makeLabelOperand(".L_true")}),
        MInstr::make(MOpcode::MOVrr, {dst, src}),
        MInstr::make(MOpcode::JMP, {makeLabelOperand(".L_false")})};

    MBasicBlock trueBlock{};
    trueBlock.label = ".L_true";
    trueBlock.instructions = {MInstr::make(MOpcode::RET)};

    MBasicBlock falseBlock{};
    falseBlock.label = ".L_false";
    falseBlock.instructions = {MInstr::make(MOpcode::RET)};

    fn.blocks = {entry, trueBlock, falseBlock};

    ra::LivenessAnalysis liveness;
    liveness.run(fn);
    const auto &succs = liveness.successors(0);
    ASSERT_EQ(succs.size(), 2u);
    EXPECT_NE(std::find(succs.begin(), succs.end(), 1u), succs.end());
    EXPECT_NE(std::find(succs.begin(), succs.end(), 2u), succs.end());
}

TEST(X86BackendRegressions, Win64RawRuntimeCallsReserveShadowSpace) {
    MFunction fn{};
    fn.name = "raw_trap_call";

    MBasicBlock entry{};
    entry.label = ".L_raw_trap_call_entry";
    entry.instructions = {MInstr::make(MOpcode::CALL, {makeLabelOperand("rt_trap_ovf")}),
                          MInstr::make(MOpcode::UD2)};
    fn.blocks.push_back(std::move(entry));

    FrameInfo frame{};
    assignSpillSlots(fn, win64Target(), frame);
    EXPECT_GE(frame.outgoingArgArea, 32);
}

TEST(X86BackendRegressions, I1ImmediateOperandsCanonicalizeToZeroOrOne) {
    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("zext", {immI1(7)}, 0, ILValue::Kind::I64),
                    op("ret", {val(ILValue::Kind::I64, 0)})};

    ILFunction fn{};
    fn.name = "i1_imm_zext";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find("$1"), std::string::npos);
    EXPECT_EQ(result.asmText.find("$7"), std::string::npos);
}

TEST(X86BackendRegressions, I1ImmediateCallArgsCanonicalizeToZeroOrOne) {
    AsmEmitter::RoDataPool roData;
    LowerILToMIR lowering(sysvTarget(), roData);

    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("call", {label("sink"), immI1(-3)}), op("ret", {imm(0)})};

    ILFunction fn{};
    fn.name = "i1_call_arg";
    fn.blocks = {entry};

    (void)lowering.lower(fn);
    ASSERT_EQ(lowering.callPlans().size(), 1u);
    ASSERT_EQ(lowering.callPlans().front().args.size(), 1u);
    EXPECT_TRUE(lowering.callPlans().front().args[0].isImm);
    EXPECT_EQ(lowering.callPlans().front().args[0].imm, 1);
}

TEST(X86BackendRegressions, I1ImmediateEdgeArgsCanonicalizeToZeroOrOne) {
    AsmEmitter::RoDataPool roData;
    LowerILToMIR lowering(sysvTarget(), roData);

    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("br", {label("target")})};
    entry.terminatorEdges = {
        ILBlock::EdgeArg{.to = "target", .argIds = {-1}, .argValues = {immI1(99)}}};

    ILBlock target{};
    target.name = "target";
    target.paramIds = {0};
    target.paramKinds = {ILValue::Kind::I1};
    target.instrs = {op("ret", {val(ILValue::Kind::I1, 0)})};

    ILFunction fn{};
    fn.name = "i1_edge_arg";
    fn.blocks = {entry, target};

    const MFunction mir = lowering.lower(fn);
    bool foundCanonicalMove = false;
    bool foundRawMove = false;
    for (const auto &block : mir.blocks) {
        if (block.label.find(".edge") == std::string::npos) {
            continue;
        }
        for (const auto &instr : block.instructions) {
            if (instr.opcode != MOpcode::MOVri || instr.operands.size() < 2) {
                continue;
            }
            const auto *imm = std::get_if<OpImm>(&instr.operands[1]);
            foundCanonicalMove = foundCanonicalMove || (imm && imm->val == 1);
            foundRawMove = foundRawMove || (imm && imm->val == 99);
        }
    }

    EXPECT_TRUE(foundCanonicalMove);
    EXPECT_FALSE(foundRawMove);
}

TEST(X86BackendRegressions, BlockParameterEdgesRejectTooFewArgs) {
    AsmEmitter::RoDataPool roData;
    LowerILToMIR lowering(sysvTarget(), roData);

    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("br", {label("target")})};
    entry.terminatorEdges = {ILBlock::EdgeArg{.to = "target", .argIds = {}}};

    ILBlock target{};
    target.name = "target";
    target.paramIds = {0};
    target.paramKinds = {ILValue::Kind::I64};
    target.instrs = {op("ret", {val(ILValue::Kind::I64, 0)})};

    ILFunction fn{};
    fn.name = "edge_too_few_args";
    fn.blocks = {entry, target};

    EXPECT_THROWS(lowering.lower(fn), std::runtime_error);
}

TEST(X86BackendRegressions, BlockParameterEdgesRejectTooManyArgs) {
    AsmEmitter::RoDataPool roData;
    LowerILToMIR lowering(sysvTarget(), roData);

    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("br", {label("target")})};
    entry.terminatorEdges = {
        ILBlock::EdgeArg{.to = "target", .argIds = {-1, -1}, .argValues = {imm(1), imm(2)}}};

    ILBlock target{};
    target.name = "target";
    target.paramIds = {0};
    target.paramKinds = {ILValue::Kind::I64};
    target.instrs = {op("ret", {val(ILValue::Kind::I64, 0)})};

    ILFunction fn{};
    fn.name = "edge_too_many_args";
    fn.blocks = {entry, target};

    EXPECT_THROWS(lowering.lower(fn), std::runtime_error);
}

TEST(X86BackendRegressions, BlockParameterEdgesRejectF64ForGprParam) {
    AsmEmitter::RoDataPool roData;
    LowerILToMIR lowering(sysvTarget(), roData);

    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("br", {label("target")})};
    entry.terminatorEdges = {
        ILBlock::EdgeArg{.to = "target", .argIds = {-1}, .argValues = {immF64(1.25)}}};

    ILBlock target{};
    target.name = "target";
    target.paramIds = {0};
    target.paramKinds = {ILValue::Kind::I64};
    target.instrs = {op("ret", {val(ILValue::Kind::I64, 0)})};

    ILFunction fn{};
    fn.name = "edge_f64_to_gpr";
    fn.blocks = {entry, target};

    EXPECT_THROWS(lowering.lower(fn), std::runtime_error);
}

TEST(X86BackendRegressions, HugeStringLiteralLengthIsRejectedBeforeResize) {
    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("const_str",
                       {strLit("x", std::numeric_limits<std::uint64_t>::max())},
                       0,
                       ILValue::Kind::STR),
                    op("ret", {val(ILValue::Kind::STR, 0)})};

    ILFunction fn{};
    fn.name = "huge_string_literal";
    fn.blocks = {entry};

    EXPECT_THROWS(compile(fn), std::runtime_error);
}

TEST(X86BackendRegressions, LoadRejectsF64Displacement) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::PTR};
    entry.instrs = {op("load", {val(ILValue::Kind::PTR, 0), immF64(4.0)}, 1, ILValue::Kind::I64),
                    op("ret", {val(ILValue::Kind::I64, 1)})};

    ILFunction fn{};
    fn.name = "load_f64_disp";
    fn.blocks = {entry};

    EXPECT_THROWS(compile(fn), std::runtime_error);
}

TEST(X86BackendRegressions, StoreRejectsF64Displacement) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::PTR};
    entry.instrs = {op("store", {val(ILValue::Kind::PTR, 0), imm(7), immF64(4.0)}),
                    op("ret", {imm(0)})};

    ILFunction fn{};
    fn.name = "store_f64_disp";
    fn.blocks = {entry};

    EXPECT_THROWS(compile(fn), std::runtime_error);
}

TEST(X86BackendRegressions, AllocaRejectsF64Size) {
    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("alloca", {immF64(8.0)}, 0, ILValue::Kind::PTR), op("ret", {imm(0)})};

    ILFunction fn{};
    fn.name = "alloca_f64_size";
    fn.blocks = {entry};

    EXPECT_THROWS(compile(fn), std::runtime_error);
}

TEST(X86BackendRegressions, GepRejectsF64ImmediateOffset) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::PTR};
    entry.instrs = {op("gep", {val(ILValue::Kind::PTR, 0), immF64(8.0)}, 1, ILValue::Kind::PTR),
                    op("ret", {val(ILValue::Kind::PTR, 1)})};

    ILFunction fn{};
    fn.name = "gep_f64_offset";
    fn.blocks = {entry};

    EXPECT_THROWS(compile(fn), std::runtime_error);
}

TEST(X86BackendRegressions, ReusedSsaIdWithDifferentRegisterClassIsRejected) {
    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("add", {imm(1), imm(2)}, 0, ILValue::Kind::I64),
                    op("fadd", {val(ILValue::Kind::F64, 0), immF64(1.0)}, 1, ILValue::Kind::F64),
                    op("ret", {imm(0)})};

    ILFunction fn{};
    fn.name = "ssa_type_reuse";
    fn.blocks = {entry};

    EXPECT_THROWS(compile(fn), std::runtime_error);
}

TEST(X86BackendRegressions, BranchEmitterRejectsNonLabelTarget) {
    AsmEmitter::RoDataPool roData;
    LowerILToMIR lowering(sysvTarget(), roData);
    MBasicBlock block{};
    block.label = ".L_bad_branch";
    MIRBuilder builder(lowering, block);

    ILInstr instr = op("br", {imm(0)});

    EXPECT_THROWS(EmitCommon(builder).emitBranch(instr), std::runtime_error);
}

TEST(X86BackendRegressions, CondBranchEmitterRejectsNonLabelTarget) {
    AsmEmitter::RoDataPool roData;
    LowerILToMIR lowering(sysvTarget(), roData);
    MBasicBlock block{};
    block.label = ".L_bad_cbr";
    MIRBuilder builder(lowering, block);

    ILInstr instr = op("cbr", {val(ILValue::Kind::I1, 0), label("yes"), imm(0)});

    EXPECT_THROWS(EmitCommon(builder).emitCondBranch(instr), std::runtime_error);
}

TEST(X86BackendRegressions, CondBranchRejectsF64Condition) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::F64};
    entry.instrs = {op("cbr", {val(ILValue::Kind::F64, 0), label("yes"), label("no")})};

    ILBlock yes{};
    yes.name = "yes";
    yes.instrs = {op("ret", {imm(1)})};

    ILBlock no{};
    no.name = "no";
    no.instrs = {op("ret", {imm(0)})};

    ILFunction fn{};
    fn.name = "cbr_f64_condition";
    fn.blocks = {entry, yes, no};

    EXPECT_THROWS(compile(fn), std::runtime_error);
}

TEST(X86BackendRegressions, SelectRejectsF64Condition) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::F64};
    entry.instrs = {op("select", {val(ILValue::Kind::F64, 0), imm(1), imm(2)}, 1),
                    op("ret", {val(ILValue::Kind::I64, 1)})};

    ILFunction fn{};
    fn.name = "select_f64_condition";
    fn.blocks = {entry};

    EXPECT_THROWS(compile(fn), std::runtime_error);
}

TEST(X86BackendRegressions, IndirectCallRejectsF64TargetRegister) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::F64};
    entry.instrs = {op("call.indirect", {val(ILValue::Kind::F64, 0)}), op("ret", {imm(0)})};

    ILFunction fn{};
    fn.name = "call_indirect_f64_target";
    fn.blocks = {entry};

    EXPECT_THROWS(compile(fn), std::runtime_error);
}

TEST(X86BackendRegressions, FpAddRejectsGprOperand) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    entry.instrs = {op("add", {val(ILValue::Kind::I64, 0), immF64(1.0)}, 1, ILValue::Kind::F64),
                    op("ret", {val(ILValue::Kind::F64, 1)}, -1, ILValue::Kind::F64)};

    ILFunction fn{};
    fn.name = "fp_add_gpr_operand";
    fn.blocks = {entry};

    EXPECT_THROWS(compile(fn), std::runtime_error);
}

TEST(X86BackendRegressions, FcmpEqRejectsGprOperand) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1};
    entry.paramKinds = {ILValue::Kind::I64, ILValue::Kind::F64};
    entry.instrs = {
        op("fcmp_eq", {val(ILValue::Kind::I64, 0), val(ILValue::Kind::F64, 1)}, 2, ILValue::Kind::I1),
        op("ret", {val(ILValue::Kind::I1, 2)})};

    ILFunction fn{};
    fn.name = "fcmp_eq_gpr_operand";
    fn.blocks = {entry};

    EXPECT_THROWS(compile(fn), std::runtime_error);
}

TEST(X86BackendRegressions, FcmpGtRejectsGprOperand) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1};
    entry.paramKinds = {ILValue::Kind::I64, ILValue::Kind::F64};
    entry.instrs = {
        op("fcmp_gt", {val(ILValue::Kind::I64, 0), val(ILValue::Kind::F64, 1)}, 2, ILValue::Kind::I1),
        op("ret", {val(ILValue::Kind::I1, 2)})};

    ILFunction fn{};
    fn.name = "fcmp_gt_gpr_operand";
    fn.blocks = {entry};

    EXPECT_THROWS(compile(fn), std::runtime_error);
}

TEST(X86BackendRegressions, SwitchRejectsMissingDefaultLabel) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    entry.instrs = {op("switch_i32", {val(ILValue::Kind::I64, 0), imm(1)})};

    ILFunction fn{};
    fn.name = "switch_missing_default_label";
    fn.blocks = {entry};

    EXPECT_THROWS(compile(fn), std::runtime_error);
}

TEST(X86BackendRegressions, SwitchRejectsNonIntegerCaseValue) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    entry.instrs = {
        op("switch_i32", {val(ILValue::Kind::I64, 0), immF64(1.0), label("case1"), label("def")})};

    ILFunction fn{};
    fn.name = "switch_f64_case_value";
    fn.blocks = {entry};

    EXPECT_THROWS(compile(fn), std::runtime_error);
}

TEST(X86BackendRegressions, SwitchRejectsDanglingCaseValue) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    entry.instrs = {op("switch_i32", {val(ILValue::Kind::I64, 0), imm(1), label("def")})};

    ILFunction fn{};
    fn.name = "switch_dangling_case_value";
    fn.blocks = {entry};

    EXPECT_THROWS(compile(fn), std::runtime_error);
}

TEST(X86BackendRegressions, ISelRejectsReservedTemporaryVregSentinel) {
    MFunction fn{};
    fn.name = "isel_reserved_temp";

    MBasicBlock entry{};
    entry.label = ".L_isel_reserved_temp";
    const Operand dst =
        makeVRegOperand(RegClass::GPR, static_cast<uint16_t>(std::numeric_limits<uint16_t>::max() - 1));
    entry.instructions = {MInstr::make(MOpcode::ADDrr, {dst, makeImmOperand(1LL << 40)})};
    fn.blocks = {entry};

    ISel isel(sysvTarget());
    EXPECT_THROWS(isel.lowerArithmetic(fn), std::runtime_error);
}

TEST(X86BackendRegressions, IndexedMemReconstructionRejectsShiftWithoutOriginalIndexCopy) {
    AsmEmitter::RoDataPool roData;
    LowerILToMIR lowering(sysvTarget(), roData);
    MBasicBlock block{};
    block.label = ".L_indexed_mem_no_copy";
    MIRBuilder builder(lowering, block);

    const VReg base = builder.ensureVReg(0, ILValue::Kind::PTR);
    const VReg index = builder.ensureVReg(1, ILValue::Kind::I64);
    const VReg addr = builder.ensureVReg(2, ILValue::Kind::PTR);
    const Operand baseOp = makeVRegOperand(base.cls, base.id);
    const Operand indexOp = makeVRegOperand(index.cls, index.id);
    const Operand addrOp = makeVRegOperand(addr.cls, addr.id);

    block.instructions = {MInstr::make(MOpcode::MOVrr, {addrOp, baseOp}),
                          MInstr::make(MOpcode::SHLri, {indexOp, makeImmOperand(3)}),
                          MInstr::make(MOpcode::ADDrr, {addrOp, indexOp})};

    const ILInstr load = op("load", {val(ILValue::Kind::PTR, 2), imm(0)}, 3, ILValue::Kind::I64);
    const std::optional<Operand> mem = EmitCommon(builder).tryMakeIndexedMem(load, 1);
    EXPECT_FALSE(mem.has_value());
}

TEST(X86BackendRegressions, LargeAluAndCompareImmediatesMaterializeBeforeEmission) {
    MFunction fn{};
    fn.name = "large_alu_immediates";

    MBasicBlock entry{};
    entry.label = ".L_large_alu_immediates_entry";
    const int64_t big = 1LL << 40;
    entry.instructions = {
        MInstr::make(MOpcode::ADDrr, {makeVRegOperand(RegClass::GPR, 1), makeImmOperand(big)}),
        MInstr::make(MOpcode::SUBrr,
                     {makeVRegOperand(RegClass::GPR, 2),
                      makeImmOperand(std::numeric_limits<int64_t>::min())}),
        MInstr::make(MOpcode::SUBrr,
                     {makeVRegOperand(RegClass::GPR, 3),
                      makeImmOperand(static_cast<int64_t>(std::numeric_limits<int32_t>::min()))}),
        MInstr::make(MOpcode::ANDrr, {makeVRegOperand(RegClass::GPR, 4), makeImmOperand(big)}),
        MInstr::make(MOpcode::ORrr, {makeVRegOperand(RegClass::GPR, 5), makeImmOperand(big)}),
        MInstr::make(MOpcode::XORrr, {makeVRegOperand(RegClass::GPR, 6), makeImmOperand(big)}),
        MInstr::make(MOpcode::CMPrr, {makeVRegOperand(RegClass::GPR, 7), makeImmOperand(big)})};
    fn.blocks.push_back(std::move(entry));

    ISel isel(sysvTarget());
    isel.lowerArithmetic(fn);
    isel.lowerCompareAndBranch(fn);

    std::size_t materialized = 0;
    for (const auto &instr : fn.blocks.front().instructions) {
        if (instr.opcode == MOpcode::MOVri) {
            ++materialized;
        }

        if (instr.operands.size() < 2 || !std::holds_alternative<OpImm>(instr.operands[1])) {
            continue;
        }

        const int64_t imm = std::get<OpImm>(instr.operands[1]).val;
        const bool fitsImm32 = imm >= std::numeric_limits<int32_t>::min() &&
                               imm <= std::numeric_limits<int32_t>::max();
        switch (instr.opcode) {
            case MOpcode::ADDri:
            case MOpcode::ANDri:
            case MOpcode::ORri:
            case MOpcode::XORri:
            case MOpcode::CMPri:
                EXPECT_TRUE(fitsImm32);
                break;
            case MOpcode::ADDrr:
            case MOpcode::SUBrr:
            case MOpcode::ANDrr:
            case MOpcode::ORrr:
            case MOpcode::XORrr:
            case MOpcode::CMPrr:
                EXPECT_TRUE(false);
                break;
            default:
                break;
        }
    }
    EXPECT_EQ(materialized, 7u);
}

TEST(X86BackendRegressions, BranchChainEliminationRetargetsJccBeforeFinalJump) {
    MFunction fn{};
    fn.name = "jcc_branch_chain";

    MBasicBlock entry{};
    entry.label = ".L_jcc_branch_chain_entry";
    entry.instructions = {
        MInstr::make(MOpcode::JCC, {makeImmOperand(1), makeLabelOperand(".L_chain")}),
        MInstr::make(MOpcode::JMP, {makeLabelOperand(".L_exit")})};

    MBasicBlock chain{};
    chain.label = ".L_chain";
    chain.instructions = {MInstr::make(MOpcode::JMP, {makeLabelOperand(".L_target")})};

    MBasicBlock target{};
    target.label = ".L_target";
    target.instructions = {MInstr::make(MOpcode::RET)};

    MBasicBlock exit{};
    exit.label = ".L_exit";
    exit.instructions = {MInstr::make(MOpcode::RET)};

    fn.blocks = {entry, chain, target, exit};

    peephole::PeepholeStats stats{};
    peephole::eliminateBranchChains(fn, stats);

    ASSERT_EQ(fn.blocks.front().instructions.front().opcode, MOpcode::JCC);
    ASSERT_GE(fn.blocks.front().instructions.front().operands.size(), 2u);
    const auto *targetLabel =
        std::get_if<OpLabel>(&fn.blocks.front().instructions.front().operands[1]);
    ASSERT_TRUE(targetLabel != nullptr);
    EXPECT_EQ(targetLabel->name, ".L_target");
    EXPECT_EQ(stats.branchChainsEliminated, 1u);
}

TEST(X86BackendRegressions, ColdBlockMovementPreservesImplicitFallthrough) {
    MFunction fn{};
    fn.name = "cold_fallthrough";

    MBasicBlock entry{};
    entry.label = ".L_cold_fallthrough_entry";
    entry.instructions = {
        MInstr::make(MOpcode::MOVri, {makeVRegOperand(RegClass::GPR, 1), makeImmOperand(1)})};

    MBasicBlock trap{};
    trap.label = ".L_cold_fallthrough_trap";
    trap.instructions = {MInstr::make(MOpcode::UD2)};

    MBasicBlock hot{};
    hot.label = ".L_cold_fallthrough_hot";
    hot.instructions = {MInstr::make(MOpcode::RET)};

    fn.blocks = {entry, trap, hot};

    peephole::PeepholeStats stats{};
    peephole::moveColdBlocks(fn, stats);

    ASSERT_EQ(fn.blocks.size(), 3u);
    EXPECT_EQ(fn.blocks[1].label, ".L_cold_fallthrough_trap");
    EXPECT_EQ(stats.coldBlocksMoved, 0u);
}

TEST(X86BackendRegressions, FrameStoreForwardingHonorsCrossClassAliases) {
    const OpReg rbp = makePhysReg(RegClass::GPR, static_cast<uint16_t>(PhysReg::RBP));
    const Operand rax = makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RAX));
    const Operand rbx = makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RBX));
    const Operand xmm0 = makePhysRegOperand(RegClass::XMM, static_cast<uint16_t>(PhysReg::XMM0));

    std::vector<MInstr> instrs = {
        MInstr::make(MOpcode::MOVrm, {makeMemOperand(rbp, -8), rax}),
        MInstr::make(MOpcode::MOVSDrm, {makeMemOperand(rbp, -8), xmm0}),
        MInstr::make(MOpcode::MOVmr, {rbx, makeMemOperand(rbp, -8)})};

    peephole::PeepholeStats stats{};
    const std::size_t forwarded = peephole::forwardFrameStoreLoads(instrs, stats);

    EXPECT_EQ(forwarded, 0u);
    ASSERT_EQ(instrs.size(), 3u);
    EXPECT_EQ(instrs[2].opcode, MOpcode::MOVmr);
}

TEST(X86BackendRegressions, DeadFrameStoreEliminationHonorsCrossClassAliases) {
    const OpReg rbp = makePhysReg(RegClass::GPR, static_cast<uint16_t>(PhysReg::RBP));
    const Operand rax = makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RAX));
    const Operand rbx = makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RBX));
    const Operand xmm0 = makePhysRegOperand(RegClass::XMM, static_cast<uint16_t>(PhysReg::XMM0));

    std::vector<MInstr> overwritten = {
        MInstr::make(MOpcode::MOVrm, {makeMemOperand(rbp, -8), rax}),
        MInstr::make(MOpcode::MOVSDrm, {makeMemOperand(rbp, -8), xmm0})};

    peephole::PeepholeStats stats{};
    EXPECT_EQ(peephole::eliminateDeadFrameStores(overwritten, stats), 1u);
    ASSERT_EQ(overwritten.size(), 1u);
    EXPECT_EQ(overwritten.front().opcode, MOpcode::MOVSDrm);

    std::vector<MInstr> observed = {
        MInstr::make(MOpcode::MOVrm, {makeMemOperand(rbp, -8), rax}),
        MInstr::make(MOpcode::MOVSDmr, {xmm0, makeMemOperand(rbp, -8)}),
        MInstr::make(MOpcode::MOVrm, {makeMemOperand(rbp, -8), rbx})};

    peephole::PeepholeStats observedStats{};
    EXPECT_EQ(peephole::eliminateDeadFrameStores(observed, observedStats), 0u);
    EXPECT_EQ(observed.size(), 3u);
}

TEST(X86BackendRegressions, FrameLoweringPreservesCalleeSavedAddressRegisters) {
    struct Case {
        const TargetInfo *target;
        PhysReg addressReg;
        bool asIndex;
        const char *name;
    };

    const std::array<Case, 10> cases{{
        {&sysvTarget(), PhysReg::RBX, false, "sysv_rbx_base"},
        {&sysvTarget(), PhysReg::R12, true, "sysv_r12_index"},
        {&sysvTarget(), PhysReg::R13, false, "sysv_r13_base"},
        {&sysvTarget(), PhysReg::R14, true, "sysv_r14_index"},
        {&sysvTarget(), PhysReg::R15, false, "sysv_r15_base"},
        {&win64Target(), PhysReg::RBX, false, "win64_rbx_base"},
        {&win64Target(), PhysReg::RDI, true, "win64_rdi_index"},
        {&win64Target(), PhysReg::RSI, false, "win64_rsi_base"},
        {&win64Target(), PhysReg::R12, true, "win64_r12_index"},
        {&win64Target(), PhysReg::R15, false, "win64_r15_base"},
    }};

    for (const auto &testCase : cases) {
        MFunction fn{};
        fn.name = testCase.name;

        MBasicBlock entry{};
        entry.label = std::string(".L_") + testCase.name;

        const Operand dst =
            makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RAX));
        const OpReg address =
            makePhysReg(RegClass::GPR, static_cast<uint16_t>(testCase.addressReg));
        const Operand mem =
            testCase.asIndex
                ? makeMemOperand(makePhysReg(RegClass::GPR, static_cast<uint16_t>(PhysReg::RAX)),
                                 address,
                                 2,
                                 16)
                : makeMemOperand(address, 16);
        entry.instructions = {MInstr::make(MOpcode::MOVmr, {dst, mem}), MInstr::make(MOpcode::RET)};
        fn.blocks.push_back(std::move(entry));

        FrameInfo frame{};
        assignSpillSlots(fn, *testCase.target, frame);

        EXPECT_TRUE(std::find(frame.usedCalleeSaved.begin(),
                              frame.usedCalleeSaved.end(),
                              testCase.addressReg) != frame.usedCalleeSaved.end());
    }
}

TEST(X86BackendRegressions, LocalLabelsAreUniqueAcrossFunctions) {
    MFunction first{};
    first.name = "select_label_first";
    MFunction second{};
    second.name = "select_label_second";

    const std::string firstGprLabel = first.makeLocalLabel(".Lselect_gpr_done");
    const std::string secondGprLabel = second.makeLocalLabel(".Lselect_gpr_done");
    EXPECT_NE(firstGprLabel, secondGprLabel);
    EXPECT_NE(firstGprLabel.find("select_label_first"), std::string::npos);
    EXPECT_NE(secondGprLabel.find("select_label_second"), std::string::npos);

    const std::string firstFalseLabel = first.makeLocalLabel(".Lfalse");
    const std::string secondFalseLabel = second.makeLocalLabel(".Lfalse");
    EXPECT_NE(firstFalseLabel, secondFalseLabel);

    const std::string firstSplitLabel = first.makeLocalLabel(".Lsplit");
    const std::string secondSplitLabel = second.makeLocalLabel(".Lsplit");
    EXPECT_NE(firstSplitLabel, secondSplitLabel);
}

TEST(X86BackendRegressions, ZextMasksToSourceBitWidth) {
    ILValue source = val(ILValue::Kind::I64, 0);
    source.bits = 8;

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    entry.instrs = {op("zext", {source}, 1, ILValue::Kind::I64),
                    op("ret", {val(ILValue::Kind::I64, 1)})};

    ILFunction fn{};
    fn.name = "zext_i8";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    if (!result.errors.empty()) {
        std::cerr << result.errors << '\n';
    }
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find("andq $255"), std::string::npos);
}

TEST(X86BackendRegressions, SextSignExtendsFromSourceBitWidth) {
    ILValue source = val(ILValue::Kind::I64, 0);
    source.bits = 8;

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    entry.instrs = {op("sext", {source}, 1, ILValue::Kind::I64),
                    op("ret", {val(ILValue::Kind::I64, 1)})};

    ILFunction fn{};
    fn.name = "sext_i8";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    if (!result.errors.empty()) {
        std::cerr << result.errors << '\n';
    }
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find("shlq $56"), std::string::npos);
    EXPECT_NE(result.asmText.find("sarq $56"), std::string::npos);
}

TEST(X86BackendRegressions, TruncMasksBooleanResultWidth) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    entry.instrs = {opBits("trunc",
                           {val(ILValue::Kind::I64, 0)},
                           1,
                           ILValue::Kind::I1,
                           1),
                    op("ret", {val(ILValue::Kind::I1, 1)})};

    ILFunction fn{};
    fn.name = "trunc_i1";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    if (!result.errors.empty()) {
        std::cerr << result.errors << '\n';
    }
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find("andq $1"), std::string::npos);
}

TEST(X86BackendRegressions, SelectTrueLabelArmMaterializesBeforeISel) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I1};
    entry.instrs = {op("select",
                       {val(ILValue::Kind::I1, 0), label("select_true_symbol"), immPtr(0)},
                       1,
                       ILValue::Kind::PTR),
                    op("ret", {val(ILValue::Kind::PTR, 1)})};

    ILFunction fn{};
    fn.name = "select_true_label";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    if (!result.errors.empty()) {
        std::cerr << result.errors << '\n';
    }
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find("leaq select_true_symbol(%rip)"), std::string::npos);
    EXPECT_EQ(result.asmText.find("select pseudo survived"), std::string::npos);
}

TEST(X86BackendRegressions, SelectFalseLabelArmMaterializesBeforeISel) {
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I1};
    entry.instrs = {op("select",
                       {val(ILValue::Kind::I1, 0), immPtr(0), label("select_false_symbol")},
                       1,
                       ILValue::Kind::PTR),
                    op("ret", {val(ILValue::Kind::PTR, 1)})};

    ILFunction fn{};
    fn.name = "select_false_label";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    if (!result.errors.empty()) {
        std::cerr << result.errors << '\n';
    }
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find("leaq select_false_symbol(%rip)"), std::string::npos);
    EXPECT_EQ(result.asmText.find("select pseudo survived"), std::string::npos);
}

TEST(X86BackendRegressions, StoreLabelValueMaterializesBeforeMemoryStore) {
    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {op("alloca", {imm(8)}, 0, ILValue::Kind::PTR),
                    op("store", {val(ILValue::Kind::PTR, 0), label("stored_label_symbol")}),
                    op("ret", {})};

    ILFunction fn{};
    fn.name = "store_label_value";
    fn.blocks = {entry};

    const CodegenResult result = compile(fn);
    if (!result.errors.empty()) {
        std::cerr << result.errors << '\n';
    }
    ASSERT_TRUE(result.errors.empty());
    EXPECT_NE(result.asmText.find("leaq stored_label_symbol(%rip)"), std::string::npos);
    EXPECT_NE(result.asmText.find("movq"), std::string::npos);
}

TEST(X86BackendRegressions, XmmAllocaSlotRemapsBelowCalleeSavedArea) {
    MFunction fn{};
    fn.name = "xmm_alloca_slot";

    const Operand rax = makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RAX));
    const Operand rbx = makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RBX));
    const Operand xmm0 = makePhysRegOperand(RegClass::XMM, static_cast<uint16_t>(PhysReg::XMM0));
    const OpReg rbp = makePhysReg(RegClass::GPR, static_cast<uint16_t>(PhysReg::RBP));

    MBasicBlock entry{};
    entry.label = ".L_xmm_alloca_slot_entry";
    entry.instructions = {MInstr::make(MOpcode::MOVrr, {rbx, rax}),
                          MInstr::make(MOpcode::MOVSDmr, {xmm0, makeMemOperand(rbp, -8)}),
                          MInstr::make(MOpcode::RET)};
    fn.blocks.push_back(std::move(entry));

    FrameInfo frame{};
    assignSpillSlots(fn, sysvTarget(), frame);

    ASSERT_FALSE(frame.usedCalleeSaved.empty());
    EXPECT_EQ(frame.usedCalleeSaved.front(), PhysReg::RBX);
    const auto &load = fn.blocks.front().instructions[1];
    ASSERT_EQ(load.opcode, MOpcode::MOVSDmr);
    const auto *mem = std::get_if<OpMem>(&load.operands[1]);
    ASSERT_TRUE(mem != nullptr);
    EXPECT_EQ(mem->disp, -16);
}

namespace {

void expectThreeOperandOverflowPseudoLowered(MOpcode pseudo, MOpcode real) {
    MFunction fn{};
    fn.name = "three_operand_ovf";

    MBasicBlock entry{};
    entry.label = ".L_three_operand_ovf_entry";
    const Operand dest = makeVRegOperand(RegClass::GPR, 3);
    const Operand lhs = makeVRegOperand(RegClass::GPR, 1);
    const Operand rhs = makeVRegOperand(RegClass::GPR, 2);
    entry.instructions = {MInstr::make(pseudo, {dest, lhs, rhs}), MInstr::make(MOpcode::RET)};
    fn.blocks.push_back(std::move(entry));

    lowerOverflowOps(fn);

    const auto &instructions = fn.blocks.front().instructions;
    ASSERT_GE(instructions.size(), 4u);
    EXPECT_EQ(instructions[0].opcode, MOpcode::MOVrr);
    EXPECT_EQ(instructions[0].operands.size(), 2u);
    EXPECT_EQ(instructions[1].opcode, real);
    EXPECT_EQ(instructions[1].operands.size(), 2u);
    EXPECT_EQ(instructions[2].opcode, MOpcode::JCC);
    ASSERT_FALSE(instructions[2].operands.empty());
    const auto *trapLabel = std::get_if<OpLabel>(&instructions[2].operands.back());
    ASSERT_TRUE(trapLabel != nullptr);
    EXPECT_EQ(trapLabel->name, ".Ltrap_ovf_three_operand_ovf");
}

} // namespace

TEST(X86BackendRegressions, ThreeOperandAddOverflowPseudoLowersToTwoOperandAdd) {
    expectThreeOperandOverflowPseudoLowered(MOpcode::ADDOvfrr, MOpcode::ADDrr);
}

TEST(X86BackendRegressions, ThreeOperandSubOverflowPseudoLowersToTwoOperandSub) {
    expectThreeOperandOverflowPseudoLowered(MOpcode::SUBOvfrr, MOpcode::SUBrr);
}

TEST(X86BackendRegressions, ThreeOperandMulOverflowPseudoLowersToTwoOperandMul) {
    expectThreeOperandOverflowPseudoLowered(MOpcode::IMULOvfrr, MOpcode::IMULrr);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
