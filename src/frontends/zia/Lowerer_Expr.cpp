//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Expr.cpp
/// @brief Expression lowering dispatcher and simple expressions for the Zia IL lowerer.
///
/// @details This file implements the main expression lowering dispatcher and
/// handles simple expression types including identifiers and ternary expressions.
/// Complex expressions (field access, new, coalesce, optional chaining, lambda,
/// try, block, and as expressions) are in Lowerer_Expr_Complex.cpp.
///
/// @see Lowerer_Expr_Complex.cpp - Complex expression lowering
/// @see Lowerer_Expr_Call.cpp - Call expression lowering
/// @see Lowerer_Expr_Binary.cpp - Binary operation lowering
/// @see Lowerer_Expr_Literals.cpp - Literal expression lowering
/// @see Lowerer_Expr_Collections.cpp - Collection expression lowering
/// @see Lowerer_Expr_Match.cpp - Pattern matching expression lowering
/// @see Lowerer_Expr_Method.cpp - Method call and type construction lowering
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"

namespace il::frontends::zia
{

using namespace runtime;

//=============================================================================
// Expression Lowering Dispatcher
//=============================================================================

LowerResult Lowerer::lowerExpr(Expr *expr)
{
    if (!expr)
        return {Value::constInt(0), Type(Type::Kind::I64)};

    if (++exprLowerDepth_ > kMaxLowerDepth)
    {
        --exprLowerDepth_;
        diag_.report({il::support::Severity::Error,
                      "expression nesting too deep during lowering (limit: 512)",
                      expr->loc, "V3200"});
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }
    struct DepthGuard { unsigned &d; ~DepthGuard() { --d; } } exprGuard_{exprLowerDepth_};

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
        case ExprKind::SelfExpr:
        {
            Value selfPtr;
            if (getSelfPtr(selfPtr))
            {
                return {selfPtr, Type(Type::Kind::Ptr)};
            }
            return {Value::constInt(0), Type(Type::Kind::Ptr)};
        }
        case ExprKind::SuperExpr:
        {
            // Super returns self pointer but is used for dispatching to parent methods
            Value selfPtr;
            if (getSelfPtr(selfPtr))
            {
                return {selfPtr, Type(Type::Kind::Ptr)};
            }
            return {Value::constInt(0), Type(Type::Kind::Ptr)};
        }
        case ExprKind::Binary:
            return lowerBinary(static_cast<BinaryExpr *>(expr));
        case ExprKind::Unary:
            return lowerUnary(static_cast<UnaryExpr *>(expr));
        case ExprKind::Ternary:
            return lowerTernary(static_cast<TernaryExpr *>(expr));
        case ExprKind::If:
            return lowerIfExpr(static_cast<IfExpr *>(expr));
        case ExprKind::StructLiteral:
            return lowerStructLiteral(static_cast<StructLiteralExpr *>(expr));
        case ExprKind::Call:
            return lowerCall(static_cast<CallExpr *>(expr));
        case ExprKind::Field:
            return lowerField(static_cast<FieldExpr *>(expr));
        case ExprKind::New:
            return lowerNew(static_cast<NewExpr *>(expr));
        case ExprKind::Coalesce:
            return lowerCoalesce(static_cast<CoalesceExpr *>(expr));
        case ExprKind::OptionalChain:
            return lowerOptionalChain(static_cast<OptionalChainExpr *>(expr));
        case ExprKind::ListLiteral:
            return lowerListLiteral(static_cast<ListLiteralExpr *>(expr));
        case ExprKind::MapLiteral:
            return lowerMapLiteral(static_cast<MapLiteralExpr *>(expr));
        case ExprKind::Index:
            return lowerIndex(static_cast<IndexExpr *>(expr));
        case ExprKind::Try:
            return lowerTry(static_cast<TryExpr *>(expr));
        case ExprKind::Lambda:
            return lowerLambda(static_cast<LambdaExpr *>(expr));
        case ExprKind::Tuple:
            return lowerTuple(static_cast<TupleExpr *>(expr));
        case ExprKind::TupleIndex:
            return lowerTupleIndex(static_cast<TupleIndexExpr *>(expr));
        case ExprKind::Block:
            return lowerBlockExpr(static_cast<BlockExpr *>(expr));
        case ExprKind::Match:
            return lowerMatchExpr(static_cast<MatchExpr *>(expr));
        case ExprKind::As:
            return lowerAs(static_cast<AsExpr *>(expr));
        default:
            return {Value::constInt(0), Type(Type::Kind::I64)};
    }
}

//=============================================================================
// Identifier Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerIdent(IdentExpr *expr)
{
    // Check for slot-based mutable variables first (e.g., loop variables)
    auto slotIt = slots_.find(expr->name);
    if (slotIt != slots_.end())
    {
        // Use localTypes_ first (set for parameters in generic method bodies), fall back to
        // sema_.typeOf()
        auto localTypeIt = localTypes_.find(expr->name);
        TypeRef type =
            (localTypeIt != localTypes_.end()) ? localTypeIt->second : sema_.typeOf(expr);
        Type ilType = mapType(type);
        Value loaded = loadFromSlot(expr->name, ilType);
        return {loaded, ilType};
    }

    Value *local = lookupLocal(expr->name);
    if (local)
    {
        auto localTypeIt = localTypes_.find(expr->name);
        TypeRef type =
            (localTypeIt != localTypes_.end()) ? localTypeIt->second : sema_.typeOf(expr);
        return {*local, mapType(type)};
    }

    // Check for implicit field access (self.field) inside a value type method
    if (currentValueType_)
    {
        const FieldLayout *field = currentValueType_->findField(expr->name);
        if (field)
        {
            Value selfPtr;
            if (getSelfPtr(selfPtr))
            {
                Value loaded = emitFieldLoad(field, selfPtr);
                return {loaded, mapType(field->type)};
            }
        }
    }

    // Check for implicit field access (self.field) inside an entity method
    if (currentEntityType_)
    {
        const FieldLayout *field = currentEntityType_->findField(expr->name);
        if (field)
        {
            Value selfPtr;
            if (getSelfPtr(selfPtr))
            {
                Value loaded = emitFieldLoad(field, selfPtr);
                return {loaded, mapType(field->type)};
            }
        }
    }

    // Check for global constants (module-level const declarations)
    auto constIt = globalConstants_.find(expr->name);
    if (constIt != globalConstants_.end())
    {
        const Value &val = constIt->second;
        // Determine the type from the value kind
        Type ilType;
        switch (val.kind)
        {
            case Value::Kind::ConstFloat:
                ilType = Type(Type::Kind::F64);
                break;
            case Value::Kind::ConstStr:
            {
                // String constants need to emit a const_str instruction to load the global
                // The stored value's str field contains the global label (e.g., ".L10")
                Value loaded = emitConstStr(val.str);
                return {loaded, Type(Type::Kind::Str)};
            }
            case Value::Kind::GlobalAddr:
                ilType = Type(Type::Kind::Str);
                break;
            case Value::Kind::ConstInt:
                // Check if it's a boolean (i1) or integer (i64)
                ilType = val.isBool ? Type(Type::Kind::I1) : Type(Type::Kind::I64);
                break;
            default:
                ilType = Type(Type::Kind::I64);
                break;
        }
        return {val, ilType};
    }

    // Check for global mutable variables (module-level var declarations)
    auto globalIt = globalVariables_.find(expr->name);
    if (globalIt != globalVariables_.end())
    {
        TypeRef type = globalIt->second;
        Type ilType = mapType(type);
        Value addr = getGlobalVarAddr(expr->name, type);
        Value loaded = emitLoad(addr, ilType);
        return {loaded, ilType};
    }

    // Check for auto-evaluated property getters (e.g., Pi → call Viper.Math.get_Pi())
    std::string autoGetter = sema_.autoEvalGetter(expr);
    if (!autoGetter.empty())
    {
        TypeRef type = sema_.typeOf(expr);
        Type ilType = mapType(type);
        Value result = emitCallRet(ilType, autoGetter, {});
        return {result, ilType};
    }

    // Check if identifier refers to a function - return its address for function pointers
    // This enables passing functions to Thread.Start, callbacks, etc.
    std::string mangledName = mangleFunctionName(expr->name);
    if (definedFunctions_.find(mangledName) != definedFunctions_.end())
    {
        // Function is defined in this module - return its address
        return {Value::global(mangledName), Type(Type::Kind::Ptr)};
    }

    // Check if it's an extern function (runtime API)
    Symbol *sym = sema_.findExternFunction(expr->name);
    if (sym)
    {
        // External function reference - return its address
        return {Value::global(expr->name), Type(Type::Kind::Ptr)};
    }

    // Unknown identifier
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

//=============================================================================
// Ternary Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerTernary(TernaryExpr *expr)
{
    auto cond = lowerExpr(expr->condition.get());
    TypeRef resultType = sema_.typeOf(expr);
    Type ilResultType = mapType(resultType);
    bool expectsOptional = resultType && resultType->kind == TypeKindSem::Optional;
    TypeRef optionalInner = expectsOptional ? resultType->innerType() : nullptr;

    // Allocate a stack slot for the result before branching.
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(8)};
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
    Value resultSlot = Value::temp(allocaId);

    size_t thenIdx = createBlock("ternary_then");
    size_t elseIdx = createBlock("ternary_else");
    size_t mergeIdx = createBlock("ternary_merge");

    emitCBr(cond.value, thenIdx, elseIdx);

    setBlock(thenIdx);
    {
        auto thenResult = lowerExpr(expr->thenExpr.get());
        Value thenValue = thenResult.value;
        if (expectsOptional)
        {
            TypeRef thenType = sema_.typeOf(expr->thenExpr.get());
            if (!thenType || thenType->kind != TypeKindSem::Optional)
            {
                if (optionalInner)
                    thenValue = emitOptionalWrap(thenResult.value, optionalInner);
            }
        }
        if (ilResultType.kind != Type::Kind::Void)
        {
            il::core::Instr storeInstr;
            storeInstr.op = Opcode::Store;
            storeInstr.type = ilResultType;
            storeInstr.operands = {resultSlot, thenValue};
            blockMgr_.currentBlock()->instructions.push_back(storeInstr);
        }
    }
    emitBr(mergeIdx);

    setBlock(elseIdx);
    {
        auto elseResult = lowerExpr(expr->elseExpr.get());
        Value elseValue = elseResult.value;
        if (expectsOptional)
        {
            TypeRef elseType = sema_.typeOf(expr->elseExpr.get());
            if (!elseType || elseType->kind != TypeKindSem::Optional)
            {
                if (optionalInner)
                    elseValue = emitOptionalWrap(elseResult.value, optionalInner);
            }
        }
        if (ilResultType.kind != Type::Kind::Void)
        {
            il::core::Instr storeInstr;
            storeInstr.op = Opcode::Store;
            storeInstr.type = ilResultType;
            storeInstr.operands = {resultSlot, elseValue};
            blockMgr_.currentBlock()->instructions.push_back(storeInstr);
        }
    }
    emitBr(mergeIdx);

    setBlock(mergeIdx);
    if (ilResultType.kind == Type::Kind::Void)
        return {Value::constInt(0), Type(Type::Kind::Void)};

    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = ilResultType;
    loadInstr.operands = {resultSlot};
    blockMgr_.currentBlock()->instructions.push_back(loadInstr);

    return {Value::temp(loadId), ilResultType};
}

//=============================================================================
// If-Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerIfExpr(IfExpr *expr)
{
    auto cond = lowerExpr(expr->condition.get());
    TypeRef resultType = sema_.typeOf(expr);
    Type ilResultType = mapType(resultType);
    bool expectsOptional = resultType && resultType->kind == TypeKindSem::Optional;
    TypeRef optionalInner = expectsOptional ? resultType->innerType() : nullptr;

    // Allocate a stack slot for the result before branching.
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(8)};
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
    Value resultSlot = Value::temp(allocaId);

    size_t thenIdx = createBlock("ifexpr_then");
    size_t elseIdx = createBlock("ifexpr_else");
    size_t mergeIdx = createBlock("ifexpr_merge");

    emitCBr(cond.value, thenIdx, elseIdx);

    setBlock(thenIdx);
    {
        auto thenResult = lowerExpr(expr->thenBranch.get());
        Value thenValue = thenResult.value;
        if (expectsOptional)
        {
            TypeRef thenType = sema_.typeOf(expr->thenBranch.get());
            if (!thenType || thenType->kind != TypeKindSem::Optional)
            {
                if (optionalInner)
                    thenValue = emitOptionalWrap(thenResult.value, optionalInner);
            }
        }
        if (ilResultType.kind != Type::Kind::Void)
        {
            il::core::Instr storeInstr;
            storeInstr.op = Opcode::Store;
            storeInstr.type = ilResultType;
            storeInstr.operands = {resultSlot, thenValue};
            blockMgr_.currentBlock()->instructions.push_back(storeInstr);
        }
    }
    emitBr(mergeIdx);

    setBlock(elseIdx);
    {
        auto elseResult = lowerExpr(expr->elseBranch.get());
        Value elseValue = elseResult.value;
        if (expectsOptional)
        {
            TypeRef elseType = sema_.typeOf(expr->elseBranch.get());
            if (!elseType || elseType->kind != TypeKindSem::Optional)
            {
                if (optionalInner)
                    elseValue = emitOptionalWrap(elseResult.value, optionalInner);
            }
        }
        if (ilResultType.kind != Type::Kind::Void)
        {
            il::core::Instr storeInstr;
            storeInstr.op = Opcode::Store;
            storeInstr.type = ilResultType;
            storeInstr.operands = {resultSlot, elseValue};
            blockMgr_.currentBlock()->instructions.push_back(storeInstr);
        }
    }
    emitBr(mergeIdx);

    setBlock(mergeIdx);
    if (ilResultType.kind == Type::Kind::Void)
        return {Value::constInt(0), Type(Type::Kind::Void)};

    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = ilResultType;
    loadInstr.operands = {resultSlot};
    blockMgr_.currentBlock()->instructions.push_back(loadInstr);

    return {Value::temp(loadId), ilResultType};
}

} // namespace il::frontends::zia
