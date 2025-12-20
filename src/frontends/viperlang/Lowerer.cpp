//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer.cpp
/// @brief Implementation of ViperLang to IL code generation.
///
/// @details This file implements the Lowerer class which transforms a
/// type-checked ViperLang AST into Viper IL. Key implementation details:
///
/// ## Lowering Process
///
/// The lower() method:
/// 1. Initializes module and IR builder
/// 2. Lowers all declarations to IL functions/globals
/// 3. Emits string constants via stringTable_
/// 4. Declares external runtime functions
///
/// ## Control Flow
///
/// Control flow constructs are lowered to basic blocks:
/// - if: Emit condition, conditional branch to then/else blocks, merge
/// - while: Header block (condition), body block, back-edge to header
/// - for-in: Lower to while loop with iterator variable
/// - match: Chain of conditional branches for patterns
///
/// ## Type Layout
///
/// Value and entity types compute field layouts:
/// - ValueTypeInfo: Inline field layout with total size
/// - EntityTypeInfo: Fields after 16-byte object header, class ID for RTTI
/// - Field offsets computed during type registration
///
/// ## Runtime Integration
///
/// Runtime calls use RuntimeNames.hpp constants. The lowerer:
/// 1. Tracks used external functions in usedExterns_
/// 2. Emits extern declarations for all used runtime functions
/// 3. Uses RuntimeSignatures.hpp for function signatures
///
/// ## Boxing/Unboxing
///
/// For generic collections (List[T], Map[K,V]):
/// - emitBox(): Allocate heap space and store primitive value
/// - emitUnbox(): Load primitive value from boxed pointer
///
/// @see Lowerer.hpp for the class interface
/// @see RuntimeNames.hpp for runtime function name constants
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Lowerer.hpp"
#include "frontends/viperlang/RuntimeNames.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include <cctype>

namespace il::frontends::viperlang
{

using namespace runtime;

Lowerer::Lowerer(Sema &sema) : sema_(sema) {}

Lowerer::Module Lowerer::lower(ModuleDecl &module)
{
    // Initialize state
    module_ = std::make_unique<Module>();
    builder_ = std::make_unique<il::build::IRBuilder>(*module_);
    locals_.clear();
    usedExterns_.clear();
    definedFunctions_.clear();

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
        // Skip functions defined in this module
        if (definedFunctions_.count(externName) > 0)
            continue;

        // Skip methods defined in this module (value type and entity type methods)
        bool isLocalMethod = false;
        auto dotPos = externName.find('.');
        if (dotPos != std::string::npos)
        {
            std::string typeName = externName.substr(0, dotPos);
            if (valueTypes_.find(typeName) != valueTypes_.end() ||
                entityTypes_.find(typeName) != entityTypes_.end())
            {
                isLocalMethod = true;
            }
        }
        if (isLocalMethod)
            continue;

        const auto *desc = il::runtime::findRuntimeDescriptor(externName);
        if (desc)
        {
            builder_->addExtern(
                std::string(desc->name), desc->signature.retType, desc->signature.paramTypes);
        }
        else
        {
            // Fallback: add extern with obj return type for box functions, void for others
            // This ensures the IL is valid even if the runtime descriptor lookup fails
            Type retType = Type(Type::Kind::Void);
            if (externName.find("Box.") != std::string::npos &&
                externName.find("To") == std::string::npos)
            {
                retType = Type(Type::Kind::Ptr); // Boxing returns obj (ptr)
            }
            else if (externName.find("Box.To") != std::string::npos)
            {
                // Unboxing returns the primitive type - use i64 as default
                retType = Type(Type::Kind::I64);
            }
            else if (externName.find(".New") != std::string::npos ||
                     externName.find(".get_") != std::string::npos)
            {
                retType = Type(Type::Kind::Ptr);
            }
            builder_->addExtern(externName, retType, {});
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
        case DeclKind::Value:
            lowerValueDecl(*static_cast<ValueDecl *>(decl));
            break;
        case DeclKind::Entity:
            lowerEntityDecl(*static_cast<EntityDecl *>(decl));
            break;
        case DeclKind::Interface:
            lowerInterfaceDecl(*static_cast<InterfaceDecl *>(decl));
            break;
        default:
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
    params.reserve(decl.params.size());
    for (const auto &param : decl.params)
    {
        TypeRef paramType = param.type ? sema_.resolveType(param.type.get()) : types::unknown();
        params.push_back({param.name, mapType(paramType)});
    }

    // Mangle function name
    std::string mangledName = mangleFunctionName(decl.name);

    // Track this function as defined in this module
    definedFunctions_.insert(mangledName);

    // Create function
    currentFunc_ = &builder_->startFunction(mangledName, ilReturnType, params);
    blockMgr_.bind(builder_.get(), currentFunc_);
    locals_.clear();

    // Create entry block with the function's params as block params
    // (required for proper VM argument passing)
    builder_->createBlock(*currentFunc_, "entry_0", currentFunc_->params);
    size_t entryIdx = currentFunc_->blocks.size() - 1;
    setBlock(entryIdx);

    // Define parameters using slot-based storage for cross-block SSA correctness
    // This ensures parameters are accessible in all basic blocks (if, while, guard, etc.)
    const auto &blockParams = currentFunc_->blocks[entryIdx].params;
    for (size_t i = 0; i < decl.params.size() && i < blockParams.size(); ++i)
    {
        TypeRef paramType =
            decl.params[i].type ? sema_.resolveType(decl.params[i].type.get()) : types::unknown();
        Type ilParamType = mapType(paramType);

        // Create slot and store the parameter value
        createSlot(decl.params[i].name, ilParamType);
        storeToSlot(decl.params[i].name, Value::temp(blockParams[i].id), ilParamType);
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

void Lowerer::lowerValueDecl(ValueDecl &decl)
{
    // Compute field layout
    ValueTypeInfo info;
    info.name = decl.name;
    info.totalSize = 0;

    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Field)
        {
            auto *field = static_cast<FieldDecl *>(member.get());
            TypeRef fieldType =
                field->type ? sema_.resolveType(field->type.get()) : types::unknown();

            FieldLayout layout;
            layout.name = field->name;
            layout.type = fieldType;
            layout.offset = info.totalSize;
            layout.size = getILTypeSize(mapType(fieldType));

            // Add to lookup map before pushing to vector
            info.fieldIndex[field->name] = info.fields.size();
            info.fields.push_back(layout);
            info.totalSize += layout.size;
        }
        else if (member->kind == DeclKind::Method)
        {
            auto *method = static_cast<MethodDecl *>(member.get());
            info.methodMap[method->name] = method;
            info.methods.push_back(method);
        }
    }

    // Store the value type info and get reference for method lowering
    valueTypes_[decl.name] = std::move(info);
    const ValueTypeInfo &storedInfo = valueTypes_[decl.name];

    // Lower all methods
    for (auto *method : storedInfo.methods)
    {
        lowerMethodDecl(*method, decl.name, false);
    }
}

void Lowerer::lowerEntityDecl(EntityDecl &decl)
{
    // Compute field layout (entity fields start after object header)
    EntityTypeInfo info;
    info.name = decl.name;
    info.baseClass = decl.baseClass; // Store parent class for super calls
    info.totalSize = kObjectHeaderSize;
    info.classId = nextClassId_++;

    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Field)
        {
            auto *field = static_cast<FieldDecl *>(member.get());
            TypeRef fieldType =
                field->type ? sema_.resolveType(field->type.get()) : types::unknown();

            FieldLayout layout;
            layout.name = field->name;
            layout.type = fieldType;
            layout.offset = info.totalSize;
            layout.size = getILTypeSize(mapType(fieldType));

            // Add to lookup map before pushing to vector
            info.fieldIndex[field->name] = info.fields.size();
            info.fields.push_back(layout);
            info.totalSize += layout.size;
        }
        else if (member->kind == DeclKind::Method)
        {
            auto *method = static_cast<MethodDecl *>(member.get());
            info.methodMap[method->name] = method;
            info.methods.push_back(method);
        }
    }

    // Store the entity type info and get reference for method lowering
    entityTypes_[decl.name] = std::move(info);
    const EntityTypeInfo &storedInfo = entityTypes_[decl.name];

    // Lower all methods
    for (auto *method : storedInfo.methods)
    {
        lowerMethodDecl(*method, decl.name, true);
    }
}

void Lowerer::lowerInterfaceDecl(InterfaceDecl &decl)
{
    // Store interface information for vtable dispatch
    InterfaceTypeInfo info;
    info.name = decl.name;

    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Method)
        {
            auto *method = static_cast<MethodDecl *>(member.get());
            info.methodMap[method->name] = method;
            info.methods.push_back(method);
        }
    }

    interfaceTypes_[decl.name] = std::move(info);

    // Note: Interface methods are not lowered directly since they're abstract.
    // The implementing entity's methods are called at runtime.
}

void Lowerer::lowerMethodDecl(MethodDecl &decl, const std::string &typeName, bool isEntity)
{
    // Find the type info
    if (isEntity)
    {
        auto it = entityTypes_.find(typeName);
        if (it == entityTypes_.end())
            return;
        currentEntityType_ = &it->second;
        currentValueType_ = nullptr;
    }
    else
    {
        auto it = valueTypes_.find(typeName);
        if (it == valueTypes_.end())
            return;
        currentValueType_ = &it->second;
        currentEntityType_ = nullptr;
    }

    // Determine return type
    TypeRef returnType =
        decl.returnType ? sema_.resolveType(decl.returnType.get()) : types::voidType();
    Type ilReturnType = mapType(returnType);

    // Build parameter list: self (ptr) + declared params
    std::vector<il::core::Param> params;
    params.reserve(decl.params.size() + 1);
    params.push_back({"self", Type(Type::Kind::Ptr)});

    for (const auto &param : decl.params)
    {
        TypeRef paramType = param.type ? sema_.resolveType(param.type.get()) : types::unknown();
        params.push_back({param.name, mapType(paramType)});
    }

    // Mangle method name: TypeName.methodName
    std::string mangledName = typeName + "." + decl.name;

    // Create function
    currentFunc_ = &builder_->startFunction(mangledName, ilReturnType, params);
    blockMgr_.bind(builder_.get(), currentFunc_);
    locals_.clear();

    // Create entry block with the function's params as block params
    // (required for proper VM argument passing)
    builder_->createBlock(*currentFunc_, "entry_0", currentFunc_->params);
    size_t entryIdx = currentFunc_->blocks.size() - 1;
    setBlock(entryIdx);

    // Define parameters using slot-based storage for cross-block SSA correctness
    const auto &blockParams = currentFunc_->blocks[entryIdx].params;
    if (!blockParams.empty())
    {
        // 'self' is first block param - store in slot
        createSlot("self", Type(Type::Kind::Ptr));
        storeToSlot("self", Value::temp(blockParams[0].id), Type(Type::Kind::Ptr));
    }

    // Define other parameters using slot-based storage
    for (size_t i = 0; i < decl.params.size(); ++i)
    {
        // Block param i+1 corresponds to method param i (after self)
        if (i + 1 < blockParams.size())
        {
            TypeRef paramType =
                decl.params[i].type ? sema_.resolveType(decl.params[i].type.get()) : types::unknown();
            Type ilParamType = mapType(paramType);

            createSlot(decl.params[i].name, ilParamType);
            storeToSlot(decl.params[i].name, Value::temp(blockParams[i + 1].id), ilParamType);
        }
    }

    // Lower method body
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
    currentValueType_ = nullptr;
    currentEntityType_ = nullptr;
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
            lowerMatchStmt(static_cast<MatchStmt *>(stmt));
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
    Type ilType;

    if (stmt->initializer)
    {
        auto result = lowerExpr(stmt->initializer.get());
        initValue = result.value;
        ilType = result.type;
    }
    else
    {
        // Default initialization
        TypeRef varType = stmt->type ? sema_.resolveType(stmt->type.get()) : types::unknown();
        ilType = mapType(varType);

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

    // Use slot-based storage for all mutable variables (enables cross-block SSA)
    if (!stmt->isFinal)
    {
        createSlot(stmt->name, ilType);
        storeToSlot(stmt->name, initValue, ilType);
    }
    else
    {
        // Final/immutable variables can use direct SSA values
        defineLocal(stmt->name, initValue);
    }
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

    // Create slot-based loop variable (alloca + initial store)
    // This enables proper SSA across basic block boundaries
    createSlot(stmt->variable, Type(Type::Kind::I64));
    storeToSlot(stmt->variable, startResult.value, Type(Type::Kind::I64));

    // Also store the end value in a slot so it's available in other blocks
    std::string endVar = stmt->variable + "_end";
    createSlot(endVar, Type(Type::Kind::I64));
    storeToSlot(endVar, endResult.value, Type(Type::Kind::I64));

    // Branch to condition
    emitBr(condIdx);

    // Condition: i < end (or <= for inclusive)
    setBlock(condIdx);
    Value loopVar = loadFromSlot(stmt->variable, Type(Type::Kind::I64));
    Value endVal = loadFromSlot(endVar, Type(Type::Kind::I64));
    Value cond;
    if (rangeExpr->inclusive)
    {
        cond = emitBinary(Opcode::SCmpLE, Type(Type::Kind::I1), loopVar, endVal);
    }
    else
    {
        cond = emitBinary(Opcode::SCmpLT, Type(Type::Kind::I1), loopVar, endVal);
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
    Value currentVal = loadFromSlot(stmt->variable, Type(Type::Kind::I64));
    Value nextVal =
        emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), currentVal, Value::constInt(1));
    storeToSlot(stmt->variable, nextVal, Type(Type::Kind::I64));
    emitBr(condIdx);

    loopStack_.pop();
    setBlock(endIdx);

    // Clean up slots
    removeSlot(stmt->variable);
    removeSlot(endVar);
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

void Lowerer::lowerMatchStmt(MatchStmt *stmt)
{
    if (stmt->arms.empty())
        return;

    // Lower the scrutinee once and store in a slot for reuse
    auto scrutinee = lowerExpr(stmt->scrutinee.get());
    std::string scrutineeSlot = "__match_scrutinee";
    createSlot(scrutineeSlot, scrutinee.type);
    storeToSlot(scrutineeSlot, scrutinee.value, scrutinee.type);

    // Create end block for the match
    size_t endIdx = createBlock("match_end");

    // Create blocks for each arm body and the next arm's test
    std::vector<size_t> armBlocks;
    std::vector<size_t> nextTestBlocks;
    for (size_t i = 0; i < stmt->arms.size(); ++i)
    {
        armBlocks.push_back(createBlock("match_arm_" + std::to_string(i)));
        if (i + 1 < stmt->arms.size())
        {
            nextTestBlocks.push_back(createBlock("match_test_" + std::to_string(i + 1)));
        }
        else
        {
            nextTestBlocks.push_back(endIdx); // Last arm falls through to end
        }
    }

    // Lower each arm
    for (size_t i = 0; i < stmt->arms.size(); ++i)
    {
        const auto &arm = stmt->arms[i];

        // In the current block, test the pattern
        Value scrutineeVal = loadFromSlot(scrutineeSlot, scrutinee.type);

        switch (arm.pattern.kind)
        {
            case MatchArm::Pattern::Kind::Wildcard:
                // Wildcard always matches - just jump to arm body
                emitBr(armBlocks[i]);
                break;

            case MatchArm::Pattern::Kind::Literal:
            {
                // Compare scrutinee with literal
                auto litResult = lowerExpr(arm.pattern.literal.get());
                Value cond;
                if (scrutinee.type.kind == Type::Kind::Str)
                {
                    // String comparison
                    cond = emitCallRet(Type(Type::Kind::I1), kStringEquals,
                                       {scrutineeVal, litResult.value});
                }
                else
                {
                    // Integer comparison
                    cond = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1),
                                      scrutineeVal, litResult.value);
                }

                // If guard is present, check it too
                if (arm.pattern.guard)
                {
                    size_t guardBlock = createBlock("match_guard_" + std::to_string(i));
                    emitCBr(cond, guardBlock, nextTestBlocks[i]);
                    setBlock(guardBlock);

                    auto guardResult = lowerExpr(arm.pattern.guard.get());
                    emitCBr(guardResult.value, armBlocks[i], nextTestBlocks[i]);
                }
                else
                {
                    emitCBr(cond, armBlocks[i], nextTestBlocks[i]);
                }
                break;
            }

            case MatchArm::Pattern::Kind::Binding:
            {
                // Binding pattern - bind the scrutinee to a local variable
                // and execute the arm (always matches if reached)
                defineLocal(arm.pattern.binding, scrutineeVal);

                // If guard is present, check it
                if (arm.pattern.guard)
                {
                    auto guardResult = lowerExpr(arm.pattern.guard.get());
                    emitCBr(guardResult.value, armBlocks[i], nextTestBlocks[i]);
                }
                else
                {
                    emitBr(armBlocks[i]);
                }
                break;
            }

            case MatchArm::Pattern::Kind::Constructor:
            case MatchArm::Pattern::Kind::Tuple:
                // TODO: Implement constructor and tuple patterns
                emitBr(nextTestBlocks[i]);
                break;
        }

        // Lower the arm body (arm.body is an expression)
        setBlock(armBlocks[i]);
        if (arm.body)
        {
            // Check if it's a block expression
            if (auto *blockExpr = dynamic_cast<BlockExpr *>(arm.body.get()))
            {
                // Lower each statement in the block
                for (auto &stmt : blockExpr->statements)
                {
                    lowerStmt(stmt.get());
                }
            }
            else
            {
                // It's a regular expression - just evaluate it
                lowerExpr(arm.body.get());
            }
        }

        // Jump to end after arm body (if not already terminated)
        if (!isTerminated())
        {
            emitBr(endIdx);
        }

        // Set up next test block for pattern matching
        if (i + 1 < stmt->arms.size())
        {
            setBlock(nextTestBlocks[i]);
        }
    }

    // Remove the scrutinee slot
    removeSlot(scrutineeSlot);

    // Continue from end block
    setBlock(endIdx);
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
        case ExprKind::SelfExpr:
        {
            Value *selfPtr = lookupLocal("self");
            if (selfPtr)
            {
                return {*selfPtr, Type(Type::Kind::Ptr)};
            }
            return {Value::constInt(0), Type(Type::Kind::Ptr)};
        }
        case ExprKind::SuperExpr:
        {
            // Super returns self pointer but is used for dispatching to parent methods
            Value *selfPtr = lookupLocal("self");
            if (selfPtr)
            {
                return {*selfPtr, Type(Type::Kind::Ptr)};
            }
            return {Value::constInt(0), Type(Type::Kind::Ptr)};
        }
        case ExprKind::Binary:
            return lowerBinary(static_cast<BinaryExpr *>(expr));
        case ExprKind::Unary:
            return lowerUnary(static_cast<UnaryExpr *>(expr));
        case ExprKind::Call:
            return lowerCall(static_cast<CallExpr *>(expr));
        case ExprKind::Field:
            return lowerField(static_cast<FieldExpr *>(expr));
        case ExprKind::New:
            return lowerNew(static_cast<NewExpr *>(expr));
        case ExprKind::Coalesce:
            return lowerCoalesce(static_cast<CoalesceExpr *>(expr));
        case ExprKind::ListLiteral:
            return lowerListLiteral(static_cast<ListLiteralExpr *>(expr));
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
    // Check for slot-based mutable variables first (e.g., loop variables)
    auto slotIt = slots_.find(expr->name);
    if (slotIt != slots_.end())
    {
        TypeRef type = sema_.typeOf(expr);
        Type ilType = mapType(type);
        Value loaded = loadFromSlot(expr->name, ilType);
        return {loaded, ilType};
    }

    Value *local = lookupLocal(expr->name);
    if (local)
    {
        TypeRef type = sema_.typeOf(expr);
        return {*local, mapType(type)};
    }

    // Check for implicit field access (self.field) inside a value type method
    if (currentValueType_)
    {
        const FieldLayout *field = currentValueType_->findField(expr->name);
        if (field)
        {
            Value *selfPtr = lookupLocal("self");
            if (selfPtr)
            {
                Value loaded = emitFieldLoad(field, *selfPtr);
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
            Value *selfPtr = lookupLocal("self");
            if (selfPtr)
            {
                Value loaded = emitFieldLoad(field, *selfPtr);
                return {loaded, mapType(field->type)};
            }
        }
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
            // Check if this is a slot-based variable (e.g., mutable loop variable)
            auto slotIt = slots_.find(ident->name);
            if (slotIt != slots_.end())
            {
                // Store to slot for mutable variables
                storeToSlot(ident->name, right.value, right.type);
                return right;
            }

            // Check for implicit field assignment (self.field) inside a value type method
            if (currentValueType_)
            {
                const FieldLayout *field = currentValueType_->findField(ident->name);
                if (field)
                {
                    Value *selfPtr = lookupLocal("self");
                    if (selfPtr)
                    {
                        emitFieldStore(field, *selfPtr, right.value);
                        return right;
                    }
                }
            }

            // Check for implicit field assignment (self.field) inside an entity method
            if (currentEntityType_)
            {
                const FieldLayout *field = currentEntityType_->findField(ident->name);
                if (field)
                {
                    Value *selfPtr = lookupLocal("self");
                    if (selfPtr)
                    {
                        emitFieldStore(field, *selfPtr, right.value);
                        return right;
                    }
                }
            }

            // Regular variable assignment
            defineLocal(ident->name, right.value);
            return right;
        }

        // Handle index assignment (list[i] = value, map[key] = value)
        if (auto *indexExpr = dynamic_cast<IndexExpr *>(expr->left.get()))
        {
            auto base = lowerExpr(indexExpr->base.get());
            auto index = lowerExpr(indexExpr->index.get());
            TypeRef baseType = sema_.typeOf(indexExpr->base.get());

            if (baseType && baseType->kind == TypeKindSem::Map)
            {
                // Map index assignment: map[key] = value
                Value boxedKey = emitBox(index.value, index.type);
                Value boxedValue = emitBox(right.value, right.type);
                emitCall(kMapSet, {base.value, boxedKey, boxedValue});
                return right;
            }
            else
            {
                // List index assignment: list[i] = value
                Value boxedValue = emitBox(right.value, right.type);
                emitCall(kListSet, {base.value, index.value, boxedValue});
                return right;
            }
        }

        // Handle field assignment (obj.field = value)
        if (auto *fieldExpr = dynamic_cast<FieldExpr *>(expr->left.get()))
        {
            auto base = lowerExpr(fieldExpr->base.get());
            TypeRef baseType = sema_.typeOf(fieldExpr->base.get());

            if (baseType)
            {
                std::string typeName = baseType->name;

                // Check value types
                auto valueIt = valueTypes_.find(typeName);
                if (valueIt != valueTypes_.end())
                {
                    const FieldLayout *field = valueIt->second.findField(fieldExpr->field);
                    if (field)
                    {
                        emitFieldStore(field, base.value, right.value);
                        return right;
                    }
                }

                // Check entity types
                auto entityIt = entityTypes_.find(typeName);
                if (entityIt != entityTypes_.end())
                {
                    const FieldLayout *field = entityIt->second.findField(fieldExpr->field);
                    if (field)
                    {
                        emitFieldStore(field, base.value, right.value);
                        return right;
                    }
                }
            }
        }

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
                // String concatenation - convert right operand to string if needed
                TypeRef rightType = sema_.typeOf(expr->right.get());
                Value rightStr = right.value;

                if (rightType && rightType->kind == TypeKindSem::Integer)
                {
                    // Convert integer to string
                    rightStr = emitCallRet(Type(Type::Kind::Str), runtime::kStringFromInt, {right.value});
                }
                else if (rightType && rightType->kind == TypeKindSem::Number)
                {
                    // Convert number to string
                    rightStr = emitCallRet(Type(Type::Kind::Str), runtime::kStringFromNum, {right.value});
                }
                else if (rightType && rightType->kind == TypeKindSem::Boolean)
                {
                    // Convert boolean to string: use "true" or "false"
                    // For now, just convert 0/1 to string
                    rightStr = emitCallRet(Type(Type::Kind::Str), runtime::kStringFromInt, {right.value});
                }

                Value result = emitCallRet(
                    Type(Type::Kind::Str), runtime::kStringConcat, {left.value, rightStr});
                return {result, Type(Type::Kind::Str)};
            }
            op = isFloat ? Opcode::FAdd : Opcode::IAddOvf;
            break;
        case BinaryOp::Sub:
            op = isFloat ? Opcode::FSub : Opcode::ISubOvf;
            break;
        case BinaryOp::Mul:
            op = isFloat ? Opcode::FMul : Opcode::IMulOvf;
            break;
        case BinaryOp::Div:
            op = isFloat ? Opcode::FDiv : Opcode::SDiv;
            break;
        case BinaryOp::Mod:
            op = Opcode::SRem;
            break;
        case BinaryOp::Eq:
            if (leftType && leftType->kind == TypeKindSem::String)
            {
                // String equality comparison via runtime
                Value result = emitCallRet(
                    Type(Type::Kind::I1), kStringEquals, {left.value, right.value});
                return {result, Type(Type::Kind::I1)};
            }
            op = isFloat ? Opcode::FCmpEQ : Opcode::ICmpEq;
            resultType = Type(Type::Kind::I1);
            break;
        case BinaryOp::Ne:
            if (leftType && leftType->kind == TypeKindSem::String)
            {
                // String inequality: !(a == b)
                Value eqResult = emitCallRet(
                    Type(Type::Kind::I1), kStringEquals, {left.value, right.value});
                Value result =
                    emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), eqResult, Value::constInt(0));
                return {result, Type(Type::Kind::I1)};
            }
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
                    emitBinary(Opcode::ISubOvf, operand.type, Value::constInt(0), operand.value);
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
    // Check for method call on value or entity type: obj.method()
    if (auto *fieldExpr = dynamic_cast<FieldExpr *>(expr->callee.get()))
    {
        // Check for super.method() call - dispatch to parent class method
        if (fieldExpr->base->kind == ExprKind::SuperExpr)
        {
            // Get self pointer
            Value *selfPtr = lookupLocal("self");
            if (selfPtr && currentEntityType_ && !currentEntityType_->baseClass.empty())
            {
                // Look up method in the parent class
                auto parentIt = entityTypes_.find(currentEntityType_->baseClass);
                if (parentIt != entityTypes_.end())
                {
                    if (auto *method = parentIt->second.findMethod(fieldExpr->field))
                    {
                        return lowerMethodCall(method, currentEntityType_->baseClass, *selfPtr, expr);
                    }
                }
            }
        }

        // Get the type of the base expression
        TypeRef baseType = sema_.typeOf(fieldExpr->base.get());
        if (baseType)
        {
            std::string typeName = baseType->name;

            // Check value type methods (O(1) lookup)
            auto it = valueTypes_.find(typeName);
            if (it != valueTypes_.end())
            {
                if (auto *method = it->second.findMethod(fieldExpr->field))
                {
                    auto baseResult = lowerExpr(fieldExpr->base.get());
                    return lowerMethodCall(method, typeName, baseResult.value, expr);
                }
            }

            // Check entity type methods (O(1) lookup)
            auto entityIt = entityTypes_.find(typeName);
            if (entityIt != entityTypes_.end())
            {
                if (auto *method = entityIt->second.findMethod(fieldExpr->field))
                {
                    auto baseResult = lowerExpr(fieldExpr->base.get());
                    return lowerMethodCall(method, typeName, baseResult.value, expr);
                }
            }

            // Check for method call on List type: list.add(), list.get(), etc.
            if (baseType->kind == TypeKindSem::List)
            {
                auto baseResult = lowerExpr(fieldExpr->base.get());
                std::string methodName = fieldExpr->field;

                // Lower arguments with boxing
                std::vector<Value> args;
                args.reserve(expr->args.size() + 1);
                args.push_back(baseResult.value); // list is first argument

                for (auto &arg : expr->args)
                {
                    auto result = lowerExpr(arg.value.get());
                    args.push_back(emitBox(result.value, result.type));
                }

                // Map method names to runtime functions (case-insensitive)
                const char *runtimeFunc = nullptr;
                Type returnType = Type(Type::Kind::Void);

                if (equalsIgnoreCase(methodName, "add"))
                {
                    runtimeFunc = kListAdd;
                }
                else if (equalsIgnoreCase(methodName, "get"))
                {
                    runtimeFunc = kListGet;
                    returnType = Type(Type::Kind::Ptr); // Returns boxed value
                }
                else if (equalsIgnoreCase(methodName, "size") || equalsIgnoreCase(methodName, "count"))
                {
                    runtimeFunc = kListCount;
                    returnType = Type(Type::Kind::I64);
                }
                else if (equalsIgnoreCase(methodName, "clear"))
                {
                    runtimeFunc = kListClear;
                }

                if (runtimeFunc != nullptr)
                {
                    if (returnType.kind == Type::Kind::Void)
                    {
                        emitCall(runtimeFunc, args);
                        return {Value::constInt(0), Type(Type::Kind::Void)};
                    }
                    else if (returnType.kind == Type::Kind::Ptr)
                    {
                        // Returns boxed value, need to unbox based on element type
                        Value boxed = emitCallRet(returnType, runtimeFunc, args);
                        TypeRef elemType = baseType->elementType();
                        if (elemType)
                        {
                            Type ilElemType = mapType(elemType);
                            return emitUnbox(boxed, ilElemType);
                        }
                        // Return the boxed/object value as-is for entity types
                        return {boxed, Type(Type::Kind::Ptr)};
                    }
                    else
                    {
                        Value result = emitCallRet(returnType, runtimeFunc, args);
                        return {result, returnType};
                    }
                }
            }

            // Check for method call on Map type: map.set(), map.get(), map.containsKey(), etc.
            if (baseType->kind == TypeKindSem::Map)
            {
                auto baseResult = lowerExpr(fieldExpr->base.get());
                std::string methodName = fieldExpr->field;

                // Get type args for boxing
                TypeRef keyType = baseType->typeArgs.size() > 0 ? baseType->typeArgs[0] : nullptr;
                TypeRef valueType = baseType->typeArgs.size() > 1 ? baseType->typeArgs[1] : nullptr;

                // Map method names to runtime functions (case-insensitive)
                const char *runtimeFunc = nullptr;
                Type returnType = Type(Type::Kind::Void);

                if (equalsIgnoreCase(methodName, "set") || equalsIgnoreCase(methodName, "put"))
                {
                    // map.set(key, value) - needs two boxed arguments
                    if (expr->args.size() >= 2)
                    {
                        auto keyResult = lowerExpr(expr->args[0].value.get());
                        auto valueResult = lowerExpr(expr->args[1].value.get());
                        Value boxedKey = emitBox(keyResult.value, keyResult.type);
                        Value boxedValue = emitBox(valueResult.value, valueResult.type);
                        emitCall(kMapSet, {baseResult.value, boxedKey, boxedValue});
                        return {Value::constInt(0), Type(Type::Kind::Void)};
                    }
                }
                else if (equalsIgnoreCase(methodName, "get"))
                {
                    // map.get(key) - returns boxed value
                    if (expr->args.size() >= 1)
                    {
                        auto keyResult = lowerExpr(expr->args[0].value.get());
                        Value boxedKey = emitBox(keyResult.value, keyResult.type);
                        Value boxed = emitCallRet(Type(Type::Kind::Ptr), kMapGet,
                                                  {baseResult.value, boxedKey});
                        if (valueType)
                        {
                            Type ilValueType = mapType(valueType);
                            return emitUnbox(boxed, ilValueType);
                        }
                        return {boxed, Type(Type::Kind::Ptr)};
                    }
                }
                else if (equalsIgnoreCase(methodName, "containsKey") || equalsIgnoreCase(methodName, "hasKey"))
                {
                    runtimeFunc = kMapContainsKey;
                    returnType = Type(Type::Kind::I64);
                }
                else if (equalsIgnoreCase(methodName, "size") || equalsIgnoreCase(methodName, "count"))
                {
                    // count() takes no args, just the map
                    Value result = emitCallRet(Type(Type::Kind::I64), kMapCount, {baseResult.value});
                    return {result, Type(Type::Kind::I64)};
                }
                else if (equalsIgnoreCase(methodName, "remove"))
                {
                    runtimeFunc = kMapRemove;
                    returnType = Type(Type::Kind::I64);
                }
                else if (equalsIgnoreCase(methodName, "clear"))
                {
                    emitCall(kMapClear, {baseResult.value});
                    return {Value::constInt(0), Type(Type::Kind::Void)};
                }

                // Handle methods that need boxed key argument
                if (runtimeFunc != nullptr)
                {
                    std::vector<Value> args;
                    args.push_back(baseResult.value);
                    for (auto &arg : expr->args)
                    {
                        auto result = lowerExpr(arg.value.get());
                        args.push_back(emitBox(result.value, result.type));
                    }

                    if (returnType.kind == Type::Kind::Void)
                    {
                        emitCall(runtimeFunc, args);
                        return {Value::constInt(0), Type(Type::Kind::Void)};
                    }
                    else
                    {
                        Value result = emitCallRet(returnType, runtimeFunc, args);
                        return {result, returnType};
                    }
                }
            }
        }
    }

    // Check if this is a resolved runtime call (e.g., Viper.Terminal.Say)
    std::string runtimeCallee = sema_.runtimeCallee(expr);
    if (!runtimeCallee.empty())
    {
        // Lower arguments
        std::vector<Value> args;
        args.reserve(expr->args.size());
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
                            emitCallRet(Type(Type::Kind::Str), kStringFromInt, {arg.value});
                    }
                    else if (argType->kind == TypeKindSem::Number)
                    {
                        strVal =
                            emitCallRet(Type(Type::Kind::Str), kStringFromNum, {arg.value});
                    }
                }

                emitCall(kTerminalSay, {strVal});
            }
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }

        // Check for value type construction
        auto it = valueTypes_.find(ident->name);
        if (it != valueTypes_.end())
        {
            const ValueTypeInfo &info = it->second;

            // Lower arguments
            std::vector<Value> argValues;
            for (auto &arg : expr->args)
            {
                auto result = lowerExpr(arg.value.get());
                argValues.push_back(result.value);
            }

            // Allocate stack space for the value
            unsigned allocaId = nextTempId();
            il::core::Instr allocaInstr;
            allocaInstr.result = allocaId;
            allocaInstr.op = Opcode::Alloca;
            allocaInstr.type = Type(Type::Kind::Ptr);
            allocaInstr.operands = {Value::constInt(static_cast<int64_t>(info.totalSize))};
            blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
            Value ptr = Value::temp(allocaId);

            // Store each argument into the corresponding field
            for (size_t i = 0; i < argValues.size() && i < info.fields.size(); ++i)
            {
                const FieldLayout &field = info.fields[i];

                // GEP to get field address
                unsigned gepId = nextTempId();
                il::core::Instr gepInstr;
                gepInstr.result = gepId;
                gepInstr.op = Opcode::GEP;
                gepInstr.type = Type(Type::Kind::Ptr);
                gepInstr.operands = {ptr, Value::constInt(static_cast<int64_t>(field.offset))};
                blockMgr_.currentBlock()->instructions.push_back(gepInstr);
                Value fieldAddr = Value::temp(gepId);

                // Store the value
                il::core::Instr storeInstr;
                storeInstr.op = Opcode::Store;
                storeInstr.type = mapType(field.type);
                storeInstr.operands = {fieldAddr, argValues[i]};
                blockMgr_.currentBlock()->instructions.push_back(storeInstr);
            }

            // Return pointer to the constructed value
            return {ptr, Type(Type::Kind::Ptr)};
        }
    }

    // Lower arguments
    std::vector<Value> args;
    args.reserve(expr->args.size());
    for (auto &arg : expr->args)
    {
        auto result = lowerExpr(arg.value.get());
        args.push_back(result.value);
    }

    // Get callee name and check if it's a function or a variable holding a function pointer
    std::string calleeName;
    bool isIndirectCall = false;
    Value funcPtr;

    // Check if callee type is a function/lambda type (for closure handling)
    TypeRef calleeType = sema_.typeOf(expr->callee.get());
    bool isLambdaClosure = calleeType && calleeType->isCallable();

    if (auto *ident = dynamic_cast<IdentExpr *>(expr->callee.get()))
    {
        // Check if this is a variable holding a function pointer (not a defined function)
        if (definedFunctions_.find(mangleFunctionName(ident->name)) == definedFunctions_.end())
        {
            // It's a variable - need indirect call
            auto slotIt = slots_.find(ident->name);
            if (slotIt != slots_.end())
            {
                // Load the closure pointer from the slot
                unsigned loadId = nextTempId();
                il::core::Instr loadInstr;
                loadInstr.result = loadId;
                loadInstr.op = Opcode::Load;
                loadInstr.type = Type(Type::Kind::Ptr);
                loadInstr.operands = {slotIt->second};
                blockMgr_.currentBlock()->instructions.push_back(loadInstr);
                funcPtr = Value::temp(loadId);
                isIndirectCall = true;
            }
            else
            {
                // Check in locals
                auto localIt = locals_.find(ident->name);
                if (localIt != locals_.end())
                {
                    funcPtr = localIt->second;
                    isIndirectCall = true;
                }
            }
        }

        if (!isIndirectCall)
        {
            calleeName = mangleFunctionName(ident->name);
        }
    }
    else
    {
        // For other expressions, lower them to get a closure pointer
        auto calleeResult = lowerExpr(expr->callee.get());
        funcPtr = calleeResult.value;
        isIndirectCall = true;
    }

    // Get return type
    TypeRef returnType = calleeType ? calleeType->returnType() : nullptr;
    Type ilReturnType = returnType ? mapType(returnType) : Type(Type::Kind::Void);

    if (isIndirectCall)
    {
        // For lambda closures, unpack the closure struct { funcPtr, envPtr }
        if (isLambdaClosure)
        {
            // funcPtr currently points to the closure struct
            Value closurePtr = funcPtr;

            // Load the actual function pointer from offset 0
            Value actualFuncPtr = emitLoad(closurePtr, Type(Type::Kind::Ptr));

            // Load the environment pointer from offset 8
            Value envFieldAddr = emitGEP(closurePtr, 8);
            Value envPtr = emitLoad(envFieldAddr, Type(Type::Kind::Ptr));

            // Prepend env to args (all lambdas expect __env as first param)
            std::vector<Value> closureArgs;
            closureArgs.reserve(args.size() + 1);
            closureArgs.push_back(envPtr);
            for (const auto &arg : args)
            {
                closureArgs.push_back(arg);
            }

            // Make the indirect call with env as first argument
            if (ilReturnType.kind == Type::Kind::Void)
            {
                emitCallIndirect(actualFuncPtr, closureArgs);
                return {Value::constInt(0), Type(Type::Kind::Void)};
            }
            else
            {
                Value result = emitCallIndirectRet(ilReturnType, actualFuncPtr, closureArgs);
                return {result, ilReturnType};
            }
        }
        else
        {
            // Non-closure indirect call (shouldn't happen often)
            if (ilReturnType.kind == Type::Kind::Void)
            {
                emitCallIndirect(funcPtr, args);
                return {Value::constInt(0), Type(Type::Kind::Void)};
            }
            else
            {
                Value result = emitCallIndirectRet(ilReturnType, funcPtr, args);
                return {result, ilReturnType};
            }
        }
    }
    else
    {
        // Direct call to named function
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
}

LowerResult Lowerer::lowerField(FieldExpr *expr)
{
    // Lower the base expression
    auto base = lowerExpr(expr->base.get());

    // Get the type of the base expression
    TypeRef baseType = sema_.typeOf(expr->base.get());
    if (!baseType)
    {
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    // Check if base is a value type
    std::string typeName = baseType->name;
    auto it = valueTypes_.find(typeName);
    if (it != valueTypes_.end())
    {
        const ValueTypeInfo &info = it->second;
        const FieldLayout *field = info.findField(expr->field);

        if (field)
        {
            // GEP to get field address
            unsigned gepId = nextTempId();
            il::core::Instr gepInstr;
            gepInstr.result = gepId;
            gepInstr.op = Opcode::GEP;
            gepInstr.type = Type(Type::Kind::Ptr);
            gepInstr.operands = {base.value, Value::constInt(static_cast<int64_t>(field->offset))};
            blockMgr_.currentBlock()->instructions.push_back(gepInstr);
            Value fieldAddr = Value::temp(gepId);

            // Load the field value
            Type fieldType = mapType(field->type);
            unsigned loadId = nextTempId();
            il::core::Instr loadInstr;
            loadInstr.result = loadId;
            loadInstr.op = Opcode::Load;
            loadInstr.type = fieldType;
            loadInstr.operands = {fieldAddr};
            blockMgr_.currentBlock()->instructions.push_back(loadInstr);

            return {Value::temp(loadId), fieldType};
        }
    }

    // Check if base is an entity type
    auto entityIt = entityTypes_.find(typeName);
    if (entityIt != entityTypes_.end())
    {
        const EntityTypeInfo &info = entityIt->second;
        const FieldLayout *field = info.findField(expr->field);

        if (field)
        {
            // GEP to get field address
            unsigned gepId = nextTempId();
            il::core::Instr gepInstr;
            gepInstr.result = gepId;
            gepInstr.op = Opcode::GEP;
            gepInstr.type = Type(Type::Kind::Ptr);
            gepInstr.operands = {base.value, Value::constInt(static_cast<int64_t>(field->offset))};
            blockMgr_.currentBlock()->instructions.push_back(gepInstr);
            Value fieldAddr = Value::temp(gepId);

            // Load the field value
            Type fieldType = mapType(field->type);
            unsigned loadId = nextTempId();
            il::core::Instr loadInstr;
            loadInstr.result = loadId;
            loadInstr.op = Opcode::Load;
            loadInstr.type = fieldType;
            loadInstr.operands = {fieldAddr};
            blockMgr_.currentBlock()->instructions.push_back(loadInstr);

            return {Value::temp(loadId), fieldType};
        }
    }

    // Unknown field access
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

LowerResult Lowerer::lowerNew(NewExpr *expr)
{
    // Get the type from the new expression
    TypeRef type = sema_.resolveType(expr->type.get());
    if (!type)
    {
        return {Value::null(), Type(Type::Kind::Ptr)};
    }

    // Handle built-in collection types
    if (type->kind == TypeKindSem::List)
    {
        // Create a new list via runtime
        Value list = emitCallRet(Type(Type::Kind::Ptr), kListNew, {});
        return {list, Type(Type::Kind::Ptr)};
    }
    if (type->kind == TypeKindSem::Set)
    {
        Value set = emitCallRet(Type(Type::Kind::Ptr), kSetNew, {});
        return {set, Type(Type::Kind::Ptr)};
    }
    if (type->kind == TypeKindSem::Map)
    {
        Value map = emitCallRet(Type(Type::Kind::Ptr), kMapNew, {});
        return {map, Type(Type::Kind::Ptr)};
    }

    // Find the entity type info
    std::string typeName = type->name;
    auto it = entityTypes_.find(typeName);
    if (it == entityTypes_.end())
    {
        // Not an entity type
        return {Value::null(), Type(Type::Kind::Ptr)};
    }

    const EntityTypeInfo &info = it->second;

    // Lower arguments
    std::vector<Value> argValues;
    for (auto &arg : expr->args)
    {
        auto result = lowerExpr(arg.value.get());
        argValues.push_back(result.value);
    }

    // Allocate heap memory for the entity using rt_alloc
    Value ptr = emitCallRet(
        Type(Type::Kind::Ptr), "rt_alloc", {Value::constInt(static_cast<int64_t>(info.totalSize))});

    // Store each argument into the corresponding field
    for (size_t i = 0; i < argValues.size() && i < info.fields.size(); ++i)
    {
        const FieldLayout &field = info.fields[i];

        // GEP to get field address
        unsigned gepId = nextTempId();
        il::core::Instr gepInstr;
        gepInstr.result = gepId;
        gepInstr.op = Opcode::GEP;
        gepInstr.type = Type(Type::Kind::Ptr);
        gepInstr.operands = {ptr, Value::constInt(static_cast<int64_t>(field.offset))};
        blockMgr_.currentBlock()->instructions.push_back(gepInstr);
        Value fieldAddr = Value::temp(gepId);

        // Store the value
        il::core::Instr storeInstr;
        storeInstr.op = Opcode::Store;
        storeInstr.type = mapType(field.type);
        storeInstr.operands = {fieldAddr, argValues[i]};
        blockMgr_.currentBlock()->instructions.push_back(storeInstr);
    }

    // Return pointer to the allocated entity
    return {ptr, Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerCoalesce(CoalesceExpr *expr)
{
    // Get the type to determine how to handle the coalesce
    TypeRef leftType = sema_.typeOf(expr->left.get());
    TypeRef resultType = sema_.typeOf(expr);
    Type ilResultType = mapType(resultType);

    // For reference types (entities, etc.), check if the pointer is null
    // For value-type optionals, we would need to check the flag field
    // Currently implementing reference-type coalesce

    // Allocate a stack slot for the result BEFORE branching
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(8)}; // 8 bytes for ptr/i64
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
    Value resultSlot = Value::temp(allocaId);

    // Lower the left expression
    auto left = lowerExpr(expr->left.get());

    // Create blocks for the coalesce
    size_t hasValueIdx = createBlock("coalesce_has");
    size_t isNullIdx = createBlock("coalesce_null");
    size_t mergeIdx = createBlock("coalesce_merge");

    // Check if it's null (for reference types, compare pointer to 0)
    // Note: ICmpNe requires i64 operands, so we convert the pointer via alloca/store/load
    unsigned ptrSlotId = nextTempId();
    il::core::Instr ptrSlotInstr;
    ptrSlotInstr.result = ptrSlotId;
    ptrSlotInstr.op = Opcode::Alloca;
    ptrSlotInstr.type = Type(Type::Kind::Ptr);
    ptrSlotInstr.operands = {Value::constInt(8)};
    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
    Value ptrSlot = Value::temp(ptrSlotId);

    il::core::Instr storePtrInstr;
    storePtrInstr.op = Opcode::Store;
    storePtrInstr.type = Type(Type::Kind::Ptr);
    storePtrInstr.operands = {ptrSlot, left.value};
    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

    unsigned ptrAsI64Id = nextTempId();
    il::core::Instr loadAsI64Instr;
    loadAsI64Instr.result = ptrAsI64Id;
    loadAsI64Instr.op = Opcode::Load;
    loadAsI64Instr.type = Type(Type::Kind::I64);
    loadAsI64Instr.operands = {ptrSlot};
    blockMgr_.currentBlock()->instructions.push_back(loadAsI64Instr);
    Value ptrAsI64 = Value::temp(ptrAsI64Id);

    Value isNotNull =
        emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), ptrAsI64, Value::constInt(0));
    emitCBr(isNotNull, hasValueIdx, isNullIdx);

    // Has value block - store left value and branch to merge
    setBlock(hasValueIdx);
    {
        il::core::Instr storeInstr;
        storeInstr.op = Opcode::Store;
        storeInstr.type = ilResultType;
        storeInstr.operands = {resultSlot, left.value};
        blockMgr_.currentBlock()->instructions.push_back(storeInstr);
    }
    emitBr(mergeIdx);

    // Is null block - evaluate right, store, and branch to merge
    setBlock(isNullIdx);
    auto right = lowerExpr(expr->right.get());
    {
        il::core::Instr storeInstr;
        storeInstr.op = Opcode::Store;
        storeInstr.type = ilResultType;
        storeInstr.operands = {resultSlot, right.value};
        blockMgr_.currentBlock()->instructions.push_back(storeInstr);
    }
    emitBr(mergeIdx);

    // Merge block - load the result
    setBlock(mergeIdx);
    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = ilResultType;
    loadInstr.operands = {resultSlot};
    blockMgr_.currentBlock()->instructions.push_back(loadInstr);

    return {Value::temp(loadId), ilResultType};
}

LowerResult Lowerer::lowerListLiteral(ListLiteralExpr *expr)
{
    // Create a new list
    Value list = emitCallRet(Type(Type::Kind::Ptr), kListNew, {});

    // Add each element to the list (boxed)
    for (auto &elem : expr->elements)
    {
        auto result = lowerExpr(elem.get());
        Value boxed = emitBox(result.value, result.type);
        emitCall(kListAdd, {list, boxed});
    }

    return {list, Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerTuple(TupleExpr *expr)
{
    // Get the tuple type from sema
    TypeRef tupleType = sema_.typeOf(expr);

    // Calculate total size (assuming 8 bytes per element for simplicity)
    size_t tupleSize = tupleType->tupleElementTypes().size() * 8;

    // Allocate space for the tuple on the stack
    unsigned slotId = nextTempId();
    il::core::Instr allocInstr;
    allocInstr.result = slotId;
    allocInstr.op = Opcode::Alloca;
    allocInstr.type = Type(Type::Kind::Ptr);
    allocInstr.operands = {Value::constInt(static_cast<int64_t>(tupleSize))};
    blockMgr_.currentBlock()->instructions.push_back(allocInstr);
    Value tuplePtr = Value::temp(slotId);

    // Store each element in the tuple
    size_t offset = 0;
    for (auto &elem : expr->elements)
    {
        auto result = lowerExpr(elem.get());

        // Calculate element pointer
        Value elemPtr = tuplePtr;
        if (offset > 0)
        {
            unsigned gepId = nextTempId();
            il::core::Instr gepInstr;
            gepInstr.result = gepId;
            gepInstr.op = Opcode::GEP;
            gepInstr.type = Type(Type::Kind::Ptr);
            gepInstr.operands = {tuplePtr, Value::constInt(static_cast<int64_t>(offset))};
            blockMgr_.currentBlock()->instructions.push_back(gepInstr);
            elemPtr = Value::temp(gepId);
        }

        // Store the value
        il::core::Instr storeInstr;
        storeInstr.op = Opcode::Store;
        storeInstr.type = result.type;
        storeInstr.operands = {elemPtr, result.value};
        blockMgr_.currentBlock()->instructions.push_back(storeInstr);

        offset += 8;
    }

    return {tuplePtr, Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerTupleIndex(TupleIndexExpr *expr)
{
    // Lower the tuple expression
    auto tupleResult = lowerExpr(expr->tuple.get());

    // Get the tuple type to determine element type
    TypeRef tupleType = sema_.typeOf(expr->tuple.get());
    TypeRef elemType = tupleType->tupleElementType(expr->index);
    Type ilType = mapType(elemType);

    // Calculate offset (assuming 8 bytes per element)
    size_t offset = expr->index * 8;

    // Calculate element pointer
    Value elemPtr = tupleResult.value;
    if (offset > 0)
    {
        unsigned gepId = nextTempId();
        il::core::Instr gepInstr;
        gepInstr.result = gepId;
        gepInstr.op = Opcode::GEP;
        gepInstr.type = Type(Type::Kind::Ptr);
        gepInstr.operands = {tupleResult.value, Value::constInt(static_cast<int64_t>(offset))};
        blockMgr_.currentBlock()->instructions.push_back(gepInstr);
        elemPtr = Value::temp(gepId);
    }

    // Load the element value
    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = ilType;
    loadInstr.operands = {elemPtr};
    blockMgr_.currentBlock()->instructions.push_back(loadInstr);

    return {Value::temp(loadId), ilType};
}

LowerResult Lowerer::lowerIndex(IndexExpr *expr)
{
    auto base = lowerExpr(expr->base.get());
    auto index = lowerExpr(expr->index.get());

    // Get the base type to determine if it's a List or Map
    TypeRef baseType = sema_.typeOf(expr->base.get());

    Value boxed;
    if (baseType && baseType->kind == TypeKindSem::Map)
    {
        // Map index access - key must be boxed first
        TypeRef keyType = baseType->typeArgs.size() > 0 ? baseType->typeArgs[0] : nullptr;
        Type keyIlType = keyType ? mapType(keyType) : Type(Type::Kind::Ptr);
        Value boxedKey = emitBox(index.value, keyIlType);

        // Call Map.get_Item
        boxed = emitCallRet(Type(Type::Kind::Ptr), kMapGet,
                            {base.value, boxedKey});
    }
    else
    {
        // List index access (default)
        boxed = emitCallRet(Type(Type::Kind::Ptr), kListGet, {base.value, index.value});
    }

    // Get the expected element type from semantic analysis
    TypeRef elemType = sema_.typeOf(expr);
    Type ilType = mapType(elemType);

    return emitUnbox(boxed, ilType);
}

LowerResult Lowerer::lowerTry(TryExpr *expr)
{
    // The ? operator propagates null/error by returning early from the function
    // For now, we implement this for optional types (null propagation)

    auto operand = lowerExpr(expr->operand.get());

    // Create blocks for the null check
    size_t hasValueIdx = createBlock("try.hasvalue");
    size_t returnNullIdx = createBlock("try.returnnull");

    // Check if the value is null (comparing pointer as i64 to 0)
    // First, store the pointer and load as i64 for comparison
    unsigned ptrSlotId = nextTempId();
    il::core::Instr ptrSlotInstr;
    ptrSlotInstr.result = ptrSlotId;
    ptrSlotInstr.op = Opcode::Alloca;
    ptrSlotInstr.type = Type(Type::Kind::Ptr);
    ptrSlotInstr.operands = {Value::constInt(8)};
    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
    Value ptrSlot = Value::temp(ptrSlotId);

    il::core::Instr storePtrInstr;
    storePtrInstr.op = Opcode::Store;
    storePtrInstr.type = Type(Type::Kind::Ptr);
    storePtrInstr.operands = {ptrSlot, operand.value};
    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

    unsigned ptrAsI64Id = nextTempId();
    il::core::Instr loadAsI64Instr;
    loadAsI64Instr.result = ptrAsI64Id;
    loadAsI64Instr.op = Opcode::Load;
    loadAsI64Instr.type = Type(Type::Kind::I64);
    loadAsI64Instr.operands = {ptrSlot};
    blockMgr_.currentBlock()->instructions.push_back(loadAsI64Instr);
    Value ptrAsI64 = Value::temp(ptrAsI64Id);

    Value isNotNull =
        emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), ptrAsI64, Value::constInt(0));
    emitCBr(isNotNull, hasValueIdx, returnNullIdx);

    // Return null block - return null from the current function
    setBlock(returnNullIdx);
    // For functions returning optional types, return null (0 as pointer)
    // For void functions, we just return void
    if (currentFunc_->retType.kind == Type::Kind::Void)
    {
        emitRetVoid();
    }
    else
    {
        // Return null for optional/pointer return types
        emitRet(Value::constInt(0));
    }

    // Has value block - continue with the unwrapped value
    setBlock(hasValueIdx);

    // Return the operand value (it's already unwrapped for simple optionals)
    return operand;
}

LowerResult Lowerer::lowerLambda(LambdaExpr *expr)
{
    // Generate unique lambda function name
    static int lambdaCounter = 0;
    std::string lambdaName = "__lambda_" + std::to_string(lambdaCounter++);

    // Check if lambda has captured variables
    bool hasCaptures = !expr->captures.empty();

    // Determine return type (inferred as the body's type if not specified)
    TypeRef returnType = types::unknown();
    if (expr->returnType)
    {
        returnType = sema_.resolveType(expr->returnType.get());
    }
    else
    {
        returnType = sema_.typeOf(expr->body.get());
    }
    Type ilReturnType = mapType(returnType);

    // Build parameter list - always add env pointer as first param for uniform closure ABI
    std::vector<il::core::Param> params;
    params.reserve(expr->params.size() + 1);
    params.push_back({"__env", Type(Type::Kind::Ptr)});
    for (const auto &param : expr->params)
    {
        TypeRef paramType = param.type ? sema_.resolveType(param.type.get()) : types::unknown();
        params.push_back({param.name, mapType(paramType)});
    }

    // Collect info about captured variables before switching contexts
    // We need to capture their current values/slot pointers
    struct CaptureInfo
    {
        std::string name;
        Value value;
        Type type;
        bool isSlot;
    };
    std::vector<CaptureInfo> captureInfos;
    if (hasCaptures)
    {
        for (const auto &cap : expr->captures)
        {
            CaptureInfo info;
            info.name = cap.name;
            info.isSlot = false;

            // Look up the variable in current scope
            auto slotIt = slots_.find(cap.name);
            if (slotIt != slots_.end())
            {
                // Load from slot to capture by value
                TypeRef varType = sema_.lookupVarType(cap.name);
                info.type = varType ? mapType(varType) : Type(Type::Kind::I64);
                info.value = loadFromSlot(cap.name, info.type);
                info.isSlot = true;
            }
            else
            {
                auto localIt = locals_.find(cap.name);
                if (localIt != locals_.end())
                {
                    info.value = localIt->second;
                    TypeRef varType = sema_.lookupVarType(cap.name);
                    info.type = varType ? mapType(varType) : Type(Type::Kind::I64);
                }
                else
                {
                    // Not found - might be a global or error
                    info.value = Value::constInt(0);
                    info.type = Type(Type::Kind::I64);
                }
            }
            captureInfos.push_back(info);
        }
    }

    // Save current function context (use index instead of pointer to handle vector reallocation)
    size_t savedFuncIdx = static_cast<size_t>(-1);
    if (currentFunc_)
    {
        for (size_t i = 0; i < module_->functions.size(); ++i)
        {
            if (&module_->functions[i] == currentFunc_)
            {
                savedFuncIdx = i;
                break;
            }
        }
    }
    size_t savedBlockIdx = blockMgr_.currentBlockIndex();
    auto savedLocals = std::move(locals_);
    auto savedSlots = std::move(slots_);
    locals_.clear();
    slots_.clear();

    // Create the lambda function
    il::core::Function func;
    func.name = lambdaName;
    func.retType = ilReturnType;

    // Create entry block
    il::core::BasicBlock entryBlock;
    entryBlock.label = "entry_0";
    for (const auto &p : params)
    {
        entryBlock.params.push_back(p);
    }
    func.blocks.push_back(std::move(entryBlock));

    module_->functions.push_back(std::move(func));
    currentFunc_ = &module_->functions.back();
    definedFunctions_.insert(lambdaName);

    blockMgr_.reset(currentFunc_);

    // Load captured variables from the environment struct if we have captures
    const auto &blockParams = currentFunc_->blocks[0].params;
    // First parameter is always __env (may be null for no-capture lambdas)
    if (hasCaptures)
    {
        Value envPtr = Value::temp(blockParams[0].id);

        // Load each captured variable from the environment
        size_t offset = 0;
        for (size_t i = 0; i < captureInfos.size(); ++i)
        {
            const auto &info = captureInfos[i];

            // GEP to get field address within env struct
            Value fieldAddr = emitGEP(envPtr, static_cast<int64_t>(offset));

            // Load the captured value
            Value capturedVal = emitLoad(fieldAddr, info.type);

            // Create a slot for mutable captured variables
            createSlot(info.name, info.type);
            storeToSlot(info.name, capturedVal, info.type);

            // Advance offset by the size of this type
            offset += getILTypeSize(info.type);
        }
    }

    // Define user parameters as locals (skip __env at index 0)
    for (size_t i = 0; i < expr->params.size(); ++i)
    {
        size_t paramIdx = i + 1; // Skip __env
        if (paramIdx < blockParams.size())
        {
            TypeRef paramType = expr->params[i].type
                                    ? sema_.resolveType(expr->params[i].type.get())
                                    : types::unknown();
            Type ilParamType = mapType(paramType);
            createSlot(expr->params[i].name, ilParamType);
            storeToSlot(expr->params[i].name, Value::temp(blockParams[paramIdx].id), ilParamType);
        }
    }

    // Lower the body - handle both block expressions and simple expressions
    LowerResult bodyResult{Value::constInt(0), Type(Type::Kind::Void)};
    if (auto *blockExpr = dynamic_cast<BlockExpr *>(expr->body.get()))
    {
        // Lower each statement in the block
        for (auto &stmt : blockExpr->statements)
        {
            lowerStmt(stmt.get());
        }
        // The block may have a final value expression
        if (blockExpr->value)
        {
            bodyResult = lowerExpr(blockExpr->value.get());
        }
    }
    else
    {
        bodyResult = lowerExpr(expr->body.get());
    }

    // Return the body result
    if (ilReturnType.kind == Type::Kind::Void)
    {
        if (!blockMgr_.isTerminated())
        {
            emitRetVoid();
        }
    }
    else
    {
        if (!blockMgr_.isTerminated())
        {
            emitRet(bodyResult.value);
        }
    }

    // Restore context (use saved index to get fresh pointer after potential vector reallocation)
    if (savedFuncIdx != static_cast<size_t>(-1))
    {
        currentFunc_ = &module_->functions[savedFuncIdx];
        blockMgr_.reset(currentFunc_);
        blockMgr_.setBlock(savedBlockIdx);
    }
    else
    {
        currentFunc_ = nullptr;
    }
    locals_ = std::move(savedLocals);
    slots_ = std::move(savedSlots);

    // Get the function pointer
    Value funcPtr = Value::global(lambdaName);

    // Always create a uniform closure struct: { funcPtr, envPtr }
    // For no-capture lambdas, envPtr is null

    // Allocate environment if we have captures
    Value envPtr = Value::constInt(0); // null for no captures
    if (hasCaptures)
    {
        size_t envSize = 0;
        for (const auto &info : captureInfos)
        {
            envSize += getILTypeSize(info.type);
        }

        // Allocate environment struct using rt_alloc (classId=0 for closures)
        Value classIdVal = Value::constInt(0);
        Value envSizeVal = Value::constInt(static_cast<int64_t>(envSize));
        envPtr = emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {classIdVal, envSizeVal});

        // Store captured values into the environment
        size_t offset = 0;
        for (const auto &info : captureInfos)
        {
            Value fieldAddr = emitGEP(envPtr, static_cast<int64_t>(offset));
            emitStore(fieldAddr, info.value, info.type);
            offset += getILTypeSize(info.type);
        }
    }

    // Allocate closure struct: { ptr funcPtr, ptr envPtr } = 16 bytes
    Value closureClassId = Value::constInt(0);
    Value closureSizeVal = Value::constInt(16);
    Value closurePtr = emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {closureClassId, closureSizeVal});

    // Store function pointer at offset 0
    emitStore(closurePtr, funcPtr, Type(Type::Kind::Ptr));

    // Store environment pointer at offset 8 (null for no captures)
    Value envFieldAddr = emitGEP(closurePtr, 8);
    emitStore(envFieldAddr, envPtr, Type(Type::Kind::Ptr));

    return {closurePtr, Type(Type::Kind::Ptr)};
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

void Lowerer::emitCallIndirect(Value funcPtr, const std::vector<Value> &args)
{
    il::core::Instr instr;
    instr.op = Opcode::CallIndirect;
    instr.type = Type(Type::Kind::Void);
    // For call.indirect, the function pointer is the first operand
    instr.operands.push_back(funcPtr);
    for (const auto &arg : args)
    {
        instr.operands.push_back(arg);
    }
    blockMgr_.currentBlock()->instructions.push_back(instr);
}

Lowerer::Value Lowerer::emitCallIndirectRet(Type retTy, Value funcPtr, const std::vector<Value> &args)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::CallIndirect;
    instr.type = retTy;
    // For call.indirect, the function pointer is the first operand
    instr.operands.push_back(funcPtr);
    for (const auto &arg : args)
    {
        instr.operands.push_back(arg);
    }
    blockMgr_.currentBlock()->instructions.push_back(instr);
    return Value::temp(id);
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
    // Use blockMgr_ directly instead of builder_->emitConstStr to avoid
    // stale curBlock pointer after createBlock reallocates the blocks vector
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::ConstStr;
    instr.type = Type(Type::Kind::Str);
    instr.operands.push_back(Value::global(globalName));
    blockMgr_.currentBlock()->instructions.push_back(instr);
    return Value::temp(id);
}

unsigned Lowerer::nextTempId()
{
    return builder_->reserveTempId();
}

//=============================================================================
// Boxing/Unboxing Helpers
//=============================================================================

Lowerer::Value Lowerer::emitBox(Value val, Type type)
{
    switch (type.kind)
    {
        case Type::Kind::I64:
        case Type::Kind::I32:
        case Type::Kind::I16:
            return emitCallRet(Type(Type::Kind::Ptr), kBoxI64, {val});
        case Type::Kind::F64:
            return emitCallRet(Type(Type::Kind::Ptr), kBoxF64, {val});
        case Type::Kind::I1:
            return emitCallRet(Type(Type::Kind::Ptr), kBoxI1, {val});
        case Type::Kind::Str:
            return emitCallRet(Type(Type::Kind::Ptr), kBoxStr, {val});
        case Type::Kind::Ptr:
            // Objects don't need boxing
            return val;
        default:
            return val;
    }
}

LowerResult Lowerer::emitUnbox(Value boxed, Type expectedType)
{
    switch (expectedType.kind)
    {
        case Type::Kind::I64:
        case Type::Kind::I32:
        case Type::Kind::I16:
        {
            Value unboxed = emitCallRet(Type(Type::Kind::I64), kUnboxI64, {boxed});
            return {unboxed, Type(Type::Kind::I64)};
        }
        case Type::Kind::F64:
        {
            Value unboxed = emitCallRet(Type(Type::Kind::F64), kUnboxF64, {boxed});
            return {unboxed, Type(Type::Kind::F64)};
        }
        case Type::Kind::I1:
        {
            Value unboxed = emitCallRet(Type(Type::Kind::I1), kUnboxI1, {boxed});
            return {unboxed, Type(Type::Kind::I1)};
        }
        case Type::Kind::Str:
        {
            Value unboxed = emitCallRet(Type(Type::Kind::Str), kUnboxStr, {boxed});
            return {unboxed, Type(Type::Kind::Str)};
        }
        case Type::Kind::Ptr:
            // Object references don't need unboxing
            return {boxed, Type(Type::Kind::Ptr)};
        default:
            return {boxed, Type(Type::Kind::Ptr)};
    }
}

//=============================================================================
// Low-Level Instruction Emission
//=============================================================================

Lowerer::Value Lowerer::emitGEP(Value ptr, int64_t offset)
{
    unsigned gepId = nextTempId();
    il::core::Instr gepInstr;
    gepInstr.result = gepId;
    gepInstr.op = Opcode::GEP;
    gepInstr.type = Type(Type::Kind::Ptr);
    gepInstr.operands = {ptr, Value::constInt(offset)};
    blockMgr_.currentBlock()->instructions.push_back(std::move(gepInstr));
    return Value::temp(gepId);
}

Lowerer::Value Lowerer::emitLoad(Value ptr, Type type)
{
    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = type;
    loadInstr.operands = {ptr};
    blockMgr_.currentBlock()->instructions.push_back(std::move(loadInstr));
    return Value::temp(loadId);
}

void Lowerer::emitStore(Value ptr, Value val, Type type)
{
    il::core::Instr storeInstr;
    storeInstr.op = Opcode::Store;
    storeInstr.type = type;
    storeInstr.operands = {ptr, val};
    blockMgr_.currentBlock()->instructions.push_back(std::move(storeInstr));
}

Lowerer::Value Lowerer::emitFieldLoad(const FieldLayout *field, Value selfPtr)
{
    Value fieldAddr = emitGEP(selfPtr, static_cast<int64_t>(field->offset));
    Type fieldType = mapType(field->type);
    return emitLoad(fieldAddr, fieldType);
}

void Lowerer::emitFieldStore(const FieldLayout *field, Value selfPtr, Value val)
{
    Value fieldAddr = emitGEP(selfPtr, static_cast<int64_t>(field->offset));
    Type fieldType = mapType(field->type);
    emitStore(fieldAddr, val, fieldType);
}

LowerResult Lowerer::lowerMethodCall(MethodDecl *method, const std::string &typeName,
                                      Value selfValue, CallExpr *expr)
{
    // Lower method arguments
    std::vector<Value> args;
    args.reserve(expr->args.size() + 1);
    args.push_back(selfValue); // self is first argument

    for (auto &arg : expr->args)
    {
        auto result = lowerExpr(arg.value.get());
        args.push_back(result.value);
    }

    // Get method return type
    TypeRef returnType = method->returnType
                             ? sema_.resolveType(method->returnType.get())
                             : types::voidType();
    Type ilReturnType = mapType(returnType);

    // Call the method: TypeName.methodName
    std::string methodName = typeName + "." + method->name;

    if (ilReturnType.kind == Type::Kind::Void)
    {
        emitCall(methodName, args);
        return {Value::constInt(0), Type(Type::Kind::Void)};
    }
    else
    {
        Value result = emitCallRet(ilReturnType, methodName, args);
        return {result, ilReturnType};
    }
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

size_t Lowerer::getILTypeSize(Type type)
{
    switch (type.kind)
    {
        case Type::Kind::I64:
        case Type::Kind::F64:
        case Type::Kind::Ptr:
        case Type::Kind::Str:
            return 8;
        case Type::Kind::I32:
            return 4;
        case Type::Kind::I16:
            return 2;
        case Type::Kind::I1:
            return 1;
        default:
            return 8;
    }
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
    // Check regular locals first
    auto it = locals_.find(name);
    return it != locals_.end() ? &it->second : nullptr;
}

Lowerer::Value Lowerer::createSlot(const std::string &name, Type type)
{
    // Allocate stack space for the variable
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(8)}; // 8 bytes for i64/f64/ptr
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);

    Value slot = Value::temp(allocaId);
    slots_[name] = slot;
    return slot;
}

void Lowerer::storeToSlot(const std::string &name, Value value, Type type)
{
    auto it = slots_.find(name);
    if (it == slots_.end())
        return;

    il::core::Instr storeInstr;
    storeInstr.op = Opcode::Store;
    storeInstr.type = type;
    storeInstr.operands = {it->second, value};
    blockMgr_.currentBlock()->instructions.push_back(storeInstr);
}

Lowerer::Value Lowerer::loadFromSlot(const std::string &name, Type type)
{
    auto it = slots_.find(name);
    if (it == slots_.end())
        return Value::constInt(0);

    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = type;
    loadInstr.operands = {it->second};
    blockMgr_.currentBlock()->instructions.push_back(loadInstr);

    return Value::temp(loadId);
}

void Lowerer::removeSlot(const std::string &name)
{
    slots_.erase(name);
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

bool Lowerer::equalsIgnoreCase(const std::string &a, const std::string &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

LowerResult Lowerer::lowerBlockExpr(BlockExpr *expr)
{
    // Lower each statement in the block
    for (auto &stmt : expr->statements)
    {
        lowerStmt(stmt.get());
    }

    // If there's a trailing value expression, lower it and return
    if (expr->value)
    {
        return lowerExpr(expr->value.get());
    }

    // No value expression - return void/unit
    return {Value::constInt(0), Type(Type::Kind::Void)};
}

LowerResult Lowerer::lowerMatchExpr(MatchExpr *expr)
{
    if (expr->arms.empty())
    {
        return {Value::constInt(0), Type(Type::Kind::Void)};
    }

    // Lower the scrutinee once and store in a slot for reuse
    auto scrutinee = lowerExpr(expr->scrutinee.get());
    std::string scrutineeSlot = "__match_scrutinee";
    createSlot(scrutineeSlot, scrutinee.type);
    storeToSlot(scrutineeSlot, scrutinee.value, scrutinee.type);

    // Determine the result type from the first arm body
    TypeRef resultType = sema_.typeOf(expr->arms[0].body.get());
    Type ilResultType = mapType(resultType);

    // Create a result slot to store the match result
    std::string resultSlot = "__match_result";
    createSlot(resultSlot, ilResultType);

    // Create end block for the match
    size_t endIdx = createBlock("match_end");

    // Create blocks for each arm body and the next arm's test
    std::vector<size_t> armBlocks;
    std::vector<size_t> nextTestBlocks;
    for (size_t i = 0; i < expr->arms.size(); ++i)
    {
        armBlocks.push_back(createBlock("match_arm_" + std::to_string(i)));
        if (i + 1 < expr->arms.size())
        {
            nextTestBlocks.push_back(createBlock("match_test_" + std::to_string(i + 1)));
        }
        else
        {
            nextTestBlocks.push_back(endIdx); // Last arm falls through to end
        }
    }

    // Lower each arm
    for (size_t i = 0; i < expr->arms.size(); ++i)
    {
        const auto &arm = expr->arms[i];

        // In the current block, test the pattern
        Value scrutineeVal = loadFromSlot(scrutineeSlot, scrutinee.type);

        switch (arm.pattern.kind)
        {
            case MatchArm::Pattern::Kind::Wildcard:
                // Wildcard always matches - just jump to arm body
                emitBr(armBlocks[i]);
                break;

            case MatchArm::Pattern::Kind::Literal:
            {
                // Compare scrutinee with literal
                auto litResult = lowerExpr(arm.pattern.literal.get());
                Value cond;
                if (scrutinee.type.kind == Type::Kind::Str)
                {
                    // String comparison
                    cond = emitCallRet(Type(Type::Kind::I1), kStringEquals,
                                       {scrutineeVal, litResult.value});
                }
                else
                {
                    // Integer comparison
                    cond = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1),
                                      scrutineeVal, litResult.value);
                }

                // If guard is present, check it too
                if (arm.pattern.guard)
                {
                    size_t guardBlock = createBlock("match_guard_" + std::to_string(i));
                    emitCBr(cond, guardBlock, nextTestBlocks[i]);
                    setBlock(guardBlock);

                    auto guardResult = lowerExpr(arm.pattern.guard.get());
                    emitCBr(guardResult.value, armBlocks[i], nextTestBlocks[i]);
                }
                else
                {
                    emitCBr(cond, armBlocks[i], nextTestBlocks[i]);
                }
                break;
            }

            case MatchArm::Pattern::Kind::Binding:
            {
                // Binding pattern - bind the scrutinee to a local variable
                // and execute the arm (always matches if reached)
                defineLocal(arm.pattern.binding, scrutineeVal);

                // If guard is present, check it
                if (arm.pattern.guard)
                {
                    auto guardResult = lowerExpr(arm.pattern.guard.get());
                    emitCBr(guardResult.value, armBlocks[i], nextTestBlocks[i]);
                }
                else
                {
                    emitBr(armBlocks[i]);
                }
                break;
            }

            case MatchArm::Pattern::Kind::Constructor:
            case MatchArm::Pattern::Kind::Tuple:
                // TODO: Implement constructor and tuple patterns
                emitBr(nextTestBlocks[i]);
                break;
        }

        // Lower the arm body and store result
        setBlock(armBlocks[i]);
        if (arm.body)
        {
            auto bodyResult = lowerExpr(arm.body.get());
            storeToSlot(resultSlot, bodyResult.value, ilResultType);
        }

        // Jump to end after arm body (if not already terminated)
        if (!isTerminated())
        {
            emitBr(endIdx);
        }

        // Set up next test block for pattern matching
        if (i + 1 < expr->arms.size())
        {
            setBlock(nextTestBlocks[i]);
        }
    }

    // Remove the scrutinee slot
    removeSlot(scrutineeSlot);

    // Continue from end block
    setBlock(endIdx);

    // Load and return the result
    Value result = loadFromSlot(resultSlot, ilResultType);
    removeSlot(resultSlot);

    return {result, ilResultType};
}

} // namespace il::frontends::viperlang
