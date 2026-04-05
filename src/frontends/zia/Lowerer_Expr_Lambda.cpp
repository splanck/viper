//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/zia/Lowerer_Expr_Lambda.cpp
// Purpose: Lambda, block expression, type cast (as), is-expression, and
//          struct literal lowering for the Zia IL lowerer.
// Key invariants:
//   - Lambda functions get unique names (__lambda_N) and closure struct ABI
//   - All lambdas use uniform closure struct: { funcPtr, envPtr }
//   - Context is saved/restored around nested function creation
// Ownership/Lifetime:
//   - Lowerer owns IL builder; lambda functions are added to module_->functions
// Links: src/frontends/zia/Lowerer.hpp, src/frontends/zia/Lowerer_Expr_Complex.cpp
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"

#include <algorithm>
#include <functional>

namespace il::frontends::zia {

using namespace runtime;

/// Closure struct layout: [funcPtr (8 bytes)] [envPtr (8 bytes)]
static constexpr int kClosureSize = 16;
static constexpr int kClosureEnvOffset = 8;

//=============================================================================
// Lambda Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerLambda(LambdaExpr *expr) {
    // Generate unique lambda function name (per-instance counter, not static)
    std::string lambdaName = "__lambda_" + std::to_string(lambdaCounter_++);

    // Check if lambda has captured variables
    bool hasCaptures = !expr->captures.empty();

    // Determine return type (inferred as the body's type if not specified)
    TypeRef returnType = types::unknown();
    if (expr->returnType) {
        returnType = sema_.resolveType(expr->returnType.get());
    } else {
        returnType = sema_.typeOf(expr->body.get());
    }
    Type ilReturnType = mapType(returnType);

    // Build parameter list - always add env pointer as first param for uniform closure ABI
    std::vector<il::core::Param> params;
    params.reserve(expr->params.size() + 1);
    params.push_back({"__env", Type(Type::Kind::Ptr)});
    for (const auto &param : expr->params) {
        TypeRef paramType = param.type ? sema_.resolveType(param.type.get()) : types::unknown();
        params.push_back({param.name, mapType(paramType)});
    }

    // Collect info about captured variables before switching contexts
    // We need to capture their current values/slot pointers
    struct CaptureInfo {
        std::string name;
        Value value;
        Type type;
        TypeRef semType;
        bool isSlot;
    };

    std::vector<CaptureInfo> captureInfos;
    if (hasCaptures) {
        for (const auto &cap : expr->captures) {
            CaptureInfo info;
            info.name = cap.name;
            info.isSlot = false;

            // Look up the variable's type - prefer localTypes_ (set during lowering)
            // over sema_.lookupVarType() which may fail due to scope mismatch
            auto localTypeIt = localTypes_.find(cap.name);
            TypeRef varType = (localTypeIt != localTypes_.end()) ? localTypeIt->second
                                                                 : sema_.lookupVarType(cap.name);

            // Look up the variable in current scope
            auto slotIt = slots_.find(cap.name);
            if (slotIt != slots_.end()) {
                // Load from slot to capture by value
                info.type = varType ? mapType(varType) : Type(Type::Kind::I64);
                info.semType = varType;
                info.value = loadFromSlot(cap.name, info.type);
                info.isSlot = true;
            } else {
                auto localIt = locals_.find(cap.name);
                if (localIt != locals_.end()) {
                    info.value = localIt->second;
                    info.type = varType ? mapType(varType) : Type(Type::Kind::I64);
                    info.semType = varType;
                } else {
                    // Not found - might be a global or error
                    info.value = Value::constInt(0);
                    info.type = Type(Type::Kind::I64);
                    info.semType = types::unknown();
                }
            }
            captureInfos.push_back(info);
        }
    }

    std::vector<size_t> captureOffsets(captureInfos.size(), 0);
    size_t envSize = 0;
    size_t envAlignment = 1;
    if (hasCaptures) {
        for (size_t i = 0; i < captureInfos.size(); ++i) {
            size_t alignment = getILTypeAlignment(captureInfos[i].type);
            envAlignment = std::max(envAlignment, alignment);
            envSize = alignTo(envSize, alignment);
            captureOffsets[i] = envSize;
            envSize += getILTypeSize(captureInfos[i].type);
        }
        envSize = alignTo(envSize, envAlignment);
    }

    // Save current function context (use index instead of pointer to handle vector reallocation)
    TypeRef savedReturnType = currentReturnType_;
    unsigned savedNextTemp = builder_->saveTempId();
    size_t savedFuncIdx = static_cast<size_t>(-1);
    if (currentFunc_) {
        for (size_t i = 0; i < module_->functions.size(); ++i) {
            if (&module_->functions[i] == currentFunc_) {
                savedFuncIdx = i;
                break;
            }
        }
    }
    size_t savedBlockIdx = blockMgr_.currentBlockIndex();
    unsigned savedNextBlockId = blockMgr_.nextBlockId();
    auto savedLocals = std::move(locals_);
    auto savedSlots = std::move(slots_);
    auto savedLocalTypes = std::move(localTypes_);
    auto savedDeferredTemps = std::move(deferredTemps_);
    locals_.clear();
    slots_.clear();
    localTypes_.clear();
    deferredTemps_.clear();

    // Create the lambda function and entry block via IRBuilder so param IDs are assigned.
    currentFunc_ = &builder_->startFunction(lambdaName, ilReturnType, params);
    currentReturnType_ = returnType;
    definedFunctions_.insert(lambdaName);

    blockMgr_.bind(builder_.get(), currentFunc_);

    // Create entry block with the lambda's params as block params.
    builder_->createBlock(*currentFunc_, "entry_0", currentFunc_->params);
    const size_t entryIdx = currentFunc_->blocks.size() - 1;
    setBlock(entryIdx);

    // Load captured variables from the environment struct if we have captures
    const auto &blockParams = currentFunc_->blocks[entryIdx].params;
    // First parameter is always __env (may be null for no-capture lambdas)
    if (hasCaptures) {
        Value envPtr = Value::temp(blockParams[0].id);

        // Load each captured variable from the environment
        for (size_t i = 0; i < captureInfos.size(); ++i) {
            const auto &info = captureInfos[i];

            // GEP to get field address within env struct
            Value fieldAddr = emitGEP(envPtr, static_cast<int64_t>(captureOffsets[i]));

            // Load the captured value
            Value capturedVal = emitLoad(fieldAddr, info.type);

            // Create a slot for mutable captured variables
            createSlot(info.name, info.type);
            storeToSlot(info.name, capturedVal, info.type);
            localTypes_[info.name] = info.semType ? info.semType : types::unknown();
        }
    }

    // Define user parameters as locals (skip __env at index 0)
    for (size_t i = 0; i < expr->params.size(); ++i) {
        size_t paramIdx = i + 1; // Skip __env
        if (paramIdx < blockParams.size()) {
            TypeRef paramType = expr->params[i].type ? sema_.resolveType(expr->params[i].type.get())
                                                     : types::unknown();
            Type ilParamType = mapType(paramType);
            createSlot(expr->params[i].name, ilParamType);
            storeToSlot(expr->params[i].name, Value::temp(blockParams[paramIdx].id), ilParamType);
            localTypes_[expr->params[i].name] = paramType;
        }
    }

    // Lower the body - handle both block expressions and simple expressions
    LowerResult bodyResult{Value::constInt(0), Type(Type::Kind::Void)};
    if (auto *blockExpr = dynamic_cast<BlockExpr *>(expr->body.get())) {
        // Lower each statement in the block
        for (auto &stmt : blockExpr->statements) {
            lowerStmt(stmt.get());
        }
        // The block may have a final value expression
        if (blockExpr->value) {
            bodyResult = lowerExpr(blockExpr->value.get());
        }
    } else {
        bodyResult = lowerExpr(expr->body.get());
    }

    // Return the body result
    if (ilReturnType.kind == Type::Kind::Void) {
        if (!blockMgr_.isTerminated()) {
            emitRetVoid();
        }
    } else {
        if (!blockMgr_.isTerminated()) {
            Value returnValue = bodyResult.value;
            if (returnType && returnType->kind == TypeKindSem::Optional) {
                TypeRef bodyType = sema_.typeOf(expr->body.get());
                if (!bodyType || bodyType->kind != TypeKindSem::Optional) {
                    TypeRef innerType = returnType->innerType();
                    if (innerType)
                        returnValue = emitOptionalWrap(bodyResult.value, innerType);
                }
            }
            emitRet(returnValue);
        }
    }

    // Restore context (use saved index to get fresh pointer after potential vector reallocation)
    if (savedFuncIdx != static_cast<size_t>(-1)) {
        currentFunc_ = &module_->functions[savedFuncIdx];
        blockMgr_.reset(currentFunc_);
        blockMgr_.setNextBlockId(savedNextBlockId);
        blockMgr_.setBlock(savedBlockIdx);
        builder_->restoreTempId(savedNextTemp);
        builder_->restoreFunction(currentFunc_);
    } else {
        currentFunc_ = nullptr;
    }
    locals_ = std::move(savedLocals);
    slots_ = std::move(savedSlots);
    localTypes_ = std::move(savedLocalTypes);
    deferredTemps_ = std::move(savedDeferredTemps);
    currentReturnType_ = savedReturnType;

    // Get the function pointer
    Value funcPtr = Value::global(lambdaName);

    // Always create a uniform closure struct: { funcPtr, envPtr }
    // For no-capture lambdas, envPtr is null

    // Allocate environment if we have captures
    Value envPtr = Value::null(); // null for no captures
    if (hasCaptures) {
        // Allocate environment struct using rt_alloc
        Value envSizeVal = Value::constInt(static_cast<int64_t>(envSize));
        envPtr = emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {envSizeVal});

        // Store captured values into the environment
        for (size_t i = 0; i < captureInfos.size(); ++i) {
            const auto &info = captureInfos[i];
            Value fieldAddr = emitGEP(envPtr, static_cast<int64_t>(captureOffsets[i]));
            emitStore(fieldAddr, info.value, info.type);
        }
    }

    // Allocate closure struct: { ptr funcPtr, ptr envPtr } = 16 bytes
    Value closureSizeVal = Value::constInt(kClosureSize);
    Value closurePtr = emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {closureSizeVal});

    // Store function pointer at offset 0
    emitStore(closurePtr, funcPtr, Type(Type::Kind::Ptr));

    // Store environment pointer at closure env offset
    Value envFieldAddr = emitGEP(closurePtr, kClosureEnvOffset);
    emitStore(envFieldAddr, envPtr, Type(Type::Kind::Ptr));

    return {closurePtr, Type(Type::Kind::Ptr)};
}

//=============================================================================
// Block Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerBlockExpr(BlockExpr *expr) {
    // Lower each statement in the block
    for (auto &stmt : expr->statements) {
        lowerStmt(stmt.get());
    }

    // If there's a trailing value expression, lower it and return
    if (expr->value) {
        return lowerExpr(expr->value.get());
    }

    // No value expression - return void/unit
    return {Value::constInt(0), Type(Type::Kind::Void)};
}

//=============================================================================
// As (Type Cast) Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerAs(AsExpr *expr) {
    // Lower the source value expression
    auto source = lowerExpr(expr->value.get());

    // Resolve the target type
    TypeRef targetType = sema_.resolveType(expr->type.get());
    Type ilTargetType = mapType(targetType);
    TypeRef sourceType = sema_.typeOf(expr->value.get());

    // Numeric conversions require actual IL conversion instructions to avoid
    // raw bit reinterpretation (e.g., f64 bits read as i64 -> garbage).
    if (sourceType && targetType) {
        if (sourceType->kind == TypeKindSem::Number && targetType->kind == TypeKindSem::Integer) {
            // f64 -> i64: checked truncation (traps on NaN/overflow)
            unsigned convId = nextTempId();
            il::core::Instr conv;
            conv.result = convId;
            conv.op = Opcode::CastFpToSiRteChk;
            conv.type = Type(Type::Kind::I64);
            conv.operands = {source.value};
            conv.loc = curLoc_;
            blockMgr_.currentBlock()->instructions.push_back(conv);
            return {Value::temp(convId), conv.type};
        }
        if (sourceType->kind == TypeKindSem::Integer && targetType->kind == TypeKindSem::Number) {
            // i64 -> f64: widening (may lose precision for values > 2^53)
            unsigned convId = nextTempId();
            il::core::Instr conv;
            conv.result = convId;
            conv.op = Opcode::Sitofp;
            conv.type = Type(Type::Kind::F64);
            conv.operands = {source.value};
            conv.loc = curLoc_;
            blockMgr_.currentBlock()->instructions.push_back(conv);
            return {Value::temp(convId), conv.type};
        }
        if (sourceType->kind == TypeKindSem::Integer && targetType->kind == TypeKindSem::Byte) {
            // i64 -> i32 (byte): checked narrowing (traps on overflow)
            unsigned convId = nextTempId();
            il::core::Instr conv;
            conv.result = convId;
            conv.op = Opcode::CastSiNarrowChk;
            conv.type = Type(Type::Kind::I32);
            conv.operands = {source.value};
            conv.loc = curLoc_;
            blockMgr_.currentBlock()->instructions.push_back(conv);
            return {Value::temp(convId), conv.type};
        }
        if (sourceType->kind == TypeKindSem::Byte && targetType->kind == TypeKindSem::Integer) {
            // i32 -> i64: zero-extend widening
            Value widened = widenByteToInteger(source.value);
            return {widened, Type(Type::Kind::I64)};
        }
    }

    // For class/object types, the cast is a no-op at the IL level since all
    // objects are represented as pointers. The semantic analysis already
    // validated the cast is valid.
    return {source.value, ilTargetType};
}

//=============================================================================
// Is Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerIsExpr(IsExpr *expr) {
    // Lower the value being tested
    auto source = lowerExpr(expr->value.get());

    // Resolve the target type name
    TypeRef targetType = sema_.resolveType(expr->type.get());
    if (!targetType) {
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    // Look up the class type info for the target type
    std::string targetName = targetType->name;
    auto it = classTypes_.find(targetName);
    if (it == classTypes_.end()) {
        // Not a class type -- fall back to false
        diag_.report(
            {il::support::Severity::Warning,
             "'is' check against non-class type '" + targetName + "' always evaluates to false",
             expr->loc,
             "W019"});
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    // Collect the target class ID and all descendant class IDs.
    // `obj is T` should return true when obj's runtime type is T or any
    // subclass of T (standard OOP semantics).
    std::vector<int64_t> matchIds;
    std::function<void(const std::string &)> collectDescendants = [&](const std::string &name) {
        auto cit = classTypes_.find(name);
        if (cit == classTypes_.end())
            return;
        matchIds.push_back(static_cast<int64_t>(cit->second.classId));
        for (const auto &[className, info] : classTypes_) {
            if (info.baseClass == name)
                collectDescendants(className);
        }
    };
    collectDescendants(targetName);

    // Emit: classId = call rt_obj_class_id(source)
    Value classId = emitCallRet(Type(Type::Kind::I64), "rt_obj_class_id", {source.value});

    if (matchIds.size() == 1) {
        // Common case: no subclasses — single comparison
        unsigned cmpId = nextTempId();
        il::core::Instr cmpInstr;
        cmpInstr.result = cmpId;
        cmpInstr.op = Opcode::ICmpEq;
        cmpInstr.type = Type(Type::Kind::I1);
        cmpInstr.operands = {classId, Value::constInt(matchIds[0])};
        cmpInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(cmpInstr);
        return {Value::temp(cmpId), Type(Type::Kind::I1)};
    }

    // Multiple classes: emit OR chain of comparisons on i1 values.
    // Zext each i1 comparison to i64, OR them, then trunc back to i1.
    Value accum;
    for (size_t i = 0; i < matchIds.size(); ++i) {
        unsigned cmpId = nextTempId();
        il::core::Instr cmpInstr;
        cmpInstr.result = cmpId;
        cmpInstr.op = Opcode::ICmpEq;
        cmpInstr.type = Type(Type::Kind::I1);
        cmpInstr.operands = {classId, Value::constInt(matchIds[i])};
        cmpInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(cmpInstr);

        // Zext i1 → i64 for the OR chain
        Value ext = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), Value::temp(cmpId));

        if (i == 0) {
            accum = ext;
        } else {
            accum = emitBinary(Opcode::Or, Type(Type::Kind::I64), accum, ext);
        }
    }
    // Trunc i64 → i1 for boolean result
    Value result = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), accum);
    return {result, Type(Type::Kind::I1)};
}

} // namespace il::frontends::zia
