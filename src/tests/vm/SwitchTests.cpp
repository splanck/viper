//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/SwitchTests.cpp
// Purpose: Validate VM switch.i32 execution paths and trace/debug diagnostics.
// Key invariants: Switch instruction selects correct successor and reports it
// Ownership/Lifetime: Tests build modules on the fly and execute them immediately.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace il::core;

namespace
{
struct CaseSpec
{
    std::string label;
    int32_t match;
    int64_t ret;
};

struct SwitchSpec
{
    int32_t scrutinee;
    std::string defaultLabel;
    int64_t defaultValue;
    std::vector<CaseSpec> cases;
};

Instr makeRet(int64_t value)
{
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::constInt(value));
    return ret;
}

Module buildSwitchModule(const SwitchSpec &spec)
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    builder.addBlock(fn, "entry");
    builder.addBlock(fn, spec.defaultLabel);

    for (const auto &cs : spec.cases)
        builder.addBlock(fn, cs.label);

    auto findBlock = [&fn](const std::string &label) -> BasicBlock &
    {
        for (auto &block : fn.blocks)
        {
            if (block.label == label)
                return block;
        }
        assert(false && "block label not found");
        std::abort();
    };

    BasicBlock &entry = findBlock("entry");
    BasicBlock &defaultBlock = findBlock(spec.defaultLabel);

    std::vector<BasicBlock *> caseBlocks;
    caseBlocks.reserve(spec.cases.size());
    for (const auto &cs : spec.cases)
        caseBlocks.push_back(&findBlock(cs.label));

    Instr sw;
    sw.op = Opcode::SwitchI32;
    sw.type = Type(Type::Kind::Void);
    sw.operands.push_back(Value::constInt(spec.scrutinee));
    sw.labels.push_back(spec.defaultLabel);
    sw.brArgs.emplace_back();
    for (const auto &cs : spec.cases)
    {
        sw.operands.push_back(Value::constInt(cs.match));
        sw.labels.push_back(cs.label);
        sw.brArgs.emplace_back();
    }
    entry.instructions.push_back(sw);
    entry.terminated = true;

    defaultBlock.instructions.push_back(makeRet(spec.defaultValue));
    defaultBlock.terminated = true;
    for (size_t i = 0; i < caseBlocks.size(); ++i)
    {
        caseBlocks[i]->instructions.push_back(makeRet(spec.cases[i].ret));
        caseBlocks[i]->terminated = true;
    }

    return module;
}

int64_t runSwitch(const SwitchSpec &baseSpec, int32_t scrutinee)
{
    SwitchSpec spec = baseSpec;
    spec.scrutinee = scrutinee;
    Module module = buildSwitchModule(spec);
    il::vm::VM vm(module);
    return vm.run();
}

std::string readFile(const std::string &path)
{
    std::ifstream file(path);
    assert(file && "failed to open golden file");
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string switchTraceGolden()
{
#ifdef TESTS_DIR
    const std::string path = std::string(TESTS_DIR) + "/golden/il_opt/switch_basic.out";
#else
    const std::string path = "tests/golden/il_opt/switch_basic.out";
#endif
    return readFile(path);
}
} // namespace

int main()
{
    {
        const SwitchSpec denseBase{
            0,
            "dense_default",
            99,
            {{"dense_case0", 0, 100}, {"dense_case1", 1, 101}, {"dense_case2", 2, 102}}};
        const std::array<std::pair<int32_t, int64_t>, 4> denseCases{{
            {0, 100},
            {1, 101},
            {2, 102},
            {37, 99},
        }};

        for (const auto &[scrutinee, expected] : denseCases)
            assert(runSwitch(denseBase, scrutinee) == expected);
    }

    {
        const SwitchSpec sparseBase{
            0,
            "sparse_default",
            0,
            {{"sparse_case2", 2, 222}, {"sparse_case10", 10, 1010}, {"sparse_case42", 42, 4242}}};
        const std::array<std::pair<int32_t, int64_t>, 4> sparseCases{{
            {2, 222},
            {10, 1010},
            {42, 4242},
            {-1, 0},
        }};

        for (const auto &[scrutinee, expected] : sparseCases)
            assert(runSwitch(sparseBase, scrutinee) == expected);
    }

    {
        SwitchSpec spec{7, "default_case", 42, {{"first_case", 1, 11}, {"last_case", 3, 33}}};
        Module module = buildSwitchModule(spec);
        il::vm::VM vm(module);
        assert(vm.run() == 42);
    }

    {
        SwitchSpec spec{1, "fallback", 99, {{"case_first", 1, 111}, {"case_last", 3, 333}}};
        Module module = buildSwitchModule(spec);
        il::vm::TraceConfig traceCfg;
        traceCfg.mode = il::vm::TraceConfig::IL;
        std::ostringstream err;
        auto *oldBuf = std::cerr.rdbuf(err.rdbuf());
        il::vm::VM vm(module, traceCfg);
        const int64_t result = vm.run();
        std::cerr.rdbuf(oldBuf);
        assert(result == 111);
        const std::string expected = switchTraceGolden();
        assert(err.str() == expected);
    }

    {
        SwitchSpec spec{3, "default_case", 0, {{"first_case", 1, 5}, {"last_case", 3, 55}}};
        Module module = buildSwitchModule(spec);
        il::vm::VM vm(module);
        assert(vm.run() == 55);
    }

    {
        SwitchSpec spec{1, "fallback", 300, {{"dup_first", 1, 10}, {"dup_second", 1, 20}}};
        Module module = buildSwitchModule(spec);
        il::vm::DebugCtrl debug;
        debug.addBreak(debug.internLabel("dup_first"));
        debug.addBreak(debug.internLabel("dup_second"));
        std::ostringstream err;
        auto *oldBuf = std::cerr.rdbuf(err.rdbuf());
        il::vm::VM vm(module, {}, 0, debug);
        const int64_t result = vm.run();
        std::cerr.rdbuf(oldBuf);
        const std::string output = err.str();
        assert(result == 10);
        assert(output.find("blk=dup_first") != std::string::npos);
        assert(output.find("blk=dup_second") == std::string::npos);
    }

    {
        SwitchSpec spec{5, "only_default", 123, {}};
        Module module = buildSwitchModule(spec);
        il::vm::VM vm(module);
        assert(vm.run() == 123);
    }

    return 0;
}
