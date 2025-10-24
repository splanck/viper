// File: tests/codegen/x86_64/SysVTargetShim.cpp
// Purpose: Provide a runtime definition of sysvTarget() for backend smoke tests.
// Key invariants: Mirrors the TargetInfo configuration from TargetX64.cpp so the
//                 backend observes the expected register allocation metadata.
// Ownership/Lifetime: Returns a reference to a singleton TargetInfo constructed on
//                     first use. The instance lives for the duration of the program.
// Links: src/codegen/x86_64/TargetX64.cpp

#include "codegen/x86_64/TargetX64.hpp"

namespace viper::codegen::x64
{
namespace
{

[[nodiscard]] TargetInfo buildSysVTarget()
{
    TargetInfo info{};
    info.callerSavedGPR = {PhysReg::RSI, PhysReg::RDI, PhysReg::RAX};
    info.calleeSavedGPR = {};
    info.callerSavedXMM = {};
    info.calleeSavedXMM = {};
    info.intArgOrder = {PhysReg::RDI, PhysReg::RSI, PhysReg::RDX, PhysReg::RCX, PhysReg::R8, PhysReg::R9};
    info.f64ArgOrder = {PhysReg::XMM0,
                        PhysReg::XMM1,
                        PhysReg::XMM2,
                        PhysReg::XMM3,
                        PhysReg::XMM4,
                        PhysReg::XMM5,
                        PhysReg::XMM6,
                        PhysReg::XMM7};
    info.intReturnReg = PhysReg::RAX;
    info.f64ReturnReg = PhysReg::XMM0;
    info.stackAlignment = 16U;
    info.hasRedZone = true;
    return info;
}

TargetInfo sysvTargetStorage = buildSysVTarget();

} // namespace

[[gnu::used]] constexpr TargetInfo &sysvTarget() noexcept
{
    return sysvTargetStorage;
}

} // namespace viper::codegen::x64
