//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/perf/vm_switch_bench.cpp
// Purpose: Micro-benchmark SwitchI32 dispatch paths to detect major performance regressions.
// Key invariants: Both Auto and Linear switch modes execute equivalent IL and produce the same
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "vm/VM.hpp"

#include <cstdlib>
#ifdef _WIN32
#include "tests/common/PosixCompat.h"
#endif
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

using namespace il::core;

namespace
{

constexpr size_t kCaseCount = 50;
constexpr size_t kIterations = 20'000;
constexpr size_t kBenchmarkRuns = 3;

int64_t caseValue(size_t index)
{
    return static_cast<int64_t>(index) * 3 + 1;
}

int64_t computeExpectedSum(size_t caseCount, size_t iterations)
{
    int64_t total = 0;
    for (size_t i = 0; i < iterations; ++i)
        total += caseValue(i % caseCount);
    return total;
}

Module buildSwitchModule(size_t caseCount, size_t iterations)
{
    Module module;

    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextTemp = 0;

    BasicBlock entry;
    entry.label = "entry";
    Instr toLoop;
    toLoop.op = Opcode::Br;
    toLoop.type = Type(Type::Kind::Void);
    toLoop.labels.push_back("loop");
    toLoop.brArgs.push_back({Value::constInt(0), Value::constInt(0)});
    entry.instructions.push_back(toLoop);
    entry.terminated = true;

    BasicBlock loop;
    loop.label = "loop";
    Param loopSum{"sum", Type(Type::Kind::I64), nextTemp++};
    Param loopIdx{"idx", Type(Type::Kind::I64), nextTemp++};
    loop.params.push_back(loopSum);
    loop.params.push_back(loopIdx);

    Instr cmp;
    cmp.result = nextTemp++;
    cmp.op = Opcode::SCmpLT;
    cmp.type = Type(Type::Kind::I1);
    cmp.operands.push_back(Value::temp(loopIdx.id));
    cmp.operands.push_back(Value::constInt(static_cast<int64_t>(iterations)));
    loop.instructions.push_back(cmp);

    Instr cbr;
    cbr.op = Opcode::CBr;
    cbr.type = Type(Type::Kind::Void);
    cbr.operands.push_back(Value::temp(*cmp.result));
    cbr.labels.push_back("work");
    cbr.labels.push_back("done");
    cbr.brArgs.push_back({Value::temp(loopSum.id), Value::temp(loopIdx.id)});
    cbr.brArgs.push_back({Value::temp(loopSum.id)});
    loop.instructions.push_back(cbr);
    loop.terminated = true;

    BasicBlock work;
    work.label = "work";
    Param workSum{"sum_in", Type(Type::Kind::I64), nextTemp++};
    Param workIdx{"idx_in", Type(Type::Kind::I64), nextTemp++};
    work.params.push_back(workSum);
    work.params.push_back(workIdx);

    Instr rem;
    rem.result = nextTemp++;
    rem.op = Opcode::URem;
    rem.type = Type(Type::Kind::I64);
    rem.operands.push_back(Value::temp(workIdx.id));
    rem.operands.push_back(Value::constInt(static_cast<int64_t>(caseCount)));
    work.instructions.push_back(rem);

    Instr sw;
    sw.op = Opcode::SwitchI32;
    sw.type = Type(Type::Kind::Void);
    sw.operands.push_back(Value::temp(*rem.result));
    sw.labels.push_back("dispatch");
    sw.brArgs.push_back({Value::temp(workSum.id), Value::temp(workIdx.id), Value::constInt(-1)});
    for (size_t i = 0; i < caseCount; ++i)
    {
        sw.operands.push_back(Value::constInt(static_cast<int32_t>(i)));
        sw.labels.push_back("dispatch");
        sw.brArgs.push_back(
            {Value::temp(workSum.id), Value::temp(workIdx.id), Value::constInt(caseValue(i))});
    }
    work.instructions.push_back(sw);
    work.terminated = true;

    BasicBlock dispatch;
    dispatch.label = "dispatch";
    Param dispatchSum{"sum_next", Type(Type::Kind::I64), nextTemp++};
    Param dispatchIdx{"idx_next", Type(Type::Kind::I64), nextTemp++};
    Param dispatchVal{"case_val", Type(Type::Kind::I64), nextTemp++};
    dispatch.params.push_back(dispatchSum);
    dispatch.params.push_back(dispatchIdx);
    dispatch.params.push_back(dispatchVal);

    Instr addSum;
    addSum.result = nextTemp++;
    addSum.op = Opcode::Add;
    addSum.type = Type(Type::Kind::I64);
    addSum.operands.push_back(Value::temp(dispatchSum.id));
    addSum.operands.push_back(Value::temp(dispatchVal.id));
    dispatch.instructions.push_back(addSum);

    Instr nextIdx;
    nextIdx.result = nextTemp++;
    nextIdx.op = Opcode::Add;
    nextIdx.type = Type(Type::Kind::I64);
    nextIdx.operands.push_back(Value::temp(dispatchIdx.id));
    nextIdx.operands.push_back(Value::constInt(1));
    dispatch.instructions.push_back(nextIdx);

    Instr backToLoop;
    backToLoop.op = Opcode::Br;
    backToLoop.type = Type(Type::Kind::Void);
    backToLoop.labels.push_back("loop");
    backToLoop.brArgs.push_back({Value::temp(*addSum.result), Value::temp(*nextIdx.result)});
    dispatch.instructions.push_back(backToLoop);
    dispatch.terminated = true;

    BasicBlock done;
    done.label = "done";
    Param doneParam{"result", Type(Type::Kind::I64), nextTemp++};
    done.params.push_back(doneParam);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(doneParam.id));
    done.instructions.push_back(ret);
    done.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(loop));
    fn.blocks.push_back(std::move(work));
    fn.blocks.push_back(std::move(dispatch));
    fn.blocks.push_back(std::move(done));

    fn.valueNames.resize(nextTemp);
    fn.valueNames[loopSum.id] = "loop_sum";
    fn.valueNames[loopIdx.id] = "loop_idx";
    fn.valueNames[*cmp.result] = "loop_cmp";
    fn.valueNames[workSum.id] = "work_sum";
    fn.valueNames[workIdx.id] = "work_idx";
    fn.valueNames[*rem.result] = "mod_case";
    fn.valueNames[dispatchSum.id] = "dispatch_sum";
    fn.valueNames[dispatchIdx.id] = "dispatch_idx";
    fn.valueNames[dispatchVal.id] = "dispatch_val";
    fn.valueNames[*addSum.result] = "new_sum";
    fn.valueNames[*nextIdx.result] = "next_idx";
    fn.valueNames[doneParam.id] = "final_sum";

    module.functions.push_back(std::move(fn));
    return module;
}

struct BenchResult
{
    double milliseconds;
    int64_t checksum;
};

BenchResult runSwitchBench(const char *mode, size_t caseCount, size_t iterations)
{
    if (mode != nullptr)
        ::setenv("VIPER_SWITCH_MODE", mode, 1);
    else
        ::unsetenv("VIPER_SWITCH_MODE");

    Module module = buildSwitchModule(caseCount, iterations);
    il::vm::VM vm(module);

    const int64_t expected = computeExpectedSum(caseCount, iterations);
    const int64_t warmup = vm.run();
    if (warmup != expected)
        throw std::runtime_error("warm-up run produced unexpected result");

    int64_t total = 0;
    const auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < kBenchmarkRuns; ++i)
    {
        const int64_t result = vm.run();
        if (result != expected)
            throw std::runtime_error("benchmark run produced unexpected result");
        total += result;
    }
    const auto end = std::chrono::steady_clock::now();

    const double elapsedMs =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(end - start).count();
    std::cout << "VIPER_SWITCH_MODE=" << (mode ? mode : "<unset>") << " cases=" << caseCount
              << " iterations=" << iterations << " runs=" << kBenchmarkRuns << " checksum=" << total
              << " elapsed_ms=" << elapsedMs << '\n';

    return BenchResult{elapsedMs, total};
}

class SwitchModeEnvGuard
{
  public:
    SwitchModeEnvGuard()
    {
        if (const char *value = std::getenv("VIPER_SWITCH_MODE"); value != nullptr)
        {
            original = value;
        }
    }

    ~SwitchModeEnvGuard()
    {
        if (original.has_value())
            ::setenv("VIPER_SWITCH_MODE", original->c_str(), 1);
        else
            ::unsetenv("VIPER_SWITCH_MODE");
    }

  private:
    std::optional<std::string> original;
};

} // namespace

int main()
{
    SwitchModeEnvGuard guard;

    const BenchResult linear = runSwitchBench("Linear", kCaseCount, kIterations);
    const BenchResult autoResult = runSwitchBench("Auto", kCaseCount, kIterations);

    if (linear.checksum != autoResult.checksum)
    {
        std::cerr << "Switch benchmark checksum mismatch: linear=" << linear.checksum
                  << ", auto=" << autoResult.checksum << '\n';
        return 1;
    }

    if (linear.milliseconds <= 0.0)
    {
        std::cout << "Linear dispatch completed too quickly; skipping ratio assertion."
                  << std::endl;
        return 0;
    }

    const double ratio = autoResult.milliseconds / linear.milliseconds;
    if (ratio > 5.0)
    {
        std::cerr << "Auto switch dispatch regressed: ratio=" << ratio
                  << ", linear=" << linear.milliseconds << "ms, auto=" << autoResult.milliseconds
                  << "ms." << std::endl;
        return 1;
    }

    return 0;
}
