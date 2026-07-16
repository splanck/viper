//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/SemanticAnalyzer_Procs.cpp
// Purpose: Implements procedure registration and analysis logic for the BASIC
//          semantic analyzer, covering SUB/FUNCTION bodies and user-defined
//          call validation.
// Key invariants: Procedure scope resets state between declarations; call
//                 validation consults ProcRegistry signatures.
// Ownership/Lifetime: Borrowed DiagnosticEmitter; ProcRegistry managed by the
//                     analyzer instance.
// Links: docs/internals/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements procedure-scope management and call validation helpers for the BASIC semantic
/// analyzer.
/// @details The routines in this translation unit manage per-procedure symbol
///          state, register SUB/FUNCTION signatures, and perform signature-based
///          diagnostics for CALL statements.

#include "frontends/basic/Diag.hpp"
#include "frontends/basic/IdentifierUtil.hpp"
#include "frontends/basic/LineUtils.hpp"
#include "frontends/basic/SemanticAnalyzer_Internal.hpp"

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/Semantic_OOP.hpp"
#include "frontends/basic/sem/RegistryBuilder.hpp"
#include "frontends/basic/sem/RuntimeMethodIndex.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

#include <algorithm>
#include <utility>

namespace il::frontends::basic {

using semantic_analyzer_detail::astToSemanticType;

/// @brief Enter a procedure scope, capturing analyzer state that must be restored.
///
/// @param analyzer Semantic analyzer coordinating symbol and control-flow tracking.
SemanticAnalyzer::ProcedureScope::ProcedureScope(SemanticAnalyzer &analyzer) noexcept
    : analyzer_(analyzer) {
    previous_ = analyzer_.activeProcScope_;
    analyzer_.activeProcScope_ = this;
    previousHandlerActive_ = analyzer_.errorHandlerActive_;
    previousHandlerTarget_ = analyzer_.errorHandlerTarget_;
    analyzer_.errorHandlerActive_ = false;
    analyzer_.errorHandlerTarget_.reset();
    forStackDepth_ = analyzer_.forStack_.size();
    loopStackDepth_ = analyzer_.loopStack_.size();
    analyzer_.scopes_.pushScope();
}

/// @brief Restore analyzer state when a procedure scope ends.
SemanticAnalyzer::ProcedureScope::~ProcedureScope() noexcept {
    analyzer_.activeProcScope_ = previous_;
    analyzer_.errorHandlerActive_ = previousHandlerActive_;
    analyzer_.errorHandlerTarget_ = previousHandlerTarget_;
    for (const auto &label : newLabelRefs_)
        analyzer_.labelRefs_.erase(label);
    for (const auto &label : newLabels_)
        analyzer_.labels_.erase(label);
    for (const auto &name : newSymbols_)
        analyzer_.symbols_.erase(name);
    for (const auto &delta : varTypeDeltas_) {
        if (delta.previous)
            analyzer_.varTypes_[delta.name] = *delta.previous;
        else
            analyzer_.varTypes_.erase(delta.name);
    }
    for (const auto &delta : objectClassDeltas_) {
        if (delta.previous)
            analyzer_.objectClassTypes_[delta.name] = *delta.previous;
        else
            analyzer_.objectClassTypes_.erase(delta.name);
    }
    for (const auto &delta : arrayDeltas_) {
        if (delta.previous)
            analyzer_.arrays_[delta.name] = *delta.previous;
        else
            analyzer_.arrays_.erase(delta.name);
    }
    for (const auto &delta : channelDeltas_) {
        if (delta.previouslyOpen)
            analyzer_.openChannels_.insert(delta.channel);
        else
            analyzer_.openChannels_.erase(delta.channel);
    }
    if (analyzer_.forStack_.size() > forStackDepth_)
        analyzer_.forStack_.resize(forStackDepth_);
    if (analyzer_.loopStack_.size() > loopStackDepth_)
        analyzer_.loopStack_.resize(loopStackDepth_);
    analyzer_.scopes_.popScope();
}

/// @brief Record that a new symbol was declared within the active procedure scope.
///
/// @param name Canonical symbol name inserted into the symbol table.
void SemanticAnalyzer::ProcedureScope::noteSymbolInserted(const std::string &name) {
    newSymbols_.push_back(name);
}

/// @brief Track that a variable's inferred type changed within the scope.
///
/// @param name Variable whose type is being updated.
/// @param previous Prior type if one was present, allowing restoration on scope exit.
void SemanticAnalyzer::ProcedureScope::noteVarTypeMutation(const std::string &name,
                                                           std::optional<Type> previous) {
    if (!trackedVarTypes_.insert(name).second)
        return;
    varTypeDeltas_.push_back({name, previous});
}

/// @brief Track that a variable's object class binding changed within the scope.
///
/// @param name Variable whose object class is being updated.
/// @param previous Prior class name if one was present.
void SemanticAnalyzer::ProcedureScope::noteObjectClassMutation(
    const std::string &name, std::optional<std::string> previous) {
    if (!trackedObjectClasses_.insert(name).second)
        return;
    objectClassDeltas_.push_back({name, std::move(previous)});
}

/// @brief Track that an array binding changed size or allocation state.
///
/// @param name Array identifier being mutated.
/// @param previous Previous metadata when available.
void SemanticAnalyzer::ProcedureScope::noteArrayMutation(const std::string &name,
                                                         std::optional<ArrayMetadata> previous) {
    if (!trackedArrays_.insert(name).second)
        return;
    arrayDeltas_.push_back({name, previous});
}

/// @brief Record the open/closed state change for a file channel.
///
/// @param channel Channel number being toggled.
/// @param previouslyOpen True when the channel was already open prior to the mutation.
void SemanticAnalyzer::ProcedureScope::noteChannelMutation(long long channel, bool previouslyOpen) {
    if (!trackedChannels_.insert(channel).second)
        return;
    channelDeltas_.push_back({channel, previouslyOpen});
}

/// @brief Remember that a new label was defined inside the procedure.
///
/// @param label Numeric line label introduced in the body.
void SemanticAnalyzer::ProcedureScope::noteLabelInserted(int label) {
    newLabels_.push_back(label);
}

/// @brief Track that a label reference was encountered in the procedure body.
///
/// @param label Numeric label referenced by a GOTO/GOSUB.
void SemanticAnalyzer::ProcedureScope::noteLabelRefInserted(int label) {
    newLabelRefs_.push_back(label);
}

/// @brief Register a procedure parameter and update tracking tables.
///
/// @param param Parameter description sourced from the AST.
void SemanticAnalyzer::registerProcedureParam(const Param &param) {
    scopes_.bind(param.name, param.name);

    std::string paramName = param.name;
    Type paramType = astToSemanticType(param.type);
    if (param.is_array) {
        if (!param.objectClass.empty())
            paramType = Type::ArrayObject;
        else if (param.type == ::il::frontends::basic::Type::Str)
            paramType = Type::ArrayString;
        else
            paramType = Type::ArrayInt;
    }

    auto itType = varTypes_.find(paramName);
    if (activeProcScope_) {
        std::optional<Type> previous;
        if (itType != varTypes_.end())
            previous = itType->second;
        activeProcScope_->noteVarTypeMutation(paramName, previous);
    }
    varTypes_[paramName] = paramType;

    if (param.is_array) {
        auto itArray = arrays_.find(paramName);
        if (activeProcScope_) {
            std::optional<ArrayMetadata> previous;
            if (itArray != arrays_.end())
                previous = itArray->second;
            activeProcScope_->noteArrayMutation(paramName, previous);
        }
        // Array parameters have unknown size
        arrays_[paramName] = ArrayMetadata();
    }

    resolveAndTrackSymbol(paramName, SymbolKind::Definition);
    if (paramName != param.name)
        scopes_.bind(param.name, paramName);
}

/// @brief Shared implementation for analyzing SUB/FUNCTION bodies.
///
/// @tparam Proc AST node type modelling the procedure declaration.
/// @tparam BodyCallback Callable invoked after statement analysis for final checks.
/// @param proc Procedure declaration being analyzed.
/// @param bodyCheck Callback that performs procedure-specific validation.
/// @param loopKind Optional loop kind for EXIT statement validation.
template <typename Proc, typename BodyCallback>
void SemanticAnalyzer::analyzeProcedureCommon(const Proc &proc,
                                              BodyCallback &&bodyCheck,
                                              std::optional<LoopKind> loopKind) {
    ProcedureScope procScope(*this);

    // Push loop context if this is a SUB or FUNCTION (for EXIT SUB/FUNCTION support)
    std::optional<std::pair<LoopKind, size_t>> loopState;
    if (loopKind) {
        loopStack_.push_back(*loopKind);
        loopState = std::make_pair(*loopKind, loopStack_.size() - 1);
    }

    for (const auto &p : proc.params)
        registerProcedureParam(p);
    for (const auto &st : proc.body)
        if (st && hasUserLine(st->line)) {
            auto insertResult = labels_.insert(st->line);
            if (insertResult.second && activeProcScope_)
                activeProcScope_->noteLabelInserted(st->line);
        }
    for (const auto &st : proc.body)
        if (st)
            visitStmt(*st);

    std::forward<BodyCallback>(bodyCheck)(proc);

    // Pop loop context
    if (loopState) {
        loopStack_.pop_back();
    }
}

/// @brief Analyze a FUNCTION declaration and ensure it produces a return value.
///
/// @param f FUNCTION declaration node.
void SemanticAnalyzer::analyzeProc(const FunctionDecl &f) {
    // Preserve current namespace stack and establish the procedure's namespace context
    auto savedNs = nsStack_;
    if (!f.qualifiedName.empty()) {
        // Split qualified name into segments and drop the final identifier (the proc name)
        std::vector<std::string> segs = SplitDots(f.qualifiedName);
        if (!segs.empty())
            segs.pop_back();
        nsStack_ = segs;
    }

    const FunctionDecl *previousFunction = activeFunction_;
    BasicType previousExplicit = activeFunctionExplicitRet_;
    bool previousNameAssigned = activeFunctionNameAssigned_;
    activeFunction_ = &f;
    activeFunctionExplicitRet_ = f.explicitRetType;
    activeFunctionNameAssigned_ = false; // BUG-003: Reset for each function

    if (f.explicitRetType != BasicType::Unknown) {
        if (auto suffix = semantic_analyzer_detail::suffixBasicType(f.name)) {
            if (*suffix != f.explicitRetType) {
                std::string msg = "Conflicting return type: AS ";
                msg += semantic_analyzer_detail::uppercaseBasicTypeName(f.explicitRetType);
                msg += " vs name suffix";
                de.emit(il::support::Severity::Error,
                        "B4006",
                        f.loc,
                        static_cast<uint32_t>(f.name.size()),
                        std::move(msg));
            }
        }
    }

    analyzeProcedureCommon(
        f,
        [this](const FunctionDecl &func) {
            if (mustReturn(func.body))
                return;

            std::string msg = "missing return in FUNCTION " + func.name;
            de.emit(il::support::Severity::Error,
                    "B1007",
                    func.endLoc.isValid() ? func.endLoc : func.loc,
                    3,
                    std::move(msg));
        },
        LoopKind::Function);

    activeFunction_ = previousFunction;
    activeFunctionExplicitRet_ = previousExplicit;
    activeFunctionNameAssigned_ = previousNameAssigned; // BUG-003: Restore state

    // Restore namespace context
    nsStack_ = std::move(savedNs);
}

/// @brief Analyze a SUB declaration.
///
/// @param s SUB declaration node.
void SemanticAnalyzer::analyzeProc(const SubDecl &s) {
    // Preserve current namespace stack and establish the procedure's namespace context
    auto savedNs = nsStack_;
    if (!s.qualifiedName.empty()) {
        std::vector<std::string> segs = SplitDots(s.qualifiedName);
        if (!segs.empty())
            segs.pop_back();
        nsStack_ = segs;
    }

    analyzeProcedureCommon(s, [](const SubDecl &) {}, LoopKind::Sub);

    // Restore namespace context
    nsStack_ = std::move(savedNs);
}

namespace {

struct ReturnFlow {
    bool alwaysReturns{false};
    bool assignedAfter{false};
};

bool assignsActiveFunctionName(const Stmt &stmt, const FunctionDecl *activeFunction) {
    if (!activeFunction)
        return false;
    const auto *let = as<const LetStmt>(stmt);
    if (!let || !let->target)
        return false;
    const auto *target = as<const VarExpr>(*let->target);
    return target && string_utils::iequals(target->name, activeFunction->name);
}

ReturnFlow flowForStmt(const Stmt &stmt, const FunctionDecl *activeFunction, bool assignedBefore);

ReturnFlow flowForStmtList(const std::vector<StmtPtr> &stmts,
                           const FunctionDecl *activeFunction,
                           bool assignedBefore) {
    bool assigned = assignedBefore;
    for (const auto &stmt : stmts) {
        if (!stmt)
            continue;
        ReturnFlow flow = flowForStmt(*stmt, activeFunction, assigned);
        if (flow.alwaysReturns)
            return flow;
        assigned = flow.assignedAfter;
    }
    return {false, assigned};
}

ReturnFlow flowForBranchStmt(const StmtPtr &stmt,
                             const FunctionDecl *activeFunction,
                             bool assignedBefore) {
    if (!stmt)
        return {false, assignedBefore};
    return flowForStmt(*stmt, activeFunction, assignedBefore);
}

ReturnFlow flowForIf(const IfStmt &stmt, const FunctionDecl *activeFunction, bool assignedBefore) {
    std::vector<ReturnFlow> branches;
    branches.push_back(flowForBranchStmt(stmt.then_branch, activeFunction, assignedBefore));
    for (const auto &elseif : stmt.elseifs)
        branches.push_back(flowForBranchStmt(elseif.then_branch, activeFunction, assignedBefore));

    const bool hasElse = stmt.else_branch != nullptr;
    if (hasElse)
        branches.push_back(flowForBranchStmt(stmt.else_branch, activeFunction, assignedBefore));

    bool allReturn = hasElse;
    bool assignedAfter = hasElse ? true : assignedBefore;
    for (const ReturnFlow &branch : branches) {
        allReturn = allReturn && branch.alwaysReturns;
        assignedAfter = assignedAfter && (branch.alwaysReturns || branch.assignedAfter);
    }
    return {allReturn, assignedAfter};
}

ReturnFlow flowForSelect(const SelectCaseStmt &stmt,
                         const FunctionDecl *activeFunction,
                         bool assignedBefore) {
    bool allReturn = !stmt.elseBody.empty();
    bool assignedAfter = stmt.elseBody.empty() ? assignedBefore : true;

    for (const auto &arm : stmt.arms) {
        ReturnFlow armFlow = flowForStmtList(arm.body, activeFunction, assignedBefore);
        allReturn = allReturn && armFlow.alwaysReturns;
        assignedAfter = assignedAfter && (armFlow.alwaysReturns || armFlow.assignedAfter);
    }

    if (!stmt.elseBody.empty()) {
        ReturnFlow elseFlow = flowForStmtList(stmt.elseBody, activeFunction, assignedBefore);
        allReturn = allReturn && elseFlow.alwaysReturns;
        assignedAfter = assignedAfter && (elseFlow.alwaysReturns || elseFlow.assignedAfter);
    }

    return {allReturn, assignedAfter};
}

ReturnFlow flowForTry(const TryCatchStmt &stmt,
                      const FunctionDecl *activeFunction,
                      bool assignedBefore) {
    ReturnFlow tryFlow = flowForStmtList(stmt.tryBody, activeFunction, assignedBefore);
    ReturnFlow catchFlow = stmt.catchBody.empty()
                               ? ReturnFlow{false, assignedBefore}
                               : flowForStmtList(stmt.catchBody, activeFunction, assignedBefore);

    bool normalReturn = tryFlow.alwaysReturns;
    if (!stmt.catchBody.empty())
        normalReturn = normalReturn && catchFlow.alwaysReturns;

    bool assignedBeforeFinally =
        (tryFlow.alwaysReturns || tryFlow.assignedAfter) &&
        (stmt.catchBody.empty() || catchFlow.alwaysReturns || catchFlow.assignedAfter);
    ReturnFlow finallyFlow =
        flowForStmtList(stmt.finallyBody, activeFunction, assignedBeforeFinally);
    if (finallyFlow.alwaysReturns)
        return finallyFlow;
    return {normalReturn, finallyFlow.assignedAfter};
}

ReturnFlow flowForStmt(const Stmt &stmt, const FunctionDecl *activeFunction, bool assignedBefore) {
    if (const auto *lst = as<const StmtList>(stmt))
        return flowForStmtList(lst->stmts, activeFunction, assignedBefore);
    if (const auto *ret = as<const ReturnStmt>(stmt))
        return {ret->value != nullptr || assignedBefore, assignedBefore};
    if (const auto *exitStmt = as<const ExitStmt>(stmt)) {
        if (exitStmt->kind == ExitStmt::LoopKind::Function)
            return {assignedBefore, assignedBefore};
        return {false, assignedBefore};
    }
    if (assignsActiveFunctionName(stmt, activeFunction))
        return {false, true};
    if (const auto *ifs = as<const IfStmt>(stmt))
        return flowForIf(*ifs, activeFunction, assignedBefore);
    if (const auto *select = as<const SelectCaseStmt>(stmt))
        return flowForSelect(*select, activeFunction, assignedBefore);
    if (const auto *tryCatch = as<const TryCatchStmt>(stmt))
        return flowForTry(*tryCatch, activeFunction, assignedBefore);
    if (is<WhileStmt>(stmt) || is<ForStmt>(stmt) || is<DoStmt>(stmt) || is<ForEachStmt>(stmt))
        return {false, assignedBefore};
    return {false, assignedBefore};
}

bool containsGosub(const Stmt &stmt);

bool containsGosubList(const std::vector<StmtPtr> &stmts) {
    for (const auto &stmt : stmts)
        if (stmt && containsGosub(*stmt))
            return true;
    return false;
}

bool containsGosub(const Stmt &stmt) {
    if (is<GosubStmt>(stmt))
        return true;
    if (const auto *lst = as<const StmtList>(stmt))
        return containsGosubList(lst->stmts);
    if (const auto *ifs = as<const IfStmt>(stmt)) {
        if (ifs->then_branch && containsGosub(*ifs->then_branch))
            return true;
        for (const auto &elseif : ifs->elseifs)
            if (elseif.then_branch && containsGosub(*elseif.then_branch))
                return true;
        return ifs->else_branch && containsGosub(*ifs->else_branch);
    }
    if (const auto *select = as<const SelectCaseStmt>(stmt)) {
        for (const auto &arm : select->arms)
            if (containsGosubList(arm.body))
                return true;
        return containsGosubList(select->elseBody);
    }
    if (const auto *tryCatch = as<const TryCatchStmt>(stmt)) {
        return containsGosubList(tryCatch->tryBody) || containsGosubList(tryCatch->catchBody) ||
               containsGosubList(tryCatch->finallyBody);
    }
    if (const auto *whileStmt = as<const WhileStmt>(stmt))
        return containsGosubList(whileStmt->body);
    if (const auto *doStmt = as<const DoStmt>(stmt))
        return containsGosubList(doStmt->body);
    if (const auto *forStmt = as<const ForStmt>(stmt))
        return containsGosubList(forStmt->body);
    if (const auto *forEach = as<const ForEachStmt>(stmt))
        return containsGosubList(forEach->body);
    if (const auto *ns = as<const NamespaceDecl>(stmt))
        return containsGosubList(ns->body);
    return false;
}

} // namespace

/// @brief Determine whether a block of statements guarantees a return value.
///
/// @param stmts Statement list to evaluate.
/// @return True when every control path produces a function value.
bool SemanticAnalyzer::mustReturn(const std::vector<StmtPtr> &stmts) const {
    ReturnFlow flow = flowForStmtList(stmts, activeFunction_, false);
    return flow.alwaysReturns || flow.assignedAfter;
}

/// @brief Inspect an individual statement to see if it mandates a return value.
///
/// @param s Statement to inspect.
/// @return True when execution of @p s guarantees a return.
bool SemanticAnalyzer::mustReturn(const Stmt &s) const {
    ReturnFlow flow = flowForStmt(s, activeFunction_, false);
    return flow.alwaysReturns || flow.assignedAfter;
}

bool SemanticAnalyzer::mainHasGosub() const noexcept {
    return mainHasGosub_;
}

/// @brief Run semantic analysis for the entire BASIC program.
///
/// @param prog Program AST containing procedure definitions and main statements.
void SemanticAnalyzer::analyze(const Program &prog) {
    symbols_.clear();
    labels_.clear();
    labelRefs_.clear();
    forStack_.clear();
    loopStack_.clear();
    varTypes_.clear();
    objectClassTypes_.clear();
    arrays_.clear();
    openChannels_.clear();
    errorHandlerActive_ = false;
    errorHandlerTarget_.reset();
    mainHasGosub_ = false;
    activeClassQName_.clear();
    activeMemberHasMe_ = false;
    procReg_.clear();
    scopes_.reset();
    sawDecl_ = false;
    usingStack_.clear();
    usingStack_.push_back(UsingScope{}); // root scope

    // Register procedure signatures first (needed for call resolution)
    for (const auto &p : prog.procs)
        if (p) {
            if (auto *f = as<FunctionDecl>(*p))
                procReg_.registerProc(*f);
            else if (auto *s = as<SubDecl>(*p))
                procReg_.registerProc(*s);
        }

    // Also register procedures declared inside namespace blocks using their
    // fully-qualified names (assigned by CollectProcedures).
    std::function<void(const std::vector<StmtPtr> &)> scan;
    scan = [&](const std::vector<StmtPtr> &stmts) {
        for (const auto &stmtPtr : stmts) {
            if (!stmtPtr)
                continue;
            switch (stmtPtr->stmtKind()) {
                case Stmt::Kind::NamespaceDecl: {
                    const auto &ns = static_cast<const NamespaceDecl &>(*stmtPtr);
                    scan(ns.body);
                    break;
                }
                case Stmt::Kind::FunctionDecl:
                    procReg_.registerProc(static_cast<const FunctionDecl &>(*stmtPtr));
                    break;
                case Stmt::Kind::SubDecl:
                    procReg_.registerProc(static_cast<const SubDecl &>(*stmtPtr));
                    break;
                default:
                    break;
            }
        }
    };
    scan(prog.main);

    oopIndex_.clear();
    buildOopIndex(prog, oopIndex_, &de.emitter());

    // Build namespace registry and USING context.
    buildNamespaceRegistry(prog, ns_, usings_, &de.emitter());

    // Seed root USING scope from file-scoped UsingContext (declaration order preserved).
    if (!usings_.imports().empty()) {
        UsingScope &root = usingStack_.front();
        for (const auto &imp : usings_.imports()) {
            // Seed only non-alias imports here; alias entries are validated and
            // applied by analyzeUsingDecl during statement analysis (so shadowing
            // checks run correctly).
            if (imp.alias.empty()) {
                std::string nsCanon = CanonicalizeQualified(SplitDots(imp.ns));
                root.imports.insert(std::move(nsCanon));
            }
        }
    }

    // Construct TypeResolver after declare pass completes.
    resolver_ = std::make_unique<TypeResolver>(ns_, usings_);

    // Pre-collect all labels (line numbers) from main block for GOTO/GOSUB validation.
    // This must happen before analysis since GOTO can jump forward.
    for (const auto &stmt : prog.main)
        if (stmt && hasUserLine(stmt->line))
            labels_.insert(stmt->line);
    mainHasGosub_ = containsGosubList(prog.main);

    // Analyze main module body so module-level variables are registered
    // in symbols_ before procedures are analyzed. This allows procedures to
    // reference module-level globals without explicit parameters.
    for (const auto &stmt : prog.main)
        if (stmt)
            visitStmt(*stmt);

    // Now analyze procedure bodies - they can reference module-level symbols
    for (const auto &p : prog.procs)
        if (p) {
            if (auto *f = as<FunctionDecl>(*p))
                analyzeProc(*f);
            else if (auto *s = as<SubDecl>(*p))
                analyzeProc(*s);
        }

    // Additionally analyze procedures declared inside namespace blocks.
    std::function<void(const std::vector<StmtPtr> &)> scanBodies;
    scanBodies = [&](const std::vector<StmtPtr> &stmts) {
        for (const auto &stmtPtr : stmts) {
            if (!stmtPtr)
                continue;
            switch (stmtPtr->stmtKind()) {
                case Stmt::Kind::NamespaceDecl: {
                    const auto &ns = static_cast<const NamespaceDecl &>(*stmtPtr);
                    // Establish namespace context
                    for (const auto &seg : ns.path)
                        nsStack_.push_back(seg);
                    // Establish USING scope for this namespace and apply any
                    // USING directives declared inside the block before
                    // analyzing procedure bodies.
                    UsingScope childScope;
                    if (!usingStack_.empty()) {
                        // Inherit imports from parent scope; aliases are local.
                        childScope.imports = usingStack_.back().imports;
                    }
                    usingStack_.push_back(std::move(childScope));
                    // First pass: apply USINGs declared in this namespace.
                    for (const auto &s : ns.body) {
                        if (s && s->stmtKind() == Stmt::Kind::UsingDecl)
                            analyzeUsingDecl(static_cast<UsingDecl &>(*s));
                    }
                    scanBodies(ns.body);
                    if (nsStack_.size() >= ns.path.size())
                        nsStack_.resize(nsStack_.size() - ns.path.size());
                    else
                        nsStack_.clear();
                    if (!usingStack_.empty())
                        usingStack_.pop_back();
                    break;
                }
                case Stmt::Kind::FunctionDecl:
                    analyzeProc(static_cast<const FunctionDecl &>(*stmtPtr));
                    break;
                case Stmt::Kind::SubDecl:
                    analyzeProc(static_cast<const SubDecl &>(*stmtPtr));
                    break;
                default:
                    break;
            }
        }
    };
    scanBodies(prog.main);
}

/// @brief Resolve the signature for a procedure call and validate its kind.
///
/// @param c Call expression whose callee should be checked.
/// @param expectedKind Whether the caller requires a function or subroutine.
/// @return Pointer to the resolved signature, or nullptr when diagnostics were emitted.
const ProcSignature *SemanticAnalyzer::resolveCallee(const CallExpr &c,
                                                     ProcSignature::Kind expectedKind) {
    auto stripSuffix = [](std::string_view name) -> std::string_view {
        if (name.empty())
            return name;
        char last = name.back();
        switch (last) {
            case '$':
            case '#':
            case '!':
            case '&':
            case '%':
                return name.substr(0, name.size() - 1);
            default:
                return name;
        }
    };

    // Build candidate list.
    std::vector<std::string> attempts;
    const ProcSignature *sig = nullptr;

    if (!c.calleeQualified.empty()) {
        // Prepare raw segments with last-suffix stripped, then expand alias if present.
        std::vector<std::string> segs = c.calleeQualified;
        if (!segs.empty()) {
            std::string &last = segs.back();
            if (!last.empty()) {
                char t = last.back();
                if (t == '$' || t == '#' || t == '!' || t == '&' || t == '%')
                    last.pop_back();
            }
        }

        // Alias expansion: if first segment matches an alias, replace it with the target path.
        bool usedAlias = false;
        std::string usedAliasName;
        std::string usedAliasTarget;
        if (!segs.empty()) {
            std::string firstCanon = CanonicalizeIdent(segs[0]);
            // 1) Prefer the innermost scoped USING aliases (namespace-scoped or file-scoped).
            if (!usingStack_.empty()) {
                const auto &aliases = usingStack_.back().aliases;
                auto itAlias = aliases.find(firstCanon);
                if (itAlias != aliases.end()) {
                    std::vector<std::string> expanded = SplitDots(itAlias->second);
                    expanded.insert(expanded.end(), segs.begin() + 1, segs.end());
                    segs = std::move(expanded);
                    usedAlias = true;
                    usedAliasName = firstCanon;
                    usedAliasTarget = itAlias->second;
                }
            }
            // 2) If not found in the scoped USINGs, consult file-scoped UsingContext aliases.
            if (!usedAlias && usings_.hasAlias(firstCanon)) {
                std::string target = usings_.resolveAlias(firstCanon);
                if (!target.empty()) {
                    std::vector<std::string> expanded = SplitDots(target);
                    expanded.insert(expanded.end(), segs.begin() + 1, segs.end());
                    segs = std::move(expanded);
                    usedAlias = true;
                    usedAliasName = firstCanon;
                    usedAliasTarget = std::move(target);
                }
            }
        }

        // Canonicalize
        std::vector<std::string> canonSegs;
        canonSegs.reserve(segs.size());
        for (const auto &seg : segs) {
            std::string cseg = CanonicalizeIdent(seg);
            if (cseg.empty() && !seg.empty()) {
                canonSegs.clear();
                break;
            }
            canonSegs.emplace_back(std::move(cseg));
        }
        std::string q;
        if (!canonSegs.empty())
            q = JoinQualified(canonSegs);
        if (q.empty()) {
            q = c.callee; // fallback to original text when canonicalization fails
        }
        attempts.push_back(q);
        sig = procReg_.lookup(q);
        if (!sig && segs.size() >= 2) {
            // Runtime class-method alias fallback: classes may expose methods that
            // alias functions registered under a different canonical name (e.g.
            // Viper.Data.Json.GetStr aliases Viper.Collections.Map.GetStr). Resolve
            // the class surface and rewrite the call to the alias target so the
            // rest of the pipeline (argument checks, lowering) sees the real
            // function, keeping BASIC's callable surface equal to Zia's.
            std::vector<std::string> classSegs(segs.begin(), segs.end() - 1);
            const std::string classQ = JoinDots(classSegs);
            if (il::runtime::findRuntimeClassByQName(classQ)) {
                if (auto alias = runtimeMethodIndex().find(classQ, segs.back(), c.args.size())) {
                    if (const auto *aliasSig = procReg_.lookup(alias->target)) {
                        auto &mutableCall = const_cast<CallExpr &>(c);
                        mutableCall.calleeQualified = SplitDots(alias->target);
                        mutableCall.callee = alias->target;
                        sig = aliasSig;
                    }
                }
            }
        }
        if (!sig && usedAlias) {
            // Unknown after alias expansion: show canonical and note alias mapping.
            diagx::ErrorUnknownProcQualified(de.emitter(), c.loc, q);
            diagx::NoteAliasExpansion(de.emitter(), usedAliasName, usedAliasTarget);
            return nullptr;
        }
    } else {
        // Unqualified resolution: parent-walk, then USING imports.
        std::vector<std::string> prefixCanon;
        prefixCanon.reserve(nsStack_.size());
        for (const auto &seg : nsStack_) {
            std::string canon = CanonicalizeIdent(seg);
            prefixCanon.push_back(canon.empty() ? seg : canon);
        }
        std::string ident = std::string{stripSuffix(c.callee)};
        ident = CanonicalizeIdent(ident);

        // Parent-walk precedence: choose the nearest match only. Do not
        // accumulate multiple hits along the chain; if multiple levels define
        // the same name, the nearest wins deterministically.
        for (std::size_t n = prefixCanon.size(); n > 0 && !sig; --n) {
            std::vector<std::string> parts(prefixCanon.begin(),
                                           prefixCanon.begin() + static_cast<std::ptrdiff_t>(n));
            parts.push_back(ident);
            std::string q = JoinQualified(parts);
            attempts.push_back(q);
            if (const auto *s = procReg_.lookup(q)) {
                sig = s;
            }
        }
        if (!sig) {
            // Try global unqualified ident.
            attempts.push_back(ident);
            sig = procReg_.lookup(ident);
        }

        if (!sig) {
            // No parent/global hit: try imported namespaces (USING imports only, no aliases).
            std::vector<std::string> importHits;
            if (!usingStack_.empty()) {
                const UsingScope &cur = usingStack_.back();
                for (const auto &ns : cur.imports) {
                    std::string q = ns;
                    if (!q.empty())
                        q.push_back('.');
                    q += ident;
                    attempts.push_back(q);
                    if (procReg_.lookup(q)) {
                        // Build a display name preserving namespace casing and, when possible,
                        // the original function spelling. This keeps diagnostics stable and
                        // user-friendly.
                        std::string displayNs = ns;
                        if (const auto *info = ns_.info(ns))
                            displayNs = info->full;

                        std::string display = displayNs + "." + ident;
                        // Attempt to find an original-cased key in the proc table that
                        // canonicalizes to the same qualified name.
                        const auto &procs = procReg_.procs();
                        int bestScore = -1; // -1: none, 0: lowercase fallback, 1: mixed-case, 2:
                                            // preferred ns + mixed-case
                        for (const auto &kv : procs) {
                            const std::string &key = kv.first;
                            // Canonicalize key and compare against q (already canonicalized
                            // ns+ident).
                            std::vector<std::string> parts = SplitDots(key);
                            std::string keyCanon = CanonicalizeQualified(parts);
                            if (keyCanon == q) {
                                // Score this candidate: prefer matching namespace + mixed-case key.
                                bool hasUpper = false;
                                for (char ch : key)
                                    if (std::isupper(static_cast<unsigned char>(ch))) {
                                        hasUpper = true;
                                        break;
                                    }
                                int score = hasUpper ? 1 : 0;
                                // Check namespace prefix equality when available.
                                auto lastDot = key.rfind('.');
                                std::string keyNs = lastDot != std::string::npos
                                                        ? key.substr(0, lastDot)
                                                        : std::string{};
                                if (hasUpper && keyNs == displayNs)
                                    score = 2;
                                if (score > bestScore) {
                                    bestScore = score;
                                    display = key;
                                    if (bestScore == 2)
                                        break; // optimal
                                }
                            }
                        }
                        importHits.push_back(std::move(display));
                    }
                }
            }
            if (importHits.size() == 1) {
                sig = procReg_.lookup(importHits[0]);
            } else if (importHits.size() > 1) {
                // Remap matches to display case using namespace registry for the prefix
                // and the original typed callee for the suffix.
                std::vector<std::string> displayHits;
                displayHits.reserve(importHits.size());
                auto titleCaseNs = [](const std::string &ns) {
                    std::string out;
                    out.reserve(ns.size());
                    bool start = true;
                    for (char ch : ns) {
                        if (ch == '.') {
                            out.push_back('.');
                            start = true;
                        } else {
                            if (start)
                                out.push_back(static_cast<char>(
                                    std::toupper(static_cast<unsigned char>(ch))));
                            else
                                out.push_back(static_cast<char>(
                                    std::tolower(static_cast<unsigned char>(ch))));
                            start = false;
                        }
                    }
                    return out;
                };
                for (const auto &qcanon : importHits) {
                    std::size_t dot = qcanon.rfind('.');
                    std::string nsPart =
                        dot == std::string::npos ? std::string{} : qcanon.substr(0, dot);
                    std::string displayNs = nsPart;
                    if (const auto *info = ns_.info(nsPart))
                        displayNs = info->full;
                    // Normalise namespace display to TitleCase for readability.
                    std::string nsTitle = titleCaseNs(displayNs);
                    std::string display =
                        nsTitle.empty() ? std::string(c.callee) : nsTitle + "." + c.callee;
                    displayHits.push_back(std::move(display));
                }
                diagx::ErrorAmbiguousProc(de.emitter(), c.loc, c.callee, displayHits);
                return nullptr;
            }
        }
    }
    if (!sig) {
        if (!c.calleeQualified.empty()) {
            std::string q = CanonicalizeQualified(c.calleeQualified);
            diagx::ErrorUnknownProcQualified(de.emitter(), c.loc, q.empty() ? c.callee : q);
        } else {
            diagx::ErrorUnknownProcWithTries(de.emitter(), c.loc, c.callee, attempts);
        }
        return nullptr;
    }
    if (sig->kind != expectedKind) {
        // Allow calling functions as statements by accepting FUNCTION where SUB is expected.
        if (expectedKind == ProcSignature::Kind::Sub &&
            sig->kind == ProcSignature::Kind::Function) {
            return sig;
        }
        if (expectedKind == ProcSignature::Kind::Function) {
            std::string msg = "subroutine '" + c.callee +
                              "' used in expression; convert to FUNCTION or call as a statement";
            de.emit(il::support::Severity::Error,
                    "B2005",
                    c.loc,
                    static_cast<uint32_t>(c.callee.size()),
                    std::move(msg));
        } else {
            std::string msg = "function '" + c.callee + "' cannot be called as a statement";
            de.emit(il::support::Severity::Error,
                    "B2015",
                    c.loc,
                    static_cast<uint32_t>(c.callee.size()),
                    std::move(msg));
        }
        return nullptr;
    }
    return sig;
}

/// @brief Validate call arguments against a procedure signature.
///
/// @param c Call expression supplying argument expressions.
/// @param sig Resolved signature for the callee; may be null when unknown.
/// @return Vector of inferred argument types for follow-up analysis.
std::vector<SemanticAnalyzer::Type> SemanticAnalyzer::checkCallArgs(const CallExpr &c,
                                                                    const ProcSignature *sig) {
    std::vector<Type> argTys;
    for (auto &a : c.args)
        argTys.push_back(a ? visitExpr(*a) : Type::Unknown);

    if (!sig)
        return argTys;

    if (c.args.size() != sig->params.size()) {
        std::string msg = "argument count mismatch for '" + c.callee + "': expected " +
                          std::to_string(sig->params.size()) + ", got " +
                          std::to_string(c.args.size());
        de.emit(il::support::Severity::Error,
                "B2008",
                c.loc,
                static_cast<uint32_t>(c.callee.size()),
                std::move(msg));
        return argTys;
    }

    size_t n = std::min(argTys.size(), sig->params.size());
    for (size_t i = 0; i < n; ++i) {
        auto expectTy = sig->params[i].type;
        auto argTy = argTys[i];
        if (sig->params[i].is_array) {
            auto *argExpr = c.args[i].get();
            auto *v = argExpr ? as<VarExpr>(*argExpr) : nullptr;
            if (!v || !arrays_.contains(v->name)) {
                il::support::SourceLoc loc = argExpr ? argExpr->loc : c.loc;
                std::string msg = "argument " + std::to_string(i + 1) + " to " + c.callee +
                                  " must be an array variable (ByRef)";
                de.emit(il::support::Severity::Error, "B2006", loc, 1, std::move(msg));
            }
            continue;
        }
        // Object parameters of runtime builtins are seeded as I64 (the AST type
        // enum cannot express objects); reject primitives explicitly so an i64
        // never reaches IL verification as an object-pointer operand. A literal
        // 0 is permitted as the null-object idiom (e.g. Thread.Start payloads).
        if (sig->isRuntimeBuiltin && i < sig->objectParams.size() && sig->objectParams[i]) {
            if (argTy == Type::Int || argTy == Type::Float || argTy == Type::Bool) {
                const auto *lit = c.args[i] ? as<const IntExpr>(*c.args[i]) : nullptr;
                if (!(argTy == Type::Int && lit && lit->value == 0)) {
                    std::string msg = "argument " + std::to_string(i + 1) + " to " + c.callee +
                                      " expects an OBJECT; box primitives with Viper.Core.Box";
                    de.emit(il::support::Severity::Error, "B2001", c.loc, 1, std::move(msg));
                }
            }
            continue; // Object/String/Unknown arguments are acceptable object payloads.
        }
        if (expectTy == ::il::frontends::basic::Type::F64 && argTy == Type::Int)
            continue;
        // Float arguments can be implicitly converted to integer parameters (truncation)
        if (expectTy == ::il::frontends::basic::Type::I64 && argTy == Type::Float)
            continue;
        // Integer/float arguments can be truncated to boolean parameters
        if (expectTy == ::il::frontends::basic::Type::Bool &&
            (argTy == Type::Int || argTy == Type::Float))
            continue;
        Type want = Type::Int;
        if (expectTy == ::il::frontends::basic::Type::F64)
            want = Type::Float;
        else if (expectTy == ::il::frontends::basic::Type::Str)
            want = Type::String;
        else if (expectTy == ::il::frontends::basic::Type::Bool)
            want = Type::Bool;
        // Object-typed arguments can match I64 parameters (objects are pointers)
        if (argTy == Type::Object && expectTy == ::il::frontends::basic::Type::I64)
            continue;
        // Relax type checking for selected runtime helpers that operate on opaque pointers.
        // Permit pointer-typed values to match integer-typed parameters for
        // Viper.Text.StringBuilder.* canonical helpers.
        bool relaxPtrArg = false;
        if (!c.calleeQualified.empty()) {
            // Build canonical lowercase name
            std::string qcanon = CanonicalizeQualified(c.calleeQualified);
            if (qcanon.rfind("viper.text.stringbuilder.", 0) == 0) {
                relaxPtrArg = true;
            }
        }
        if (argTy != Type::Unknown && argTy != want) {
            if (!(relaxPtrArg && want == Type::Int)) {
                std::string msg = "argument type mismatch";
                de.emit(il::support::Severity::Error, "B2001", c.loc, 1, std::move(msg));
            }
        }
    }

    if (sig->isRuntimeBuiltin) {
        std::string display = sig->runtimeTarget;
        if (display.empty())
            display = !c.calleeQualified.empty() ? JoinQualified(c.calleeQualified) : c.callee;
        checkRuntimePointerSafety(sig->runtimeTarget,
                                  sig->rawPointerReturn,
                                  sig->rawPointerParams,
                                  c.args,
                                  c.loc,
                                  display);
    }
    return argTys;
}

/// @brief Infer the result type produced by a procedure call expression.
///
/// @param c Call expression being analyzed.
/// @param sig Signature describing the callee.
/// @return Semantic type returned by the procedure, or Unknown when unresolved.
SemanticAnalyzer::Type SemanticAnalyzer::inferCallType([[maybe_unused]] const CallExpr &c,
                                                       const ProcSignature *sig) {
    if (!sig || !sig->retType)
        return Type::Unknown;
    if (sig->objectReturn)
        return Type::Object;
    if (*sig->retType == ::il::frontends::basic::Type::F64)
        return Type::Float;
    if (*sig->retType == ::il::frontends::basic::Type::Str)
        return Type::String;
    if (*sig->retType == ::il::frontends::basic::Type::Bool)
        return Type::Bool;
    return Type::Int;
}

/// @brief Analyze a call expression using the shared helper implementation.
///
/// @param c Call expression to analyze.
/// @return Inferred result type of the call.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeCall(const CallExpr &c) {
    return sem::analyzeCallExpr(*this, c);
}

} // namespace il::frontends::basic
