//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements a small direct-call inliner with a simple cost model. The inliner
// targets tiny, non-recursive callees with a handful of blocks and no
// exception-handling constructs. Callee parameters (including block parameters)
// are mapped to call operands, SSA temporaries are remapped into the caller,
// and returns branch to a continuation block at the call site. A hard budget on
// instruction count, block count, and inline depth keeps code growth bounded.
//
//===----------------------------------------------------------------------===//

#include "il/transform/Inline.hpp"

#include "il/analysis/CallGraph.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include "il/utils/UseDefInfo.hpp"
#include "il/utils/Utils.hpp"

#include <algorithm>
#include <climits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace il::core;

namespace il::transform
{

namespace
{
constexpr unsigned kMaxCallSites = 8;
constexpr char kDepthKeySep = '\0';

using BlockDepthMap = std::unordered_map<std::string, unsigned>;

struct InlineCost
{
    unsigned instrCount = 0;
    unsigned blockCount = 0;
    unsigned callSites = 0;
    unsigned nestedCalls = 0; // Number of calls within this function
    unsigned returnCount = 0; // Number of return statements
    bool recursive = false;
    bool hasEH = false;
    bool hasAlloca = false;
    bool hasNonScalarSignature = false;
    bool unsupportedCFG = false;
    bool hasReturn = false;

    /// @brief Check if within basic structural constraints.
    bool isInlinable() const
    {
        return !recursive && !hasEH && !hasAlloca && !hasNonScalarSignature && !unsupportedCFG &&
               hasReturn;
    }

    /// @brief Compute adjusted cost considering bonuses.
    int adjustedCost(const InlineCostConfig &config, unsigned constArgCount) const
    {
        if (!isInlinable())
            return INT_MAX;

        int cost = static_cast<int>(instrCount);

        // Apply bonuses
        if (callSites == 1)
            cost -= static_cast<int>(config.singleUseBonus);

        if (instrCount <= 8)
            cost -= static_cast<int>(config.tinyFunctionBonus);

        // Constant arguments enable optimization
        cost -= static_cast<int>(constArgCount * config.constArgBonus);

        // Penalty for functions with many nested calls (may cause code explosion)
        cost += static_cast<int>(nestedCalls * 2);

        // Multiple returns are slightly more expensive to inline
        if (returnCount > 1)
            cost += static_cast<int>((returnCount - 1) * 2);

        return cost;
    }

    /// @brief Decide whether a call site is within the inline budget.
    /// @details First checks hard limits (inlinability flag, block count, call-site
    ///          count) and then compares the adjusted instruction cost against the
    ///          configured threshold.  Constant argument bonuses are applied inside
    ///          @ref adjustedCost so specialisable sites are more aggressively inlined.
    /// @param config        Tuning parameters for the inliner.
    /// @param constArgCount Number of call arguments known to be constants.
    /// @return @c true when inlining is legal and within cost budget.
    bool withinBudget(const InlineCostConfig &config, unsigned constArgCount) const
    {
        if (!isInlinable())
            return false;
        if (blockCount > config.blockBudget)
            return false;
        if (callSites > kMaxCallSites)
            return false;

        int cost = adjustedCost(config, constArgCount);
        return cost <= static_cast<int>(config.instrThreshold);
    }
};

/// @brief Build a composite key for the block-depth map.
/// @details Concatenates the function name and block label with a NUL separator
///          so that no valid identifier can collide with the combined key.
/// @param fn Function name prefix.
/// @param label Block label suffix.
/// @return Composite key suitable for BlockDepthMap lookups.
std::string depthKey(const std::string &fn, const std::string &label)
{
    return fn + kDepthKeySep + label;
}

/// @brief Query the inline depth recorded for a specific block.
/// @param depths Map of (function+label) → depth values.
/// @param fn Owning function name.
/// @param label Block label within the function.
/// @return Recorded depth, or 0 if no entry exists.
unsigned getBlockDepth(const BlockDepthMap &depths, const std::string &fn, const std::string &label)
{
    auto it = depths.find(depthKey(fn, label));
    if (it == depths.end())
        return 0;
    return it->second;
}

/// @brief Record the inline depth for a specific block.
/// @param depths Map of (function+label) → depth values (modified in-place).
/// @param fn Owning function name.
/// @param label Block label within the function.
/// @param depth Inline nesting depth to store.
void setBlockDepth(BlockDepthMap &depths,
                   const std::string &fn,
                   const std::string &label,
                   unsigned depth)
{
    depths[depthKey(fn, label)] = depth;
}

/// @brief Test whether an instruction is a direct (non-indirect) call.
/// @param I Instruction to inspect.
/// @return True when the opcode is Call and a callee name is present.
bool isDirectCall(const Instr &I)
{
    return I.op == Opcode::Call && !I.callee.empty();
}

/// @brief Test whether an instruction is part of the exception-handling framework.
/// @param I Instruction to inspect.
/// @return True for EhPush, EhPop, EhEntry, ResumeSame, ResumeNext, ResumeLabel.
bool isEHSensitive(const Instr &I)
{
    switch (I.op)
    {
        case Opcode::EhPush:
        case Opcode::EhPop:
        case Opcode::EhEntry:
        case Opcode::ResumeSame:
        case Opcode::ResumeNext:
        case Opcode::ResumeLabel:
            return true;
        default:
            return false;
    }
}

/// @brief Test whether a terminator instruction is unsupported for inlining.
/// @details The inliner only handles Ret, Br, CBr, and SwitchI32 terminators.
/// @param I Terminator instruction to check.
/// @return True when the terminator cannot be inlined.
bool hasUnsupportedTerminator(const Instr &I)
{
    return !(I.op == Opcode::Ret || I.op == Opcode::Br || I.op == Opcode::CBr ||
             I.op == Opcode::SwitchI32);
}

/// @brief Count the total number of instructions across all blocks of a function.
/// @param F Function to measure.
/// @return Sum of instruction counts in every block.
unsigned countInstructions(const Function &F)
{
    unsigned n = 0;
    for (const auto &B : F.blocks)
        n += static_cast<unsigned>(B.instructions.size());
    return n;
}

/// @brief Look up the debug name for an SSA value, falling back to a default.
/// @param F Function whose valueNames table is queried.
/// @param id SSA value identifier.
/// @param fallback String returned when no name is recorded for @p id.
/// @return The stored name if present and non-empty; otherwise @p fallback.
std::string lookupValueName(const Function &F, unsigned id, const std::string &fallback)
{
    if (id < F.valueNames.size() && !F.valueNames[id].empty())
        return F.valueNames[id];
    return fallback;
}

/// @brief Collect every explicit temp/param name already used in a function.
/// @details The textual IL parser resolves temps by printed name, so any new
///          names introduced by the inliner must be unique within the whole
///          function, not just within the inlined region.
std::unordered_set<std::string> collectUsedValueNames(const Function &F)
{
    std::unordered_set<std::string> names;
    names.reserve(F.params.size() + F.blocks.size() * 4 + F.valueNames.size());

    for (const auto &param : F.params)
        if (!param.name.empty())
            names.insert(param.name);

    for (const auto &block : F.blocks)
        for (const auto &param : block.params)
            if (!param.name.empty())
                names.insert(param.name);

    for (const auto &name : F.valueNames)
        if (!name.empty())
            names.insert(name);

    return names;
}

/// @brief Make a temp/param name unique within a function namespace.
/// @param usedNames Set of names already reserved in the function. Updated in place.
/// @param base Desired name.
/// @return A unique name derived from @p base.
std::string reserveUniqueValueName(std::unordered_set<std::string> &usedNames, std::string base)
{
    if (base.empty())
        base = "tmp";
    if (!usedNames.contains(base))
    {
        usedNames.insert(base);
        return base;
    }

    std::string candidate = base;
    unsigned suffix = 0;
    do
    {
        candidate = base + "_" + std::to_string(++suffix);
    } while (usedNames.contains(candidate));

    usedNames.insert(candidate);
    return candidate;
}

/// @brief Record a debug name for an SSA value, growing the table if needed.
/// @param F Function whose valueNames table is modified.
/// @param id SSA value identifier.
/// @param name Name to associate; empty names are silently ignored.
void ensureValueName(Function &F, unsigned id, const std::string &name)
{
    if (name.empty())
        return;
    if (F.valueNames.size() <= id)
        F.valueNames.resize(id + 1);
    F.valueNames[id] = name;
}

InlineCost evaluateInlineCost(const Function &fn, const viper::analysis::CallGraph &cg)
{
    InlineCost cost;
    cost.instrCount = countInstructions(fn);
    cost.blockCount = static_cast<unsigned>(fn.blocks.size());

    auto isScalarType = [](const Type &type)
    {
        return type.kind == Type::Kind::I64 || type.kind == Type::Kind::I1 ||
               type.kind == Type::Kind::F64 || type.kind == Type::Kind::Void;
    };
    if (!isScalarType(fn.retType))
        cost.hasNonScalarSignature = true;
    for (const auto &param : fn.params)
        if (!isScalarType(param.type))
            cost.hasNonScalarSignature = true;

    auto callIt = cg.callCounts.find(fn.name);
    if (callIt != cg.callCounts.end())
        cost.callSites = callIt->second;

    auto edgeIt = cg.edges.find(fn.name);
    if (edgeIt != cg.edges.end())
    {
        for (const auto &target : edgeIt->second)
        {
            if (target == fn.name)
            {
                cost.recursive = true;
                break;
            }
        }
    }

    if (fn.blocks.empty())
    {
        cost.unsupportedCFG = true;
        return cost;
    }

    // Entry-block params are handled: we pass call arguments as branch args
    // when jumping to the cloned entry block (see inlineCallSite).

    for (const auto &B : fn.blocks)
    {
        if (!B.terminated || B.instructions.empty())
        {
            cost.unsupportedCFG = true;
            continue;
        }

        const Instr &term = B.instructions.back();
        if (hasUnsupportedTerminator(term))
            cost.unsupportedCFG = true;

        if (term.op == Opcode::Ret)
        {
            cost.hasReturn = true;
            ++cost.returnCount;
            bool expectValue = fn.retType.kind != Type::Kind::Void;
            bool hasValue = !term.operands.empty();
            if (expectValue != hasValue)
                cost.unsupportedCFG = true;
        }

        for (const auto &I : B.instructions)
        {
            if (isEHSensitive(I))
                cost.hasEH = true;

            if (I.op == Opcode::Alloca)
                cost.hasAlloca = true;

            // Count nested calls
            if (I.op == Opcode::Call || I.op == Opcode::CallIndirect)
                ++cost.nestedCalls;
        }
    }

    return cost;
}

/// @brief Count constant arguments in a call instruction.
unsigned countConstantArgs(const Instr &callInstr)
{
    unsigned count = 0;
    for (const auto &op : callInstr.operands)
    {
        if (op.kind == Value::Kind::ConstInt || op.kind == Value::Kind::ConstFloat ||
            op.kind == Value::Kind::NullPtr || op.kind == Value::Kind::ConstStr)
        {
            ++count;
        }
    }
    return count;
}

/// @brief Generate a block label that does not collide with existing labels.
/// @details Builds an unordered_set of existing labels for O(1) collision checks,
///          then appends increasing numeric suffixes until a unique name is found.
///          This replaces a previous O(n) linear scan per candidate, improving
///          performance from O(n*k) to O(n+k) where n = block count, k = attempts.
/// @param function Function whose blocks define the label namespace.
/// @param base Desired label prefix; returned as-is when no collision occurs.
/// @return A label guaranteed to be unique within @p function.
std::string makeUniqueLabel(const Function &function, const std::string &base)
{
    std::unordered_set<std::string> existingLabels;
    existingLabels.reserve(function.blocks.size());
    for (const auto &block : function.blocks)
        existingLabels.insert(block.label);

    std::string candidate = base;
    unsigned suffix = 0;
    while (existingLabels.count(candidate))
    {
        candidate = base + "." + std::to_string(++suffix);
    }
    return candidate;
}

/// @brief Remap a temporary value through a substitution map.
/// @param v The value to remap.
/// @param map Mapping from old temporary IDs to replacement values.
/// @return The replacement value if \p v is a temporary found in \p map,
///         otherwise \p v unchanged.
Value remapValue(const Value &v, const std::unordered_map<unsigned, Value> &map)
{
    if (v.kind != Value::Kind::Temp)
        return v;
    auto it = map.find(v.id);
    if (it == map.end())
        return v;
    return it->second;
}

/// @brief Replace all uses of a temporary in a basic block.
/// @details Scans every instruction operand and branch argument in \p block,
///          replacing any temporary whose ID matches \p from with \p replacement.
/// @param block The basic block to rewrite.
/// @param from  The temporary ID to search for.
/// @param replacement The value to substitute in place of the old temporary.
void replaceUsesInBlock(BasicBlock &block, unsigned from, const Value &replacement)
{
    for (auto &instr : block.instructions)
    {
        for (auto &op : instr.operands)
        {
            if (op.kind == Value::Kind::Temp && op.id == from)
                op = replacement;
        }

        for (auto &argList : instr.brArgs)
        {
            for (auto &arg : argList)
            {
                if (arg.kind == Value::Kind::Temp && arg.id == from)
                    arg = replacement;
            }
        }
    }
}

bool inlineCallSite(Function &caller,
                    size_t callBlockIdx,
                    size_t callIndex,
                    const Function &callee,
                    unsigned callDepth,
                    unsigned maxDepth,
                    BlockDepthMap &depths,
                    const std::unordered_map<std::string, const Function *> &functionLookup)
{
    if (callDepth >= maxDepth)
        return false;

    if (callBlockIdx >= caller.blocks.size())
        return false;

    caller.blocks.reserve(caller.blocks.size() + callee.blocks.size() + 1);

    BasicBlock &callBlock = caller.blocks[callBlockIdx];
    if (callIndex >= callBlock.instructions.size())
        return false;
    // Copy the call instruction — the reference becomes dangling after the
    // block is resized below.
    const Instr callInstr = callBlock.instructions[callIndex];

    if (callInstr.operands.size() != callee.params.size())
        return false;

    // Skip inlining callees that were already modified by a prior inline.
    for (const auto &B : callee.blocks)
    {
        if (B.label.find(".inline.") != std::string::npos)
            return false;
    }

    // Skip inlining when the callee's entry block has params that can't ALL
    // be mapped to function params. After mem2reg + SimplifyCFG, the entry
    // block may have loop-carried params or rewritten param IDs that don't
    // correspond to any function param. The inline pass can only provide call
    // operands (matching function params), not loop-internal values.
    if (!callee.blocks.empty())
    {
        for (const auto &ep : callee.blocks.front().params)
        {
            bool mapped = false;
            for (const auto &fp : callee.params)
            {
                if (fp.id == ep.id)
                {
                    mapped = true;
                    break;
                }
            }
            if (!mapped)
                return false;
        }
    }

    // The parser leaves call instruction type as Void for non-f64 returns
    // (only f64 is recorded for register-class selection).  Use the callee's
    // declared retType instead of the call instruction's type for the match.
    // Bail out only when the callee genuinely returns void but the call has a
    // result, or vice versa.
    if (callee.retType.kind == Type::Kind::Void && callInstr.result)
        return false;
    if (callee.retType.kind != Type::Kind::Void && !callInstr.result)
        return false;

    bool returnsValue = callee.retType.kind != Type::Kind::Void;
    if (!returnsValue && callInstr.result)
        return false;

    unsigned nextId = viper::il::nextTempId(caller);

    // Value mapping from callee temps/params to caller values.
    std::unordered_map<unsigned, Value> valueMap;
    valueMap.reserve(callee.params.size() + callee.blocks.size() * 2);
    for (size_t i = 0; i < callee.params.size(); ++i)
        valueMap.emplace(callee.params[i].id, callInstr.operands[i]);

    // Build label map for cloned blocks.
    std::unordered_map<std::string, std::string> labelMap;
    labelMap.reserve(callee.blocks.size());
    for (const auto &B : callee.blocks)
    {
        std::string base = callBlock.label + ".inline." + callee.name + "." + B.label;
        labelMap.emplace(B.label, makeUniqueLabel(caller, base));
    }

    // =========================================================================
    // PHASE 1: Read-only analysis — can safely bail at any point
    // =========================================================================

    // Build continuation block from instructions after the call.
    BasicBlock continuation;
    continuation.label = makeUniqueLabel(caller, callBlock.label + ".inline.cont");
    continuation.instructions.assign(callBlock.instructions.begin() +
                                         static_cast<long>(callIndex + 1),
                                     callBlock.instructions.end());
    continuation.terminated = callBlock.terminated;

    // Compute return param info (but don't add to continuation yet).
    std::unordered_set<std::string> usedValueNames = collectUsedValueNames(caller);

    Param retParam;
    bool hasRetParam = returnsValue && callInstr.result;
    if (hasRetParam)
    {
        retParam.name = reserveUniqueValueName(
            usedValueNames,
            lookupValueName(caller, *callInstr.result, "ret" + std::to_string(*callInstr.result)));
        retParam.id = nextId++;
        retParam.type = callee.retType;
    }

    // Compute escaped IDs: temps used in continuation but not defined there.
    // Include both the return param AND the original call result ID in contDefined.
    // The call result will be replaced by retParam in Phase 2 (UseDefInfo::replaceAllUses),
    // but since escape analysis runs in Phase 1 (before replacement), we must exclude
    // the original call result from escapedIds to avoid creating a spurious escaped param.
    std::unordered_set<unsigned> contDefined;
    if (hasRetParam)
    {
        contDefined.insert(retParam.id);
        contDefined.insert(*callInstr.result); // exclude original result from escape detection
    }
    for (const auto &instr : continuation.instructions)
    {
        if (instr.result)
            contDefined.insert(*instr.result);
    }

    std::unordered_set<unsigned> contUsed;
    for (const auto &instr : continuation.instructions)
    {
        for (const auto &op : instr.operands)
        {
            if (op.kind == Value::Kind::Temp)
                contUsed.insert(op.id);
        }
        for (const auto &argList : instr.brArgs)
        {
            for (const auto &v : argList)
            {
                if (v.kind == Value::Kind::Temp)
                    contUsed.insert(v.id);
            }
        }
    }

    // Build set of alloca result IDs — allocas are function-scoped resources
    // that persist after call-block truncation.  They must NOT be threaded
    // through continuation block parameters because:
    //   1. The alloca definition survives in the truncated call block.
    //   2. Escaping an alloca creates a bridge reference (Value::temp(origId))
    //      that DCE can orphan when it removes write-only allocas.
    //   3. The continuation can reference the alloca directly.
    std::unordered_set<unsigned> allocaIds;
    for (const auto &bb : caller.blocks)
    {
        for (const auto &instr : bb.instructions)
        {
            if (instr.op == Opcode::Alloca && instr.result)
                allocaIds.insert(*instr.result);
        }
    }

    std::vector<unsigned> escapedIds;
    for (unsigned id : contUsed)
    {
        if (contDefined.find(id) == contDefined.end() && allocaIds.find(id) == allocaIds.end())
            escapedIds.push_back(id);
    }
    std::sort(escapedIds.begin(), escapedIds.end());

    // Type inference for escaped values — runs on FULL (pre-truncated) caller.
    // This is the key advantage of Phase 1: we see ALL instructions including
    // those in the call block that will be truncated in Phase 2.
    struct EscapedParamInfo
    {
        Param param;
        bool typeFound{false};
    };

    std::vector<EscapedParamInfo> escapedParamInfos;
    escapedParamInfos.reserve(escapedIds.size());

    for (unsigned origId : escapedIds)
    {
        EscapedParamInfo info;
        Param &p = info.param;
        p.name = reserveUniqueValueName(
            usedValueNames, lookupValueName(caller, origId, "ext" + std::to_string(origId)));
        p.id = nextId++;
        p.type = Type(Type::Kind::I64); // fallback default

        // Search ALL caller blocks (including the FULL call block, not yet truncated).
        for (const auto &bb : caller.blocks)
        {
            for (const auto &bp : bb.params)
            {
                if (bp.id == origId)
                {
                    p.type = bp.type;
                    info.typeFound = true;
                }
            }
            for (const auto &ins : bb.instructions)
            {
                if (ins.result && *ins.result == origId)
                {
                    // Call instruction types are canonically Void for non-F64
                    // returns (only F64 is recorded for register-class selection).
                    // Resolve the actual return type from the module's function
                    // lookup to avoid typing escaped Call results as Void.
                    if (ins.op == Opcode::Call && ins.type.kind == Type::Kind::Void)
                    {
                        auto calleeFnIt = functionLookup.find(ins.callee);
                        if (calleeFnIt != functionLookup.end())
                            p.type = calleeFnIt->second->retType;
                        // else: external/unresolved call — keep I64 fallback
                    }
                    else
                    {
                        p.type = ins.type;
                    }
                    info.typeFound = true;
                }
            }
        }
        // Also check function params.
        for (const auto &fp : caller.params)
        {
            if (fp.id == origId)
            {
                p.type = fp.type;
                info.typeFound = true;
            }
        }
        // Also check continuation instructions.
        if (!info.typeFound)
        {
            for (const auto &ins : continuation.instructions)
            {
                if (ins.result && *ins.result == origId)
                {
                    p.type = ins.type;
                    info.typeFound = true;
                }
            }
        }

        escapedParamInfos.push_back(std::move(info));
    }

    // Note: escaped values without found types use the I64 fallback.
    // This is imprecise but safe — the verifier will catch actual type
    // mismatches, and the inline pass can be retried with better type info
    // after other optimization passes clean up the IL.

    // =========================================================================
    // PHASE 2: Commit — all validation passed, now mutate the caller
    // =========================================================================

    // Truncate call block (POINT OF NO RETURN).
    callBlock.instructions.resize(callIndex);
    callBlock.terminated = false;

    // Add return param to continuation.
    if (hasRetParam)
    {
        continuation.params.push_back(retParam);
        ensureValueName(caller, retParam.id, retParam.name);

        Value repl = Value::temp(retParam.id);
        viper::il::UseDefInfo useInfo(caller);
        useInfo.replaceAllUses(*callInstr.result, repl);
        replaceUsesInBlock(continuation, *callInstr.result, repl);
    }

    // Add escaped params to continuation and build the remap.
    std::unordered_map<unsigned, unsigned> escapedMap;
    for (size_t i = 0; i < escapedIds.size(); ++i)
    {
        continuation.params.push_back(escapedParamInfos[i].param);
        ensureValueName(caller, escapedParamInfos[i].param.id, escapedParamInfos[i].param.name);
        escapedMap[escapedIds[i]] = escapedParamInfos[i].param.id;
    }

    // Remap continuation instructions to use the new params.
    if (!escapedMap.empty())
    {
        for (auto &instr : continuation.instructions)
        {
            for (auto &op : instr.operands)
            {
                if (op.kind == Value::Kind::Temp)
                {
                    auto it = escapedMap.find(op.id);
                    if (it != escapedMap.end())
                        op = Value::temp(it->second);
                }
            }
            for (auto &argList : instr.brArgs)
            {
                for (auto &v : argList)
                {
                    if (v.kind == Value::Kind::Temp)
                    {
                        auto it = escapedMap.find(v.id);
                        if (it != escapedMap.end())
                            v = Value::temp(it->second);
                    }
                }
            }
        }
    }

    // Clone callee blocks.
    std::vector<BasicBlock> clonedBlocks;
    clonedBlocks.reserve(callee.blocks.size());

    for (const auto &srcBlock : callee.blocks)
    {
        BasicBlock clone;
        clone.label = labelMap.at(srcBlock.label);

        // Clone block parameters with fresh IDs.
        // Use unique names to avoid collision with caller-scope temps that have
        // the same numeric names but different types (e.g., caller has %t8:ptr,
        // callee has %t8:i64). Without uniquification, the IL verifier may
        // resolve a branch argument to the wrong definition in scope.
        clone.params.reserve(srcBlock.params.size());
        for (const auto &param : srcBlock.params)
        {
            Param p = param;
            p.id = nextId++;
            valueMap[param.id] = Value::temp(p.id);
            std::string origName = lookupValueName(callee, param.id, param.name);
            std::string uniqueName = origName + "_il" + std::to_string(p.id);
            p.name = uniqueName; // Update the Param's own name to avoid collision
            clone.params.push_back(p);
            ensureValueName(caller, p.id, uniqueName);
        }

        for (size_t idx = 0; idx < srcBlock.instructions.size(); ++idx)
        {
            const Instr &CI = srcBlock.instructions[idx];

            if (idx + 1 == srcBlock.instructions.size() && CI.op == Opcode::Ret)
            {
                Instr bridge;
                bridge.op = Opcode::Br;
                bridge.type = Type(Type::Kind::Void);
                bridge.labels.push_back(continuation.label);

                if (!continuation.params.empty())
                {
                    bridge.brArgs.emplace_back();
                    auto &bridgeArgs = bridge.brArgs.back();
                    // Pass the return value as the continuation block's first parameter.
                    // Always emit a return value arg when the callee is non-void,
                    // even if this particular Ret has no operands (e.g., unreachable
                    // void-ret in a non-void function after optimization).
                    if (returnsValue)
                    {
                        if (!CI.operands.empty())
                            bridgeArgs.push_back(remapValue(CI.operands.front(), valueMap));
                        else
                            bridgeArgs.push_back(Value::constInt(0));
                    }
                    // Pass escaped caller values as extra parameters.
                    for (unsigned origId : escapedIds)
                        bridgeArgs.push_back(Value::temp(origId));
                }

                clone.instructions.push_back(std::move(bridge));
                clone.terminated = true;
                continue;
            }

            Instr cloned = CI;
            cloned.operands.clear();
            cloned.labels.clear();
            cloned.brArgs.clear();

            cloned.operands.reserve(CI.operands.size());
            for (const auto &op : CI.operands)
                cloned.operands.push_back(remapValue(op, valueMap));

            cloned.labels.reserve(CI.labels.size());
            for (const auto &lab : CI.labels)
                cloned.labels.push_back(labelMap.at(lab));

            cloned.brArgs.reserve(CI.brArgs.size());
            for (const auto &argList : CI.brArgs)
            {
                std::vector<Value> remapped;
                remapped.reserve(argList.size());
                for (const auto &arg : argList)
                    remapped.push_back(remapValue(arg, valueMap));
                cloned.brArgs.push_back(std::move(remapped));
            }

            if (CI.result)
            {
                cloned.result = nextId;
                valueMap[*CI.result] = Value::temp(nextId);
                std::string origName = lookupValueName(callee, *CI.result, "");
                std::string uniqueName = origName.empty()
                                             ? ("t" + std::to_string(nextId))
                                             : (origName + "_il" + std::to_string(nextId));
                ensureValueName(caller, nextId, uniqueName);
                ++nextId;
            }

            clone.instructions.push_back(std::move(cloned));
        }

        if (!clone.terminated && !clone.instructions.empty())
            clone.terminated = clone.instructions.back().op == Opcode::Br ||
                               clone.instructions.back().op == Opcode::CBr ||
                               clone.instructions.back().op == Opcode::SwitchI32;

        clonedBlocks.push_back(std::move(clone));
    }

    // Branch from call site to cloned entry block.
    Instr jump;
    jump.op = Opcode::Br;
    jump.type = Type(Type::Kind::Void);
    jump.labels.push_back(labelMap.at(callee.blocks.front().label));

    // Pass call arguments as branch args when the entry block has params.
    // The pre-mutation validation guarantees all entry block params can be
    // mapped to function params by ID. Map each to its call operand.
    const auto &origEntryParams = callee.blocks.front().params;
    if (!origEntryParams.empty())
    {
        std::vector<Value> args;
        args.reserve(origEntryParams.size());
        for (const auto &ep : origEntryParams)
        {
            bool found = false;
            for (size_t k = 0; k < callee.params.size(); ++k)
            {
                if (callee.params[k].id == ep.id && k < callInstr.operands.size())
                {
                    args.push_back(callInstr.operands[k]);
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                // Entry block param has no matching function param — bail out.
                // This can happen after SimplifyCFG/mem2reg rewrites params.
                return false;
            }
        }
        jump.brArgs.push_back(std::move(args));
    }

    callBlock.instructions.push_back(std::move(jump));
    callBlock.terminated = true;

    // Insert the inlined region immediately after the call block. The textual
    // IL parser expects value definitions to appear before uses, so appending
    // the continuation block at function end can leave its allocas/results
    // referenced by original successor blocks that are serialized earlier.
    setBlockDepth(depths, caller.name, continuation.label, callDepth);
    for (auto &B : clonedBlocks)
        setBlockDepth(depths, caller.name, B.label, callDepth + 1);

    auto insertPos = caller.blocks.begin() + static_cast<std::ptrdiff_t>(callBlockIdx + 1);
    insertPos = caller.blocks.insert(insertPos,
                                     std::make_move_iterator(clonedBlocks.begin()),
                                     std::make_move_iterator(clonedBlocks.end()));
    caller.blocks.insert(insertPos + static_cast<std::ptrdiff_t>(callee.blocks.size()),
                         std::move(continuation));

    return true;
}

} // namespace

std::string_view Inliner::id() const
{
    return "inline";
}

PreservedAnalyses Inliner::run(Module &module, AnalysisManager &)
{
    viper::analysis::CallGraph cg = viper::analysis::buildCallGraph(module);

    std::unordered_map<std::string, const Function *> functionLookup;
    std::unordered_map<std::string, InlineCost> costCache;

    functionLookup.reserve(module.functions.size());
    costCache.reserve(module.functions.size());

    for (const auto &fn : module.functions)
    {
        functionLookup.emplace(fn.name, &fn);
        costCache.emplace(fn.name, evaluateInlineCost(fn, cg));
    }

    unsigned codeGrowth = 0;

    BlockDepthMap depths;
    for (const auto &fn : module.functions)
        for (const auto &B : fn.blocks)
            setBlockDepth(depths, fn.name, B.label, 0);

    bool changed = false;
    std::unordered_set<std::string> changedFunctions;

    for (size_t fnIdx = 0; fnIdx < module.functions.size(); ++fnIdx)
    {
        Function &caller = module.functions[fnIdx];

        // Snapshot block count: only iterate ORIGINAL blocks, not ones added
        // by inlining. Newly inlined blocks will be handled on the next pass
        // invocation. This prevents unbounded growth when inlined code contains
        // more calls.
        const size_t originalBlockCount = caller.blocks.size();
        for (size_t blockIdx = 0; blockIdx < originalBlockCount; ++blockIdx)
        {
            BasicBlock &block = caller.blocks[blockIdx];
            size_t instIdx = 0;
            while (instIdx < block.instructions.size())
            {
                const Instr &I = block.instructions[instIdx];
                if (!isDirectCall(I))
                {
                    ++instIdx;
                    continue;
                }

                auto calleeIt = functionLookup.find(I.callee);
                if (calleeIt == functionLookup.end())
                {
                    ++instIdx;
                    continue;
                }
                const Function *callee = calleeIt->second;
                if (callee->name == caller.name)
                {
                    ++instIdx;
                    continue;
                }

                auto edgeIt = cg.edges.find(callee->name);
                if (edgeIt != cg.edges.end() &&
                    std::find(edgeIt->second.begin(), edgeIt->second.end(), caller.name) !=
                        edgeIt->second.end())
                {
                    ++instIdx;
                    continue;
                }

                // Check code growth budget
                const InlineCost &cost = costCache.at(callee->name);
                if (codeGrowth + cost.instrCount > config_.maxCodeGrowth)
                {
                    ++instIdx;
                    continue;
                }

                // Use enhanced cost model with constant argument bonuses
                unsigned constArgs = countConstantArgs(I);
                if (!cost.withinBudget(config_, constArgs))
                {
                    ++instIdx;
                    continue;
                }

                unsigned depth = getBlockDepth(depths, caller.name, block.label);
                if (!inlineCallSite(caller,
                                    blockIdx,
                                    instIdx,
                                    *callee,
                                    depth,
                                    config_.maxInlineDepth,
                                    depths,
                                    functionLookup))
                {
                    ++instIdx;
                    continue;
                }

                // Track code growth (callee instructions minus the call itself)
                if (cost.instrCount > 1)
                    codeGrowth += cost.instrCount - 1;

                changed = true;
                changedFunctions.insert(caller.name);
                break; // block reshaped; move to next block
            }
        }
    }

    if (!changed)
        return PreservedAnalyses::all();
    PreservedAnalyses preserved;
    for (const auto &name : changedFunctions)
        preserved.markChangedFunction(name);
    return preserved;
}

void registerInlinePass(PassRegistry &registry)
{
    registry.registerModulePass("inline",
                                [](core::Module &module, AnalysisManager &analysis)
                                {
                                    Inliner inliner;
                                    return inliner.run(module, analysis);
                                });
}

} // namespace il::transform
