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

#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/SemanticAnalyzer.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace il::frontends::pascal
{

/// @brief Result of lowering an expression: value and its IL type.
struct LowerResult
{
    il::core::Value value;
    il::core::Type type;
};

/// @brief Loop context for break/continue support (uses indices to avoid pointer invalidation).
struct LoopContext
{
    size_t breakBlockIdx;    ///< Index of target for break
    size_t continueBlockIdx; ///< Index of target for continue
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

    std::unique_ptr<Module> module_;                 ///< Module being built
    std::unique_ptr<il::build::IRBuilder> builder_;  ///< IR builder
    SemanticAnalyzer *sema_{nullptr};                ///< Semantic analyzer
    Function *currentFunc_{nullptr};                 ///< Current function
    size_t currentBlockIdx_{0};                      ///< Current block index
    std::map<std::string, Value> locals_;            ///< Variable -> alloca slot
    std::map<std::string, Value> constants_;         ///< Constant -> value
    std::map<std::string, std::string> stringTable_; ///< String -> global name
    std::vector<LoopContext> loopStack_;             ///< Loop context stack
    std::set<std::string> usedExterns_;              ///< Tracked runtime externs
    unsigned blockCounter_{0};                       ///< Block name counter
    unsigned stringCounter_{0};                      ///< String global counter
    Value currentResumeTok_;                         ///< Resume token in current handler
    bool inExceptHandler_{false};                    ///< True when inside except handler

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

    /// @brief Lower a try-except statement.
    void lowerTryExcept(const TryExceptStmt &stmt);

    /// @brief Lower a try-finally statement.
    void lowerTryFinally(const TryFinallyStmt &stmt);

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
};

} // namespace il::frontends::pascal
