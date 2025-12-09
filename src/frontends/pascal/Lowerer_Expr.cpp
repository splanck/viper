//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Lowerer_Expr.cpp
// Purpose: Expression lowering for Pascal AST to IL.
// Key invariants: Produces valid IL values with proper typing.
// Ownership/Lifetime: Part of Lowerer; operates on borrowed AST.
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Lowerer.hpp"
#include "frontends/pascal/BuiltinRegistry.hpp"
#include "frontends/common/CharUtils.hpp"
#include "il/runtime/RuntimeSignatures.hpp"

namespace il::frontends::pascal
{

using common::char_utils::toLowercase;

inline std::string toLower(const std::string &s)
{
    return toLowercase(s);
}

//===----------------------------------------------------------------------===//
// Expression Lowering
//===----------------------------------------------------------------------===//

LowerResult Lowerer::lowerExpr(const Expr &expr)
{
    switch (expr.kind)
    {
    case ExprKind::IntLiteral:
        return lowerIntLiteral(static_cast<const IntLiteralExpr &>(expr));
    case ExprKind::RealLiteral:
        return lowerRealLiteral(static_cast<const RealLiteralExpr &>(expr));
    case ExprKind::StringLiteral:
        return lowerStringLiteral(static_cast<const StringLiteralExpr &>(expr));
    case ExprKind::BoolLiteral:
        return lowerBoolLiteral(static_cast<const BoolLiteralExpr &>(expr));
    case ExprKind::NilLiteral:
        return lowerNilLiteral(static_cast<const NilLiteralExpr &>(expr));
    case ExprKind::Name:
        return lowerName(static_cast<const NameExpr &>(expr));
    case ExprKind::Unary:
        return lowerUnary(static_cast<const UnaryExpr &>(expr));
    case ExprKind::Binary:
        return lowerBinary(static_cast<const BinaryExpr &>(expr));
    case ExprKind::Call:
        return lowerCall(static_cast<const CallExpr &>(expr));
    case ExprKind::Index:
        return lowerIndex(static_cast<const IndexExpr &>(expr));
    case ExprKind::Field:
        return lowerField(static_cast<const FieldExpr &>(expr));
    case ExprKind::Is: {
        const auto &isExpr = static_cast<const IsExpr &>(expr);
        // Lower operand
        LowerResult obj = lowerExpr(*isExpr.operand);
        // Resolve target type via semantic analyzer
        il::frontends::pascal::PasType target = sema_->resolveType(*isExpr.targetType);
        // Default: false
        Value result = Value::constBool(false);
        if (target.kind == PasTypeKind::Class)
        {
            // Lookup class id
            int64_t classId = 0;
            auto it = classLayouts_.find(toLower(target.name));
            if (it != classLayouts_.end())
                classId = it->second.classId;
            usedExterns_.insert("rt_cast_as");
            Value casted = emitCallRet(Type(Type::Kind::Ptr), "rt_cast_as", {obj.value, Value::constInt(classId)});
            // Compare ptr != null -> i1
            result = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), casted, Value::null());
        }
        else if (target.kind == PasTypeKind::Interface)
        {
            // If/when interfaces have ids, use rt_cast_as_iface; for now fall back to false
            usedExterns_.insert("rt_cast_as_iface");
            // Without interface ids wired here, result remains false
        }
        return {result, Type(Type::Kind::I1)};
    }
    default:
        // Unsupported expression type - return zero
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }
}

LowerResult Lowerer::lowerIntLiteral(const IntLiteralExpr &expr)
{
    return {Value::constInt(expr.value), Type(Type::Kind::I64)};
}

LowerResult Lowerer::lowerRealLiteral(const RealLiteralExpr &expr)
{
    return {Value::constFloat(expr.value), Type(Type::Kind::F64)};
}

LowerResult Lowerer::lowerStringLiteral(const StringLiteralExpr &expr)
{
    std::string globalName = getStringGlobal(expr.value);
    Value strVal = emitConstStr(globalName);
    return {strVal, Type(Type::Kind::Str)};
}

LowerResult Lowerer::lowerBoolLiteral(const BoolLiteralExpr &expr)
{
    return {Value::constBool(expr.value), Type(Type::Kind::I1)};
}

LowerResult Lowerer::lowerNilLiteral(const NilLiteralExpr &)
{
    return {Value::null(), Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerName(const NameExpr &expr)
{
    std::string key = toLower(expr.name);

    // Check for built-in math constants (Pi and E from Viper.Math)
    if (key == "pi")
    {
        return {Value::constFloat(3.14159265358979323846), Type(Type::Kind::F64)};
    }
    if (key == "e")
    {
        return {Value::constFloat(2.71828182845904523536), Type(Type::Kind::F64)};
    }

    // Check constants first (including enum constants from semantic analyzer)
    auto constIt = constants_.find(key);
    if (constIt != constants_.end())
    {
        return {constIt->second, Type(Type::Kind::I64)}; // Type approximation
    }

    // Check semantic analyzer for enum constants and typed constants
    if (auto constType = sema_->lookupConstant(key))
    {
        if (constType->kind == PasTypeKind::Enum && constType->enumOrdinal >= 0)
        {
            // Enum constant: emit its ordinal value as an integer
            return {Value::constInt(constType->enumOrdinal), Type(Type::Kind::I64)};
        }
        // Handle Integer constants
        if (constType->kind == PasTypeKind::Integer)
        {
            if (auto val = sema_->lookupConstantInt(key))
            {
                return {Value::constInt(*val), Type(Type::Kind::I64)};
            }
        }
        // Handle Real constants
        if (constType->kind == PasTypeKind::Real)
        {
            if (auto val = sema_->lookupConstantReal(key))
            {
                return {Value::constFloat(*val), Type(Type::Kind::F64)};
            }
            return {Value::constFloat(0.0), Type(Type::Kind::F64)};
        }
        // Handle String constants
        if (constType->kind == PasTypeKind::String)
        {
            if (auto val = sema_->lookupConstantStr(key))
            {
                std::string globalName = getStringGlobal(*val);
                Value strVal = emitConstStr(globalName);
                return {strVal, Type(Type::Kind::Str)};
            }
        }
        // Handle Boolean constants (stored as integers 0/1 in constantValues_)
        if (constType->kind == PasTypeKind::Boolean)
        {
            if (auto val = sema_->lookupConstantInt(key))
            {
                return {Value::constBool(*val != 0), Type(Type::Kind::I1)};
            }
        }
    }

    // Check locals
    auto localIt = locals_.find(key);
    if (localIt != locals_.end())
    {
        // First check our own localTypes_ map (for procedure locals)
        // then fall back to semantic analyzer (for global variables)
        Type ilType = Type(Type::Kind::I64);
        auto localTypeIt = localTypes_.find(key);
        if (localTypeIt != localTypes_.end())
        {
            ilType = mapType(localTypeIt->second);
        }
        else
        {
            auto varType = sema_->lookupVariable(key);
            if (varType)
                ilType = mapType(*varType);
        }
        Value loaded = emitLoad(ilType, localIt->second);
        return {loaded, ilType};
    }

    // Check if we're inside a class method and the name is a field of the current class
    // This must be checked BEFORE builtins/functions so field names can shadow them
    if (!currentClassName_.empty())
    {
        auto *classInfo = sema_->lookupClass(toLower(currentClassName_));
        if (classInfo)
        {
            auto fieldIt = classInfo->fields.find(key);
            if (fieldIt != classInfo->fields.end())
            {
                // Access Self.fieldName
                // First, load Self pointer from locals
                auto selfIt = locals_.find("self");
                if (selfIt != locals_.end())
                {
                    Value selfPtr = emitLoad(Type(Type::Kind::Ptr), selfIt->second);

                    // Build a PasType with the class fields for getFieldAddress
                    PasType selfType = PasType::classType(currentClassName_);
                    for (const auto &[fname, finfo] : classInfo->fields)
                    {
                        selfType.fields[fname] = std::make_shared<PasType>(finfo.type);
                    }
                    auto [fieldAddr, fieldType] = getFieldAddress(selfPtr, selfType, expr.name);

                    // Load the field value
                    Value fieldVal = emitLoad(fieldType, fieldAddr);
                    return {fieldVal, fieldType};
                }
            }
        }
    }

    // Check for zero-argument builtin functions (Pascal allows calling without parens)
    if (auto builtinOpt = lookupBuiltin(key))
    {
        const auto &desc = getBuiltinDescriptor(*builtinOpt);
        // Only handle if it can be called with 0 args and has non-void return type
        if (desc.minArgs == 0 && desc.result != ResultKind::Void)
        {
            const char *rtSym = getBuiltinRuntimeSymbol(*builtinOpt);

            // Look up the actual runtime signature to get the correct return type
            const auto *rtDesc = il::runtime::findRuntimeDescriptor(rtSym);
            Type rtRetType = Type(Type::Kind::Void);
            if (rtDesc)
            {
                rtRetType = rtDesc->signature.retType;
            }
            else
            {
                // Fallback to Pascal type mapping
                PasType resultPasType = getBuiltinResultType(*builtinOpt);
                rtRetType = mapType(resultPasType);
            }

            // Also get the Pascal-expected return type for conversion
            PasType pascalResultType = getBuiltinResultType(*builtinOpt);
            Type pascalRetType = mapType(pascalResultType);

            // Emit call with no arguments
            Value result = emitCallRet(rtRetType, rtSym, {});

            // Convert integer to i1 if Pascal expects Boolean but runtime returns integer
            if (pascalRetType.kind == Type::Kind::I1 &&
                (rtRetType.kind == Type::Kind::I32 || rtRetType.kind == Type::Kind::I64))
            {
                // Convert to i1: compare != 0
                Value zero = Value::constInt(0);
                result = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), result, zero);
                return {result, Type(Type::Kind::I1)};
            }

            return {result, rtRetType};
        }
    }

    // Check for zero-argument user-defined functions (Pascal allows calling without parens)
    if (auto sig = sema_->lookupFunction(key))
    {
        // Only handle if it can be called with 0 args and has non-void return type
        if (sig->requiredParams == 0 && sig->returnType.kind != PasTypeKind::Void)
        {
            Type retType = mapType(sig->returnType);
            // Use the original function name from the signature (preserves case)
            Value result = emitCallRet(retType, sig->name, {});
            return {result, retType};
        }
    }

    // Unknown - return zero
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

LowerResult Lowerer::lowerUnary(const UnaryExpr &expr)
{
    LowerResult operand = lowerExpr(*expr.operand);

    switch (expr.op)
    {
    case UnaryExpr::Op::Neg:
        if (operand.type.kind == Type::Kind::F64)
        {
            // Negate float: 0.0 - x
            Value zero = Value::constFloat(0.0);
            Value result = emitBinary(Opcode::FSub, operand.type, zero, operand.value);
            return {result, operand.type};
        }
        else
        {
            // Negate integer: 0 - x (use overflow-checking subtraction)
            Value zero = Value::constInt(0);
            Value result = emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), zero, operand.value);
            return {result, Type(Type::Kind::I64)};
        }

    case UnaryExpr::Op::Not:
        // Boolean not
        {
            // If operand is i1, first zext to i64
            Value opVal = operand.value;
            if (operand.type.kind == Type::Kind::I1)
            {
                opVal = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), opVal);
            }
            // xor with 1
            Value one = Value::constInt(1);
            Value result = emitBinary(Opcode::Xor, Type(Type::Kind::I64), opVal, one);
            // Truncate back to i1
            result = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), result);
            return {result, Type(Type::Kind::I1)};
        }

    case UnaryExpr::Op::Plus:
        // Identity
        return operand;
    }

    return operand;
}

LowerResult Lowerer::lowerBinary(const BinaryExpr &expr)
{
    // Handle short-circuit operators specially
    switch (expr.op)
    {
    case BinaryExpr::Op::And:
        return lowerLogicalAnd(expr);
    case BinaryExpr::Op::Or:
        return lowerLogicalOr(expr);
    case BinaryExpr::Op::Coalesce:
        return lowerCoalesce(expr);
    default:
        break;
    }

    // Lower operands
    LowerResult lhs = lowerExpr(*expr.left);
    LowerResult rhs = lowerExpr(*expr.right);

    // Handle string comparisons via runtime call
    bool isString = (lhs.type.kind == Type::Kind::Str || rhs.type.kind == Type::Kind::Str);
    if (isString && (expr.op == BinaryExpr::Op::Eq || expr.op == BinaryExpr::Op::Ne))
    {
        // Call rt_str_eq(str, str) -> i1
        usedExterns_.insert("rt_str_eq");
        Value result = emitCallRet(Type(Type::Kind::I1), "rt_str_eq", {lhs.value, rhs.value});
        if (expr.op == BinaryExpr::Op::Ne)
        {
            // Negate the result for !=
            Value zero = Value::constBool(false);
            result = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), result, zero);
        }
        return {result, Type(Type::Kind::I1)};
    }

    // Determine result type
    bool isFloat = (lhs.type.kind == Type::Kind::F64 || rhs.type.kind == Type::Kind::F64);

    // Promote integer to float if needed
    Value lhsVal = lhs.value;
    Value rhsVal = rhs.value;
    if (isFloat)
    {
        if (lhs.type.kind != Type::Kind::F64)
            lhsVal = emitSitofp(lhs.value);
        if (rhs.type.kind != Type::Kind::F64)
            rhsVal = emitSitofp(rhs.value);
    }

    Type resultType = isFloat ? Type(Type::Kind::F64) : Type(Type::Kind::I64);

    switch (expr.op)
    {
    // Arithmetic
    // For signed integers (Pascal Integer type is always signed), use the .ovf
    // variants which trap on overflow as required by the IL spec
    case BinaryExpr::Op::Add:
        return {emitBinary(isFloat ? Opcode::FAdd : Opcode::IAddOvf, resultType, lhsVal, rhsVal),
                resultType};

    case BinaryExpr::Op::Sub:
        return {emitBinary(isFloat ? Opcode::FSub : Opcode::ISubOvf, resultType, lhsVal, rhsVal),
                resultType};

    case BinaryExpr::Op::Mul:
        return {emitBinary(isFloat ? Opcode::FMul : Opcode::IMulOvf, resultType, lhsVal, rhsVal),
                resultType};

    case BinaryExpr::Op::Div:
        // Real division always returns Real
        if (!isFloat)
        {
            lhsVal = emitSitofp(lhs.value);
            rhsVal = emitSitofp(rhs.value);
        }
        return {emitBinary(Opcode::FDiv, Type(Type::Kind::F64), lhsVal, rhsVal),
                Type(Type::Kind::F64)};

    case BinaryExpr::Op::IntDiv:
        // Integer division (with trap on divide-by-zero)
        return {emitBinary(Opcode::SDivChk0, Type(Type::Kind::I64), lhs.value, rhs.value),
                Type(Type::Kind::I64)};

    case BinaryExpr::Op::Mod:
        // Integer remainder (with trap on divide-by-zero)
        return {emitBinary(Opcode::SRemChk0, Type(Type::Kind::I64), lhs.value, rhs.value),
                Type(Type::Kind::I64)};

    // Comparisons
    case BinaryExpr::Op::Eq:
        return {emitBinary(isFloat ? Opcode::FCmpEQ : Opcode::ICmpEq, Type(Type::Kind::I1),
                           lhsVal, rhsVal),
                Type(Type::Kind::I1)};

    case BinaryExpr::Op::Ne:
        return {emitBinary(isFloat ? Opcode::FCmpNE : Opcode::ICmpNe, Type(Type::Kind::I1),
                           lhsVal, rhsVal),
                Type(Type::Kind::I1)};

    case BinaryExpr::Op::Lt:
        return {emitBinary(isFloat ? Opcode::FCmpLT : Opcode::SCmpLT, Type(Type::Kind::I1),
                           lhsVal, rhsVal),
                Type(Type::Kind::I1)};

    case BinaryExpr::Op::Le:
        return {emitBinary(isFloat ? Opcode::FCmpLE : Opcode::SCmpLE, Type(Type::Kind::I1),
                           lhsVal, rhsVal),
                Type(Type::Kind::I1)};

    case BinaryExpr::Op::Gt:
        return {emitBinary(isFloat ? Opcode::FCmpGT : Opcode::SCmpGT, Type(Type::Kind::I1),
                           lhsVal, rhsVal),
                Type(Type::Kind::I1)};

    case BinaryExpr::Op::Ge:
        return {emitBinary(isFloat ? Opcode::FCmpGE : Opcode::SCmpGE, Type(Type::Kind::I1),
                           lhsVal, rhsVal),
                Type(Type::Kind::I1)};

    default:
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }
}

LowerResult Lowerer::lowerLogicalAnd(const BinaryExpr &expr)
{
    // Short-circuit: if left is false, result is false; else result is right
    size_t evalRhsBlock = createBlock("and_rhs");
    size_t shortCircuitBlock = createBlock("and_short");
    size_t joinBlock = createBlock("and_join");

    // Allocate result slot before any branches
    Value resultSlot = emitAlloca(1);

    // Evaluate left
    LowerResult left = lowerExpr(*expr.left);
    Value leftBool = left.value;

    // If left is true, evaluate right; else short-circuit with false
    emitCBr(leftBool, evalRhsBlock, shortCircuitBlock);

    // Short-circuit: left was false, result is false
    setBlock(shortCircuitBlock);
    emitStore(Type(Type::Kind::I1), resultSlot, Value::constInt(0));
    emitBr(joinBlock);

    // Evaluate right in evalRhsBlock
    setBlock(evalRhsBlock);
    LowerResult right = lowerExpr(*expr.right);
    emitStore(Type(Type::Kind::I1), resultSlot, right.value);
    emitBr(joinBlock);

    // Join block - load result
    setBlock(joinBlock);
    Value result = emitLoad(Type(Type::Kind::I1), resultSlot);

    return {result, Type(Type::Kind::I1)};
}

LowerResult Lowerer::lowerLogicalOr(const BinaryExpr &expr)
{
    // Short-circuit: if left is true, result is true; else result is right
    size_t shortCircuitBlock = createBlock("or_short");
    size_t evalRhsBlock = createBlock("or_rhs");
    size_t joinBlock = createBlock("or_join");

    // Allocate result slot before any branches
    Value resultSlot = emitAlloca(1);

    // Evaluate left
    LowerResult left = lowerExpr(*expr.left);
    Value leftBool = left.value;

    // If left is true, short-circuit with true; else evaluate right
    emitCBr(leftBool, shortCircuitBlock, evalRhsBlock);

    // Short-circuit: left was true, result is true
    setBlock(shortCircuitBlock);
    emitStore(Type(Type::Kind::I1), resultSlot, Value::constInt(1));
    emitBr(joinBlock);

    // Evaluate right in evalRhsBlock
    setBlock(evalRhsBlock);
    LowerResult right = lowerExpr(*expr.right);
    emitStore(Type(Type::Kind::I1), resultSlot, right.value);
    emitBr(joinBlock);

    // Join block - load result
    setBlock(joinBlock);
    Value result = emitLoad(Type(Type::Kind::I1), resultSlot);

    return {result, Type(Type::Kind::I1)};
}

LowerResult Lowerer::lowerCoalesce(const BinaryExpr &expr)
{
    // a ?? b: if a is not nil, use a; else evaluate and use b
    // Short-circuits: b is only evaluated if a is nil

    size_t useLeftBlock = createBlock("coalesce_use_lhs");
    size_t evalRhsBlock = createBlock("coalesce_rhs");
    size_t joinBlock = createBlock("coalesce_join");

    // Allocate result slot before any branches
    Value resultSlot = emitAlloca(8);

    // Evaluate left operand
    LowerResult left = lowerExpr(*expr.left);

    // For reference-type optionals, null pointer means nil
    // For value-type optionals, we'd check the hasValue flag at offset 0
    // Currently, both are represented as Ptr (null = nil)
    Value isNotNil = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1),
                                left.value, Value::null());

    emitCBr(isNotNil, useLeftBlock, evalRhsBlock);

    // Use left value (not nil) - store to result slot
    setBlock(useLeftBlock);
    emitStore(left.type, resultSlot, left.value);
    emitBr(joinBlock);

    // Evaluate right operand (left was nil) - store to result slot
    setBlock(evalRhsBlock);
    LowerResult right = lowerExpr(*expr.right);
    emitStore(right.type, resultSlot, right.value);
    emitBr(joinBlock);

    // Join block - load from result slot
    setBlock(joinBlock);
    Value result = emitLoad(right.type, resultSlot);

    return {result, right.type};
}

LowerResult Lowerer::lowerCall(const CallExpr &expr)
{
    // Check for constructor call (marked by semantic analyzer)
    if (expr.isConstructorCall && !expr.constructorClassName.empty())
    {
        // Use the OOP constructor lowering which properly initializes vtable
        return lowerConstructorCall(expr);
    }

    // Check for method call: obj.Method(args)
    if (expr.callee->kind == ExprKind::Field)
    {
        const auto &fieldExpr = static_cast<const FieldExpr &>(*expr.callee);
        return lowerMethodCall(fieldExpr, expr);
    }

    // Get callee name for regular calls
    if (expr.callee->kind != ExprKind::Name)
    {
        // Unknown callee type - return default
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    auto &nameExpr = static_cast<const NameExpr &>(*expr.callee);
    std::string callee = nameExpr.name;

    // Implicit method call on Self inside a method: MethodName(args)
    if (!currentClassName_.empty())
    {
        std::string classKey = toLower(currentClassName_);
        auto *ci = sema_->lookupClass(classKey);
        if (ci)
        {
            std::string mkey = toLower(callee);
            auto mit = ci->methods.find(mkey);
            if (mit != ci->methods.end())
            {
                // Resolve Self from locals
                auto itSelf = locals_.find("self");
                if (itSelf != locals_.end())
                {
                    // Build arg list: Self + user args
                    std::vector<Value> args;
                    Value selfPtr = emitLoad(Type(Type::Kind::Ptr), itSelf->second);
                    args.push_back(selfPtr);
                    std::vector<PasType> argTypes;
                    for (const auto &arg : expr.args)
                    {
                        LowerResult lr = lowerExpr(*arg);
                        args.push_back(lr.value);
                        argTypes.push_back(PasType::unknown());
                    }

                    // Direct call to Class.Method
                    std::string funcName = currentClassName_ + "." + callee;
                    Type retType = mapType(mit->second.returnType);
                    if (retType.kind == Type::Kind::Void)
                    {
                        emitCall(funcName, args);
                        return {Value::constInt(0), Type(Type::Kind::Void)};
                    }
                    else
                    {
                        Value res = emitCallRet(retType, funcName, args);
                        return {res, retType};
                    }
                }
            }
        }
    }

    // Type-cast form: TClass(expr)
    // If callee is a type name and that type is a class, lower as rt_cast_as
    {
        std::string key = toLower(callee);
        auto typeOpt = sema_->lookupType(key);
        if (typeOpt && (typeOpt->kind == PasTypeKind::Class || typeOpt->kind == PasTypeKind::Interface))
        {
            // Expect exactly one argument; if missing, return null pointer
            if (expr.args.empty())
            {
                return {Value::null(), Type(Type::Kind::Ptr)};
            }

            // Lower the operand
            LowerResult obj = lowerExpr(*expr.args[0]);

            // Determine class id for the target type
            int64_t classId = 0;
            if (typeOpt->kind == PasTypeKind::Class)
            {
                std::string classKey = toLower(typeOpt->name);
                auto layoutIt = classLayouts_.find(classKey);
                if (layoutIt != classLayouts_.end())
                {
                    classId = layoutIt->second.classId;
                }
            }
            else
            {
                // For interfaces, we could support rt_cast_as_iface; for now use class path if available
                // Fallback: return original pointer
                return obj;
            }

            usedExterns_.insert("rt_cast_as");
            Value casted = emitCallRet(Type(Type::Kind::Ptr), "rt_cast_as",
                                       {obj.value, Value::constInt(classId)});
            return {casted, Type(Type::Kind::Ptr)};
        }
    }

    // Lower arguments and track their types
    std::vector<Value> args;
    std::vector<PasType> argTypes;
    for (const auto &arg : expr.args)
    {
        LowerResult argResult = lowerExpr(*arg);
        args.push_back(argResult.value);
        // Map IL type back to PasType for dispatch
        PasType pasType;
        switch (argResult.type.kind)
        {
        case Type::Kind::I64:
        case Type::Kind::I32:
        case Type::Kind::I1:
            pasType.kind = PasTypeKind::Integer;
            break;
        case Type::Kind::F64:
            pasType.kind = PasTypeKind::Real;
            break;
        case Type::Kind::Ptr:
        case Type::Kind::Str:
            pasType.kind = PasTypeKind::String;
            break;
        default:
            pasType.kind = PasTypeKind::Unknown;
            break;
        }
        argTypes.push_back(pasType);
    }

    // Check for builtin functions
    std::string lowerCallee = toLower(callee);
    auto builtinOpt = lookupBuiltin(lowerCallee);

    if (builtinOpt)
    {
        PascalBuiltin builtin = *builtinOpt;
        const BuiltinDescriptor &desc = getBuiltinDescriptor(builtin);

        // Determine first arg type for dispatch
        PasTypeKind firstArgType =
            argTypes.empty() ? PasTypeKind::Unknown : argTypes[0].kind;

        // Handle Write/WriteLn specially (variadic with type dispatch)
        if (builtin == PascalBuiltin::Write || builtin == PascalBuiltin::WriteLn)
        {
            // Print each argument using type-appropriate runtime call
            for (size_t i = 0; i < args.size(); ++i)
            {
                const char *rtSym = getBuiltinRuntimeSymbol(PascalBuiltin::Write, argTypes[i].kind);
                if (rtSym)
                {
                    emitCall(rtSym, {args[i]});
                }
                else
                {
                    // Default to i64
                    emitCall("rt_print_i64", {args[i]});
                }
            }
            if (builtin == PascalBuiltin::WriteLn)
            {
                std::string nlGlobal = getStringGlobal("\n");
                Value nlStr = emitConstStr(nlGlobal);
                emitCall("rt_print_str", {nlStr});
            }
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }

        // Handle ReadLn
        if (builtin == PascalBuiltin::ReadLn)
        {
            // For now, just call rt_input_line and discard result
            emitCallRet(Type(Type::Kind::Str), "rt_input_line", {});
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }

        // Handle inline builtins
        if (builtin == PascalBuiltin::Ord)
        {
            // Ord just returns the integer value (identity for integers)
            if (!args.empty())
                return {args[0], Type(Type::Kind::I64)};
            return {Value::constInt(0), Type(Type::Kind::I64)};
        }

        if (builtin == PascalBuiltin::Pred)
        {
            // Pred(x) = x - 1
            if (!args.empty())
            {
                Value one = Value::constInt(1);
                Value result = emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), args[0], one);
                return {result, Type(Type::Kind::I64)};
            }
            return {Value::constInt(0), Type(Type::Kind::I64)};
        }

        if (builtin == PascalBuiltin::Succ)
        {
            // Succ(x) = x + 1
            if (!args.empty())
            {
                Value one = Value::constInt(1);
                Value result = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), args[0], one);
                return {result, Type(Type::Kind::I64)};
            }
            return {Value::constInt(0), Type(Type::Kind::I64)};
        }

        if (builtin == PascalBuiltin::Sqr)
        {
            // Sqr(x) = x * x (use overflow-checking multiplication for integers)
            if (!args.empty())
            {
                Opcode mulOp = (firstArgType == PasTypeKind::Real) ? Opcode::FMul : Opcode::IMulOvf;
                Type ty = (firstArgType == PasTypeKind::Real) ? Type(Type::Kind::F64)
                                                              : Type(Type::Kind::I64);
                Value result = emitBinary(mulOp, ty, args[0], args[0]);
                return {result, ty};
            }
            return {Value::constInt(0), Type(Type::Kind::I64)};
        }

        if (builtin == PascalBuiltin::Randomize)
        {
            // Randomize([seed]) - if no seed provided, use 0 as default
            usedExterns_.insert("rt_randomize_i64");
            Value seed = args.empty() ? Value::constInt(0) : args[0];
            emitCall("rt_randomize_i64", {seed});
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }

        // Handle builtins with runtime symbols
        const char *rtSym = getBuiltinRuntimeSymbol(builtin, firstArgType);
        if (rtSym)
        {
            // Look up the actual runtime signature to get the correct return type
            const auto *rtDesc = il::runtime::findRuntimeDescriptor(rtSym);
            Type rtRetType = Type(Type::Kind::Void);
            if (rtDesc)
            {
                rtRetType = rtDesc->signature.retType;
            }
            else
            {
                // Fallback to Pascal type mapping
                PasType resultPasType = getBuiltinResultType(builtin, firstArgType);
                rtRetType = mapType(resultPasType);
            }

            // Also get the Pascal-expected return type for conversion
            PasType pascalResultType = getBuiltinResultType(builtin, firstArgType);
            Type pascalRetType = mapType(pascalResultType);

            if (rtRetType.kind == Type::Kind::Void)
            {
                emitCall(rtSym, args);
                return {Value::constInt(0), Type(Type::Kind::Void)};
            }
            else
            {
                Value result = emitCallRet(rtRetType, rtSym, args);

                // Convert integer to i1 if Pascal expects Boolean but runtime returns integer
                if (pascalRetType.kind == Type::Kind::I1 &&
                    (rtRetType.kind == Type::Kind::I32 || rtRetType.kind == Type::Kind::I64))
                {
                    // Convert to i1: compare != 0
                    Value zero = Value::constInt(0);
                    result = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), result, zero);
                    return {result, Type(Type::Kind::I1)};
                }

                return {result, rtRetType};
            }
        }
    }

    // Regular function call
    const FuncSignature *sig = sema_->lookupFunction(callee);
    Type retType = sig ? mapType(sig->returnType) : Type(Type::Kind::I64);

    if (retType.kind == Type::Kind::Void)
    {
        emitCall(callee, args);
        return {Value::constInt(0), retType};
    }
    else
    {
        Value result = emitCallRet(retType, callee, args);
        return {result, retType};
    }
}

LowerResult Lowerer::lowerIndex(const IndexExpr &expr)
{
    // Get base type
    PasType baseType = sema_->typeOf(*expr.base);

    if (baseType.kind == PasTypeKind::Array && !expr.indices.empty())
    {
        // Get base address (the array variable's alloca slot)
        if (expr.base->kind == ExprKind::Name)
        {
            const auto &nameExpr = static_cast<const NameExpr &>(*expr.base);
            std::string key = toLower(nameExpr.name);
            auto it = locals_.find(key);
            if (it != locals_.end())
            {
                Value baseAddr = it->second;

                // Get element type and size
                Type elemType = Type(Type::Kind::I64); // Default
                int64_t elemSize = 8;
                if (baseType.elementType)
                {
                    elemType = mapType(*baseType.elementType);
                    elemSize = sizeOf(*baseType.elementType);
                }

                // Calculate offset: index * elemSize
                LowerResult index = lowerExpr(*expr.indices[0]);
                Value offset = emitBinary(Opcode::IMulOvf, Type(Type::Kind::I64),
                                          index.value, Value::constInt(elemSize));

                // GEP to get element address
                Value elemAddr = emitGep(baseAddr, offset);

                // Load the element
                Value result = emitLoad(elemType, elemAddr);
                return {result, elemType};
            }
        }
    }

    // Fallback for other cases
    LowerResult base = lowerExpr(*expr.base);
    (void)base;
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

std::pair<Lowerer::Value, Lowerer::Type> Lowerer::getFieldAddress(
    Value baseAddr, const PasType &baseType, const std::string &fieldName)
{
    std::string fieldKey = toLower(fieldName);

    // Calculate field offset by iterating through fields in order
    // Fields are stored in a map, but we need consistent ordering
    // For simplicity, use alphabetical order (same as iteration order in std::map)
    int64_t offset = 0;
    Type fieldType = Type(Type::Kind::I64);

    for (const auto &[name, typePtr] : baseType.fields)
    {
        if (name == fieldKey)
        {
            if (typePtr)
            {
                fieldType = mapType(*typePtr);
            }
            break;
        }
        // Add size of this field to offset
        if (typePtr)
        {
            offset += sizeOf(*typePtr);
        }
        else
        {
            offset += 8; // Default size
        }
    }

    // GEP to get field address
    Value fieldAddr = emitGep(baseAddr, Value::constInt(offset));
    return {fieldAddr, fieldType};
}

LowerResult Lowerer::lowerField(const FieldExpr &expr)
{
    if (!expr.base)
        return {Value::constInt(0), Type(Type::Kind::I64)};

    // Check for zero-argument constructor call without parentheses: ClassName.Create
    // This happens in Pascal when calling a parameterless constructor
    if (expr.base->kind == ExprKind::Name)
    {
        const auto &nameExpr = static_cast<const NameExpr &>(*expr.base);
        std::string baseName = toLower(nameExpr.name);

        // Check if base is a class type (not a variable)
        if (!locals_.count(baseName))
        {
            auto typeOpt = sema_->lookupType(baseName);
            if (typeOpt && typeOpt->kind == PasTypeKind::Class)
            {
                // This might be a constructor call: ClassName.Create
                auto *classInfo = sema_->lookupClass(baseName);
                if (classInfo)
                {
                    std::string methodKey = toLower(expr.field);
                    auto methodIt = classInfo->methods.find(methodKey);
                    if (methodIt != classInfo->methods.end())
                    {
                        // Found a method - check if it's a constructor
                        // Constructors are typically named "Create" and return void
                        // Treat this as a zero-argument constructor call

                        // Create a synthetic CallExpr for lowering
                        CallExpr syntheticCall(nullptr, {}, expr.loc);
                        syntheticCall.isConstructorCall = true;
                        syntheticCall.constructorClassName = typeOpt->name;

                        // Copy callee from the field expression to get the method name
                        syntheticCall.callee = std::make_unique<FieldExpr>(
                            std::make_unique<NameExpr>(nameExpr.name, nameExpr.loc),
                            expr.field, expr.loc);

                        return lowerConstructorCall(syntheticCall);
                    }
                }
            }
        }
    }

    // Get base type
    PasType baseType = sema_->typeOf(*expr.base);

    if (baseType.kind == PasTypeKind::Record)
    {
        // Records are stored inline in the variable's slot
        if (expr.base->kind == ExprKind::Name)
        {
            const auto &nameExpr = static_cast<const NameExpr &>(*expr.base);
            std::string key = toLower(nameExpr.name);
            auto it = locals_.find(key);
            if (it != locals_.end())
            {
                Value baseAddr = it->second;
                auto [fieldAddr, fieldType] = getFieldAddress(baseAddr, baseType, expr.field);

                // Load the field value
                Value result = emitLoad(fieldType, fieldAddr);
                return {result, fieldType};
            }
        }
        // Handle nested field access (e.g., a.b.c)
        else if (expr.base->kind == ExprKind::Field)
        {
            // Recursively get the base field's address
            // For now, fall through to default handling
        }
    }
    else if (baseType.kind == PasTypeKind::Class)
    {
        // Classes are reference types - the variable's slot contains a pointer to the object
        Value objPtr = Value::null(); // Sentinel value
        bool foundObjPtr = false;

        if (expr.base->kind == ExprKind::Name)
        {
            const auto &nameExpr = static_cast<const NameExpr &>(*expr.base);
            std::string key = toLower(nameExpr.name);
            auto it = locals_.find(key);
            if (it != locals_.end())
            {
                // Load the object pointer from the variable's slot
                objPtr = emitLoad(Type(Type::Kind::Ptr), it->second);
                foundObjPtr = true;
            }
            else if (!currentClassName_.empty())
            {
                // Check if it's a class field accessed inside a method
                auto *classInfo = sema_->lookupClass(toLower(currentClassName_));
                if (classInfo)
                {
                    auto fieldIt = classInfo->fields.find(key);
                    if (fieldIt != classInfo->fields.end())
                    {
                        // Access Self.fieldName to get the object pointer
                        auto selfIt = locals_.find("self");
                        if (selfIt != locals_.end())
                        {
                            Value selfPtr = emitLoad(Type(Type::Kind::Ptr), selfIt->second);

                            // Build type with fields for getFieldAddress
                            PasType selfType = PasType::classType(currentClassName_);
                            for (const auto &[fname, finfo] : classInfo->fields)
                            {
                                selfType.fields[fname] = std::make_shared<PasType>(finfo.type);
                            }
                            auto [fieldAddr, fieldType] = getFieldAddress(selfPtr, selfType, nameExpr.name);

                            // Load the field value (which is a pointer to another object)
                            objPtr = emitLoad(Type(Type::Kind::Ptr), fieldAddr);
                            foundObjPtr = true;
                        }
                    }
                }
            }

            if (!foundObjPtr)
            {
                return {Value::constInt(0), Type(Type::Kind::I64)};
            }
        }
        else if (expr.base->kind == ExprKind::Field)
        {
            // Nested field access: a.b.c where a.b is a class-typed field
            // Recursively lower the base to get the object pointer
            LowerResult baseResult = lowerField(static_cast<const FieldExpr &>(*expr.base));
            objPtr = baseResult.value;
            // The result of lowerField should be a pointer to the nested object
        }
        else
        {
            return {Value::constInt(0), Type(Type::Kind::I64)};
        }

        // Check if this is a zero-argument method call (Pascal allows calling without parens)
        auto *classInfo = sema_->lookupClass(toLower(baseType.name));
        if (classInfo)
        {
            std::string methodKey = toLower(expr.field);
            auto methodIt = classInfo->methods.find(methodKey);
            if (methodIt != classInfo->methods.end())
            {
                // This is a method - check if it can be called with zero arguments
                const auto &methodInfo = methodIt->second;
                if (methodInfo.requiredParams == 0 && methodInfo.returnType.kind != PasTypeKind::Void)
                {
                    // Call the method with just the Self pointer
                    std::string methodName = baseType.name + "." + expr.field;
                    Type retType = mapType(methodInfo.returnType);
                    Value result = emitCallRet(retType, methodName, {objPtr});
                    return {result, retType};
                }
            }

            // Not a method or method requires arguments - check for field access
            PasType classTypeWithFields = baseType;
            for (const auto &[fname, finfo] : classInfo->fields)
            {
                classTypeWithFields.fields[fname] = std::make_shared<PasType>(finfo.type);
            }

            auto [fieldAddr, fieldType] = getFieldAddress(objPtr, classTypeWithFields, expr.field);

            // Load the field value
            Value result = emitLoad(fieldType, fieldAddr);
            return {result, fieldType};
        }

        // No class info - fall through to default field access
        auto [fieldAddr, fieldType] = getFieldAddress(objPtr, baseType, expr.field);
        Value result = emitLoad(fieldType, fieldAddr);
        return {result, fieldType};
    }

    // Fallback
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

//===----------------------------------------------------------------------===//
// Statement Lowering
//===----------------------------------------------------------------------===//

} // namespace il::frontends::pascal
