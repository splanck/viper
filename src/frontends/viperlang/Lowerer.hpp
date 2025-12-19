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
#include <unordered_map>
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

    // O(1) lookup maps (built during construction)
    std::unordered_map<std::string, size_t> fieldIndex;
    std::unordered_map<std::string, MethodDecl *> methodMap;

    const FieldLayout *findField(const std::string &n) const
    {
        auto it = fieldIndex.find(n);
        return it != fieldIndex.end() ? &fields[it->second] : nullptr;
    }

    MethodDecl *findMethod(const std::string &n) const
    {
        auto it = methodMap.find(n);
        return it != methodMap.end() ? it->second : nullptr;
    }
};

/// @brief Entity type layout information (reference types).
struct EntityTypeInfo
{
    std::string name;
    std::string baseClass; // Parent class name for inheritance
    std::vector<FieldLayout> fields;
    std::vector<MethodDecl *> methods;
    size_t totalSize; // Size of object data (excluding vtable pointer)
    int classId;      // Runtime class ID for object allocation

    // O(1) lookup maps (built during construction)
    std::unordered_map<std::string, size_t> fieldIndex;
    std::unordered_map<std::string, MethodDecl *> methodMap;

    const FieldLayout *findField(const std::string &n) const
    {
        auto it = fieldIndex.find(n);
        return it != fieldIndex.end() ? &fields[it->second] : nullptr;
    }

    MethodDecl *findMethod(const std::string &n) const
    {
        auto it = methodMap.find(n);
        return it != methodMap.end() ? it->second : nullptr;
    }
};

/// @brief Interface type information.
struct InterfaceTypeInfo
{
    std::string name;
    std::vector<MethodDecl *> methods;

    // O(1) lookup map
    std::unordered_map<std::string, MethodDecl *> methodMap;

    MethodDecl *findMethod(const std::string &n) const
    {
        auto it = methodMap.find(n);
        return it != methodMap.end() ? it->second : nullptr;
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
    std::set<std::string> definedFunctions_; // Functions defined in this module
    std::map<std::string, ValueTypeInfo> valueTypes_;
    std::map<std::string, EntityTypeInfo> entityTypes_;
    std::map<std::string, InterfaceTypeInfo> interfaceTypes_;
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
    void lowerInterfaceDecl(InterfaceDecl &decl);
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
    void lowerMatchStmt(MatchStmt *stmt);

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
    LowerResult lowerTry(TryExpr *expr);
    LowerResult lowerLambda(LambdaExpr *expr);

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

    /// @brief Emit a GEP (get element pointer) instruction.
    Value emitGEP(Value ptr, int64_t offset);

    /// @brief Emit a Load instruction.
    Value emitLoad(Value ptr, Type type);

    /// @brief Emit a Store instruction.
    void emitStore(Value ptr, Value val, Type type);

    /// @brief Emit a field load from a struct pointer.
    Value emitFieldLoad(const FieldLayout *field, Value selfPtr);

    /// @brief Emit a field store to a struct pointer.
    void emitFieldStore(const FieldLayout *field, Value selfPtr, Value val);

    /// @brief Lower a method call given the method decl and base expression result.
    LowerResult lowerMethodCall(MethodDecl *method, const std::string &typeName,
                                Value selfValue, CallExpr *expr);

    //=========================================================================
    // Boxing/Unboxing Helpers
    //=========================================================================

    /// @brief Box a primitive value for collection storage.
    /// @param val The value to box.
    /// @param type The IL type of the value.
    /// @return Pointer to the boxed value.
    Value emitBox(Value val, Type type);

    /// @brief Unbox a value to a primitive type.
    /// @param boxed The boxed pointer value.
    /// @param expectedType The expected IL type.
    /// @return The unboxed value with its type.
    LowerResult emitUnbox(Value boxed, Type expectedType);

    //=========================================================================
    // Type Mapping
    //=========================================================================

    Type mapType(TypeRef type);

    /// @brief Get the size in bytes for an IL type.
    /// @param type The IL type.
    /// @return Size in bytes (8 for 64-bit types, 4 for i32, 2 for i16, 1 for i1).
    static size_t getILTypeSize(Type type);

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

    /// @brief Case-insensitive string comparison for method names.
    static bool equalsIgnoreCase(const std::string &a, const std::string &b);
};

} // namespace il::frontends::viperlang
