// File: src/vm/RuntimeBridge.cpp
// Purpose: Bridges VM to C runtime functions.
// Key invariants: None.
// Ownership/Lifetime: Bridge does not own VM or runtime resources.
// Links: docs/il-spec.md
#include "vm/RuntimeBridge.h"
#include "vm/VM.h"
#include <cassert>
#include <sstream>

namespace il::vm {

Slot RuntimeBridge::call(const std::string &name, const std::vector<Slot> &args) {
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
  return res;
}

void RuntimeBridge::trap(const std::string &msg, const il::support::SourceLoc &loc,
                         const std::string &fn, const std::string &block) {
  std::ostringstream os;
  os << msg << " in " << fn << ":" << block << " at " << loc.file_id << ":" << loc.line << ":"
     << loc.column;
  rt_trap(os.str().c_str());
}

} // namespace il::vm
