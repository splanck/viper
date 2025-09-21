// File: src/frontends/basic/Lowerer.hpp
// Purpose: Declares lowering from BASIC AST to IL with helper routines and
// centralized runtime declarations.
// Key invariants: Procedure block labels are deterministic.
// Ownership/Lifetime: Lowerer does not own AST or module.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/NameMangler.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include <bitset>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::frontends::basic
{

/// @brief Lowers BASIC AST into IL Module.
/// @invariant Generates deterministic block names per procedure using BlockNamer.
/// @ownership Owns produced Module; uses IRBuilder for structure emission.
class Lowerer : public ExprVisitor, public StmtVisitor
{
  public:
    /// @brief Construct a lowerer.
    /// @param boundsChecks Enable debug array bounds checks.
    explicit Lowerer(bool boundsChecks = false);

    /// @brief Lower @p prog into an IL module with @main entry.
    /// @notes Procedures are lowered before a synthetic `@main` encompassing
    ///        the program's top-level statements.
    il::core::Module lowerProgram(const Program &prog);

    /// @brief Backward-compatibility wrapper for older call sites.
    il::core::Module lower(const Program &prog);

  private:
    using Module = il::core::Module;
    using Function = il::core::Function;
    using BasicBlock = il::core::BasicBlock;
    using Value = il::core::Value;
    using Type = il::core::Type;
    using Opcode = il::core::Opcode;
    using IlValue = Value;
    using IlType = Type;
    using AstType = ::il::frontends::basic::Type;

  public:
    struct RVal
    {
        Value value;
        Type type;
    };

  private:
    enum class ExprVisitMode
    {
        Value,
        Address,
    };

    struct LValue
    {
        Value address;
        Type type;
        bool isArray{false};
    };

    // ExprVisitor interface
    void visit(const IntExpr &expr) override;
    void visit(const FloatExpr &expr) override;
    void visit(const StringExpr &expr) override;
    void visit(const BoolExpr &expr) override;
    void visit(const VarExpr &expr) override;
    void visit(const ArrayExpr &expr) override;
    void visit(const UnaryExpr &expr) override;
    void visit(const BinaryExpr &expr) override;
    void visit(const BuiltinCallExpr &expr) override;
    void visit(const CallExpr &expr) override;

    // StmtVisitor interface
    void visit(const PrintStmt &stmt) override;
    void visit(const LetStmt &stmt) override;
    void visit(const DimStmt &stmt) override;
    void visit(const RandomizeStmt &stmt) override;
    void visit(const IfStmt &stmt) override;
    void visit(const WhileStmt &stmt) override;
    void visit(const ForStmt &stmt) override;
    void visit(const NextStmt &stmt) override;
    void visit(const GotoStmt &stmt) override;
    void visit(const EndStmt &stmt) override;
    void visit(const InputStmt &stmt) override;
    void visit(const ReturnStmt &stmt) override;
    void visit(const FunctionDecl &stmt) override;
    void visit(const SubDecl &stmt) override;
    void visit(const StmtList &stmt) override;

  private:
    /// @brief Layout of blocks emitted for an IF/ELSEIF chain.
    struct IfBlocks
    {
        std::vector<size_t> tests; ///< indexes of test blocks
        std::vector<size_t> thens; ///< indexes of THEN blocks
        BasicBlock *elseBlk;       ///< pointer to ELSE block
        BasicBlock *exitBlk;       ///< pointer to common exit
    };

    /// @brief Deterministic per-procedure block name generator.
    /// @invariant `k` starts at 0 per procedure and increases monotonically.
    ///            WHILE, FOR, and synthetic call continuations share the same
    ///            sequence to reflect lexical ordering.
    /// @ownership Owned by Lowerer; scoped to a single procedure.
    struct BlockNamer
    {
        std::string proc;        ///< procedure name
        unsigned ifCounter{0};   ///< sequential IF identifiers
        unsigned loopCounter{0}; ///< WHILE/FOR/call_cont identifiers
        std::unordered_map<std::string, unsigned> genericCounters; ///< other shapes

        explicit BlockNamer(std::string p);

        std::string entry() const;

        std::string ret() const;

        std::string line(int line) const;

        unsigned nextIf();

        std::string ifTest(unsigned id) const;

        std::string ifThen(unsigned id) const;

        std::string ifElse(unsigned id) const;

        std::string ifEnd(unsigned id) const;

        unsigned nextWhile();

        std::string whileHead(unsigned id) const;

        std::string whileBody(unsigned id) const;

        std::string whileEnd(unsigned id) const;

        unsigned nextFor();

        /// @brief Allocate next sequential ID for a call continuation.
        unsigned nextCall();

        std::string forHead(unsigned id) const;

        std::string forBody(unsigned id) const;

        std::string forInc(unsigned id) const;

        std::string forEnd(unsigned id) const;

        /// @brief Build label for a synthetic call continuation block.
        std::string callCont(unsigned id) const;

        std::string generic(const std::string &hint);

        std::string tag(const std::string &base) const;
    };

    struct ForBlocks
    {
        size_t headIdx{0};
        size_t headPosIdx{0};
        size_t headNegIdx{0};
        size_t bodyIdx{0};
        size_t incIdx{0};
        size_t doneIdx{0};
    };

    std::unique_ptr<BlockNamer> blockNamer;

#include "frontends/basic/LowerEmit.hpp"

    build::IRBuilder *builder{nullptr};
    Module *mod{nullptr};
    Function *func{nullptr};
    BasicBlock *cur{nullptr};
    size_t fnExit{0};
    NameMangler mangler;
    std::unordered_map<int, size_t> lineBlocks;
    std::unordered_map<std::string, unsigned> varSlots;
    std::unordered_map<std::string, unsigned> arrayLenSlots;
    std::unordered_map<std::string, AstType> varTypes;
    std::unordered_map<std::string, std::string> strings;
    std::unordered_set<std::string> vars;
    std::unordered_set<std::string> arrays;
    il::support::SourceLoc curLoc{}; ///< current source location for emitted IR
    ExprVisitMode exprMode{ExprVisitMode::Value};
    RVal exprResult{Value::constInt(0), Type(Type::Kind::I64)};
    LValue lvalueResult{Value::constInt(0), Type(Type::Kind::I64)};
    std::optional<il::support::SourceLoc> pendingLValueLoc;
    bool boundsChecks{false};
    unsigned boundsCheckId{0};

    // runtime requirement tracking
    using RuntimeFeature = il::runtime::RuntimeFeature;

    static constexpr size_t kRuntimeFeatureCount =
        static_cast<size_t>(RuntimeFeature::Count);

    std::bitset<kRuntimeFeatureCount> runtimeFeatures;

#include "frontends/basic/LowerRuntime.hpp"
#include "frontends/basic/LowerScan.hpp"
};

} // namespace il::frontends::basic
