// File: src/vm/VM.cpp
// Purpose: Implements stack-based virtual machine for IL.
// Key invariants: None.
// Ownership/Lifetime: VM references module owned externally.
// Links: docs/il-spec.md

#include "vm/VM.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "vm/RuntimeBridge.hpp"
#include <cassert>

using namespace il::core;

namespace il::vm
{

/// Locate and execute the module's @c main function.
///
/// The entry point is looked up by name in the cached function map and then
/// executed via @c execFunction. Any tracing or debugging configured on the VM
/// applies to the entire run.
///
/// @returns Signed 64-bit exit code produced by the program's @c main function.
int64_t VM::run()
{
    auto it = fnMap.find("main");
    assert(it != fnMap.end());
    return execFunction(*it->second, {}).i64;
}

/// Materialise an IL @p Value into a runtime @c Slot within a given frame.
///
/// The evaluator reads temporaries from the frame's register file and converts
/// constant operands into the appropriate slot representation. Global addresses
/// and constant strings are resolved through the VM's string pool, which bridges
/// to the runtime's string handling.
///
/// @param fr Active call frame supplying registers and pending parameters.
/// @param v  IL value to evaluate.
/// @returns A @c Slot containing the realised value; when the value is unknown,
///          a default-initialised slot is returned.
Slot VM::eval(Frame &fr, const Value &v)
{
    Slot s{};
    switch (v.kind)
    {
        case Value::Kind::Temp:
            if (v.id < fr.regs.size())
                return fr.regs[v.id];
            return s;
        case Value::Kind::ConstInt:
            s.i64 = v.i64;
            return s;
        case Value::Kind::ConstFloat:
            s.f64 = v.f64;
            return s;
        case Value::Kind::ConstStr:
            s.str = rt_const_cstr(v.str.c_str());
            return s;
        case Value::Kind::GlobalAddr:
        {
            auto it = strMap.find(v.str);
            if (it == strMap.end())
                RuntimeBridge::trap("unknown global", {}, fr.func->name, "");
            else
                s.str = it->second;
            return s;
        }
        case Value::Kind::NullPtr:
            s.ptr = nullptr;
            return s;
    }
    return s;
}

/// Dispatch and execute a single IL instruction.
///
/// A handler is selected from a static table based on the opcode and invoked to
/// perform the operation. Handlers such as @c handleCall and @c handleTrap
/// communicate with the runtime bridge for foreign function calls or traps.
///
/// @param fr     Current frame.
/// @param in     Instruction to execute.
/// @param blocks Mapping of block labels used for branch resolution.
/// @param bb     [in,out] Updated to the current basic block after any branch.
/// @param ip     [in,out] Instruction index within @p bb.
/// @return Execution result capturing control-flow effects and return value.
VM::ExecResult VM::executeOpcode(Frame &fr,
                                 const Instr &in,
                                 const std::unordered_map<std::string, const BasicBlock *> &blocks,
                                 const BasicBlock *&bb,
                                 size_t &ip)
{
    const auto &table = getOpcodeHandlers();
    OpcodeHandler handler = table[static_cast<size_t>(in.op)];
    if (!handler)
    {
        assert(false && "unimplemented opcode");
        return {};
    }
    return handler(*this, fr, in, blocks, bb, ip);
}

/// Main interpreter loop for executing a function.
///
/// Iterates over instructions in the current basic block, invokes tracing,
/// dispatches opcodes via @c executeOpcode, and checks debug controls before and
/// after each step. The loop exits when a return is executed or a debug pause is
/// requested.
///
/// @param st Mutable execution state containing frame and control flow info.
/// @return Return value of the function or special slot from debug control.
Slot VM::runFunctionLoop(ExecState &st)
{
    while (st.bb && st.ip < st.bb->instructions.size())
    {
        const Instr &in = st.bb->instructions[st.ip];
        if (auto br = processDebugControl(st, &in, false))
            return *br;
        tracer.onStep(in, st.fr);
        ++instrCount;
        auto res = executeOpcode(st.fr, in, st.blocks, st.bb, st.ip);
        if (res.returned)
            return res.value;
        if (res.jumped)
            debug.resetLastHit();
        else
            ++st.ip;
        if (auto br = processDebugControl(st, nullptr, true))
            return *br;
    }
    Slot s{};
    s.i64 = 0;
    return s;
}

/// Execute function @p fn with optional arguments.
///
/// Prepares an execution state, then runs the interpreter loop. The callee's
/// execution participates fully in tracing, debugging, and runtime bridge
/// interactions triggered through individual instructions.
///
/// @param fn   Function to execute.
/// @param args Argument slots passed to the entry block parameters.
/// @return Slot containing the function's return value.
Slot VM::execFunction(const Function &fn, const std::vector<Slot> &args)
{
    auto st = prepareExecution(fn, args);
    return runFunctionLoop(st);
}

} // namespace il::vm
