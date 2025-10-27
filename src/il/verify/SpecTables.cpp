//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/verify/SpecTables.cpp
// Purpose: Define schema-driven instruction specification tables for the verifier.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements accessors for the instruction specification tables.
/// @details The tables are generated from the unified opcode schema so that the
///          verifier can validate instructions without maintaining bespoke
///          switch ladders.  This translation unit provides lightweight lookup
///          helpers that expose the generated data.

#include "il/verify/SpecTables.hpp"

#include <array>
#include <cstddef>

namespace il::verify::spec
{
namespace
{

#include "il/verify/generated/InstructionSpec.inc"

} // namespace

const InstructionSpec &lookup(il::core::Opcode opcode)
{
    const auto index = static_cast<std::size_t>(opcode);
    return kInstructionSpecs[index];
}

const std::array<InstructionSpec, il::core::kNumOpcodes> &all()
{
    return kInstructionSpecs;
}

} // namespace il::verify::spec

