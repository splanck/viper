// File: src/vm/Marshal.hpp
// Purpose: Declares helpers for converting between VM and runtime data types.
// Key invariants: Conversion helpers preserve existing runtime encodings.
// Ownership/Lifetime: Views returned do not extend the lifetime of underlying data.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "rt_string.h"
#include "support/source_location.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace il::vm
{

using StringRef = std::string_view;
using ViperString = ::rt_string;

union Slot;

struct ResultBuffers
{
    int64_t i64 = 0;
    double f64 = 0.0;
    ViperString str = nullptr;
    void *ptr = nullptr;
};

struct PowStatus
{
    bool active{false};
    bool ok{true};
    bool *ptr{nullptr};
};

ViperString toViperString(StringRef text);
StringRef fromViperString(const ViperString &str);
int64_t toI64(const il::core::Value &value);
double toF64(const il::core::Value &value);

std::vector<void *> marshalRuntimeArguments(const il::runtime::RuntimeSignature &sig,
                                            const std::vector<Slot> &args,
                                            PowStatus &powStatus);

void *resultBufferFor(il::core::Type::Kind kind, ResultBuffers &buffers);

void assignResult(Slot &slot, il::core::Type::Kind kind, const ResultBuffers &buffers);

bool handlePowTrap(const il::runtime::RuntimeSignature &sig,
                   il::runtime::RuntimeTrapClass trapClass,
                   const PowStatus &powStatus,
                   const std::vector<Slot> &args,
                   const ResultBuffers &buffers,
                   const il::support::SourceLoc &loc,
                   const std::string &fn,
                   const std::string &block);

} // namespace il::vm
