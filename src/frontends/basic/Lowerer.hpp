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
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::frontends::basic
{

class LowererExprVisitor;
class LowererStmtVisitor;
class ScanExprVisitor;
class ScanStmtVisitor;
struct ProgramLowering;
struct ProcedureLowering;
struct StatementLowering;

/// @brief Lowers BASIC AST into IL Module.
/// @invariant Generates deterministic block names per procedure using BlockNamer.
/// @ownership Owns produced Module; uses IRBuilder for structure emission.
class Lowerer
{
  public:
    /// @brief Construct a lowerer.
    /// @param boundsChecks Enable debug array bounds checks.
    explicit Lowerer(bool boundsChecks = false);

    ~Lowerer();

    /// @brief Lower @p prog into an IL module with @main entry.
    /// @notes Procedures are lowered before a synthetic `@main` encompassing
    ///        the program's top-level statements.
    il::core::Module lowerProgram(const Program &prog);

    /// @brief Backward-compatibility wrapper for older call sites.
    il::core::Module lower(const Program &prog);

  private:
    friend class LowererExprVisitor;
    friend class LowererStmtVisitor;
    friend class ScanExprVisitor;
    friend class ScanStmtVisitor;
    friend struct ProgramLowering;
    friend struct ProcedureLowering;
    friend struct StatementLowering;

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
    struct SlotType
    {
        Type type{Type(Type::Kind::I64)};
        bool isArray{false};
        bool isBoolean{false};
    };

    /// @brief Cached signature for a user-defined procedure.
    struct ProcedureSignature
    {
        Type retType{Type(Type::Kind::I64)};           ///< Declared return type.
        std::vector<Type> paramTypes;                  ///< Declared parameter types.
    };

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

  public:
    struct ProcedureConfig;

  private:
    struct ProcedureMetadata
    {
        std::vector<const Stmt *> bodyStmts;
        std::unordered_set<std::string> paramNames;
        std::vector<il::core::Param> irParams;
        size_t paramCount{0};
    };

    ProcedureMetadata collectProcedureMetadata(const std::vector<Param> &params,
                                               const std::vector<StmtPtr> &body,
                                               const ProcedureConfig &config);

    void buildProcedureSkeleton(Function &f,
                                const std::string &name,
                                const ProcedureMetadata &metadata);

    void allocateLocalSlots(const std::unordered_set<std::string> &paramNames,
                            bool includeParams);

    void lowerStatementSequence(const std::vector<const Stmt *> &stmts,
                                bool stopOnTerminated,
                                const std::function<void(const Stmt &)> &beforeBranch = {});

#include "frontends/basic/LowerEmit.hpp"

    std::unique_ptr<ProgramLowering> programLowering;
    std::unique_ptr<ProcedureLowering> procedureLowering;
    std::unique_ptr<StatementLowering> statementLowering;

    build::IRBuilder *builder{nullptr};
    Module *mod{nullptr};
    Function *func{nullptr};
    BasicBlock *cur{nullptr};
    NameMangler mangler;
    std::unordered_map<std::string, std::string> strings;
    il::support::SourceLoc curLoc{}; ///< current source location for emitted IR
    bool boundsChecks{false};
    std::unordered_map<std::string, ProcedureSignature> procSignatures;

    // runtime requirement tracking
    using RuntimeFeature = il::runtime::RuntimeFeature;

    static constexpr size_t kRuntimeFeatureCount =
        static_cast<size_t>(RuntimeFeature::Count);

    struct RuntimeFeatureHash
    {
        size_t operator()(RuntimeFeature f) const;
    };

    /// @brief Aggregates per-procedure lowering state.
    /// @invariant Must be reset between procedures to avoid leaking metadata.
    struct ProcedureState
    {
        size_t fnExit{0};
        unsigned nextTemp{0};
        unsigned boundsCheckId{0};
        std::unordered_map<int, size_t> lineBlocks;
        std::unordered_map<std::string, unsigned> varSlots;
        std::unordered_map<std::string, unsigned> arrayLenSlots;
        std::unordered_map<std::string, AstType> varTypes;
        std::unordered_set<std::string> vars;
        std::unordered_set<std::string> arrays;
        std::bitset<kRuntimeFeatureCount> runtimeFeatures;
        std::vector<RuntimeFeature> runtimeOrder;
        std::unordered_set<RuntimeFeature, RuntimeFeatureHash> runtimeSet;

        void reset();
    };

    ProcedureState procState{};

#include "frontends/basic/LowerRuntime.hpp"
#include "frontends/basic/LowerScan.hpp"

    SlotType getSlotType(std::string_view name) const;

  public:
    /// @brief Lookup a cached procedure signature by BASIC name.
    /// @return Pointer to the signature when present, nullptr otherwise.
    const ProcedureSignature *findProcSignature(const std::string &name) const;
};

} // namespace il::frontends::basic

#include "frontends/basic/LoweringPipeline.hpp"
