//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_opt_equivalence.cpp
// Purpose: Differential test to ensure optimizer pipelines preserve VM semantics.
// Key invariants: Randomly generated IL programs remain well-formed and return
//                 identical results before and after O0/O1/O2 pipelines.
// Ownership/Lifetime: Builds ephemeral modules per iteration; no filesystem I/O.
// Links: docs/devdocs/il-passes.md, src/il/transform/PassManager.cpp
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/io/Serializer.hpp"
#include "il/transform/AnalysisManager.hpp"
#include "il/transform/PassManager.hpp"
#include "il/transform/PassRegistry.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"
#include "tests/TestHarness.hpp"
#include "tests/common/PosixCompat.h"
#include "tests/common/WaitCompat.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using il::core::BasicBlock;
using il::core::Instr;
using il::core::Opcode;
using il::core::Type;
using il::core::Value;
using il::support::SourceLoc;

namespace
{

constexpr SourceLoc kLoc{1, 1, 1};

struct ProgramConfig
{
    std::size_t minOpsPerBlock = 2;
    std::size_t maxOpsPerBlock = 5;
    std::size_t maxSwitchCases = 2;
    std::int64_t minIntConst = -16;
    std::int64_t maxIntConst = 16;
    double minFloatConst = -6.0;
    double maxFloatConst = 6.0;
};

struct GeneratedProgram
{
    il::core::Module module;
    std::uint64_t seed = 0;
    std::string ilText;
};

/// @brief Execution result when running a module on the VM.
struct ExecResult
{
    bool trapped = false;
    int exitCode = 0;
    std::int64_t value = 0;
    std::string stderrText;
};

class RandomProgramGenerator
{
  public:
    RandomProgramGenerator(std::uint64_t seed, ProgramConfig cfg)
        : seed_(seed), cfg_(cfg), rng_(seed), intDist_(cfg.minIntConst, cfg.maxIntConst),
          floatDist_(cfg.minFloatConst, cfg.maxFloatConst)
    {
    }

    GeneratedProgram generate()
    {
        GeneratedProgram out{};
        out.seed = seed_;

        il::core::Module module;
        il::build::IRBuilder builder(module);
        auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
        fn.blocks.reserve(5 + cfg_.maxSwitchCases);

        const std::size_t entryIdx = fn.blocks.size();
        builder.addBlock(fn, "entry");
        const std::size_t thenIdx = fn.blocks.size();
        builder.addBlock(fn, "then");
        const std::size_t elseIdx = fn.blocks.size();
        builder.addBlock(fn, "else");
        const std::size_t mergeIdx = fn.blocks.size();
        builder.createBlock(fn, "merge", {il::core::Param{"acc", Type(Type::Kind::I64), 0}});

        std::vector<std::size_t> retIdx;
        retIdx.reserve(cfg_.maxSwitchCases + 1);
        retIdx.push_back(fn.blocks.size());
        builder.createBlock(fn, "ret_default", {il::core::Param{"v", Type(Type::Kind::I64), 0}});
        for (std::size_t i = 0; i < cfg_.maxSwitchCases; ++i)
        {
            const std::string label = "ret_case" + std::to_string(i);
            retIdx.push_back(fn.blocks.size());
            builder.createBlock(fn, label, {il::core::Param{"v", Type(Type::Kind::I64), 0}});
        }

        BasicBlock &entry = fn.blocks[entryIdx];
        BasicBlock &thenBB = fn.blocks[thenIdx];
        BasicBlock &elseBB = fn.blocks[elseIdx];
        BasicBlock &mergeBB = fn.blocks[mergeIdx];
        std::vector<BasicBlock *> retCases;
        retCases.reserve(retIdx.size());
        for (std::size_t idx : retIdx)
        {
            retCases.push_back(&fn.blocks[idx]);
        }

        // Entry block: build a handful of int/float ops and a branch condition.
        builder.setInsertPoint(entry);
        auto entryInts = std::vector<Value>{Value::constInt(randomInt()),
                                            Value::constInt(randomInt()),
                                            Value::constInt(randomInt())};
        auto entryFloats =
            std::vector<Value>{Value::constFloat(randomFloat()), Value::constFloat(randomFloat())};

        auto appendIntOp = [&](Opcode op, Value lhs, Value rhs)
        {
            Instr instr;
            instr.result = builder.reserveTempId();
            instr.op = op;
            instr.type = Type(Type::Kind::I64);
            instr.operands = {lhs, rhs};
            instr.loc = kLoc;
            entry.instructions.push_back(std::move(instr));
            Value v = Value::temp(*entry.instructions.back().result);
            entryInts.push_back(v);
            return v;
        };

        if (entryInts.size() >= 2)
        {
            appendIntOp(Opcode::IAddOvf, entryInts[0], entryInts[1]);
            appendIntOp(Opcode::IMulOvf, entryInts.back(), Value::constInt(2));
        }

        auto appendFloatOp = [&](Opcode op, Value lhs, Value rhs)
        {
            Instr instr;
            instr.result = builder.reserveTempId();
            instr.op = op;
            instr.type = Type(Type::Kind::F64);
            instr.operands = {lhs, rhs};
            instr.loc = kLoc;
            entry.instructions.push_back(std::move(instr));
            Value v = Value::temp(*entry.instructions.back().result);
            entryFloats.push_back(v);
            return v;
        };

        if (entryFloats.size() >= 2)
        {
            appendFloatOp(Opcode::FAdd, entryFloats[0], entryFloats[1]);
            appendFloatOp(Opcode::FMul, entryFloats.back(), Value::constFloat(1.5));
        }

        Value cond{};
        if (coinFlip())
        {
            cond = appendCmp(
                entry, builder, Opcode::SCmpGT, entryInts.back(), Value::constInt(randomInt()));
        }
        else
        {
            cond = appendFloatCmp(entry,
                                  builder,
                                  Opcode::FCmpLT,
                                  entryFloats.back(),
                                  Value::constFloat(randomFloat()));
        }

        builder.cbr(cond, thenBB, {}, elseBB, {});

        // Then block: compute a value to feed the merge block.
        builder.setInsertPoint(thenBB);
        Value thenVal = emitPathValue(thenBB, builder);
        builder.br(mergeBB, {thenVal});

        // Else block: compute an alternate value.
        builder.setInsertPoint(elseBB);
        Value elseVal = emitPathValue(elseBB, builder);
        builder.br(mergeBB, {elseVal});

        // Merge block: derive return candidates and a switch scrutinee.
        builder.setInsertPoint(mergeBB);
        Value incoming = builder.blockParam(mergeBB, 0);
        Value adjusted =
            appendInt(mergeBB, builder, Opcode::IAddOvf, incoming, Value::constInt(randomInt()));
        Value lifted = appendInt(mergeBB, builder, Opcode::IMulOvf, adjusted, Value::constInt(3));

        const std::size_t caseCount = randomCaseCount();
        const int32_t lo = 0;
        const int32_t hi = 50000 + static_cast<int32_t>(caseCount);
        std::uniform_int_distribution<int32_t> idxDist(lo, hi);
        const int32_t idxConst = idxDist(rng_);

        Instr idxChk;
        idxChk.result = builder.reserveTempId();
        idxChk.op = Opcode::IdxChk;
        idxChk.type = Type(Type::Kind::I32);
        idxChk.operands = {Value::constInt(idxConst), Value::constInt(lo), Value::constInt(hi)};
        idxChk.loc = kLoc;
        mergeBB.instructions.push_back(std::move(idxChk));
        Value switchKey = Value::temp(*mergeBB.instructions.back().result);

        std::vector<int32_t> caseValues;
        while (caseValues.size() < caseCount)
        {
            int32_t candidate = static_cast<int32_t>(std::abs(randomInt()) % (hi + 1));
            if (candidate != idxConst &&
                std::find(caseValues.begin(), caseValues.end(), candidate) == caseValues.end())
            {
                caseValues.push_back(candidate);
            }
        }

        Instr switchInstr;
        switchInstr.op = Opcode::SwitchI32;
        switchInstr.type = Type(Type::Kind::Void);
        switchInstr.loc = kLoc;
        switchInstr.operands.push_back(switchKey);
        switchInstr.labels.push_back(retCases[0]->label);
        switchInstr.brArgs.push_back({lifted});

        for (std::size_t i = 0; i < caseValues.size(); ++i)
        {
            const int32_t value = caseValues[i];
            Value branchVal =
                appendInt(mergeBB, builder, Opcode::ISubOvf, lifted, Value::constInt(value));
            switchInstr.operands.push_back(Value::constInt(value));
            switchInstr.labels.push_back(retCases[i + 1]->label);
            switchInstr.brArgs.push_back({branchVal});
        }

        mergeBB.instructions.push_back(std::move(switchInstr));
        mergeBB.terminated = true;

        // Return blocks.
        for (BasicBlock *retBB : retCases)
        {
            builder.setInsertPoint(*retBB);
            builder.emitRet({builder.blockParam(*retBB, 0)}, kLoc);
        }

        out.module = std::move(module);
        out.ilText = il::io::Serializer::toString(out.module, il::io::Serializer::Mode::Pretty);
        return out;
    }

  private:
    std::size_t randomCaseCount()
    {
        std::uniform_int_distribution<std::size_t> dist(1, cfg_.maxSwitchCases);
        return dist(rng_);
    }

    std::int64_t randomInt()
    {
        return intDist_(rng_);
    }

    double randomFloat()
    {
        return floatDist_(rng_);
    }

    bool coinFlip()
    {
        std::uniform_int_distribution<int> dist(0, 1);
        return dist(rng_) == 1;
    }

    Value appendCmp(BasicBlock &bb, il::build::IRBuilder &builder, Opcode op, Value lhs, Value rhs)
    {
        Instr instr;
        instr.result = builder.reserveTempId();
        instr.op = op;
        instr.type = Type(Type::Kind::I1);
        instr.operands = {lhs, rhs};
        instr.loc = kLoc;
        bb.instructions.push_back(std::move(instr));
        return Value::temp(*bb.instructions.back().result);
    }

    Value appendFloatCmp(
        BasicBlock &bb, il::build::IRBuilder &builder, Opcode op, Value lhs, Value rhs)
    {
        return appendCmp(bb, builder, op, lhs, rhs);
    }

    Value appendInt(BasicBlock &bb, il::build::IRBuilder &builder, Opcode op, Value lhs, Value rhs)
    {
        Instr instr;
        instr.result = builder.reserveTempId();
        instr.op = op;
        instr.type = Type(Type::Kind::I64);
        instr.operands = {lhs, rhs};
        instr.loc = kLoc;
        bb.instructions.push_back(std::move(instr));
        return Value::temp(*bb.instructions.back().result);
    }

    Value emitPathValue(BasicBlock &bb, il::build::IRBuilder &builder)
    {
        std::uniform_int_distribution<std::size_t> count(cfg_.minOpsPerBlock, cfg_.maxOpsPerBlock);
        const std::size_t ops = count(rng_);
        std::vector<Value> ints{Value::constInt(randomInt()), Value::constInt(randomInt())};

        for (std::size_t i = 0; i < ops; ++i)
        {
            Value lhs = ints[rng_() % ints.size()];
            Value rhs = ints[rng_() % ints.size()];
            Value res = appendInt(bb, builder, pickIntOpcode(), lhs, rhs);
            ints.push_back(res);
        }

        return ints.back();
    }

    Opcode pickIntOpcode()
    {
        constexpr Opcode kOps[] = {
            Opcode::IAddOvf, Opcode::ISubOvf, Opcode::IMulOvf, Opcode::And, Opcode::Or};
        std::uniform_int_distribution<std::size_t> dist(0, std::size(kOps) - 1);
        return kOps[dist(rng_)];
    }

    std::uint64_t seed_;
    ProgramConfig cfg_;
    std::mt19937_64 rng_;
    std::uniform_int_distribution<std::int64_t> intDist_;
    std::uniform_real_distribution<double> floatDist_;
};

std::string describeFailure(std::string_view pipeline, const GeneratedProgram &program)
{
    std::ostringstream oss;
    oss << "Pipeline " << pipeline << " changed behaviour\n"
        << "Seed: " << program.seed << "\n"
        << "IL:\n"
        << program.ilText;
    return oss.str();
}

#ifdef _WIN32
// Windows version: run directly without process isolation
ExecResult runModuleIsolated(const il::core::Module &module)
{
    ExecResult result{};
    try
    {
        viper::tests::VmFixture fixture;
        il::core::Module copy = module;
        result.value = fixture.run(copy);
        result.exitCode = 0;
        result.trapped = false;
    }
    catch (...)
    {
        result.exitCode = 1;
        result.trapped = true;
    }
    return result;
}
#else
// POSIX version: run in isolated subprocess to catch crashes
ExecResult runModuleIsolated(const il::core::Module &module)
{
    ExecResult result{};

    std::array<int, 2> dataPipe{};
    std::array<int, 2> errPipe{};
    [[maybe_unused]] int okData = ::pipe(dataPipe.data());
    [[maybe_unused]] int okErr = ::pipe(errPipe.data());

    const pid_t pid = ::fork();
    if (pid == 0)
    {
        ::close(dataPipe[0]);
        ::close(errPipe[0]);
        ::dup2(errPipe[1], STDERR_FILENO);

        viper::tests::VmFixture fixture;
        il::core::Module copy = module;
        const std::int64_t value = fixture.run(copy);
        [[maybe_unused]] ssize_t wrote = ::write(dataPipe[1], &value, sizeof(value));
        _exit(0);
    }

    ::close(dataPipe[1]);
    ::close(errPipe[1]);

    std::int64_t value = 0;
    const ssize_t bytes = ::read(dataPipe[0], &value, sizeof(value));
    ::close(dataPipe[0]);

    std::string stderrText;
    std::array<char, 512> buffer{};
    while (true)
    {
        const ssize_t count = ::read(errPipe[0], buffer.data(), buffer.size());
        if (count <= 0)
            break;
        stderrText.append(buffer.data(), static_cast<std::size_t>(count));
    }
    ::close(errPipe[0]);

    int status = 0;
    [[maybe_unused]] const pid_t waited = ::waitpid(pid, &status, 0);

    result.stderrText = std::move(stderrText);
    if (WIFEXITED(status))
    {
        result.exitCode = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status))
    {
        result.exitCode = 128 + WTERMSIG(status);
    }

    result.trapped = (bytes != sizeof(value)) || result.exitCode != 0;
    if (!result.trapped)
    {
        result.value = value;
    }
    return result;
}
#endif

bool verifyModule(il::core::Module &module, std::string &diagOut)
{
    auto verified = il::verify::Verifier::verify(module);
    if (!verified)
    {
        std::ostringstream oss;
        il::support::printDiag(verified.error(), oss);
        diagOut = oss.str();
        return false;
    }
    return true;
}

bool runPipeline(il::core::Module &module, const std::string &pipelineId, std::string &diagOut)
{
    il::transform::PassManager pm;
    pm.addSimplifyCFG();
    const auto *pipeline = pm.getPipeline(pipelineId);
    if (!pipeline)
    {
        diagOut = "unknown pipeline " + pipelineId;
        return false;
    }

    il::transform::AnalysisManager analysis(module, pm.analyses());

    for (const auto &passId : *pipeline)
    {
        const auto *factory = pm.passes().lookup(passId);
        if (!factory)
        {
            diagOut = "missing pass " + passId;
            return false;
        }

        switch (factory->kind)
        {
            case il::transform::detail::PassKind::Module:
            {
                auto pass = factory->makeModule ? factory->makeModule() : nullptr;
                if (!pass)
                {
                    diagOut = "failed to create module pass " + passId;
                    return false;
                }
                il::transform::PreservedAnalyses preserved = pass->run(module, analysis);
                analysis.invalidateAfterModulePass(preserved);
                break;
            }
            case il::transform::detail::PassKind::Function:
            {
                auto pass = factory->makeFunction ? factory->makeFunction() : nullptr;
                if (!pass)
                {
                    diagOut = "failed to create function pass " + passId;
                    return false;
                }
                for (auto &fn : module.functions)
                {
                    il::transform::PreservedAnalyses preserved = pass->run(fn, analysis);
                    analysis.invalidateAfterFunctionPass(preserved, fn);
                }
                break;
            }
        }
    }

    return verifyModule(module, diagOut);
}

std::uint64_t baseSeed()
{
    if (const char *env = std::getenv("VIPER_OPT_EQ_SEED"))
    {
        char *end = nullptr;
        const auto value = std::strtoull(env, &end, 10);
        if (end != env)
            return value;
    }
    return 0xC0FFEE123456789ULL;
}

} // namespace

TEST(OptimizerDifferential, PipelinesPreserveVmSemantics)
{
    constexpr std::size_t kIterations = 12;
    ProgramConfig cfg{};
    const std::uint64_t seed0 = baseSeed();

    for (std::size_t i = 0; i < kIterations; ++i)
    {
        const std::uint64_t seed = seed0 + i;
        RandomProgramGenerator generator(seed, cfg);
        GeneratedProgram program = generator.generate();

        std::string diag;
        const bool verified = verifyModule(program.module, diag);
        if (!verified)
        {
            std::cerr << "Verifier rejected generated module\n"
                      << diag << "\nSeed: " << seed << "\nIL:\n"
                      << program.ilText << std::endl;
        }
        ASSERT_TRUE(verified);

        il::core::Module o0 = program.module;
        il::core::Module o1 = program.module;
        il::core::Module o2 = program.module;

        const bool ranO0 = runPipeline(o0, "O0", diag);
        if (!ranO0)
            std::cerr << describeFailure("O0", program) << "\n" << diag << std::endl;
        ASSERT_TRUE(ranO0);

        const bool ranO1 = runPipeline(o1, "O1", diag);
        if (!ranO1)
            std::cerr << describeFailure("O1", program) << "\n" << diag << std::endl;
        ASSERT_TRUE(ranO1);

        const bool ranO2 = runPipeline(o2, "O2", diag);
        if (!ranO2)
            std::cerr << describeFailure("O2", program) << "\n" << diag << std::endl;
        ASSERT_TRUE(ranO2);

        const ExecResult baseline = runModuleIsolated(program.module);
        const ExecResult resO0 = runModuleIsolated(o0);
        const ExecResult resO1 = runModuleIsolated(o1);
        const ExecResult resO2 = runModuleIsolated(o2);

        if (baseline.trapped != resO0.trapped)
            std::cerr << describeFailure("O0", program) << std::endl;
        if (baseline.trapped != resO1.trapped)
            std::cerr << describeFailure("O1", program) << std::endl;
        if (baseline.trapped != resO2.trapped)
            std::cerr << describeFailure("O2", program) << std::endl;
        ASSERT_EQ(baseline.trapped, resO0.trapped);
        ASSERT_EQ(baseline.trapped, resO1.trapped);
        ASSERT_EQ(baseline.trapped, resO2.trapped);

        if (!baseline.trapped)
        {
            if (baseline.value != resO0.value)
                std::cerr << describeFailure("O0", program) << std::endl;
            if (baseline.value != resO1.value)
                std::cerr << describeFailure("O1", program) << std::endl;
            if (baseline.value != resO2.value)
                std::cerr << describeFailure("O2", program) << std::endl;
            EXPECT_EQ(baseline.value, resO0.value);
            EXPECT_EQ(baseline.value, resO1.value);
            EXPECT_EQ(baseline.value, resO2.value);
        }
        else
        {
            if (baseline.exitCode != resO0.exitCode)
                std::cerr << describeFailure("O0", program) << std::endl;
            if (baseline.exitCode != resO1.exitCode)
                std::cerr << describeFailure("O1", program) << std::endl;
            if (baseline.exitCode != resO2.exitCode)
                std::cerr << describeFailure("O2", program) << std::endl;
            EXPECT_EQ(baseline.exitCode, resO0.exitCode);
            EXPECT_EQ(baseline.exitCode, resO1.exitCode);
            EXPECT_EQ(baseline.exitCode, resO2.exitCode);
        }
    }
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
