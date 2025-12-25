//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/Lowering.EH.cpp
// Purpose: Provide Phase A lowering stubs for IL EH markers so functions that
//          contain them still compile. Proper unwind info is intentionally out
//          of scope for Phase A.
//
//===----------------------------------------------------------------------===//

#include "LoweringRuleTable.hpp"

#include "LowerILToMIR.hpp"
#include "MachineIR.hpp"

namespace viper::codegen::x64::lowering
{

void emitEhPush(const ILInstr &, MIRBuilder &)
{
    // Phase A: no-op. Runtime EH machinery is VM-only; native codegen omits
    // explicit handler stack manipulation.
}

void emitEhPop(const ILInstr &, MIRBuilder &)
{
    // Phase A: no-op.
}

void emitEhEntry(const ILInstr &, MIRBuilder &)
{
    // Phase A: no-op. The handler block is already materialised as a MIR block
    // with a label; an extra in-block label is not required for emission.
}

void emitTrap(const ILInstr &, MIRBuilder &builder)
{
    // Emit x86 UD2 (undefined instruction) to signal an unrecoverable error.
    builder.append(MInstr::make(MOpcode::UD2));
}

} // namespace viper::codegen::x64::lowering
