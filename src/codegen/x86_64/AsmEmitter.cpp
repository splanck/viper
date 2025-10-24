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
#include <iomanip>
#include <sstream>
#include <utility>

namespace viper::codegen::x64
{

namespace
{

/// @brief Provide an overload set capable of visiting std::variant operands.
/// @details Aggregates multiple lambda visitors into a single callable so
///          @ref std::visit can dispatch over operand kinds without defining a
///          bespoke struct at each call site.
template <typename... Ts> struct Overload : Ts...
{
    using Ts::operator()...;
};

template <typename... Ts> Overload(Ts...) -> Overload<Ts...>;

/// @brief Pretty-print a byte buffer using AT&T `.byte` directives.
/// @details Emits up to 16 comma-separated byte literals per line. When the
///          buffer is empty a comment line is written so the output still
///          conveys that the literal was intentionally empty.
/// @param bytes Raw literal payload destined for the `.rodata` section.
/// @return Assembly text representing @p bytes.
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
        case MOpcode::CMOVNErr:
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
                               [&](const OpLabel &label) { return formatLabel(label); }},
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
                               [&](const OpImm &imm) { return formatImm(imm); }},
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
                 [&](const OpImm &imm) { return formatImm(imm); }},
        operand);
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

/// @brief Look up the canonical mnemonic for a Machine IR opcode.
/// @details Opcodes without a direct textual form (such as PX_COPY) yield
///          @c nullptr so callers can special-case them.
/// @param opcode Machine opcode to translate.
/// @return Null-terminated mnemonic or @c nullptr when no mapping exists.
const char *AsmEmitter::mnemonicFor(MOpcode opcode) noexcept
{
    switch (opcode)
    {
        case MOpcode::MOVrr:
        case MOpcode::MOVri:
            return "movq";
        case MOpcode::CMOVNErr:
            return "cmovne";
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
