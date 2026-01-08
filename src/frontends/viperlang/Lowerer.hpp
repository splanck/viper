//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer.hpp
/// @brief IL code generation for the ViperLang programming language.
///
/// @details The Lowerer transforms a type-checked ViperLang AST into Viper
/// Intermediate Language (IL). This is the final stage of the frontend
/// pipeline before the IL is passed to the VM or code generator.
///
/// ## Lowering Pipeline
///
/// The lowering process follows this order:
///
/// 1. **Type Layout Computation**
///    - Compute field offsets for value and entity types
///    - Build method tables for dispatch
///    - Assign class IDs to entity types
///
/// 2. **Declaration Lowering**
///    - Lower global variables to IL global slots
///    - Lower function declarations to IL functions
///    - Lower type declarations (methods, constructors)
///
/// 3. **Statement and Expression Lowering**
///    - Lower each function body to IL instructions
///    - Convert control flow to basic blocks with branches
///    - Generate calls to runtime library functions
///
/// ## IL Generation Concepts
///
/// **Basic Blocks:**
/// IL uses a control flow graph of basic blocks. Each block contains
/// a sequence of instructions ending with a terminator (branch, return).
/// The BlockManager helper tracks blocks and termination state.
///
/// **Values and Slots:**
/// - SSA values: Immutable results of instructions
/// - Slots: Stack-allocated mutable storage (for var declarations)
///
/// **Type Mapping:**
/// ViperLang types are mapped to IL types:
/// - Integer -> i64
/// - Number -> f64
/// - Boolean -> i64 (0 or 1)
/// - String -> ptr (runtime string reference)
/// - Entity/Collections -> ptr (heap-allocated objects)
///
/// ## Runtime Integration
///
/// The lowerer generates calls to the Viper runtime for:
/// - Object allocation and deallocation
/// - String operations
/// - Collection operations (List, Map, Set)
/// - I/O (Terminal functions)
/// - Type checking and casting
///
/// Runtime function names follow the pattern `Viper.Category.Function`,
/// which are resolved to runtime symbols during linking.
///
/// ## Usage Example
///
/// ```cpp
/// // After parsing and semantic analysis
/// Sema sema(diag);
/// sema.analyze(*module);
///
/// // Lower to IL
/// Lowerer lowerer(sema);
/// auto ilModule = lowerer.lower(*module);
///
/// // The IL module can now be executed or compiled
/// ```
///
/// @invariant Input AST must be type-checked (Sema::analyze successful).
/// @invariant Produced IL is well-formed and valid.
/// @invariant All runtime calls use RuntimeNames.hpp constants.
///
/// @see AST.hpp - Input AST types
/// @see Sema.hpp - Semantic analysis results
/// @see RuntimeNames.hpp - Runtime function name constants
/// @see il/core/Module.hpp - Output IL module
///
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/common/BlockManager.hpp"
#include "frontends/common/ExprResult.hpp"
#include "frontends/common/LoopContext.hpp"
#include "frontends/common/StringTable.hpp"
#include "frontends/viperlang/AST.hpp"
#include "frontends/viperlang/Options.hpp"
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

//===----------------------------------------------------------------------===//
/// @name Result Type
/// @brief Type alias for expression lowering results.
/// @{
//===----------------------------------------------------------------------===//

/// @brief Result of lowering an expression.
/// @details Contains both the IL value and its type information.
/// Used to track expression results through lowering.
using LowerResult = ::il::frontends::common::ExprResult;

/// @}

//===----------------------------------------------------------------------===//
/// @name Type Layout Structures
/// @brief Structures for computing and storing type layout information.
/// @{
//===----------------------------------------------------------------------===//

/// @brief Field layout information within a type.
/// @details Describes a single field's position and size in memory.
/// Used for generating field access instructions.
struct FieldLayout
{
    /// @brief The field name as declared.
    std::string name;

    /// @brief The semantic type of the field.
    TypeRef type;

    /// @brief Byte offset from the start of the struct.
    /// @details For entity types, this is the offset after the object header.
    size_t offset;

    /// @brief Size in bytes of this field.
    size_t size;
};

/// @brief Value type layout information.
/// @details Contains everything needed to generate code for a value type:
/// field layout for access, methods for calls, and total size for copying.
///
/// ## Memory Layout
///
/// Value types are laid out inline (no header):
/// ```
/// [field0][field1][field2]...
/// ```
///
/// ## Method Dispatch
///
/// Value type methods are statically dispatched by mangled name.
struct ValueTypeInfo
{
    /// @brief The type name.
    std::string name;

    /// @brief Fields in declaration order with computed offsets.
    std::vector<FieldLayout> fields;

    /// @brief Methods declared in this type.
    std::vector<MethodDecl *> methods;

    /// @brief Total size in bytes of an instance.
    size_t totalSize;

    /// @brief Fast lookup: field name -> index in fields vector.
    std::unordered_map<std::string, size_t> fieldIndex;

    /// @brief Fast lookup: method name -> method declaration.
    std::unordered_map<std::string, MethodDecl *> methodMap;

    /// @brief Find a field by name.
    /// @param n The field name.
    /// @return Pointer to FieldLayout, or nullptr if not found.
    const FieldLayout *findField(const std::string &n) const
    {
        auto it = fieldIndex.find(n);
        return it != fieldIndex.end() ? &fields[it->second] : nullptr;
    }

    /// @brief Find a method by name.
    /// @param n The method name.
    /// @return Pointer to MethodDecl, or nullptr if not found.
    MethodDecl *findMethod(const std::string &n) const
    {
        auto it = methodMap.find(n);
        return it != methodMap.end() ? it->second : nullptr;
    }
};

/// @brief Entity type layout information (reference types).
/// @details Contains everything needed to generate code for an entity type:
/// field layout, methods, inheritance info, and runtime class ID.
///
/// ## Memory Layout
///
/// Entity types are heap-allocated with an object header:
/// ```
/// [header: classId, refCount][field0][field1]...
/// ```
///
/// The header is 16 bytes (8 for class ID, 8 for reference count).
///
/// ## Inheritance
///
/// If baseClass is set, inherited fields come first, followed by
/// this class's fields. The classId is unique per type for RTTI.
///
/// ## Method Dispatch
///
/// Entity methods use static dispatch currently (no vtable yet).
/// The classId enables future virtual dispatch and `is`/`as` checks.
struct EntityTypeInfo
{
    /// @brief The type name.
    std::string name;

    /// @brief Parent class name (empty if no inheritance).
    std::string baseClass;

    /// @brief Fields in declaration order with computed offsets.
    /// @details Inherited fields (if any) come first.
    std::vector<FieldLayout> fields;

    /// @brief Methods declared in this type.
    std::vector<MethodDecl *> methods;

    /// @brief Total size in bytes of object data (excluding header).
    size_t totalSize;

    /// @brief Runtime class ID for object allocation and RTTI.
    /// @details Unique per entity type, assigned during lowering.
    int classId;

    /// @brief Fast lookup: field name -> index in fields vector.
    std::unordered_map<std::string, size_t> fieldIndex;

    /// @brief Fast lookup: method name -> method declaration.
    std::unordered_map<std::string, MethodDecl *> methodMap;

    /// @brief vtable method entries in vtable order (includes inherited methods).
    /// @details Each entry is the fully qualified method name (e.g., "Dog.speak").
    std::vector<std::string> vtable;

    /// @brief vtable slot lookup: method name -> vtable index.
    std::unordered_map<std::string, size_t> vtableIndex;

    /// @brief Name of the global vtable symbol.
    std::string vtableName;

    /// @brief Set of interfaces this entity implements.
    /// @details Used for interface method dispatch.
    std::set<std::string> implementedInterfaces;

    /// @brief Find a field by name.
    /// @param n The field name.
    /// @return Pointer to FieldLayout, or nullptr if not found.
    const FieldLayout *findField(const std::string &n) const
    {
        auto it = fieldIndex.find(n);
        return it != fieldIndex.end() ? &fields[it->second] : nullptr;
    }

    /// @brief Find a method by name.
    /// @param n The method name.
    /// @return Pointer to MethodDecl, or nullptr if not found.
    MethodDecl *findMethod(const std::string &n) const
    {
        auto it = methodMap.find(n);
        return it != methodMap.end() ? it->second : nullptr;
    }

    /// @brief Find vtable index for a method.
    /// @param n The method name.
    /// @return vtable index, or SIZE_MAX if not found.
    size_t findVtableSlot(const std::string &n) const
    {
        auto it = vtableIndex.find(n);
        return it != vtableIndex.end() ? it->second : SIZE_MAX;
    }
};

/// @brief Interface type information.
/// @details Contains method signatures for interface types.
/// Used for generating interface method calls via vtable dispatch.
///
/// ## Interface Dispatch
///
/// When calling a method on an interface-typed value, the lowerer
/// generates a vtable lookup and indirect call. Each implementing
/// type has a vtable with method pointers at fixed slots.
struct InterfaceTypeInfo
{
    /// @brief The interface name.
    std::string name;

    /// @brief Method declarations (signatures only, no bodies).
    std::vector<MethodDecl *> methods;

    /// @brief Fast lookup: method name -> method declaration.
    std::unordered_map<std::string, MethodDecl *> methodMap;

    /// @brief Find a method by name.
    /// @param n The method name.
    /// @return Pointer to MethodDecl, or nullptr if not found.
    MethodDecl *findMethod(const std::string &n) const
    {
        auto it = methodMap.find(n);
        return it != methodMap.end() ? it->second : nullptr;
    }
};

/// @}

//===----------------------------------------------------------------------===//
/// @name IL Lowerer
/// @{
//===----------------------------------------------------------------------===//

/// @brief Lowers ViperLang AST to Viper IL.
/// @details Transforms a semantically analyzed AST into IL instructions.
/// The lowerer produces a complete IL Module that can be executed by the
/// VM or compiled to native code.
///
/// ## Lowering Process
///
/// The lower() method performs:
/// 1. Type registration - compute layouts for all types
/// 2. Function lowering - lower each function declaration
/// 3. String table finalization - emit string constants
/// 4. External function declarations - declare used runtime functions
///
/// ## Control Flow Lowering
///
/// Control flow constructs are lowered to basic blocks:
/// - `if`: conditional branch to then/else blocks, merge after
/// - `while`: header block with loop condition, body, back edge
/// - `for-in`: lowered to while with iterator variable
/// - `match`: chain of conditional branches for each arm
///
/// ## Value vs Reference Handling
///
/// - Value types: Inline storage, copied on assignment
/// - Entity types: Pointer storage, reference counted
/// - Optionals of reference types: null pointer represents none
///
/// @invariant sema_ must have successfully analyzed the input module.
class Lowerer
{
  public:
    /// @brief IL core type aliases for convenience.
    /// @{
    using Type = il::core::Type;
    using Value = il::core::Value;
    using BasicBlock = il::core::BasicBlock;
    using Function = il::core::Function;
    using Module = il::core::Module;
    using Opcode = il::core::Opcode;
    /// @}

    /// @brief Create a lowerer with semantic analysis results.
    /// @param sema The semantic analyzer that analyzed the input module.
    ///
    /// @details The lowerer uses sema for:
    /// - Expression type lookup (sema.typeOf)
    /// - Runtime function resolution (sema.runtimeCallee)
    explicit Lowerer(Sema &sema, CompilerOptions options = {});

    /// @brief Lower a module to IL.
    /// @param module The parsed and analyzed module.
    /// @return The IL Module (moved from internal storage).
    ///
    /// @details Performs complete lowering:
    /// 1. Compute type layouts
    /// 2. Lower all declarations
    /// 3. Emit string constants
    /// 4. Declare external functions
    Module lower(ModuleDecl &module);

  private:
    //=========================================================================
    /// @name State
    /// @brief Internal state maintained during lowering.
    /// @{
    //=========================================================================

    /// @brief Semantic analysis results.
    Sema &sema_;

    /// @brief Frontend compilation options.
    CompilerOptions options_;

    /// @brief The IL module being constructed.
    std::unique_ptr<Module> module_;

    /// @brief IR builder for emitting instructions.
    std::unique_ptr<il::build::IRBuilder> builder_;

    /// @brief Current function being lowered.
    Function *currentFunc_{nullptr};

    /// @brief Current function return type (semantic).
    TypeRef currentReturnType_{nullptr};

    /// @brief Basic block manager for the current function.
    ::il::frontends::common::BlockManager blockMgr_;

    /// @brief String constant table.
    ::il::frontends::common::StringTable stringTable_;

    /// @brief Loop context stack for break/continue.
    ::il::frontends::common::LoopContextStack loopStack_;

    /// @brief Local variable bindings: name -> SSA value.
    std::map<std::string, Value> locals_;

    /// @brief Local variable types: name -> semantic type.
    std::map<std::string, TypeRef> localTypes_;

    /// @brief Mutable variable slots: name -> slot pointer.
    std::map<std::string, Value> slots_;

    /// @brief External functions used (for declaration).
    std::set<std::string> usedExterns_;

    /// @brief Functions defined in this module.
    std::set<std::string> definedFunctions_;

    /// @brief Value type layout information.
    std::map<std::string, ValueTypeInfo> valueTypes_;

    /// @brief Entity type layout information.
    std::map<std::string, EntityTypeInfo> entityTypes_;

    /// @brief Interface type information.
    std::map<std::string, InterfaceTypeInfo> interfaceTypes_;

    /// @brief Current value type context (for self access).
    const ValueTypeInfo *currentValueType_{nullptr};

    /// @brief Current entity type context (for self access).
    const EntityTypeInfo *currentEntityType_{nullptr};

    /// @brief Counter for assigning unique class IDs.
    int nextClassId_{1};

    /// @brief Global constant values: name -> IL value.
    /// @details Stores the lowered values of module-level constants
    /// (e.g., `Integer GAME_WIDTH = 70;`). Used during identifier
    /// resolution to replace constant references with their values.
    std::map<std::string, Value> globalConstants_;

    /// @brief Global mutable variable types: name -> semantic type.
    /// @details Stores the types of module-level mutable variables
    /// (e.g., `var running: Boolean;`). Used for generating runtime
    /// storage access calls.
    std::map<std::string, TypeRef> globalVariables_;

    /// @brief Initial values for mutable global variables with literal initializers.
    /// @details Stores literal initializer values that need to be stored to
    /// runtime storage during module initialization (e.g., `var counter = 10;`).
    std::map<std::string, Value> globalInitializers_;

    /// @}
    //=========================================================================
    /// @name Block Management
    /// @brief Methods for managing basic blocks.
    /// @{
    //=========================================================================

    /// @brief Create a new basic block.
    /// @param base Base name for the block (e.g., "if.then").
    /// @return Index of the created block.
    size_t createBlock(const std::string &base);

    /// @brief Set the current block for instruction emission.
    /// @param blockIdx Index of the block to make current.
    void setBlock(size_t blockIdx);

    /// @brief Get a block by index.
    /// @param idx Block index.
    /// @return Reference to the BasicBlock.
    BasicBlock &getBlock(size_t idx)
    {
        return blockMgr_.getBlock(idx);
    }

    /// @brief Check if current block is terminated.
    /// @return True if the current block has a terminator.
    bool isTerminated() const
    {
        return blockMgr_.isTerminated();
    }

    /// @}
    //=========================================================================
    /// @name Declaration Lowering
    /// @brief Methods for lowering declarations.
    /// @{
    //=========================================================================

    /// @brief Lower any declaration (dispatcher).
    /// @param decl The declaration to lower.
    void lowerDecl(Decl *decl);

    /// @brief Lower a function declaration.
    /// @param decl The function declaration.
    void lowerFunctionDecl(FunctionDecl &decl);

    /// @brief Lower a value type declaration.
    /// @param decl The value type declaration.
    void lowerValueDecl(ValueDecl &decl);

    /// @brief Lower an entity type declaration.
    /// @param decl The entity type declaration.
    void lowerEntityDecl(EntityDecl &decl);

    /// @brief Emit vtable global for an entity type.
    /// @param info The entity type info with vtable entries.
    void emitVtable(const EntityTypeInfo &info);

    /// @brief Lower an interface declaration.
    /// @param decl The interface declaration.
    void lowerInterfaceDecl(InterfaceDecl &decl);

    /// @brief Lower a global variable declaration.
    /// @param decl The global variable declaration.
    /// @details Handles module-level constants by storing their values in
    /// globalConstants_ for later resolution during identifier lowering.
    void lowerGlobalVarDecl(GlobalVarDecl &decl);

    /// @brief Lower a method declaration within a type.
    /// @param decl The method declaration.
    /// @param typeName The enclosing type name.
    /// @param isEntity True if this is an entity method.
    void lowerMethodDecl(MethodDecl &decl, const std::string &typeName, bool isEntity = false);

    /// @}
    //=========================================================================
    /// @name Statement Lowering
    /// @brief Methods for lowering statements.
    /// @{
    //=========================================================================

    /// @brief Lower any statement (dispatcher).
    /// @param stmt The statement to lower.
    void lowerStmt(Stmt *stmt);

    /// @brief Lower a block statement.
    /// @param stmt The block statement.
    void lowerBlockStmt(BlockStmt *stmt);

    /// @brief Lower an expression statement.
    /// @param stmt The expression statement.
    void lowerExprStmt(ExprStmt *stmt);

    /// @brief Lower a variable declaration statement.
    /// @param stmt The variable statement.
    void lowerVarStmt(VarStmt *stmt);

    /// @brief Lower an if statement.
    /// @param stmt The if statement.
    void lowerIfStmt(IfStmt *stmt);

    /// @brief Lower a while statement.
    /// @param stmt The while statement.
    void lowerWhileStmt(WhileStmt *stmt);

    /// @brief Lower a C-style for statement.
    /// @param stmt The for statement.
    void lowerForStmt(ForStmt *stmt);

    /// @brief Lower a for-in statement.
    /// @param stmt The for-in statement.
    void lowerForInStmt(ForInStmt *stmt);

    /// @brief Lower a return statement.
    /// @param stmt The return statement.
    void lowerReturnStmt(ReturnStmt *stmt);

    /// @brief Lower a break statement.
    /// @param stmt The break statement.
    void lowerBreakStmt(BreakStmt *stmt);

    /// @brief Lower a continue statement.
    /// @param stmt The continue statement.
    void lowerContinueStmt(ContinueStmt *stmt);

    /// @brief Lower a guard statement.
    /// @param stmt The guard statement.
    void lowerGuardStmt(GuardStmt *stmt);

    /// @brief Lower a match statement.
    /// @param stmt The match statement.
    void lowerMatchStmt(MatchStmt *stmt);

    /// @}
    //=========================================================================
    /// @name Expression Lowering
    /// @brief Methods for lowering expressions.
    /// @{
    //=========================================================================

    /// @brief Lower any expression (dispatcher).
    /// @param expr The expression to lower.
    /// @return The lowered result value and type.
    LowerResult lowerExpr(Expr *expr);

    /// @brief Lower an integer literal.
    /// @return LowerResult with i64 constant.
    LowerResult lowerIntLiteral(IntLiteralExpr *expr);

    /// @brief Lower a number (float) literal.
    /// @return LowerResult with f64 constant.
    LowerResult lowerNumberLiteral(NumberLiteralExpr *expr);

    /// @brief Lower a string literal.
    /// @return LowerResult with pointer to string constant.
    LowerResult lowerStringLiteral(StringLiteralExpr *expr);

    /// @brief Lower a boolean literal.
    /// @return LowerResult with i64 constant (0 or 1).
    LowerResult lowerBoolLiteral(BoolLiteralExpr *expr);

    /// @brief Lower a null literal.
    /// @return LowerResult with null pointer.
    LowerResult lowerNullLiteral(NullLiteralExpr *expr);

    /// @brief Lower an identifier expression.
    /// @return LowerResult with the variable's value.
    LowerResult lowerIdent(IdentExpr *expr);

    /// @brief Lower a binary expression.
    /// @return LowerResult with the operation result.
    LowerResult lowerBinary(BinaryExpr *expr);

    /// @brief Lower a unary expression.
    /// @return LowerResult with the operation result.
    LowerResult lowerUnary(UnaryExpr *expr);

    /// @brief Lower a ternary conditional expression.
    /// @return LowerResult with the selected branch value.
    LowerResult lowerTernary(TernaryExpr *expr);

    /// @brief Lower a call expression.
    /// @return LowerResult with the call result.
    LowerResult lowerCall(CallExpr *expr);

    /// @brief Lower a field access expression.
    /// @return LowerResult with the field value.
    LowerResult lowerField(FieldExpr *expr);

    /// @brief Lower a new expression (object creation).
    /// @return LowerResult with pointer to new object.
    LowerResult lowerNew(NewExpr *expr);

    /// @brief Lower a null coalesce expression.
    /// @return LowerResult with the coalesced value.
    LowerResult lowerCoalesce(CoalesceExpr *expr);

    /// @brief Lower an optional chain expression.
    /// @return LowerResult with optional result value.
    LowerResult lowerOptionalChain(OptionalChainExpr *expr);

    /// @brief Lower a list literal expression.
    /// @return LowerResult with pointer to new list.
    LowerResult lowerListLiteral(ListLiteralExpr *expr);

    /// @brief Lower a map literal expression.
    /// @return LowerResult with pointer to new map.
    LowerResult lowerMapLiteral(MapLiteralExpr *expr);

    /// @brief Lower a tuple literal expression.
    /// @return LowerResult with pointer to tuple on stack.
    LowerResult lowerTuple(TupleExpr *expr);

    /// @brief Lower a tuple index access expression.
    /// @return LowerResult with the element value.
    LowerResult lowerTupleIndex(TupleIndexExpr *expr);

    /// @brief Lower a block expression.
    /// @return LowerResult with the block's value (or void).
    LowerResult lowerBlockExpr(BlockExpr *expr);

    /// @brief Lower a match expression.
    /// @return LowerResult with the match result value.
    LowerResult lowerMatchExpr(MatchExpr *expr);

    /// @brief Lower an index expression.
    /// @return LowerResult with the element value.
    LowerResult lowerIndex(IndexExpr *expr);

    /// @brief Lower a try expression (propagate operator).
    /// @return LowerResult with unwrapped value or propagated null/error.
    LowerResult lowerTry(TryExpr *expr);

    /// @brief Lower a lambda expression.
    /// @return LowerResult with closure pointer.
    LowerResult lowerLambda(LambdaExpr *expr);

    /// @}
    //=========================================================================
    /// @name Instruction Emission Helpers
    /// @brief Low-level helpers for emitting IL instructions.
    /// @{
    //=========================================================================

    /// @brief Emit a binary arithmetic/comparison instruction.
    /// @param op The opcode (e.g., Opcode::IAdd, Opcode::ICmpEq).
    /// @param ty The result type.
    /// @param lhs Left operand value.
    /// @param rhs Right operand value.
    /// @return The result value.
    Value emitBinary(Opcode op, Type ty, Value lhs, Value rhs);

    /// @brief Emit a unary instruction.
    /// @param op The opcode (e.g., Opcode::INeg).
    /// @param ty The result type.
    /// @param operand The operand value.
    /// @return The result value.
    Value emitUnary(Opcode op, Type ty, Value operand);

    /// @brief Widen a Byte (i32) value to Integer (i64).
    /// @details Uses bitwise AND to zero-extend the value.
    /// @param value The i32 value to widen.
    /// @return The widened i64 value.
    Value widenByteToInteger(Value value);

    /// @brief Emit a function call with return value.
    /// @param retTy The expected return type.
    /// @param callee The function name.
    /// @param args The argument values.
    /// @return The return value.
    Value emitCallRet(Type retTy, const std::string &callee, const std::vector<Value> &args);

    /// @brief Emit a void function call.
    /// @param callee The function name.
    /// @param args The argument values.
    void emitCall(const std::string &callee, const std::vector<Value> &args);

    /// @brief Emit a void indirect function call.
    /// @param funcPtr The function pointer value.
    /// @param args The argument values.
    void emitCallIndirect(Value funcPtr, const std::vector<Value> &args);

    /// @brief Emit an indirect function call with return value.
    /// @param retTy The return type.
    /// @param funcPtr The function pointer value.
    /// @param args The argument values.
    /// @return The result value.
    Value emitCallIndirectRet(Type retTy, Value funcPtr, const std::vector<Value> &args);

    /// @brief Emit an unconditional branch.
    /// @param targetIdx The target block index.
    void emitBr(size_t targetIdx);

    /// @brief Emit a conditional branch.
    /// @param cond The condition value (i64, 0 = false).
    /// @param trueIdx Block to branch to if true.
    /// @param falseIdx Block to branch to if false.
    void emitCBr(Value cond, size_t trueIdx, size_t falseIdx);

    /// @brief Emit a return instruction with value.
    /// @param val The value to return.
    void emitRet(Value val);

    /// @brief Emit a void return instruction.
    void emitRetVoid();

    /// @brief Emit a string constant load.
    /// @param globalName The global name of the string constant.
    /// @return Pointer value to the string.
    Value emitConstStr(const std::string &globalName);

    /// @brief Get the next unique temporary ID.
    /// @return A unique temporary name counter.
    unsigned nextTempId();

    /// @brief Emit a GEP (get element pointer) instruction.
    /// @param ptr The base pointer.
    /// @param offset The byte offset to add.
    /// @return The offset pointer.
    Value emitGEP(Value ptr, int64_t offset);

    /// @brief Emit a Load instruction.
    /// @param ptr The pointer to load from.
    /// @param type The type to load.
    /// @return The loaded value.
    Value emitLoad(Value ptr, Type type);

    /// @brief Emit a Store instruction.
    /// @param ptr The pointer to store to.
    /// @param val The value to store.
    /// @param type The type being stored.
    void emitStore(Value ptr, Value val, Type type);

    /// @brief Emit a field load from a struct pointer.
    /// @param field The field layout info.
    /// @param selfPtr Pointer to the struct.
    /// @return The loaded field value.
    Value emitFieldLoad(const FieldLayout *field, Value selfPtr);

    /// @brief Emit a field store to a struct pointer.
    /// @param field The field layout info.
    /// @param selfPtr Pointer to the struct.
    /// @param val The value to store.
    void emitFieldStore(const FieldLayout *field, Value selfPtr, Value val);

    /// @brief Lower a method call.
    /// @param method The method declaration.
    /// @param typeName The type containing the method.
    /// @param selfValue The receiver value (self).
    /// @param expr The call expression for arguments.
    /// @return The call result.
    LowerResult lowerMethodCall(MethodDecl *method,
                                const std::string &typeName,
                                Value selfValue,
                                CallExpr *expr);

    /// @brief Lower a virtual method call using vtable dispatch.
    /// @param entityInfo The entity type info with vtable.
    /// @param methodName The method name.
    /// @param vtableSlot The vtable slot index.
    /// @param selfValue The receiver value (self).
    /// @param expr The call expression for arguments.
    /// @return The call result.
    LowerResult lowerVirtualMethodCall(const EntityTypeInfo &entityInfo,
                                       const std::string &methodName,
                                       size_t vtableSlot,
                                       Value selfValue,
                                       CallExpr *expr);

    /// @brief Lower an interface method call using class_id-based dispatch.
    /// @param ifaceInfo The interface type info.
    /// @param methodName The method name.
    /// @param method The method declaration from the interface.
    /// @param selfValue The receiver value (self).
    /// @param expr The call expression for arguments.
    /// @return The call result.
    LowerResult lowerInterfaceMethodCall(const InterfaceTypeInfo &ifaceInfo,
                                         const std::string &methodName,
                                         MethodDecl *method,
                                         Value selfValue,
                                         CallExpr *expr);

    /// @}
    //=========================================================================
    /// @name Boxing/Unboxing Helpers
    /// @brief Helpers for boxing primitives for generic collections.
    /// @{
    //=========================================================================

    /// @brief Box a primitive value for collection storage.
    /// @param val The value to box.
    /// @param type The IL type of the value.
    /// @return Pointer to the boxed value.
    ///
    /// @details Allocates space for the value and stores it.
    /// Used when inserting primitives into List[T], Map[K,V], etc.
    Value emitBox(Value val, Type type);

    /// @brief Unbox a value to a primitive type.
    /// @param boxed The boxed pointer value.
    /// @param expectedType The expected IL type.
    /// @return The unboxed value with its type.
    ///
    /// @details Loads the value from the boxed pointer.
    /// Used when retrieving primitives from collections.
    LowerResult emitUnbox(Value boxed, Type expectedType);

    /// @brief Wrap a value in optional storage (box primitives/strings when needed).
    /// @param val The value to wrap.
    /// @param innerType The semantic inner type of the optional.
    /// @return Pointer representing the optional value (null represents None).
    Value emitOptionalWrap(Value val, TypeRef innerType);

    /// @brief Unwrap an optional value to its inner IL type.
    /// @param val Pointer representing the optional value.
    /// @param innerType The semantic inner type of the optional.
    /// @return The unwrapped value with its type.
    LowerResult emitOptionalUnwrap(Value val, TypeRef innerType);

    /// @}
    //=========================================================================
    /// @name Type Mapping
    /// @brief Methods for mapping ViperLang types to IL types.
    /// @{
    //=========================================================================

    /// @brief Map a semantic type to an IL type.
    /// @param type The ViperLang semantic type.
    /// @return The corresponding IL type.
    Type mapType(TypeRef type);

    /// @brief Get the size in bytes for an IL type.
    /// @param type The IL type.
    /// @return Size in bytes.
    ///
    /// @details Size mapping:
    /// - i64, f64, ptr: 8 bytes
    /// - i32: 4 bytes
    /// - i16: 2 bytes
    /// - i1: 1 byte (but often stored as 8)
    static size_t getILTypeSize(Type type);

    /// @brief Get the alignment in bytes for an IL type.
    /// @param type The IL type.
    /// @return Alignment in bytes.
    ///
    /// @details Alignment ensures proper memory access for the type.
    /// Boolean (i1) aligns to 8 bytes to avoid misalignment issues
    /// when followed by pointer-sized fields.
    static size_t getILTypeAlignment(Type type);

    /// @brief Align an offset to a given alignment boundary.
    /// @param offset The current offset.
    /// @param alignment The required alignment.
    /// @return The aligned offset (>= original offset).
    static size_t alignTo(size_t offset, size_t alignment);

    /// @}
    //=========================================================================
    /// @name Local Variable Management
    /// @brief Methods for managing function-local variables.
    /// @{
    //=========================================================================

    /// @brief Define an immutable local variable.
    /// @param name The variable name.
    /// @param value The SSA value.
    void defineLocal(const std::string &name, Value value);

    /// @brief Look up a local variable.
    /// @param name The variable name.
    /// @return Pointer to the value, or nullptr if not found.
    Value *lookupLocal(const std::string &name);

    /// @brief Create a mutable variable slot.
    /// @param name The variable name.
    /// @param type The variable type.
    /// @return Pointer value to the slot.
    Value createSlot(const std::string &name, Type type);

    /// @brief Store a value to a mutable slot.
    /// @param name The variable name.
    /// @param value The value to store.
    /// @param type The value type.
    void storeToSlot(const std::string &name, Value value, Type type);

    /// @brief Load a value from a mutable slot.
    /// @param name The variable name.
    /// @param type The value type.
    /// @return The loaded value.
    Value loadFromSlot(const std::string &name, Type type);

    /// @brief Remove a slot (for scope cleanup).
    /// @param name The variable name.
    void removeSlot(const std::string &name);

    /// @brief Get the self pointer for the current method.
    /// @details Checks both slots and locals for "self".
    /// @param result Output parameter for the self pointer value.
    /// @return True if self was found, false otherwise.
    bool getSelfPtr(Value &result);

    /// @}
    //=========================================================================
    /// @name Pattern Matching Helpers
    /// @brief Helpers for lowering match patterns.
    /// @{
    //=========================================================================

    struct PatternValue
    {
        Value value;
        TypeRef type;
    };

    /// @brief Emit control flow to test a pattern against a value.
    /// @param pattern The pattern to match.
    /// @param scrutinee The value/type being matched.
    /// @param successBlock Block to branch to on success.
    /// @param failureBlock Block to branch to on failure.
    void emitPatternTest(const MatchArm::Pattern &pattern,
                         const PatternValue &scrutinee,
                         size_t successBlock,
                         size_t failureBlock);

    /// @brief Emit bindings for a matched pattern in the current block.
    /// @param pattern The pattern to bind.
    /// @param scrutinee The value/type being matched (assumed to have matched).
    void emitPatternBindings(const MatchArm::Pattern &pattern, const PatternValue &scrutinee);

    /// @brief Load a tuple element value.
    /// @param tuple The tuple value/type.
    /// @param index The element index.
    /// @param elemType The semantic type for the element.
    /// @return The element value/type pair.
    PatternValue emitTupleElement(const PatternValue &tuple, size_t index, TypeRef elemType);

    /// @}
    //=========================================================================
    /// @name Helper Functions
    /// @brief Utility functions for lowering.
    /// @{
    //=========================================================================

    /// @brief Mangle a function name for IL.
    /// @param name The source-level function name.
    /// @return The mangled name.
    std::string mangleFunctionName(const std::string &name);

    /// @brief Get or create a global string constant.
    /// @param value The string value.
    /// @return The global name for the string.
    std::string getStringGlobal(const std::string &value);

    /// @brief Case-insensitive string comparison.
    /// @param a First string.
    /// @param b Second string.
    /// @return True if equal ignoring case.
    static bool equalsIgnoreCase(const std::string &a, const std::string &b);

    /// @brief Get the runtime helper name for module variable address lookup.
    /// @param kind The IL type kind.
    /// @return Runtime function name (e.g., "rt_modvar_addr_i64").
    std::string getModvarAddrHelper(Type::Kind kind);

    /// @brief Get the address of a global variable using runtime storage.
    /// @param name The variable name.
    /// @param type The semantic type.
    /// @return Pointer value to the variable storage.
    Value getGlobalVarAddr(const std::string &name, TypeRef type);

    /// @}
};

/// @}

} // namespace il::frontends::viperlang
