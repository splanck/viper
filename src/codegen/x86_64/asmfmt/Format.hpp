//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/asmfmt/Format.hpp
// Purpose: Provide reusable helpers for formatting x86-64 assembly operands.
// Key invariants: Helpers are side-effect free and produce AT&T syntax; negative
//                 register ids denote virtual registers formatted as "%vN";
//                 escape_ascii handles embedded quotes and backslashes.
// Ownership/Lifetime: Functions allocate and return std::string values by copy. Callers
//                     own the returned strings.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace asmfmt
{

/// \brief Escape a run of ASCII characters for use within an .ascii directive.
/// \param bytes Input bytes assumed to be printable ASCII.
/// \return Escaped string with embedded quotes and backslashes protected.
[[nodiscard]] std::string escape_ascii(std::string_view bytes);

/// \brief Format an immediate operand in AT&T syntax.
/// \param v Immediate value to format.
/// \return String beginning with '$' followed by the decimal value.
[[nodiscard]] std::string format_imm(std::int64_t v);

/// \brief Format a plain assembly label.
/// \param name Label name.
/// \return Copy of the label suitable for emission.
[[nodiscard]] std::string format_label(std::string_view name);

/// \brief Format a RIP-relative label reference.
/// \param name Label name.
/// \return Label suffixed with the "(%rip)" addressing mode.
[[nodiscard]] std::string format_rip_label(std::string_view name);

/// \brief Describe an x86-64 memory operand.
struct MemAddr
{
    int base{-1};          ///< Encoded base register; negative for virtual regs.
    int index{-1};         ///< Encoded index register; negative when absent.
    std::uint8_t scale{1}; ///< Scaling factor applied to the index register.
    std::int32_t disp{0};  ///< Signed displacement.
    bool has_index{false}; ///< True when the index register participates.
};

/// \brief Format a memory operand using AT&T syntax.
/// \param a Memory operand descriptor.
/// \return String describing the address expression.
[[nodiscard]] std::string format_mem(const MemAddr &a);

/// \brief Format a register name from its encoded identifier.
/// \param reg Encoded register value; negative values denote virtual registers.
/// \return Register string such as "%rax" or "%v0".
[[nodiscard]] std::string fmt_reg(int reg);

/// \brief Pretty-print a byte buffer for insertion into the .rodata section.
/// \param bytes Literal payload to emit.
/// \return Multi-line assembly text using .ascii/.byte directives.
[[nodiscard]] std::string format_rodata_bytes(std::string_view bytes);

} // namespace asmfmt
