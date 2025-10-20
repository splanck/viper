// File: src/vm/Marshal.hpp
// Purpose: Declares helpers for converting between VM and runtime data types.
// Key invariants: Conversion helpers preserve existing runtime encodings.
// Ownership/Lifetime: Views returned do not extend the lifetime of underlying data.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/Value.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "rt_string.h"
#include "vm/Trap.hpp"

#include <cstdint>
#include <cstddef>
#include <span>
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

struct PowTrapOutcome
{
    bool triggered{false};
    TrapKind kind{TrapKind::RuntimeError};
    std::string message;
};

ViperString toViperString(StringRef text);
StringRef fromViperString(const ViperString &str);
int64_t toI64(const il::core::Value &value);
double toF64(const il::core::Value &value);

void *slotToArgPointer(Slot &slot, il::core::Type::Kind kind);
void *resultBufferFor(il::core::Type::Kind kind, ResultBuffers &buffers);
void assignResult(Slot &slot, il::core::Type::Kind kind, const ResultBuffers &buffers);
std::vector<void *> marshalArguments(const il::runtime::RuntimeSignature &sig,
                                     std::span<Slot> args,
                                     PowStatus &powStatus);
PowTrapOutcome classifyPowTrap(const il::runtime::RuntimeDescriptor &desc,
                               const PowStatus &powStatus,
                               std::span<const Slot> args,
                               const ResultBuffers &buffers);
Slot assignCallResult(const il::runtime::RuntimeSignature &signature, const ResultBuffers &buffers);

} // namespace il::vm
