//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/viperlang/Lowerer.cpp
// Purpose: IL code generation for ViperLang.
//
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Lowerer.hpp"
#include "il/runtime/RuntimeSignatures.hpp"

namespace il::frontends::viperlang
{

Lowerer::Lowerer(Sema &sema) : sema_(sema) {}

Lowerer::Module Lowerer::lower(ModuleDecl &module)
{
    // Initialize state
    module_ = std::make_unique<Module>();
    builder_ = std::make_unique<il::build::IRBuilder>(*module_);
    locals_.clear();
    usedExterns_.clear();

    // Setup string table emitter
    stringTable_.setEmitter([this](const std::string &label, const std::string &content)
                            { builder_->addGlobalStr(label, content); });

    // Lower all declarations
    for (auto &decl : module.declarations)
    {
        lowerDecl(decl.get());
    }

    // Add extern declarations for used runtime functions
    for (const auto &externName : usedExterns_)
    {
        const auto *desc = il::runtime::findRuntimeDescriptor(externName);
        if (desc)
        {
            builder_->addExtern(
                std::string(desc->name), desc->signature.retType, desc->signature.paramTypes);
        }
    }

    return std::move(*module_);
}

//=============================================================================
// Block Management
//=============================================================================

size_t Lowerer::createBlock(const std::string &base)
{
    return blockMgr_.createBlock(base);
}

void Lowerer::setBlock(size_t blockIdx)
{
    blockMgr_.setBlock(blockIdx);
}

//=============================================================================
// Declaration Lowering
//=============================================================================

void Lowerer::lowerDecl(Decl *decl)
{
    if (!decl)
        return;

    switch (decl->kind)
    {
        case DeclKind::Function:
            lowerFunctionDecl(*static_cast<FunctionDecl *>(decl));
            break;
        default:
            // Value, Entity, Interface handled later
            break;
    }
}

void Lowerer::lowerFunctionDecl(FunctionDecl &decl)
{
    // Determine return type
    TypeRef returnType =
        decl.returnType ? sema_.resolveType(decl.returnType.get()) : types::voidType();
    Type ilReturnType = mapType(returnType);

    // Build parameter list
    std::vector<il::core::Param> params;
    for (const auto &param : decl.params)
    {
        TypeRef paramType = param.type ? sema_.resolveType(param.type.get()) : types::unknown();
        params.push_back({param.name, mapType(paramType)});
    }

    // Mangle function name
    std::string mangledName = mangleFunctionName(decl.name);

    // Create function
    currentFunc_ = &builder_->startFunction(mangledName, ilReturnType, params);
    blockMgr_.bind(builder_.get(), currentFunc_);
    locals_.clear();

    // Create entry block
    size_t entryIdx = createBlock("entry");
    setBlock(entryIdx);

    // Define parameters as locals (they are function arguments)
    for (size_t i = 0; i < decl.params.size(); ++i)
    {
        // Get argument value from function
        Value argVal = Value::temp(static_cast<unsigned>(i));
        defineLocal(decl.params[i].name, argVal);
    }

    // Lower function body
    if (decl.body)
    {
        lowerStmt(decl.body.get());
    }

    // Add implicit return if needed
    if (!isTerminated())
    {
        if (ilReturnType.kind == Type::Kind::Void)
        {
            emitRetVoid();
        }
        else
        {
            emitRet(Value::constInt(0));
        }
    }

    currentFunc_ = nullptr;
}

//=============================================================================
// Statement Lowering
//=============================================================================

void Lowerer::lowerStmt(Stmt *stmt)
{
    if (!stmt)
        return;

    switch (stmt->kind)
    {
        case StmtKind::Block:
            lowerBlockStmt(static_cast<BlockStmt *>(stmt));
            break;
        case StmtKind::Expr:
            lowerExprStmt(static_cast<ExprStmt *>(stmt));
            break;
        case StmtKind::Var:
            lowerVarStmt(static_cast<VarStmt *>(stmt));
            break;
        case StmtKind::If:
            lowerIfStmt(static_cast<IfStmt *>(stmt));
            break;
        case StmtKind::While:
            lowerWhileStmt(static_cast<WhileStmt *>(stmt));
            break;
        case StmtKind::For:
            lowerForStmt(static_cast<ForStmt *>(stmt));
            break;
        case StmtKind::ForIn:
            lowerForInStmt(static_cast<ForInStmt *>(stmt));
            break;
        case StmtKind::Return:
            lowerReturnStmt(static_cast<ReturnStmt *>(stmt));
            break;
        case StmtKind::Break:
            lowerBreakStmt(static_cast<BreakStmt *>(stmt));
            break;
        case StmtKind::Continue:
            lowerContinueStmt(static_cast<ContinueStmt *>(stmt));
            break;
        case StmtKind::Guard:
            lowerGuardStmt(static_cast<GuardStmt *>(stmt));
            break;
        case StmtKind::Match:
            // TODO: Lower match statements
            break;
    }
}

void Lowerer::lowerBlockStmt(BlockStmt *stmt)
{
    for (auto &s : stmt->statements)
    {
        lowerStmt(s.get());
    }
}

void Lowerer::lowerExprStmt(ExprStmt *stmt)
{
    lowerExpr(stmt->expr.get());
}

void Lowerer::lowerVarStmt(VarStmt *stmt)
{
    Value initValue;

    if (stmt->initializer)
    {
        auto result = lowerExpr(stmt->initializer.get());
        initValue = result.value;
    }
    else
    {
        // Default initialization
        TypeRef varType = stmt->type ? sema_.resolveType(stmt->type.get()) : types::unknown();
        Type ilType = mapType(varType);

        switch (ilType.kind)
        {
            case Type::Kind::I64:
            case Type::Kind::I32:
            case Type::Kind::I16:
            case Type::Kind::I1:
                initValue = Value::constInt(0);
                break;
            case Type::Kind::F64:
                initValue = Value::constFloat(0.0);
                break;
            case Type::Kind::Str:
                initValue = Value::constStr("");
                break;
            case Type::Kind::Ptr:
                initValue = Value::null();
                break;
            default:
                initValue = Value::constInt(0);
                break;
        }
    }

    defineLocal(stmt->name, initValue);
}

void Lowerer::lowerIfStmt(IfStmt *stmt)
{
    size_t thenIdx = createBlock("if_then");
    size_t elseIdx = stmt->elseBranch ? createBlock("if_else") : 0;
    size_t mergeIdx = createBlock("if_end");

    // Lower condition
    auto cond = lowerExpr(stmt->condition.get());

    // Emit branch
    if (stmt->elseBranch)
    {
        emitCBr(cond.value, thenIdx, elseIdx);
    }
    else
    {
        emitCBr(cond.value, thenIdx, mergeIdx);
    }

    // Lower then branch
    setBlock(thenIdx);
    lowerStmt(stmt->thenBranch.get());
    if (!isTerminated())
    {
        emitBr(mergeIdx);
    }

    // Lower else branch
    if (stmt->elseBranch)
    {
        setBlock(elseIdx);
        lowerStmt(stmt->elseBranch.get());
        if (!isTerminated())
        {
            emitBr(mergeIdx);
        }
    }

    setBlock(mergeIdx);
}

void Lowerer::lowerWhileStmt(WhileStmt *stmt)
{
    size_t condIdx = createBlock("while_cond");
    size_t bodyIdx = createBlock("while_body");
    size_t endIdx = createBlock("while_end");

    // Push loop context
    loopStack_.push(endIdx, condIdx);

    // Branch to condition
    emitBr(condIdx);

    // Lower condition
    setBlock(condIdx);
    auto cond = lowerExpr(stmt->condition.get());
    emitCBr(cond.value, bodyIdx, endIdx);

    // Lower body
    setBlock(bodyIdx);
    lowerStmt(stmt->body.get());
    if (!isTerminated())
    {
        emitBr(condIdx);
    }

    // Pop loop context
    loopStack_.pop();

    setBlock(endIdx);
}

void Lowerer::lowerForStmt(ForStmt *stmt)
{
    size_t condIdx = createBlock("for_cond");
    size_t bodyIdx = createBlock("for_body");
    size_t updateIdx = createBlock("for_update");
    size_t endIdx = createBlock("for_end");

    // Push loop context
    loopStack_.push(endIdx, updateIdx);

    // Lower init
    if (stmt->init)
    {
        lowerStmt(stmt->init.get());
    }

    // Branch to condition
    emitBr(condIdx);

    // Lower condition
    setBlock(condIdx);
    if (stmt->condition)
    {
        auto cond = lowerExpr(stmt->condition.get());
        emitCBr(cond.value, bodyIdx, endIdx);
    }
    else
    {
        emitBr(bodyIdx);
    }

    // Lower body
    setBlock(bodyIdx);
    lowerStmt(stmt->body.get());
    if (!isTerminated())
    {
        emitBr(updateIdx);
    }

    // Lower update
    setBlock(updateIdx);
    if (stmt->update)
    {
        lowerExpr(stmt->update.get());
    }
    emitBr(condIdx);

    // Pop loop context
    loopStack_.pop();

    setBlock(endIdx);
}

void Lowerer::lowerForInStmt(ForInStmt *stmt)
{
    // For now, only support range iteration
    auto *rangeExpr = dynamic_cast<RangeExpr *>(stmt->iterable.get());
    if (!rangeExpr)
    {
        // TODO: Support collection iteration
        return;
    }

    size_t condIdx = createBlock("forin_cond");
    size_t bodyIdx = createBlock("forin_body");
    size_t updateIdx = createBlock("forin_update");
    size_t endIdx = createBlock("forin_end");

    loopStack_.push(endIdx, updateIdx);

    // Lower range bounds
    auto startResult = lowerExpr(rangeExpr->start.get());
    auto endResult = lowerExpr(rangeExpr->end.get());

    // Create loop variable
    defineLocal(stmt->variable, startResult.value);

    // Branch to condition
    emitBr(condIdx);

    // Condition: i < end (or <= for inclusive)
    setBlock(condIdx);
    Value *loopVar = lookupLocal(stmt->variable);
    Value cond;
    if (rangeExpr->inclusive)
    {
        cond = emitBinary(Opcode::SCmpLE, Type(Type::Kind::I1), *loopVar, endResult.value);
    }
    else
    {
        cond = emitBinary(Opcode::SCmpLT, Type(Type::Kind::I1), *loopVar, endResult.value);
    }
    emitCBr(cond, bodyIdx, endIdx);

    // Body
    setBlock(bodyIdx);
    lowerStmt(stmt->body.get());
    if (!isTerminated())
    {
        emitBr(updateIdx);
    }

    // Update: i = i + 1
    setBlock(updateIdx);
    loopVar = lookupLocal(stmt->variable);
    Value nextVal = emitBinary(Opcode::Add, Type(Type::Kind::I64), *loopVar, Value::constInt(1));
    defineLocal(stmt->variable, nextVal);
    emitBr(condIdx);

    loopStack_.pop();
    setBlock(endIdx);
}

void Lowerer::lowerReturnStmt(ReturnStmt *stmt)
{
    if (stmt->value)
    {
        auto result = lowerExpr(stmt->value.get());
        emitRet(result.value);
    }
    else
    {
        emitRetVoid();
    }
}

void Lowerer::lowerBreakStmt(BreakStmt * /*stmt*/)
{
    if (!loopStack_.empty())
    {
        emitBr(loopStack_.breakTarget());
    }
}

void Lowerer::lowerContinueStmt(ContinueStmt * /*stmt*/)
{
    if (!loopStack_.empty())
    {
        emitBr(loopStack_.continueTarget());
    }
}

void Lowerer::lowerGuardStmt(GuardStmt *stmt)
{
    size_t elseIdx = createBlock("guard_else");
    size_t contIdx = createBlock("guard_cont");

    // Lower condition
    auto cond = lowerExpr(stmt->condition.get());

    // If condition is true, continue; else, execute else block
    emitCBr(cond.value, contIdx, elseIdx);

    // Lower else block (must exit)
    setBlock(elseIdx);
    lowerStmt(stmt->elseBlock.get());
    // Else block should have terminator (return, break, continue)

    setBlock(contIdx);
}

//=============================================================================
// Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerExpr(Expr *expr)
{
    if (!expr)
        return {Value::constInt(0), Type(Type::Kind::I64)};

    switch (expr->kind)
    {
        case ExprKind::IntLiteral:
            return lowerIntLiteral(static_cast<IntLiteralExpr *>(expr));
        case ExprKind::NumberLiteral:
            return lowerNumberLiteral(static_cast<NumberLiteralExpr *>(expr));
        case ExprKind::StringLiteral:
            return lowerStringLiteral(static_cast<StringLiteralExpr *>(expr));
        case ExprKind::BoolLiteral:
            return lowerBoolLiteral(static_cast<BoolLiteralExpr *>(expr));
        case ExprKind::NullLiteral:
            return lowerNullLiteral(static_cast<NullLiteralExpr *>(expr));
        case ExprKind::Ident:
            return lowerIdent(static_cast<IdentExpr *>(expr));
        case ExprKind::Binary:
            return lowerBinary(static_cast<BinaryExpr *>(expr));
        case ExprKind::Unary:
            return lowerUnary(static_cast<UnaryExpr *>(expr));
        case ExprKind::Call:
            return lowerCall(static_cast<CallExpr *>(expr));
        default:
            return {Value::constInt(0), Type(Type::Kind::I64)};
    }
}

LowerResult Lowerer::lowerIntLiteral(IntLiteralExpr *expr)
{
    return {Value::constInt(expr->value), Type(Type::Kind::I64)};
}

LowerResult Lowerer::lowerNumberLiteral(NumberLiteralExpr *expr)
{
    return {Value::constFloat(expr->value), Type(Type::Kind::F64)};
}

LowerResult Lowerer::lowerStringLiteral(StringLiteralExpr *expr)
{
    std::string globalName = getStringGlobal(expr->value);
    Value val = emitConstStr(globalName);
    return {val, Type(Type::Kind::Str)};
}

LowerResult Lowerer::lowerBoolLiteral(BoolLiteralExpr *expr)
{
    return {Value::constBool(expr->value), Type(Type::Kind::I1)};
}

LowerResult Lowerer::lowerNullLiteral(NullLiteralExpr * /*expr*/)
{
    return {Value::null(), Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerIdent(IdentExpr *expr)
{
    Value *local = lookupLocal(expr->name);
    if (local)
    {
        TypeRef type = sema_.typeOf(expr);
        return {*local, mapType(type)};
    }

    // Unknown identifier
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

LowerResult Lowerer::lowerBinary(BinaryExpr *expr)
{
    // Handle assignment specially
    if (expr->op == BinaryOp::Assign)
    {
        // Evaluate RHS first
        auto right = lowerExpr(expr->right.get());

        // LHS must be an identifier for simple assignment
        if (auto *ident = dynamic_cast<IdentExpr *>(expr->left.get()))
        {
            defineLocal(ident->name, right.value);
            return right;
        }

        // TODO: Handle field assignment, index assignment, etc.
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    auto left = lowerExpr(expr->left.get());
    auto right = lowerExpr(expr->right.get());

    TypeRef leftType = sema_.typeOf(expr->left.get());
    bool isFloat = leftType && leftType->kind == TypeKindSem::Number;

    Opcode op;
    Type resultType = left.type;

    switch (expr->op)
    {
        case BinaryOp::Add:
            if (leftType && leftType->kind == TypeKindSem::String)
            {
                // String concatenation
                Value result = emitCallRet(
                    Type(Type::Kind::Str), "Viper.String.Concat", {left.value, right.value});
                return {result, Type(Type::Kind::Str)};
            }
            op = isFloat ? Opcode::FAdd : Opcode::Add;
            break;
        case BinaryOp::Sub:
            op = isFloat ? Opcode::FSub : Opcode::Sub;
            break;
        case BinaryOp::Mul:
            op = isFloat ? Opcode::FMul : Opcode::Mul;
            break;
        case BinaryOp::Div:
            op = isFloat ? Opcode::FDiv : Opcode::SDiv;
            break;
        case BinaryOp::Mod:
            op = Opcode::SRem;
            break;
        case BinaryOp::Eq:
            op = isFloat ? Opcode::FCmpEQ : Opcode::ICmpEq;
            resultType = Type(Type::Kind::I1);
            break;
        case BinaryOp::Ne:
            op = isFloat ? Opcode::FCmpNE : Opcode::ICmpNe;
            resultType = Type(Type::Kind::I1);
            break;
        case BinaryOp::Lt:
            op = isFloat ? Opcode::FCmpLT : Opcode::SCmpLT;
            resultType = Type(Type::Kind::I1);
            break;
        case BinaryOp::Le:
            op = isFloat ? Opcode::FCmpLE : Opcode::SCmpLE;
            resultType = Type(Type::Kind::I1);
            break;
        case BinaryOp::Gt:
            op = isFloat ? Opcode::FCmpGT : Opcode::SCmpGT;
            resultType = Type(Type::Kind::I1);
            break;
        case BinaryOp::Ge:
            op = isFloat ? Opcode::FCmpGE : Opcode::SCmpGE;
            resultType = Type(Type::Kind::I1);
            break;
        case BinaryOp::And:
            op = Opcode::And;
            resultType = Type(Type::Kind::I1);
            break;
        case BinaryOp::Or:
            op = Opcode::Or;
            resultType = Type(Type::Kind::I1);
            break;
        case BinaryOp::BitAnd:
            op = Opcode::And;
            break;
        case BinaryOp::BitOr:
            op = Opcode::Or;
            break;
        case BinaryOp::BitXor:
            op = Opcode::Xor;
            break;
        case BinaryOp::Assign:
            // Handled above
            break;
    }

    Value result = emitBinary(op, resultType, left.value, right.value);
    return {result, resultType};
}

LowerResult Lowerer::lowerUnary(UnaryExpr *expr)
{
    auto operand = lowerExpr(expr->operand.get());
    TypeRef operandType = sema_.typeOf(expr->operand.get());
    bool isFloat = operandType && operandType->kind == TypeKindSem::Number;

    switch (expr->op)
    {
        case UnaryOp::Neg:
        {
            // Synthesize negation: 0 - x (no INeg/FNeg opcode)
            if (isFloat)
            {
                Value result =
                    emitBinary(Opcode::FSub, operand.type, Value::constFloat(0.0), operand.value);
                return {result, operand.type};
            }
            else
            {
                Value result =
                    emitBinary(Opcode::Sub, operand.type, Value::constInt(0), operand.value);
                return {result, operand.type};
            }
        }
        case UnaryOp::Not:
        {
            // Boolean NOT: compare with 0 (false)
            Value result =
                emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), operand.value, Value::constInt(0));
            return {result, Type(Type::Kind::I1)};
        }
        case UnaryOp::BitNot:
        {
            // Bitwise NOT: XOR with -1 (all bits set)
            Value result =
                emitBinary(Opcode::Xor, operand.type, operand.value, Value::constInt(-1));
            return {result, operand.type};
        }
    }

    return operand;
}

LowerResult Lowerer::lowerCall(CallExpr *expr)
{
    // Check if this is a resolved runtime call (e.g., Viper.Terminal.Say)
    std::string runtimeCallee = sema_.runtimeCallee(expr);
    if (!runtimeCallee.empty())
    {
        // Lower arguments
        std::vector<Value> args;
        for (auto &arg : expr->args)
        {
            auto result = lowerExpr(arg.value.get());
            args.push_back(result.value);
        }

        // Get return type from expression
        TypeRef exprType = sema_.typeOf(expr);
        Type ilReturnType = exprType ? mapType(exprType) : Type(Type::Kind::Void);

        if (ilReturnType.kind == Type::Kind::Void)
        {
            emitCall(runtimeCallee, args);
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }
        else
        {
            Value result = emitCallRet(ilReturnType, runtimeCallee, args);
            return {result, ilReturnType};
        }
    }

    // Check for built-in functions
    if (auto *ident = dynamic_cast<IdentExpr *>(expr->callee.get()))
    {
        if (ident->name == "print" || ident->name == "println")
        {
            if (!expr->args.empty())
            {
                auto arg = lowerExpr(expr->args[0].value.get());
                TypeRef argType = sema_.typeOf(expr->args[0].value.get());

                // Convert to string if needed
                Value strVal = arg.value;
                if (argType && argType->kind != TypeKindSem::String)
                {
                    if (argType->kind == TypeKindSem::Integer)
                    {
                        strVal =
                            emitCallRet(Type(Type::Kind::Str), "Viper.String.FromInt", {arg.value});
                    }
                    else if (argType->kind == TypeKindSem::Number)
                    {
                        strVal =
                            emitCallRet(Type(Type::Kind::Str), "Viper.String.FromNum", {arg.value});
                    }
                }

                emitCall("Viper.Terminal.Say", {strVal});
            }
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }
    }

    // Lower arguments
    std::vector<Value> args;
    for (auto &arg : expr->args)
    {
        auto result = lowerExpr(arg.value.get());
        args.push_back(result.value);
    }

    // Get callee name
    std::string calleeName;
    if (auto *ident = dynamic_cast<IdentExpr *>(expr->callee.get()))
    {
        calleeName = mangleFunctionName(ident->name);
    }
    else
    {
        calleeName = "unknown";
    }

    // Get return type
    TypeRef calleeType = sema_.typeOf(expr->callee.get());
    TypeRef returnType = calleeType ? calleeType->returnType() : nullptr;
    Type ilReturnType = returnType ? mapType(returnType) : Type(Type::Kind::Void);

    if (ilReturnType.kind == Type::Kind::Void)
    {
        emitCall(calleeName, args);
        return {Value::constInt(0), Type(Type::Kind::Void)};
    }
    else
    {
        Value result = emitCallRet(ilReturnType, calleeName, args);
        return {result, ilReturnType};
    }
}

//=============================================================================
// Instruction Emission Helpers
//=============================================================================

Lowerer::Value Lowerer::emitBinary(Opcode op, Type ty, Value lhs, Value rhs)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = op;
    instr.type = ty;
    instr.operands = {lhs, rhs};
    blockMgr_.currentBlock()->instructions.push_back(instr);
    return Value::temp(id);
}

Lowerer::Value Lowerer::emitUnary(Opcode op, Type ty, Value operand)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = op;
    instr.type = ty;
    instr.operands = {operand};
    blockMgr_.currentBlock()->instructions.push_back(instr);
    return Value::temp(id);
}

Lowerer::Value Lowerer::emitCallRet(Type retTy,
                                    const std::string &callee,
                                    const std::vector<Value> &args)
{
    usedExterns_.insert(callee);
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::Call;
    instr.type = retTy;
    instr.callee = callee;
    instr.operands = args;
    blockMgr_.currentBlock()->instructions.push_back(instr);
    return Value::temp(id);
}

void Lowerer::emitCall(const std::string &callee, const std::vector<Value> &args)
{
    usedExterns_.insert(callee);
    il::core::Instr instr;
    instr.op = Opcode::Call;
    instr.type = Type(Type::Kind::Void);
    instr.callee = callee;
    instr.operands = args;
    blockMgr_.currentBlock()->instructions.push_back(instr);
}

void Lowerer::emitBr(size_t targetIdx)
{
    // Use index-based access to avoid stale pointer after vector reallocation
    il::core::Instr instr;
    instr.op = Opcode::Br;
    instr.type = Type(Type::Kind::Void);
    instr.labels.push_back(currentFunc_->blocks[targetIdx].label);
    instr.brArgs.push_back({});
    blockMgr_.currentBlock()->instructions.push_back(std::move(instr));
    blockMgr_.currentBlock()->terminated = true;
}

void Lowerer::emitCBr(Value cond, size_t trueIdx, size_t falseIdx)
{
    // Use index-based access to avoid stale pointer after vector reallocation
    il::core::Instr instr;
    instr.op = Opcode::CBr;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(cond);
    instr.labels.push_back(currentFunc_->blocks[trueIdx].label);
    instr.labels.push_back(currentFunc_->blocks[falseIdx].label);
    instr.brArgs.push_back({});
    instr.brArgs.push_back({});
    blockMgr_.currentBlock()->instructions.push_back(std::move(instr));
    blockMgr_.currentBlock()->terminated = true;
}

void Lowerer::emitRet(Value val)
{
    // Use index-based access to avoid stale pointer after vector reallocation
    il::core::Instr instr;
    instr.op = Opcode::Ret;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(val);
    blockMgr_.currentBlock()->instructions.push_back(std::move(instr));
    blockMgr_.currentBlock()->terminated = true;
}

void Lowerer::emitRetVoid()
{
    // Use index-based access to avoid stale pointer after vector reallocation
    il::core::Instr instr;
    instr.op = Opcode::Ret;
    instr.type = Type(Type::Kind::Void);
    blockMgr_.currentBlock()->instructions.push_back(std::move(instr));
    blockMgr_.currentBlock()->terminated = true;
}

Lowerer::Value Lowerer::emitConstStr(const std::string &globalName)
{
    return builder_->emitConstStr(globalName, {});
}

unsigned Lowerer::nextTempId()
{
    return builder_->reserveTempId();
}

//=============================================================================
// Type Mapping
//=============================================================================

Lowerer::Type Lowerer::mapType(TypeRef type)
{
    if (!type)
        return Type(Type::Kind::Void);

    return Type(toILType(*type));
}

//=============================================================================
// Local Variable Management
//=============================================================================

void Lowerer::defineLocal(const std::string &name, Value value)
{
    locals_[name] = value;
}

Lowerer::Value *Lowerer::lookupLocal(const std::string &name)
{
    auto it = locals_.find(name);
    return it != locals_.end() ? &it->second : nullptr;
}

//=============================================================================
// Helper Functions
//=============================================================================

std::string Lowerer::mangleFunctionName(const std::string &name)
{
    // Entry point is special
    if (name == "start")
        return "main";
    return name;
}

std::string Lowerer::getStringGlobal(const std::string &value)
{
    return stringTable_.intern(value);
}

} // namespace il::frontends::viperlang
