// File: src/vm/Marshal.hpp
// Purpose: Declares helpers for converting between VM and runtime data types.
// Key invariants: Conversion helpers preserve existing runtime encodings.
// Ownership/Lifetime: Views returned do not extend the lifetime of underlying data.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/Value.hpp"
#include "rt_string.h"

#include <cstdint>
#include <string_view>

namespace il::vm
{

using StringRef = std::string_view;
using ViperString = ::rt_string;

ViperString toViperString(StringRef text);
StringRef fromViperString(const ViperString &str);
int64_t toI64(const il::core::Value &value);
double toF64(const il::core::Value &value);

} // namespace il::vm
