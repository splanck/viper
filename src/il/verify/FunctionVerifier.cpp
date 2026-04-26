//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/verify/FunctionVerifier.cpp
// Purpose: Coordinate function-level IL verification by combining block checks
//          with opcode-specific instruction strategies.
// Key invariants: Each function must expose a valid entry block, maintain
//                 unique labels, and respect extern/runtime signatures and
//                 handler semantics.
// Ownership/Lifetime: Operates on module-provided IR structures without
//                     retaining ownership beyond the call scope.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/verify/FunctionVerifier.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include "il/verify/ControlFlowChecker.hpp"
#include "il/verify/DiagFormat.hpp"
#include "il/verify/ExceptionHandlerAnalysis.hpp"
#include "il/verify/InstructionChecker.hpp"
#include "il/verify/InstructionStrategies.hpp"
#include "il/verify/TypeInference.hpp"
#include "il/verify/VerifyCtx.hpp"

#include <algorithm>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

using namespace il::core;

namespace il::verify {
using il::support::Diag;
using il::support::Expected;
using il::support::makeError;

namespace {

/// @brief Identify whether an opcode belongs to the resume-family terminators.
///
/// @details Resume opcodes have additional verifier requirements: they are only
///          legal inside handler blocks and must forward the `%tok` parameter.
///          Recognising them allows the verifier to enforce those constraints
///          uniformly.
///
/// @param op Opcode under classification.
/// @return @c true when the opcode is resume.same/next/label.
bool isResumeOpcode(Opcode op) {
    return op == Opcode::ResumeSame || op == Opcode::ResumeNext || op == Opcode::ResumeLabel;
}

/// @brief Detect opcodes that read fields from an error value.
///
/// @details Used to prevent explicit-error-operand `err.get_*` opcodes from
///          appearing outside handler blocks. Native EH lowering can leave the
///          operandless form, which reads the platform's current exception.
///
/// @param op Opcode under inspection.
/// @return @c true when @p op accesses error metadata.
bool isErrAccessOpcode(Opcode op) {
    switch (op) {
        case Opcode::ErrGetKind:
        case Opcode::ErrGetCode:
        case Opcode::ErrGetIp:
        case Opcode::ErrGetLine:
        case Opcode::ErrGetMsg:
            return true;
        default:
            return false;
    }
}

/// @brief Recognise runtime helper calls that release array handles.
///
/// @details The verifier tracks releases so it can flag double-free and
///          use-after-release errors on SSA temporaries that reference runtime
///          arrays.
///
/// @param instr Instruction being analysed.
/// @return @c true when the instruction is the runtime array release helper.
bool isRuntimeArrayRelease(const Instr &instr) {
    return instr.op == Opcode::Call &&
           (instr.callee == "rt_arr_i32_release" || instr.callee == "rt_arr_str_release" ||
            instr.callee == "rt_arr_obj_release");
}

Expected<void> validateFunctionParams(const Function &fn) {
    std::unordered_set<std::string> paramNames;
    std::unordered_set<unsigned> paramIds;
    for (const auto &param : fn.params) {
        if (!paramNames.insert(param.name).second)
            return Expected<void>{makeError({}, fn.name + ": duplicate param %" + param.name)};

        if (!paramIds.insert(param.id).second) {
            std::ostringstream message;
            message << fn.name << ": duplicate param id %" << param.id;
            return Expected<void>{makeError({}, message.str())};
        }

        if (param.type.kind == Type::Kind::Void)
            return Expected<void>{
                makeError({}, fn.name + ": param %" + param.name + " has void type")};
    }
    return {};
}

bool signaturesMatch(const Extern &ext, const Function &fn) {
    if (ext.retType.kind != fn.retType.kind || ext.params.size() != fn.params.size())
        return false;
    for (size_t i = 0; i < ext.params.size(); ++i)
        if (ext.params[i].kind != fn.params[i].type.kind)
            return false;
    return true;
}

} // namespace

Expected<void> validateBlockParams_E(const Function &fn,
                                     const BasicBlock &bb,
                                     TypeInference &types,
                                     std::vector<unsigned> &paramIds);
Expected<void> checkBlockTerminators_E(const Function &fn, const BasicBlock &bb);
Expected<void> verifyOpcodeSignature_E(const VerifyCtx &ctx);
Expected<void> verifyInstruction_E(const Function &fn,
                                   const BasicBlock &bb,
                                   const Instr &instr,
                                   const std::unordered_map<std::string, const Extern *> &externs,
                                   const std::unordered_map<std::string, const Function *> &funcs,
                                   TypeInference &types,
                                   DiagSink &sink);

/// @brief Construct a verifier with knowledge of extern signatures.
///
/// @details The extern map is cached so that call instructions can be checked
///          against known signatures.  Instruction strategies are seeded with
///          the default collection used to validate every opcode.
///
/// @param externs Map from extern names to their declarations.
FunctionVerifier::FunctionVerifier(const ExternMap &externs)
    : externs_(externs), strategies_(makeDefaultInstructionStrategies()) {}

/// @brief Verify every function in a module for structural correctness.
///
/// @details Builds a name-to-function map to detect duplicates before invoking
///          @ref verifyFunction on each function.  Verification stops at the
///          first failure so the most relevant diagnostic can be reported to
///          users immediately.
///
/// @param module Module containing functions to verify.
/// @param sink Diagnostic sink receiving instruction-level messages.
/// @return Empty Expected on success or the first failure diagnostic.
Expected<void> FunctionVerifier::run(const Module &module, DiagSink &sink) {
    functionMap_.clear();

    for (const auto &fn : module.functions) {
        if (!functionMap_.emplace(fn.name, &fn).second)
            return Expected<void>{makeError({}, "duplicate function @" + fn.name)};
    }

    std::unordered_set<std::string> globalNames;
    for (const auto &global : module.globals) {
        globalNames.insert(global.name);
        if (externs_.contains(global.name))
            return Expected<void>{
                makeError({}, "global @" + global.name + " collides with extern @" + global.name)};
    }

    for (const auto &fn : module.functions) {
        if (globalNames.contains(fn.name))
            return Expected<void>{
                makeError({}, "function @" + fn.name + " collides with global @" + fn.name)};
    }

    for (const auto &fn : module.functions)
        if (auto result = verifyFunction(fn, sink); !result)
            return result;

    return {};
}

/// @brief Validate a single function's blocks, labels, and handler metadata.
///
/// @details Ensures the first block is an entry block, checks for extern
///          signature parity, records handler signatures, and validates that all
///          referenced labels exist. Block-level checks are delegated to
///          @ref verifyBlock.
///
/// @param fn Function being verified.
/// @param sink Diagnostic sink for detailed messages.
/// @return Success or a diagnostic describing the first failure.
Expected<void> FunctionVerifier::verifyFunction(const Function &fn, DiagSink &sink) {
    if (auto result = validateFunctionParams(fn); !result)
        return result;

    if (auto it = externs_.find(fn.name); it != externs_.end()) {
        const Extern *ext = it->second;
        if (!signaturesMatch(*ext, fn))
            return Expected<void>{
                makeError({}, "function @" + fn.name + " signature mismatch with extern")};
        if (fn.linkage != Linkage::Import)
            return Expected<void>{
                makeError({}, "function @" + fn.name + " collides with extern @" + fn.name)};
    }

    // Import-linkage functions are declarations with no body; skip body verification.
    if (fn.linkage == Linkage::Import) {
        if (!fn.blocks.empty())
            return Expected<void>{
                makeError({}, formatFunctionDiag(fn, "import function must not have a body"))};
        return {};
    }

    if (fn.blocks.empty())
        return Expected<void>{makeError({}, formatFunctionDiag(fn, "function has no blocks"))};

    const std::string &firstLabel = fn.blocks.front().label;
    const bool isEntry = firstLabel == "entry" || firstLabel.rfind("entry_", 0) == 0;
    if (!isEntry)
        return Expected<void>{makeError({}, formatFunctionDiag(fn, "first block must be entry"))};

    std::unordered_set<std::string> labels;
    BlockMap blockMap;
    blockMap.reserve(fn.blocks.size());
    for (const auto &bb : fn.blocks) {
        if (!labels.insert(bb.label).second)
            return Expected<void>{
                /// @brief Handles error condition.
                makeError({}, formatFunctionDiag(fn, "duplicate label " + bb.label))};
        // Use string_view key referencing bb.label; the Function outlives this map.
        blockMap.emplace(std::string_view{bb.label}, &bb);
    }

    handlerInfo_.clear();

    std::unordered_map<unsigned, Type> temps;
    std::unordered_map<unsigned, std::string> tempDefinitions;
    auto defineTemp = [&](unsigned id, Type type, std::string description) -> Expected<void> {
        auto [it, inserted] = tempDefinitions.emplace(id, std::move(description));
        if (!inserted) {
            std::ostringstream message;
            message << "duplicate temp %" << id << " defined by " << it->second;
            return Expected<void>{makeError({}, formatFunctionDiag(fn, message.str()))};
        }
        temps[id] = type;
        return {};
    };

    std::unordered_map<unsigned, const Param *> functionParamsById;
    functionParamsById.reserve(fn.params.size());
    for (const auto &param : fn.params) {
        functionParamsById.emplace(param.id, &param);
        if (auto result = defineTemp(param.id, param.type, "function param %" + param.name);
            !result)
            return result;
    }

    auto isEntryFunctionParamAlias = [&](const BasicBlock &bb, const Param &param) {
        if (&bb != &fn.blocks.front())
            return false;
        auto it = functionParamsById.find(param.id);
        if (it == functionParamsById.end())
            return false;
        const Param &functionParam = *it->second;
        return param.type.kind == functionParam.type.kind;
    };

    // ===== PASS 1: Pre-collect all definitions for type information =====
    // This is necessary because SimplifyCFG and other transforms may reorder blocks
    // such that definitions appear later in declaration order but still dominate uses.
    // By collecting all definitions first, we have complete type information for
    // cross-block operand references.
    //
    // We also track which block each definition comes from so that verifyBlock can
    // still detect within-block use-before-def errors.
    std::unordered_map<unsigned, const BasicBlock *> definingBlock;

    for (const auto &bb : fn.blocks) {
        // Block parameters define temporaries
        for (const auto &param : bb.params) {
            if (isEntryFunctionParamAlias(bb, param))
                continue;
            if (auto result = defineTemp(
                    param.id, param.type, "block param %" + param.name + " in ^" + bb.label);
                !result)
                return result;
            definingBlock[param.id] = &bb;
        }

        // Instructions with results define temporaries
        for (const auto &instr : bb.instructions) {
            if (instr.result.has_value()) {
                std::ostringstream description;
                description << "instruction result in ^" << bb.label;
                if (auto result = defineTemp(*instr.result, instr.type, description.str()); !result)
                    return result;
                definingBlock[*instr.result] = &bb;
            }
        }
    }

    // ===== PASS 2: Full verification with complete type info =====
    // Collect EhPush targets and label references during single pass over blocks.
    // This avoids two additional O(blocks × instructions) traversals.
    struct EhPushCheck {
        const BasicBlock *bb;
        const Instr *instr;
        std::string target;
    };

    std::vector<EhPushCheck> ehPushChecks;
    std::vector<std::string> labelRefs;

    for (const auto &bb : fn.blocks) {
        if (auto result = verifyBlock(fn, bb, blockMap, temps, definingBlock, sink); !result)
            return result;

        // Collect EhPush targets and all label references in single pass
        for (const auto &instr : bb.instructions) {
            if (instr.op == Opcode::EhPush && !instr.labels.empty())
                ehPushChecks.push_back({&bb, &instr, instr.labels.front()});

            for (const auto &label : instr.labels)
                labelRefs.push_back(label);
        }
    }

    // Validate EhPush targets exist in handlerInfo_ (populated during verifyBlock)
    for (const auto &check : ehPushChecks) {
        if (handlerInfo_.find(check.target) == handlerInfo_.end()) {
            std::ostringstream message;
            message << "eh.push target ^" << check.target << " must name a handler block";
            return Expected<void>{makeError(
                check.instr->loc, formatInstrDiag(fn, *check.bb, *check.instr, message.str()))};
        }
    }

    // Validate all label references exist
    for (const auto &label : labelRefs) {
        if (!labels.contains(label))
            return Expected<void>{makeError({}, formatFunctionDiag(fn, "unknown label " + label))};
    }

    // ===== PASS 3: Dominance verification =====
    // Compute dominators using iterative dataflow (Cooper-Harvey-Kennedy) and
    // verify that every use of a temp is dominated by its definition.
    {
        const BasicBlock *entry = &fn.blocks.front();

        // Build CFG predecessor map from the block map.
        std::unordered_map<const BasicBlock *, std::vector<const BasicBlock *>> preds;
        for (const auto &blk : fn.blocks) {
            for (const auto &instr : blk.instructions) {
                if (!isTerminator(instr.op))
                    continue;
                for (const auto &label : instr.labels) {
                    if (auto it = blockMap.find(label); it != blockMap.end())
                        preds[it->second].push_back(&blk);
                }
                break;
            }
        }

        // Compute reverse post-order via DFS.
        std::vector<const BasicBlock *> rpo;
        {
            std::unordered_set<const BasicBlock *> visited;

            struct Frame {
                const BasicBlock *bb;
                bool childrenPushed;
            };

            std::vector<Frame> stack;
            stack.push_back({entry, false});
            visited.insert(entry);
            while (!stack.empty()) {
                auto &top = stack.back();
                if (!top.childrenPushed) {
                    top.childrenPushed = true;
                    // Push successors
                    for (const auto &instr : top.bb->instructions) {
                        if (!isTerminator(instr.op))
                            continue;
                        for (const auto &label : instr.labels) {
                            if (auto it = blockMap.find(label); it != blockMap.end()) {
                                if (visited.insert(it->second).second)
                                    stack.push_back({it->second, false});
                            }
                        }
                        break;
                    }
                } else {
                    rpo.push_back(top.bb);
                    stack.pop_back();
                }
            }
            std::reverse(rpo.begin(), rpo.end());
        }

        std::unordered_set<const BasicBlock *> reachable(rpo.begin(), rpo.end());

        // Assign RPO indices for intersection.
        std::unordered_map<const BasicBlock *, unsigned> rpoIndex;
        for (unsigned i = 0; i < rpo.size(); ++i)
            rpoIndex[rpo[i]] = i;

        // Iterative dominator computation.
        std::unordered_map<const BasicBlock *, const BasicBlock *> idom;
        idom[entry] = entry;

        auto intersect = [&](const BasicBlock *b1, const BasicBlock *b2) -> const BasicBlock * {
            auto finger1 = b1;
            auto finger2 = b2;
            while (finger1 != finger2) {
                while (rpoIndex[finger1] > rpoIndex[finger2])
                    finger1 = idom[finger1];
                while (rpoIndex[finger2] > rpoIndex[finger1])
                    finger2 = idom[finger2];
            }
            return finger1;
        };

        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto *bb : rpo) {
                if (bb == entry)
                    continue;
                auto predIt = preds.find(bb);
                if (predIt == preds.end() || predIt->second.empty())
                    continue;

                // Pick first processed predecessor as initial idom.
                const BasicBlock *newIdom = nullptr;
                for (const auto *p : predIt->second) {
                    if (idom.contains(p)) {
                        newIdom = p;
                        break;
                    }
                }
                if (!newIdom)
                    continue;

                // Intersect with remaining processed predecessors.
                for (const auto *p : predIt->second) {
                    if (p == newIdom || !idom.contains(p))
                        continue;
                    newIdom = intersect(p, newIdom);
                }

                if (idom[bb] != newIdom) {
                    idom[bb] = newIdom;
                    changed = true;
                }
            }
        }

        // dominates(A, B): walk B's idom chain up to entry looking for A.
        auto dominates = [&](const BasicBlock *A, const BasicBlock *B) -> bool {
            if (A == B)
                return true;
            const BasicBlock *cur = B;
            while (cur != entry) {
                auto it = idom.find(cur);
                if (it == idom.end() || it->second == cur)
                    return false;
                cur = it->second;
                if (cur == A)
                    return true;
            }
            return A == entry;
        };

        // Check every operand use: the defining block must dominate the using block.
        for (const auto &blk : fn.blocks) {
            if (!reachable.contains(&blk))
                continue;
            for (const auto &instr : blk.instructions) {
                auto checkValue = [&](const Value &op) -> Expected<void> {
                    if (op.kind != Value::Kind::Temp)
                        return {};
                    auto defIt = definingBlock.find(op.id);
                    if (defIt == definingBlock.end())
                        return {};

                    if (!reachable.contains(defIt->second)) {
                        std::ostringstream msg;
                        msg << "use of %" << op.id << " defined in unreachable block ^"
                            << defIt->second->label;
                        return Expected<void>{
                            makeError(instr.loc, formatInstrDiag(fn, blk, instr, msg.str()))};
                    }

                    if (defIt->second != &blk && !dominates(defIt->second, &blk)) {
                        std::ostringstream msg;
                        msg << "use of %" << op.id << " in ^" << blk.label
                            << " not dominated by definition in ^" << defIt->second->label;
                        return Expected<void>{
                            makeError(instr.loc, formatInstrDiag(fn, blk, instr, msg.str()))};
                    }
                    return {};
                };

                for (const auto &op : instr.operands)
                    if (auto result = checkValue(op); !result)
                        return result;
                for (const auto &bundle : instr.brArgs)
                    for (const auto &arg : bundle)
                        if (auto result = checkValue(arg); !result)
                            return result;
            }
        }

        struct ReleaseSite {
            const BasicBlock *block;
            const Instr *instr;
            size_t index;
            unsigned id;
        };
        struct ReleasedUse {
            const BasicBlock *block;
            const Instr *instr;
            size_t index;
            unsigned id;
        };

        std::vector<ReleaseSite> releaseSites;
        std::vector<ReleasedUse> releasedUses;
        for (const auto &blk : fn.blocks) {
            if (!reachable.contains(&blk))
                continue;
            for (size_t index = 0; index < blk.instructions.size(); ++index) {
                const Instr &instr = blk.instructions[index];
                if (isRuntimeArrayRelease(instr)) {
                    if (!instr.operands.empty() && instr.operands[0].kind == Value::Kind::Temp)
                        releaseSites.push_back({&blk, &instr, index, instr.operands[0].id});
                    continue;
                }

                auto recordUse = [&](const Value &value) {
                    if (value.kind == Value::Kind::Temp)
                        releasedUses.push_back({&blk, &instr, index, value.id});
                };
                for (const auto &operand : instr.operands)
                    recordUse(operand);
                for (const auto &bundle : instr.brArgs)
                    for (const auto &argument : bundle)
                        recordUse(argument);
            }
        }

        auto siteDominates = [&](const BasicBlock *releaseBlock,
                                 size_t releaseIndex,
                                 const BasicBlock *useBlock,
                                 size_t useIndex) {
            if (releaseBlock == useBlock)
                return releaseIndex < useIndex;
            return dominates(releaseBlock, useBlock);
        };

        for (size_t i = 0; i < releaseSites.size(); ++i) {
            const ReleaseSite &first = releaseSites[i];
            for (size_t j = 0; j < releaseSites.size(); ++j) {
                if (i == j)
                    continue;
                const ReleaseSite &second = releaseSites[j];
                if (first.id != second.id)
                    continue;
                if (!siteDominates(first.block, first.index, second.block, second.index))
                    continue;

                std::ostringstream message;
                message << "double release of %" << second.id;
                return Expected<void>{makeError(
                    second.instr->loc,
                    formatInstrDiag(fn, *second.block, *second.instr, message.str()))};
            }

            for (const ReleasedUse &use : releasedUses) {
                if (first.id != use.id)
                    continue;
                if (!siteDominates(first.block, first.index, use.block, use.index))
                    continue;

                std::ostringstream message;
                message << "use after release of %" << use.id;
                return Expected<void>{makeError(
                    use.instr->loc, formatInstrDiag(fn, *use.block, *use.instr, message.str()))};
            }
        }
    }

    // ===== PASS 4: Alloca escape verification =====
    // Detect obvious alloca-derived pointer escapes. Returning a stack address
    // can leave dangling addresses after the function returns.
    {
        std::unordered_set<unsigned> stackDerived;
        for (const auto &blk : fn.blocks) {
            for (const auto &instr : blk.instructions) {
                if (instr.op == Opcode::Alloca && instr.result)
                    stackDerived.insert(*instr.result);
            }
        }

        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto &blk : fn.blocks) {
                for (const auto &instr : blk.instructions) {
                    if (instr.op == Opcode::GEP && instr.result && !instr.operands.empty() &&
                        instr.operands[0].kind == Value::Kind::Temp &&
                        stackDerived.contains(instr.operands[0].id)) {
                        changed |= stackDerived.insert(*instr.result).second;
                    }

                    if (isTerminator(instr.op)) {
                        for (size_t edge = 0;
                             edge < instr.labels.size() && edge < instr.brArgs.size();
                             ++edge) {
                            auto targetIt = blockMap.find(instr.labels[edge]);
                            if (targetIt == blockMap.end())
                                continue;
                            const BasicBlock *target = targetIt->second;
                            const auto &args = instr.brArgs[edge];
                            const size_t count = std::min(args.size(), target->params.size());
                            for (size_t idx = 0; idx < count; ++idx) {
                                const Value &arg = args[idx];
                                if (arg.kind == Value::Kind::Temp &&
                                    stackDerived.contains(arg.id)) {
                                    changed |= stackDerived.insert(target->params[idx].id).second;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!stackDerived.empty()) {
            for (const auto &blk : fn.blocks) {
                for (const auto &instr : blk.instructions) {
                    auto failEscape = [&](const Value &op, std::string_view action)
                        -> Expected<void> {
                        if (op.kind != Value::Kind::Temp || !stackDerived.contains(op.id))
                            return {};
                        std::ostringstream msg;
                        msg << action << " alloca-derived pointer %" << op.id;
                        return Expected<void>{
                            makeError(instr.loc, formatInstrDiag(fn, blk, instr, msg.str()))};
                    };

                    if (instr.op == Opcode::Ret) {
                        for (const auto &op : instr.operands)
                            if (auto result = failEscape(op, "returning"); !result)
                                return result;
                    }

                }
            }
        }
    }

    return {};
}

/// @brief Run block-level verification including handler semantics.
///
/// @details Establishes a type inference context seeded with incoming
///          temporaries, validates block parameters, records handler metadata,
///          enforces resume and error accessor placement rules, tracks runtime
///          array releases, dispatches opcode-specific verification, and finally
///          ensures terminators are well-formed. Parameter temporaries are
///          removed from the inference context after the block is processed.
///
/// @param fn Enclosing function definition.
/// @param bb Block under inspection.
/// @param blockMap Mapping from labels to block pointers for CFG lookups.
/// @param temps Table of SSA temporaries and their known types.
/// @param definingBlock Maps temp IDs to their defining blocks for within-block ordering.
/// @param sink Diagnostic sink receiving instruction-level messages.
/// @return Success or a diagnostic describing the failure.
Expected<void> FunctionVerifier::verifyBlock(
    const Function &fn,
    const BasicBlock &bb,
    const BlockMap &blockMap,
    std::unordered_map<unsigned, Type> &temps,
    const std::unordered_map<unsigned, const BasicBlock *> &definingBlock,
    DiagSink &sink) {
    // Initialize defined set with definitions from OTHER blocks.
    // This allows cross-block uses to pass verification even when the defining
    // block appears later in declaration order (which is valid after SimplifyCFG).
    // Within-block definitions are added incrementally to detect within-block
    // use-before-def errors.
    std::unordered_set<unsigned> defined;
    for (const auto &entry : temps) {
        auto it = definingBlock.find(entry.first);
        if (it == definingBlock.end() || it->second != &bb) {
            // Definition is from another block or a function parameter
            defined.insert(entry.first);
        }
    }

    TypeInference types(temps, defined);

    std::vector<unsigned> paramIds;
    if (auto result = validateBlockParams_E(fn, bb, types, paramIds); !result)
        return result;

    std::optional<HandlerSignature> handlerSignature;
    auto handlerCheck = analyzeHandlerBlock(fn, bb);
    if (!handlerCheck)
        return Expected<void>{handlerCheck.error()};
    handlerSignature = handlerCheck.value();
    if (handlerSignature)
        handlerInfo_[bb.label] = *handlerSignature;

    std::unordered_set<unsigned> released;

    for (const auto &instr : bb.instructions) {
        if (auto result = types.ensureOperandsDefined_E(fn, bb, instr); !result)
            return result;

        if (instr.op == Opcode::EhEntry && &instr != &bb.instructions.front())
            return Expected<void>{makeError(
                instr.loc,
                formatInstrDiag(
                    fn, bb, instr, "eh.entry only allowed as first instruction of handler block"))};

        if (isResumeOpcode(instr.op)) {
            if (!handlerSignature)
                return Expected<void>{makeError(
                    instr.loc,
                    formatInstrDiag(fn, bb, instr, "resume.* only allowed in handler block"))};
            if (instr.operands.empty() || instr.operands[0].kind != Value::Kind::Temp ||
                instr.operands[0].id != handlerSignature->resumeTokenParam) {
                return Expected<void>{makeError(
                    instr.loc,
                    formatInstrDiag(fn, bb, instr, "resume.* must use handler %tok parameter"))};
            }
        }

        if (isErrAccessOpcode(instr.op) && !instr.operands.empty()) {
            if (!handlerSignature) {
                return Expected<void>{makeError(
                    instr.loc,
                    formatInstrDiag(fn, bb, instr, "err.get_* only allowed in handler block"))};
            }
        }

        if (isRuntimeArrayRelease(instr)) {
            if (!instr.operands.empty() && instr.operands[0].kind == Value::Kind::Temp) {
                const unsigned id = instr.operands[0].id;
                if (released.contains(id)) {
                    std::ostringstream message;
                    message << "double release of %" << id;
                    return Expected<void>{
                        /// @brief Handles error condition.
                        makeError(instr.loc, formatInstrDiag(fn, bb, instr, message.str()))};
                }
            }
        } else {
            const auto checkValue = [&](const Value &value) -> Expected<void> {
                if (value.kind != Value::Kind::Temp)
                    return Expected<void>{};
                const unsigned id = value.id;
                if (!released.contains(id))
                    return Expected<void>{};
                std::ostringstream message;
                message << "use after release of %" << id;
                return Expected<void>{
                    /// @brief Handles error condition.
                    makeError(instr.loc, formatInstrDiag(fn, bb, instr, message.str()))};
            };

            for (const auto &operand : instr.operands)
                if (auto result = checkValue(operand); !result)
                    return result;

            for (const auto &bundle : instr.brArgs)
                for (const auto &argument : bundle)
                    if (auto result = checkValue(argument); !result)
                        return result;
        }

        if (auto result = verifyInstruction(fn, bb, instr, blockMap, types, sink); !result)
            return result;

        if (isRuntimeArrayRelease(instr) && !instr.operands.empty() &&
            instr.operands[0].kind == Value::Kind::Temp) {
            released.insert(instr.operands[0].id);
        }

        if (isTerminator(instr.op))
            break;
    }

    if (auto result = checkBlockTerminators_E(fn, bb); !result)
        return result;

    // Block params persist in the type map for cross-block use.  The pre-collection
    // pass already registers all definitions so successor blocks can reference them.
    // (Removed per-block removeTemp that prevented valid cross-block references
    //  after inlining.)
    (void)paramIds;

    return {};
}

/// @brief Dispatch verification logic for a single instruction.
///
/// @details Constructs a @ref VerifyCtx populated with the surrounding context,
///          validates operand/result signature contracts, and iterates the
///          registered strategy list until one claims the opcode.  The selected
///          strategy performs opcode-specific checks and returns its result.
///
/// @param fn Function containing the instruction.
/// @param bb Block containing the instruction.
/// @param instr Instruction to verify.
/// @param blockMap Mapping from labels to block pointers for CFG queries.
/// @param types Type inference state for SSA values.
/// @param sink Diagnostic sink receiving verification messages.
/// @return Success or a diagnostic describing the error.
Expected<void> FunctionVerifier::verifyInstruction(const Function &fn,
                                                   const BasicBlock &bb,
                                                   const Instr &instr,
                                                   const BlockMap &blockMap,
                                                   TypeInference &types,
                                                   DiagSink &sink) {
    VerifyCtx ctx{sink, types, externs_, functionMap_, fn, bb, instr};
    if (auto result = verifyOpcodeSignature_E(ctx); !result)
        return result;

    for (const auto &strategy : strategies_) {
        if (!strategy->matches(instr))
            continue;
        return strategy->verify(fn, bb, instr, blockMap, externs_, functionMap_, types, sink);
    }

    return Expected<void>{makeError({}, formatFunctionDiag(fn, "no instruction strategy for op"))};
}

/// @brief Compose a function-scoped diagnostic prefix.
///
/// @details Formats the function name and appends an optional suffix so callers
///          can reuse the string as a consistent diagnostic prefix when no
///          specific instruction location is available.
///
/// @param fn Function associated with the diagnostic.
/// @param message Additional context appended after the name.
/// @return Human-readable string describing the function context.
std::string FunctionVerifier::formatFunctionDiag(const Function &fn,
                                                 std::string_view message) const {
    std::ostringstream oss;
    oss << fn.name;
    if (!message.empty())
        oss << ": " << message;
    return oss.str();
}

} // namespace il::verify
