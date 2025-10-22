//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/SemanticAnalyzer.Procs.cpp
// Purpose: Implement procedure-scoped utilities for the BASIC semantic analyzer.
// Key invariants: Procedure scopes reset state between declarations and consult
//                 the registry to validate calls.
// Ownership/Lifetime: The analyzer borrows diagnostics and owns the registry
//                     that stores discovered procedure signatures.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Semantic analyzer helpers for procedure registration and validation.
/// @details This translation unit defines the `ProcedureScope` guard that
///          manages per-procedure bookkeeping, along with routines for
///          registering parameters, analysing SUB/FUNCTION bodies, and checking
///          call sites against the recorded signatures.

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

#    include "frontends/basic/Semantic_OOP.hpp"

#include <algorithm>
#include <utility>

namespace il::frontends::basic
{

using semantic_analyzer_detail::astToSemanticType;

/// @brief RAII helper that establishes procedure-local analyzer state.
/// @details The guard pushes a new scope, resets error-handling metadata, and
///          captures the stacks that track loops and FOR variables.  When
///          destroyed it restores the previous analyzer state so nested
///          procedures or global analysis continue unaffected.
/// @param analyzer Semantic analyzer whose state is being managed.
SemanticAnalyzer::ProcedureScope::ProcedureScope(SemanticAnalyzer &analyzer) noexcept
    : analyzer_(analyzer)
{
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

/// @brief Tear down the procedure scope and roll back mutations on exit.
/// @details Restores the previous procedure scope pointer, re-enables any
///          active error handler, and rewinds collections such as labels,
///          symbol tables, and variable types to their pre-scope state.  Stack
///          depths and scope bindings are also reset to ensure no leakage
///          occurs between procedures.
SemanticAnalyzer::ProcedureScope::~ProcedureScope() noexcept
{
    analyzer_.activeProcScope_ = previous_;
    analyzer_.errorHandlerActive_ = previousHandlerActive_;
    analyzer_.errorHandlerTarget_ = previousHandlerTarget_;
    for (const auto &label : newLabelRefs_)
        analyzer_.labelRefs_.erase(label);
    for (const auto &label : newLabels_)
        analyzer_.labels_.erase(label);
    for (const auto &name : newSymbols_)
        analyzer_.symbols_.erase(name);
    for (const auto &delta : varTypeDeltas_)
    {
        if (delta.previous)
            analyzer_.varTypes_[delta.name] = *delta.previous;
        else
            analyzer_.varTypes_.erase(delta.name);
    }
    for (const auto &delta : arrayDeltas_)
    {
        if (delta.previous)
            analyzer_.arrays_[delta.name] = *delta.previous;
        else
            analyzer_.arrays_.erase(delta.name);
    }
    for (const auto &delta : channelDeltas_)
    {
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

/// @brief Remember that a new symbol was inserted during the active scope.
/// @details Scope teardown removes tracked symbols from the analyzer-wide set so
///          definitions do not leak beyond the procedure.  Each insertion is
///          recorded exactly once to keep the rollback logic efficient.
/// @param name Canonical symbol inserted into the analyzer state.
void SemanticAnalyzer::ProcedureScope::noteSymbolInserted(const std::string &name)
{
    newSymbols_.push_back(name);
}

/// @brief Track mutations to variable types for later rollback.
/// @details When a procedure updates the inferred type of a variable the
///          previous value must be restored upon exit.  The helper records the
///          prior value (if any) the first time a symbol is mutated within the
///          scope.
/// @param name Variable identifier whose type changed.
/// @param previous Prior semantic type, or empty when the variable was unseen.
void SemanticAnalyzer::ProcedureScope::noteVarTypeMutation(const std::string &name,
                                                           std::optional<Type> previous)
{
    if (!trackedVarTypes_.insert(name).second)
        return;
    varTypeDeltas_.push_back({name, previous});
}

/// @brief Track mutations to array metadata for later rollback.
/// @details Array declarations record dimension metadata that should be scoped
///          to the procedure.  Capturing the previous entry allows the
///          destructor to restore the analyzer's view once the scope ends.
/// @param name Array identifier affected by the mutation.
/// @param previous Previous array descriptor (nullopt when none existed).
void SemanticAnalyzer::ProcedureScope::noteArrayMutation(const std::string &name,
                                                         std::optional<long long> previous)
{
    if (!trackedArrays_.insert(name).second)
        return;
    arrayDeltas_.push_back({name, previous});
}

/// @brief Track open-channel state changes for cleanup at scope exit.
/// @details Procedures may open or close runtime channels.  Recording the prior
///          state allows the analyzer to reinstate the global bookkeeping so
///          other procedures observe the original channel map.
/// @param channel Channel identifier being mutated.
/// @param previouslyOpen Whether the channel was already open before the scope.
void SemanticAnalyzer::ProcedureScope::noteChannelMutation(long long channel, bool previouslyOpen)
{
    if (!trackedChannels_.insert(channel).second)
        return;
    channelDeltas_.push_back({channel, previouslyOpen});
}

/// @brief Record that a new label definition was introduced in this scope.
/// @details Label definitions are scoped to procedure bodies.  Tracking them
///          enables the destructor to remove the labels that were added if the
///          enclosing procedure is rolled back.
/// @param label Numeric label defined within the procedure.
void SemanticAnalyzer::ProcedureScope::noteLabelInserted(int label)
{
    newLabels_.push_back(label);
}

/// @brief Record that a label reference was seen during the scope.
/// @details Label references are tracked separately from definitions.  By
///          recording them the scope can roll back any references that were
///          inserted while analysing the procedure.
/// @param label Numeric label referenced in the procedure.
void SemanticAnalyzer::ProcedureScope::noteLabelRefInserted(int label)
{
    newLabelRefs_.push_back(label);
}

/// @brief Register a procedure parameter and bind it within the current scope.
/// @details Parameters behave like definitions: they are bound in the scope,
///          their inferred types (including array metadata) are recorded, and
///          the canonical symbol tracking is updated so later references resolve
///          correctly.
/// @param param Parameter descriptor from the procedure declaration.
void SemanticAnalyzer::registerProcedureParam(const Param &param)
{
    scopes_.bind(param.name, param.name);

    std::string paramName = param.name;
    Type paramType = param.is_array ? Type::ArrayInt : astToSemanticType(param.type);

    auto itType = varTypes_.find(paramName);
    if (activeProcScope_)
    {
        std::optional<Type> previous;
        if (itType != varTypes_.end())
            previous = itType->second;
        activeProcScope_->noteVarTypeMutation(paramName, previous);
    }
    varTypes_[paramName] = paramType;

    if (param.is_array)
    {
        auto itArray = arrays_.find(paramName);
        if (activeProcScope_)
        {
            std::optional<long long> previous;
            if (itArray != arrays_.end())
                previous = itArray->second;
            activeProcScope_->noteArrayMutation(paramName, previous);
        }
        arrays_[paramName] = -1;
    }

    resolveAndTrackSymbol(paramName, SymbolKind::Definition);
    if (paramName != param.name)
        scopes_.bind(param.name, paramName);
}

/// @brief Shared driver for analysing SUB/FUNCTION bodies.
/// @details Establishes a `ProcedureScope`, registers parameters, tracks any
///          labels that appear in the body, and dispatches statement visitors.
///          A caller-supplied callback performs additional validation such as
///          enforcing returns in functions.
/// @tparam Proc Procedure AST node type (SUB or FUNCTION declaration).
/// @tparam BodyCallback Callable invoked after visiting the body.
/// @param proc Procedure being analysed.
/// @param bodyCheck Callback executed after the body has been walked.
template <typename Proc, typename BodyCallback>
void SemanticAnalyzer::analyzeProcedureCommon(const Proc &proc, BodyCallback &&bodyCheck)
{
    ProcedureScope procScope(*this);
    for (const auto &p : proc.params)
        registerProcedureParam(p);
    for (const auto &st : proc.body)
        if (st)
        {
            auto insertResult = labels_.insert(st->line);
            if (insertResult.second && activeProcScope_)
                activeProcScope_->noteLabelInserted(st->line);
        }
    for (const auto &st : proc.body)
        if (st)
            visitStmt(*st);

    std::forward<BodyCallback>(bodyCheck)(proc);
}

/// @brief Analyse a FUNCTION declaration including return-path validation.
/// @details After the common procedure analysis runs, the callback checks that
///          every control-flow path returns a value.  Missing returns produce a
///          diagnostic anchored at the function's end location when available.
/// @param f FUNCTION declaration node to analyse.
void SemanticAnalyzer::analyzeProc(const FunctionDecl &f)
{
    analyzeProcedureCommon(f, [this](const FunctionDecl &func) {
        if (mustReturn(func.body))
            return;

        std::string msg = "missing return in FUNCTION " + func.name;
        de.emit(il::support::Severity::Error,
                "B1007",
                func.endLoc.isValid() ? func.endLoc : func.loc,
                3,
                std::move(msg));
    });
}

/// @brief Analyse a SUB declaration.
/// @details SUB bodies reuse the shared procedure analysis without any
///          additional checks because they do not return values.
/// @param s SUB declaration node to analyse.
void SemanticAnalyzer::analyzeProc(const SubDecl &s)
{
    analyzeProcedureCommon(s, [](const SubDecl &) {});
}

/// @brief Determine whether a statement list forces a return on all paths.
/// @details Inspects the trailing statement in a block to determine if control
///          flow must return.  Empty lists never mandate a return.
/// @param stmts Statement list to inspect.
/// @return True when execution cannot fall through without returning.
bool SemanticAnalyzer::mustReturn(const std::vector<StmtPtr> &stmts) const
{
    if (stmts.empty())
        return false;
    return mustReturn(*stmts.back());
}

/// @brief Determine whether a single statement enforces a return on all paths.
/// @details Handles nested statement lists, RETURN statements, and IF/ELSE
///          chains.  Loop constructs never force a return because they may not
///          execute.
/// @param s Statement node to inspect.
/// @return True when the statement guarantees a return value.
bool SemanticAnalyzer::mustReturn(const Stmt &s) const
{
    if (auto *lst = dynamic_cast<const StmtList *>(&s))
        return !lst->stmts.empty() && mustReturn(*lst->stmts.back());
    if (auto *ret = dynamic_cast<const ReturnStmt *>(&s))
        return ret->value != nullptr;
    if (auto *ifs = dynamic_cast<const IfStmt *>(&s))
    {
        if (!ifs->then_branch || !mustReturn(*ifs->then_branch))
            return false;
        for (const auto &e : ifs->elseifs)
            if (!e.then_branch || !mustReturn(*e.then_branch))
                return false;
        if (!ifs->else_branch)
            return false;
        return mustReturn(*ifs->else_branch);
    }
    if (dynamic_cast<const WhileStmt *>(&s) || dynamic_cast<const ForStmt *>(&s))
        return false;
    return false;
}

/// @brief Run semantic analysis across an entire BASIC program.
/// @details Resets analyzer state, registers all procedures to gather
///          signatures, analyses each procedure body, and finally visits the
///          program's main block.  OOP metadata is rebuilt at the end so
///          lowering stages observe a consistent index.
/// @param prog Parsed BASIC program AST.
void SemanticAnalyzer::analyze(const Program &prog)
{
    symbols_.clear();
    labels_.clear();
    labelRefs_.clear();
    forStack_.clear();
    loopStack_.clear();
    varTypes_.clear();
    arrays_.clear();
    openChannels_.clear();
    errorHandlerActive_ = false;
    errorHandlerTarget_.reset();
    procReg_.clear();
    scopes_.reset();

    for (const auto &p : prog.procs)
        if (p)
        {
            if (auto *f = dynamic_cast<FunctionDecl *>(p.get()))
                procReg_.registerProc(*f);
            else if (auto *s = dynamic_cast<SubDecl *>(p.get()))
                procReg_.registerProc(*s);
        }
    for (const auto &p : prog.procs)
        if (p)
        {
            if (auto *f = dynamic_cast<FunctionDecl *>(p.get()))
                analyzeProc(*f);
            else if (auto *s = dynamic_cast<SubDecl *>(p.get()))
                analyzeProc(*s);
        }
    for (const auto &stmt : prog.main)
        if (stmt)
            labels_.insert(stmt->line);
    for (const auto &stmt : prog.main)
        if (stmt)
            visitStmt(*stmt);

    static OopIndex g_oopIndex;
    g_oopIndex.classes().clear();
    buildOopIndex(prog, g_oopIndex, &de.emitter());
}

/// @brief Resolve a procedure call to its registered signature, enforcing kind.
/// @details Looks up the callee in the procedure registry and validates that it
///          matches the expected kind (function vs subroutine).  Emits targeted
///          diagnostics when the procedure is unknown or invoked in the wrong
///          context.
/// @param c Call expression being analysed.
/// @param expectedKind Required procedure kind for the context.
/// @return Pointer to the signature when found and compatible; otherwise null.
const ProcSignature *SemanticAnalyzer::resolveCallee(const CallExpr &c,
                                                     ProcSignature::Kind expectedKind)
{
    const ProcSignature *sig = procReg_.lookup(c.callee);
    if (!sig)
    {
        std::string msg = "unknown procedure '" + c.callee + "'";
        de.emit(il::support::Severity::Error,
                "B1006",
                c.loc,
                static_cast<uint32_t>(c.callee.size()),
                std::move(msg));
        return nullptr;
    }
    if (sig->kind != expectedKind)
    {
        if (expectedKind == ProcSignature::Kind::Function)
        {
            std::string msg = "subroutine '" + c.callee +
                              "' used in expression; convert to FUNCTION or call as a statement";
            de.emit(il::support::Severity::Error,
                    "B2005",
                    c.loc,
                    static_cast<uint32_t>(c.callee.size()),
                    std::move(msg));
        }
        else
        {
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

/// @brief Type-check arguments at a call site against a procedure signature.
/// @details Evaluates each argument to determine its semantic type, verifies
///          argument count and ByRef array requirements, and emits diagnostics
///          when mismatches are detected.  Returns the computed argument types
///          so the caller can reason about overload resolution.
/// @param c Call expression supplying the arguments.
/// @param sig Signature resolved for the callee (nullable when lookup failed).
/// @return Semantic types for each argument in the call.
std::vector<SemanticAnalyzer::Type> SemanticAnalyzer::checkCallArgs(const CallExpr &c,
                                                                    const ProcSignature *sig)
{
    std::vector<Type> argTys;
    for (auto &a : c.args)
        argTys.push_back(a ? visitExpr(*a) : Type::Unknown);

    if (!sig)
        return argTys;

    if (c.args.size() != sig->params.size())
    {
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
    for (size_t i = 0; i < n; ++i)
    {
        auto expectTy = sig->params[i].type;
        auto argTy = argTys[i];
        if (sig->params[i].is_array)
        {
            auto *argExpr = c.args[i].get();
            auto *v = dynamic_cast<VarExpr *>(argExpr);
            if (!v || !arrays_.count(v->name))
            {
                il::support::SourceLoc loc = argExpr ? argExpr->loc : c.loc;
                std::string msg = "argument " + std::to_string(i + 1) + " to " + c.callee +
                                  " must be an array variable (ByRef)";
                de.emit(il::support::Severity::Error, "B2006", loc, 1, std::move(msg));
            }
            continue;
        }
        if (expectTy == ::il::frontends::basic::Type::F64 && argTy == Type::Int)
            continue;
        Type want = Type::Int;
        if (expectTy == ::il::frontends::basic::Type::F64)
            want = Type::Float;
        else if (expectTy == ::il::frontends::basic::Type::Str)
            want = Type::String;
        else if (expectTy == ::il::frontends::basic::Type::Bool)
            want = Type::Bool;
        if (argTy != Type::Unknown && argTy != want)
        {
            std::string msg = "argument type mismatch";
            de.emit(il::support::Severity::Error, "B2001", c.loc, 1, std::move(msg));
        }
    }
    return argTys;
}

/// @brief Infer the semantic return type of a call expression.
/// @details Consults the resolved procedure signature and maps the recorded
///          return type into the analyzer's type system.  Unknown signatures
///          yield @ref Type::Unknown so callers can short-circuit.
/// @param c Call expression being analysed (unused when signature is known).
/// @param sig Signature describing the callee.
/// @return Semantic type produced by the call, or Unknown when no type exists.
SemanticAnalyzer::Type SemanticAnalyzer::inferCallType([[maybe_unused]] const CallExpr &c,
                                                       const ProcSignature *sig)
{
    if (!sig || !sig->retType)
        return Type::Unknown;
    if (*sig->retType == ::il::frontends::basic::Type::F64)
        return Type::Float;
    if (*sig->retType == ::il::frontends::basic::Type::Str)
        return Type::String;
    if (*sig->retType == ::il::frontends::basic::Type::Bool)
        return Type::Bool;
    return Type::Int;
}

/// @brief Entry point for analysing a call expression.
/// @details Delegates to the dedicated semantic analysis helper implemented in
///          the `sem` namespace, allowing reuse between expression and
///          statement contexts.
/// @param c Call expression AST node.
/// @return Semantic type produced by the call.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeCall(const CallExpr &c)
{
    return sem::analyzeCallExpr(*this, c);
}

} // namespace il::frontends::basic
