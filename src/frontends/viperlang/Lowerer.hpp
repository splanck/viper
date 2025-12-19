//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/viperlang/Lowerer.hpp
// Purpose: IL code generation for ViperLang.
// Key invariants: Produces valid IL from type-checked AST.
// Ownership/Lifetime: Lowerer borrows AST and Sema; produces IL Module.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/common/BlockManager.hpp"
#include "frontends/common/ExprResult.hpp"
#include "frontends/common/LoopContext.hpp"
#include "frontends/common/StringTable.hpp"
#include "frontends/viperlang/AST.hpp"
#include "frontends/viperlang/Sema.hpp"
#include "frontends/viperlang/Types.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace il::frontends::viperlang
{

// Use common ExprResult
using LowerResult = ::il::frontends::common::ExprResult;

/// @brief Field layout for value types.
struct FieldLayout
{
    std::string name;
    TypeRef type;
    size_t offset;
    size_t size;
};

/// @brief Value type layout information.
struct ValueTypeInfo
{
    std::string name;
    std::vector<FieldLayout> fields;
    std::vector<MethodDecl *> methods;
    size_t totalSize;

    const FieldLayout *findField(const std::string &n) const
    {
        for (const auto &f : fields)
            if (f.name == n)
                return &f;
        return nullptr;
    }
};

/// @brief Entity type layout information (reference types).
struct EntityTypeInfo
{
    std::string name;
    std::vector<FieldLayout> fields;
    std::vector<MethodDecl *> methods;
    size_t totalSize; // Size of object data (excluding vtable pointer)
    int classId;      // Runtime class ID for object allocation

    const FieldLayout *findField(const std::string &n) const
    {
        for (const auto &f : fields)
            if (f.name == n)
                return &f;
        return nullptr;
    }
};

/// @brief Lowers ViperLang AST to Viper IL.
class Lowerer
{
  public:
    using Type = il::core::Type;
    using Value = il::core::Value;
    using BasicBlock = il::core::BasicBlock;
    using Function = il::core::Function;
    using Module = il::core::Module;
    using Opcode = il::core::Opcode;

    /// @brief Create a lowerer with semantic information.
    explicit Lowerer(Sema &sema);

    /// @brief Lower a module to IL.
    Module lower(ModuleDecl &module);

  private:
    //=========================================================================
    // State
    //=========================================================================

    Sema &sema_;
    std::unique_ptr<Module> module_;
    std::unique_ptr<il::build::IRBuilder> builder_;
    Function *currentFunc_{nullptr};
    ::il::frontends::common::BlockManager blockMgr_;
    ::il::frontends::common::StringTable stringTable_;
    ::il::frontends::common::LoopContextStack loopStack_;
    std::map<std::string, Value> locals_;
    std::map<std::string, Value> slots_; // Slot pointers for mutable variables
    std::set<std::string> usedExterns_;
    std::map<std::string, ValueTypeInfo> valueTypes_;
    std::map<std::string, EntityTypeInfo> entityTypes_;
    const ValueTypeInfo *currentValueType_{nullptr};
    const EntityTypeInfo *currentEntityType_{nullptr};
    int nextClassId_{1}; // Class ID counter for entity types

    //=========================================================================
    // Block Management
    //=========================================================================

    size_t createBlock(const std::string &base);
    void setBlock(size_t blockIdx);

    BasicBlock &getBlock(size_t idx)
    {
        return blockMgr_.getBlock(idx);
    }

    bool isTerminated() const
    {
        return blockMgr_.isTerminated();
    }

    //=========================================================================
    // Declaration Lowering
    //=========================================================================

    void lowerDecl(Decl *decl);
    void lowerFunctionDecl(FunctionDecl &decl);
    void lowerValueDecl(ValueDecl &decl);
    void lowerEntityDecl(EntityDecl &decl);
    void lowerMethodDecl(MethodDecl &decl, const std::string &typeName, bool isEntity = false);

    //=========================================================================
    // Statement Lowering
    //=========================================================================

    void lowerStmt(Stmt *stmt);
    void lowerBlockStmt(BlockStmt *stmt);
    void lowerExprStmt(ExprStmt *stmt);
    void lowerVarStmt(VarStmt *stmt);
    void lowerIfStmt(IfStmt *stmt);
    void lowerWhileStmt(WhileStmt *stmt);
    void lowerForStmt(ForStmt *stmt);
    void lowerForInStmt(ForInStmt *stmt);
    void lowerReturnStmt(ReturnStmt *stmt);
    void lowerBreakStmt(BreakStmt *stmt);
    void lowerContinueStmt(ContinueStmt *stmt);
    void lowerGuardStmt(GuardStmt *stmt);

    //=========================================================================
    // Expression Lowering
    //=========================================================================

    LowerResult lowerExpr(Expr *expr);
    LowerResult lowerIntLiteral(IntLiteralExpr *expr);
    LowerResult lowerNumberLiteral(NumberLiteralExpr *expr);
    LowerResult lowerStringLiteral(StringLiteralExpr *expr);
    LowerResult lowerBoolLiteral(BoolLiteralExpr *expr);
    LowerResult lowerNullLiteral(NullLiteralExpr *expr);
    LowerResult lowerIdent(IdentExpr *expr);
    LowerResult lowerBinary(BinaryExpr *expr);
    LowerResult lowerUnary(UnaryExpr *expr);
    LowerResult lowerCall(CallExpr *expr);
    LowerResult lowerField(FieldExpr *expr);
    LowerResult lowerNew(NewExpr *expr);
    LowerResult lowerCoalesce(CoalesceExpr *expr);
    LowerResult lowerListLiteral(ListLiteralExpr *expr);
    LowerResult lowerIndex(IndexExpr *expr);

    //=========================================================================
    // Instruction Emission Helpers
    //=========================================================================

    Value emitBinary(Opcode op, Type ty, Value lhs, Value rhs);
    Value emitUnary(Opcode op, Type ty, Value operand);
    Value emitCallRet(Type retTy, const std::string &callee, const std::vector<Value> &args);
    void emitCall(const std::string &callee, const std::vector<Value> &args);
    void emitBr(size_t targetIdx);
    void emitCBr(Value cond, size_t trueIdx, size_t falseIdx);
    void emitRet(Value val);
    void emitRetVoid();
    Value emitConstStr(const std::string &globalName);
    unsigned nextTempId();

    //=========================================================================
    // Type Mapping
    //=========================================================================

    Type mapType(TypeRef type);

    //=========================================================================
    // Local Variable Management
    //=========================================================================

    void defineLocal(const std::string &name, Value value);
    Value *lookupLocal(const std::string &name);

    // Slot-based mutable variable support
    Value createSlot(const std::string &name, Type type);
    void storeToSlot(const std::string &name, Value value, Type type);
    Value loadFromSlot(const std::string &name, Type type);
    void removeSlot(const std::string &name);

    //=========================================================================
    // Helper Functions
    //=========================================================================

    std::string mangleFunctionName(const std::string &name);
    std::string getStringGlobal(const std::string &value);
};

} // namespace il::frontends::viperlang
