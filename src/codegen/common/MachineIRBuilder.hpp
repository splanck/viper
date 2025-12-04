//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/MachineIRBuilder.hpp
// Purpose: Provide shared instruction building utilities for Machine IR.
//
// This header contains generic helpers for constructing MIR instructions
// and managing instruction lists. These utilities reduce boilerplate in
// target-specific lowering code.
//
// Key invariants:
// - Factory functions always produce valid instructions.
// - All operands are owned by value.
//
// Ownership/Lifetime: Instructions own their operands by value.
//
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace viper::codegen::common
{

//===----------------------------------------------------------------------===//
// MBasicBlock Mixin
//===----------------------------------------------------------------------===//

/// @brief Mixin providing common basic block functionality.
/// @tparam BlockT The derived basic block type.
/// @tparam InstrT The instruction type stored in the block.
///
/// Use via CRTP:
///   struct MyBlock : BlockMixin<MyBlock, MyInstr> { ... };
template <typename BlockT, typename InstrT> struct BlockMixin
{
    /// @brief Append an instruction and return a reference to it.
    InstrT &append(InstrT instr)
    {
        auto &self = static_cast<BlockT &>(*this);
        self.instructions.push_back(std::move(instr));
        return self.instructions.back();
    }

    /// @brief Append multiple instructions from a vector.
    void appendAll(std::vector<InstrT> instrs)
    {
        auto &self = static_cast<BlockT &>(*this);
        for (auto &i : instrs)
            self.instructions.push_back(std::move(i));
    }
};

//===----------------------------------------------------------------------===//
// MFunction Mixin
//===----------------------------------------------------------------------===//

/// @brief Mixin providing common function-level functionality.
/// @tparam FuncT The derived function type.
/// @tparam BlockT The basic block type stored in the function.
///
/// Use via CRTP:
///   struct MyFunc : FunctionMixin<MyFunc, MyBlock> { ... };
template <typename FuncT, typename BlockT> struct FunctionMixin
{
    /// @brief Add a basic block and return a reference to it.
    BlockT &addBlock(BlockT block)
    {
        auto &self = static_cast<FuncT &>(*this);
        self.blocks.push_back(std::move(block));
        return self.blocks.back();
    }

    /// @brief Generate a unique local label using the given prefix.
    [[nodiscard]] std::string makeLocalLabel(std::string_view prefix)
    {
        auto &self = static_cast<FuncT &>(*this);
        std::string label;
        label.reserve(prefix.size() + 12);
        label = prefix;
        label += std::to_string(self.localLabelCounter++);
        return label;
    }
};

//===----------------------------------------------------------------------===//
// Instruction Factory Helpers
//===----------------------------------------------------------------------===//

/// @brief Create an instruction with explicit opcode and operand list.
/// @tparam InstrT Instruction type to construct.
/// @tparam OpcodeT Opcode type.
/// @tparam OperandT Operand type.
/// @param opc Opcode for the instruction.
/// @param ops Operand vector (moved into instruction).
/// @return Constructed instruction.
template <typename InstrT, typename OpcodeT, typename OperandT>
[[nodiscard]] InstrT makeInstr(OpcodeT opc, std::vector<OperandT> ops)
{
    InstrT instr{};
    instr.opcode = opc;
    instr.operands = std::move(ops);
    return instr;
}

/// @brief Append an operand to an instruction and return the instruction.
/// @tparam InstrT Instruction type.
/// @tparam OperandT Operand type.
/// @param instr Instruction to modify.
/// @param op Operand to append.
/// @return Reference to the modified instruction.
template <typename InstrT, typename OperandT>
InstrT &addOperand(InstrT &instr, OperandT op)
{
    instr.operands.push_back(std::move(op));
    return instr;
}

} // namespace viper::codegen::common
