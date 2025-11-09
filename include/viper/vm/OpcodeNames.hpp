// include/viper/vm/OpcodeNames.hpp
// Purpose: Provide a stable opcode->name helper for VM diagnostics.
// Invariants: Forwards to the canonical IL opcode name table; no duplication.
// Ownership: Header-only convenience; no state or allocation.

#pragma once

#include "il/core/Opcode.hpp"

#include <string_view>

namespace il::vm
{

/// @brief Return the stable mnemonic for an IL opcode.
/// @details This forwards to the canonical IL core mapping, ensuring VM
///          diagnostics and tools use the same mnemonic set as the rest of the
///          toolchain.
inline std::string_view opcodeName(il::core::Opcode op) noexcept
{
    return il::core::toString(op);
}

} // namespace il::vm

