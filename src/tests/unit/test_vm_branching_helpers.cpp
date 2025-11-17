// File: tests/unit/test_vm_branching_helpers.cpp
// Purpose: Exercise shared branching helpers covering selection and jump plumbing.
// Key invariants: select_case must honour exact and range matches while jump
//                 validates argument counts before transferring control.
// Ownership: Uses VMTestHook to prepare temporary execution state; no resources
//            escape the test scope.
// Links: docs/codemap.md

#include "VMTestHook.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include "vm/VM.hpp"
#include "vm/VMContext.hpp"
#include "vm/ops/common/Branching.hpp"

#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>
#include "common/RunProcess.hpp"

using il::vm::ops::common::Case;
using il::vm::ops::common::Scalar;
using il::vm::ops::common::Target;

int main(int argc, char **argv)
{
    {
        // Exact match should select the corresponding target.
        Target t1{};
        t1.labelIndex = 1;
        Target t2{};
        t2.labelIndex = 2;
        std::vector<Case> table;
        table.push_back(Case::exact(Scalar{10}, t1));
        table.push_back(Case::exact(Scalar{20}, t2));
        Target fallback{};
        Target selected = il::vm::ops::common::select_case(Scalar{20}, table, fallback);
        assert(selected.labelIndex == 2);
    }

    {
        // Range match should capture inclusive bounds.
        Target rangeTarget{};
        rangeTarget.labelIndex = 5;
        std::vector<Case> table;
        table.push_back(Case::range(Scalar{5}, Scalar{10}, rangeTarget));
        Target fallback{};
        Target selected = il::vm::ops::common::select_case(Scalar{7}, table, fallback);
        assert(selected.labelIndex == 5);
    }

    {
        // When no case matches, the default target must be returned.
        Target defaultTarget{};
        defaultTarget.labelIndex = 3;
        std::vector<Case> table;
        table.push_back(Case::exact(Scalar{1}, Target{}));
        Target selected = il::vm::ops::common::select_case(Scalar{42}, table, defaultTarget);
        assert(selected.labelIndex == 3);
    }

    {
        // Jump transfers control and propagates arguments when counts match.
        il::core::Module module;
        auto &function = module.functions.emplace_back();
        function.name = "main";
        function.valueNames.push_back("p0");

        auto &entry = function.blocks.emplace_back();
        entry.label = "entry";
        auto &dest = function.blocks.emplace_back();
        dest.label = "dest";
        auto &param = dest.params.emplace_back();
        param.name = "p0";
        param.type = il::core::Type(il::core::Type::Kind::I64);
        param.id = 0;

        il::core::Type voidType(il::core::Type::Kind::Void);
        auto &branch = entry.instructions.emplace_back();
        branch.op = il::core::Opcode::Br;
        branch.type = voidType;
        branch.labels.push_back(dest.label);
        branch.brArgs.push_back({il::core::Value::constInt(42)});
        entry.terminated = true;

        il::vm::VM vm(module);
        il::vm::ActiveVMGuard guard(&vm);
        auto state = il::vm::VMTestHook::prepare(vm, function);

        Target target{};
        target.vm = &vm;
        target.instr = &entry.instructions.front();
        target.labelIndex = 0;
        target.blocks = &state.blocks;
        target.currentBlock = &state.bb;
        target.ip = &state.ip;

        il::vm::ops::common::jump(state.fr, target);

        assert(state.bb == &dest);
        assert(state.ip == 0);
        assert(state.fr.params[0].has_value());
        assert(state.fr.params[0]->i64 == 42);
    }

    {
        // Argument count mismatches must trigger a trap; run in a subprocess for isolation.
        const char *reentry = std::getenv("VIPER_VM_BRANCH_TRAP");
        if (reentry && std::string(reentry) == "1")
        {
            il::core::Module module;
            auto &function = module.functions.emplace_back();
            function.name = "main";
            function.valueNames.push_back("p0");

            auto &entry = function.blocks.emplace_back();
            entry.label = "entry";
            auto &dest = function.blocks.emplace_back();
            dest.label = "dest";
            auto &param = dest.params.emplace_back();
            param.name = "p0";
            param.type = il::core::Type(il::core::Type::Kind::I64);
            param.id = 0;

            il::core::Type voidType(il::core::Type::Kind::Void);
            auto &branch = entry.instructions.emplace_back();
            branch.op = il::core::Opcode::Br;
            branch.type = voidType;
            branch.labels.push_back(dest.label);
            branch.brArgs.emplace_back(); // missing argument
            entry.terminated = true;

            il::vm::VM vm(module);
            il::vm::ActiveVMGuard guard(&vm);
            auto state = il::vm::VMTestHook::prepare(vm, function);

            Target target{};
            target.vm = &vm;
            target.instr = &entry.instructions.front();
            target.labelIndex = 0;
            target.blocks = &state.blocks;
            target.currentBlock = &state.bb;
            target.ip = &state.ip;

            il::vm::ops::common::jump(state.fr, target);
            std::_Exit(0);
        }

        // Spawn subprocess of this test with an env var flag.
        std::vector<std::string> argv_vec;
        for (int i = 0; i < argc; ++i) argv_vec.emplace_back(argv[i]);
        RunResult rr = run_process(argv_vec, std::nullopt, {{"VIPER_VM_BRANCH_TRAP", std::string("1")}});
        assert(rr.exit_code != 0 && "trap should yield a non-zero termination status");
    }

    return 0;
}
