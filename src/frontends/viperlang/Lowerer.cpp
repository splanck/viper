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
        default:
            // Interface handled later
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

    // Create entry block with the function's params as block params
    // (required for proper VM argument passing)
    builder_->createBlock(*currentFunc_, "entry_0", currentFunc_->params);
    size_t entryIdx = currentFunc_->blocks.size() - 1;
    setBlock(entryIdx);

    // Define parameters using the actual block param IDs (assigned by createBlock)
    const auto &blockParams = currentFunc_->blocks[entryIdx].params;
    for (size_t i = 0; i < decl.params.size() && i < blockParams.size(); ++i)
    {
        defineLocal(decl.params[i].name, Value::temp(blockParams[i].id));
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
            TypeRef fieldType = field->type ? sema_.resolveType(field->type.get()) : types::unknown();

            FieldLayout layout;
            layout.name = field->name;
            layout.type = fieldType;
            layout.offset = info.totalSize;

            // Determine field size based on type
            Type ilType = mapType(fieldType);
            switch (ilType.kind)
            {
                case Type::Kind::I64:
                case Type::Kind::F64:
                case Type::Kind::Ptr:
                case Type::Kind::Str:
                    layout.size = 8;
                    break;
                case Type::Kind::I32:
                    layout.size = 4;
                    break;
                case Type::Kind::I16:
                    layout.size = 2;
                    break;
                case Type::Kind::I1:
                    layout.size = 1;
                    break;
                default:
                    layout.size = 8;
                    break;
            }

            info.fields.push_back(layout);
            info.totalSize += layout.size;
        }
        else if (member->kind == DeclKind::Method)
        {
            info.methods.push_back(static_cast<MethodDecl *>(member.get()));
        }
    }

    // Store the value type info
    valueTypes_[decl.name] = info;

    // Lower all methods
    for (auto *method : info.methods)
    {
        lowerMethodDecl(*method, decl.name, false);
    }
}

void Lowerer::lowerEntityDecl(EntityDecl &decl)
{
    // Compute field layout (entity fields start after object header)
    // Object header is typically 8 bytes for class ID / refcount
    constexpr size_t headerSize = 8;

    EntityTypeInfo info;
    info.name = decl.name;
    info.totalSize = headerSize;
    info.classId = nextClassId_++;

    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Field)
        {
            auto *field = static_cast<FieldDecl *>(member.get());
            TypeRef fieldType = field->type ? sema_.resolveType(field->type.get()) : types::unknown();

            FieldLayout layout;
            layout.name = field->name;
            layout.type = fieldType;
            layout.offset = info.totalSize;

            // Determine field size based on type
            Type ilType = mapType(fieldType);
            switch (ilType.kind)
            {
                case Type::Kind::I64:
                case Type::Kind::F64:
                case Type::Kind::Ptr:
                case Type::Kind::Str:
                    layout.size = 8;
                    break;
                case Type::Kind::I32:
                    layout.size = 4;
                    break;
                case Type::Kind::I16:
                    layout.size = 2;
                    break;
                case Type::Kind::I1:
                    layout.size = 1;
                    break;
                default:
                    layout.size = 8;
                    break;
            }

            info.fields.push_back(layout);
            info.totalSize += layout.size;
        }
        else if (member->kind == DeclKind::Method)
        {
            info.methods.push_back(static_cast<MethodDecl *>(member.get()));
        }
    }

    // Store the entity type info
    entityTypes_[decl.name] = info;

    // Lower all methods
    for (auto *method : info.methods)
    {
        lowerMethodDecl(*method, decl.name, true);
    }
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

    // Define locals using the actual block param IDs (assigned by createBlock)
    const auto &blockParams = currentFunc_->blocks[entryIdx].params;
    if (!blockParams.empty())
    {
        // 'self' is first block param
        defineLocal("self", Value::temp(blockParams[0].id));
    }

    // Define other parameters using their block param IDs
    for (size_t i = 0; i < decl.params.size(); ++i)
    {
        // Block param i+1 corresponds to method param i (after self)
        if (i + 1 < blockParams.size())
        {
            defineLocal(decl.params[i].name, Value::temp(blockParams[i + 1].id));
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
    Value nextVal = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), currentVal, Value::constInt(1));
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

    // Check for implicit field access (self.field) inside a method
    if (currentValueType_)
    {
        const FieldLayout *field = currentValueType_->findField(expr->name);
        if (field)
        {
            // Get self pointer
            Value *selfPtr = lookupLocal("self");
            if (selfPtr)
            {
                // Emit GEP to get field address
                unsigned gepId = nextTempId();
                il::core::Instr gepInstr;
                gepInstr.result = gepId;
                gepInstr.op = Opcode::GEP;
                gepInstr.type = Type(Type::Kind::Ptr);
                gepInstr.operands = {*selfPtr, Value::constInt(static_cast<int64_t>(field->offset))};
                blockMgr_.currentBlock()->instructions.push_back(gepInstr);
                Value fieldAddr = Value::temp(gepId);

                // Emit Load to get field value
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
    }

    // Check for implicit field access (self.field) inside an entity method
    if (currentEntityType_)
    {
        const FieldLayout *field = currentEntityType_->findField(expr->name);
        if (field)
        {
            // Get self pointer
            Value *selfPtr = lookupLocal("self");
            if (selfPtr)
            {
                // Emit GEP to get field address
                unsigned gepId = nextTempId();
                il::core::Instr gepInstr;
                gepInstr.result = gepId;
                gepInstr.op = Opcode::GEP;
                gepInstr.type = Type(Type::Kind::Ptr);
                gepInstr.operands = {*selfPtr, Value::constInt(static_cast<int64_t>(field->offset))};
                blockMgr_.currentBlock()->instructions.push_back(gepInstr);
                Value fieldAddr = Value::temp(gepId);

                // Emit Load to get field value
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
            }
            else
            {
                defineLocal(ident->name, right.value);
            }
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
    // Check for method call on value type: p.getX()
    if (auto *fieldExpr = dynamic_cast<FieldExpr *>(expr->callee.get()))
    {
        // Get the type of the base expression
        TypeRef baseType = sema_.typeOf(fieldExpr->base.get());
        if (baseType)
        {
            std::string typeName = baseType->name;
            auto it = valueTypes_.find(typeName);
            if (it != valueTypes_.end())
            {
                const ValueTypeInfo &info = it->second;

                // Check if the field name matches a method
                for (auto *method : info.methods)
                {
                    if (method->name == fieldExpr->field)
                    {
                        // Lower the base expression (this is the 'self' pointer)
                        auto baseResult = lowerExpr(fieldExpr->base.get());

                        // Lower method arguments
                        std::vector<Value> args;
                        args.push_back(baseResult.value); // self is first argument

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
                }
            }

            // Check for method call on entity type: e.getX()
            auto entityIt = entityTypes_.find(typeName);
            if (entityIt != entityTypes_.end())
            {
                const EntityTypeInfo &info = entityIt->second;

                // Check if the field name matches a method
                for (auto *method : info.methods)
                {
                    if (method->name == fieldExpr->field)
                    {
                        // Lower the base expression (this is the 'self' pointer)
                        auto baseResult = lowerExpr(fieldExpr->base.get());

                        // Lower method arguments
                        std::vector<Value> args;
                        args.push_back(baseResult.value); // self is first argument

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
    Value ptr =
        emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {Value::constInt(static_cast<int64_t>(info.totalSize))});

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
    Value list = emitCallRet(Type(Type::Kind::Ptr), "Viper.Collections.List.New", {});

    // Add each element to the list (boxed)
    for (auto &elem : expr->elements)
    {
        auto result = lowerExpr(elem.get());

        // Box the element based on its type
        Value boxed;
        switch (result.type.kind)
        {
            case Type::Kind::I64:
            case Type::Kind::I32:
            case Type::Kind::I16:
                boxed = emitCallRet(Type(Type::Kind::Ptr), "Viper.Box.I64", {result.value});
                break;
            case Type::Kind::F64:
                boxed = emitCallRet(Type(Type::Kind::Ptr), "Viper.Box.F64", {result.value});
                break;
            case Type::Kind::I1:
                boxed = emitCallRet(Type(Type::Kind::Ptr), "Viper.Box.I1", {result.value});
                break;
            case Type::Kind::Str:
                boxed = emitCallRet(Type(Type::Kind::Ptr), "Viper.Box.Str", {result.value});
                break;
            case Type::Kind::Ptr:
                // Objects don't need boxing, use directly
                boxed = result.value;
                break;
            default:
                boxed = result.value;
                break;
        }

        // Add to list
        emitCall("Viper.Collections.List.Add", {list, boxed});
    }

    return {list, Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerIndex(IndexExpr *expr)
{
    auto base = lowerExpr(expr->base.get());
    auto index = lowerExpr(expr->index.get());

    // Assume this is a List - call get_Item
    // Result is a boxed value that needs unboxing based on expected type
    Value boxed = emitCallRet(Type(Type::Kind::Ptr), "Viper.Collections.List.get_Item",
                              {base.value, index.value});

    // Get the expected element type from semantic analysis
    TypeRef elemType = sema_.typeOf(expr);
    Type ilType = mapType(elemType);

    // Unbox based on expected type
    switch (ilType.kind)
    {
        case Type::Kind::I64:
        case Type::Kind::I32:
        case Type::Kind::I16:
        {
            Value unboxed = emitCallRet(Type(Type::Kind::I64), "Viper.Box.ToI64", {boxed});
            return {unboxed, Type(Type::Kind::I64)};
        }
        case Type::Kind::F64:
        {
            Value unboxed = emitCallRet(Type(Type::Kind::F64), "Viper.Box.ToF64", {boxed});
            return {unboxed, Type(Type::Kind::F64)};
        }
        case Type::Kind::I1:
        {
            Value unboxed = emitCallRet(Type(Type::Kind::I1), "Viper.Box.ToI1", {boxed});
            return {unboxed, Type(Type::Kind::I1)};
        }
        case Type::Kind::Str:
        {
            Value unboxed = emitCallRet(Type(Type::Kind::Str), "Viper.Box.ToStr", {boxed});
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

} // namespace il::frontends::viperlang
