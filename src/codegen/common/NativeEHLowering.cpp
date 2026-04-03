//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/NativeEHLowering.cpp
// Purpose: Rewrite structured EH into setjmp-backed IL that native backends can
// lower like ordinary control flow.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/NativeEHLowering.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/verify/ControlFlowChecker.hpp"

#include <algorithm>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace viper::codegen::common {
namespace {

using il::core::BasicBlock;
using il::core::Extern;
using il::core::Function;
using il::core::Instr;
using il::core::Module;
using il::core::Opcode;
using il::core::Param;
using il::core::Type;
using il::core::Value;

constexpr const char *kFrameAlloc = "rt_native_eh_frame_alloc";
constexpr const char *kFrameFree = "rt_native_eh_frame_free";
constexpr const char *kFramePush = "rt_native_eh_push";
constexpr const char *kFramePop = "rt_native_eh_pop";
constexpr const char *kFrameSetSite = "rt_native_eh_set_site";
constexpr const char *kFrameGetSite = "rt_native_eh_get_site";
constexpr const char *kSetjmpSymbol = "setjmp";
constexpr int32_t kErrInvalidOperation = 8;

struct PushKey {
    std::size_t blockIndex = 0;
    std::size_t instrIndex = 0;

    bool operator==(const PushKey &other) const noexcept {
        return blockIndex == other.blockIndex && instrIndex == other.instrIndex;
    }
};

struct PushKeyHash {
    std::size_t operator()(const PushKey &key) const noexcept {
        return (key.blockIndex * 1315423911u) ^ key.instrIndex;
    }
};

struct SiteInfo {
    int64_t siteId = 0;
    std::string sameLabel;
    std::string nextLabel;
};

struct ScopeInfo {
    int id = -1;
    std::string handlerLabel;
    unsigned slotTemp = 0;
    std::vector<int> outerStack;
    std::vector<SiteInfo> sites;
};

struct RewrittenFunction {
    std::vector<BasicBlock> blocks;
    bool changed = false;
};

static Type voidTy() {
    return Type(Type::Kind::Void);
}

static Type ptrTy() {
    return Type(Type::Kind::Ptr);
}

static Type i1Ty() {
    return Type(Type::Kind::I1);
}

static Type i32Ty() {
    return Type(Type::Kind::I32);
}

static Type i64Ty() {
    return Type(Type::Kind::I64);
}

static unsigned reserveTemp(Function &fn, const std::string &name) {
    const unsigned id = static_cast<unsigned>(fn.valueNames.size());
    fn.valueNames.push_back(name);
    return id;
}

static bool isEhOpcode(Opcode op) {
    switch (op) {
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

static bool mayTrap(Opcode op) {
    switch (op) {
        case Opcode::Call:
        case Opcode::CallIndirect:
        case Opcode::Trap:
        case Opcode::TrapFromErr:
        case Opcode::IdxChk:
        case Opcode::IAddOvf:
        case Opcode::ISubOvf:
        case Opcode::IMulOvf:
        case Opcode::SDivChk0:
        case Opcode::UDivChk0:
        case Opcode::SRemChk0:
        case Opcode::URemChk0:
        case Opcode::CastSiNarrowChk:
        case Opcode::CastUiNarrowChk:
        case Opcode::CastFpToSiRteChk:
        case Opcode::CastFpToUiRteChk:
            return true;
        default:
            return false;
    }
}

static void ensureExtern(Module &module, std::string name, Type retType, std::vector<Type> params) {
    for (const auto &ext : module.externs) {
        if (ext.name == name)
            return;
    }
    module.externs.push_back(Extern{std::move(name), retType, std::move(params)});
}

static void ensureRuntimeExterns(Module &module) {
    ensureExtern(module, kFrameAlloc, ptrTy(), {});
    ensureExtern(module, kFrameFree, voidTy(), {ptrTy()});
    ensureExtern(module, kFramePush, voidTy(), {ptrTy()});
    ensureExtern(module, kFramePop, voidTy(), {ptrTy()});
    ensureExtern(module, kFrameSetSite, voidTy(), {ptrTy(), i64Ty()});
    ensureExtern(module, kFrameGetSite, i64Ty(), {ptrTy()});
    ensureExtern(module, kSetjmpSymbol, i64Ty(), {ptrTy()});
}

static Instr makeCallVoid(const char *callee, std::vector<Value> operands = {}) {
    Instr instr;
    instr.op = Opcode::Call;
    instr.type = voidTy();
    instr.callee = callee;
    instr.operands = std::move(operands);
    return instr;
}

static Instr makeCallResult(unsigned result, Type type, const char *callee, std::vector<Value> operands = {}) {
    Instr instr;
    instr.result = result;
    instr.op = Opcode::Call;
    instr.type = type;
    instr.callee = callee;
    instr.operands = std::move(operands);
    return instr;
}

static Instr makeLoad(unsigned result, Value ptr) {
    Instr instr;
    instr.result = result;
    instr.op = Opcode::Load;
    instr.type = ptrTy();
    instr.operands.push_back(std::move(ptr));
    return instr;
}

static Instr makeStore(Value ptr, Value value, Type storedType = ptrTy()) {
    Instr instr;
    instr.op = Opcode::Store;
    instr.operands.push_back(std::move(ptr));
    instr.operands.push_back(std::move(value));
    instr.type = storedType;
    return instr;
}

static Instr makeAlloca(unsigned result, int64_t sizeBytes) {
    Instr instr;
    instr.result = result;
    instr.op = Opcode::Alloca;
    instr.type = ptrTy();
    instr.operands.push_back(Value::constInt(sizeBytes));
    return instr;
}

static Instr makeBr(const std::string &label, std::vector<Value> args = {}) {
    Instr instr;
    instr.op = Opcode::Br;
    instr.type = voidTy();
    instr.labels.push_back(label);
    instr.brArgs.push_back(std::move(args));
    return instr;
}

static Instr makeCBr(Value cond, const std::string &trueLabel, const std::string &falseLabel) {
    Instr instr;
    instr.op = Opcode::CBr;
    instr.type = voidTy();
    instr.operands.push_back(std::move(cond));
    instr.labels.push_back(trueLabel);
    instr.labels.push_back(falseLabel);
    instr.brArgs.push_back({});
    instr.brArgs.push_back({});
    return instr;
}

static Instr makeTrapFromErr(int32_t code) {
    Instr instr;
    instr.op = Opcode::TrapFromErr;
    instr.type = i32Ty();
    instr.operands.push_back(Value::constInt(code));
    return instr;
}

static std::vector<std::size_t> normalSuccessors(const Instr &terminator,
                                                 const std::unordered_map<std::string, std::size_t> &blockIndex) {
    std::vector<std::size_t> successors;
    switch (terminator.op) {
        case Opcode::Br:
            if (!terminator.labels.empty()) {
                auto it = blockIndex.find(terminator.labels[0]);
                if (it != blockIndex.end())
                    successors.push_back(it->second);
            }
            break;
        case Opcode::CBr:
        case Opcode::SwitchI32:
            for (const auto &label : terminator.labels) {
                auto it = blockIndex.find(label);
                if (it != blockIndex.end())
                    successors.push_back(it->second);
            }
            break;
        default:
            break;
    }
    return successors;
}

static bool rewriteErrGetterForHandlerToken(const std::unordered_map<std::string, unsigned> &handlerErrParam,
                                            const BasicBlock &block,
                                            Instr &instr) {
    switch (instr.op) {
        case Opcode::ErrGetKind:
        case Opcode::ErrGetCode:
        case Opcode::ErrGetIp:
        case Opcode::ErrGetLine:
        case Opcode::ErrGetMsg:
            break;
        default:
            return false;
    }

    auto it = handlerErrParam.find(block.label);
    if (it == handlerErrParam.end() || instr.operands.empty())
        return false;
    if (instr.operands[0].kind != Value::Kind::Temp || instr.operands[0].id != it->second)
        return false;
    instr.operands.clear();
    return true;
}

static std::vector<int> handlerEntryStackFor(const std::vector<ScopeInfo> &scopes, const std::string &handlerLabel) {
    for (const auto &scope : scopes) {
        if (scope.handlerLabel == handlerLabel)
            return scope.outerStack;
    }
    return {};
}

static RewrittenFunction rewriteFunction(Module &module, Function &fn) {
    RewrittenFunction rewritten{};

    std::unordered_map<std::string, std::size_t> blockIndex;
    for (std::size_t i = 0; i < fn.blocks.size(); ++i)
        blockIndex.emplace(fn.blocks[i].label, i);

    std::vector<ScopeInfo> scopes;
    std::unordered_map<PushKey, int, PushKeyHash> pushToScope;
    std::unordered_map<std::string, std::vector<int>> handlerScopes;
    std::unordered_map<PushKey, SiteInfo, PushKeyHash> siteForInstr;
    std::unordered_map<std::string, std::vector<SiteInfo>> handlerSites;

    bool hasEh = false;
    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi) {
        const auto &bb = fn.blocks[bi];
        for (std::size_t ii = 0; ii < bb.instructions.size(); ++ii) {
            const auto &instr = bb.instructions[ii];
            if (!isEhOpcode(instr.op))
                continue;
            hasEh = true;
            if (instr.op == Opcode::EhPush && !instr.labels.empty()) {
                ScopeInfo scope;
                scope.id = static_cast<int>(scopes.size());
                scope.handlerLabel = instr.labels[0];
                scope.slotTemp = reserveTemp(fn, "__neh.slot." + std::to_string(scope.id));
                pushToScope.emplace(PushKey{bi, ii}, scope.id);
                handlerScopes[scope.handlerLabel].push_back(scope.id);
                scopes.push_back(std::move(scope));
            }
        }
    }
    if (!hasEh)
        return rewritten;

    ensureRuntimeExterns(module);

    std::vector<std::optional<std::vector<int>>> entryStacks(fn.blocks.size());
    entryStacks[0] = std::vector<int>{};
    std::deque<std::size_t> worklist;
    worklist.push_back(0);

    while (!worklist.empty()) {
        const std::size_t bi = worklist.front();
        worklist.pop_front();
        auto state = *entryStacks[bi];

        const auto &bb = fn.blocks[bi];
        for (std::size_t ii = 0; ii < bb.instructions.size(); ++ii) {
            const auto &instr = bb.instructions[ii];
            if (instr.op == Opcode::EhPush) {
                auto it = pushToScope.find(PushKey{bi, ii});
                if (it != pushToScope.end()) {
                    auto &scope = scopes[static_cast<std::size_t>(it->second)];
                    if (scope.outerStack.empty())
                        scope.outerStack = state;
                    state.push_back(it->second);
                }
            } else if (instr.op == Opcode::EhPop) {
                if (!state.empty())
                    state.pop_back();
            }
        }

        if (bb.instructions.empty())
            continue;
        const auto &term = bb.instructions.back();
        for (const std::size_t succ : normalSuccessors(term, blockIndex)) {
            if (!entryStacks[succ].has_value()) {
                entryStacks[succ] = state;
                worklist.push_back(succ);
            }
        }
    }

    const std::string invalidResumeLabel = fn.name + ".__neh.invalid_resume";
    int64_t nextSyntheticSiteId = 1;
    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi) {
        const auto &bb = fn.blocks[bi];
        std::vector<int> active;
        if (entryStacks[bi].has_value()) {
            active = *entryStacks[bi];
        } else {
            active = handlerEntryStackFor(scopes, bb.label);
        }

        for (std::size_t ii = 0; ii < bb.instructions.size(); ++ii) {
            const auto &instr = bb.instructions[ii];
            if (instr.op == Opcode::EhPush) {
                const int scopeId = pushToScope.at(PushKey{bi, ii});
                active.push_back(scopeId);
                continue;
            }
            if (instr.op == Opcode::EhPop) {
                if (!active.empty())
                    active.pop_back();
                continue;
            }
            if (!mayTrap(instr.op) || active.empty())
                continue;

            const int scopeId = active.back();
            auto &scope = scopes[static_cast<std::size_t>(scopeId)];
            const int64_t siteId = nextSyntheticSiteId++;
            const bool isTerm = il::verify::isTerminator(instr.op);
            SiteInfo site;
            site.siteId = siteId;
            site.sameLabel = fn.name + ".__neh.site." + std::to_string(siteId);
            site.nextLabel = isTerm ? invalidResumeLabel
                                    : fn.name + ".__neh.cont." + std::to_string(siteId);
            siteForInstr.emplace(PushKey{bi, ii}, site);
            scope.sites.push_back(site);
            handlerSites[scope.handlerLabel].push_back(site);
        }
    }

    std::unordered_map<std::string, unsigned> handlerErrParam;
    std::unordered_map<std::string, unsigned> handlerSiteParam;
    for (auto &bb : fn.blocks) {
        auto it = handlerScopes.find(bb.label);
        if (it == handlerScopes.end())
            continue;
        if (!bb.params.empty()) {
            bb.params[0].type = ptrTy();
            handlerErrParam.emplace(bb.label, bb.params[0].id);
        }
        if (bb.params.size() > 1) {
            bb.params[1].type = i64Ty();
            handlerSiteParam.emplace(bb.label, bb.params[1].id);
        }
    }

    bool needsInvalidResume = false;
    int synthCounter = 0;

    auto newLabel = [&](const std::string &base) {
        return fn.name + ".__neh." + base + "." + std::to_string(synthCounter++);
    };

    auto appendBlock = [&](BasicBlock &&bb) {
        if (!bb.instructions.empty())
            bb.terminated = il::verify::isTerminator(bb.instructions.back().op);
        rewritten.blocks.push_back(std::move(bb));
    };

    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi) {
        const auto &orig = fn.blocks[bi];
        std::vector<int> active;
        if (entryStacks[bi].has_value()) {
            active = *entryStacks[bi];
        } else {
            active = handlerEntryStackFor(scopes, orig.label);
        }

        BasicBlock current;
        current.label = orig.label;
        current.params = orig.params;
        if (bi == 0) {
            for (const auto &scope : scopes)
                current.instructions.push_back(makeAlloca(scope.slotTemp, 8));
        }

        for (std::size_t ii = 0; ii < orig.instructions.size(); ++ii) {
            Instr instr = orig.instructions[ii];
            rewriteErrGetterForHandlerToken(handlerErrParam, orig, instr);

            if (instr.op == Opcode::EhEntry) {
                rewritten.changed = true;
                continue;
            }

            if (instr.op == Opcode::EhPush) {
                rewritten.changed = true;
                const int scopeId = pushToScope.at(PushKey{bi, ii});
                const auto &scope = scopes[static_cast<std::size_t>(scopeId)];

                const unsigned frameTemp = reserveTemp(fn, "__neh.frame.alloc." + std::to_string(scopeId));
                const unsigned setjmpTemp = reserveTemp(fn, "__neh.sj." + std::to_string(scopeId));
                const unsigned caughtTemp = reserveTemp(fn, "__neh.caught." + std::to_string(scopeId));
                const std::string afterLabel = newLabel("after_push");
                const std::string handlerEntryLabel = newLabel("handler_entry");

                current.instructions.push_back(
                    makeCallResult(frameTemp, ptrTy(), kFrameAlloc, {}));
                current.instructions.push_back(
                    makeStore(Value::temp(scope.slotTemp), Value::temp(frameTemp)));
                current.instructions.push_back(makeCallVoid(kFramePush, {Value::temp(frameTemp)}));
                current.instructions.push_back(
                    makeCallResult(setjmpTemp, i64Ty(), kSetjmpSymbol, {Value::temp(frameTemp)}));
                {
                    Instr cmp;
                    cmp.result = caughtTemp;
                    cmp.op = Opcode::ICmpNe;
                    cmp.type = i1Ty();
                    cmp.operands.push_back(Value::temp(setjmpTemp));
                    cmp.operands.push_back(Value::constInt(0));
                    current.instructions.push_back(std::move(cmp));
                }
                current.instructions.push_back(
                    makeCBr(Value::temp(caughtTemp), handlerEntryLabel, afterLabel));
                current.terminated = true;
                appendBlock(std::move(current));

                BasicBlock handlerEntry;
                handlerEntry.label = handlerEntryLabel;
                const unsigned frameLoad = reserveTemp(fn, "__neh.frame.load." + std::to_string(scopeId));
                const unsigned siteTemp = reserveTemp(fn, "__neh.site." + std::to_string(scopeId));
                handlerEntry.instructions.push_back(makeLoad(frameLoad, Value::temp(scope.slotTemp)));
                handlerEntry.instructions.push_back(
                    makeCallVoid(kFramePop, {Value::temp(frameLoad)}));
                handlerEntry.instructions.push_back(
                    makeCallResult(siteTemp, i64Ty(), kFrameGetSite, {Value::temp(frameLoad)}));
                handlerEntry.instructions.push_back(
                    makeCallVoid(kFrameFree, {Value::temp(frameLoad)}));
                handlerEntry.instructions.push_back(
                    makeStore(Value::temp(scope.slotTemp), Value::null()));

                std::vector<Value> handlerArgs;
                auto hIt = blockIndex.find(scope.handlerLabel);
                if (hIt != blockIndex.end()) {
                    const auto &handlerBlock = fn.blocks[hIt->second];
                    if (!handlerBlock.params.empty())
                        handlerArgs.push_back(Value::null());
                    if (handlerBlock.params.size() > 1)
                        handlerArgs.push_back(Value::temp(siteTemp));
                }
                handlerEntry.instructions.push_back(makeBr(scope.handlerLabel, std::move(handlerArgs)));
                handlerEntry.terminated = true;
                appendBlock(std::move(handlerEntry));

                active.push_back(scopeId);
                current = {};
                current.label = afterLabel;
                continue;
            }

            if (instr.op == Opcode::EhPop) {
                rewritten.changed = true;
                if (!active.empty()) {
                    const int scopeId = active.back();
                    active.pop_back();
                    const auto &scope = scopes[static_cast<std::size_t>(scopeId)];
                    const unsigned frameLoad =
                        reserveTemp(fn, "__neh.pop.frame." + std::to_string(scopeId) + "." +
                                            std::to_string(ii));
                    current.instructions.push_back(makeLoad(frameLoad, Value::temp(scope.slotTemp)));
                    current.instructions.push_back(
                        makeCallVoid(kFramePop, {Value::temp(frameLoad)}));
                    current.instructions.push_back(
                        makeCallVoid(kFrameFree, {Value::temp(frameLoad)}));
                    current.instructions.push_back(
                        makeStore(Value::temp(scope.slotTemp), Value::null()));
                }
                continue;
            }

            if ((instr.op == Opcode::ResumeLabel || instr.op == Opcode::ResumeSame ||
                 instr.op == Opcode::ResumeNext) &&
                handlerScopes.find(orig.label) != handlerScopes.end()) {
                rewritten.changed = true;
                if (instr.op == Opcode::ResumeLabel) {
                    current.instructions.push_back(makeBr(instr.labels.empty() ? invalidResumeLabel
                                                                              : instr.labels[0],
                                                         instr.brArgs.empty() ? std::vector<Value>{}
                                                                             : instr.brArgs[0]));
                    current.terminated = true;
                    appendBlock(std::move(current));
                    current = {};
                } else {
                    const auto tokIt = handlerSiteParam.find(orig.label);
                    needsInvalidResume = true;
                    std::string nextDispatchLabel = invalidResumeLabel;
                    const auto siteListIt = handlerSites.find(orig.label);
                    if (tokIt == handlerSiteParam.end() || siteListIt == handlerSites.end() ||
                        siteListIt->second.empty()) {
                        current.instructions.push_back(makeBr(invalidResumeLabel));
                        current.terminated = true;
                        appendBlock(std::move(current));
                        current = {};
                    } else {
                        const auto &siteList = siteListIt->second;
                        for (std::size_t si = 0; si < siteList.size(); ++si) {
                            const auto &site = siteList[si];
                            const std::string fallback =
                                (si + 1 < siteList.size()) ? newLabel("resume_dispatch") : invalidResumeLabel;
                            BasicBlock dispatch;
                            if (si == 0) {
                                dispatch = std::move(current);
                            } else {
                                dispatch.label = nextDispatchLabel;
                            }
                            const unsigned cmpTemp =
                                reserveTemp(fn, "__neh.resume.cmp." + std::to_string(si));
                            Instr cmp;
                            cmp.result = cmpTemp;
                            cmp.op = Opcode::ICmpEq;
                            cmp.type = i1Ty();
                            cmp.operands.push_back(Value::temp(tokIt->second));
                            cmp.operands.push_back(Value::constInt(site.siteId));
                            dispatch.instructions.push_back(std::move(cmp));
                            dispatch.instructions.push_back(makeCBr(
                                Value::temp(cmpTemp),
                                instr.op == Opcode::ResumeSame ? site.sameLabel : site.nextLabel,
                                fallback));
                            dispatch.terminated = true;
                            appendBlock(std::move(dispatch));
                            nextDispatchLabel = fallback;
                        }
                        current = {};
                    }
                }
                break;
            }

            if (mayTrap(instr.op) && !active.empty()) {
                rewritten.changed = true;
                const int scopeId = active.back();
                auto &scope = scopes[static_cast<std::size_t>(scopeId)];
                const auto &site = siteForInstr.at(PushKey{bi, ii});
                const int64_t siteId = site.siteId;
                const std::string &siteLabel = site.sameLabel;
                const bool isTerm = il::verify::isTerminator(instr.op);
                const std::string &nextLabel = site.nextLabel;

                current.instructions.push_back(makeBr(siteLabel));
                current.terminated = true;
                appendBlock(std::move(current));

                BasicBlock siteBlock;
                siteBlock.label = siteLabel;
                const unsigned frameLoad =
                    reserveTemp(fn, "__neh.site.frame." + std::to_string(siteId));
                siteBlock.instructions.push_back(makeLoad(frameLoad, Value::temp(scope.slotTemp)));
                siteBlock.instructions.push_back(
                    makeCallVoid(kFrameSetSite, {Value::temp(frameLoad), Value::constInt(siteId)}));
                siteBlock.instructions.push_back(std::move(instr));
                if (!isTerm)
                    siteBlock.instructions.push_back(makeBr(nextLabel));
                siteBlock.terminated = true;
                appendBlock(std::move(siteBlock));

                if (isTerm) {
                    current = {};
                    break;
                }
                current = {};
                current.label = nextLabel;
                continue;
            }

            current.instructions.push_back(std::move(instr));
        }

        if (!current.label.empty() && !current.instructions.empty())
            appendBlock(std::move(current));
    }

    if (needsInvalidResume) {
        BasicBlock invalid;
        invalid.label = invalidResumeLabel;
        invalid.instructions.push_back(makeTrapFromErr(kErrInvalidOperation));
        invalid.terminated = true;
        appendBlock(std::move(invalid));
    }

    if (rewritten.changed)
        fn.blocks = rewritten.blocks;
    return rewritten;
}

} // namespace

bool lowerNativeEh(Module &module) {
    bool changed = false;
    for (auto &fn : module.functions) {
        auto rewritten = rewriteFunction(module, fn);
        changed |= rewritten.changed;
    }
    return changed;
}

} // namespace viper::codegen::common
