//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/AsmEmitter.cpp
// Purpose: Materialise textual x86-64 assembly from Machine IR functions while
//          maintaining deterministic literal pools for read-only data.
// Key invariants: Emission preserves operand ordering, branch destinations, and
//                 condition suffixes carried by Machine IR. Literal pools
//                 deduplicate entries, emit stable labels, and are never emitted
//                 when empty.
// Ownership/Lifetime: AsmEmitter borrows the caller-provided RoDataPool; the
//                     pool outlives the emitter and continues to own all stored
//                     literal buffers.
// Links: docs/codemap.md#codegen, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#include "AsmEmitter.hpp"

#include <bit>
#include <cassert>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <utility>

namespace viper::codegen::x64
{

namespace
{

static const EmitterSpec kEmitterTable[] = {
    {MOpcode::MOVrr, "movq", OperandOrder::R_R, 0U},
    {MOpcode::MOVri, "movq", OperandOrder::R_I, 0U},
    {MOpcode::CMOVNErr, "cmovne", OperandOrder::R_R, 0U},
    {MOpcode::LEA, "leaq", OperandOrder::LEA, 0U},
    {MOpcode::ADDrr, "addq", OperandOrder::R_R, 0U},
    {MOpcode::ADDri, "addq", OperandOrder::R_I, 0U},
    {MOpcode::ANDrr, "andq", OperandOrder::R_R, 0U},
    {MOpcode::ANDri, "andq", OperandOrder::R_I, 0U},
    {MOpcode::ORrr, "orq", OperandOrder::R_R, 0U},
    {MOpcode::ORri, "orq", OperandOrder::R_I, 0U},
    {MOpcode::XORrr, "xorq", OperandOrder::R_R, 0U},
    {MOpcode::XORri, "xorq", OperandOrder::R_I, 0U},
    {MOpcode::SUBrr, "subq", OperandOrder::R_R, 0U},
    {MOpcode::SHLri, "shlq", OperandOrder::R_I, 0U},
    {MOpcode::SHLrc, "shlq", OperandOrder::SHIFT, 0U},
    {MOpcode::SHRri, "shrq", OperandOrder::R_I, 0U},
    {MOpcode::SHRrc, "shrq", OperandOrder::SHIFT, 0U},
    {MOpcode::SARri, "sarq", OperandOrder::R_I, 0U},
    {MOpcode::SARrc, "sarq", OperandOrder::SHIFT, 0U},
    {MOpcode::IMULrr, "imulq", OperandOrder::R_R, 0U},
    {MOpcode::CQO, "cqto", OperandOrder::NONE, 0U},
    {MOpcode::IDIVrm, "idivq", OperandOrder::DIRECT, 0U},
    {MOpcode::DIVrm, "divq", OperandOrder::DIRECT, 0U},
    {MOpcode::XORrr32, "xorl", OperandOrder::R_R, 0U},
    {MOpcode::CMPrr, "cmpq", OperandOrder::R_R, 0U},
    {MOpcode::CMPri, "cmpq", OperandOrder::R_I, 0U},
    {MOpcode::SETcc, "set", OperandOrder::SETCC, 0U},
    {MOpcode::MOVZXrr32, "movzbq", OperandOrder::MOVZX_RR8, 0U},
    {MOpcode::TESTrr, "testq", OperandOrder::R_R, 0U},
    {MOpcode::JMP, "jmp", OperandOrder::JUMP, 0U},
    {MOpcode::JCC, "j", OperandOrder::JCC, 0U},
    {MOpcode::CALL, "callq", OperandOrder::CALL, 0U},
    {MOpcode::UD2, "ud2", OperandOrder::NONE, 0U},
    {MOpcode::RET, "ret", OperandOrder::NONE, 0U},
    {MOpcode::FADD, "addsd", OperandOrder::R_R, 0U},
    {MOpcode::FSUB, "subsd", OperandOrder::R_R, 0U},
    {MOpcode::FMUL, "mulsd", OperandOrder::R_R, 0U},
    {MOpcode::FDIV, "divsd", OperandOrder::R_R, 0U},
    {MOpcode::UCOMIS, "ucomisd", OperandOrder::R_R, 0U},
    {MOpcode::CVTSI2SD, "cvtsi2sdq", OperandOrder::R_R, 0U},
    {MOpcode::CVTTSD2SI, "cvttsd2siq", OperandOrder::R_R, 0U},
    {MOpcode::MOVSDrr, "movsd", OperandOrder::R_R, 0U},
    {MOpcode::MOVSDrm, "movsd", OperandOrder::R_M, 0U},
    {MOpcode::MOVSDmr, "movsd", OperandOrder::M_R, 0U},
};

/// @brief Provide an overload set capable of visiting std::variant operands.
/// @details Aggregates multiple lambda visitors into a single callable so
///          @ref std::visit can dispatch over operand kinds without defining a
///          bespoke struct at each call site.
template <typename... Ts> struct Overload : Ts...
{
    using Ts::operator()...;
};

template <typename... Ts> Overload(Ts...) -> Overload<Ts...>;

/// @brief Determine whether @p ch is printable ASCII suitable for `.ascii`.
/// @param ch Byte under consideration.
/// @return True when @p ch falls within the printable ASCII range.
[[nodiscard]] bool isAsciiPrintable(unsigned char ch) noexcept
{
    return ch >= 0x20U && ch <= 0x7EU;
}

/// @brief Escape a run of printable characters for insertion into `.ascii`.
/// @param chunk Sequence of bytes to escape.
/// @return Escaped string with quotes and backslashes protected.
[[nodiscard]] std::string escapeAsciiChunk(std::string_view chunk)
{
    std::string escaped{};
    escaped.reserve(chunk.size());
    for (const unsigned char ch : chunk)
    {
        if (ch == static_cast<unsigned char>('\\') || ch == static_cast<unsigned char>('"'))
        {
            escaped.push_back('\\');
        }
        escaped.push_back(static_cast<char>(ch));
    }
    return escaped;
}

/// @brief Pretty-print a byte buffer using `.ascii`/`.byte` directives.
/// @details Groups printable runs into `.ascii` directives for readability and
///          falls back to `.byte` for non-printable data, emitting up to sixteen
///          entries per `.byte` line. Empty literals generate a comment marker.
/// @param bytes Raw literal payload destined for the `.rodata` section.
/// @return Assembly text representing @p bytes.
[[nodiscard]] std::string formatBytes(const std::string &bytes)
{
    std::ostringstream os;
    if (bytes.empty())
    {
        os << "  # empty literal\n";
        return os.str();
    }

    std::size_t index = 0;
    while (index < bytes.size())
    {
        const unsigned char current = static_cast<unsigned char>(bytes[index]);
        if (isAsciiPrintable(current))
        {
            const std::size_t begin = index;
            while (index < bytes.size() &&
                   isAsciiPrintable(static_cast<unsigned char>(bytes[index])))
            {
                ++index;
            }
            const std::string_view view{bytes.data() + begin, index - begin};
            os << "  .ascii \"" << escapeAsciiChunk(view) << "\"\n";
            continue;
        }

        os << "  .byte ";
        std::size_t emitted = 0;
        while (index < bytes.size() && emitted < 16U)
        {
            const unsigned char byteVal = static_cast<unsigned char>(bytes[index]);
            if (isAsciiPrintable(byteVal))
            {
                break;
            }
            if (emitted != 0)
            {
                os << ", ";
            }
            os << static_cast<unsigned>(byteVal);
            ++index;
            ++emitted;
        }
        os << '\n';
    }

    return os.str();
}

} // namespace

/// @brief Intern a string literal into the read-only data pool.
/// @details Deduplicates identical byte sequences so repeated literals emit a
///          single `.rodata` entry. New literals are appended to the pool and
///          assigned a stable numeric index.
/// @param bytes Literal payload to store.
/// @return Index referencing the canonicalised literal.
int AsmEmitter::RoDataPool::addStringLiteral(std::string bytes)
{
    if (const auto it = stringLookup_.find(bytes); it != stringLookup_.end())
    {
        return it->second;
    }
    const int index = static_cast<int>(stringLiterals_.size());
    stringLookup_.emplace(bytes, index);
    stringLengths_.push_back(bytes.size());
    stringLiterals_.push_back(std::move(bytes));
    return index;
}

/// @brief Intern a 64-bit floating literal into the read-only data pool.
/// @details Bit-casts the floating value and deduplicates based on the
///          resulting bit pattern, ensuring `+0.0` and `-0.0` remain distinct.
/// @param value Floating-point literal to store.
/// @return Index referencing the canonical literal entry.
int AsmEmitter::RoDataPool::addF64Literal(double value)
{
    const auto bits = std::bit_cast<std::uint64_t>(value);
    if (const auto it = f64Lookup_.find(bits); it != f64Lookup_.end())
    {
        return it->second;
    }
    const int index = static_cast<int>(f64Literals_.size());
    f64Lookup_.emplace(bits, index);
    f64Literals_.push_back(value);
    return index;
}

/// @brief Generate the assembly label for a stored string literal.
/// @param index Pool index returned by @ref addStringLiteral.
/// @return Mangled label suitable for use in assembly.
std::string AsmEmitter::RoDataPool::stringLabel(int index) const
{
    return ".LC_str_" + std::to_string(index);
}

/// @brief Retrieve the byte length recorded for a string literal entry.
/// @param index Pool index supplied by @ref addStringLiteral.
/// @return Number of bytes stored for the literal.
std::size_t AsmEmitter::RoDataPool::stringByteLength(int index) const
{
    assert(index >= 0);
    const auto idx = static_cast<std::size_t>(index);
    assert(idx < stringLengths_.size());
    return stringLengths_[idx];
}

/// @brief Generate the assembly label for a stored 64-bit float literal.
/// @param index Pool index returned by @ref addF64Literal.
/// @return Mangled label suitable for use in assembly.
std::string AsmEmitter::RoDataPool::f64Label(int index) const
{
    return ".LC_f64_" + std::to_string(index);
}

/// @brief Emit the `.rodata` directives for all stored literals.
/// @details Writes a `.section .rodata` header followed by labels and
///          directives for each pooled string and floating literal. The method
///          preserves insertion order so indices map consistently to labels.
/// @param os Output stream receiving assembly text.
void AsmEmitter::RoDataPool::emit(std::ostream &os) const
{
    if (empty())
    {
        return;
    }
    assert(stringLiterals_.size() == stringLengths_.size());
    os << ".section .rodata\n";
    for (std::size_t i = 0; i < stringLiterals_.size(); ++i)
    {
        os << stringLabel(static_cast<int>(i)) << ":\n";
        os << formatBytes(stringLiterals_[i]);
    }
    if (!f64Literals_.empty())
    {
        os << "  .p2align 3\n";
    }
    for (std::size_t i = 0; i < f64Literals_.size(); ++i)
    {
        os << f64Label(static_cast<int>(i)) << ":\n";
        const auto bits = std::bit_cast<std::uint64_t>(f64Literals_[i]);
        const auto oldFlags = os.flags();
        const auto oldFill = os.fill();
        os << "  .quad 0x" << std::hex << std::setw(16) << std::setfill('0') << bits << std::dec;
        os.fill(oldFill);
        os.flags(oldFlags);
        os << '\n';
    }
}

/// @brief Query whether the pool currently holds any literals.
/// @return @c true when no string or floating literals have been interned.
bool AsmEmitter::RoDataPool::empty() const noexcept
{
    return stringLiterals_.empty() && f64Literals_.empty();
}

/// @brief Construct an emitter bound to a shared read-only data pool.
/// @param pool Pool responsible for owning literal buffers referenced by the
///             emitted assembly.
AsmEmitter::AsmEmitter(RoDataPool &pool) noexcept : pool_{&pool} {}

/// @brief Emit an assembly function, including basic blocks and instructions.
/// @details Writes the `.text` header, global symbol directive, function label,
///          and each Machine IR block. The first block is treated as the entry
///          and emitted without a label when it already matches the function
///          name.
/// @param os Output stream receiving the assembly.
/// @param func Machine IR function to serialise.
/// @param target Target lowering information controlling register selection.
void AsmEmitter::emitFunction(std::ostream &os,
                              const MFunction &func,
                              const TargetInfo &target) const
{
    os << ".text\n";
    os << ".globl " << func.name << "\n";
    os << func.name << ":\n";

    for (std::size_t i = 0; i < func.blocks.size(); ++i)
    {
        const auto &block = func.blocks[i];
        const bool isEntry = (i == 0U && block.label == func.name);
        if (isEntry)
        {
            for (const auto &instr : block.instructions)
            {
                emitInstruction(os, instr, target);
            }
        }
        else
        {
            emitBlock(os, block, target);
        }
        if (i + 1 < func.blocks.size())
        {
            os << '\n';
        }
    }
}

/// @brief Emit the `.rodata` section for literals referenced by emitted code.
/// @details Forwards to the shared pool only when it contains entries so that
///          translation units without literals avoid spurious section headers.
/// @param os Output stream receiving the assembly.
void AsmEmitter::emitRoData(std::ostream &os) const
{
    if (pool_ && !pool_->empty())
    {
        pool_->emit(os);
    }
}

/// @brief Access the underlying literal pool.
/// @return Mutable reference to the associated pool.
AsmEmitter::RoDataPool &AsmEmitter::roDataPool() noexcept
{
    return *pool_;
}

/// @brief Access the underlying literal pool (const overload).
/// @return Const reference to the associated pool.
const AsmEmitter::RoDataPool &AsmEmitter::roDataPool() const noexcept
{
    return *pool_;
}

/// @brief Emit a labelled basic block and all contained instructions.
/// @details Prints the block label when present and delegates each instruction
///          to @ref emitInstruction.
/// @param os Output stream receiving the assembly.
/// @param block Machine basic block to serialise.
/// @param target Target lowering information controlling operand formatting.
void AsmEmitter::emitBlock(std::ostream &os, const MBasicBlock &block, const TargetInfo &target)
{
    if (!block.label.empty())
    {
        os << block.label << ":\n";
    }
    for (const auto &instr : block.instructions)
    {
        emitInstruction(os, instr, target);
    }
}

/// @brief Emit a single Machine IR instruction in AT&T syntax.
/// @details Handles opcode-specific quirks (such as operand ordering for
///          `MOV`, condition suffixes, and synthetic PX_COPY) before falling
///          back to a generic visitor that prints each operand.
/// @param os Output stream receiving the assembly.
/// @param instr Instruction to serialise.
/// @param target Target lowering information controlling operand formatting.
void AsmEmitter::emitInstruction(std::ostream &os, const MInstr &instr, const TargetInfo &target)
{
    if (instr.opcode == MOpcode::LABEL)
    {
        if (instr.operands.empty())
        {
            os << ".L?\n";
            return;
        }
        const auto *label = std::get_if<OpLabel>(&instr.operands.front());
        if (!label)
        {
            os << "# <invalid label>\n";
            return;
        }
        os << label->name << ":\n";
        return;
    }

    if (instr.opcode == MOpcode::PX_COPY)
    {
        os << "  # px_copy";
        bool first = true;
        for (const auto &operand : instr.operands)
        {
            if (first)
            {
                os << ' ' << formatOperand(operand, target);
                first = false;
            }
            else
            {
                os << ", " << formatOperand(operand, target);
            }
        }
        os << '\n';
        return;
    }

    const auto *spec = lookup_emitter_spec(instr.opcode);
    if (!spec)
    {
        os << "  # <unknown opcode>\n";
        return;
    }

    const auto emitBinary = [&](auto &&formatSource, auto &&formatDest)
    {
        if (instr.operands.size() < 2)
        {
            os << " #<missing>\n";
            return;
        }
        os << ' ' << formatSource(instr.operands[1]) << ", "
           << formatDest(instr.operands[0]);
        os << '\n';
    };

    const auto emitUnary = [&](const auto &formatter)
    {
        if (instr.operands.empty())
        {
            os << " #<missing>\n";
            return;
        }
        os << ' ' << formatter(instr.operands.front()) << '\n';
    };

    os << "  " << spec->mnem;

    switch (spec->order)
    {
        case OperandOrder::NONE:
            os << '\n';
            return;
        case OperandOrder::DIRECT:
        {
            if (instr.operands.empty())
            {
                os << '\n';
                return;
            }
            os << ' ';
            bool first = true;
            for (const auto &operand : instr.operands)
            {
                if (!first)
                {
                    os << ", ";
                }
                os << formatOperand(operand, target);
                first = false;
            }
            os << '\n';
            return;
        }
        case OperandOrder::R:
        case OperandOrder::M:
        case OperandOrder::I:
            emitUnary([&](const Operand &operand) { return formatOperand(operand, target); });
            return;
        case OperandOrder::R_R:
        case OperandOrder::R_M:
        case OperandOrder::M_R:
        case OperandOrder::R_I:
        case OperandOrder::M_I:
            emitBinary([&](const Operand &operand) { return formatOperand(operand, target); },
                       [&](const Operand &operand) { return formatOperand(operand, target); });
            return;
        case OperandOrder::R_R_R:
        {
            if (instr.operands.size() < 3)
            {
                os << " #<missing>\n";
                return;
            }
            os << ' ' << formatOperand(instr.operands[2], target) << ", "
               << formatOperand(instr.operands[1], target) << ", "
               << formatOperand(instr.operands[0], target) << '\n';
            return;
        }
        case OperandOrder::SHIFT:
            emitBinary([&](const Operand &operand) { return formatShiftCount(operand, target); },
                       [&](const Operand &operand) { return formatOperand(operand, target); });
            return;
        case OperandOrder::MOVZX_RR8:
        {
            if (instr.operands.size() < 2)
            {
                os << " #<missing>\n";
                return;
            }
            const auto *dest = std::get_if<OpReg>(&instr.operands[0]);
            const auto *src = std::get_if<OpReg>(&instr.operands[1]);
            if (!dest || !src)
            {
                os << " #<invalid>\n";
                return;
            }
            os << ' ' << formatReg8(*src, target) << ", " << formatReg(*dest, target) << '\n';
            return;
        }
        case OperandOrder::LEA:
        {
            if (instr.operands.size() < 2)
            {
                os << " #<missing>\n";
                return;
            }
            os << ' ' << formatLeaSource(instr.operands[1], target) << ", "
               << formatOperand(instr.operands[0], target) << '\n';
            return;
        }
        case OperandOrder::CALL:
        {
            if (instr.operands.empty())
            {
                os << " #<missing>\n";
                return;
            }
            os << ' ' << formatCallTarget(instr.operands.front(), target) << '\n';
            return;
        }
        case OperandOrder::JUMP:
        {
            if (instr.operands.empty())
            {
                os << " #<missing>\n";
                return;
            }
            os << ' ';
            const auto &targetOp = instr.operands.front();
            if (std::holds_alternative<OpLabel>(targetOp))
            {
                os << formatOperand(targetOp, target) << '\n';
            }
            else
            {
                os << '*' << formatOperand(targetOp, target) << '\n';
            }
            return;
        }
        case OperandOrder::JCC:
        {
            const Operand *branchTarget = nullptr;
            const OpImm *cond = nullptr;
            for (const auto &operand : instr.operands)
            {
                if (!cond)
                {
                    cond = std::get_if<OpImm>(&operand);
                }
                if (!branchTarget && std::holds_alternative<OpLabel>(operand))
                {
                    branchTarget = &operand;
                }
            }
            if (!branchTarget && !instr.operands.empty())
            {
                branchTarget = &instr.operands.back();
            }
            const auto suffix = cond ? conditionSuffix(cond->val) : std::string_view{"e"};
            os << suffix << ' ';
            if (branchTarget)
            {
                if (std::holds_alternative<OpLabel>(*branchTarget))
                {
                    os << formatOperand(*branchTarget, target) << '\n';
                }
                else
                {
                    os << '*' << formatOperand(*branchTarget, target) << '\n';
                }
            }
            else
            {
                os << "#<missing>\n";
            }
            return;
        }
        case OperandOrder::SETCC:
        {
            const Operand *dest = nullptr;
            const OpImm *cond = nullptr;
            for (const auto &operand : instr.operands)
            {
                if (!cond)
                {
                    cond = std::get_if<OpImm>(&operand);
                }
                if (!dest && (std::holds_alternative<OpReg>(operand) ||
                              std::holds_alternative<OpMem>(operand)))
                {
                    dest = &operand;
                }
            }
            const auto suffix = cond ? conditionSuffix(cond->val) : std::string_view{"e"};
            os << suffix << ' ';
            if (dest)
            {
                os << formatOperand(*dest, target) << '\n';
            }
            else
            {
                os << "#<missing>\n";
            }
            return;
        }
    }

    os << "\n";
}

/// @brief Convert a Machine IR operand into its assembly representation.
/// @details Dispatches on the operand variant and delegates to specialised
///          formatting helpers for registers, immediates, memory operands, and
///          labels.
/// @param operand Operand to print.
/// @param target Target lowering information controlling register names.
/// @return Textual representation of the operand.
std::string AsmEmitter::formatOperand(const Operand &operand, const TargetInfo &target)
{
    return std::visit(Overload{[&](const OpReg &reg) { return formatReg(reg, target); },
                               [&](const OpImm &imm) { return formatImm(imm); },
                               [&](const OpMem &mem) { return formatMem(mem, target); },
                               [&](const OpLabel &label) { return formatLabel(label); },
                               [&](const OpRipLabel &label) { return formatRipLabel(label); }},
                      operand);
}

/// @brief Format a register operand.
/// @details Returns the physical register name for hardware registers or a
///          synthetic `%vN` name for virtual registers to aid debugging.
/// @param reg Register operand to print.
/// @param target Target lowering context (unused for now but preserved for
///               future extensions).
/// @return Assembly string naming the register.
std::string AsmEmitter::formatReg(const OpReg &reg, const TargetInfo &)
{
    if (reg.isPhys)
    {
        const auto phys = static_cast<PhysReg>(reg.idOrPhys);
        return regName(phys);
    }
    std::ostringstream os;
    os << "%v" << static_cast<unsigned>(reg.idOrPhys);
    return os.str();
}

std::string AsmEmitter::formatReg8(const OpReg &reg, const TargetInfo &target)
{
    if (!reg.isPhys)
    {
        std::ostringstream os;
        os << "%v" << static_cast<unsigned>(reg.idOrPhys) << ".b";
        return os.str();
    }

    const auto phys = static_cast<PhysReg>(reg.idOrPhys);
    switch (phys)
    {
        case PhysReg::RAX:
            return "%al";
        case PhysReg::RBX:
            return "%bl";
        case PhysReg::RCX:
            return "%cl";
        case PhysReg::RDX:
            return "%dl";
        case PhysReg::RSI:
            return "%sil";
        case PhysReg::RDI:
            return "%dil";
        case PhysReg::RBP:
            return "%bpl";
        case PhysReg::RSP:
            return "%spl";
        case PhysReg::R8:
            return "%r8b";
        case PhysReg::R9:
            return "%r9b";
        case PhysReg::R10:
            return "%r10b";
        case PhysReg::R11:
            return "%r11b";
        case PhysReg::R12:
            return "%r12b";
        case PhysReg::R13:
            return "%r13b";
        case PhysReg::R14:
            return "%r14b";
        case PhysReg::R15:
            return "%r15b";
        default:
            return formatReg(reg, target);
    }
}

/// @brief Format an immediate operand using AT&T syntax.
/// @param imm Immediate operand to print.
/// @return Assembly string beginning with '$'.
std::string AsmEmitter::formatImm(const OpImm &imm)
{
    std::ostringstream os;
    os << '$' << imm.val;
    return os.str();
}

/// @brief Format a memory operand.
/// @details Produces the canonical `disp(base)` representation, eliding the
///          displacement when zero.
/// @param mem Memory operand to print.
/// @param target Target lowering information for register formatting.
/// @return Assembly string describing the memory reference.
std::string AsmEmitter::formatMem(const OpMem &mem, const TargetInfo &target)
{
    std::ostringstream os;
    if (mem.disp != 0)
    {
        os << mem.disp;
    }
    os << '(' << formatReg(mem.base, target) << ')';
    return os.str();
}

/// @brief Format a label operand.
/// @param label Label operand to print.
/// @return Raw label text.
std::string AsmEmitter::formatLabel(const OpLabel &label)
{
    return label.name;
}

/// @brief Format a RIP-relative label operand.
/// @param label RIP-relative label to print.
/// @return Label text suffixed with the RIP-relative addressing mode.
std::string AsmEmitter::formatRipLabel(const OpRipLabel &label)
{
    std::string result = label.name;
    result += "(%rip)";
    return result;
}

/// @brief Format a shift count operand, rewriting RCX to CL when required.
/// @param operand Operand describing the shift count.
/// @param target Target lowering context for fallback formatting.
/// @return Assembly string for the shift count operand.
std::string AsmEmitter::formatShiftCount(const Operand &operand, const TargetInfo &target)
{
    if (const auto *reg = std::get_if<OpReg>(&operand))
    {
        if (reg->isPhys && reg->cls == RegClass::GPR &&
            reg->idOrPhys == static_cast<uint16_t>(PhysReg::RCX))
        {
            return "%cl";
        }
    }
    return formatOperand(operand, target);
}

/// @brief Format the source operand for an @c LEA instruction.
/// @details Labels are converted into RIP-relative references to match how
///          immediate addresses are encoded on x86-64.
/// @param operand Operand supplying the effective address computation.
/// @param target Target lowering context for register/memory formatting.
/// @return Assembly string representing the effective address source.
std::string AsmEmitter::formatLeaSource(const Operand &operand, const TargetInfo &target)
{
    return std::visit(Overload{[&](const OpLabel &label)
                               {
                                   std::string result = label.name;
                                   result += "(%rip)";
                                   return result;
                               },
                               [&](const OpMem &mem) { return formatMem(mem, target); },
                               [&](const OpReg &reg) { return formatReg(reg, target); },
                               [&](const OpImm &imm) { return formatImm(imm); },
                               [&](const OpRipLabel &label) { return formatRipLabel(label); }},
                      operand);
}

/// @brief Format the target operand for @c CALL instructions.
/// @details Ensures indirect targets are prefixed with `*` per AT&T syntax
///          while direct labels are passed through verbatim.
/// @param operand Operand describing the call target.
/// @param target Target lowering context for register/memory formatting.
/// @return Assembly string representing the call target.
std::string AsmEmitter::formatCallTarget(const Operand &operand, const TargetInfo &target)
{
    return std::visit(
        Overload{[&](const OpLabel &label) { return label.name; },
                 [&](const OpReg &reg) { return std::string{"*"} + formatReg(reg, target); },
                 [&](const OpMem &mem) { return std::string{"*"} + formatMem(mem, target); },
                 [&](const OpImm &imm) { return formatImm(imm); },
                 [&](const OpRipLabel &label) { return std::string{"*"} + formatRipLabel(label); }},
        operand);
}

const EmitterSpec *lookup_emitter_spec(MOpcode op) noexcept
{
    for (const auto &entry : kEmitterTable)
    {
        if (entry.op == op)
        {
            return &entry;
        }
    }
    return nullptr;
}

/// @brief Translate a Machine IR condition code into an x86 suffix.
/// @param code Integer encoding produced by the selector.
/// @return String view containing the condition suffix, defaulting to "e".
std::string_view AsmEmitter::conditionSuffix(std::int64_t code) noexcept
{
    switch (static_cast<int>(code))
    {
        case 0:
            return "e";
        case 1:
            return "ne";
        case 2:
            return "l";
        case 3:
            return "le";
        case 4:
            return "g";
        case 5:
            return "ge";
        case 6:
            return "a";
        case 7:
            return "ae";
        case 8:
            return "b";
        case 9:
            return "be";
        case 10:
            return "p";
        case 11:
            return "np";
        default:
            return "e";
    }
}

} // namespace viper::codegen::x64
