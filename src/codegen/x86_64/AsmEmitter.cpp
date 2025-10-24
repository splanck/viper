//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/AsmEmitter.cpp
// Purpose: Implement AT&T-style assembly emission for the experimental x86-64
//          backend.
// Key invariants:
//   * Machine IR operand ordering and label bindings are preserved verbatim in
//     the emitted assembly.
//   * Read-only literal pools deduplicate string and f64 constants, assigning
//     deterministic labels that remain stable across runs.
//   * PX_COPY pseudo instructions are rendered as annotated comments so later
//     passes can validate allocation decisions.
// Ownership model: The emitter borrows the literal pool supplied at
// construction while writing assembly into caller-provided streams.  No global
// state is mutated.
// Links: docs/architecture.md#codegen
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Converts Machine IR into textual x86-64 assembly.
/// @details The emitter walks Machine IR functions, printing each block and
///          instruction using AT&T syntax.  A nested read-only data pool tracks
///          string and floating-point literals so they can be emitted with
///          stable labels.  The translation favours determinism to keep golden
///          tests straightforward.

#include "AsmEmitter.hpp"

#include <bit>
#include <iomanip>
#include <sstream>
#include <utility>

namespace viper::codegen::x64
{

namespace
{

template <typename... Ts> struct Overload : Ts...
{
    using Ts::operator()...;
};

template <typename... Ts> Overload(Ts...) -> Overload<Ts...>;

/// @brief Render a string literal into `.byte` directives.
/// @details Emits up to sixteen bytes per line using decimal notation, matching
///          the style of other tools in the toolchain.  An explicit comment is
///          produced for empty strings so diffs remain clear.
[[nodiscard]] std::string formatBytes(const std::string &bytes)
{
    std::ostringstream os;
    constexpr std::size_t kBytesPerLine = 16U;
    for (std::size_t i = 0; i < bytes.size();)
    {
        os << "  .byte ";
        for (std::size_t j = 0; j < kBytesPerLine && i < bytes.size(); ++j, ++i)
        {
            if (j != 0)
            {
                os << ", ";
            }
            const auto value = static_cast<unsigned>(static_cast<unsigned char>(bytes[i]));
            os << value;
        }
        os << '\n';
    }
    if (bytes.empty())
    {
        os << "  # empty literal\n";
    }
    return os.str();
}

} // namespace

/// @brief Deduplicate and store a string literal in the read-only pool.
/// @details Returns the stable index assigned to @p bytes.  Repeated literals
///          reuse the same slot so the emitted assembly contains a single copy
///          per unique string.
int AsmEmitter::RoDataPool::addStringLiteral(std::string bytes)
{
    if (const auto it = stringLookup_.find(bytes); it != stringLookup_.end())
    {
        return it->second;
    }
    const int index = static_cast<int>(stringLiterals_.size());
    stringLookup_.emplace(bytes, index);
    stringLiterals_.push_back(std::move(bytes));
    return index;
}

/// @brief Record an f64 literal and return its index in the pool.
/// @details The literal is keyed by its bit pattern to ensure `-0.0` and
///          `+0.0` remain distinct when required.
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

/// @brief Produce the assembly label that names a string literal slot.
std::string AsmEmitter::RoDataPool::stringLabel(int index) const
{
    return ".LC_str_" + std::to_string(index);
}

/// @brief Produce the assembly label that names an f64 literal slot.
std::string AsmEmitter::RoDataPool::f64Label(int index) const
{
    return ".LC_f64_" + std::to_string(index);
}

/// @brief Emit all stored literals into the provided stream.
/// @details Writes a `.rodata` section containing each string as a sequence of
///          `.byte` directives followed by every double literal encoded as a
///          `.quad`.  The existing stream formatting flags are preserved.
void AsmEmitter::RoDataPool::emit(std::ostream &os) const
{
    if (empty())
    {
        return;
    }
    os << ".section .rodata\n";
    for (std::size_t i = 0; i < stringLiterals_.size(); ++i)
    {
        os << stringLabel(static_cast<int>(i)) << ":\n";
        os << formatBytes(stringLiterals_[i]);
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

/// @brief Query whether the literal pool contains any entries.
bool AsmEmitter::RoDataPool::empty() const noexcept
{
    return stringLiterals_.empty() && f64Literals_.empty();
}

/// @brief Construct an emitter backed by an external literal pool.
/// @details The pool pointer is stored so both assembly emission and literal
///          emission operate on the same underlying data structure.
AsmEmitter::AsmEmitter(RoDataPool &pool) noexcept : pool_{&pool} {}

/// @brief Emit a Machine IR function as AT&T assembly.
/// @details Prints the `.text` directive, declares the function global, and
///          then walks each block.  The entry block is emitted inline while
///          subsequent blocks use @ref emitBlock to attach labels.
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

/// @brief Emit the read-only data section if literals are present.
void AsmEmitter::emitRoData(std::ostream &os) const
{
    if (pool_ && !pool_->empty())
    {
        pool_->emit(os);
    }
}

/// @brief Access the mutable literal pool owned by the emitter.
AsmEmitter::RoDataPool &AsmEmitter::roDataPool() noexcept
{
    return *pool_;
}

/// @brief Access the literal pool without granting mutation.
const AsmEmitter::RoDataPool &AsmEmitter::roDataPool() const noexcept
{
    return *pool_;
}

/// @brief Emit a labelled Machine IR block.
/// @details If the block supplies a label it is printed before the instruction
///          sequence.  Each instruction is then forwarded to @ref emitInstruction.
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

/// @brief Translate a single Machine IR instruction into assembly syntax.
/// @details Handles special cases such as PX_COPY (rendered as comments),
///          conditional branches, and LEA operands while falling back to
///          @ref mnemonicFor and @ref formatOperand for straightforward cases.
void AsmEmitter::emitInstruction(std::ostream &os, const MInstr &instr, const TargetInfo &target)
{
    switch (instr.opcode)
    {
        case MOpcode::PX_COPY:
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
        case MOpcode::RET:
            os << "  ret\n";
            return;
        case MOpcode::JMP:
        {
            os << "  jmp ";
            if (!instr.operands.empty())
            {
                const auto &targetOp = instr.operands.front();
                if (std::holds_alternative<OpLabel>(targetOp))
                {
                    os << formatOperand(targetOp, target);
                }
                else
                {
                    os << '*' << formatOperand(targetOp, target);
                }
            }
            else
            {
                os << "#<missing>";
            }
            os << '\n';
            return;
        }
        case MOpcode::JCC:
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
            os << "  j" << suffix << ' ';
            if (branchTarget)
            {
                if (std::holds_alternative<OpLabel>(*branchTarget))
                {
                    os << formatOperand(*branchTarget, target);
                }
                else
                {
                    os << '*' << formatOperand(*branchTarget, target);
                }
            }
            else
            {
                os << "#<missing>";
            }
            os << '\n';
            return;
        }
        case MOpcode::SETcc:
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
            os << "  set" << suffix << ' ';
            if (dest)
            {
                os << formatOperand(*dest, target);
            }
            else
            {
                os << "#<missing>";
            }
            os << '\n';
            return;
        }
        case MOpcode::CALL:
            os << "  callq ";
            if (!instr.operands.empty())
            {
                os << formatCallTarget(instr.operands.front(), target);
            }
            else
            {
                os << "#<missing>";
            }
            os << '\n';
            return;
        case MOpcode::LEA:
            if (instr.operands.size() < 2)
            {
                os << "  leaq #<missing>\n";
                return;
            }
            os << "  leaq " << formatLeaSource(instr.operands[1], target) << ", "
               << formatOperand(instr.operands[0], target) << '\n';
            return;
        default:
            break;
    }

    const char *mnemonic = mnemonicFor(instr.opcode);
    if (!mnemonic)
    {
        os << "  # <unknown opcode>\n";
        return;
    }

    switch (instr.opcode)
    {
        case MOpcode::MOVrr:
        case MOpcode::ADDrr:
        case MOpcode::SUBrr:
        case MOpcode::IMULrr:
        case MOpcode::XORrr32:
        case MOpcode::MOVZXrr32:
        case MOpcode::FADD:
        case MOpcode::FSUB:
        case MOpcode::FMUL:
        case MOpcode::FDIV:
        case MOpcode::UCOMIS:
        case MOpcode::MOVSDrr:
        case MOpcode::CVTSI2SD:
        case MOpcode::CVTTSD2SI:
        {
            if (instr.operands.size() < 2)
            {
                os << "  " << mnemonic << " #<missing>\n";
                return;
            }
            os << "  " << mnemonic << ' ' << formatOperand(instr.operands[1], target) << ", "
               << formatOperand(instr.operands[0], target) << '\n';
            return;
        }
        case MOpcode::MOVri:
        case MOpcode::ADDri:
        case MOpcode::CMPri:
        {
            if (instr.operands.size() < 2)
            {
                os << "  " << mnemonic << " #<missing>\n";
                return;
            }
            os << "  " << mnemonic << ' ' << formatOperand(instr.operands[1], target) << ", "
               << formatOperand(instr.operands[0], target) << '\n';
            return;
        }
        case MOpcode::CMPrr:
        case MOpcode::TESTrr:
        {
            if (instr.operands.size() < 2)
            {
                os << "  " << mnemonic << " #<missing>\n";
                return;
            }
            os << "  " << mnemonic << ' ' << formatOperand(instr.operands[1], target) << ", "
               << formatOperand(instr.operands[0], target) << '\n';
            return;
        }
        case MOpcode::MOVSDrm:
        {
            if (instr.operands.size() < 2)
            {
                os << "  movsd #<missing>\n";
                return;
            }
            os << "  movsd " << formatOperand(instr.operands[1], target) << ", "
               << formatOperand(instr.operands[0], target) << '\n';
            return;
        }
        case MOpcode::MOVSDmr:
        {
            if (instr.operands.size() < 2)
            {
                os << "  movsd #<missing>\n";
                return;
            }
            os << "  movsd " << formatOperand(instr.operands[1], target) << ", "
               << formatOperand(instr.operands[0], target) << '\n';
            return;
        }
        default:
            break;
    }

    os << "  " << mnemonic;
    if (!instr.operands.empty())
    {
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
    }
    os << '\n';
}

/// @brief Convert an operand variant into its AT&T textual representation.
std::string AsmEmitter::formatOperand(const Operand &operand, const TargetInfo &target)
{
    return std::visit(Overload{[&](const OpReg &reg) { return formatReg(reg, target); },
                               [&](const OpImm &imm) { return formatImm(imm); },
                               [&](const OpMem &mem) { return formatMem(mem, target); },
                               [&](const OpLabel &label) { return formatLabel(label); }},
                      operand);
}

/// @brief Format a register operand using ABI register names.
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

/// @brief Format an immediate operand with the `$` prefix.
std::string AsmEmitter::formatImm(const OpImm &imm)
{
    std::ostringstream os;
    os << '$' << imm.val;
    return os.str();
}

/// @brief Format a base+displacement memory operand.
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

/// @brief Emit the raw label name for label operands.
std::string AsmEmitter::formatLabel(const OpLabel &label)
{
    return label.name;
}

/// @brief Format the source operand for an LEA instruction.
/// @details Handles labels specially by appending `(%rip)` to produce
///          position-independent addressing.
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
                               [&](const OpImm &imm) { return formatImm(imm); }},
                      operand);
}

/// @brief Format a call target operand, prefixing indirect forms with `*`.
std::string AsmEmitter::formatCallTarget(const Operand &operand, const TargetInfo &target)
{
    return std::visit(
        Overload{[&](const OpLabel &label) { return label.name; },
                 [&](const OpReg &reg) { return std::string{"*"} + formatReg(reg, target); },
                 [&](const OpMem &mem) { return std::string{"*"} + formatMem(mem, target); },
                 [&](const OpImm &imm) { return formatImm(imm); }},
        operand);
}

/// @brief Map a condition-code enumeration to its mnemonic suffix.
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

/// @brief Return the AT&T mnemonic for a Machine IR opcode.
/// @details Returns @c nullptr for opcodes that require bespoke emission.
const char *AsmEmitter::mnemonicFor(MOpcode opcode) noexcept
{
    switch (opcode)
    {
        case MOpcode::MOVrr:
        case MOpcode::MOVri:
            return "movq";
        case MOpcode::LEA:
            return "leaq";
        case MOpcode::ADDrr:
        case MOpcode::ADDri:
            return "addq";
        case MOpcode::SUBrr:
            return "subq";
        case MOpcode::IMULrr:
            return "imulq";
        case MOpcode::DIVS64rr:
        case MOpcode::REMS64rr:
            return nullptr;
        case MOpcode::CQO:
            return "cqto";
        case MOpcode::IDIVrm:
            return "idivq";
        case MOpcode::XORrr32:
            return "xorl";
        case MOpcode::CMPrr:
        case MOpcode::CMPri:
            return "cmpq";
        case MOpcode::SETcc:
            return "set";
        case MOpcode::MOVZXrr32:
            return "movl";
        case MOpcode::TESTrr:
            return "testq";
        case MOpcode::JMP:
            return "jmp";
        case MOpcode::JCC:
            return "j";
        case MOpcode::CALL:
            return "callq";
        case MOpcode::RET:
            return "ret";
        case MOpcode::PX_COPY:
            return nullptr;
        case MOpcode::FADD:
            return "addsd";
        case MOpcode::FSUB:
            return "subsd";
        case MOpcode::FMUL:
            return "mulsd";
        case MOpcode::FDIV:
            return "divsd";
        case MOpcode::UCOMIS:
            return "ucomisd";
        case MOpcode::CVTSI2SD:
            return "cvtsi2sdq";
        case MOpcode::CVTTSD2SI:
            return "cvttsd2siq";
        case MOpcode::MOVSDrr:
        case MOpcode::MOVSDrm:
        case MOpcode::MOVSDmr:
            return "movsd";
    }
    return nullptr;
}

} // namespace viper::codegen::x64
