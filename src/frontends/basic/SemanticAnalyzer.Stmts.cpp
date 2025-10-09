// File: src/frontends/basic/SemanticAnalyzer.Stmts.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements statement-level analysis for the BASIC semantic analyzer,
//          covering symbol tracking, control-flow validation, and declaration
//          handling.
// Key invariants: Statement visitors propagate scope information and emit
//                 diagnostics for invalid constructs.
// Ownership/Lifetime: Analyzer borrows DiagnosticEmitter; AST nodes remain
//                     owned externally.
// Links: docs/codemap.md

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

namespace il::frontends::basic
{

using semantic_analyzer_detail::astToSemanticType;
using semantic_analyzer_detail::conditionExprText;
using semantic_analyzer_detail::semanticTypeName;

class SemanticAnalyzerStmtVisitor final : public MutStmtVisitor
{
  public:
    explicit SemanticAnalyzerStmtVisitor(SemanticAnalyzer &analyzer) noexcept
        : analyzer_(analyzer)
    {
    }

    void visit(LabelStmt &) override {}
    void visit(PrintStmt &stmt) override { analyzer_.analyzePrint(stmt); }
    void visit(PrintChStmt &stmt) override { analyzer_.analyzePrintCh(stmt); }
    void visit(CallStmt &stmt) override { analyzer_.analyzeCallStmt(stmt); }
    void visit(ClsStmt &stmt) override { analyzer_.analyzeCls(stmt); }
    void visit(ColorStmt &stmt) override { analyzer_.analyzeColor(stmt); }
    void visit(LocateStmt &stmt) override { analyzer_.analyzeLocate(stmt); }
    void visit(LetStmt &stmt) override { analyzer_.analyzeLet(stmt); }
    void visit(DimStmt &stmt) override { analyzer_.analyzeDim(stmt); }
    void visit(ReDimStmt &stmt) override { analyzer_.analyzeReDim(stmt); }
    void visit(RandomizeStmt &stmt) override { analyzer_.analyzeRandomize(stmt); }
    void visit(IfStmt &stmt) override { analyzer_.analyzeIf(stmt); }
    void visit(WhileStmt &stmt) override { analyzer_.analyzeWhile(stmt); }
    void visit(DoStmt &stmt) override { analyzer_.analyzeDo(stmt); }
    void visit(ForStmt &stmt) override { analyzer_.analyzeFor(stmt); }
    void visit(NextStmt &stmt) override { analyzer_.analyzeNext(stmt); }
    void visit(ExitStmt &stmt) override { analyzer_.analyzeExit(stmt); }
    void visit(GotoStmt &stmt) override { analyzer_.analyzeGoto(stmt); }
    void visit(GosubStmt &stmt) override { analyzer_.analyzeGosub(stmt); }
    void visit(OpenStmt &stmt) override { analyzer_.analyzeOpen(stmt); }
    void visit(CloseStmt &stmt) override { analyzer_.analyzeClose(stmt); }
    void visit(OnErrorGoto &stmt) override { analyzer_.analyzeOnErrorGoto(stmt); }
    void visit(EndStmt &stmt) override { analyzer_.analyzeEnd(stmt); }
    void visit(InputStmt &stmt) override { analyzer_.analyzeInput(stmt); }
    void visit(LineInputChStmt &stmt) override { analyzer_.analyzeLineInputCh(stmt); }
    void visit(Resume &stmt) override { analyzer_.analyzeResume(stmt); }
    void visit(ReturnStmt &stmt) override { analyzer_.analyzeReturn(stmt); }
    void visit(FunctionDecl &) override {}
    void visit(SubDecl &) override {}
    void visit(StmtList &stmt) override { analyzer_.analyzeStmtList(stmt); }

  private:
    SemanticAnalyzer &analyzer_;
};

void SemanticAnalyzer::visitStmt(Stmt &s)
{
    SemanticAnalyzerStmtVisitor visitor(*this);
    s.accept(visitor);
}

void SemanticAnalyzer::analyzeStmtList(const StmtList &lst)
{
    for (const auto &st : lst.stmts)
        if (st)
            visitStmt(*st);
}

void SemanticAnalyzer::analyzePrint(const PrintStmt &p)
{
    for (const auto &it : p.items)
        if (it.kind == PrintItem::Kind::Expr && it.expr)
            visitExpr(*it.expr);
}

void SemanticAnalyzer::analyzePrintCh(const PrintChStmt &p)
{
    if (p.channelExpr)
        visitExpr(*p.channelExpr);
    for (const auto &arg : p.args)
        if (arg)
            visitExpr(*arg);
}

void SemanticAnalyzer::analyzeCallStmt(CallStmt &stmt)
{
    if (!stmt.call)
        return;
    const ProcSignature *sig = resolveCallee(*stmt.call, ProcSignature::Kind::Sub);
    checkCallArgs(*stmt.call, sig);
}

void SemanticAnalyzer::visit(const ClsStmt &s)
{
    (void)s; // nothing to check
}

void SemanticAnalyzer::visit(const ColorStmt &s)
{
    requireNumeric(*s.fg, "COLOR foreground must be numeric");
    if (s.bg)
        requireNumeric(*s.bg, "COLOR background must be numeric");
}

void SemanticAnalyzer::visit(const LocateStmt &s)
{
    requireNumeric(*s.row, "LOCATE row must be numeric");
    if (s.col)
        requireNumeric(*s.col, "LOCATE column must be numeric");
}

void SemanticAnalyzer::analyzeCls(const ClsStmt &stmt)
{
    visit(stmt);
}

void SemanticAnalyzer::analyzeColor(const ColorStmt &stmt)
{
    visit(stmt);
}

void SemanticAnalyzer::analyzeLocate(const LocateStmt &stmt)
{
    visit(stmt);
}

void SemanticAnalyzer::requireNumeric(Expr &expr, std::string_view message)
{
    Type exprType = visitExpr(expr);
    if (exprType == Type::Unknown || exprType == Type::Int || exprType == Type::Float)
        return;

    std::string msg(message);
    msg += ", got ";
    msg += semanticTypeName(exprType);
    msg += '.';

    de.emit(il::support::Severity::Error, "B2001", expr.loc, 1, std::move(msg));
}

void SemanticAnalyzer::analyzeVarAssignment(VarExpr &v, const LetStmt &l)
{
    resolveAndTrackSymbol(v.name, SymbolKind::Definition);
    bool loopVarMutation = false;
    for (auto it = forStack_.rbegin(); it != forStack_.rend(); ++it)
    {
        if (*it == v.name)
        {
            loopVarMutation = true;
            break;
        }
    }
    if (loopVarMutation)
    {
        std::string msg = "cannot assign to loop variable '" + v.name + "' inside FOR";
        de.emit(il::support::Severity::Error,
                "B1010",
                l.loc,
                static_cast<uint32_t>(v.name.size()),
                std::move(msg));
    }
    Type varTy = Type::Int;
    if (auto itType = varTypes_.find(v.name); itType != varTypes_.end())
        varTy = itType->second;
    Type exprTy = Type::Unknown;
    if (l.expr)
        exprTy = visitExpr(*l.expr);

    if (varTy == Type::ArrayInt)
    {
        if (exprTy != Type::Unknown && exprTy != Type::ArrayInt)
        {
            std::string msg = "cannot assign scalar to array variable";
            de.emit(il::support::Severity::Error, "B2001", l.loc, 1, std::move(msg));
        }
        return;
    }

    if (!l.expr)
        return;

    if (exprTy == Type::ArrayInt)
    {
        std::string msg = "cannot assign array value to scalar variable";
        de.emit(il::support::Severity::Error, "B2001", l.loc, 1, std::move(msg));
    }
    else if (varTy == Type::Int && exprTy == Type::Float)
    {
        bool allowFloatPromotion = false;
        if (l.expr)
        {
            if (const auto *bin = dynamic_cast<const BinaryExpr *>(l.expr.get()))
            {
                const bool hasExplicitIntSuffix =
                    !v.name.empty() && (v.name.back() == '%' || v.name.back() == '&');
                switch (bin->op)
                {
                    case BinaryExpr::Op::Div:
                    case BinaryExpr::Op::Add:
                    case BinaryExpr::Op::Sub:
                    case BinaryExpr::Op::Mul:
                        allowFloatPromotion = !hasExplicitIntSuffix;
                        break;
                    default:
                        break;
                }
            }
        }

        if (allowFloatPromotion)
        {
            if (activeProcScope_)
            {
                std::optional<Type> previous = varTy;
                activeProcScope_->noteVarTypeMutation(v.name, previous);
            }
            varTypes_[v.name] = Type::Float;
        }
        else
        {
            markImplicitConversion(*l.expr, Type::Int);
            std::string msg = "narrowing conversion from FLOAT to INT in assignment";
            de.emit(il::support::Severity::Warning, "B2002", l.loc, 1, std::move(msg));
        }
    }
    else if (varTy == Type::String && exprTy != Type::Unknown && exprTy != Type::String)
    {
        std::string msg = "operand type mismatch";
        de.emit(il::support::Severity::Error, "B2001", l.loc, 1, std::move(msg));
    }
    else if (varTy == Type::Bool && exprTy != Type::Unknown && exprTy != Type::Bool)
    {
        std::string msg = "operand type mismatch";
        de.emit(il::support::Severity::Error, "B2001", l.loc, 1, std::move(msg));
    }
}

void SemanticAnalyzer::analyzeArrayAssignment(ArrayExpr &a, const LetStmt &l)
{
    resolveAndTrackSymbol(a.name, SymbolKind::Reference);
    if (!arrays_.count(a.name))
    {
        std::string msg = "unknown array '" + a.name + "'";
        de.emit(il::support::Severity::Error,
                "B1001",
                a.loc,
                static_cast<uint32_t>(a.name.size()),
                std::move(msg));
    }
    if (auto itType = varTypes_.find(a.name);
        itType != varTypes_.end() && itType->second != Type::ArrayInt)
    {
        std::string msg = "variable '" + a.name + "' is not an array";
        de.emit(il::support::Severity::Error,
                "B2001",
                a.loc,
                static_cast<uint32_t>(a.name.size()),
                std::move(msg));
    }
    auto indexTy = visitExpr(*a.index);
    if (indexTy != Type::Unknown && indexTy != Type::Int)
    {
        std::string msg = "index type mismatch";
        de.emit(il::support::Severity::Error, "B2001", a.loc, 1, std::move(msg));
    }
    Type valueTy = Type::Unknown;
    if (l.expr)
    {
        valueTy = visitExpr(*l.expr);
        if (valueTy != Type::Unknown && valueTy != Type::Int)
        {
            std::string msg = "array element type mismatch";
            de.emit(il::support::Severity::Error, "B2001", l.loc, 1, std::move(msg));
        }
    }
    auto it = arrays_.find(a.name);
    if (it != arrays_.end() && it->second >= 0)
    {
        if (auto *ci = dynamic_cast<const IntExpr *>(a.index.get()))
        {
            if (ci->value < 0 || ci->value >= it->second)
            {
                std::string msg = "index out of bounds";
                de.emit(il::support::Severity::Warning, "B3001", a.loc, 1, std::move(msg));
            }
        }
    }
}

void SemanticAnalyzer::analyzeConstExpr(const LetStmt &l)
{
    if (l.target)
        visitExpr(*l.target);
    if (l.expr)
        visitExpr(*l.expr);
    std::string msg = "left-hand side of LET must be a variable or array element";
    de.emit(il::support::Severity::Error, "B2007", l.loc, 1, std::move(msg));
}

void SemanticAnalyzer::analyzeLet(LetStmt &l)
{
    if (!l.target)
        return;
    if (auto *v = dynamic_cast<VarExpr *>(l.target.get()))
    {
        analyzeVarAssignment(*v, l);
    }
    else if (auto *a = dynamic_cast<ArrayExpr *>(l.target.get()))
    {
        analyzeArrayAssignment(*a, l);
    }
    else
    {
        analyzeConstExpr(l);
    }
}

void SemanticAnalyzer::analyzeOpen(OpenStmt &stmt)
{
    const bool modeValid = stmt.mode == OpenStmt::Mode::Input || stmt.mode == OpenStmt::Mode::Output ||
                           stmt.mode == OpenStmt::Mode::Append || stmt.mode == OpenStmt::Mode::Binary ||
                           stmt.mode == OpenStmt::Mode::Random;
    if (!modeValid)
    {
        std::string msg = "invalid OPEN mode";
        de.emit(il::support::Severity::Error, "B4001", stmt.loc, 4, std::move(msg));
    }

    if (stmt.pathExpr)
    {
        Type pathTy = visitExpr(*stmt.pathExpr);
        if (pathTy != Type::Unknown && pathTy != Type::String)
        {
            std::string msg = "OPEN path expression must be STRING, got ";
            msg += semanticTypeName(pathTy);
            msg += '.';
            de.emit(il::support::Severity::Error, "B2001", stmt.pathExpr->loc, 1, std::move(msg));
        }
    }

    if (stmt.channelExpr)
    {
        Type channelTy = visitExpr(*stmt.channelExpr);
        if (channelTy != Type::Unknown && channelTy != Type::Int)
        {
            std::string msg = "OPEN channel expression must be INTEGER, got ";
            msg += semanticTypeName(channelTy);
            msg += '.';
            de.emit(il::support::Severity::Error, "B2001", stmt.channelExpr->loc, 1, std::move(msg));
        }
        else if (auto *intExpr = dynamic_cast<IntExpr *>(stmt.channelExpr.get()))
        {
            long long channel = intExpr->value;
            bool wasOpen = openChannels_.count(channel) > 0;
            if (wasOpen)
            {
                std::string msg = "channel #";
                msg += std::to_string(channel);
                msg += " is already open";
                de.emit(il::support::Severity::Warning,
                        "B3002",
                        stmt.channelExpr->loc,
                        1,
                        std::move(msg));
            }
            else
            {
                if (activeProcScope_)
                    activeProcScope_->noteChannelMutation(channel, false);
                openChannels_.insert(channel);
            }
        }
    }
}

void SemanticAnalyzer::analyzeClose(CloseStmt &stmt)
{
    if (!stmt.channelExpr)
        return;

    Type channelTy = visitExpr(*stmt.channelExpr);
    if (channelTy != Type::Unknown && channelTy != Type::Int)
    {
        std::string msg = "CLOSE channel expression must be INTEGER, got ";
        msg += semanticTypeName(channelTy);
        msg += '.';
        de.emit(il::support::Severity::Error, "B2001", stmt.channelExpr->loc, 1, std::move(msg));
        return;
    }

    if (auto *intExpr = dynamic_cast<IntExpr *>(stmt.channelExpr.get()))
    {
        long long channel = intExpr->value;
        if (openChannels_.count(channel))
        {
            if (activeProcScope_)
                activeProcScope_->noteChannelMutation(channel, true);
            openChannels_.erase(channel);
        }
    }
}

void SemanticAnalyzer::checkConditionExpr(Expr &expr)
{
    Type condTy = visitExpr(expr);
    if (condTy == Type::Unknown || condTy == Type::Bool)
        return;

    if (condTy == Type::Int)
    {
        if (auto *intExpr = dynamic_cast<const IntExpr *>(&expr))
        {
            if (intExpr->value == 0 || intExpr->value == 1)
                return;
        }
    }

    std::string exprText = conditionExprText(expr);
    if (exprText.empty())
        exprText = "<expr>";

    de.emitNonBooleanCondition(std::string(DiagNonBooleanCondition),
                               expr.loc,
                               1,
                               semanticTypeName(condTy),
                               exprText);
}

void SemanticAnalyzer::analyzeIf(const IfStmt &i)
{
    if (i.cond)
        checkConditionExpr(*i.cond);
    auto analyzeBranch = [&](const StmtPtr &branch)
    {
        if (!branch)
            return;
        ScopeTracker::ScopedScope scope(scopes_);
        if (const auto *list = dynamic_cast<const StmtList *>(branch.get()))
        {
            for (const auto &child : list->stmts)
                if (child)
                    visitStmt(*child);
        }
        else
        {
            visitStmt(*branch);
        }
    };
    analyzeBranch(i.then_branch);
    for (const auto &e : i.elseifs)
    {
        if (e.cond)
            checkConditionExpr(*e.cond);
        analyzeBranch(e.then_branch);
    }
    analyzeBranch(i.else_branch);
}

void SemanticAnalyzer::analyzeWhile(const WhileStmt &w)
{
    if (w.cond)
        checkConditionExpr(*w.cond);
    loopStack_.push_back(LoopKind::While);
    {
        ScopeTracker::ScopedScope scope(scopes_);
        for (const auto &bs : w.body)
            if (bs)
                visitStmt(*bs);
    }
    loopStack_.pop_back();
}

void SemanticAnalyzer::analyzeDo(const DoStmt &d)
{
    auto checkCond = [&]() {
        if (d.cond)
            checkConditionExpr(*d.cond);
    };

    if (d.testPos == DoStmt::TestPos::Pre)
        checkCond();

    loopStack_.push_back(LoopKind::Do);
    {
        ScopeTracker::ScopedScope scope(scopes_);
        for (const auto &bs : d.body)
            if (bs)
                visitStmt(*bs);
    }
    loopStack_.pop_back();

    if (d.testPos == DoStmt::TestPos::Post)
        checkCond();
}

void SemanticAnalyzer::analyzeFor(ForStmt &f)
{
    resolveAndTrackSymbol(f.var, SymbolKind::Definition);
    if (f.start)
        visitExpr(*f.start);
    if (f.end)
        visitExpr(*f.end);
    if (f.step)
        visitExpr(*f.step);
    forStack_.push_back(f.var);
    loopStack_.push_back(LoopKind::For);
    {
        ScopeTracker::ScopedScope scope(scopes_);
        for (const auto &bs : f.body)
            if (bs)
                visitStmt(*bs);
    }
    loopStack_.pop_back();
    forStack_.pop_back();
}

void SemanticAnalyzer::analyzeGoto(const GotoStmt &g)
{
    auto insertResult = labelRefs_.insert(g.target);
    if (insertResult.second && activeProcScope_)
        activeProcScope_->noteLabelRefInserted(g.target);
    if (!labels_.count(g.target))
    {
        std::string msg = "unknown line " + std::to_string(g.target);
        de.emit(il::support::Severity::Error, "B1003", g.loc, 4, std::move(msg));
    }
}

void SemanticAnalyzer::analyzeGosub(const GosubStmt &stmt)
{
    auto insertResult = labelRefs_.insert(stmt.targetLine);
    if (insertResult.second && activeProcScope_)
        activeProcScope_->noteLabelRefInserted(stmt.targetLine);
    if (!labels_.count(stmt.targetLine))
    {
        std::string msg = "unknown line " + std::to_string(stmt.targetLine);
        de.emit(il::support::Severity::Error, "B1003", stmt.loc, 5, std::move(msg));
    }
}

void SemanticAnalyzer::analyzeOnErrorGoto(const OnErrorGoto &stmt)
{
    if (stmt.toZero)
    {
        clearErrorHandler();
        return;
    }
    auto insertResult = labelRefs_.insert(stmt.target);
    if (insertResult.second && activeProcScope_)
        activeProcScope_->noteLabelRefInserted(stmt.target);
    if (!labels_.count(stmt.target))
    {
        std::string msg = "unknown line " + std::to_string(stmt.target);
        de.emit(il::support::Severity::Error, "B1003", stmt.loc, 4, std::move(msg));
    }
    installErrorHandler(stmt.target);
}

void SemanticAnalyzer::analyzeNext(const NextStmt &n)
{
    if (forStack_.empty() || (!n.var.empty() && n.var != forStack_.back()))
    {
        std::string msg = "mismatched NEXT";
        if (!n.var.empty())
            msg += " '" + n.var + "'";
        if (!forStack_.empty())
            msg += ", expected '" + forStack_.back() + "'";
        else
            msg += ", no active FOR";
        de.emit(il::support::Severity::Error, "B1002", n.loc, 4, std::move(msg));
    }
    else
    {
        forStack_.pop_back();
    }
}

void SemanticAnalyzer::analyzeExit(const ExitStmt &stmt)
{
    const auto toLoopKind = [](ExitStmt::LoopKind kind) {
        switch (kind)
        {
            case ExitStmt::LoopKind::For:
                return LoopKind::For;
            case ExitStmt::LoopKind::While:
                return LoopKind::While;
            case ExitStmt::LoopKind::Do:
                return LoopKind::Do;
        }
        return LoopKind::While;
    };
    const auto loopKindName = [](LoopKind kind) {
        switch (kind)
        {
            case LoopKind::For:
                return "FOR";
            case LoopKind::While:
                return "WHILE";
            case LoopKind::Do:
                return "DO";
        }
        return "WHILE";
    };

    const auto targetLoop = toLoopKind(stmt.kind);
    const char *targetName = loopKindName(targetLoop);

    if (loopStack_.empty())
    {
        std::string msg = "EXIT ";
        msg += targetName;
        msg += " used outside of any loop";
        de.emit(il::support::Severity::Error, "B1011", stmt.loc, 4, std::move(msg));
        return;
    }

    const auto activeLoop = loopStack_.back();
    if (activeLoop != targetLoop)
    {
        std::string msg = "EXIT ";
        msg += targetName;
        msg += " does not match innermost loop (";
        msg += loopKindName(activeLoop);
        msg += ')';
        de.emit(il::support::Severity::Error, "B1011", stmt.loc, 4, std::move(msg));
    }
}

void SemanticAnalyzer::analyzeEnd(const EndStmt &)
{
    // nothing
}

void SemanticAnalyzer::analyzeResume(const Resume &stmt)
{
    if (!hasActiveErrorHandler())
    {
        std::string msg = "RESUME requires an active error handler";
        de.emit(il::support::Severity::Error,
                "B1012",
                stmt.loc,
                6,
                std::move(msg));
    }
    if (stmt.mode != Resume::Mode::Label)
        return;
    auto insertResult = labelRefs_.insert(stmt.target);
    if (insertResult.second && activeProcScope_)
        activeProcScope_->noteLabelRefInserted(stmt.target);
    if (!labels_.count(stmt.target))
    {
        std::string msg = "unknown line " + std::to_string(stmt.target);
        de.emit(il::support::Severity::Error, "B1003", stmt.loc, 4, std::move(msg));
    }
}

void SemanticAnalyzer::analyzeReturn(ReturnStmt &stmt)
{
    if (!activeProcScope_)
    {
        if (stmt.value)
        {
            std::string msg = "RETURN with value not allowed at top level";
            de.emit(il::support::Severity::Error,
                    "B1008",
                    stmt.loc,
                    6,
                    std::move(msg));
        }
        else
        {
            stmt.isGosubReturn = true;
        }
    }
    if (hasActiveErrorHandler())
        clearErrorHandler();
}

void SemanticAnalyzer::analyzeRandomize(const RandomizeStmt &r)
{
    if (r.seed)
    {
        auto ty = visitExpr(*r.seed);
        if (ty != Type::Unknown && ty != Type::Int && ty != Type::Float)
        {
            std::string msg = "seed type mismatch";
            de.emit(il::support::Severity::Error, "B2001", r.loc, 1, std::move(msg));
        }
    }
}

void SemanticAnalyzer::analyzeInput(InputStmt &inp)
{
    if (inp.prompt)
        visitExpr(*inp.prompt);
    for (auto &name : inp.vars)
    {
        if (name.empty())
            continue;
        resolveAndTrackSymbol(name, SymbolKind::InputTarget);
        bool loopVarMutation = false;
        for (auto it = forStack_.rbegin(); it != forStack_.rend(); ++it)
        {
            if (*it == name)
            {
                loopVarMutation = true;
                break;
            }
        }
        if (loopVarMutation)
        {
            std::string msg = "cannot assign to loop variable '" + name + "' inside FOR";
            de.emit(il::support::Severity::Error,
                    "B1010",
                    inp.loc,
                    static_cast<uint32_t>(name.size()),
                    std::move(msg));
        }
    }
}

void SemanticAnalyzer::analyzeLineInputCh(LineInputChStmt &inp)
{
    if (inp.channelExpr)
        visitExpr(*inp.channelExpr);
    if (inp.targetVar)
        visitExpr(*inp.targetVar);
}

void SemanticAnalyzer::analyzeDim(DimStmt &d)
{
    long long sz = -1;
    if (d.isArray)
    {
        if (d.size)
        {
            auto ty = visitExpr(*d.size);
            if (ty != Type::Unknown && ty != Type::Int)
            {
                std::string msg = "size type mismatch";
                de.emit(il::support::Severity::Error, "B2001", d.loc, 1, std::move(msg));
            }
            if (auto *ci = dynamic_cast<const IntExpr *>(d.size.get()))
            {
                sz = ci->value;
                if (sz < 0)
                {
                    std::string msg = "array size must be non-negative";
                    de.emit(il::support::Severity::Error, "B2003", d.loc, 1, std::move(msg));
                }
            }
        }
    }
    if (scopes_.hasScope())
    {
        if (scopes_.isDeclaredInCurrentScope(d.name))
        {
            std::string msg = "duplicate local '" + d.name + "'";
            de.emit(il::support::Severity::Error,
                    "B1006",
                    d.loc,
                    static_cast<uint32_t>(d.name.size()),
                    std::move(msg));
        }
        else
        {
            std::string unique = scopes_.declareLocal(d.name);
            d.name = unique;
            auto insertResult = symbols_.insert(unique);
            if (insertResult.second && activeProcScope_)
                activeProcScope_->noteSymbolInserted(unique);
        }
    }
    else
    {
        auto insertResult = symbols_.insert(d.name);
        if (insertResult.second && activeProcScope_)
            activeProcScope_->noteSymbolInserted(d.name);
    }
    if (d.isArray)
    {
        auto itArray = arrays_.find(d.name);
        if (activeProcScope_)
        {
            std::optional<long long> previous;
            if (itArray != arrays_.end())
                previous = itArray->second;
            activeProcScope_->noteArrayMutation(d.name, previous);
        }
        arrays_[d.name] = sz;

        auto itType = varTypes_.find(d.name);
        if (activeProcScope_)
        {
            std::optional<Type> previous;
            if (itType != varTypes_.end())
                previous = itType->second;
            activeProcScope_->noteVarTypeMutation(d.name, previous);
        }
        varTypes_[d.name] = Type::ArrayInt;
    }
    else
    {
        auto itType = varTypes_.find(d.name);
        if (activeProcScope_)
        {
            std::optional<Type> previous;
            if (itType != varTypes_.end())
                previous = itType->second;
            activeProcScope_->noteVarTypeMutation(d.name, previous);
        }
        varTypes_[d.name] = astToSemanticType(d.type);
    }
}

void SemanticAnalyzer::analyzeReDim(ReDimStmt &d)
{
    long long sz = -1;
    if (d.size)
    {
        auto ty = visitExpr(*d.size);
        if (ty != Type::Unknown && ty != Type::Int)
        {
            std::string msg = "size type mismatch";
            de.emit(il::support::Severity::Error, "B2001", d.loc, 1, std::move(msg));
        }
        if (auto *ci = dynamic_cast<const IntExpr *>(d.size.get()))
        {
            sz = ci->value;
            if (sz < 0)
            {
                std::string msg = "array size must be non-negative";
                de.emit(il::support::Severity::Error, "B2003", d.loc, 1, std::move(msg));
            }
        }
    }

    resolveAndTrackSymbol(d.name, SymbolKind::Reference);

    auto itArray = arrays_.find(d.name);
    if (itArray == arrays_.end())
    {
        std::string msg = "unknown array '" + d.name + "'";
        de.emit(il::support::Severity::Error,
                "B1001",
                d.loc,
                static_cast<uint32_t>(d.name.size()),
                std::move(msg));
        return;
    }

    if (auto itType = varTypes_.find(d.name);
        itType != varTypes_.end() && itType->second != Type::ArrayInt)
    {
        std::string msg = "REDIM target must be an array";
        de.emit(il::support::Severity::Error, "B2001", d.loc, 1, std::move(msg));
        return;
    }

    if (activeProcScope_)
    {
        activeProcScope_->noteArrayMutation(d.name, itArray->second);
    }
    arrays_[d.name] = sz;
}

void SemanticAnalyzer::installErrorHandler(int label)
{
    errorHandlerActive_ = true;
    errorHandlerTarget_ = label;
}

void SemanticAnalyzer::clearErrorHandler()
{
    errorHandlerActive_ = false;
    errorHandlerTarget_.reset();
}

bool SemanticAnalyzer::hasActiveErrorHandler() const noexcept
{
    return errorHandlerActive_;
}

} // namespace il::frontends::basic
