// File: src/vm/VM.cpp
// Purpose: Implements stack-based virtual machine for IL.
// Key invariants: None.
// Ownership/Lifetime: VM references module owned externally.
// Links: docs/il-spec.md

#include "vm/VM.hpp"
#include "VM/DebugScript.h"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "vm/RuntimeBridge.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <utility>

using namespace il::core;

namespace il::vm
{

namespace
{
/// Store an opcode result into the destination register when one is provided.
///
/// @param fr  Frame whose register file receives the value.
/// @param in  Instruction describing the destination register.
/// @param val Slot value to store.
/// @sideeffects Extends and writes to the frame's register array.
/// @return Nothing.
inline void storeResult(Frame &fr, const il::core::Instr &in, const Slot &val)
{
    if (in.result)
    {
        if (fr.regs.size() <= *in.result)
            fr.regs.resize(*in.result + 1);
        fr.regs[*in.result] = val;
    }
}
} // namespace

/// Construct a VM instance bound to a specific IL @p Module.
/// The constructor wires the tracing and debugging subsystems and pre-populates
/// lookup tables for functions and runtime strings.
///
/// @param m   Module containing code and globals to execute. It must outlive the VM.
/// @param tc  Trace configuration used to initialise the @c TraceSink. The contained
///            source manager is passed to the debug controller so source locations
///            can be reported in breaks.
/// @param ms  Optional step limit; execution aborts after this many instructions
///            have been retired. A value of @c 0 disables the limit.
/// @param dbg Initial debugger control block describing active breakpoints and
///            stepping behaviour.
/// @param script Optional scripted debugger interaction. When provided, scripted
///            actions drive how pauses are handled; otherwise breaks cause the VM
///            to return a fixed slot.
VM::VM(const Module &m, TraceConfig tc, uint64_t ms, DebugCtrl dbg, DebugScript *script)
    : mod(m), tracer(tc), debug(std::move(dbg)), script(script), maxSteps(ms)
{
    debug.setSourceManager(tc.sm);
    // Cache function pointers and constant strings for fast lookup during
    // execution and for resolving runtime bridge requests such as ConstStr.
    for (const auto &f : m.functions)
        fnMap[f.name] = &f;
    for (const auto &g : m.globals)
        strMap[g.name] = rt_const_cstr(g.init.c_str());
}

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
            s.str = strMap[v.str];
            return s;
        case Value::Kind::NullPtr:
            s.ptr = nullptr;
            return s;
    }
    return s;
}

/// Initialise a fresh @c Frame for executing function @p fn.
///
/// Populates a basic-block lookup table, selects the entry block and seeds the
/// register file and any entry parameters. This prepares state for the main
/// interpreter loop without performing any tracing.
///
/// @param fn     Function to execute.
/// @param args   Argument slots for the function's entry block.
/// @param blocks Output mapping from block labels to blocks for fast branch resolution.
/// @param bb     Set to the entry basic block of @p fn.
/// @return Fully initialised frame ready to run.
Frame VM::setupFrame(const Function &fn,
                     const std::vector<Slot> &args,
                     std::unordered_map<std::string, const BasicBlock *> &blocks,
                     const BasicBlock *&bb)
{
    Frame fr;
    fr.func = &fn;
    fr.regs.resize(64);
    for (const auto &b : fn.blocks)
        blocks[b.label] = &b;
    bb = fn.blocks.empty() ? nullptr : &fn.blocks.front();
    if (bb)
    {
        const auto &params = bb->params;
        for (size_t i = 0; i < params.size() && i < args.size(); ++i)
            fr.params[params[i].id] = args[i];
    }
    return fr;
}

/// Manage a potential debug break before or after executing an instruction.
///
/// Checks label and source line breakpoints using @c DebugCtrl. When a break is
/// hit the optional @c DebugScript controls stepping; otherwise a fixed slot is
/// returned to pause execution. Pending block parameter transfers are also
/// applied here when entering a new block.
///
/// @param fr            Current frame.
/// @param bb            Current basic block.
/// @param ip            Instruction index within @p bb.
/// @param skipBreakOnce Internal flag used to skip a single break when stepping.
/// @param in            Optional instruction for source line breakpoints.
/// @return @c std::nullopt to continue or a @c Slot signalling a pause.
std::optional<Slot> VM::handleDebugBreak(
    Frame &fr, const BasicBlock &bb, size_t ip, bool &skipBreakOnce, const Instr *in)
{
    if (!in)
    {
        if (debug.shouldBreak(bb))
        {
            std::cerr << "[BREAK] fn=@" << fr.func->name << " blk=" << bb.label
                      << " reason=label\n";
            if (!script || script->empty())
            {
                Slot s{};
                s.i64 = 10;
                return s;
            }
            auto act = script->nextAction();
            if (act.kind == DebugActionKind::Step)
                stepBudget = act.count;
            skipBreakOnce = true;
        }
        for (const auto &p : bb.params)
        {
            auto it = fr.params.find(p.id);
            if (it != fr.params.end())
            {
                if (fr.regs.size() <= p.id)
                    fr.regs.resize(p.id + 1);
                fr.regs[p.id] = it->second;
                debug.onStore(p.name,
                              p.type.kind,
                              fr.regs[p.id].i64,
                              fr.regs[p.id].f64,
                              fr.func->name,
                              bb.label,
                              0);
            }
        }
        fr.params.clear();
        return std::nullopt;
    }
    if (debug.hasSrcLineBPs() && debug.shouldBreakOn(*in))
    {
        const auto *sm = debug.getSourceManager();
        std::string path;
        if (sm && in->loc.isValid())
            path = std::filesystem::path(sm->getPath(in->loc.file_id)).filename().string();
        std::cerr << "[BREAK] src=" << path << ':' << in->loc.line << " fn=@" << fr.func->name
                  << " blk=" << bb.label << " ip=#" << ip << "\n";
        Slot s{};
        s.i64 = 10;
        return s;
    }
    return std::nullopt;
}

//===----------------------------------------------------------------------===//
// Opcode handlers
//===----------------------------------------------------------------------===//

#define DEFINE_BIN_INT_OP(NAME, OP)                                                                \
    VM::ExecResult VM::handle##NAME(Frame &fr, const Instr &in)                                    \
    {                                                                                              \
        Slot a = eval(fr, in.operands[0]);                                                         \
        Slot b = eval(fr, in.operands[1]);                                                         \
        Slot res{};                                                                                \
        res.i64 = a.i64 OP b.i64;                                                                  \
        storeResult(fr, in, res);                                                                  \
        return {};                                                                                 \
    }

#define DEFINE_BIN_FLOAT_OP(NAME, OP)                                                              \
    VM::ExecResult VM::handle##NAME(Frame &fr, const Instr &in)                                    \
    {                                                                                              \
        Slot a = eval(fr, in.operands[0]);                                                         \
        Slot b = eval(fr, in.operands[1]);                                                         \
        Slot res{};                                                                                \
        res.f64 = a.f64 OP b.f64;                                                                  \
        storeResult(fr, in, res);                                                                  \
        return {};                                                                                 \
    }

#define DEFINE_INT_CMP(NAME, CMP)                                                                  \
    VM::ExecResult VM::handle##NAME(Frame &fr, const Instr &in)                                    \
    {                                                                                              \
        Slot a = eval(fr, in.operands[0]);                                                         \
        Slot b = eval(fr, in.operands[1]);                                                         \
        Slot res{};                                                                                \
        res.i64 = (a.i64 CMP b.i64) ? 1 : 0;                                                       \
        storeResult(fr, in, res);                                                                  \
        return {};                                                                                 \
    }

#define DEFINE_FLOAT_CMP(NAME, CMP)                                                                \
    VM::ExecResult VM::handle##NAME(Frame &fr, const Instr &in)                                    \
    {                                                                                              \
        Slot a = eval(fr, in.operands[0]);                                                         \
        Slot b = eval(fr, in.operands[1]);                                                         \
        Slot res{};                                                                                \
        res.i64 = (a.f64 CMP b.f64) ? 1 : 0;                                                       \
        storeResult(fr, in, res);                                                                  \
        return {};                                                                                 \
    }

/// Allocate a block of zeroed stack memory.
///
/// @param fr Frame whose stack is extended.
/// @param in Instruction providing the size in bytes and optional result.
/// @sideeffects Advances the stack pointer and writes zero bytes.
/// @returns ExecResult with no control-flow effects; result slot holds pointer.
VM::ExecResult VM::handleAlloca(Frame &fr, const Instr &in)
{
    int64_t bytes = eval(fr, in.operands[0]).i64;
    if (bytes < 0)
    {
        RuntimeBridge::trap("negative allocation", in.loc, fr.func->name, "");
        ExecResult r{};
        r.value.i64 = 0; // unreachable
        r.returned = true;
        return r;
    }
    size_t sz = static_cast<size_t>(bytes);
    size_t addr = fr.sp;
    assert(addr + sz <= fr.stack.size());
    std::memset(fr.stack.data() + addr, 0, sz);
    Slot res{};
    res.ptr = fr.stack.data() + addr;
    fr.sp += sz;
    storeResult(fr, in, res);
    return {};
}

/// Load a typed value from memory.
///
/// @param fr Frame supplying the source pointer.
/// @param in Instruction specifying the pointer operand and result type.
/// @sideeffects Reads from memory; stores value in destination register.
/// @returns ExecResult with no control-flow effects.
VM::ExecResult VM::handleLoad(Frame &fr, const Instr &in)
{
    void *ptr = eval(fr, in.operands[0]).ptr;
    assert(ptr && "null load");
    Slot res{};
    if (in.type.kind == Type::Kind::I64)
        res.i64 = *reinterpret_cast<int64_t *>(ptr);
    else if (in.type.kind == Type::Kind::F64)
        res.f64 = *reinterpret_cast<double *>(ptr);
    else if (in.type.kind == Type::Kind::Str)
        res.str = *reinterpret_cast<rt_string *>(ptr);
    else if (in.type.kind == Type::Kind::Ptr)
        res.ptr = *reinterpret_cast<void **>(ptr);
    storeResult(fr, in, res);
    return {};
}

/// Store a typed value to memory and emit debug events.
///
/// @param fr Frame supplying pointer and value operands.
/// @param in Instruction describing the store.
/// @param bb Basic block containing the store, for debug reporting.
/// @param ip Instruction index within the block for debug reporting.
/// @sideeffects Writes to memory and may emit debug store callbacks.
/// @returns ExecResult with no control-flow effects.
VM::ExecResult VM::handleStore(Frame &fr, const Instr &in, const BasicBlock *bb, size_t ip)
{
    void *ptr = eval(fr, in.operands[0]).ptr;
    assert(ptr && "null store");
    Slot val = eval(fr, in.operands[1]);
    if (in.type.kind == Type::Kind::I64)
        *reinterpret_cast<int64_t *>(ptr) = val.i64;
    else if (in.type.kind == Type::Kind::F64)
        *reinterpret_cast<double *>(ptr) = val.f64;
    else if (in.type.kind == Type::Kind::Str)
        *reinterpret_cast<rt_string *>(ptr) = val.str;
    else if (in.type.kind == Type::Kind::Ptr)
        *reinterpret_cast<void **>(ptr) = val.ptr;
    if (in.operands[0].kind == Value::Kind::Temp)
    {
        unsigned id = in.operands[0].id;
        if (id < fr.func->valueNames.size())
        {
            const std::string &nm = fr.func->valueNames[id];
            if (!nm.empty())
                debug.onStore(nm, in.type.kind, val.i64, val.f64, fr.func->name, bb->label, ip);
        }
    }
    return {};
}

/// Handle integer addition.
/// @param fr Frame providing operands.
/// @param in Instruction with two integer operands and optional result.
/// @sideeffects Writes the sum to the destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_BIN_INT_OP(Add, +)
/// Handle integer subtraction.
/// @param fr Frame providing operands.
/// @param in Instruction with two integer operands and optional result.
/// @sideeffects Writes the difference to the destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_BIN_INT_OP(Sub, -)
/// Handle integer multiplication.
/// @param fr Frame providing operands.
/// @param in Instruction with two integer operands and optional result.
/// @sideeffects Writes the product to the destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_BIN_INT_OP(Mul, *)
/// Handle floating-point addition.
/// @param fr Frame providing operands.
/// @param in Instruction with two floating operands and optional result.
/// @sideeffects Writes the sum to the destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_BIN_FLOAT_OP(FAdd, +)
/// Handle floating-point subtraction.
/// @param fr Frame providing operands.
/// @param in Instruction with two floating operands and optional result.
/// @sideeffects Writes the difference to the destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_BIN_FLOAT_OP(FSub, -)
/// Handle floating-point multiplication.
/// @param fr Frame providing operands.
/// @param in Instruction with two floating operands and optional result.
/// @sideeffects Writes the product to the destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_BIN_FLOAT_OP(FMul, *)
/// Handle floating-point division.
/// @param fr Frame providing operands.
/// @param in Instruction with two floating operands and optional result.
/// @sideeffects Writes the quotient to the destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_BIN_FLOAT_OP(FDiv, /)
/// Handle integer bitwise XOR.
/// @param fr Frame providing operands.
/// @param in Instruction with two integer operands and optional result.
/// @sideeffects Writes the xor result to the destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_BIN_INT_OP(Xor, ^)
/// Handle integer left shift.
/// @param fr Frame providing operands.
/// @param in Instruction with two integer operands and optional result.
/// @sideeffects Writes the shifted value to the destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_BIN_INT_OP(Shl, <<)

/// Compute pointer address using getelementptr-style offsetting.
///
/// @param fr Frame providing base pointer and offset operands.
/// @param in Instruction describing operands and result.
/// @sideeffects None besides writing the result register.
/// @returns ExecResult with no control-flow effects.
VM::ExecResult VM::handleGEP(Frame &fr, const Instr &in)
{
    Slot base = eval(fr, in.operands[0]);
    Slot off = eval(fr, in.operands[1]);
    Slot res{};
    res.ptr = static_cast<char *>(base.ptr) + off.i64;
    storeResult(fr, in, res);
    return {};
}

/// Compare two integers for equality.
/// @param fr Frame providing operands.
/// @param in Instruction with two integer operands and result destination.
/// @sideeffects Writes comparison result (1 or 0) to destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_INT_CMP(ICmpEq, ==)
/// Compare two integers for inequality.
/// @param fr Frame providing operands.
/// @param in Instruction with two integer operands and result destination.
/// @sideeffects Writes comparison result to destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_INT_CMP(ICmpNe, !=)
/// Compare if first integer greater than second.
/// @param fr Frame providing operands.
/// @param in Instruction with two integer operands and result destination.
/// @sideeffects Writes comparison result to destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_INT_CMP(SCmpGT, >)
/// Compare if first integer less than second.
/// @param fr Frame providing operands.
/// @param in Instruction with two integer operands and result destination.
/// @sideeffects Writes comparison result to destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_INT_CMP(SCmpLT, <)
/// Compare if first integer less than or equal to second.
/// @param fr Frame providing operands.
/// @param in Instruction with two integer operands and result destination.
/// @sideeffects Writes comparison result to destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_INT_CMP(SCmpLE, <=)
/// Compare if first integer greater than or equal to second.
/// @param fr Frame providing operands.
/// @param in Instruction with two integer operands and result destination.
/// @sideeffects Writes comparison result to destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_INT_CMP(SCmpGE, >=)
/// Compare two floating-point numbers for equality.
/// @param fr Frame providing operands.
/// @param in Instruction with two floating operands and result destination.
/// @sideeffects Writes comparison result to destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_FLOAT_CMP(FCmpEQ, ==)
/// Compare two floating-point numbers for inequality.
/// @param fr Frame providing operands.
/// @param in Instruction with two floating operands and result destination.
/// @sideeffects Writes comparison result to destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_FLOAT_CMP(FCmpNE, !=)
/// Compare if first floating operand greater than second.
/// @param fr Frame providing operands.
/// @param in Instruction with two floating operands and result destination.
/// @sideeffects Writes comparison result to destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_FLOAT_CMP(FCmpGT, >)
/// Compare if first floating operand less than second.
/// @param fr Frame providing operands.
/// @param in Instruction with two floating operands and result destination.
/// @sideeffects Writes comparison result to destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_FLOAT_CMP(FCmpLT, <)
/// Compare if first floating operand less than or equal to second.
/// @param fr Frame providing operands.
/// @param in Instruction with two floating operands and result destination.
/// @sideeffects Writes comparison result to destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_FLOAT_CMP(FCmpLE, <=)
/// Compare if first floating operand greater than or equal to second.
/// @param fr Frame providing operands.
/// @param in Instruction with two floating operands and result destination.
/// @sideeffects Writes comparison result to destination register.
/// @returns Empty ExecResult with no control-flow changes.
DEFINE_FLOAT_CMP(FCmpGE, >=)

/// Unconditionally branch to another basic block, transferring arguments.
///
/// @param fr   Current frame for parameter evaluation.
/// @param in   Branch instruction containing target label and arguments.
/// @param blocks Block lookup table for resolving targets.
/// @param bb   [in,out] Updated to destination block.
/// @param ip   [in,out] Reset to zero for new block.
/// @sideeffects Updates frame parameter map and alters control flow.
/// @returns ExecResult indicating a jump was taken.
VM::ExecResult VM::handleBr(
    Frame &fr, const Instr &in, const BlockMap &blocks, const BasicBlock *&bb, size_t &ip)
{
    const auto &targetLabel = in.labels[0];
    auto itBlk = blocks.find(targetLabel);
    assert(itBlk != blocks.end() && "invalid block");
    const BasicBlock *target = itBlk->second;
    const auto &args = in.brArgs.empty() ? std::vector<Value>{} : in.brArgs[0];
    for (size_t i = 0; i < args.size() && i < target->params.size(); ++i)
        fr.params[target->params[i].id] = eval(fr, args[i]);
    bb = target;
    ip = 0;
    ExecResult r{};
    r.jumped = true;
    return r;
}

/// Conditionally branch based on a boolean slot value.
///
/// @param fr   Current frame for evaluating condition and arguments.
/// @param in   Branch instruction with two targets and optional arguments.
/// @param blocks Block lookup table for resolving targets.
/// @param bb   [in,out] Updated to chosen destination block.
/// @param ip   [in,out] Reset to zero for new block.
/// @sideeffects Updates frame parameter map and alters control flow.
/// @returns ExecResult indicating a jump was taken.
VM::ExecResult VM::handleCBr(
    Frame &fr, const Instr &in, const BlockMap &blocks, const BasicBlock *&bb, size_t &ip)
{
    Slot cond = eval(fr, in.operands[0]);
    size_t idx = cond.i64 ? 0 : 1;
    const auto &targetLabel = in.labels[idx];
    auto itBlk = blocks.find(targetLabel);
    assert(itBlk != blocks.end() && "invalid block");
    const BasicBlock *target = itBlk->second;
    const auto &args = in.brArgs.size() > idx ? in.brArgs[idx] : std::vector<Value>{};
    for (size_t i = 0; i < args.size() && i < target->params.size(); ++i)
        fr.params[target->params[i].id] = eval(fr, args[i]);
    bb = target;
    ip = 0;
    ExecResult r{};
    r.jumped = true;
    return r;
}

/// Return from the current function.
///
/// @param fr Frame providing optional return operand.
/// @param in Ret instruction with optional value operand.
/// @sideeffects Ends execution of the current function.
/// @returns ExecResult marked as returned with value slot.
VM::ExecResult VM::handleRet(Frame &fr, const Instr &in)
{
    ExecResult r{};
    r.value.i64 = 0;
    if (!in.operands.empty())
        r.value = eval(fr, in.operands[0]);
    r.returned = true;
    return r;
}

/// Load a constant string from the global table.
///
/// @param fr Frame receiving the string slot.
/// @param in Instruction referencing a global string name.
/// @sideeffects Writes the string pointer to the destination register.
/// @returns ExecResult with no control-flow changes.
VM::ExecResult VM::handleConstStr(Frame &fr, const Instr &in)
{
    Slot res{};
    res.str = strMap[in.operands[0].str];
    storeResult(fr, in, res);
    return {};
}

/// Invoke a function or runtime bridge call.
///
/// @param fr Frame providing argument operands.
/// @param in Call instruction naming the callee and operands.
/// @param bb Basic block containing the call for runtime diagnostics.
/// @sideeffects May invoke other functions or runtime bridges; writes result register.
/// @returns ExecResult with no control-flow effects aside from call.
VM::ExecResult VM::handleCall(Frame &fr, const Instr &in, const BasicBlock *bb)
{
    std::vector<Slot> callArgs;
    for (const auto &op : in.operands)
        callArgs.push_back(eval(fr, op));
    Slot res{};
    auto itFn = fnMap.find(in.callee);
    if (itFn != fnMap.end())
        res = execFunction(*itFn->second, callArgs);
    else
        res = RuntimeBridge::call(in.callee, callArgs, in.loc, fr.func->name, bb->label);
    storeResult(fr, in, res);
    return {};
}

/// Convert a signed integer slot to floating point.
///
/// @param fr Frame providing the source integer operand.
/// @param in Conversion instruction describing destination type.
/// @sideeffects Writes converted value to destination register.
/// @returns ExecResult with no control-flow changes.
VM::ExecResult VM::handleSitofp(Frame &fr, const Instr &in)
{
    Slot v = eval(fr, in.operands[0]);
    Slot res{};
    res.f64 = static_cast<double>(v.i64);
    storeResult(fr, in, res);
    return {};
}

/// Convert a floating-point slot to signed integer.
///
/// @param fr Frame providing the source floating operand.
/// @param in Conversion instruction describing destination type.
/// @sideeffects Writes converted value to destination register.
/// @returns ExecResult with no control-flow changes.
VM::ExecResult VM::handleFptosi(Frame &fr, const Instr &in)
{
    Slot v = eval(fr, in.operands[0]);
    Slot res{};
    res.i64 = static_cast<int64_t>(v.f64);
    storeResult(fr, in, res);
    return {};
}

/// Truncate or zero-extend an integer to 1 bit.
///
/// @param fr Frame providing the source integer operand.
/// @param in Conversion instruction specifying source value.
/// @sideeffects Writes masked result to destination register.
/// @returns ExecResult with no control-flow changes.
VM::ExecResult VM::handleTruncOrZext1(Frame &fr, const Instr &in)
{
    Slot v = eval(fr, in.operands[0]);
    v.i64 &= 1;
    storeResult(fr, in, v);
    return {};
}

/// Signal an unrecoverable trap to the runtime bridge.
///
/// @param fr Frame active at the trap site.
/// @param in Trap instruction containing source location.
/// @param bb Basic block containing the trap for diagnostics.
/// @sideeffects Emits a trap via the runtime bridge and terminates execution.
/// @returns ExecResult marked as returned (unreachable).
VM::ExecResult VM::handleTrap(Frame &fr, const Instr &in, const BasicBlock *bb)
{
    RuntimeBridge::trap("trap", in.loc, fr.func->name, bb->label);
    ExecResult r{};
    r.value.i64 = 0; // unreachable
    r.returned = true;
    return r;
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
    using Handler = ExecResult (*)(
        VM &, Frame &, const Instr &, const BlockMap &, const BasicBlock *&, size_t &);
    static const std::array<Handler, static_cast<size_t>(Opcode::Trap) + 1> table = []
    {
        std::array<Handler, static_cast<size_t>(Opcode::Trap) + 1> t{};
        t[static_cast<size_t>(Opcode::Alloca)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleAlloca(fr, in); };
        t[static_cast<size_t>(Opcode::Load)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleLoad(fr, in); };
        t[static_cast<size_t>(Opcode::Store)] = [](VM &vm,
                                                   Frame &fr,
                                                   const Instr &in,
                                                   const BlockMap &,
                                                   const BasicBlock *&b,
                                                   size_t &i)
        { return vm.handleStore(fr, in, b, i); };
        t[static_cast<size_t>(Opcode::Add)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleAdd(fr, in); };
        t[static_cast<size_t>(Opcode::Sub)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleSub(fr, in); };
        t[static_cast<size_t>(Opcode::Mul)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleMul(fr, in); };
        t[static_cast<size_t>(Opcode::FAdd)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFAdd(fr, in); };
        t[static_cast<size_t>(Opcode::FSub)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFSub(fr, in); };
        t[static_cast<size_t>(Opcode::FMul)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFMul(fr, in); };
        t[static_cast<size_t>(Opcode::FDiv)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFDiv(fr, in); };
        t[static_cast<size_t>(Opcode::Xor)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleXor(fr, in); };
        t[static_cast<size_t>(Opcode::Shl)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleShl(fr, in); };
        t[static_cast<size_t>(Opcode::GEP)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleGEP(fr, in); };
        t[static_cast<size_t>(Opcode::ICmpEq)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleICmpEq(fr, in); };
        t[static_cast<size_t>(Opcode::ICmpNe)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleICmpNe(fr, in); };
        t[static_cast<size_t>(Opcode::SCmpGT)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleSCmpGT(fr, in); };
        t[static_cast<size_t>(Opcode::SCmpLT)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleSCmpLT(fr, in); };
        t[static_cast<size_t>(Opcode::SCmpLE)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleSCmpLE(fr, in); };
        t[static_cast<size_t>(Opcode::SCmpGE)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleSCmpGE(fr, in); };
        t[static_cast<size_t>(Opcode::FCmpEQ)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFCmpEQ(fr, in); };
        t[static_cast<size_t>(Opcode::FCmpNE)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFCmpNE(fr, in); };
        t[static_cast<size_t>(Opcode::FCmpGT)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFCmpGT(fr, in); };
        t[static_cast<size_t>(Opcode::FCmpLT)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFCmpLT(fr, in); };
        t[static_cast<size_t>(Opcode::FCmpLE)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFCmpLE(fr, in); };
        t[static_cast<size_t>(Opcode::FCmpGE)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFCmpGE(fr, in); };
        t[static_cast<size_t>(Opcode::Br)] = [](VM &vm,
                                                Frame &fr,
                                                const Instr &in,
                                                const BlockMap &blk,
                                                const BasicBlock *&b,
                                                size_t &i)
        { return vm.handleBr(fr, in, blk, b, i); };
        t[static_cast<size_t>(Opcode::CBr)] = [](VM &vm,
                                                 Frame &fr,
                                                 const Instr &in,
                                                 const BlockMap &blk,
                                                 const BasicBlock *&b,
                                                 size_t &i)
        { return vm.handleCBr(fr, in, blk, b, i); };
        t[static_cast<size_t>(Opcode::Ret)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleRet(fr, in); };
        t[static_cast<size_t>(Opcode::ConstStr)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleConstStr(fr, in); };
        t[static_cast<size_t>(Opcode::Call)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&b, size_t &)
        { return vm.handleCall(fr, in, b); };
        t[static_cast<size_t>(Opcode::Sitofp)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleSitofp(fr, in); };
        t[static_cast<size_t>(Opcode::Fptosi)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFptosi(fr, in); };
        t[static_cast<size_t>(Opcode::Trunc1)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleTruncOrZext1(fr, in); };
        t[static_cast<size_t>(Opcode::Zext1)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleTruncOrZext1(fr, in); };
        t[static_cast<size_t>(Opcode::Trap)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&b, size_t &)
        { return vm.handleTrap(fr, in, b); };
        return t;
    }();

    Handler handler = table[static_cast<size_t>(in.op)];
    if (!handler)
    {
        assert(false && "unimplemented opcode");
        return {};
    }
    return handler(*this, fr, in, blocks, bb, ip);
}

/// Create an initial execution state for running @p fn.
///
/// This sets up the frame and block map via @c setupFrame, resets debugging
/// state, and initialises the instruction pointer and stepping flags.
///
/// @param fn   Function to execute.
/// @param args Arguments passed to the function's entry block.
/// @return Fully initialised execution state ready for the interpreter loop.
VM::ExecState VM::prepareExecution(const Function &fn, const std::vector<Slot> &args)
{
    ExecState st;
    st.fr = setupFrame(fn, args, st.blocks, st.bb);
    debug.resetLastHit();
    st.ip = 0;
    st.skipBreakOnce = false;
    return st;
}

/// Handle debugging-related bookkeeping before or after an instruction executes.
///
/// Enforces the global step limit, performs breakpoint checks via
/// @c handleDebugBreak, and manages the single-step budget. When a pause is
/// requested, a special slot is returned to signal the interpreter loop.
///
/// @param st      Current execution state.
/// @param in      Instruction being processed, if any.
/// @param postExec Set to true when invoked after executing @p in.
/// @return Optional slot causing execution to pause; @c std::nullopt otherwise.
std::optional<Slot> VM::processDebugControl(ExecState &st, const Instr *in, bool postExec)
{
    if (!postExec)
    {
        if (maxSteps && instrCount >= maxSteps)
        {
            std::cerr << "VM: step limit exceeded (" << maxSteps << "); aborting.\n";
            Slot s{};
            s.i64 = 1;
            return s;
        }
        if (st.ip == 0 && stepBudget == 0 && !st.skipBreakOnce)
            if (auto br = handleDebugBreak(st.fr, *st.bb, st.ip, st.skipBreakOnce, nullptr))
                return br;
        st.skipBreakOnce = false;
        if (in)
            if (auto br = handleDebugBreak(st.fr, *st.bb, st.ip, st.skipBreakOnce, in))
                return br;
        return std::nullopt;
    }
    if (stepBudget > 0)
    {
        --stepBudget;
        if (stepBudget == 0)
        {
            std::cerr << "[BREAK] fn=@" << st.fr.func->name << " blk=" << st.bb->label
                      << " reason=step\n";
            if (!script || script->empty())
            {
                Slot s{};
                s.i64 = 10;
                return s;
            }
            auto act = script->nextAction();
            if (act.kind == DebugActionKind::Step)
                stepBudget = act.count;
            st.skipBreakOnce = true;
        }
    }
    return std::nullopt;
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
