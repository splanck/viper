//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the CheckOpt pass for optimizing check opcodes. The pass performs
// dominance-based redundancy elimination and loop-invariant check hoisting to
// reduce runtime overhead from bounds checks and division-by-zero checks.
//
//===----------------------------------------------------------------------===//

#include "il/transform/CheckOpt.hpp"

#include "il/transform/AnalysisIDs.hpp"
#include "il/transform/AnalysisManager.hpp"
#include "il/transform/analysis/LoopInfo.hpp"

#include "il/analysis/Dominators.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include "il/utils/UseDefInfo.hpp"
#include "il/utils/Utils.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace il::core;

namespace il::transform {
namespace {

/// @brief Check if an opcode is a check operation that can be optimized.
bool isCheckOpcode(Opcode op) {
    switch (op) {
        case Opcode::IdxChk:
        case Opcode::SDivChk0:
        case Opcode::UDivChk0:
        case Opcode::SRemChk0:
        case Opcode::URemChk0:
        case Opcode::CastFpToSiRteChk:
        case Opcode::CastFpToUiRteChk:
        case Opcode::CastSiNarrowChk:
        case Opcode::CastUiNarrowChk:
        case Opcode::IAddOvf:
        case Opcode::ISubOvf:
        case Opcode::IMulOvf:
            return true;
        default:
            return false;
    }
}

/// @brief Check if an opcode is an overflow-checked arithmetic operation.
bool isOverflowOpcode(Opcode op) {
    return op == Opcode::IAddOvf || op == Opcode::ISubOvf || op == Opcode::IMulOvf;
}

/// @brief Return the plain arithmetic opcode corresponding to a checked op.
std::optional<Opcode> plainOpcodeForOverflow(Opcode op) {
    switch (op) {
        case Opcode::IAddOvf:
            return Opcode::Add;
        case Opcode::ISubOvf:
            return Opcode::Sub;
        case Opcode::IMulOvf:
            return Opcode::Mul;
        default:
            return std::nullopt;
    }
}

/// @brief Check if a signed addition of two constants overflows.
bool addOverflows(int64_t a, int64_t b) {
    if (b > 0 && a > std::numeric_limits<int64_t>::max() - b)
        return true;
    if (b < 0 && a < std::numeric_limits<int64_t>::min() - b)
        return true;
    return false;
}

/// @brief Check if a signed subtraction of two constants overflows.
bool subOverflows(int64_t a, int64_t b) {
    if (b < 0 && a > std::numeric_limits<int64_t>::max() + b)
        return true;
    if (b > 0 && a < std::numeric_limits<int64_t>::min() + b)
        return true;
    return false;
}

/// @brief Check if a signed multiplication of two constants overflows.
bool mulOverflows(int64_t a, int64_t b) {
    if (a == 0 || b == 0)
        return false;
    if (a == -1)
        return b == std::numeric_limits<int64_t>::min();
    if (b == -1)
        return a == std::numeric_limits<int64_t>::min();
    if ((a > 0) == (b > 0))
        return a > std::numeric_limits<int64_t>::max() / b;
    else
        return a < std::numeric_limits<int64_t>::min() / b;
}

struct IntRange {
    std::optional<int64_t> lower;
    std::optional<int64_t> upper;
};

IntRange exactRange(int64_t value) {
    return IntRange{value, value};
}

std::optional<IntRange> rangeForValue(
    const Value &value,
    const std::unordered_map<unsigned, IntRange> &ranges) {
    if (value.kind == Value::Kind::ConstInt)
        return exactRange(value.i64);
    if (value.kind != Value::Kind::Temp)
        return std::nullopt;
    auto it = ranges.find(value.id);
    if (it == ranges.end())
        return std::nullopt;
    return it->second;
}

std::optional<int64_t> addCheckedValue(int64_t lhs, int64_t rhs) {
    if (addOverflows(lhs, rhs))
        return std::nullopt;
    return lhs + rhs;
}

std::optional<int64_t> subCheckedValue(int64_t lhs, int64_t rhs) {
    if (subOverflows(lhs, rhs))
        return std::nullopt;
    return lhs - rhs;
}

std::optional<int64_t> mulCheckedValue(int64_t lhs, int64_t rhs) {
    if (mulOverflows(lhs, rhs))
        return std::nullopt;
    return lhs * rhs;
}

std::optional<IntRange> addRanges(const IntRange &lhs, const IntRange &rhs) {
    const int64_t lhsLower = lhs.lower.value_or(std::numeric_limits<int64_t>::min());
    const int64_t lhsUpper = lhs.upper.value_or(std::numeric_limits<int64_t>::max());
    const int64_t rhsLower = rhs.lower.value_or(std::numeric_limits<int64_t>::min());
    const int64_t rhsUpper = rhs.upper.value_or(std::numeric_limits<int64_t>::max());
    auto lower = addCheckedValue(lhsLower, rhsLower);
    auto upper = addCheckedValue(lhsUpper, rhsUpper);
    if (!lower || !upper)
        return std::nullopt;

    IntRange result;
    if (lhs.lower || rhs.lower)
        result.lower = *lower;
    if (lhs.upper || rhs.upper)
        result.upper = *upper;
    return result;
}

std::optional<IntRange> subRanges(const IntRange &lhs, const IntRange &rhs) {
    const int64_t lhsLower = lhs.lower.value_or(std::numeric_limits<int64_t>::min());
    const int64_t lhsUpper = lhs.upper.value_or(std::numeric_limits<int64_t>::max());
    const int64_t rhsLower = rhs.lower.value_or(std::numeric_limits<int64_t>::min());
    const int64_t rhsUpper = rhs.upper.value_or(std::numeric_limits<int64_t>::max());
    auto lower = subCheckedValue(lhsLower, rhsUpper);
    auto upper = subCheckedValue(lhsUpper, rhsLower);
    if (!lower || !upper)
        return std::nullopt;

    IntRange result;
    if (lhs.lower || rhs.upper)
        result.lower = *lower;
    if (lhs.upper || rhs.lower)
        result.upper = *upper;
    return result;
}

std::optional<IntRange> mulRanges(const IntRange &lhs, const IntRange &rhs) {
    if (!lhs.lower || !lhs.upper || !rhs.lower || !rhs.upper)
        return std::nullopt;

    std::array<std::optional<int64_t>, 4> products{
        mulCheckedValue(*lhs.lower, *rhs.lower),
        mulCheckedValue(*lhs.lower, *rhs.upper),
        mulCheckedValue(*lhs.upper, *rhs.lower),
        mulCheckedValue(*lhs.upper, *rhs.upper),
    };
    for (const auto &product : products)
        if (!product)
            return std::nullopt;

    int64_t lo = *products[0];
    int64_t hi = *products[0];
    for (const auto &product : products) {
        lo = std::min(lo, *product);
        hi = std::max(hi, *product);
    }
    return IntRange{lo, hi};
}

std::optional<IntRange> mergeIncomingRange(const IntRange &lhs, const IntRange &rhs) {
    IntRange merged;
    if (lhs.lower && rhs.lower)
        merged.lower = std::min(*lhs.lower, *rhs.lower);
    if (lhs.upper && rhs.upper)
        merged.upper = std::max(*lhs.upper, *rhs.upper);
    if (!merged.lower && !merged.upper)
        return std::nullopt;
    return merged;
}

bool deriveCompareBranchRange(const Instr &cmp,
                              size_t branchIndex,
                              Value &constrainedValue,
                              IntRange &range) {
    if (cmp.operands.size() != 2)
        return false;

    Opcode op = cmp.op;
    Value variable;
    int64_t constant = 0;

    if (cmp.operands[0].kind == Value::Kind::Temp &&
        cmp.operands[1].kind == Value::Kind::ConstInt) {
        variable = cmp.operands[0];
        constant = cmp.operands[1].i64;
    } else if (cmp.operands[0].kind == Value::Kind::ConstInt &&
               cmp.operands[1].kind == Value::Kind::Temp) {
        variable = cmp.operands[1];
        constant = cmp.operands[0].i64;
        switch (op) {
            case Opcode::SCmpLT:
                op = Opcode::SCmpGT;
                break;
            case Opcode::SCmpLE:
                op = Opcode::SCmpGE;
                break;
            case Opcode::SCmpGT:
                op = Opcode::SCmpLT;
                break;
            case Opcode::SCmpGE:
                op = Opcode::SCmpLE;
                break;
            default:
                break;
        }
    } else {
        return false;
    }

    if (variable.kind != Value::Kind::Temp)
        return false;

    IntRange fact;
    const bool trueBranch = branchIndex == 0;
    switch (op) {
        case Opcode::SCmpLT:
            if (trueBranch) {
                if (constant == std::numeric_limits<int64_t>::min())
                    return false;
                fact.upper = constant - 1;
            } else {
                fact.lower = constant;
            }
            break;
        case Opcode::SCmpLE:
            if (trueBranch) {
                fact.upper = constant;
            } else {
                if (constant == std::numeric_limits<int64_t>::max())
                    return false;
                fact.lower = constant + 1;
            }
            break;
        case Opcode::SCmpGT:
            if (trueBranch) {
                if (constant == std::numeric_limits<int64_t>::max())
                    return false;
                fact.lower = constant + 1;
            } else {
                fact.upper = constant;
            }
            break;
        case Opcode::SCmpGE:
            if (trueBranch) {
                fact.lower = constant;
            } else {
                if (constant == std::numeric_limits<int64_t>::min())
                    return false;
                fact.upper = constant - 1;
            }
            break;
        case Opcode::ICmpEq:
            if (!trueBranch)
                return false;
            fact = exactRange(constant);
            break;
        default:
            return false;
    }

    constrainedValue = variable;
    range = fact;
    return true;
}

std::unordered_map<unsigned, IntRange> edgeRangesForTarget(const BasicBlock &pred,
                                                           const Instr &term,
                                                           size_t branchIndex,
                                                           const BasicBlock &target) {
    std::unordered_map<unsigned, IntRange> facts;
    if (term.labels.size() <= branchIndex)
        return facts;

    if (branchIndex < term.brArgs.size()) {
        const auto &args = term.brArgs[branchIndex];
        for (size_t i = 0; i < args.size() && i < target.params.size(); ++i)
            if (args[i].kind == Value::Kind::ConstInt)
                facts[target.params[i].id] = exactRange(args[i].i64);
    }

    if (term.op != Opcode::CBr || term.operands.size() != 1)
        return facts;

    const Value &cond = term.operands.front();
    if (cond.kind != Value::Kind::Temp)
        return facts;

    const Instr *cmp = nullptr;
    for (const auto &instr : pred.instructions) {
        if (instr.result && *instr.result == cond.id) {
            cmp = &instr;
            break;
        }
    }
    if (!cmp)
        return facts;

    Value constrained;
    IntRange range;
    if (!deriveCompareBranchRange(*cmp, branchIndex, constrained, range))
        return facts;

    facts[constrained.id] = range;

    if (branchIndex >= term.brArgs.size())
        return facts;

    const auto &args = term.brArgs[branchIndex];
    for (size_t i = 0; i < args.size() && i < target.params.size(); ++i) {
        if (valueEquals(args[i], constrained))
            facts[target.params[i].id] = range;
    }

    return facts;
}

std::unordered_map<unsigned, IntRange> collectIncomingRanges(const Function &function,
                                                             const BasicBlock &target) {
    std::unordered_map<unsigned, IntRange> merged;
    bool sawPred = false;

    for (const auto &pred : function.blocks) {
        if (pred.instructions.empty())
            continue;
        const Instr &term = pred.instructions.back();
        for (size_t branchIndex = 0; branchIndex < term.labels.size(); ++branchIndex) {
            if (term.labels[branchIndex] != target.label)
                continue;

            auto edgeFacts = edgeRangesForTarget(pred, term, branchIndex, target);
            if (!sawPred) {
                merged = std::move(edgeFacts);
                sawPred = true;
                continue;
            }

            for (auto it = merged.begin(); it != merged.end();) {
                auto rhs = edgeFacts.find(it->first);
                if (rhs == edgeFacts.end()) {
                    it = merged.erase(it);
                    continue;
                }
                auto combined = mergeIncomingRange(it->second, rhs->second);
                if (!combined) {
                    it = merged.erase(it);
                    continue;
                }
                it->second = *combined;
                ++it;
            }
        }
    }

    return merged;
}

std::optional<IntRange> computeInstructionRange(
    const Instr &instr,
    const std::unordered_map<unsigned, IntRange> &ranges) {
    if (instr.operands.size() >= 2) {
        auto lhs = rangeForValue(instr.operands[0], ranges);
        auto rhs = rangeForValue(instr.operands[1], ranges);
        switch (instr.op) {
            case Opcode::Add:
            case Opcode::IAddOvf:
                if (lhs && rhs)
                    return addRanges(*lhs, *rhs);
                break;
            case Opcode::Sub:
            case Opcode::ISubOvf:
                if (lhs && rhs)
                    return subRanges(*lhs, *rhs);
                break;
            case Opcode::Mul:
            case Opcode::IMulOvf:
                if (lhs && rhs)
                    return mulRanges(*lhs, *rhs);
                break;
            case Opcode::And: {
                if (instr.operands[1].kind == Value::Kind::ConstInt &&
                    instr.operands[1].i64 >= 0) {
                    return IntRange{0, instr.operands[1].i64};
                }
                if (instr.operands[0].kind == Value::Kind::ConstInt &&
                    instr.operands[0].i64 >= 0) {
                    return IntRange{0, instr.operands[0].i64};
                }
                break;
            }
            case Opcode::LShr:
                if (instr.operands[1].kind == Value::Kind::ConstInt &&
                    instr.operands[1].i64 > 0 && instr.operands[1].i64 < 64) {
                    return IntRange{0, std::numeric_limits<int64_t>::max()};
                }
                break;
            default:
                break;
        }
    }
    return std::nullopt;
}

/// @brief Try to strength-reduce an overflow-checked arithmetic op to its plain
///        counterpart when both operands are constant and the operation is known
///        not to overflow at compile time.
/// @param instr The overflow instruction to evaluate.
/// @return True when the instruction was rewritten to a plain op.
bool tryConstantFoldOverflow(Instr &instr) {
    if (instr.operands.size() < 2)
        return false;

    const Value &lhs = instr.operands[0];
    const Value &rhs = instr.operands[1];

    if (lhs.kind != Value::Kind::ConstInt || rhs.kind != Value::Kind::ConstInt)
        return false;

    bool overflows = false;
    switch (instr.op) {
        case Opcode::IAddOvf:
            overflows = addOverflows(lhs.i64, rhs.i64);
            break;
        case Opcode::ISubOvf:
            overflows = subOverflows(lhs.i64, rhs.i64);
            break;
        case Opcode::IMulOvf:
            overflows = mulOverflows(lhs.i64, rhs.i64);
            break;
        default:
            return false;
    }

    // When both operands are constant and no overflow occurs, the operation is
    // safe but we leave the overflow opcode in place — the IL verifier requires
    // signed integer arithmetic to use overflow-checking opcodes, and ConstFold
    // will handle the actual constant folding with proper use-def replacement.
    (void)overflows;
    return false;
}

/// @brief Return true when a checked div/rem instruction can be represented by
///        its plain counterpart because the divisor check is statically proven.
bool tryDemoteCheckedDivRem(Instr &instr) {
    if (instr.operands.size() < 2 || instr.operands[1].kind != Value::Kind::ConstInt)
        return false;

    const Value &lhs = instr.operands[0];
    const int64_t divisor = instr.operands[1].i64;
    if (divisor == 0)
        return false;

    switch (instr.op) {
        case Opcode::SDivChk0:
            // sdiv.chk0 also traps on INT_MIN / -1. A constant divisor other than
            // -1 proves the overflow case impossible; divisor -1 needs a known
            // non-minimum numerator.
            if (divisor == -1 &&
                (lhs.kind != Value::Kind::ConstInt ||
                 lhs.i64 == std::numeric_limits<int64_t>::min())) {
                return false;
            }
            instr.op = Opcode::SDiv;
            return true;
        case Opcode::UDivChk0:
            instr.op = Opcode::UDiv;
            return true;
        case Opcode::SRemChk0:
            // MIN % -1 is defined as 0 for Viper's checked remainder semantics,
            // so a nonzero divisor fully proves the trap check unnecessary.
            instr.op = Opcode::SRem;
            return true;
        case Opcode::URemChk0:
            instr.op = Opcode::URem;
            return true;
        default:
            return false;
    }
}

const Instr *findDefBefore(const BasicBlock &block, const Instr &limit, unsigned id) {
    for (const auto &instr : block.instructions) {
        if (&instr == &limit)
            return nullptr;
        if (instr.result && *instr.result == id)
            return &instr;
    }
    return nullptr;
}

bool isSignBiasForDividend(const BasicBlock &block,
                           const Instr &limit,
                           const Value &dividend,
                           const Value &biasValue) {
    if (dividend.kind != Value::Kind::Temp || biasValue.kind != Value::Kind::Temp)
        return false;

    const Instr *bias = findDefBefore(block, limit, biasValue.id);
    if (!bias || bias->op != Opcode::And || bias->operands.size() != 2 ||
        bias->operands[0].kind != Value::Kind::Temp ||
        bias->operands[1].kind != Value::Kind::ConstInt || bias->operands[1].i64 < 0) {
        return false;
    }

    const Instr *sign = findDefBefore(block, *bias, bias->operands[0].id);
    return sign && sign->op == Opcode::AShr && sign->operands.size() == 2 &&
           valueEquals(sign->operands[0], dividend) &&
           sign->operands[1].kind == Value::Kind::ConstInt && sign->operands[1].i64 == 63;
}

bool tryDemoteSignBiasAdd(const BasicBlock &block, Instr &instr) {
    if (instr.op != Opcode::IAddOvf || instr.operands.size() != 2)
        return false;

    if (isSignBiasForDividend(block, instr, instr.operands[0], instr.operands[1]) ||
        isSignBiasForDividend(block, instr, instr.operands[1], instr.operands[0])) {
        instr.op = Opcode::Add;
        return true;
    }
    return false;
}

std::unordered_map<std::string, unsigned> computePredecessorCounts(const Function &function) {
    std::unordered_map<std::string, unsigned> counts;
    for (const auto &block : function.blocks) {
        if (block.instructions.empty())
            continue;
        const auto &term = block.instructions.back();
        for (const auto &label : term.labels)
            ++counts[label];
    }
    return counts;
}

/// @brief Key representing a check condition for redundancy detection.
/// @details Two checks with the same key test the same condition. Uses the
///          shared valueEquals() helper for consistent value comparison.
struct CheckKey {
    Opcode op;
    Type type;
    std::vector<Value> operands;

    bool operator==(const CheckKey &other) const {
        if (op != other.op || operands.size() != other.operands.size())
            return false;
        // Type comparison (only relevant for typed checks like IdxChk)
        if (type.kind != other.type.kind)
            return false;
        for (size_t i = 0; i < operands.size(); ++i) {
            if (!valueEquals(operands[i], other.operands[i]))
                return false;
        }
        return true;
    }
};

/// @brief Hash functor for CheckKey using shared value hashing.
/// @details Combines opcode and type with each operand hash using the
///          shared valueHash() helper for consistency.
struct CheckKeyHash {
    size_t operator()(const CheckKey &key) const {
        size_t h = static_cast<size_t>(key.op);
        h ^= static_cast<size_t>(key.type.kind) << 8;
        for (const auto &v : key.operands) {
            h ^= valueHash(v) + kHashPhiMix + (h << 6) + (h >> 2);
        }
        return h;
    }
};

/// @brief Build a CheckKey from an instruction.
CheckKey makeCheckKey(const Instr &instr) {
    CheckKey key;
    key.op = instr.op;
    key.type = instr.type;
    key.operands = instr.operands;
    return key;
}

/// @brief Test whether a standalone check instruction is trivially satisfied by constant operands.
///
/// @details After SCCP runs its constant-rewriting phase, any operand that was proven
///          constant appears in the IR as a ConstInt/ConstFloat literal rather than a
///          Temp reference.  This helper exploits that property to eliminate checks
///          whose condition can be verified at compile time without consulting the
///          SCCP lattice directly.
///
///          Rules applied per opcode:
///          - IdxChk(index, lo, hi)     — all three ConstInt and lo <= index < hi
///          Checked div/rem result-producing instructions are handled earlier
///          by tryDemoteCheckedDivRem(), which preserves the result and rewrites
///          to the corresponding plain arithmetic opcode when safe.
///
/// @param instr Check instruction to inspect.
/// @param replacementOut When the function returns true and the check has a result,
///        this is set to the value that should replace all uses of the result
///        (the normalized value produced by the check).
/// @return True when the check condition is statically guaranteed to succeed.
bool isCheckTriviallyTrue(const Instr &instr, Value &replacementOut) {
    auto isConstInt = [](const Value &v) { return v.kind == Value::Kind::ConstInt; };

    switch (instr.op) {
        case Opcode::IdxChk: {
            // idx.chk index lo hi — passes when lo <= index < hi
            if (instr.operands.size() < 3)
                return false;
            const Value &index = instr.operands[0];
            const Value &lo = instr.operands[1];
            const Value &hi = instr.operands[2];
            if (!isConstInt(index) || !isConstInt(lo) || !isConstInt(hi))
                return false;
            if (lo.i64 <= index.i64 && index.i64 < hi.i64) {
                replacementOut = Value::constInt(index.i64 - lo.i64);
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

/// @brief Information about a dominating check instruction.
struct DominatingCheck {
    BasicBlock *block;
    std::optional<unsigned> resultId;
};

/// @brief Find a basic block by label using a pre-built map for O(1) lookup.
/// @param blockMap Pre-computed label → block pointer map.
/// @param label The block label to search for.
/// @return Pointer to the block, or nullptr if not found.
BasicBlock *findBlock(const std::unordered_map<std::string, BasicBlock *> &blockMap,
                      const std::string &label) {
    auto it = blockMap.find(label);
    return it != blockMap.end() ? it->second : nullptr;
}

/// @brief Find the preheader block for a loop.
/// @details The preheader is the unique block outside the loop that branches
///          to the loop header. It provides a safe location for hoisting
///          loop-invariant operations. Returns nullptr if no unique preheader
///          exists (e.g., multiple entry edges or missing block).
/// @param function The function containing the loop.
/// @param loop The loop to find the preheader for.
/// @param header The loop's header block.
/// @return Pointer to the preheader block, or nullptr if not canonical.
BasicBlock *findPreheader(Function &function, const Loop &loop, BasicBlock &header) {
    BasicBlock *preheader = nullptr;
    for (auto &block : function.blocks) {
        if (loop.contains(block.label))
            continue;
        if (!viper::il::isTerminated(block))
            continue;
        const Instr &term = block.instructions.back();
        bool targetsHeader = false;
        for (const auto &label : term.labels) {
            if (label == header.label) {
                targetsHeader = true;
                break;
            }
        }
        if (!targetsHeader)
            continue;
        if (preheader && preheader != &block)
            return nullptr; // Multiple preheaders - not canonical form
        preheader = &block;
    }
    return preheader;
}

/// @brief Seed the invariant set with values defined outside the loop.
/// @details Populates the invariant set with all SSA temporaries that are:
///          - Function parameters (always loop-invariant)
///          - Block parameters of blocks outside the loop
///          - Results of instructions in blocks outside the loop
/// @param loop The loop whose invariants are being computed.
/// @param function The function containing the loop.
/// @param invariants Output set to populate with invariant temporary IDs.
void seedInvariants(const Loop &loop,
                    Function &function,
                    std::unordered_set<unsigned> &invariants) {
    // Function parameters are always invariant
    for (const auto &param : function.params)
        invariants.insert(param.id);

    // Values defined in blocks outside the loop are invariant
    for (auto &block : function.blocks) {
        if (loop.contains(block.label))
            continue;
        for (const auto &param : block.params)
            invariants.insert(param.id);
        for (const auto &instr : block.instructions)
            if (instr.result)
                invariants.insert(*instr.result);
    }
}

/// @brief Check if all operands of an instruction are loop-invariant.
/// @details An operand is invariant if it's a constant (non-Temp) or if its
///          temporary ID is in the invariant set (defined outside the loop).
/// @param instr The instruction to check.
/// @param invariants Set of known loop-invariant temporary IDs.
/// @return True if all operands are loop-invariant.
bool operandsInvariant(const Instr &instr, const std::unordered_set<unsigned> &invariants) {
    auto isInvariantValue = [&invariants](const Value &value) {
        if (value.kind != Value::Kind::Temp)
            return true; // Constants are always invariant
        return invariants.contains(value.id);
    };

    for (const auto &operand : instr.operands) {
        if (!isInvariantValue(operand))
            return false;
    }
    return true;
}

/// @brief Check if an instruction would execute on every loop iteration.
/// @details Only instructions that must execute can be safely hoisted to the
///          preheader. Currently uses a conservative approximation: only
///          instructions in the loop header are considered guaranteed.
/// @param block The block containing the instruction.
/// @param loop The loop being analyzed.
/// @return True if instructions in this block are guaranteed to execute.
bool isGuaranteedToExecute(const BasicBlock &block, const Loop &loop) {
    // Conservative: only hoist from header where check must execute on loop entry
    return block.label == loop.headerLabel;
}

/// @brief Check if a loop contains exception handling operations.
/// @details Loops with EH-sensitive operations (exception handlers, resume
///          instructions, trap instructions) require special care when
///          hoisting. This function conservatively detects such loops.
/// @param loop The loop to check.
/// @param function The function containing the loop.
/// @return True if the loop contains any EH-sensitive operations.
bool loopHasEHSensitiveOps(const Loop &loop,
                           const std::unordered_map<std::string, BasicBlock *> &blockMap) {
    for (const auto &label : loop.blockLabels) {
        BasicBlock *block = findBlock(blockMap, label);
        if (!block)
            continue;
        for (const auto &instr : block->instructions) {
            switch (instr.op) {
                case Opcode::ResumeSame:
                case Opcode::ResumeNext:
                case Opcode::ResumeLabel:
                case Opcode::EhPush:
                case Opcode::EhPop:
                case Opcode::TrapFromErr:
                case Opcode::TrapErr:
                    return true;
                default:
                    break;
            }
        }
    }
    return false;
}

} // namespace

std::string_view CheckOpt::id() const {
    return "check-opt";
}

PreservedAnalyses CheckOpt::run(Function &function, AnalysisManager &analysis) {
    if (function.blocks.empty())
        return PreservedAnalyses::all();

    auto &domTree =
        analysis.getFunctionResult<viper::analysis::DomTree>(kAnalysisDominators, function);
    auto &loopInfo = analysis.getFunctionResult<LoopInfo>(kAnalysisLoopInfo, function);

    bool changed = false;

    // Build label → block map once for O(1) lookup (replaces O(n) findBlock).
    std::unordered_map<std::string, BasicBlock *> blockMap;
    for (auto &bb : function.blocks)
        blockMap[bb.label] = &bb;
    const auto predecessorCounts = computePredecessorCounts(function);

    // Build initial use-count info once for safe temp replacement queries.
    viper::il::UseDefInfo useInfo(function);

    // =========================================================================
    // Phase 0: Check opcode demotion
    // =========================================================================
    // Result-producing checks cannot be erased when their value is used. For
    // checked div/rem, a statically nonzero divisor proves the guard safe, so
    // demote to the matching plain opcode and let later arithmetic passes keep
    // optimizing the value.
    for (auto &block : function.blocks) {
        for (auto &instr : block.instructions) {
            if (isOverflowOpcode(instr.op) && tryConstantFoldOverflow(instr))
                changed = true;
            if (tryDemoteCheckedDivRem(instr))
                changed = true;
            if (tryDemoteSignBiasAdd(block, instr))
                changed = true;
        }
    }

    // =========================================================================
    // Phase 0.5: Guard-based overflow elimination
    // =========================================================================
    // When a CBr on a signed comparison guards an overflow-checked subtraction,
    // the overflow check may be provably safe. For example:
    //   %cmp = scmp_le %n, 1
    //   cbr %cmp, base, recurse
    // On the false branch (recurse): n > 1, so n >= 2.
    // Therefore: isub.ovf %n, 1 → n-1 >= 1 (no overflow)
    //            isub.ovf %n, 2 → n-2 >= 0 (no overflow)
    //
    // We scan each block for CBr instructions on signed comparisons, propagate
    // range constraints to the target blocks, and demote overflow ops to plain
    // ops when the range proves safety.
    for (auto &block : function.blocks) {
        if (block.instructions.empty())
            continue;

        const auto &term = block.instructions.back();
        if (term.op != Opcode::CBr || term.labels.size() != 2 || term.operands.empty())
            continue;

        // Find the comparison instruction feeding the CBr.
        const Value &condVal = term.operands[0];
        if (condVal.kind != Value::Kind::Temp)
            continue;

        const Instr *cmpInstr = nullptr;
        for (const auto &instr : block.instructions) {
            if (instr.result && *instr.result == condVal.id) {
                cmpInstr = &instr;
                break;
            }
        }
        if (!cmpInstr || cmpInstr->operands.size() < 2)
            continue;

        // Only handle scmp_le %x, C where C is a constant.
        if (cmpInstr->op != Opcode::SCmpLE)
            continue;

        const Value &cmpLHS = cmpInstr->operands[0]; // %x
        const Value &cmpRHS = cmpInstr->operands[1]; // C
        if (cmpRHS.kind != Value::Kind::ConstInt || cmpLHS.kind != Value::Kind::Temp)
            continue;

        const int64_t threshold = cmpRHS.i64;
        const unsigned guardedVar = cmpLHS.id;

        // Avoid overflow in threshold + 1.
        if (threshold == std::numeric_limits<int64_t>::max())
            continue;

        // CBr: labels[0] = true branch (cmp is true → x <= C)
        //      labels[1] = false branch (cmp is false → x > C → x >= C+1)
        // On the FALSE branch, we know: guardedVar >= threshold + 1
        const std::string &falseBranch = term.labels[1];
        auto predCountIt = predecessorCounts.find(falseBranch);
        if (predCountIt == predecessorCounts.end() || predCountIt->second != 1)
            continue;
        const int64_t lowerBound = threshold + 1; // x >= lowerBound on false branch

        // Find the false-branch target block and demote safe overflow ops.
        auto tgtIt = blockMap.find(falseBranch);
        if (tgtIt == blockMap.end())
            continue;
        BasicBlock *targetBlock = tgtIt->second;

        for (auto &instr : targetBlock->instructions) {
            if (instr.op != Opcode::ISubOvf)
                continue;
            if (instr.operands.size() < 2)
                continue;

            const Value &subLHS = instr.operands[0];
            const Value &subRHS = instr.operands[1];

            // Match: isub.ovf %guardedVar, K where K is a constant
            if (subLHS.kind != Value::Kind::Temp || subLHS.id != guardedVar)
                continue;
            if (subRHS.kind != Value::Kind::ConstInt)
                continue;

            const int64_t K = subRHS.i64;

            // Check: lowerBound - K >= INT64_MIN (subtraction can't overflow)
            // Since lowerBound >= C+1 and K is small (typically 1 or 2),
            // this is almost always safe.
            if (K >= 0 && lowerBound >= std::numeric_limits<int64_t>::min() + K) {
                // x >= lowerBound, K >= 0 → x - K >= lowerBound - K >= INT64_MIN → safe
                instr.op = Opcode::Sub;
                changed = true;
            }
        }
    }

    // =========================================================================
    // Phase 0.6: Range-backed overflow elimination
    // =========================================================================
    // Seed simple integer ranges from comparison-controlled incoming edges and
    // propagate them through straight-line arithmetic. This catches hot counted
    // loop bodies where the header condition proves the induction increment is
    // safe (for example, `%i < 50_000_000` before `%i + 1`) while keeping the
    // proof syntactically visible to the verifier.
    for (auto &block : function.blocks) {
        auto ranges = collectIncomingRanges(function, block);

        for (auto &instr : block.instructions) {
            std::optional<IntRange> resultRange = computeInstructionRange(instr, ranges);

            if (resultRange) {
                if (auto plainOp = plainOpcodeForOverflow(instr.op)) {
                    instr.op = *plainOp;
                    changed = true;
                }
                if (instr.result)
                    ranges[*instr.result] = *resultRange;
                continue;
            }

            if (instr.result)
                ranges.erase(*instr.result);
        }
    }

    // =========================================================================
    // Phase 1: Dominance-based redundancy elimination
    // =========================================================================
    // Walk dominator tree with scoped map so siblings do not incorrectly share
    // availability. Only checks that dominate the current block may be reused.
    std::unordered_map<CheckKey, DominatingCheck, CheckKeyHash> available;
    std::unordered_map<CheckKey, unsigned, CheckKeyHash> depthCount;
    std::vector<std::pair<BasicBlock *, size_t>> toErase;

    std::function<void(BasicBlock *)> visit = [&](BasicBlock *block) {
        if (!block)
            return;
        std::vector<CheckKey> added;
        for (size_t idx = 0; idx < block->instructions.size(); ++idx) {
            Instr &instr = block->instructions[idx];
            if (!isCheckOpcode(instr.op))
                continue;

            // Constant-operand elimination: if SCCP has already inlined the
            // operands as literals and the check condition is statically
            // satisfied, remove the check and replace its result with the
            // pass-through value.  This avoids redundant runtime checks for
            // patterns like idx.chk(5, 0, 10) where the bounds are known.
            //
            // Type constraint: ConstInt values type as I64 in the verifier.
            // When the check result type is narrower (e.g. I32 for IdxChk)
            // and the result has uses, substituting an I64 constant would
            // produce a type mismatch that the verifier rejects.  In that
            // case fall through to the dominance-based check instead.
            Value trivialReplacement;
            if (isCheckTriviallyTrue(instr, trivialReplacement)) {
                if (instr.result && useInfo.hasUses(*instr.result)) {
                    // Check whether the replacement type matches the result type.
                    // ConstInt values type as I64; if the instruction result is
                    // narrower we cannot safely substitute without a type error.
                    const bool replacementIsI64ConstInt =
                        trivialReplacement.kind == Value::Kind::ConstInt &&
                        !trivialReplacement.isBool;
                    if (replacementIsI64ConstInt && instr.type.kind != Type::Kind::I64) {
                        // Type mismatch — fall through to dominance-based check.
                        // The dominance check can still replace with a same-typed
                        // Temp if a dominating occurrence already holds the result.
                    } else {
                        useInfo.replaceAllUses(*instr.result, trivialReplacement);
                        toErase.push_back({block, idx});
                        changed = true;
                        continue;
                    }
                } else {
                    // Result is either absent or has no live uses — safe to erase
                    // without substitution regardless of type.
                    toErase.push_back({block, idx});
                    changed = true;
                    continue;
                }
            }

            CheckKey key = makeCheckKey(instr);
            auto it = available.find(key);
            if (it != available.end() && domTree.dominates(it->second.block, block)) {
                if (instr.result && it->second.resultId) {
                    useInfo.replaceAllUses(*instr.result, Value::temp(*it->second.resultId));
                }
                toErase.push_back({block, idx});
                changed = true;
            } else {
                available[key] = DominatingCheck{block, instr.result};
                ++depthCount[key];
                added.push_back(key);
            }
        }

        auto childIt = domTree.children.find(block);
        if (childIt != domTree.children.end()) {
            for (auto *child : childIt->second)
                visit(child);
        }

        for (const auto &k : added) {
            auto cntIt = depthCount.find(k);
            if (cntIt != depthCount.end()) {
                if (--cntIt->second == 0) {
                    depthCount.erase(cntIt);
                    available.erase(k);
                }
            }
        }
    };

    visit(&function.blocks.front());

    for (auto it = toErase.rbegin(); it != toErase.rend(); ++it) {
        BasicBlock *block = it->first;
        size_t idx = it->second;
        block->instructions.erase(block->instructions.begin() + static_cast<std::ptrdiff_t>(idx));
    }

    // =========================================================================
    // Phase 2: Loop-invariant check hoisting
    // =========================================================================
    // For each loop, identify checks whose operands are loop-invariant and
    // hoist them to the preheader.

    for (const Loop &loop : loopInfo.loops()) {
        BasicBlock *header = findBlock(blockMap, loop.headerLabel);
        if (!header)
            continue;

        BasicBlock *preheader = findPreheader(function, loop, *header);
        if (!preheader)
            continue;
        if (loopHasEHSensitiveOps(loop, blockMap))
            continue;

        // Seed invariants with out-of-loop definitions
        std::unordered_set<unsigned> invariants;
        invariants.reserve(function.params.size() + 32);
        seedInvariants(loop, function, invariants);

        // Also include results from hoisted checks as invariant
        // (enabling cascading hoists in a single pass)

        // Process each block in the loop
        for (const std::string &blockLabel : loop.blockLabels) {
            BasicBlock *block = findBlock(blockMap, blockLabel);
            if (!block)
                continue;

            // Only hoist from blocks where the check is guaranteed to execute
            if (!isGuaranteedToExecute(*block, loop))
                continue;

            for (size_t idx = 0; idx < block->instructions.size();) {
                Instr &instr = block->instructions[idx];

                if (!isCheckOpcode(instr.op) || !operandsInvariant(instr, invariants)) {
                    ++idx;
                    continue;
                }

                // Hoist the check to the preheader
                Instr hoisted = std::move(instr);
                block->instructions.erase(block->instructions.begin() +
                                          static_cast<std::ptrdiff_t>(idx));

                // Insert before the terminator in the preheader
                size_t insertIdx = preheader->instructions.size();
                if (viper::il::isTerminated(*preheader) && insertIdx > 0)
                    --insertIdx;
                auto inserted = preheader->instructions.insert(
                    preheader->instructions.begin() + static_cast<std::ptrdiff_t>(insertIdx),
                    std::move(hoisted));

                // Copy result before any further vector operations could
                // invalidate the iterator returned by insert().
                auto hoistedResult = inserted->result;
                if (hoistedResult)
                    invariants.insert(*hoistedResult);

                changed = true;
                // Don't increment idx - we removed the current instruction
            }
        }
    }

    if (!changed)
        return PreservedAnalyses::all();

    // Invalidate analyses since we modified instructions
    PreservedAnalyses preserved;
    preserved.preserveAllModules();
    // CFG structure is preserved (no block additions/removals or edge changes)
    preserved.preserveFunction(kAnalysisCFG);
    // Dominators are preserved (we only remove instructions, not blocks)
    preserved.preserveFunction(kAnalysisDominators);
    // Loop info is preserved (loop structure unchanged)
    preserved.preserveFunction(kAnalysisLoopInfo);
    return preserved;
}

void registerCheckOptPass(PassRegistry &registry) {
    registry.registerFunctionPass("check-opt", []() { return std::make_unique<CheckOpt>(); }, true);
}

} // namespace il::transform
