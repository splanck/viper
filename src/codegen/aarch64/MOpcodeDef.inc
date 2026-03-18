//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/MOpcodeDef.inc
// Purpose: X-macro definition file for AArch64 Machine IR opcodes. This is the
//          single source of truth for the MOpcode enum and name table. Include
//          this file with VIPER_MIR_OPCODE(name) defined to generate:
//            - The enum body (in MachineIR.hpp)
//            - The opcodeName switch cases (in MachineIR.cpp)
//            - Any future metadata tables
//
// Key invariants:
//   - Order must match the historical enum order for ABI stability.
//   - Every opcode here must have an encoding handler or be a pseudo-op.
//   - New opcodes are added ONLY here; enum and name table are auto-generated.
//
// Ownership/Lifetime: Stateless macro file, included multiple times.
// Links: codegen/aarch64/MachineIR.hpp, plans/audit-02-mir-opcode-generation.md
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_MIR_OPCODE
#error "Define VIPER_MIR_OPCODE(name) before including MOpcodeDef.inc"
#endif

// --- GPR move ---
VIPER_MIR_OPCODE(MovRR)
VIPER_MIR_OPCODE(MovRI)
// --- Floating-point (64-bit, scalar) ---
VIPER_MIR_OPCODE(FMovRR)
VIPER_MIR_OPCODE(FMovRI)
VIPER_MIR_OPCODE(FMovGR)
VIPER_MIR_OPCODE(FAddRRR)
VIPER_MIR_OPCODE(FSubRRR)
VIPER_MIR_OPCODE(FMulRRR)
VIPER_MIR_OPCODE(FDivRRR)
VIPER_MIR_OPCODE(FCmpRR)
// --- Integer<->Float conversions (64-bit) ---
VIPER_MIR_OPCODE(SCvtF)
VIPER_MIR_OPCODE(FCvtZS)
VIPER_MIR_OPCODE(UCvtF)
VIPER_MIR_OPCODE(FCvtZU)
// --- Floating-point rounding ---
VIPER_MIR_OPCODE(FRintN)
// --- Stack pointer adjust ---
VIPER_MIR_OPCODE(SubSpImm)
VIPER_MIR_OPCODE(AddSpImm)
// --- Store to outgoing arg area ---
VIPER_MIR_OPCODE(StrRegSpImm)
VIPER_MIR_OPCODE(StrFprSpImm)
// --- Load/store from frame pointer ---
VIPER_MIR_OPCODE(LdrRegFpImm)
VIPER_MIR_OPCODE(StrRegFpImm)
VIPER_MIR_OPCODE(LdrFprFpImm)
VIPER_MIR_OPCODE(StrFprFpImm)
// --- Phi-edge copies ---
VIPER_MIR_OPCODE(PhiStoreGPR)
VIPER_MIR_OPCODE(PhiStoreFPR)
VIPER_MIR_OPCODE(AddFpImm)
// --- Load/store from arbitrary base register ---
VIPER_MIR_OPCODE(LdrRegBaseImm)
VIPER_MIR_OPCODE(StrRegBaseImm)
VIPER_MIR_OPCODE(LdrFprBaseImm)
VIPER_MIR_OPCODE(StrFprBaseImm)
// --- Integer arithmetic ---
VIPER_MIR_OPCODE(AddRRR)
VIPER_MIR_OPCODE(SubRRR)
VIPER_MIR_OPCODE(MulRRR)
VIPER_MIR_OPCODE(SmulhRRR)
VIPER_MIR_OPCODE(SDivRRR)
VIPER_MIR_OPCODE(UDivRRR)
VIPER_MIR_OPCODE(MSubRRRR)
// --- Compare and branch ---
VIPER_MIR_OPCODE(Cbz)
// --- Bitwise ---
VIPER_MIR_OPCODE(AndRRR)
VIPER_MIR_OPCODE(OrrRRR)
VIPER_MIR_OPCODE(EorRRR)
VIPER_MIR_OPCODE(AndRI)
VIPER_MIR_OPCODE(OrrRI)
VIPER_MIR_OPCODE(EorRI)
// --- Immediate arithmetic ---
VIPER_MIR_OPCODE(AddRI)
VIPER_MIR_OPCODE(SubRI)
// --- Shifts ---
VIPER_MIR_OPCODE(LslRI)
VIPER_MIR_OPCODE(LsrRI)
VIPER_MIR_OPCODE(AsrRI)
VIPER_MIR_OPCODE(LslvRRR)
VIPER_MIR_OPCODE(LsrvRRR)
VIPER_MIR_OPCODE(AsrvRRR)
// --- Compare ---
VIPER_MIR_OPCODE(CmpRR)
VIPER_MIR_OPCODE(CmpRI)
VIPER_MIR_OPCODE(TstRR)
VIPER_MIR_OPCODE(Cset)
// --- Control flow ---
VIPER_MIR_OPCODE(Br)
VIPER_MIR_OPCODE(BCond)
VIPER_MIR_OPCODE(Bl)
VIPER_MIR_OPCODE(Blr)
VIPER_MIR_OPCODE(Ret)
// --- Address materialisation ---
VIPER_MIR_OPCODE(AdrPage)
VIPER_MIR_OPCODE(AddPageOff)
// --- Peephole optimization opcodes ---
VIPER_MIR_OPCODE(Cbnz)
VIPER_MIR_OPCODE(MAddRRRR)
VIPER_MIR_OPCODE(Csel)
VIPER_MIR_OPCODE(LdpRegFpImm)
VIPER_MIR_OPCODE(StpRegFpImm)
VIPER_MIR_OPCODE(LdpFprFpImm)
VIPER_MIR_OPCODE(StpFprFpImm)
// --- Flag-setting arithmetic ---
VIPER_MIR_OPCODE(AddsRRR)
VIPER_MIR_OPCODE(SubsRRR)
VIPER_MIR_OPCODE(AddsRI)
VIPER_MIR_OPCODE(SubsRI)
// --- Overflow-checked pseudo-opcodes ---
VIPER_MIR_OPCODE(AddOvfRRR)
VIPER_MIR_OPCODE(SubOvfRRR)
VIPER_MIR_OPCODE(AddOvfRI)
VIPER_MIR_OPCODE(SubOvfRI)
VIPER_MIR_OPCODE(MulOvfRRR)

#undef VIPER_MIR_OPCODE
