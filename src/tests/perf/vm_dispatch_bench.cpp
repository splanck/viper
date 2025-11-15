// File: tests/perf/vm_dispatch_bench.cpp
// Purpose: Benchmark interpreter dispatch strategies using a branch-reduced arithmetic loop.
// Key invariants: All dispatch modes execute the same loop body and yield identical checksums.
// Ownership/Lifetime: Benchmarks build a transient module and execute it immediately.
// Links: docs/il-guide.md#reference

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "vm/VM.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

using namespace il::core;

namespace
{

constexpr size_t kLoopIterations = 250'000;
constexpr size_t kBenchmarkRuns = 5;

int64_t computeExpectedSum(size_t iterations)
{
    int64_t sum = 0;
    for (size_t i = 0; i < iterations; ++i)
    {
        const int64_t idx = static_cast<int64_t>(i);
        const int64_t tmp1 = idx + 1;
        const int64_t tmp2 = tmp1 * 2;
        const int64_t tmp3 = idx + 3;
        const int64_t tmp4 = tmp2 + tmp3;
        const int64_t tmp5 = tmp4 * 5;
        sum += tmp5;
    }
    return sum;
}

Module buildArithmeticModule(size_t iterations)
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

    Instr idxPlusOne;
    idxPlusOne.result = nextTemp++;
    idxPlusOne.op = Opcode::Add;
    idxPlusOne.type = Type(Type::Kind::I64);
    idxPlusOne.operands.push_back(Value::temp(workIdx.id));
    idxPlusOne.operands.push_back(Value::constInt(1));
    work.instructions.push_back(idxPlusOne);

    Instr doubleIdxPlusOne;
    doubleIdxPlusOne.result = nextTemp++;
    doubleIdxPlusOne.op = Opcode::Mul;
    doubleIdxPlusOne.type = Type(Type::Kind::I64);
    doubleIdxPlusOne.operands.push_back(Value::temp(*idxPlusOne.result));
    doubleIdxPlusOne.operands.push_back(Value::constInt(2));
    work.instructions.push_back(doubleIdxPlusOne);

    Instr idxPlusThree;
    idxPlusThree.result = nextTemp++;
    idxPlusThree.op = Opcode::Add;
    idxPlusThree.type = Type(Type::Kind::I64);
    idxPlusThree.operands.push_back(Value::temp(workIdx.id));
    idxPlusThree.operands.push_back(Value::constInt(3));
    work.instructions.push_back(idxPlusThree);

    Instr combine;
    combine.result = nextTemp++;
    combine.op = Opcode::Add;
    combine.type = Type(Type::Kind::I64);
    combine.operands.push_back(Value::temp(*doubleIdxPlusOne.result));
    combine.operands.push_back(Value::temp(*idxPlusThree.result));
    work.instructions.push_back(combine);

    Instr scaled;
    scaled.result = nextTemp++;
    scaled.op = Opcode::Mul;
    scaled.type = Type(Type::Kind::I64);
    scaled.operands.push_back(Value::temp(*combine.result));
    scaled.operands.push_back(Value::constInt(5));
    work.instructions.push_back(scaled);

    Instr newSum;
    newSum.result = nextTemp++;
    newSum.op = Opcode::Add;
    newSum.type = Type(Type::Kind::I64);
    newSum.operands.push_back(Value::temp(workSum.id));
    newSum.operands.push_back(Value::temp(*scaled.result));
    work.instructions.push_back(newSum);

    Instr nextIdx;
    nextIdx.result = nextTemp++;
    nextIdx.op = Opcode::Add;
    nextIdx.type = Type(Type::Kind::I64);
    nextIdx.operands.push_back(Value::temp(workIdx.id));
    nextIdx.operands.push_back(Value::constInt(1));
    work.instructions.push_back(nextIdx);

    Instr backToLoop;
    backToLoop.op = Opcode::Br;
    backToLoop.type = Type(Type::Kind::Void);
    backToLoop.labels.push_back("loop");
    backToLoop.brArgs.push_back({Value::temp(*newSum.result), Value::temp(*nextIdx.result)});
    work.instructions.push_back(backToLoop);
    work.terminated = true;

    BasicBlock done;
    done.label = "done";
    Param doneSum{"result", Type(Type::Kind::I64), nextTemp++};
    done.params.push_back(doneSum);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(doneSum.id));
    done.instructions.push_back(ret);
    done.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(loop));
    fn.blocks.push_back(std::move(work));
    fn.blocks.push_back(std::move(done));

    fn.valueNames.resize(nextTemp);
    fn.valueNames[loopSum.id] = "loop_sum";
    fn.valueNames[loopIdx.id] = "loop_idx";
    fn.valueNames[*cmp.result] = "loop_cmp";
    fn.valueNames[workSum.id] = "work_sum";
    fn.valueNames[workIdx.id] = "work_idx";
    fn.valueNames[*idxPlusOne.result] = "idx_plus_one";
    fn.valueNames[*doubleIdxPlusOne.result] = "twice_idx_plus_two";
    fn.valueNames[*idxPlusThree.result] = "idx_plus_three";
    fn.valueNames[*combine.result] = "combined";
    fn.valueNames[*scaled.result] = "scaled_value";
    fn.valueNames[*newSum.result] = "accum_sum";
    fn.valueNames[*nextIdx.result] = "next_idx";
    fn.valueNames[doneSum.id] = "final_sum";

    module.functions.push_back(std::move(fn));
    return module;
}

struct BenchResult
{
    double milliseconds;
    int64_t checksum;
};

BenchResult runDispatchBench(const char *mode, size_t iterations)
{
    if (mode != nullptr)
        ::setenv("VIPER_DISPATCH", mode, 1);
    else
        ::unsetenv("VIPER_DISPATCH");

    Module module = buildArithmeticModule(iterations);
    il::vm::VM vm(module);

    const int64_t expected = computeExpectedSum(iterations);
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
    std::cout << "VIPER_DISPATCH=" << (mode ? mode : "<unset>") << " iterations=" << iterations
              << " runs=" << kBenchmarkRuns << " checksum=" << total << " elapsed_ms=" << elapsedMs
              << '\n';

    return BenchResult{elapsedMs, total};
}

class DispatchEnvGuard
{
  public:
    DispatchEnvGuard()
    {
        if (const char *value = std::getenv("VIPER_DISPATCH"); value != nullptr)
            original = value;
    }

    ~DispatchEnvGuard()
    {
        if (original.has_value())
            ::setenv("VIPER_DISPATCH", original->c_str(), 1);
        else
            ::unsetenv("VIPER_DISPATCH");
    }

  private:
    std::optional<std::string> original;
};

} // namespace

int main()
{
    DispatchEnvGuard guard;

    const BenchResult table = runDispatchBench("table", kLoopIterations);
    const BenchResult switchResult = runDispatchBench("switch", kLoopIterations);

#if VIPER_THREADING_SUPPORTED
    const BenchResult threaded = runDispatchBench("threaded", kLoopIterations);
#endif

    if (table.checksum != switchResult.checksum)
    {
        std::cerr << "Dispatch benchmark checksum mismatch between table and switch modes."
                  << std::endl;
        return 1;
    }

#if VIPER_THREADING_SUPPORTED
    if (table.checksum != threaded.checksum)
    {
        std::cerr << "Dispatch benchmark checksum mismatch between table and threaded modes."
                  << std::endl;
        return 1;
    }
#endif

    return 0;
}
