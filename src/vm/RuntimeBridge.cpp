// File: src/vm/RuntimeBridge.cpp
// Purpose: Bridges VM to C runtime functions.
// Key invariants: None.
// Ownership/Lifetime: Bridge does not own VM or runtime resources.
// Links: docs/il-spec.md
#include "vm/RuntimeBridge.h"
#include "vm/VM.h"
#include <cassert>
#include <sstream>

using il::support::SourceLoc;

namespace {
SourceLoc curLoc{};
std::string curFn;
std::string curBlock;
} // namespace

extern "C" void vm_trap(const char *msg) {
  il::vm::RuntimeBridge::trap(msg ? msg : "trap", curLoc, curFn, curBlock);
}

namespace il::vm {

Slot RuntimeBridge::call(const std::string &name, const std::vector<Slot> &args,
                         const SourceLoc &loc, const std::string &fn, const std::string &block) {
  curLoc = loc;
  curFn = fn;
  curBlock = block;
  Slot res{};
  if (name == "rt_print_str") {
    rt_print_str(args[0].str);
  } else if (name == "rt_print_i64") {
    rt_print_i64(args[0].i64);
  } else if (name == "rt_len") {
    res.i64 = rt_len(args[0].str);
  } else if (name == "rt_concat") {
    res.str = rt_concat(args[0].str, args[1].str);
  } else if (name == "rt_substr") {
    res.str = rt_substr(args[0].str, args[1].i64, args[2].i64);
  } else if (name == "rt_str_eq") {
    res.i64 = rt_str_eq(args[0].str, args[1].str);
  } else if (name == "rt_to_int") {
    res.i64 = rt_to_int(args[0].str);
  } else {
    assert(false && "unknown runtime call");
  }
  curLoc = {};
  curFn.clear();
  curBlock.clear();
  return res;
}

void RuntimeBridge::trap(const std::string &msg, const SourceLoc &loc, const std::string &fn,
                         const std::string &block) {
  std::ostringstream os;
  os << msg;
  if (!fn.empty()) {
    os << ' ' << fn << ": " << block;
    if (loc.isValid())
      os << " (" << loc.file_id << ':' << loc.line << ':' << loc.column << ')';
  }
  rt_abort(os.str().c_str());
}

} // namespace il::vm
