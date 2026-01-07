//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/vm/OpcodeNames.hpp
// Purpose: Provide a stable opcode->name helper for VM diagnostics.
// Key invariants: Forwards to the canonical IL opcode name table to avoid
//                 duplication and drift.
// Ownership/Lifetime: Header-only convenience; no state or allocation.
// Links: docs/il-guide.md
//
//===----------------------------------------------------------------------===//

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
