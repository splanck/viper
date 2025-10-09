//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements construction and initialisation helpers for the VM.  The routines
// here seed lookup tables, prepare execution frames, and apply debugger settings
// so that the interpreter loop can focus on opcode execution without worrying
// about setup and teardown invariants.
//
//===----------------------------------------------------------------------===//

#include "vm/VM.hpp"
#include "vm/Marshal.hpp"
#include "vm/RuntimeBridge.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Module.hpp"

#include <cassert>
#include <clocale>
#include <sstream>
#include <utility>

using namespace il::core;

namespace il::vm
{

namespace
{

/// @brief One-time initialiser that forces the numeric locale to "C".
struct NumericLocaleInitializer
{
    NumericLocaleInitializer()
    {
        std::setlocale(LC_NUMERIC, "C");
    }
};

[[maybe_unused]] const NumericLocaleInitializer kNumericLocaleInitializer{};

} // namespace

/// @brief Construct a VM instance bound to a specific IL module.
///
/// The constructor captures pointers to every function and string literal so
/// that later opcode handlers can resolve them in constant time.  It also wires
/// the tracing and debugging subsystems together, handing the trace sink's
/// source manager to the debugger so breakpoints can report file information.
///
/// @param m      Module containing code and globals to execute. It must outlive
///               the VM.
/// @param tc     Trace configuration used to initialise the trace sink. The
///               contained source manager is also shared with the debugger.
/// @param ms     Optional step limit; execution aborts after this many
///               instructions have been retired. A value of zero disables the
///               limit.
/// @param dbg    Initial debugger control block describing active breakpoints
///               and stepping behaviour.
/// @param script Optional scripted debugger interaction. When provided,
///               scripted actions drive how pauses are handled; otherwise
///               breaks cause the VM to return a fixed slot.
VM::VM(const Module &m, TraceConfig tc, uint64_t ms, DebugCtrl dbg, DebugScript *script)
    : mod(m), tracer(tc), debug(std::move(dbg)), script(script), maxSteps(ms)
{
    debug.setSourceManager(tc.sm);
    // Cache function pointers and constant strings for fast lookup during
    // execution and for resolving runtime bridge requests such as ConstStr.
    for (const auto &f : m.functions)
        fnMap[f.name] = &f;
    for (const auto &g : m.globals)
        strMap[g.name] = toViperString(g.init);
}

/// @brief Release runtime string handles owned by the VM instance.
VM::~VM()
{
    for (auto &entry : strMap)
        rt_str_release_maybe(entry.second);
    strMap.clear();

    for (auto &entry : inlineLiteralCache)
        rt_str_release_maybe(entry.second);
    inlineLiteralCache.clear();
}

/// @brief Initialise a fresh frame for executing a function.
///
/// The routine builds a label-to-block lookup table, seeds the register file and
/// staged parameter slots, verifies the argument count, and stores the entry
/// block pointer.  This prepares the frame for the interpreter loop without
/// emitting any trace events.
///
/// @param fn     Function to execute.
/// @param args   Argument slots for the function's entry block.
/// @param blocks Output mapping from block labels to blocks for fast branch resolution.
/// @param bb     Receives the entry basic block of @p fn, or nullptr when none exist.
/// @return Fully initialised frame ready to run.
Frame VM::setupFrame(const Function &fn,
                     const std::vector<Slot> &args,
                     std::unordered_map<std::string, const BasicBlock *> &blocks,
                     const BasicBlock *&bb)
{
    Frame fr;
    fr.func = &fn;
    // Pre-size register file to the function's SSA value count. This mirrors
    // the number of temporaries and parameters required by @p fn and avoids
    // incremental growth during execution.
    fr.regs.resize(fn.valueNames.size());
    assert(fr.regs.size() == fn.valueNames.size());
    fr.params.assign(fr.regs.size(), std::nullopt);
    fr.ehStack.clear();
    fr.activeError = {};
    fr.resumeState = {};
    for (const auto &b : fn.blocks)
        blocks[b.label] = &b;
    bb = fn.blocks.empty() ? nullptr : &fn.blocks.front();
    if (bb)
    {
        const auto &params = bb->params;
        if (args.size() != params.size())
        {
            std::ostringstream os;
            os << "argument count mismatch for function " << fn.name << ": expected "
               << params.size() << " argument" << (params.size() == 1 ? "" : "s")
               << ", received " << args.size();
            RuntimeBridge::trap(
                TrapKind::InvalidOperation, os.str(), {}, fn.name, bb->label);
        }
        for (size_t i = 0; i < params.size() && i < args.size(); ++i)
        {
            const auto id = params[i].id;
            assert(id < fr.params.size());
            fr.params[id] = args[i];
        }
    }
    return fr;
}

/// @brief Create an initial execution state for running @p fn.
///
/// The execution state wraps the freshly prepared frame, resets debugger
/// book-keeping, primes the trace sink with instruction locations, and starts
/// execution at the entry block.  The interpreter loop consumes this structure
/// directly.
///
/// @param fn   Function to execute.
/// @param args Arguments passed to the function's entry block.
/// @return Fully initialised execution state ready for the interpreter loop.
VM::ExecState VM::prepareExecution(const Function &fn, const std::vector<Slot> &args)
{
    ExecState st;
    st.fr = setupFrame(fn, args, st.blocks, st.bb);
    tracer.onFramePrepared(st.fr);
    debug.resetLastHit();
    st.ip = 0;
    st.skipBreakOnce = false;
    return st;
}

} // namespace il::vm

