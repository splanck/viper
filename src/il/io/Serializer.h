// File: src/il/io/Serializer.h
// Purpose: Declares text serializer for IL modules.
// Key invariants: None.
// Ownership/Lifetime: Serializer operates on provided modules.
// Links: docs/il-spec.md
#pragma once
#include "il/core/Module.h"
#include <ostream>
#include <string>

namespace il::io {

class Serializer {
public:
  static void write(const il::core::Module &m, std::ostream &os);
  static std::string toString(const il::core::Module &m);
};

} // namespace il::io
