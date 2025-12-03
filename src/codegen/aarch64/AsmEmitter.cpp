//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/AsmEmitter.cpp
// Purpose: Minimal assembly emission helpers for the AArch64 backend.
//
//===----------------------------------------------------------------------===//

#include "AsmEmitter.hpp"

#include <cstring>
#include <iomanip>

namespace viper::codegen::aarch64
{

/// @brief Map IL extern names to C runtime symbol names.
/// The IL uses namespaced names like "Viper.Console.PrintI64" but the runtime
/// exports C-style names like "rt_print_i64".
static std::string mapRuntimeSymbol(const std::string &name)
{
    // Common runtime symbol mappings
    if (name == "Viper.Console.PrintI64")
        return "rt_print_i64";
    if (name == "Viper.Console.PrintF64")
        return "rt_print_f64";
    if (name == "Viper.Console.PrintStr")
        return "rt_print_str";
    if (name == "Viper.Console.ReadLine")
        return "rt_input_line";
    if (name == "Viper.Strings.Len")
        return "rt_len";
    if (name == "Viper.String.get_Length")
        return "rt_len";
    if (name == "Viper.Strings.Concat")
        return "rt_concat";
    if (name == "Viper.String.Concat")
        return "rt_concat";
    if (name == "Viper.Strings.Mid")
        return "rt_substr";
    if (name == "Viper.String.Substring")
        return "rt_substr";
    if (name == "Viper.Convert.ToInt")
        return "rt_to_int";
    if (name == "Viper.Convert.ToDouble")
        return "rt_to_double";
    if (name == "Viper.Strings.FromInt")
        return "rt_int_to_str";
    if (name == "Viper.Strings.FromDouble")
        return "rt_f64_to_str";
    if (name == "Viper.Diagnostics.Trap")
        return "rt_trap";
    if (name == "Viper.Math.Abs")
        return "rt_abs_f64";
    if (name == "Viper.Math.Sqrt")
        return "rt_sqrt";
    if (name == "Viper.Math.Sin")
        return "rt_sin";
    if (name == "Viper.Math.Cos")
        return "rt_cos";
    if (name == "Viper.Math.Tan")
        return "rt_tan";
    if (name == "Viper.Math.Floor")
        return "rt_floor";
    if (name == "Viper.Math.Ceil")
        return "rt_ceil";
    if (name == "Viper.Math.Pow")
        return "rt_pow_f64_chkdom";
    if (name == "Viper.Math.Log")
        return "rt_log";
    if (name == "Viper.Math.Exp")
        return "rt_exp";
    if (name == "Viper.Math.Atan")
        return "rt_atan";
    if (name == "Viper.Math.Sgn")
        return "rt_sgn_f64";
    if (name == "Viper.Math.SgnInt")
        return "rt_sgn_i64";
    if (name == "Viper.Math.AbsInt")
        return "rt_abs_i64";
    if (name == "Viper.Math.Min")
        return "rt_min_f64";
    if (name == "Viper.Math.Max")
        return "rt_max_f64";
    if (name == "Viper.Math.MinInt")
        return "rt_min_i64";
    if (name == "Viper.Math.MaxInt")
        return "rt_max_i64";
    if (name == "Viper.Random.Seed")
        return "rt_randomize_i64";
    if (name == "Viper.Random.Next")
        return "rt_rnd";
    if (name == "Viper.Environment.GetArgumentCount")
        return "rt_args_count";
    if (name == "Viper.Environment.GetArgument")
        return "rt_args_get";
    if (name == "Viper.Environment.GetCommandLine")
        return "rt_cmdline";
    if (name == "Viper.String.Left")
        return "rt_left";
    if (name == "Viper.String.Right")
        return "rt_right";
    if (name == "Viper.String.Mid")
        return "rt_mid2";
    if (name == "Viper.String.MidLen")
        return "rt_mid3";
    if (name == "Viper.String.Trim")
        return "rt_trim";
    if (name == "Viper.String.TrimStart")
        return "rt_ltrim";
    if (name == "Viper.String.TrimEnd")
        return "rt_rtrim";
    if (name == "Viper.String.ToUpper")
        return "rt_ucase";
    if (name == "Viper.String.ToLower")
        return "rt_lcase";
    if (name == "Viper.String.IndexOf")
        return "rt_instr2";
    if (name == "Viper.String.IndexOfFrom")
        return "rt_instr3";
    if (name == "Viper.String.Chr")
        return "rt_chr";
    if (name == "Viper.String.Asc")
        return "rt_asc";
    if (name == "Viper.Collections.List.New")
        return "rt_ns_list_new";
    if (name == "Viper.Collections.List.get_Count")
        return "rt_list_get_count";
    if (name == "Viper.Collections.List.Add")
        return "rt_list_add";
    if (name == "Viper.Collections.List.Clear")
        return "rt_list_clear";
    if (name == "Viper.Collections.List.RemoveAt")
        return "rt_list_remove_at";
    if (name == "Viper.Collections.List.get_Item")
        return "rt_list_get_item";
    if (name == "Viper.Collections.List.set_Item")
        return "rt_list_set_item";
    if (name == "Viper.Text.StringBuilder.New")
        return "rt_ns_stringbuilder_new";
    if (name == "Viper.Text.StringBuilder.Append")
        return "rt_text_sb_append";
    if (name == "Viper.Text.StringBuilder.ToString")
        return "rt_text_sb_to_string";
    if (name == "Viper.Text.StringBuilder.Clear")
        return "rt_text_sb_clear";
    if (name == "Viper.IO.File.Exists")
        return "rt_io_file_exists";
    if (name == "Viper.IO.File.ReadAllText")
        return "rt_io_file_read_all_text";
    if (name == "Viper.IO.File.WriteAllText")
        return "rt_io_file_write_all_text";
    if (name == "Viper.IO.File.Delete")
        return "rt_io_file_delete";

    // Terminal operations
    if (name == "Viper.Terminal.Clear")
        return "rt_term_cls";
    if (name == "Viper.Terminal.InKey")
        return "rt_inkey_str";
    if (name == "Viper.Terminal.SetColor")
        return "rt_term_color_i32";
    if (name == "Viper.Terminal.SetPosition")
        return "rt_term_locate_i32";
    if (name == "Viper.Terminal.SetCursorVisible")
        return "rt_term_cursor_visible_i32";
    if (name == "Viper.Terminal.SetAltScreen")
        return "rt_term_alt_screen_i32";
    if (name == "Viper.Terminal.Bell")
        return "rt_bell";
    if (name == "Viper.Terminal.GetKey")
        return "rt_getkey_str";
    if (name == "Viper.Terminal.GetKeyTimeout")
        return "rt_getkey_timeout_i32";
    if (name == "Viper.Terminal.BeginBatch")
        return "rt_term_begin_batch";
    if (name == "Viper.Terminal.EndBatch")
        return "rt_term_end_batch";
    if (name == "Viper.Terminal.Flush")
        return "rt_term_flush";

    // String formatting (number to string conversions)
    if (name == "Viper.Strings.FromI32")
        return "rt_str_i32_alloc";
    if (name == "Viper.Strings.FromI16")
        return "rt_str_i16_alloc";
    if (name == "Viper.Strings.FromSingle")
        return "rt_str_f_alloc";
    if (name == "Viper.Strings.FromDoublePrecise")
        return "rt_str_d_alloc";
    if (name == "Viper.Strings.SplitFields")
        return "rt_split_fields";
    if (name == "Viper.Strings.Equals")
        return "rt_str_eq";
    if (name == "Viper.Strings.FromStr")
        return "rt_str"; // identity string copy

    // Parsing (string to number conversions)
    if (name == "Viper.Parse.Int64")
        return "rt_parse_int64";
    if (name == "Viper.Parse.Double")
        return "rt_parse_double";

    // Additional string properties/methods
    if (name == "Viper.String.ConcatSelf")
        return "rt_concat";
    if (name == "Viper.String.get_IsEmpty")
        return "rt_str_is_empty";

    // Object methods
    if (name == "Viper.Object.Equals")
        return "rt_obj_equals";
    if (name == "Viper.Object.GetHashCode")
        return "rt_obj_get_hash_code";
    if (name == "Viper.Object.ReferenceEquals")
        return "rt_obj_reference_equals";
    if (name == "Viper.Object.ToString")
        return "rt_obj_to_string";

    // StringBuilder properties
    if (name == "Viper.Text.StringBuilder.get_Length")
        return "rt_text_sb_get_length";
    if (name == "Viper.Text.StringBuilder.get_Capacity")
        return "rt_text_sb_get_capacity";

    // Timer
    if (name == "Viper.Environment.GetTickCount")
        return "rt_timer_ms";
    if (name == "Viper.Threading.Sleep")
        return "rt_sleep_ms";

    // Not a known runtime symbol, return as-is
    return name;
}

/// @brief Mangle a symbol name for the target platform.
/// On Darwin (macOS), C symbols require an underscore prefix.
/// Local labels (starting with L or .) are not mangled.
static std::string mangleSymbol(const std::string &name)
{
#if defined(__APPLE__)
    // Don't mangle local labels (L* or .L*)
    if (!name.empty() && (name[0] == 'L' || name[0] == '.'))
        return name;
    // Add underscore prefix for Darwin
    return "_" + name;
#else
    return name;
#endif
}

/// @brief Mangle a call target symbol for emission.
/// This first maps IL runtime names to C runtime names, then applies platform mangling.
static std::string mangleCallTarget(const std::string &name)
{
    return mangleSymbol(mapRuntimeSymbol(name));
}

void AsmEmitter::emitFunctionHeader(std::ostream &os, const std::string &name) const
{
    // Keep directives minimal and assembler-agnostic for testing.
    os << ".text\n";
    os << ".align 2\n";
    // Mangle symbol names for the target platform (e.g., add '_' prefix on Darwin).
    // On Darwin, avoid emitting .globl for L*-prefixed names (reserved for local/temporary).
    const std::string sym = mangleSymbol(name);
#if defined(__APPLE__)
    if (!(sym.size() >= 1 &&
          (sym[0] == 'L' || (sym.size() >= 2 && sym[0] == '_' && sym[1] == 'L'))))
    {
        os << ".globl " << sym << "\n";
    }
#else
    os << ".globl " << sym << "\n";
#endif
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

void AsmEmitter::emitCmpRR(std::ostream &os, PhysReg lhs, PhysReg rhs) const
{
    os << "  cmp " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitCmpRI(std::ostream &os, PhysReg lhs, long long imm) const
{
    os << "  cmp " << rn(lhs) << ", #" << imm << "\n";
}

void AsmEmitter::emitCset(std::ostream &os, PhysReg dst, const char *cond) const
{
    os << "  cset " << rn(dst) << ", " << cond << "\n";
}

void AsmEmitter::emitSubSp(std::ostream &os, long long bytes) const
{
    // ARM64 add/sub immediate supports 12-bit unsigned values (0-4095).
    // For larger frames, we need to break it into multiple instructions.
    constexpr long long kMaxImm = 4095;
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
    constexpr long long kMaxImm = 4095;
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
    os << std::fixed;
    os << "  fmov ";
    printD(os, dst);
    os << ", #" << imm << "\n";
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
    if (fn.name == "main")
    {
        os << "  ; Initialize runtime context for native execution\n";
        os << "  bl _rt_legacy_context\n";
        os << "  bl _rt_set_current_context\n";
    }

    // Store the plan for use by Ret instructions
    currentPlan_ = &plan;
    currentPlanValid_ = usePlan;

    for (const auto &bb : fn.blocks)
        emitBlock(os, bb);

    currentPlan_ = nullptr;
    currentPlanValid_ = false;
    // Note: epilogue is emitted by each Ret instruction, not here
}

void AsmEmitter::emitBlock(std::ostream &os, const MBasicBlock &bb) const
{
    if (!bb.name.empty())
        os << bb.name << ":\n";
    for (const auto &mi : bb.instrs)
        /// @brief Emits instruction.
        emitInstruction(os, mi);
}

void AsmEmitter::emitInstruction(std::ostream &os, const MInstr &mi) const
{
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

#include "generated/OpcodeDispatch.inc"
}

} // namespace viper::codegen::aarch64
