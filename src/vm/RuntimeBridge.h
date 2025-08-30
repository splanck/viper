// File: src/vm/RuntimeBridge.h
// Purpose: Declares adapter between VM and runtime library.
// Key invariants: None.
// Ownership/Lifetime: VM owns the bridge.
// Links: docs/il-spec.md
#pragma once
#include "rt.h"
#include "support/source_manager.h"
#include <string>
#include <vector>

namespace il::vm {

union Slot; // defined in VM.h

/// @brief Provides entry points from the VM into the C runtime library.
class RuntimeBridge {
public:
  /// @brief Invoke runtime function @p name with arguments @p args.
  /// @param name Runtime function symbol.
  /// @param args Evaluated argument slots.
  /// @return Result slot from runtime call.
  static Slot call(const std::string &name, const std::vector<Slot> &args);

  /// @brief Report a trap with source location @p loc within function @p fn and
  /// block @p block.
  static void trap(const std::string &msg, const il::support::SourceLoc &loc, const std::string &fn,
                   const std::string &block);
};

} // namespace il::vm
