// File: src/frontends/basic/Lowerer.hpp
// Purpose: Declares lowering from BASIC AST to IL.
// Key invariants: None.
// Ownership/Lifetime: Lowerer does not own AST or module.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/NameMangler.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace il::frontends::basic
{

/// @brief Lowers BASIC AST into IL Module.
/// @invariant Generates deterministic block names via NameMangler.
/// @ownership Owns produced Module; uses IRBuilder for structure emission.
class Lowerer
{
  public:
    /// @brief Construct a lowerer.
    /// @param boundsChecks Enable debug array bounds checks.
    explicit Lowerer(bool boundsChecks = false);

    /// @brief Lower @p prog into an IL module with @main entry.
    il::core::Module lower(const Program &prog);

  private:
    using Module = il::core::Module;
    using Function = il::core::Function;
    using BasicBlock = il::core::BasicBlock;
    using Value = il::core::Value;
    using Type = il::core::Type;
    using Opcode = il::core::Opcode;

    struct RVal
    {
        Value value;
        Type type;
    };

    void collectVars(const Program &prog);
    void lowerStmt(const Stmt &stmt);
    RVal lowerExpr(const Expr &expr);

    void lowerLet(const LetStmt &stmt);
    void lowerPrint(const PrintStmt &stmt);
    void lowerIf(const IfStmt &stmt);
    void lowerWhile(const WhileStmt &stmt);
    void lowerFor(const ForStmt &stmt);
    void lowerNext(const NextStmt &stmt);
    void lowerGoto(const GotoStmt &stmt);
    void lowerEnd(const EndStmt &stmt);
    void lowerInput(const InputStmt &stmt);
    void lowerDim(const DimStmt &stmt);

    // helpers
    Value emitAlloca(int bytes);
    Value emitLoad(Type ty, Value addr);
    void emitStore(Type ty, Value addr, Value val);
    Value emitBinary(Opcode op, Type ty, Value lhs, Value rhs);
    /// @brief Emit unary instruction of @p op on @p val producing @p ty.
    Value emitUnary(Opcode op, Type ty, Value val);
    void emitBr(BasicBlock *target);
    void emitCBr(Value cond, BasicBlock *t, BasicBlock *f);
    Value emitCallRet(Type ty, const std::string &callee, const std::vector<Value> &args);
    void emitCall(const std::string &callee, const std::vector<Value> &args);
    Value emitConstStr(const std::string &globalName);
    void emitTrap();
    void emitRet(Value v);
    std::string getStringLabel(const std::string &s);
    unsigned nextTempId();
    Value lowerArrayAddr(const ArrayExpr &expr);

    build::IRBuilder *builder{nullptr};
    Module *mod{nullptr};
    Function *func{nullptr};
    BasicBlock *cur{nullptr};
    size_t fnExit{0};
    NameMangler mangler;
    std::unordered_map<int, size_t> lineBlocks;
    std::unordered_map<std::string, unsigned> varSlots;
    std::unordered_map<std::string, unsigned> arrayLenSlots;
    std::unordered_map<std::string, std::string> strings;
    std::unordered_set<std::string> vars;
    std::unordered_set<std::string> arrays;
    il::support::SourceLoc curLoc{}; ///< current source location for emitted IR
    bool usedStrEq{false};
    bool boundsChecks{false};
    unsigned boundsCheckId{0};
};

} // namespace il::frontends::basic
