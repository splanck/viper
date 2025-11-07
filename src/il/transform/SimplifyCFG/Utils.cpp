//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/transform/SimplifyCFG/Utils.cpp
// Purpose: Provide cross-cutting utilities shared by SimplifyCFG subpasses.
// Key invariants: Terminator discovery mirrors verifier behaviour; value
//                 comparisons treat floating-point payloads bitwise to preserve
//                 NaN distinctions; helper routines never mutate EH-sensitive
//                 state accidentally.
// Ownership/Lifetime: Operates on caller-owned IL structures and returns
//                     lightweight views or primitive values without owning
//                     storage.
// Perf/Threading notes: Helpers are small and inline-friendly; any allocations
//                       are bounded to temporary containers passed by callers.
// Links: docs/il-passes.md#simplifycfg, docs/codemap.md#passes
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
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace il::transform::simplify_cfg
{

/// @brief Locate the terminator instruction in a mutable block.
///
/// @details Walks the block's instruction list in reverse so the first
///          terminator encountered matches the structural terminator enforced by
///          the verifier.  The scan stops immediately once a terminating opcode
///          is found, returning @c nullptr only when the block has no
///          instructions or lacks a recognised terminator.
///
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
///
/// @details Delegates to the mutable overload after casting away constness.  The
///          helper is safe because @ref findTerminator never mutates the block;
///          the overload exists purely to avoid code duplication for const
///          callers.
///
/// @param block Basic block to inspect.
/// @return Pointer to the terminator or nullptr if the block lacks one.
const il::core::Instr *findTerminator(const il::core::BasicBlock &block)
{
    return findTerminator(const_cast<il::core::BasicBlock &>(block));
}

namespace
{

/// @brief Convert a double precision value into its IEEE bit representation.
///
/// @details Uses @c std::bit_cast when available to avoid undefined behaviour
///          and falls back to @c std::memcpy otherwise.  The helper lives in an
///          anonymous namespace so callers can treat floating-point values as
///          exact bit patterns when performing structural comparisons.
///
/// @param value Floating-point number to encode.
/// @return 64-bit representation matching IEEE 754 semantics.
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

/// @brief Compare two IL values for structural equality.
///
/// @details Examines the value kind and compares the associated payload.  For
///          temporaries the helper compares SSA identifiers, integers use the
///          stored width and boolean flag, floats are compared bit-for-bit via
///          @ref encodeDoubleToBits so NaN payloads remain distinguishable, and
///          string-based kinds compare the referenced identifier.  Null pointers
///          always compare equal because they carry no additional payload.
///
/// @param lhs Left-hand value.
/// @param rhs Right-hand value.
/// @return True when both values encode the same literal/temporary.
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
///
/// @details Early-outs when the vectors differ in length; otherwise iterates the
///          pairs and relies on @ref valuesEqual for structural comparison.  This
///          mirrors how branch-argument lists are compared when deciding whether
///          two control-flow edges are equivalent.
///
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
///
/// @details Used by block-merging code to replace block parameters with the
///          actual incoming SSA values.  Only temporary kinds are eligible for
///          substitution; all other values are returned unchanged.  The helper
///          performs a single hash lookup and therefore runs in amortised O(1).
///
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
///
/// @details Performs a hash-table lookup in @p labelToIndex.  Returning
///          `static_cast<size_t>(-1)` when the label is not found allows callers
///          to propagate a sentinel while still using unsigned indices.
///
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
///
/// @details Guards against invalid indices and ensures each block is enqueued at
///          most once by consulting @p reachable before pushing onto the queue.
///          The helper is used by breadth-first walks to avoid duplicating
///          bookkeeping code at each call site.
///
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
///
/// @details Reads the `VIPER_DEBUG_PASSES` environment variable exactly once per
///          query.  An empty string counts as disabled so the caller can toggle
///          logging simply by unsetting or clearing the variable.
///
/// @return True when the `VIPER_DEBUG_PASSES` environment variable is set to a non-empty string.
bool readDebugFlagFromEnv()
{
    if (const char *flag = std::getenv("VIPER_DEBUG_PASSES"))
        return flag[0] != '\0';
    return false;
}

/// @brief Determine whether an instruction has side effects per opcode metadata.
///
/// @details Delegates to opcode metadata so all queries share the same
///          definition of "side effect" as the verifier and optimiser.  This is
///          primarily used to avoid removing instructions that must be preserved
///          even if their results appear unused.
///
/// @param instr Instruction to query.
/// @return True when the opcode reports side effects.
bool hasSideEffects(const il::core::Instr &instr)
{
    return il::core::getOpcodeInfo(instr.op).hasSideEffects;
}

/// @brief Check whether a label represents a function entry block.
///
/// @details SimplifyCFG recognises the conventional "entry" name as well as the
///          compiler-emitted variants that use "entry_" prefixes for nested
///          entry regions.  The helper allows passes to skip or treat entry
///          blocks specially when manipulating control flow.
///
/// @param label Label string to inspect.
/// @return True for "entry" or strings prefixed with "entry_".
bool isEntryLabel(const std::string &label)
{
    return label == "entry" || label.rfind("entry_", 0) == 0;
}

/// @brief Determine whether an opcode is a resume-style terminator.
///
/// @details Resume opcodes transfer control back into an enclosing exception
///          handler.  SimplifyCFG must treat them as EH-sensitive, so the helper
///          centralises the opcode test for reuse across passes.
///
/// @param op Opcode to inspect.
/// @return True for resume opcodes, false otherwise.
bool isResumeOpcode(il::core::Opcode op)
{
    return op == il::core::Opcode::ResumeSame || op == il::core::Opcode::ResumeNext ||
           op == il::core::Opcode::ResumeLabel;
}

/// @brief Identify opcodes that manipulate the EH stack structure.
///
/// @details Matches the small set of opcodes that push, pop, or describe EH
///          regions.  Grouping the check here keeps EH-sensitive logic in sync
///          across SimplifyCFG helpers.
///
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
///
/// @details Treats a block as EH-sensitive when it begins with @c EhEntry,
///          contains any EH structural opcode, or ends with a resume-style
///          terminator.  These blocks must be preserved during CFG rewrites so
///          exception semantics remain intact.
///
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
