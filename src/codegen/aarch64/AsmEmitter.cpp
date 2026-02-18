//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file AsmEmitter.cpp
/// @brief Assembly text emission for AArch64 Machine IR.
///
/// This file implements the final stage of the AArch64 code generation pipeline,
/// converting register-allocated Machine IR into assembler-compatible text that
/// can be processed by the system assembler (as/clang).
///
/// **What is the AsmEmitter?**
/// The AsmEmitter translates MIR instructions with physical registers into
/// AArch64 assembly syntax. It handles:
/// - Instruction mnemonic selection
/// - Register name formatting (x0-x30, d0-d31, sp, fp)
/// - Immediate operand formatting
/// - Symbol mangling for the target platform
/// - Prologue/epilogue generation for function frames
///
/// **Output Format:**
/// The emitter produces GNU Assembler (GAS) compatible syntax:
/// ```asm
/// .text
/// .align 2
/// .globl _main
/// _main:
///   stp x29, x30, [sp, #-16]!
///   mov x29, sp
///   mov x0, #42
///   bl _print_i64
///   mov x0, #0
///   ldp x29, x30, [sp], #16
///   ret
/// ```
///
/// **Symbol Mangling:**
/// The emitter applies platform-specific name mangling:
/// | Platform | C Symbol `foo`  | Reason                    |
/// |----------|-----------------|---------------------------|
/// | Darwin   | `_foo`          | Underscore prefix         |
/// | Linux    | `foo`           | No prefix                 |
///
/// **Runtime Symbol Mapping:**
/// IL extern names are mapped to C runtime symbols:
/// | IL Name                    | C Symbol        |
/// |----------------------------|-----------------|
/// | Viper.Console.PrintI64     | rt_print_i64    |
/// | Viper.String.Concat        | rt_str_concat   |
/// | Viper.Math.Sqrt            | rt_math_sqrt    |
///
/// **Function Structure:**
/// ```
/// emitFunctionHeader()     ; .text, .globl, label
/// emitPrologue()           ; stp fp/lr, allocate frame
///   ... instruction body ...
/// emitEpilogue()           ; deallocate frame, ldp fp/lr, ret
/// ```
///
/// **Prologue/Epilogue Generation:**
/// The emitter generates standard AAPCS64-compliant function prologues:
/// ```asm
/// ; Prologue
/// stp x29, x30, [sp, #-16]!   ; Save FP and LR, update SP
/// mov x29, sp                  ; Set up frame pointer
/// sub sp, sp, #<framesize>     ; Allocate local frame (if needed)
/// stp x19, x20, [sp, #-16]!    ; Save callee-saved regs (if used)
///
/// ; Epilogue
/// ldp x19, x20, [sp], #16      ; Restore callee-saved regs
/// add sp, sp, #<framesize>     ; Deallocate local frame
/// ldp x29, x30, [sp], #16      ; Restore FP and LR
/// ret                          ; Return to caller
/// ```
///
/// **Instruction Emission:**
/// Each MIR opcode maps to one or more assembly instructions:
/// | MOpcode       | Assembly Output            |
/// |---------------|----------------------------|
/// | MovRR         | mov xd, xn                 |
/// | MovRI         | mov xd, #imm               |
/// | AddRRR        | add xd, xn, xm             |
/// | LdrRegFpImm   | ldr xd, [x29, #offset]     |
/// | Bl            | bl <symbol>                |
/// | BCond         | b.<cond> <label>           |
///
/// **Register Naming:**
/// | PhysReg | GPR Name | FPR Name | Special |
/// |---------|----------|----------|---------|
/// | X0-X28  | x0-x28   | -        | -       |
/// | X29     | x29      | -        | fp      |
/// | X30     | x30      | -        | lr      |
/// | SP      | sp       | -        | -       |
/// | D0-D31  | -        | d0-d31   | -       |
///
/// @see MachineIR.hpp For MIR type definitions
/// @see RegAllocLinear.cpp For register allocation
/// @see TargetAArch64.hpp For register and ABI definitions
///
//===----------------------------------------------------------------------===//

#include "AsmEmitter.hpp"

#include "codegen/common/LabelUtil.hpp"
#include "il/runtime/RuntimeNameMap.hpp"

#include <cstring>
#include <iomanip>

namespace viper::codegen::aarch64
{

/// @brief Map IL extern names to C runtime symbol names.
/// The IL uses namespaced names like "Viper.Console.PrintI64" but the runtime
/// exports C-style names like "rt_print_i64".
static std::string mapRuntimeSymbol(const std::string &name)
{
    if (auto mapped = il::runtime::mapCanonicalRuntimeName(name))
        return std::string(*mapped);
    // Not a known runtime symbol, return as-is.
    return name;
}

/// @brief Mangle a symbol name for the target platform.
/// On Darwin (macOS), C symbols require an underscore prefix.
/// Local labels (starting with L or .) are not mangled.
/// On Linux ELF, no prefix is applied.
static std::string mangleSymbolImpl(const std::string &name, bool isDarwin)
{
    if (isDarwin)
    {
        // Don't mangle local labels (L* or .L*)
        if (!name.empty() && (name[0] == 'L' || name[0] == '.'))
            return name;
        return "_" + name;
    }
    return name;
}

/// @brief Mangle a call target symbol for emission.
/// This first maps IL runtime names to C runtime names, then applies platform mangling.
static std::string mangleCallTargetImpl(const std::string &name, bool isDarwin)
{
    return mangleSymbolImpl(mapRuntimeSymbol(name), isDarwin);
}

/// @brief Sanitize a label for assembly output.
/// @details Delegates to the common label sanitizer which replaces hyphens
///          with underscores and handles other illegal assembly characters.
/// @param name Original label identifier.
/// @return Sanitized copy suitable for assembly.
static std::string sanitizeLabel(const std::string &name)
{
    return viper::codegen::common::sanitizeLabel(name);
}

void AsmEmitter::emitFunctionHeader(std::ostream &os, const std::string &name) const
{
    const bool darwin = !target_->isLinux();
    os << ".text\n";
    os << ".align 2\n";

    const std::string sym = mangleSymbolImpl(name, darwin);

    // On Darwin, skip .globl for L*/_L*-prefixed local labels.
    // On Linux, always emit .globl followed by .type for ELF function metadata.
    if (darwin)
    {
        if (!(sym.size() >= 1 &&
              (sym[0] == 'L' || (sym.size() >= 2 && sym[0] == '_' && sym[1] == 'L'))))
        {
            os << ".globl " << sym << "\n";
        }
    }
    else
    {
        os << ".globl " << sym << "\n";
        os << ".type " << sym << ", @function\n";
    }
    os << sym << ":\n";
}

void AsmEmitter::emitPrologue(std::ostream &os) const
{
    // stp x29, x30, [sp, #-16]!; mov x29, sp
    os << "  stp x29, x30, [sp, #-16]!\n";
    os << "  mov x29, sp\n";
}

void AsmEmitter::emitEpilogue(std::ostream &os) const
{
    // ldp x29, x30, [sp], #16; ret
    os << "  ldp x29, x30, [sp], #16\n";
    os << "  ret\n";
}

void AsmEmitter::emitPrologue(std::ostream &os, const FramePlan &plan) const
{
    emitPrologue(os);
    if (plan.localFrameSize > 0)
    {
        /// @brief Emits subsp.
        emitSubSp(os, plan.localFrameSize);
    }
    for (std::size_t i = 0; i < plan.saveGPRs.size();)
    {
        const PhysReg r0 = plan.saveGPRs[i++];
        if (i < plan.saveGPRs.size())
        {
            const PhysReg r1 = plan.saveGPRs[i++];
            os << "  stp " << rn(r0) << ", " << rn(r1) << ", [sp, #-16]!\n";
        }
        else
        {
            os << "  str " << rn(r0) << ", [sp, #-16]!\n";
        }
    }
    for (std::size_t i = 0; i < plan.saveFPRs.size();)
    {
        const PhysReg r0 = plan.saveFPRs[i++];
        if (i < plan.saveFPRs.size())
        {
            const PhysReg r1 = plan.saveFPRs[i++];
            os << "  stp ";
            printD(os, r0);
            os << ", ";
            printD(os, r1);
            os << ", [sp, #-16]!\n";
        }
        else
        {
            os << "  str ";
            printD(os, r0);
            os << ", [sp, #-16]!\n";
        }
    }
}

void AsmEmitter::emitEpilogue(std::ostream &os, const FramePlan &plan) const
{
    // Restore in reverse order of saves, then FP/LR
    // Compute how many stores we emitted.
    std::size_t n = plan.saveGPRs.size();
    if (n % 2 == 1)
    {
        const PhysReg r0 = plan.saveGPRs[n - 1];
        os << "  ldr " << rn(r0) << ", [sp], #16\n";
        --n;
    }
    while (n > 0)
    {
        const PhysReg r1 = plan.saveGPRs[n - 1];
        const PhysReg r0 = plan.saveGPRs[n - 2];
        os << "  ldp " << rn(r0) << ", " << rn(r1) << ", [sp], #16\n";
        n -= 2;
    }
    // Restore FPRs in reverse
    std::size_t nf = plan.saveFPRs.size();
    if (nf % 2 == 1)
    {
        const PhysReg r0 = plan.saveFPRs[nf - 1];
        os << "  ldr ";
        printD(os, r0);
        os << ", [sp], #16\n";
        --nf;
    }
    while (nf > 0)
    {
        const PhysReg r1 = plan.saveFPRs[nf - 1];
        const PhysReg r0 = plan.saveFPRs[nf - 2];
        os << "  ldp ";
        printD(os, r0);
        os << ", ";
        printD(os, r1);
        os << ", [sp], #16\n";
        nf -= 2;
    }
    if (plan.localFrameSize > 0)
    {
        /// @brief Emits addsp.
        emitAddSp(os, plan.localFrameSize);
    }
    emitEpilogue(os);
}

void AsmEmitter::emitMovRR(std::ostream &os, PhysReg dst, PhysReg src) const
{
    os << "  mov " << rn(dst) << ", " << rn(src) << "\n";
}

void AsmEmitter::emitMovRI(std::ostream &os, PhysReg dst, long long imm) const
{
    // Use movz/movk sequence for wide immediates that can't be encoded directly
    if (needsWideImmSequence(imm))
    {
        emitMovImm64(os, dst, static_cast<unsigned long long>(imm));
    }
    else
    {
        os << "  mov " << rn(dst) << ", #" << imm << "\n";
    }
}

void AsmEmitter::emitAddRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  add " << rn(dst) << ", " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitSubRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  sub " << rn(dst) << ", " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitMulRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    // AArch64 mul: mul xd, xn, xm
    os << "  mul " << rn(dst) << ", " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitSDivRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    // AArch64 signed division: sdiv xd, xn, xm
    os << "  sdiv " << rn(dst) << ", " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitUDivRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    // AArch64 unsigned division: udiv xd, xn, xm
    os << "  udiv " << rn(dst) << ", " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitMSubRRRR(
    std::ostream &os, PhysReg dst, PhysReg mul1, PhysReg mul2, PhysReg sub) const
{
    // AArch64 multiply-subtract: msub xd, xn, xm, xa => xd = xa - xn*xm
    // Used for remainder: rem = dividend - (dividend/divisor)*divisor
    os << "  msub " << rn(dst) << ", " << rn(mul1) << ", " << rn(mul2) << ", " << rn(sub) << "\n";
}

void AsmEmitter::emitCbz(std::ostream &os, PhysReg reg, const std::string &label) const
{
    // AArch64 compare and branch if zero: cbz xn, label
    os << "  cbz " << rn(reg) << ", " << label << "\n";
}

void AsmEmitter::emitAddRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long imm) const
{
    os << "  add " << rn(dst) << ", " << rn(lhs) << ", #" << imm << "\n";
}

void AsmEmitter::emitSubRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long imm) const
{
    os << "  sub " << rn(dst) << ", " << rn(lhs) << ", #" << imm << "\n";
}

void AsmEmitter::emitAndRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  and " << rn(dst) << ", " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitOrrRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  orr " << rn(dst) << ", " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitEorRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  eor " << rn(dst) << ", " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitAndRI(std::ostream &os, PhysReg dst, PhysReg src, long long imm) const
{
    os << "  and " << rn(dst) << ", " << rn(src) << ", #" << imm << "\n";
}

void AsmEmitter::emitOrrRI(std::ostream &os, PhysReg dst, PhysReg src, long long imm) const
{
    os << "  orr " << rn(dst) << ", " << rn(src) << ", #" << imm << "\n";
}

void AsmEmitter::emitEorRI(std::ostream &os, PhysReg dst, PhysReg src, long long imm) const
{
    os << "  eor " << rn(dst) << ", " << rn(src) << ", #" << imm << "\n";
}

void AsmEmitter::emitLslRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long sh) const
{
    os << "  lsl " << rn(dst) << ", " << rn(lhs) << ", #" << sh << "\n";
}

void AsmEmitter::emitLsrRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long sh) const
{
    os << "  lsr " << rn(dst) << ", " << rn(lhs) << ", #" << sh << "\n";
}

void AsmEmitter::emitAsrRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long sh) const
{
    os << "  asr " << rn(dst) << ", " << rn(lhs) << ", #" << sh << "\n";
}

void AsmEmitter::emitLslvRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  lslv " << rn(dst) << ", " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitLsrvRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  lsrv " << rn(dst) << ", " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitAsrvRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  asrv " << rn(dst) << ", " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitCmpRR(std::ostream &os, PhysReg lhs, PhysReg rhs) const
{
    os << "  cmp " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitCmpRI(std::ostream &os, PhysReg lhs, long long imm) const
{
    // ARM64 cmp immediate range: 0-4095 (12-bit unsigned)
    // ARM64 cmn immediate range: 0-4095 (equivalent to cmp with negated value)
    // The assembler accepts "cmp xn, #-imm" and translates to "cmn xn, #imm"
    if (imm >= 0 && imm <= 4095)
    {
        os << "  cmp " << rn(lhs) << ", #" << imm << "\n";
    }
    else if (imm >= -4095 && imm < 0)
    {
        // Negative immediates in cmn range - emit directly, assembler handles it
        os << "  cmp " << rn(lhs) << ", #" << imm << "\n";
    }
    else
    {
        // Use x16 (IP0) as scratch register - it's caller-saved and not used for args
        emitMovImm64(os, PhysReg::X16, imm);
        os << "  cmp " << rn(lhs) << ", " << rn(PhysReg::X16) << "\n";
    }
}

void AsmEmitter::emitTstRR(std::ostream &os, PhysReg lhs, PhysReg rhs) const
{
    os << "  tst " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitCset(std::ostream &os, PhysReg dst, const char *cond) const
{
    os << "  cset " << rn(dst) << ", " << cond << "\n";
}

void AsmEmitter::emitSubSp(std::ostream &os, long long bytes) const
{
    // ARM64 add/sub immediate supports 12-bit unsigned values (0-4095).
    // For larger frames, we need to break it into multiple instructions.
    // Use 4080 (not 4095) to maintain 16-byte SP alignment between steps —
    // AArch64 requires SP to be 16-byte aligned when accessed via SP-relative
    // addressing, and a signal/interrupt between steps would see misaligned SP.
    constexpr long long kMaxImm = 4080;
    while (bytes > kMaxImm)
    {
        os << "  sub sp, sp, #" << kMaxImm << "\n";
        bytes -= kMaxImm;
    }
    if (bytes > 0)
    {
        os << "  sub sp, sp, #" << bytes << "\n";
    }
}

void AsmEmitter::emitAddSp(std::ostream &os, long long bytes) const
{
    // ARM64 add/sub immediate supports 12-bit unsigned values (0-4095).
    // For larger frames, we need to break it into multiple instructions.
    // Use 4080 (not 4095) to maintain 16-byte SP alignment — see emitSubSp.
    constexpr long long kMaxImm = 4080;
    while (bytes > kMaxImm)
    {
        os << "  add sp, sp, #" << kMaxImm << "\n";
        bytes -= kMaxImm;
    }
    if (bytes > 0)
    {
        os << "  add sp, sp, #" << bytes << "\n";
    }
}

void AsmEmitter::emitStrToSp(std::ostream &os, PhysReg src, long long offset) const
{
    os << "  str " << rn(src) << ", [sp, #" << offset << "]\n";
}

void AsmEmitter::emitStrFprToSp(std::ostream &os, PhysReg src, long long offset) const
{
    os << "  str ";
    printD(os, src);
    os << ", [sp, #" << offset << "]\n";
}

/// @brief Check if offset is in ARM64 signed immediate range for str/ldr instructions.
/// The signed unscaled immediate for str/ldr is [-256, 255].
static bool isInSignedImmRange(long long offset)
{
    return offset >= -256 && offset <= 255;
}

void AsmEmitter::emitLdrFromFp(std::ostream &os, PhysReg dst, long long offset) const
{
    if (isInSignedImmRange(offset))
    {
        os << "  ldr " << rn(dst) << ", [x29, #" << offset << "]\n";
    }
    else
    {
        // Large offset: use scratch register x9 to compute address
        // mov x9, #offset
        // add x9, x29, x9
        // ldr dst, [x9]
        emitMovRI(os, kScratchGPR, offset);
        os << "  add " << rn(kScratchGPR) << ", x29, " << rn(kScratchGPR) << "\n";
        os << "  ldr " << rn(dst) << ", [" << rn(kScratchGPR) << "]\n";
    }
}

void AsmEmitter::emitStrToFp(std::ostream &os, PhysReg src, long long offset) const
{
    if (isInSignedImmRange(offset))
    {
        os << "  str " << rn(src) << ", [x29, #" << offset << "]\n";
    }
    else
    {
        // Large offset: use scratch register x9 to compute address
        // mov x9, #offset
        // add x9, x29, x9
        // str src, [x9]
        emitMovRI(os, kScratchGPR, offset);
        os << "  add " << rn(kScratchGPR) << ", x29, " << rn(kScratchGPR) << "\n";
        os << "  str " << rn(src) << ", [" << rn(kScratchGPR) << "]\n";
    }
}

void AsmEmitter::emitLdrFprFromFp(std::ostream &os, PhysReg dst, long long offset) const
{
    if (isInSignedImmRange(offset))
    {
        os << "  ldr ";
        printD(os, dst);
        os << ", [x29, #" << offset << "]\n";
    }
    else
    {
        // Large offset: use scratch register x9 to compute address
        emitMovRI(os, kScratchGPR, offset);
        os << "  add " << rn(kScratchGPR) << ", x29, " << rn(kScratchGPR) << "\n";
        os << "  ldr ";
        printD(os, dst);
        os << ", [" << rn(kScratchGPR) << "]\n";
    }
}

void AsmEmitter::emitStrFprToFp(std::ostream &os, PhysReg src, long long offset) const
{
    if (isInSignedImmRange(offset))
    {
        os << "  str ";
        printD(os, src);
        os << ", [x29, #" << offset << "]\n";
    }
    else
    {
        // Large offset: use scratch register x9 to compute address
        emitMovRI(os, kScratchGPR, offset);
        os << "  add " << rn(kScratchGPR) << ", x29, " << rn(kScratchGPR) << "\n";
        os << "  str ";
        printD(os, src);
        os << ", [" << rn(kScratchGPR) << "]\n";
    }
}

void AsmEmitter::emitAddFpImm(std::ostream &os, PhysReg dst, long long offset) const
{
    // Compute dst = x29 + offset (where offset is typically negative for locals)
    // ARM64 add/sub immediate can only handle 12-bit unsigned values,
    // so we use sub for negative offsets
    if (offset >= 0 && offset <= 4095)
    {
        os << "  add " << rn(dst) << ", x29, #" << offset << "\n";
    }
    else if (offset < 0 && -offset <= 4095)
    {
        os << "  sub " << rn(dst) << ", x29, #" << -offset << "\n";
    }
    else
    {
        // Large offset: use scratch register to load offset, then add
        emitMovRI(os, kScratchGPR, offset);
        os << "  add " << rn(dst) << ", x29, " << rn(kScratchGPR) << "\n";
    }
}

void AsmEmitter::emitLdrFromBase(std::ostream &os,
                                 PhysReg dst,
                                 PhysReg base,
                                 long long offset) const
{
    if (isInSignedImmRange(offset))
    {
        os << "  ldr " << rn(dst) << ", [" << rn(base) << ", #" << offset << "]\n";
    }
    else
    {
        // Large offset: use scratch register x9 to compute address
        emitMovRI(os, kScratchGPR, offset);
        os << "  add " << rn(kScratchGPR) << ", " << rn(base) << ", " << rn(kScratchGPR) << "\n";
        os << "  ldr " << rn(dst) << ", [" << rn(kScratchGPR) << "]\n";
    }
}

void AsmEmitter::emitStrToBase(std::ostream &os, PhysReg src, PhysReg base, long long offset) const
{
    if (isInSignedImmRange(offset))
    {
        os << "  str " << rn(src) << ", [" << rn(base) << ", #" << offset << "]\n";
    }
    else
    {
        // Large offset: use scratch register x9 to compute address
        emitMovRI(os, kScratchGPR, offset);
        os << "  add " << rn(kScratchGPR) << ", " << rn(base) << ", " << rn(kScratchGPR) << "\n";
        os << "  str " << rn(src) << ", [" << rn(kScratchGPR) << "]\n";
    }
}

void AsmEmitter::emitLdrFprFromBase(std::ostream &os,
                                    PhysReg dst,
                                    PhysReg base,
                                    long long offset) const
{
    if (isInSignedImmRange(offset))
    {
        os << "  ldr ";
        printD(os, dst);
        os << ", [" << rn(base) << ", #" << offset << "]\n";
    }
    else
    {
        // Large offset: use scratch register x9 to compute address
        emitMovRI(os, kScratchGPR, offset);
        os << "  add " << rn(kScratchGPR) << ", " << rn(base) << ", " << rn(kScratchGPR) << "\n";
        os << "  ldr ";
        printD(os, dst);
        os << ", [" << rn(kScratchGPR) << "]\n";
    }
}

void AsmEmitter::emitStrFprToBase(std::ostream &os,
                                  PhysReg src,
                                  PhysReg base,
                                  long long offset) const
{
    if (isInSignedImmRange(offset))
    {
        os << "  str ";
        printD(os, src);
        os << ", [" << rn(base) << ", #" << offset << "]\n";
    }
    else
    {
        // Large offset: use scratch register x9 to compute address
        emitMovRI(os, kScratchGPR, offset);
        os << "  add " << rn(kScratchGPR) << ", " << rn(base) << ", " << rn(kScratchGPR) << "\n";
        os << "  str ";
        printD(os, src);
        os << ", [" << rn(kScratchGPR) << "]\n";
    }
}

void AsmEmitter::emitMovZ(std::ostream &os, PhysReg dst, unsigned imm16, unsigned lsl) const
{
    os << "  movz " << rn(dst) << ", #" << imm16;
    if (lsl)
        os << ", lsl #" << lsl;
    os << "\n";
}

void AsmEmitter::emitMovK(std::ostream &os, PhysReg dst, unsigned imm16, unsigned lsl) const
{
    os << "  movk " << rn(dst) << ", #" << imm16;
    if (lsl)
        os << ", lsl #" << lsl;
    os << "\n";
}

void AsmEmitter::emitMovImm64(std::ostream &os, PhysReg dst, unsigned long long value) const
{
    unsigned chunks[4] = {
        static_cast<unsigned>(value & 0xFFFFULL),
        static_cast<unsigned>((value >> 16) & 0xFFFFULL),
        static_cast<unsigned>((value >> 32) & 0xFFFFULL),
        static_cast<unsigned>((value >> 48) & 0xFFFFULL),
    };
    emitMovZ(os, dst, chunks[0], 0);
    if (chunks[1])
        /// @brief Emits movk.
        emitMovK(os, dst, chunks[1], 16);
    if (chunks[2])
        /// @brief Emits movk.
        emitMovK(os, dst, chunks[2], 32);
    if (chunks[3])
        /// @brief Emits movk.
        emitMovK(os, dst, chunks[3], 48);
}

void AsmEmitter::emitRet(std::ostream &os) const
{
    os << "  ret\n";
}

void AsmEmitter::emitFMovRR(std::ostream &os, PhysReg dst, PhysReg src) const
{
    os << "  fmov ";
    printD(os, dst);
    os << ", ";
    printD(os, src);
    os << "\n";
}

void AsmEmitter::emitFMovRI(std::ostream &os, PhysReg dst, double imm) const
{
    const auto savedFlags = os.flags();
    os << std::fixed;
    os << "  fmov ";
    printD(os, dst);
    os << ", #" << imm << "\n";
    os.flags(savedFlags);
}

void AsmEmitter::emitFMovGR(std::ostream &os, PhysReg dst, PhysReg src) const
{
    // fmov dN, xM - transfer bits from GPR to FPR without conversion
    os << "  fmov ";
    printD(os, dst);
    os << ", " << rn(src) << "\n";
}

void AsmEmitter::emitFAddRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  fadd ";
    printD(os, dst);
    os << ", ";
    printD(os, lhs);
    os << ", ";
    printD(os, rhs);
    os << "\n";
}

void AsmEmitter::emitFSubRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  fsub ";
    printD(os, dst);
    os << ", ";
    printD(os, lhs);
    os << ", ";
    printD(os, rhs);
    os << "\n";
}

void AsmEmitter::emitFMulRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  fmul ";
    printD(os, dst);
    os << ", ";
    printD(os, lhs);
    os << ", ";
    printD(os, rhs);
    os << "\n";
}

void AsmEmitter::emitFDivRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  fdiv ";
    printD(os, dst);
    os << ", ";
    printD(os, lhs);
    os << ", ";
    printD(os, rhs);
    os << "\n";
}

void AsmEmitter::emitFCmpRR(std::ostream &os, PhysReg lhs, PhysReg rhs) const
{
    os << "  fcmp ";
    printD(os, lhs);
    os << ", ";
    printD(os, rhs);
    os << "\n";
}

void AsmEmitter::emitSCvtF(std::ostream &os, PhysReg dstFPR, PhysReg srcGPR) const
{
    os << "  scvtf ";
    printD(os, dstFPR);
    os << ", " << rn(srcGPR) << "\n";
}

void AsmEmitter::emitFCvtZS(std::ostream &os, PhysReg dstGPR, PhysReg srcFPR) const
{
    os << "  fcvtzs " << rn(dstGPR) << ", ";
    printD(os, srcFPR);
    os << "\n";
}

void AsmEmitter::emitUCvtF(std::ostream &os, PhysReg dstFPR, PhysReg srcGPR) const
{
    os << "  ucvtf ";
    printD(os, dstFPR);
    os << ", " << rn(srcGPR) << "\n";
}

void AsmEmitter::emitFCvtZU(std::ostream &os, PhysReg dstGPR, PhysReg srcFPR) const
{
    os << "  fcvtzu " << rn(dstGPR) << ", ";
    printD(os, srcFPR);
    os << "\n";
}

void AsmEmitter::emitFRintN(std::ostream &os, PhysReg dstFPR, PhysReg srcFPR) const
{
    os << "  frintn ";
    printD(os, dstFPR);
    os << ", ";
    printD(os, srcFPR);
    os << "\n";
}

void AsmEmitter::emitFunction(std::ostream &os, const MFunction &fn) const
{
    emitFunctionHeader(os, fn.name);
    const bool usePlan = !fn.savedGPRs.empty() || fn.localFrameSize > 0;
    FramePlan plan;
    if (usePlan)
    {
        plan.saveGPRs = fn.savedGPRs;
        plan.saveFPRs = fn.savedFPRs;
        plan.localFrameSize = fn.localFrameSize;
    }
    if (usePlan)
        emitPrologue(os, plan);
    else
        emitPrologue(os);

    // For the main function, initialize the runtime context before executing user code.
    // This is required because runtime functions expect an active RtContext.
    const bool darwin = !target_->isLinux();
    if (fn.name == "main")
    {
        os << "  ; Initialize runtime context for native execution\n";
        os << "  bl " << mangleCallTargetImpl("rt_legacy_context", darwin) << "\n";
        os << "  bl " << mangleCallTargetImpl("rt_set_current_context", darwin) << "\n";
    }

    // Store the plan for use by Ret instructions
    currentPlan_ = &plan;
    currentPlanValid_ = usePlan;

    for (const auto &bb : fn.blocks)
        emitBlock(os, bb);

    currentPlan_ = nullptr;
    currentPlanValid_ = false;

    // On Linux ELF, emit .size after the function body so the linker and
    // profilers can determine the function's byte extent.
    if (!darwin)
    {
        const std::string sym = mangleSymbolImpl(fn.name, /*isDarwin=*/false);
        os << ".size " << sym << ", .-" << sym << "\n";
    }
    // Note: epilogue is emitted by each Ret instruction, not here
}

void AsmEmitter::emitBlock(std::ostream &os, const MBasicBlock &bb) const
{
    if (!bb.name.empty())
        os << sanitizeLabel(bb.name) << ":\n";
    for (const auto &mi : bb.instrs)
        emitInstruction(os, mi);
}

void AsmEmitter::emitInstruction(std::ostream &os, const MInstr &mi) const
{
    // Provide single-argument wrappers used by the generated OpcodeDispatch.inc,
    // which was generated without knowledge of the isDarwin parameter.
    // These lambdas shadow the free-function names within this scope.
    const bool kDarwin_ = !target_->isLinux();
    [[maybe_unused]] auto mangleSymbol = [kDarwin_](const std::string &n) -> std::string
    {
        return mangleSymbolImpl(n, kDarwin_);
    };
    [[maybe_unused]] auto mangleCallTarget = [kDarwin_](const std::string &n) -> std::string
    {
        return mangleCallTargetImpl(n, kDarwin_);
    };

    // Handle Ret specially since it needs the epilogue
    if (mi.opc == MOpcode::Ret)
    {
        // Emit epilogue using the stored frame plan from emitFunction
        if (currentPlanValid_ && currentPlan_)
            emitEpilogue(os, *currentPlan_);
        else
            emitEpilogue(os);
        return;
    }

    // Fallback handling for opcodes that may be missing from the generated
    // dispatch table due to generator drift. Keep these early and return to
    // avoid duplicate emission when present in the generated switch.
    auto getReg = [](const MOperand &op) -> PhysReg
    {
        assert(op.kind == MOperand::Kind::Reg && "expected reg operand");
        assert(op.reg.isPhys && "unallocated vreg reached emitter");
        return static_cast<PhysReg>(op.reg.idOrPhys);
    };
    auto getImm = [](const MOperand &op) -> long long
    {
        assert(op.kind == MOperand::Kind::Imm && "expected imm operand");
        return op.imm;
    };
    switch (mi.opc)
    {
        case MOpcode::SDivRRR:
            emitSDivRRR(os, getReg(mi.ops[0]), getReg(mi.ops[1]), getReg(mi.ops[2]));
            return;
        case MOpcode::UDivRRR:
            emitUDivRRR(os, getReg(mi.ops[0]), getReg(mi.ops[1]), getReg(mi.ops[2]));
            return;
        case MOpcode::MSubRRRR:
            emitMSubRRRR(
                os, getReg(mi.ops[0]), getReg(mi.ops[1]), getReg(mi.ops[2]), getReg(mi.ops[3]));
            return;
        case MOpcode::AddFpImm:
            emitAddFpImm(os, getReg(mi.ops[0]), getImm(mi.ops[1]));
            return;
        case MOpcode::LdrFprBaseImm:
            emitLdrFprFromBase(os, getReg(mi.ops[0]), getReg(mi.ops[1]), getImm(mi.ops[2]));
            return;
        case MOpcode::StrFprBaseImm:
            emitStrFprToBase(os, getReg(mi.ops[0]), getReg(mi.ops[1]), getImm(mi.ops[2]));
            return;
        case MOpcode::TstRR:
            emitTstRR(os, getReg(mi.ops[0]), getReg(mi.ops[1]));
            return;
        case MOpcode::Bl:
            // Direct call to named symbol - apply runtime name mapping and mangling.
            // Uses the local mangleCallTarget lambda (captures kDarwin_).
            os << "  bl " << mangleCallTarget(mi.ops[0].label) << "\n";
            return;
        case MOpcode::Blr:
            os << "  blr " << rn(getReg(mi.ops[0])) << "\n";
            return;
        case MOpcode::Cbz:
            os << "  cbz " << rn(getReg(mi.ops[0])) << ", " << sanitizeLabel(mi.ops[1].label)
               << "\n";
            return;
        case MOpcode::LslvRRR:
            emitLslvRRR(os, getReg(mi.ops[0]), getReg(mi.ops[1]), getReg(mi.ops[2]));
            return;
        case MOpcode::LsrvRRR:
            emitLsrvRRR(os, getReg(mi.ops[0]), getReg(mi.ops[1]), getReg(mi.ops[2]));
            return;
        case MOpcode::AsrvRRR:
            emitAsrvRRR(os, getReg(mi.ops[0]), getReg(mi.ops[1]), getReg(mi.ops[2]));
            return;
        case MOpcode::FMovGR:
            emitFMovGR(os, getReg(mi.ops[0]), getReg(mi.ops[1]));
            return;
        case MOpcode::FRintN:
            emitFRintN(os, getReg(mi.ops[0]), getReg(mi.ops[1]));
            return;
        case MOpcode::Br:
            os << "  b " << sanitizeLabel(mi.ops[0].label) << "\n";
            return;
        case MOpcode::BCond:
            os << "  b." << mi.ops[0].cond << " " << sanitizeLabel(mi.ops[1].label) << "\n";
            return;
        case MOpcode::Cbnz:
            os << "  cbnz " << rn(getReg(mi.ops[0])) << ", " << sanitizeLabel(mi.ops[1].label)
               << "\n";
            return;
        case MOpcode::MAddRRRR:
            os << "  madd " << rn(getReg(mi.ops[0])) << ", " << rn(getReg(mi.ops[1])) << ", "
               << rn(getReg(mi.ops[2])) << ", " << rn(getReg(mi.ops[3])) << "\n";
            return;
        case MOpcode::Csel:
            os << "  csel " << rn(getReg(mi.ops[0])) << ", " << rn(getReg(mi.ops[1])) << ", "
               << rn(getReg(mi.ops[2])) << ", " << mi.ops[3].cond << "\n";
            return;
        case MOpcode::LdpRegFpImm:
            os << "  ldp " << rn(getReg(mi.ops[0])) << ", " << rn(getReg(mi.ops[1])) << ", [x29, #"
               << getImm(mi.ops[2]) << "]\n";
            return;
        case MOpcode::StpRegFpImm:
            os << "  stp " << rn(getReg(mi.ops[0])) << ", " << rn(getReg(mi.ops[1])) << ", [x29, #"
               << getImm(mi.ops[2]) << "]\n";
            return;
        case MOpcode::LdpFprFpImm:
        {
            os << "  ldp ";
            printD(os, getReg(mi.ops[0]));
            os << ", ";
            printD(os, getReg(mi.ops[1]));
            os << ", [x29, #" << getImm(mi.ops[2]) << "]\n";
            return;
        }
        case MOpcode::StpFprFpImm:
        {
            os << "  stp ";
            printD(os, getReg(mi.ops[0]));
            os << ", ";
            printD(os, getReg(mi.ops[1]));
            os << ", [x29, #" << getImm(mi.ops[2]) << "]\n";
            return;
        }
        default:
            break;
    }

#include "generated/OpcodeDispatch.inc"
}

} // namespace viper::codegen::aarch64
