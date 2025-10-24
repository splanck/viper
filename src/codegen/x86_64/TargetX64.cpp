// src/codegen/x86_64/TargetX64.cpp
// SPDX-License-Identifier: MIT
//
// Purpose: Implement helper routines and singleton construction for the SysV AMD64
//          target description used by the Viper x86-64 backend.
// Invariants: Singleton data is initialised once with ABI-compliant register sets and
//             remains immutable through program execution.
// Ownership: All data is stored in static duration objects; no manual resource
//            management is required.
// Notes: Translation unit stands alone aside from its matching header include.

#include "TargetX64.hpp"

namespace viper::codegen::x64
{

namespace
{

TargetInfo makeSysVTarget()
{
    TargetInfo info{};
    info.callerSavedGPR = {
        PhysReg::RAX,
        PhysReg::RCX,
        PhysReg::RDX,
        PhysReg::RSI,
        PhysReg::RDI,
        PhysReg::R8,
        PhysReg::R9,
        PhysReg::R10,
        PhysReg::R11,
    };
    info.calleeSavedGPR = {
        PhysReg::RBX,
        PhysReg::R12,
        PhysReg::R13,
        PhysReg::R14,
        PhysReg::R15,
        PhysReg::RBP,
    };
    info.callerSavedXMM = {
        PhysReg::XMM0,
        PhysReg::XMM1,
        PhysReg::XMM2,
        PhysReg::XMM3,
        PhysReg::XMM4,
        PhysReg::XMM5,
        PhysReg::XMM6,
        PhysReg::XMM7,
        PhysReg::XMM8,
        PhysReg::XMM9,
        PhysReg::XMM10,
        PhysReg::XMM11,
        PhysReg::XMM12,
        PhysReg::XMM13,
        PhysReg::XMM14,
        PhysReg::XMM15,
    };
    info.calleeSavedXMM = {};
    info.intArgOrder = {
        PhysReg::RDI,
        PhysReg::RSI,
        PhysReg::RDX,
        PhysReg::RCX,
        PhysReg::R8,
        PhysReg::R9,
    };
    info.f64ArgOrder = {
        PhysReg::XMM0,
        PhysReg::XMM1,
        PhysReg::XMM2,
        PhysReg::XMM3,
        PhysReg::XMM4,
        PhysReg::XMM5,
        PhysReg::XMM6,
        PhysReg::XMM7,
    };
    info.intReturnReg = PhysReg::RAX;
    info.f64ReturnReg = PhysReg::XMM0;
    info.stackAlignment = 16U;
    info.hasRedZone = true; // Phase A: do not rely on red zone.
    return info;
}

TargetInfo sysvTargetInstance = makeSysVTarget();

} // namespace

constexpr TargetInfo &sysvTarget() noexcept
{
    return sysvTargetInstance;
}

bool isGPR(PhysReg reg) noexcept
{
    switch (reg)
    {
        case PhysReg::RAX:
        case PhysReg::RBX:
        case PhysReg::RCX:
        case PhysReg::RDX:
        case PhysReg::RSI:
        case PhysReg::RDI:
        case PhysReg::R8:
        case PhysReg::R9:
        case PhysReg::R10:
        case PhysReg::R11:
        case PhysReg::R12:
        case PhysReg::R13:
        case PhysReg::R14:
        case PhysReg::R15:
        case PhysReg::RBP:
        case PhysReg::RSP:
            return true;
        default:
            return false;
    }
}

bool isXMM(PhysReg reg) noexcept
{
    switch (reg)
    {
        case PhysReg::XMM0:
        case PhysReg::XMM1:
        case PhysReg::XMM2:
        case PhysReg::XMM3:
        case PhysReg::XMM4:
        case PhysReg::XMM5:
        case PhysReg::XMM6:
        case PhysReg::XMM7:
        case PhysReg::XMM8:
        case PhysReg::XMM9:
        case PhysReg::XMM10:
        case PhysReg::XMM11:
        case PhysReg::XMM12:
        case PhysReg::XMM13:
        case PhysReg::XMM14:
        case PhysReg::XMM15:
            return true;
        default:
            return false;
    }
}

const char *regName(PhysReg reg) noexcept
{
    switch (reg)
    {
        case PhysReg::RAX:
            return "%rax";
        case PhysReg::RBX:
            return "%rbx";
        case PhysReg::RCX:
            return "%rcx";
        case PhysReg::RDX:
            return "%rdx";
        case PhysReg::RSI:
            return "%rsi";
        case PhysReg::RDI:
            return "%rdi";
        case PhysReg::R8:
            return "%r8";
        case PhysReg::R9:
            return "%r9";
        case PhysReg::R10:
            return "%r10";
        case PhysReg::R11:
            return "%r11";
        case PhysReg::R12:
            return "%r12";
        case PhysReg::R13:
            return "%r13";
        case PhysReg::R14:
            return "%r14";
        case PhysReg::R15:
            return "%r15";
        case PhysReg::RBP:
            return "%rbp";
        case PhysReg::RSP:
            return "%rsp";
        case PhysReg::XMM0:
            return "%xmm0";
        case PhysReg::XMM1:
            return "%xmm1";
        case PhysReg::XMM2:
            return "%xmm2";
        case PhysReg::XMM3:
            return "%xmm3";
        case PhysReg::XMM4:
            return "%xmm4";
        case PhysReg::XMM5:
            return "%xmm5";
        case PhysReg::XMM6:
            return "%xmm6";
        case PhysReg::XMM7:
            return "%xmm7";
        case PhysReg::XMM8:
            return "%xmm8";
        case PhysReg::XMM9:
            return "%xmm9";
        case PhysReg::XMM10:
            return "%xmm10";
        case PhysReg::XMM11:
            return "%xmm11";
        case PhysReg::XMM12:
            return "%xmm12";
        case PhysReg::XMM13:
            return "%xmm13";
        case PhysReg::XMM14:
            return "%xmm14";
        case PhysReg::XMM15:
            return "%xmm15";
        default:
            return "%unknown";
    }
}

} // namespace viper::codegen::x64
