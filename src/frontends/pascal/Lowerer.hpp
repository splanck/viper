//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Lowerer.hpp
// Purpose: Declares the Lowerer class for transforming Pascal AST into IL.
// Key invariants: Generates deterministic block names; produces valid SSA.
// Ownership/Lifetime: Does not own AST or Module; populates Module via builder.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/common/ExprResult.hpp"
#include "frontends/common/LoopContext.hpp"
#include "frontends/common/NameMangler.hpp"
#include "frontends/common/StringTable.hpp"
#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/SemanticAnalyzer.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace il::frontends::pascal
{

// Use common ExprResult but alias as LowerResult for backward compatibility
using LowerResult = ::il::frontends::common::ExprResult;

// Use common LoopContext
using LoopContext = ::il::frontends::common::LoopContext;

//===----------------------------------------------------------------------===//
// OOP Support Structures
//===----------------------------------------------------------------------===//

/// @brief Layout information for a single field in a class.
struct ClassFieldLayout
{
    std::string name;       ///< Field name
    PasType type;           ///< Field type
    std::size_t offset;     ///< Byte offset from object base
    std::size_t size;       ///< Size in bytes
};

/// @brief Complete layout for a class including inherited fields.
struct ClassLayout
{
    std::string name;                       ///< Class name
    std::vector<ClassFieldLayout> fields;   ///< All fields in layout order
    std::size_t size;                       ///< Total object size (8-byte aligned)
    std::int64_t classId;                   ///< Unique runtime type ID

    /// @brief Find a field by name.
    const ClassFieldLayout *findField(const std::string &name) const;
};

/// @brief Vtable slot information.
struct VtableSlot
{
    std::string methodName;     ///< Method name
    std::string implClass;      ///< Class that provides implementation
    int slot;                   ///< Slot index in vtable
};

/// @brief Vtable layout for a class.
struct VtableLayout
{
    std::string className;              ///< Class this vtable belongs to
    std::vector<VtableSlot> slots;      ///< Slots in order
    std::size_t slotCount;              ///< Number of slots
};

/// @brief Interface method slot.
struct InterfaceSlot
{
    std::string methodName;     ///< Method name in the interface
    int slot;                   ///< Slot index in interface table
};

/// @brief Interface layout (method table).
struct InterfaceLayout
{
    std::string name;                   ///< Interface name
    std::int64_t interfaceId;           ///< Unique interface ID
    std::vector<InterfaceSlot> slots;   ///< Method slots in order
    std::size_t slotCount;              ///< Number of slots
};

/// @brief Interface implementation table for a class.
/// Maps interface method slots to actual class method implementations.
struct InterfaceImplTable
{
    std::string className;              ///< Class implementing the interface
    std::string interfaceName;          ///< Interface being implemented
    std::vector<std::string> implMethods; ///< Mangled names of implementing methods, in slot order
};

/// @brief Transforms validated Pascal AST into Viper IL.
/// @invariant Generates deterministic block names via BlockNamer.
/// @ownership Owns produced Module; uses IRBuilder for emission.
class Lowerer
{
  public:
    using Type = il::core::Type;
    using Value = il::core::Value;
    using BasicBlock = il::core::BasicBlock;
    using Function = il::core::Function;
    using Module = il::core::Module;
    using Opcode = il::core::Opcode;

    /// @brief Construct a lowerer.
    Lowerer();

    /// @brief Lower a Pascal program into an IL module.
    /// @param prog The validated Pascal program AST.
    /// @param sema Semantic analyzer with type information.
    /// @return Lowered IL module.
    Module lower(Program &prog, SemanticAnalyzer &sema);

    /// @brief Lower a Pascal unit into an IL module.
    /// @param unit The validated Pascal unit AST.
    /// @param sema Semantic analyzer with type information.
    /// @return Lowered IL module.
    Module lower(Unit &unit, SemanticAnalyzer &sema);

    /// @brief Merge another module's functions and globals into this module.
    /// @param target Target module to merge into.
    /// @param source Source module to merge from.
    static void mergeModule(Module &target, Module &source);

  private:
    //=========================================================================
    // State
    //=========================================================================

    std::unique_ptr<Module> module_;                                ///< Module being built
    std::unique_ptr<il::build::IRBuilder> builder_;                 ///< IR builder
    SemanticAnalyzer *sema_{nullptr};                               ///< Semantic analyzer
    Function *currentFunc_{nullptr};                                ///< Current function
    std::string currentFuncName_;                                   ///< Current function name (lowercase, for Result mapping)
    std::string currentClassName_;                                  ///< Current class name (for Self/field access in methods)
    size_t currentBlockIdx_{0};                                     ///< Current block index
    std::map<std::string, Value> locals_;                           ///< Variable -> alloca slot
    std::map<std::string, PasType> localTypes_;                     ///< Variable -> type (for procedure locals)
    std::map<std::string, Value> constants_;                        ///< Constant -> value
    ::il::frontends::common::StringTable stringTable_;              ///< String interning table
    ::il::frontends::common::LoopContextStack loopStack_;           ///< Loop context stack
    std::set<std::string> usedExterns_;                             ///< Tracked runtime externs
    unsigned blockCounter_{0};                                      ///< Block name counter
    Value currentResumeTok_;                                        ///< Resume token in current handler
    bool inExceptHandler_{false};                                   ///< True when inside except handler

    // OOP State
    std::unordered_map<std::string, ClassLayout> classLayouts_;     ///< Class name -> layout
    std::unordered_map<std::string, VtableLayout> vtableLayouts_;   ///< Class name -> vtable layout
    std::int64_t nextClassId_{1};                                   ///< Next class ID to assign
    std::vector<std::string> classRegistrationOrder_;               ///< Order to register classes (base before derived)

    // Interface State
    std::unordered_map<std::string, InterfaceLayout> interfaceLayouts_;  ///< Interface name -> layout
    /// @brief Class+Interface -> implementation table (key = "classname.ifacename")
    std::unordered_map<std::string, InterfaceImplTable> interfaceImplTables_;
    std::int64_t nextInterfaceId_{1};                               ///< Next interface ID to assign
    std::vector<std::string> interfaceRegistrationOrder_;           ///< Order to register interfaces

    // With statement state
    struct WithContext
    {
        PasType type;       ///< Type of the with expression (class or record)
        Value slot;         ///< Alloca slot holding the value
    };
    std::vector<WithContext> withContexts_;                         ///< Stack of with contexts

    /// @brief Get the current block by index.
    BasicBlock *currentBlock() { return &currentFunc_->blocks[currentBlockIdx_]; }

    //=========================================================================
    // Block and Name Management
    //=========================================================================

    /// @brief Create a new basic block with unique name.
    /// @return Index of the created block.
    size_t createBlock(const std::string &base);

    /// @brief Set the current block for emission by index.
    void setBlock(size_t blockIdx);

    /// @brief Get a block by index.
    BasicBlock &getBlock(size_t idx) { return currentFunc_->blocks[idx]; }

    /// @brief Get or create a global string constant.
    std::string getStringGlobal(const std::string &value);

    //=========================================================================
    // Type Mapping
    //=========================================================================

    /// @brief Map Pascal type to IL type.
    Type mapType(const PasType &pasType);

    /// @brief Get the size in bytes for a type.
    int64_t sizeOf(const PasType &pasType);

    /// @brief Get the Pascal type of an expression.
    /// Uses localTypes_ for local variables, then falls back to sema_->typeOf().
    PasType typeOfExpr(const Expr &expr);

    //=========================================================================
    // Declaration Lowering
    //=========================================================================

    /// @brief Lower all top-level declarations.
    void lowerDeclarations(Program &prog);

    /// @brief Allocate local variables for a scope.
    void allocateLocals(const std::vector<std::unique_ptr<Decl>> &decls);

    /// @brief Initialize a local variable with default value.
    void initializeLocal(const std::string &name, const PasType &type);

    /// @brief Lower a function declaration (create IL function).
    void lowerFunctionDecl(FunctionDecl &decl);

    /// @brief Lower a procedure declaration (create IL function).
    void lowerProcedureDecl(ProcedureDecl &decl);

    /// @brief Lower a constructor declaration (create IL function).
    void lowerConstructorDecl(ConstructorDecl &decl);

    /// @brief Lower a destructor declaration (create IL function).
    void lowerDestructorDecl(DestructorDecl &decl);

    //=========================================================================
    // Expression Lowering
    //=========================================================================

    /// @brief Lower an expression to a value.
    LowerResult lowerExpr(const Expr &expr);

    /// @brief Lower an integer literal.
    LowerResult lowerIntLiteral(const IntLiteralExpr &expr);

    /// @brief Lower a real literal.
    LowerResult lowerRealLiteral(const RealLiteralExpr &expr);

    /// @brief Lower a string literal.
    LowerResult lowerStringLiteral(const StringLiteralExpr &expr);

    /// @brief Lower a boolean literal.
    LowerResult lowerBoolLiteral(const BoolLiteralExpr &expr);

    /// @brief Lower a nil literal.
    LowerResult lowerNilLiteral(const NilLiteralExpr &expr);

    /// @brief Lower a name expression (variable/constant reference).
    LowerResult lowerName(const NameExpr &expr);

    /// @brief Lower a unary expression.
    LowerResult lowerUnary(const UnaryExpr &expr);

    /// @brief Lower a binary expression.
    LowerResult lowerBinary(const BinaryExpr &expr);

    /// @brief Lower a call expression.
    LowerResult lowerCall(const CallExpr &expr);

    /// @brief Lower an index expression.
    LowerResult lowerIndex(const IndexExpr &expr);

    /// @brief Lower a field access expression.
    LowerResult lowerField(const FieldExpr &expr);

    /// @brief Get the address of a field in a record/class.
    /// @param baseAddr The base address of the record.
    /// @param baseType The PasType of the record/class.
    /// @param fieldName The name of the field.
    /// @return Pair of (field address, field IL type).
    std::pair<Value, Type> getFieldAddress(Value baseAddr, const PasType &baseType,
                                            const std::string &fieldName);

    /// @brief Lower short-circuit logical and.
    LowerResult lowerLogicalAnd(const BinaryExpr &expr);

    /// @brief Lower short-circuit logical or.
    LowerResult lowerLogicalOr(const BinaryExpr &expr);

    /// @brief Lower nil-coalescing operator.
    LowerResult lowerCoalesce(const BinaryExpr &expr);

    //=========================================================================
    // Statement Lowering
    //=========================================================================

    /// @brief Lower a statement.
    void lowerStmt(const Stmt &stmt);

    /// @brief Lower an assignment statement.
    void lowerAssign(const AssignStmt &stmt);

    /// @brief Lower a call statement.
    void lowerCallStmt(const CallStmt &stmt);

    /// @brief Lower a block statement.
    void lowerBlock(const BlockStmt &stmt);

    /// @brief Lower an if statement.
    void lowerIf(const IfStmt &stmt);

    /// @brief Lower a case statement.
    void lowerCase(const CaseStmt &stmt);

    /// @brief Lower a for loop.
    void lowerFor(const ForStmt &stmt);

    /// @brief Lower a for-in loop.
    void lowerForIn(const ForInStmt &stmt);

    /// @brief Lower a while loop.
    void lowerWhile(const WhileStmt &stmt);

    /// @brief Lower a repeat-until loop.
    void lowerRepeat(const RepeatStmt &stmt);

    /// @brief Lower a break statement.
    void lowerBreak(const BreakStmt &stmt);

    /// @brief Lower a continue statement.
    void lowerContinue(const ContinueStmt &stmt);

    /// @brief Lower a raise statement.
    void lowerRaise(const RaiseStmt &stmt);

    /// @brief Lower an exit statement.
    void lowerExit(const ExitStmt &stmt);

    /// @brief Lower a try-except statement.
    void lowerTryExcept(const TryExceptStmt &stmt);

    /// @brief Lower a try-finally statement.
    void lowerTryFinally(const TryFinallyStmt &stmt);

    /// @brief Lower an inherited method call statement.
    void lowerInherited(const InheritedStmt &stmt);

    /// @brief Lower a with statement.
    void lowerWith(const WithStmt &stmt);

    //=========================================================================
    // Instruction Emission Helpers
    //=========================================================================

    /// @brief Emit an alloca instruction.
    Value emitAlloca(int64_t size);

    /// @brief Emit a load instruction.
    Value emitLoad(Type ty, Value addr);

    /// @brief Emit a store instruction.
    void emitStore(Type ty, Value addr, Value val);

    /// @brief Emit a binary arithmetic instruction.
    Value emitBinary(Opcode op, Type ty, Value lhs, Value rhs);

    /// @brief Emit a unary instruction.
    Value emitUnary(Opcode op, Type ty, Value operand);

    /// @brief Emit a call instruction with return value.
    Value emitCallRet(Type retTy, const std::string &callee,
                      const std::vector<Value> &args);

    /// @brief Emit a call instruction without return value.
    void emitCall(const std::string &callee, const std::vector<Value> &args);

    /// @brief Emit an unconditional branch.
    void emitBr(size_t targetIdx);

    /// @brief Emit a conditional branch.
    void emitCBr(Value cond, size_t trueIdx, size_t falseIdx);

    /// @brief Emit a return instruction.
    void emitRet(Value val);

    /// @brief Emit a void return instruction.
    void emitRetVoid();

    /// @brief Emit a constant string reference.
    Value emitConstStr(const std::string &globalName);

    /// @brief Emit integer to float conversion.
    Value emitSitofp(Value intVal);

    /// @brief Emit float to integer conversion.
    Value emitFptosi(Value floatVal);

    /// @brief Emit zero-extend i1 to i64.
    Value emitZext1(Value boolVal);

    /// @brief Emit truncate i64 to i1.
    Value emitTrunc1(Value intVal);

    /// @brief Emit GEP (get element pointer) for pointer arithmetic.
    /// @param base Base pointer.
    /// @param offset Byte offset.
    /// @return Resulting pointer.
    Value emitGep(Value base, Value offset);

    /// @brief Emit EhPush instruction to register handler.
    void emitEhPush(size_t handlerBlockIdx);

    /// @brief Emit EhPop instruction to unregister handler.
    void emitEhPop();

    /// @brief Emit ResumeSame instruction to re-raise exception.
    void emitResumeSame(Value resumeTok);

    /// @brief Emit ResumeLabel instruction to exit handler to target block.
    void emitResumeLabel(Value resumeTok, size_t targetBlockIdx);

    /// @brief Create an EH handler block with standard params (%err, %tok).
    size_t createHandlerBlock(const std::string &base);

    /// @brief Reserve next temp ID.
    unsigned nextTempId();

    /// @brief Emit an indirect call with return value.
    Value emitCallIndirectRet(Type retTy, Value callee, const std::vector<Value> &args);

    /// @brief Emit an indirect call without return value.
    void emitCallIndirect(Value callee, const std::vector<Value> &args);

    //=========================================================================
    // OOP Lowering
    //=========================================================================

    /// @brief Scan all class declarations and compute layouts/vtables.
    void scanClasses(const std::vector<std::unique_ptr<Decl>> &decls);

    /// @brief Compute the layout for a single class.
    void computeClassLayout(const std::string &className);

    /// @brief Compute the vtable layout for a class.
    void computeVtableLayout(const std::string &className);

    /// @brief Get virtual method slot for a method (-1 if not virtual).
    int getVirtualSlot(const std::string &className, const std::string &methodName) const;

    /// @brief Emit the OOP module initialization function.
    void emitOopModuleInit();

    /// @brief Emit vtable registration for a class.
    void emitVtableRegistration(const std::string &className);

    /// @brief Lower a constructor call (NEW expression equivalent).
    LowerResult lowerConstructorCall(const CallExpr &expr);

    /// @brief Lower a method call expression.
    LowerResult lowerMethodCall(const FieldExpr &fieldExpr, const CallExpr &callExpr);

    /// @brief Lower field access on an object.
    LowerResult lowerObjectFieldAccess(const FieldExpr &expr);

    /// @brief Get field offset in a class layout.
    std::size_t getFieldOffset(const std::string &className, const std::string &fieldName) const;

    //=========================================================================
    // Interface Lowering
    //=========================================================================

    /// @brief Scan all interface declarations and compute layouts.
    void scanInterfaces(const std::vector<std::unique_ptr<Decl>> &decls);

    /// @brief Compute the layout for a single interface.
    void computeInterfaceLayout(const std::string &ifaceName);

    /// @brief Compute interface implementation tables for a class.
    void computeInterfaceImplTables(const std::string &className);

    /// @brief Emit interface table registration in __pas_oop_init.
    void emitInterfaceTableRegistration(const std::string &className, const std::string &ifaceName);

    /// @brief Lower an interface method call expression.
    LowerResult lowerInterfaceMethodCall(const FieldExpr &fieldExpr, const CallExpr &callExpr);

    /// @brief Get interface method slot for a method in an interface.
    int getInterfaceSlot(const std::string &ifaceName, const std::string &methodName) const;

    /// @brief Get the interface layout for an interface name.
    const InterfaceLayout *getInterfaceLayout(const std::string &ifaceName) const;
};

} // namespace il::frontends::pascal
