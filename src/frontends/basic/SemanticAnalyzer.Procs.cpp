// File: src/frontends/basic/SemanticAnalyzer.Procs.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements procedure registration and analysis logic for the BASIC
//          semantic analyzer, covering SUB/FUNCTION bodies and user-defined
//          call validation.
// Key invariants: Procedure scope resets state between declarations; call
//                 validation consults ProcRegistry signatures.
// Ownership/Lifetime: Borrowed DiagnosticEmitter; ProcRegistry managed by the
//                     analyzer instance.
// Links: docs/class-catalog.md

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

#include <algorithm>
#include <utility>

namespace il::frontends::basic
{

using semantic_analyzer_detail::astToSemanticType;

SemanticAnalyzer::ProcedureScope::ProcedureScope(SemanticAnalyzer &analyzer) noexcept
    : analyzer_(analyzer)
{
    previous_ = analyzer_.activeProcScope_;
    analyzer_.activeProcScope_ = this;
    forStackDepth_ = analyzer_.forStack_.size();
    analyzer_.scopes_.pushScope();
}

SemanticAnalyzer::ProcedureScope::~ProcedureScope() noexcept
{
    analyzer_.activeProcScope_ = previous_;
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
    if (analyzer_.forStack_.size() > forStackDepth_)
        analyzer_.forStack_.resize(forStackDepth_);
    analyzer_.scopes_.popScope();
}

void SemanticAnalyzer::ProcedureScope::noteSymbolInserted(const std::string &name)
{
    newSymbols_.push_back(name);
}

void SemanticAnalyzer::ProcedureScope::noteVarTypeMutation(const std::string &name,
                                                           std::optional<Type> previous)
{
    if (!trackedVarTypes_.insert(name).second)
        return;
    varTypeDeltas_.push_back({name, previous});
}

void SemanticAnalyzer::ProcedureScope::noteArrayMutation(const std::string &name,
                                                         std::optional<long long> previous)
{
    if (!trackedArrays_.insert(name).second)
        return;
    arrayDeltas_.push_back({name, previous});
}

void SemanticAnalyzer::ProcedureScope::noteLabelInserted(int label)
{
    newLabels_.push_back(label);
}

void SemanticAnalyzer::ProcedureScope::noteLabelRefInserted(int label)
{
    newLabelRefs_.push_back(label);
}

template <typename Proc, typename BodyCallback>
void SemanticAnalyzer::analyzeProcedureCommon(const Proc &proc, BodyCallback &&bodyCheck)
{
    ProcedureScope procScope(*this);
    for (const auto &p : proc.params)
    {
        scopes_.bind(p.name, p.name);
        auto insertResult = symbols_.insert(p.name);
        if (insertResult.second && activeProcScope_)
            activeProcScope_->noteSymbolInserted(p.name);

        auto itType = varTypes_.find(p.name);
        if (activeProcScope_)
        {
            std::optional<Type> previous;
            if (itType != varTypes_.end())
                previous = itType->second;
            activeProcScope_->noteVarTypeMutation(p.name, previous);
        }
        varTypes_[p.name] = astToSemanticType(p.type);

        if (p.is_array)
        {
            auto itArray = arrays_.find(p.name);
            if (activeProcScope_)
            {
                std::optional<long long> previous;
                if (itArray != arrays_.end())
                    previous = itArray->second;
                activeProcScope_->noteArrayMutation(p.name, previous);
            }
            arrays_[p.name] = -1;
        }
    }
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

void SemanticAnalyzer::analyzeProc(const SubDecl &s)
{
    analyzeProcedureCommon(s, [](const SubDecl &) {});
}

bool SemanticAnalyzer::mustReturn(const std::vector<StmtPtr> &stmts) const
{
    if (stmts.empty())
        return false;
    return mustReturn(*stmts.back());
}

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

void SemanticAnalyzer::analyze(const Program &prog)
{
    symbols_.clear();
    labels_.clear();
    labelRefs_.clear();
    forStack_.clear();
    varTypes_.clear();
    arrays_.clear();
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
}

const ProcSignature *SemanticAnalyzer::resolveCallee(const CallExpr &c)
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
    if (sig->kind == ProcSignature::Kind::Sub)
    {
        std::string msg = "subroutine '" + c.callee + "' used in expression";
        de.emit(il::support::Severity::Error,
                "B2005",
                c.loc,
                static_cast<uint32_t>(c.callee.size()),
                std::move(msg));
        return nullptr;
    }
    return sig;
}

std::vector<SemanticAnalyzer::Type> SemanticAnalyzer::checkCallArgs(const CallExpr &c,
                                                                    const ProcSignature *sig)
{
    std::vector<Type> argTys;
    for (auto &a : c.args)
        argTys.push_back(a ? visitExpr(*a) : Type::Unknown);

    if (!sig)
        return argTys;

    if (argTys.size() != sig->params.size())
    {
        std::string msg = "wrong number of arguments";
        de.emit(il::support::Severity::Error, "B2005", c.loc, 1, std::move(msg));
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

SemanticAnalyzer::Type SemanticAnalyzer::analyzeCall(const CallExpr &c)
{
    const ProcSignature *sig = resolveCallee(c);
    auto argTys [[maybe_unused]] = checkCallArgs(c, sig);
    return inferCallType(c, sig);
}

} // namespace il::frontends::basic
