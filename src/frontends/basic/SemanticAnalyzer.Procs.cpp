//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/SemanticAnalyzer.Procs.cpp
// Purpose: Implements procedure registration and analysis logic for the BASIC
//          semantic analyzer, covering SUB/FUNCTION bodies and user-defined
//          call validation.
// Key invariants: Procedure scope resets state between declarations; call
//                 validation consults ProcRegistry signatures.
// Ownership/Lifetime: Borrowed DiagnosticEmitter; ProcRegistry managed by the
//                     analyzer instance.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements procedure-scope management and call validation helpers for the BASIC semantic
/// analyzer.
/// @details The routines in this translation unit manage per-procedure symbol
///          state, register SUB/FUNCTION signatures, and perform signature-based
///          diagnostics for CALL statements.

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

#include "frontends/basic/Semantic_OOP.hpp"

#include <algorithm>
#include <utility>

namespace il::frontends::basic
{

using semantic_analyzer_detail::astToSemanticType;

/// @brief Enter a procedure scope, capturing analyzer state that must be restored.
///
/// @param analyzer Semantic analyzer coordinating symbol and control-flow tracking.
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

/// @brief Restore analyzer state when a procedure scope ends.
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

/// @brief Record that a new symbol was declared within the active procedure scope.
///
/// @param name Canonical symbol name inserted into the symbol table.
void SemanticAnalyzer::ProcedureScope::noteSymbolInserted(const std::string &name)
{
    newSymbols_.push_back(name);
}

/// @brief Track that a variable's inferred type changed within the scope.
///
/// @param name Variable whose type is being updated.
/// @param previous Prior type if one was present, allowing restoration on scope exit.
void SemanticAnalyzer::ProcedureScope::noteVarTypeMutation(const std::string &name,
                                                           std::optional<Type> previous)
{
    if (!trackedVarTypes_.insert(name).second)
        return;
    varTypeDeltas_.push_back({name, previous});
}

/// @brief Track that an array binding changed size or allocation state.
///
/// @param name Array identifier being mutated.
/// @param previous Previous extent when available.
void SemanticAnalyzer::ProcedureScope::noteArrayMutation(const std::string &name,
                                                         std::optional<long long> previous)
{
    if (!trackedArrays_.insert(name).second)
        return;
    arrayDeltas_.push_back({name, previous});
}

/// @brief Record the open/closed state change for a file channel.
///
/// @param channel Channel number being toggled.
/// @param previouslyOpen True when the channel was already open prior to the mutation.
void SemanticAnalyzer::ProcedureScope::noteChannelMutation(long long channel, bool previouslyOpen)
{
    if (!trackedChannels_.insert(channel).second)
        return;
    channelDeltas_.push_back({channel, previouslyOpen});
}

/// @brief Remember that a new label was defined inside the procedure.
///
/// @param label Numeric line label introduced in the body.
void SemanticAnalyzer::ProcedureScope::noteLabelInserted(int label)
{
    newLabels_.push_back(label);
}

/// @brief Track that a label reference was encountered in the procedure body.
///
/// @param label Numeric label referenced by a GOTO/GOSUB.
void SemanticAnalyzer::ProcedureScope::noteLabelRefInserted(int label)
{
    newLabelRefs_.push_back(label);
}

/// @brief Register a procedure parameter and update tracking tables.
///
/// @param param Parameter description sourced from the AST.
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

/// @brief Shared implementation for analyzing SUB/FUNCTION bodies.
///
/// @tparam Proc AST node type modelling the procedure declaration.
/// @tparam BodyCallback Callable invoked after statement analysis for final checks.
/// @param proc Procedure declaration being analyzed.
/// @param bodyCheck Callback that performs procedure-specific validation.
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

/// @brief Analyze a FUNCTION declaration and ensure it produces a return value.
///
/// @param f FUNCTION declaration node.
void SemanticAnalyzer::analyzeProc(const FunctionDecl &f)
{
    analyzeProcedureCommon(f,
                           [this](const FunctionDecl &func)
                           {
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

/// @brief Analyze a SUB declaration.
///
/// @param s SUB declaration node.
void SemanticAnalyzer::analyzeProc(const SubDecl &s)
{
    analyzeProcedureCommon(s, [](const SubDecl &) {});
}

/// @brief Determine whether a block of statements guarantees a return value.
///
/// @param stmts Statement list to evaluate.
/// @return True when the final statement must return.
bool SemanticAnalyzer::mustReturn(const std::vector<StmtPtr> &stmts) const
{
    if (stmts.empty())
        return false;
    return mustReturn(*stmts.back());
}

/// @brief Inspect an individual statement to see if it mandates a return value.
///
/// @param s Statement to inspect.
/// @return True when execution of @p s guarantees a return.
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

/// @brief Run semantic analysis for the entire BASIC program.
///
/// @param prog Program AST containing procedure definitions and main statements.
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
    exprOwners_.clear();

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

/// @brief Resolve the signature for a procedure call and validate its kind.
///
/// @param c Call expression whose callee should be checked.
/// @param expectedKind Whether the caller requires a function or subroutine.
/// @return Pointer to the resolved signature, or nullptr when diagnostics were emitted.
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

/// @brief Validate call arguments against a procedure signature.
///
/// @param c Call expression supplying argument expressions.
/// @param sig Resolved signature for the callee; may be null when unknown.
/// @return Vector of inferred argument types for follow-up analysis.
std::vector<SemanticAnalyzer::Type> SemanticAnalyzer::checkCallArgs(const CallExpr &c,
                                                                    const ProcSignature *sig)
{
    std::vector<Type> argTys;
    for (std::size_t idx = 0; idx < c.args.size(); ++idx)
    {
        const ExprPtr &arg = c.args[idx];
        if (arg)
        {
            auto *slot = const_cast<ExprPtr *>(&c.args[idx]);
            argTys.push_back(visitExpr(*arg, slot));
        }
        else
        {
            argTys.push_back(Type::Unknown);
        }
    }

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

/// @brief Infer the result type produced by a procedure call expression.
///
/// @param c Call expression being analyzed.
/// @param sig Signature describing the callee.
/// @return Semantic type returned by the procedure, or Unknown when unresolved.
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

/// @brief Analyze a call expression using the shared helper implementation.
///
/// @param c Call expression to analyze.
/// @return Inferred result type of the call.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeCall(const CallExpr &c)
{
    return sem::analyzeCallExpr(*this, c);
}

} // namespace il::frontends::basic
