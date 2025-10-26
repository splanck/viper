//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements shared helper routines for SimplifyCFG transforms.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Utility functions supporting SimplifyCFG transformations.
/// @details Includes helpers for locating terminators, comparing values, and
///          interrogating control-flow constructs.

#include "il/transform/SimplifyCFG/Utils.hpp"

#include "il/core/Instr.hpp"
#include "il/verify/ControlFlowChecker.hpp"

#include <bit>
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <cstring>

namespace il::transform::simplify_cfg
{

/// @brief Locate the terminator instruction in a mutable block.
/// @param block Basic block to inspect.
/// @return Pointer to the terminator or nullptr if the block lacks one.
il::core::Instr *findTerminator(il::core::BasicBlock &block)
{
    for (auto it = block.instructions.rbegin(); it != block.instructions.rend(); ++it)
    {
        if (il::verify::isTerminator(it->op))
            return &*it;
    }
    return nullptr;
}

/// @brief Locate the terminator instruction in an immutable block.
/// @param block Basic block to inspect.
/// @return Pointer to the terminator or nullptr if the block lacks one.
const il::core::Instr *findTerminator(const il::core::BasicBlock &block)
{
    return findTerminator(const_cast<il::core::BasicBlock &>(block));
}

/// @brief Compare two IL values for structural equality.
/// @param lhs Left-hand value.
/// @param rhs Right-hand value.
/// @return True when both values encode the same literal/temporary.
namespace
{

std::uint64_t encodeDoubleToBits(double value)
{
#if defined(__cpp_lib_bit_cast)
    return std::bit_cast<std::uint64_t>(value);
#else
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(double));
    return bits;
#endif
}

} // namespace

bool valuesEqual(const il::core::Value &lhs, const il::core::Value &rhs)
{
    if (lhs.kind != rhs.kind)
        return false;

    switch (lhs.kind)
    {
        case il::core::Value::Kind::Temp:
            return lhs.id == rhs.id;
        case il::core::Value::Kind::ConstInt:
            return lhs.i64 == rhs.i64 && lhs.isBool == rhs.isBool;
        case il::core::Value::Kind::ConstFloat:
            return encodeDoubleToBits(lhs.f64) == encodeDoubleToBits(rhs.f64);
        case il::core::Value::Kind::ConstStr:
        case il::core::Value::Kind::GlobalAddr:
            return lhs.str == rhs.str;
        case il::core::Value::Kind::NullPtr:
            return true;
    }

    return false;
}

/// @brief Compare two vectors of IL values for element-wise equality.
/// @param lhs First vector.
/// @param rhs Second vector.
/// @return True when both vectors have equal length and corresponding values match.
bool valueVectorsEqual(const std::vector<il::core::Value> &lhs,
                       const std::vector<il::core::Value> &rhs)
{
    if (lhs.size() != rhs.size())
        return false;

    for (size_t index = 0; index < lhs.size(); ++index)
    {
        if (!valuesEqual(lhs[index], rhs[index]))
            return false;
    }

    return true;
}

/// @brief Substitute temporaries using the provided mapping.
/// @param value Value that may reference a temporary.
/// @param mapping Map from temporary ids to replacement values.
/// @return Replacement value when found; otherwise the original @p value.
il::core::Value substituteValue(const il::core::Value &value,
                                const std::unordered_map<unsigned, il::core::Value> &mapping)
{
    if (value.kind != il::core::Value::Kind::Temp)
        return value;

    if (auto it = mapping.find(value.id); it != mapping.end())
        return it->second;

    return value;
}

/// @brief Translate a block label into its index when available.
/// @param labelToIndex Mapping from labels to indices.
/// @param label Label to search for.
/// @return Block index or `static_cast<size_t>(-1)` when absent.
size_t lookupBlockIndex(const std::unordered_map<std::string, size_t> &labelToIndex,
                        const std::string &label)
{
    if (auto it = labelToIndex.find(label); it != labelToIndex.end())
        return it->second;
    return static_cast<size_t>(-1);
}

/// @brief Mark a successor as reachable and add it to a traversal worklist.
/// @param reachable Bit vector tracking visited blocks.
/// @param worklist Queue of blocks pending traversal.
/// @param successor Candidate successor index to enqueue.
void enqueueSuccessor(BitVector &reachable, std::deque<size_t> &worklist, size_t successor)
{
    if (successor == static_cast<size_t>(-1))
        return;
    if (successor < reachable.size() && !reachable.test(successor))
    {
        reachable.set(successor);
        worklist.push_back(successor);
    }
}

/// @brief Check whether SimplifyCFG debug logging is enabled.
/// @return True when the `VIPER_DEBUG_PASSES` environment variable is set to a non-empty string.
bool readDebugFlagFromEnv()
{
    if (const char *flag = std::getenv("VIPER_DEBUG_PASSES"))
        return flag[0] != '\0';
    return false;
}

/// @brief Determine whether an instruction has side effects per opcode metadata.
/// @param instr Instruction to query.
/// @return True when the opcode reports side effects.
bool hasSideEffects(const il::core::Instr &instr)
{
    return il::core::getOpcodeInfo(instr.op).hasSideEffects;
}

/// @brief Check whether a label represents a function entry block.
/// @param label Label string to inspect.
/// @return True for "entry" or strings prefixed with "entry_".
bool isEntryLabel(const std::string &label)
{
    return label == "entry" || label.rfind("entry_", 0) == 0;
}

/// @brief Determine whether an opcode is a resume-style terminator.
/// @param op Opcode to inspect.
/// @return True for resume opcodes, false otherwise.
bool isResumeOpcode(il::core::Opcode op)
{
    return op == il::core::Opcode::ResumeSame || op == il::core::Opcode::ResumeNext ||
           op == il::core::Opcode::ResumeLabel;
}

/// @brief Identify opcodes that manipulate the EH stack structure.
/// @param op Opcode to inspect.
/// @return True when @p op is one of the EH structural instructions.
bool isEhStructuralOpcode(il::core::Opcode op)
{
    switch (op)
    {
        case il::core::Opcode::EhPush:
        case il::core::Opcode::EhPop:
        case il::core::Opcode::EhEntry:
            return true;
        default:
            return false;
    }
}

/// @brief Determine whether a block participates in exception-handling structure.
/// @param block Block to inspect.
/// @return True when the block contains EH structural instructions or resume terminators.
bool isEHSensitiveBlock(const il::core::BasicBlock &block)
{
    if (block.instructions.empty())
        return false;

    if (block.instructions.front().op == il::core::Opcode::EhEntry)
        return true;

    for (const auto &instr : block.instructions)
    {
        if (isEhStructuralOpcode(instr.op))
            return true;
    }

    const il::core::Instr *terminator = findTerminator(block);
    return terminator && isResumeOpcode(terminator->op);
}

} // namespace il::transform::simplify_cfg
