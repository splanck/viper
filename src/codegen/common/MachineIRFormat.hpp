//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/MachineIRFormat.hpp
// Purpose: Provide shared formatting utilities for Machine IR debug output.
//
// This header contains generic helpers for formatting MIR instructions,
// operands, and basic blocks. Target-specific formatting (opcode names,
// physical register names) must be provided by the backend via trait structs.
//
// Key invariants:
// - Formatting functions are pure and have no side effects.
// - Target traits must provide all required formatting hooks.
//
// Ownership/Lifetime: No dynamic allocation; output strings are returned by value.
//
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace viper::codegen::common
{

//===----------------------------------------------------------------------===//
// Generic Formatting Helpers
//===----------------------------------------------------------------------===//

/// @brief Format an instruction with opcode name and operand list.
/// @tparam InstrT Instruction type with `operands` member.
/// @tparam Traits Target traits providing opcodeName() and formatOperand().
/// @param instr Instruction to format.
/// @param traits Target-specific formatting callbacks.
/// @return Human-readable instruction string.
template <typename InstrT, typename Traits>
[[nodiscard]] std::string formatInstruction(const InstrT &instr, const Traits &traits)
{
    std::ostringstream os;
    os << traits.opcodeName(instr);
    bool first = true;
    for (const auto &operand : traits.operands(instr))
    {
        if (first)
        {
            os << ' ' << traits.formatOperand(operand);
            first = false;
        }
        else
        {
            os << ", " << traits.formatOperand(operand);
        }
    }
    return os.str();
}

/// @brief Format a basic block with label and instruction list.
/// @tparam BlockT Basic block type with `instructions` member.
/// @tparam Traits Target traits providing block label and instruction formatting.
/// @param block Basic block to format.
/// @param traits Target-specific formatting callbacks.
/// @return Human-readable block string.
template <typename BlockT, typename Traits>
[[nodiscard]] std::string formatBasicBlock(const BlockT &block, const Traits &traits)
{
    std::ostringstream os;
    os << traits.blockLabel(block) << ":\n";
    for (const auto &instr : traits.instructions(block))
    {
        os << "  " << formatInstruction(instr, traits) << '\n';
    }
    return os.str();
}

/// @brief Format an immediate value with standard prefix.
/// @param val Immediate constant to format.
/// @return String representation prefixed with '#'.
[[nodiscard]] inline std::string formatImmediate(long long val)
{
    return "#" + std::to_string(val);
}

/// @brief Format a register with virtual/physical prefix.
/// @tparam Traits Target traits providing physRegName() and regClassSuffix().
/// @param isPhys True if the register is physical.
/// @param idOrPhys Virtual register ID or physical register enum value.
/// @param regClass Register class suffix string.
/// @param physRegName Physical register name (only used if isPhys).
/// @return Human-readable register string.
[[nodiscard]] inline std::string formatRegister(bool isPhys,
                                                uint16_t idOrPhys,
                                                std::string_view regClassSuffix,
                                                std::string_view physRegName = {})
{
    std::ostringstream os;
    if (isPhys)
    {
        os << '@' << physRegName;
    }
    else
    {
        os << "%v" << static_cast<unsigned>(idOrPhys);
    }
    os << ':' << regClassSuffix;
    return os.str();
}

/// @brief Format a label operand.
/// @param name Label name.
/// @return The label name unchanged.
[[nodiscard]] inline std::string formatLabel(std::string_view name)
{
    return std::string{name};
}

} // namespace viper::codegen::common
