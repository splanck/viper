// File: src/vm/RuntimeBridge.cpp
// Purpose: Bridges VM to C runtime functions.
// Key invariants: None.
// Ownership/Lifetime: Bridge does not own VM or runtime resources.
// Links: docs/il-spec.md

#include "vm/RuntimeBridge.hpp"
#include "rt_math.h"
#include "rt_random.h"
#include "vm/VM.hpp"
#include <cassert>
#include <sstream>

using il::support::SourceLoc;

namespace
{
/// @brief Global scratch space for recording the current source location.
/// @details Runtime calls may trigger traps inside the C runtime. The VM
/// populates these globals prior to dispatch so that the hook `vm_trap` can
/// report a precise function/block/SourceLoc. They are cleared after every call.
SourceLoc curLoc{};
/// @brief Fully qualified name of the function currently executing.
std::string curFn;
/// @brief Label of the current basic block within the function.
std::string curBlock;
} // namespace

/// @brief Entry point invoked from the C runtime when a trap occurs.
/// @details Uses the globals recorded by `RuntimeBridge::call` to route the
/// message through `RuntimeBridge::trap`, preserving the source location
/// and function context for diagnostics.
extern "C" void vm_trap(const char *msg)
{
    il::vm::RuntimeBridge::trap(msg ? msg : "trap", curLoc, curFn, curBlock);
}

namespace il::vm
{

/// @brief Dispatch a VM runtime call to the corresponding C implementation.
/// @details The VM passes the symbolic runtime `name` along with any
/// arguments. The bridge records the current function and source location
/// before selecting the matching C function, enabling any nested trap to
/// report meaningful diagnostics.
Slot RuntimeBridge::call(const std::string &name,
                         const std::vector<Slot> &args,
                         const SourceLoc &loc,
                         const std::string &fn,
                         const std::string &block)
{
    // Stash call site info so `vm_trap` can find it if the callee traps.
    curLoc = loc;
    curFn = fn;
    curBlock = block;
    Slot res{};
    auto checkArgs = [&](size_t count)
    {
        if (args.size() < count)
        {
            std::ostringstream os;
            os << name << ": expected " << count << " argument(s), got " << args.size();
            RuntimeBridge::trap(os.str(), loc, fn, block);
            return false;
        }
        return true;
    };
    if (name == "rt_print_str")
    {
        if (checkArgs(1))
            rt_print_str(args[0].str);
    }
    else if (name == "rt_print_i64")
    {
        if (checkArgs(1))
            rt_print_i64(args[0].i64);
    }
    else if (name == "rt_print_f64")
    {
        if (checkArgs(1))
            rt_print_f64(args[0].f64);
    }
    else if (name == "rt_len")
    {
        if (checkArgs(1))
            res.i64 = rt_len(args[0].str);
    }
    else if (name == "rt_concat")
    {
        if (checkArgs(2))
            res.str = rt_concat(args[0].str, args[1].str);
    }
    else if (name == "rt_substr")
    {
        if (checkArgs(3))
            res.str = rt_substr(args[0].str, args[1].i64, args[2].i64);
    }
    else if (name == "rt_str_eq")
    {
        if (checkArgs(2))
            res.i64 = rt_str_eq(args[0].str, args[1].str);
    }
    else if (name == "rt_input_line")
    {
        if (checkArgs(0))
            res.str = rt_input_line();
    }
    else if (name == "rt_to_int")
    {
        if (checkArgs(1))
            res.i64 = rt_to_int(args[0].str);
    }
    else if (name == "rt_int_to_str")
    {
        if (checkArgs(1))
            res.str = rt_int_to_str(args[0].i64);
    }
    else if (name == "rt_f64_to_str")
    {
        if (checkArgs(1))
            res.str = rt_f64_to_str(args[0].f64);
    }
    else if (name == "rt_alloc")
    {
        if (checkArgs(1))
            res.ptr = rt_alloc(args[0].i64);
    }
    else if (name == "rt_left")
    {
        if (checkArgs(2))
            res.str = rt_left(args[0].str, args[1].i64);
    }
    else if (name == "rt_right")
    {
        if (checkArgs(2))
            res.str = rt_right(args[0].str, args[1].i64);
    }
    else if (name == "rt_mid2")
    {
        if (checkArgs(2))
            res.str = rt_mid2(args[0].str, args[1].i64);
    }
    else if (name == "rt_mid3")
    {
        if (checkArgs(3))
            res.str = rt_mid3(args[0].str, args[1].i64, args[2].i64);
    }
    else if (name == "rt_instr2")
    {
        if (checkArgs(2))
            res.i64 = rt_instr2(args[0].str, args[1].str);
    }
    else if (name == "rt_instr3")
    {
        if (checkArgs(3))
            res.i64 = rt_instr3(args[0].i64, args[1].str, args[2].str);
    }
    else if (name == "rt_ltrim")
    {
        if (checkArgs(1))
            res.str = rt_ltrim(args[0].str);
    }
    else if (name == "rt_rtrim")
    {
        if (checkArgs(1))
            res.str = rt_rtrim(args[0].str);
    }
    else if (name == "rt_trim")
    {
        if (checkArgs(1))
            res.str = rt_trim(args[0].str);
    }
    else if (name == "rt_ucase")
    {
        if (checkArgs(1))
            res.str = rt_ucase(args[0].str);
    }
    else if (name == "rt_lcase")
    {
        if (checkArgs(1))
            res.str = rt_lcase(args[0].str);
    }
    else if (name == "rt_chr")
    {
        if (checkArgs(1))
            res.str = rt_chr(args[0].i64);
    }
    else if (name == "rt_asc")
    {
        if (checkArgs(1))
            res.i64 = rt_asc(args[0].str);
    }
    else if (name == "rt_sqrt")
    {
        if (checkArgs(1))
            res.f64 = rt_sqrt(args[0].f64);
    }
    else if (name == "rt_floor")
    {
        if (checkArgs(1))
            res.f64 = rt_floor(args[0].f64);
    }
    else if (name == "rt_ceil")
    {
        if (checkArgs(1))
            res.f64 = rt_ceil(args[0].f64);
    }
    else if (name == "rt_sin")
    {
        if (checkArgs(1))
            res.f64 = rt_sin(args[0].f64);
    }
    else if (name == "rt_cos")
    {
        if (checkArgs(1))
            res.f64 = rt_cos(args[0].f64);
    }
    else if (name == "rt_pow")
    {
        if (checkArgs(2))
            res.f64 = rt_pow(args[0].f64, args[1].f64);
    }
    else if (name == "rt_abs_i64")
    {
        if (checkArgs(1))
            res.i64 = rt_abs_i64(args[0].i64);
    }
    else if (name == "rt_abs_f64")
    {
        if (checkArgs(1))
            res.f64 = rt_abs_f64(args[0].f64);
    }
    else if (name == "rt_randomize_i64")
    {
        if (checkArgs(1))
            rt_randomize_i64(args[0].i64);
    }
    else if (name == "rt_rnd")
    {
        if (checkArgs(0))
            res.f64 = rt_rnd();
    }
    else
    {
        assert(false && "unknown runtime call");
    }
    curLoc = {};
    curFn.clear();
    curBlock.clear();
    return res;
}

/// @brief Report a trap originating from the C runtime.
/// @details Invoked by `vm_trap` when a runtime builtin signals a fatal
/// condition. Formats the message with optional function, block, and source
/// location before forwarding it to `rt_abort`.
/// @param msg   Description of the trap condition.
/// @param loc   Source location of the trapping instruction, if available.
/// @param fn    Fully qualified function name containing the call.
/// @param block Label of the basic block with the trapping call.
void RuntimeBridge::trap(const std::string &msg,
                         const SourceLoc &loc,
                         const std::string &fn,
                         const std::string &block)
{
    std::ostringstream os;
    os << msg;
    if (!fn.empty())
    {
        os << ' ' << fn << ": " << block;
        if (loc.isValid())
            os << " (" << loc.file_id << ':' << loc.line << ':' << loc.column << ')';
    }
    rt_abort(os.str().c_str());
}

} // namespace il::vm
