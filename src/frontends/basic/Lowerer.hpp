//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/Lowerer.hpp
// Purpose: Transforms validated BASIC AST nodes into Viper IL instructions.
//          Final stage of the pipeline: Lexer -> Parser -> AST -> Semantic -> Lowerer -> IL.
// Key invariants: Generates deterministic block names per procedure using BlockNamer.
//                 Procedures are lowered before a synthetic @main encompassing
//                 top-level statements. Runtime helpers are declared lazily.
// Ownership/Lifetime: Owns the produced Module via lowerProgram(). Uses IRBuilder
//                     for structure emission. Does not own AST nodes.
// Links: docs/architecture.md, docs/codemap.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/BasicTypes.hpp"
#include "frontends/basic/EmitCommon.hpp"
#include "frontends/basic/IdentifierUtil.hpp"
#include "frontends/basic/LowerRuntime.hpp"
#include "frontends/basic/LowererContext.hpp"
#include "frontends/basic/LowererFwd.hpp"
#include "frontends/basic/LowererRuntimeHelpers.hpp"
#include "frontends/basic/LowererTypes.hpp"
#include "frontends/basic/NameMangler.hpp"
#include "frontends/basic/OopIndex.hpp"
#include "frontends/basic/StringTable.hpp"
#include "frontends/basic/SymbolTable.hpp"
#include "frontends/basic/TypeCoercionEngine.hpp"
#include "frontends/basic/TypeRules.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "viper/il/IRBuilder.hpp"
#include "viper/il/Module.hpp"
#include <array>
#include <cstdint>
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

// Forward declarations are now in LowererFwd.hpp

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

    /// @brief Attach diagnostic emitter used for frontend errors during lowering.
    /// @param emitter Diagnostic sink; may be nullptr to disable emission.
    void setDiagnosticEmitter(DiagnosticEmitter *emitter) noexcept;

    /// @brief Access the diagnostic emitter when present.
    [[nodiscard]] DiagnosticEmitter *diagnosticEmitter() const noexcept;

    /// @brief Attach semantic analyzer for type information lookup during lowering.
    /// @param analyzer Semantic analyzer providing variable type information; may be nullptr.
    void setSemanticAnalyzer(const SemanticAnalyzer *analyzer) noexcept;

    /// @brief Access the semantic analyzer when present.
    [[nodiscard]] const SemanticAnalyzer *semanticAnalyzer() const noexcept;

    /// @brief Map a canonical (case-insensitive) qualified class name to its declared casing.
    /// @details Uses the OOP index to find the class by case-insensitive match and returns
    ///          the qualified name as declared. Falls back to the input when not found.
    std::string resolveQualifiedClassCasing(const std::string &qname) const;

    /// @brief Mark a symbol as storing an object reference.
    /// @param name BASIC symbol name tracked in the symbol table.
    /// @param className Fully-qualified BASIC class name associated with the object.
    void setSymbolObjectType(std::string_view name, std::string className);

    /// @brief Ensure the given runtime helper is available to lowering.
    /// @param feature Runtime feature required by an auxiliary pass.
    void requestRuntimeFeature(il::runtime::RuntimeFeature feature)
    {
        requestHelper(feature);
    }

    // Namespace qualification support -------------------------------------------------
    /// @brief Push a namespace path onto the current qualification stack.
    /// @param path Ordered segments, e.g. {"A","B"} for A.B.
    void pushNamespace(const std::vector<std::string> &path);

    /// @brief Pop @p count segments from the namespace stack (best-effort clamp).
    /// @param count Number of trailing segments to remove.
    void popNamespace(std::size_t count);

    /// @brief Qualify a class name with the active namespace path if any.
    /// @param klass Unqualified BASIC class name.
    /// @return Fully-qualified name like "A.B.Klass" or the original when no namespace active.
    [[nodiscard]] std::string qualify(const std::string &klass) const;

    // =========================================================================
    // Source Location API
    // =========================================================================
    // These methods provide controlled access to the current source location
    // for RAII helpers and emission utilities without requiring friendship.

    /// @brief Get the current source location for IR emission.
    /// @return Reference to the mutable source location.
    [[nodiscard]] il::support::SourceLoc &sourceLocation() noexcept;

    /// @brief Get the current source location (const).
    /// @return Reference to the immutable source location.
    [[nodiscard]] const il::support::SourceLoc &sourceLocation() const noexcept;

    /// @brief Set the current source location for IR emission.
    /// @param loc New source location to use for subsequent emissions.
    void setSourceLocation(il::support::SourceLoc loc) noexcept;

  private:
    // =========================================================================
    // Friend Declarations
    // =========================================================================
    // Friends are grouped by functional category. Each friend requires access
    // to private Lowerer internals that cannot be reasonably exposed via public
    // accessors without breaking encapsulation or creating overly complex APIs.
    //
    // Categories:
    // - Pipeline Helpers: Core lowering orchestration (Program/Procedure/Statement)
    // - Expression Lowering: Numeric, logical, builtin expression handlers
    // - Statement Lowering: I/O, control flow, runtime statements
    // - OOP Helpers: Class method and field emission
    // - Scanning Infrastructure: Runtime needs and type analysis
    // - Emission Utilities: IL instruction generation helpers

    // Pipeline Helpers - orchestrate multi-phase lowering
    friend struct ProgramLowering;
    friend struct ProcedureLowering;
    friend struct StatementLowering;

    // Expression Lowering - handle specific expression categories
    friend class LowererExprVisitor;
    friend struct LogicalExprLowering;
    friend struct NumericExprLowering;
    friend struct BuiltinExprLowering;
    friend class builtins::LowerCtx;

    // Statement Lowering - handle specific statement categories
    friend class LowererStmtVisitor;
    friend class SelectCaseLowering;
    friend class IoStatementLowerer;
    friend class ControlStatementLowerer;
    friend class RuntimeStatementLowerer;

    // OOP Helpers - class/method/field emission
    friend class OopEmitHelper;
    friend struct OopLoweringContext;
    friend class MethodDispatchResolver;
    friend class BoundsCheckEmitter;

    // Scanning Infrastructure - pre-lowering analysis
    friend class lower::detail::ExprTypeScanner;
    friend class lower::detail::RuntimeNeedsScanner;

    // Emission Utilities - IL generation helpers that need private emit* methods
    friend class Emit;               // Wraps emitUnary/emitBinary with location tracking
    friend class TypeCoercionEngine; // Type coercion needs emitUnary/emitBasicLogicalI64
    friend class lower::Emitter;
    friend class lower::BuiltinLowerContext;
    friend class lower::common::CommonLowering;
    friend class RuntimeCallBuilder;
    friend class NumericTypeClassifier; // Needs access to classifyNumericType

    using Module = il::core::Module;
    using Function = il::core::Function;
    using BasicBlock = il::core::BasicBlock;
    using Value = il::core::Value;
    using Type = il::core::Type;
    using Opcode = il::core::Opcode;
    using IlValue = Value;
    using IlType = Type;
    using AstType = ::il::frontends::basic::Type;

    // Type aliases for context structures (defined in LowererContext.hpp)
    using ProcedureContext = ::il::frontends::basic::ProcedureContext;
    using BlockNamer = ::il::frontends::basic::BlockNamer;
    using ForBlocks = ::il::frontends::basic::ForBlocks;

  public:
    // Re-export types from LowererTypes.hpp for API compatibility
    using RVal = ::il::frontends::basic::RVal;
    using PrintChArgString = ::il::frontends::basic::PrintChArgString;

    // =========================================================================
    // Detail Access API (Internal)
    // =========================================================================
    // Exposes a narrow, explicit surface for modular lowering helpers without
    // granting them direct friendship to the full Lowerer class.

    /// @brief Narrow public facade providing controlled access to Lowerer internals.
    /// @details Modular lowering helpers receive a DetailAccess handle instead of
    ///          friendship to the entire Lowerer class. This limits the surface area
    ///          exposed to helper modules while still allowing them to delegate back
    ///          to core lowering routines.
    class DetailAccess
    {
      public:
        /// @brief Construct a detail-access handle bound to the given Lowerer.
        /// @param lowerer The owning Lowerer instance to delegate through.
        explicit DetailAccess(Lowerer &lowerer) noexcept : lowerer_(&lowerer) {}

        /// @brief Retrieve the underlying Lowerer reference.
        /// @return The Lowerer instance backing this handle.
        [[nodiscard]] Lowerer &lowerer() const noexcept
        {
            return *lowerer_;
        }

        /// @brief Lower a variable reference expression.
        /// @param expr Variable expression AST node.
        /// @return Loaded value and its type.
        [[nodiscard]] RVal lowerVarExpr(const VarExpr &expr)
        {
            return lowerer_->lowerVarExpr(expr);
        }

        /// @brief Lower a unary expression (e.g. NOT, negation).
        /// @param expr Unary expression AST node.
        /// @return Resulting value and type.
        [[nodiscard]] RVal lowerUnaryExpr(const UnaryExpr &expr)
        {
            return lowerer_->lowerUnaryExpr(expr);
        }

        /// @brief Lower a binary expression (arithmetic, comparison, etc.).
        /// @param expr Binary expression AST node.
        /// @return Resulting value and type.
        [[nodiscard]] RVal lowerBinaryExpr(const BinaryExpr &expr)
        {
            return lowerer_->lowerBinaryExpr(expr);
        }

        /// @brief Lower a UBOUND query expression.
        /// @param expr UBOUND expression AST node naming the array.
        /// @return Resulting value and type.
        [[nodiscard]] RVal lowerUBoundExpr(const UBoundExpr &expr)
        {
            return lowerer_->lowerUBoundExpr(expr);
        }

        /// @brief Lower an IF/ELSEIF/ELSE statement.
        /// @param stmt IF statement AST node.
        void lowerIf(const IfStmt &stmt)
        {
            lowerer_->lowerIf(stmt);
        }

        /// @brief Lower a WHILE/WEND loop statement.
        /// @param stmt WHILE statement AST node.
        void lowerWhile(const WhileStmt &stmt)
        {
            lowerer_->lowerWhile(stmt);
        }

        /// @brief Lower a DO/LOOP statement.
        /// @param stmt DO statement AST node.
        void lowerDo(const DoStmt &stmt)
        {
            lowerer_->lowerDo(stmt);
        }

        /// @brief Lower a FOR/NEXT loop statement.
        /// @param stmt FOR statement AST node.
        void lowerFor(const ForStmt &stmt)
        {
            lowerer_->lowerFor(stmt);
        }

        /// @brief Lower a FOR EACH iteration statement.
        /// @param stmt FOR EACH statement AST node.
        void lowerForEach(const ForEachStmt &stmt)
        {
            lowerer_->lowerForEach(stmt);
        }

        /// @brief Lower a SELECT CASE statement.
        /// @param stmt SELECT CASE statement AST node.
        void lowerSelectCase(const SelectCaseStmt &stmt)
        {
            lowerer_->lowerSelectCase(stmt);
        }

        /// @brief Lower a NEXT statement (FOR loop increment/termination).
        /// @param stmt NEXT statement AST node.
        void lowerNext(const NextStmt &stmt)
        {
            lowerer_->lowerNext(stmt);
        }

        /// @brief Lower an EXIT statement (loop early termination).
        /// @param stmt EXIT statement AST node.
        void lowerExit(const ExitStmt &stmt)
        {
            lowerer_->lowerExit(stmt);
        }

        /// @brief Lower a GOTO statement (unconditional branch).
        /// @param stmt GOTO statement AST node.
        void lowerGoto(const GotoStmt &stmt)
        {
            lowerer_->lowerGoto(stmt);
        }

        /// @brief Lower a GOSUB statement (subroutine call with return address).
        /// @param stmt GOSUB statement AST node.
        void lowerGosub(const GosubStmt &stmt)
        {
            lowerer_->lowerGosub(stmt);
        }

        /// @brief Lower a GOSUB RETURN statement.
        /// @param stmt RETURN statement AST node.
        void lowerGosubReturn(const ReturnStmt &stmt)
        {
            lowerer_->lowerGosubReturn(stmt);
        }

        /// @brief Lower an ON ERROR GOTO statement (error handler registration).
        /// @param stmt ON ERROR GOTO statement AST node.
        void lowerOnErrorGoto(const OnErrorGoto &stmt)
        {
            lowerer_->lowerOnErrorGoto(stmt);
        }

        /// @brief Lower a RESUME statement (error handler continuation).
        /// @param stmt RESUME statement AST node.
        void lowerResume(const Resume &stmt)
        {
            lowerer_->lowerResume(stmt);
        }

        /// @brief Lower an END statement (program termination).
        /// @param stmt END statement AST node.
        void lowerEnd(const EndStmt &stmt)
        {
            lowerer_->lowerEnd(stmt);
        }

        /// @brief Lower a TRY/CATCH statement.
        /// @param stmt TRY/CATCH statement AST node.
        void lowerTryCatch(const TryCatchStmt &stmt)
        {
            lowerer_->lowerTryCatch(stmt);
        }

        /// @brief Lower a NEW expression allocating a BASIC object instance.
        /// @param expr NEW expression AST node.
        /// @return Resulting object pointer value and type.
        [[nodiscard]] RVal lowerNewExpr(const NewExpr &expr)
        {
            return lowerer_->lowerNewExpr(expr);
        }

        /// @brief Lower a NEW expression with an explicit OOP context.
        /// @param expr NEW expression AST node.
        /// @param ctx OOP lowering context providing cached metadata.
        /// @return Resulting object pointer value and type.
        [[nodiscard]] RVal lowerNewExpr(const NewExpr &expr, OopLoweringContext &ctx)
        {
            return lowerer_->lowerNewExpr(expr, ctx);
        }

        /// @brief Lower a ME expression referencing the implicit instance slot.
        /// @param expr ME expression AST node.
        /// @return Resulting self-pointer value and type.
        [[nodiscard]] RVal lowerMeExpr(const MeExpr &expr)
        {
            return lowerer_->lowerMeExpr(expr);
        }

        /// @brief Lower a ME expression with an explicit OOP context.
        /// @param expr ME expression AST node.
        /// @param ctx OOP lowering context providing cached metadata.
        /// @return Resulting self-pointer value and type.
        [[nodiscard]] RVal lowerMeExpr(const MeExpr &expr, OopLoweringContext &ctx)
        {
            return lowerer_->lowerMeExpr(expr, ctx);
        }

        /// @brief Lower a member access expression (field read).
        /// @param expr Member access expression AST node.
        /// @return Loaded field value and its type.
        [[nodiscard]] RVal lowerMemberAccessExpr(const MemberAccessExpr &expr)
        {
            return lowerer_->lowerMemberAccessExpr(expr);
        }

        /// @brief Lower a member access expression with an explicit OOP context.
        /// @param expr Member access expression AST node.
        /// @param ctx OOP lowering context providing cached metadata.
        /// @return Loaded field value and its type.
        [[nodiscard]] RVal lowerMemberAccessExpr(const MemberAccessExpr &expr,
                                                 OopLoweringContext &ctx)
        {
            return lowerer_->lowerMemberAccessExpr(expr, ctx);
        }

        /// @brief Lower a method call expression.
        /// @param expr Method call expression AST node.
        /// @return Resulting return value and its type.
        [[nodiscard]] RVal lowerMethodCallExpr(const MethodCallExpr &expr)
        {
            return lowerer_->lowerMethodCallExpr(expr);
        }

        /// @brief Lower a method call expression with an explicit OOP context.
        /// @param expr Method call expression AST node.
        /// @param ctx OOP lowering context providing cached metadata.
        /// @return Resulting return value and its type.
        [[nodiscard]] RVal lowerMethodCallExpr(const MethodCallExpr &expr, OopLoweringContext &ctx)
        {
            return lowerer_->lowerMethodCallExpr(expr, ctx);
        }

        /// @brief Lower a DELETE statement releasing an object reference.
        /// @param stmt DELETE statement AST node.
        void lowerDelete(const DeleteStmt &stmt)
        {
            lowerer_->lowerDelete(stmt);
        }

        /// @brief Lower a DELETE statement with an explicit OOP context.
        /// @param stmt DELETE statement AST node.
        /// @param ctx OOP lowering context providing cached metadata.
        void lowerDelete(const DeleteStmt &stmt, OopLoweringContext &ctx)
        {
            lowerer_->lowerDelete(stmt, ctx);
        }

        /// @brief Scan program OOP constructs to populate class layouts.
        /// @param prog Program AST root node.
        void scanOOP(const Program &prog)
        {
            lowerer_->scanOOP(prog);
        }

        /// @brief Emit constructor, destructor, and method bodies for CLASS declarations.
        /// @param prog Program AST root node.
        void emitOopDeclsAndBodies(const Program &prog)
        {
            lowerer_->emitOopDeclsAndBodies(prog);
        }

        /// @brief Lower a LET assignment statement.
        /// @param stmt LET statement AST node.
        void lowerLet(const LetStmt &stmt)
        {
            lowerer_->lowerLet(stmt);
        }

        /// @brief Lower a CONST declaration statement.
        /// @param stmt CONST statement AST node.
        void lowerConst(const ConstStmt &stmt)
        {
            lowerer_->lowerConst(stmt);
        }

        /// @brief Lower a STATIC variable declaration statement.
        /// @param stmt STATIC statement AST node.
        void lowerStatic(const StaticStmt &stmt)
        {
            lowerer_->lowerStatic(stmt);
        }

        /// @brief Lower a DIM array/variable declaration statement.
        /// @param stmt DIM statement AST node.
        void lowerDim(const DimStmt &stmt)
        {
            lowerer_->lowerDim(stmt);
        }

        /// @brief Lower a REDIM array resizing statement.
        /// @param stmt REDIM statement AST node.
        void lowerReDim(const ReDimStmt &stmt)
        {
            lowerer_->lowerReDim(stmt);
        }

        /// @brief Lower a RANDOMIZE statement (seed random number generator).
        /// @param stmt RANDOMIZE statement AST node.
        void lowerRandomize(const RandomizeStmt &stmt)
        {
            lowerer_->lowerRandomize(stmt);
        }

        /// @brief Lower a SWAP statement (exchange two variables).
        /// @param stmt SWAP statement AST node.
        void lowerSwap(const SwapStmt &stmt)
        {
            lowerer_->lowerSwap(stmt);
        }

      private:
        Lowerer *lowerer_; ///< Non-owning pointer to the backing Lowerer.
    };

    /// @brief Create a detail-access handle for modular lowering helpers.
    /// @return A lightweight facade exposing a controlled subset of Lowerer internals.
    [[nodiscard]] DetailAccess detailAccess() noexcept
    {
        return DetailAccess(*this);
    }

    // Friend declarations for I/O helper functions (must appear after RVal definition)
    friend PrintChArgString lowerPrintChArgToString(IoStatementLowerer &self,
                                                    const Expr &expr,
                                                    RVal value,
                                                    bool quoteStrings);
    friend Value buildPrintChWriteRecord(IoStatementLowerer &self, const PrintChStmt &stmt);

    // Re-export more types from LowererTypes.hpp
    using ArrayAccess = ::il::frontends::basic::ArrayAccess;
    using ArrayAccessKind = ::il::frontends::basic::ArrayAccessKind;
    using SymbolInfo = ::il::frontends::basic::SymbolInfo;

  private:
    /// @brief Aggregates state shared across helper stages of program lowering.
    struct ProgramEmitContext
    {
        std::vector<const Stmt *> mainStmts; ///< Flattened top-level statements for @main.
        Function *function{nullptr};         ///< The synthetic @main IL function.
        BasicBlock *entry{nullptr};          ///< Entry basic block of @main.
    };

    // Re-export private types from LowererTypes.hpp
    using SlotType = ::il::frontends::basic::SlotType;
    using VariableStorage = ::il::frontends::basic::VariableStorage;
    using ProcedureSignature = ::il::frontends::basic::ProcedureSignature;
    using IfBlocks = ::il::frontends::basic::IfBlocks;
    using CtrlState = ::il::frontends::basic::CtrlState;

    // Note: ProcedureContext, BlockNamer, and ForBlocks are defined in LowererContext.hpp

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

    void allocateLocalSlots(const std::unordered_set<std::string> &paramNames, bool includeParams);

    /// @brief Allocate stack slots for boolean scalars (Pass 1 of slot allocation).
    void allocateBooleanSlots(const std::unordered_set<std::string> &paramNames,
                              bool includeParams);

    /// @brief Allocate stack slots for arrays and non-boolean scalars (Pass 2).
    void allocateNonBooleanSlots(const std::unordered_set<std::string> &paramNames,
                                 bool includeParams);

    /// @brief Allocate auxiliary slots for array length tracking (Pass 3).
    void allocateArrayLengthSlots(const std::unordered_set<std::string> &paramNames,
                                  bool includeParams);

    /// @brief Check if a symbol should have a slot allocated.
    [[nodiscard]] bool shouldAllocateSlot(const std::string &name,
                                          const SymbolInfo &info,
                                          const std::unordered_set<std::string> &paramNames,
                                          bool includeParams) const;

    /// @brief Compute the IL type for a procedure parameter.
    [[nodiscard]] static il::core::Type computeParamILType(const Param &p);

    /// @brief Materialize a single parameter into a stack slot.
    void materializeSingleParam(const Param &p, size_t index, size_t ilParamOffset);

    /// @brief Resolve storage for a STATIC variable.
    [[nodiscard]] std::optional<VariableStorage> resolveStaticVariableStorage(
        std::string_view name, const SlotType &slotInfo);

    /// @brief Resolve storage for a module-level global variable.
    [[nodiscard]] std::optional<VariableStorage> resolveModuleLevelStorage(
        std::string_view name, const SlotType &slotInfo);

    /// @brief Select the appropriate rt_modvar_addr_* helper based on type kind.
    [[nodiscard]] std::string selectModvarAddrHelper(il::core::Type::Kind kind);

    void lowerStatementSequence(const std::vector<const Stmt *> &stmts,
                                bool stopOnTerminated,
                                const std::function<void(const Stmt &)> &beforeBranch = {});

    void collectProcedureSignatures(const Program &prog);

    void collectVars(const Program &prog);

    void collectVars(const std::vector<const Stmt *> &stmts);

    int virtualLine(const Stmt &s);

    void lowerFunctionDecl(const FunctionDecl &decl);

    void lowerSubDecl(const SubDecl &decl);

  public:
    /// @brief Configuration shared by FUNCTION and SUB lowering.
    struct ProcedureConfig
    {
        Type retType{Type(Type::Kind::Void)};  ///< IL return type for the procedure.
        std::function<void()> postCollect;     ///< Hook after variable discovery.
        std::function<void()> emitEmptyBody;   ///< Emit return path for empty bodies.
        std::function<void()> emitFinalReturn; ///< Emit return in the synthetic exit block.
    };

  private:
    /// @brief Lower shared procedure scaffolding for FUNCTION/SUB declarations.
    void lowerProcedure(const std::string &name,
                        const std::vector<Param> &params,
                        const std::vector<StmtPtr> &body,
                        const ProcedureConfig &config);

    /// @brief Collect declarations and cache main statement pointers.
    ProgramEmitContext collectProgramDeclarations(const Program &prog);

    /// @brief Create the main function skeleton and block mappings.
    void buildMainFunctionSkeleton(ProgramEmitContext &state);

    /// @brief Discover global variables referenced by the main body.
    void collectMainVariables(ProgramEmitContext &state);

    /// @brief Materialise stack storage for main locals and bookkeeping.
    void allocateMainLocals(ProgramEmitContext &state);

    /// @brief Emit the main body along with epilogue clean-up.
    void emitMainBodyAndEpilogue(ProgramEmitContext &state);

    /// @brief Stack-allocate parameters and seed local map.
    void materializeParams(const std::vector<Param> &params);

    /// @brief Reset procedure-level lowering state.
    void resetLoweringState();

    void lowerStmt(const Stmt &stmt);

    RVal lowerExpr(const Expr &expr);
    RVal lowerScalarExpr(const Expr &expr);
    RVal lowerScalarExpr(RVal value, il::support::SourceLoc loc);

    /// @brief Lower a variable reference expression.
    /// @param expr Variable expression node.
    /// @return Loaded value and its type.
    RVal lowerVarExpr(const VarExpr &expr);

    /// @brief Lower a unary expression (e.g. NOT).
    /// @param expr Unary expression node.
    /// @return Resulting value and type.
    RVal lowerUnaryExpr(const UnaryExpr &expr);

    /// @brief Lower a binary expression.
    /// @param expr Binary expression node.
    /// @return Resulting value and type.
    RVal lowerBinaryExpr(const BinaryExpr &expr);

    /// @brief Translate a BASIC return type hint into an IL type.
    /// @param fnName BASIC function name used for suffix inference.
    /// @param hint Explicit BASIC return type annotation, if any.
    /// @return IL type matching the BASIC semantics for the function return.
    Type functionRetTypeFromHint(const std::string &fnName, BasicType hint) const;

    /// @brief Lower a boolean expression using explicit branch bodies.
    /// @param cond Boolean condition selecting THEN branch.
    /// @param loc Source location to attribute to control flow.
    /// @param emitThen Lambda emitting THEN branch body, storing into result slot.
    /// @param emitElse Lambda emitting ELSE branch body, storing into result slot.
    /// @param thenLabelBase Optional label base for THEN block naming.
    /// @param elseLabelBase Optional label base for ELSE block naming.
    /// @param joinLabelBase Optional label base for join block naming.
    /// @return Resulting value and type.
    RVal lowerBoolBranchExpr(Value cond,
                             il::support::SourceLoc loc,
                             const std::function<void(Value)> &emitThen,
                             const std::function<void(Value)> &emitElse,
                             std::string_view thenLabelBase = {},
                             std::string_view elseLabelBase = {},
                             std::string_view joinLabelBase = {});

    /// @brief Lower logical (`AND`/`OR`) expressions with short-circuiting.
    /// @param expr Binary expression node.
    /// @return Resulting value and type.
    RVal lowerLogicalBinary(const BinaryExpr &expr);

    /// @brief Lower integer division and modulo with divide-by-zero check.
    /// @param expr Binary expression node.
    /// @return Resulting value and type.
    RVal lowerDivOrMod(const BinaryExpr &expr);

    /// @brief Lower string concatenation and equality/inequality comparisons.
    /// @param expr Binary expression node.
    /// @param lhs Pre-lowered left-hand side.
    /// @param rhs Pre-lowered right-hand side.
    /// @return Resulting value and type.
    RVal lowerStringBinary(const BinaryExpr &expr, RVal lhs, RVal rhs);

    /// @brief Lower numeric arithmetic and comparisons.
    /// @param expr Binary expression node.
    /// @param lhs Pre-lowered left-hand side.
    /// @param rhs Pre-lowered right-hand side.
    /// @return Resulting value and type.
    RVal lowerNumericBinary(const BinaryExpr &expr, RVal lhs, RVal rhs);

    /// @brief Lower the power operator by invoking the runtime helper.
    /// @param expr Binary expression node.
    /// @param lhs Pre-lowered left-hand side.
    /// @param rhs Pre-lowered right-hand side.
    /// @return Resulting double-precision value.
    RVal lowerPowBinary(const BinaryExpr &expr, RVal lhs, RVal rhs);

    /// @brief Lower a built-in call expression.
    /// @param expr Built-in call expression node.
    /// @return Resulting value and type.
    RVal lowerBuiltinCall(const BuiltinCallExpr &expr);

    /// @brief Classify the numeric type of @p expr using BASIC promotion rules.
    /// @param expr Expression to inspect.
    /// @return Numeric type describing INTEGER, LONG, SINGLE, or DOUBLE semantics.
    TypeRules::NumericType classifyNumericType(const Expr &expr);

    /// @brief Lower a UBOUND query expression.
    /// @param expr UBOUND expression node naming the array.
    /// @return Resulting value and type.
    RVal lowerUBoundExpr(const UBoundExpr &expr);

    /// @brief Lower a NEW expression allocating a BASIC object instance.
    RVal lowerNewExpr(const NewExpr &expr);
    RVal lowerNewExpr(const NewExpr &expr, OopLoweringContext &ctx);

    /// @brief Lower a ME expression referencing the implicit instance slot.
    RVal lowerMeExpr(const MeExpr &expr);
    RVal lowerMeExpr(const MeExpr &expr, OopLoweringContext &ctx);

    /// @brief Lower a member access reading a field from an object instance.
    RVal lowerMemberAccessExpr(const MemberAccessExpr &expr);
    RVal lowerMemberAccessExpr(const MemberAccessExpr &expr, OopLoweringContext &ctx);

    /// @brief Lower an object method invocation expression.
    RVal lowerMethodCallExpr(const MethodCallExpr &expr);
    RVal lowerMethodCallExpr(const MethodCallExpr &expr, OopLoweringContext &ctx);

    /// @brief Lower a DELETE statement releasing an object reference.
    void lowerDelete(const DeleteStmt &stmt);
    void lowerDelete(const DeleteStmt &stmt, OopLoweringContext &ctx);

    // Shared argument helpers
    RVal coerceToI64(RVal v, il::support::SourceLoc loc);

    RVal coerceToF64(RVal v, il::support::SourceLoc loc);

    RVal coerceToBool(RVal v, il::support::SourceLoc loc);

    RVal ensureI64(RVal v, il::support::SourceLoc loc);

    RVal ensureF64(RVal v, il::support::SourceLoc loc);

    PrintChArgString lowerPrintChArgToString(const Expr &expr, RVal value, bool quoteStrings);

    Value buildPrintChWriteRecord(const PrintChStmt &stmt);

    // -------------------------------------------------------------------------
    // Core statement lowering (formerly LowerStmt_Core.hpp)
    // -------------------------------------------------------------------------
    void lowerStmtList(const StmtList &stmt);
    void lowerCallStmt(const CallStmt &stmt);
    void lowerReturn(const ReturnStmt &stmt);
    RVal normalizeChannelToI32(RVal channel, il::support::SourceLoc loc);
    void emitRuntimeErrCheck(Value err,
                             il::support::SourceLoc loc,
                             std::string_view labelStem,
                             const std::function<void(Value)> &onFailure);

    // -------------------------------------------------------------------------
    // Runtime statement lowering (formerly LowerStmt_Runtime.hpp)
    // -------------------------------------------------------------------------
    void lowerLet(const LetStmt &stmt);
    void lowerConst(const ConstStmt &stmt);
    void lowerStatic(const StaticStmt &stmt);
    void assignScalarSlot(const SlotType &slotInfo,
                          Value slot,
                          RVal value,
                          il::support::SourceLoc loc);
    void assignArrayElement(const ArrayExpr &target, RVal value, il::support::SourceLoc loc);
    void lowerDim(const DimStmt &stmt);
    void lowerReDim(const ReDimStmt &stmt);
    void lowerRandomize(const RandomizeStmt &stmt);
    void lowerTryCatch(const TryCatchStmt &stmt);
    void lowerUsingStmt(const UsingStmt &stmt);
    void lowerSwap(const SwapStmt &stmt);
    void visit(const BeepStmt &stmt);
    void visit(const ClsStmt &stmt);
    void visit(const ColorStmt &stmt);
    void visit(const SleepStmt &stmt);
    void visit(const LocateStmt &stmt);
    void visit(const CursorStmt &stmt);
    void visit(const AltScreenStmt &stmt);
    Value emitArrayLengthCheck(Value bound, il::support::SourceLoc loc, std::string_view labelBase);

    // -------------------------------------------------------------------------
    // I/O statement lowering (formerly LowerStmt_IO.hpp)
    // -------------------------------------------------------------------------
    void lowerOpen(const OpenStmt &stmt);
    void lowerClose(const CloseStmt &stmt);
    void lowerSeek(const SeekStmt &stmt);
    void lowerPrint(const PrintStmt &stmt);
    void lowerPrintCh(const PrintChStmt &stmt);
    void lowerInput(const InputStmt &stmt);
    void lowerInputCh(const InputChStmt &stmt);
    void lowerLineInputCh(const LineInputChStmt &stmt);

    // -------------------------------------------------------------------------
    // Control flow lowering (formerly LowerStmt_Control.hpp)
    // -------------------------------------------------------------------------

    /// @brief Emit blocks for an IF/ELSEIF chain.
    /// @param conds Number of conditions (IF + ELSEIFs).
    /// @return Indices for test/then blocks and ELSE/exit blocks.
    IfBlocks emitIfBlocks(size_t conds);
    void lowerIfCondition(const Expr &cond,
                          BasicBlock *testBlk,
                          BasicBlock *thenBlk,
                          BasicBlock *falseBlk,
                          il::support::SourceLoc loc);
    void lowerCondBranch(const Expr &expr,
                         BasicBlock *trueBlk,
                         BasicBlock *falseBlk,
                         il::support::SourceLoc loc);
    bool lowerIfBranch(const Stmt *stmt,
                       BasicBlock *thenBlk,
                       size_t exitIdx,
                       il::support::SourceLoc loc);
    CtrlState emitIf(const IfStmt &stmt);
    void lowerIf(const IfStmt &stmt);
    void lowerLoopBody(const std::vector<StmtPtr> &body);
    CtrlState emitWhile(const WhileStmt &stmt);
    void lowerWhile(const WhileStmt &stmt);
    CtrlState emitDo(const DoStmt &stmt);
    void lowerDo(const DoStmt &stmt);
    ForBlocks setupForBlocks(bool varStep);
    void lowerForConstStep(const ForStmt &stmt, Value slot, RVal end, RVal step, int64_t stepConst);
    void lowerForVarStep(const ForStmt &stmt, Value slot, RVal end, RVal step);
    CtrlState emitFor(const ForStmt &stmt, Value slot, RVal end, RVal step);
    void lowerFor(const ForStmt &stmt);
    void lowerForEach(const ForEachStmt &stmt);
    void emitForStep(Value slot, Value step);
    CtrlState emitSelect(const SelectCaseStmt &stmt);
    void lowerNext(const NextStmt &stmt);
    void lowerExit(const ExitStmt &stmt);
    void lowerGosub(const GosubStmt &stmt);
    void lowerGoto(const GotoStmt &stmt);
    void lowerGosubReturn(const ReturnStmt &stmt);
    void lowerOnErrorGoto(const OnErrorGoto &stmt);
    void lowerResume(const Resume &stmt);
    void lowerEnd(const EndStmt &stmt);

    void lowerSelectCase(const SelectCaseStmt &stmt);

    // helpers
    IlType ilBoolTy();

    IlValue emitBoolConst(bool v);

    IlValue emitBoolFromBranches(const std::function<void(Value)> &emitThen,
                                 const std::function<void(Value)> &emitElse,
                                 std::string_view thenLabelBase = "bool_then",
                                 std::string_view elseLabelBase = "bool_else",
                                 std::string_view joinLabelBase = "bool_join");

    Value emitAlloca(int bytes);

    Value emitLoad(Type ty, Value addr);

    void emitStore(Type ty, Value addr, Value val);

    Value emitBinary(Opcode op, Type ty, Value lhs, Value rhs);

    /// @brief Emit unary instruction of @p op on @p val producing @p ty.
    Value emitUnary(Opcode op, Type ty, Value val);

    Value emitConstI64(std::int64_t v);

    Value emitZext1ToI64(Value val);

    Value emitISub(Value lhs, Value rhs);

    Value emitBasicLogicalI64(Value b1);
    Emit emitCommon() noexcept;
    Emit emitCommon(il::support::SourceLoc loc) noexcept;

    /// @brief Get the type coercion engine for this lowerer.
    /// @return Reference to the coercion engine.
    TypeCoercionEngine &coercion() noexcept;

    /// @brief Narrow a 64-bit value to 32 bits for runtime calls.
    /// @details Convenience wrapper around emitCommon().to_iN(value, 32) that reduces
    ///          boilerplate when preparing arguments for 32-bit runtime functions.
    /// @param value The 64-bit value to narrow.
    /// @param loc Source location for the narrowing operation.
    /// @return Narrowed 32-bit value.
    Value narrow32(Value value, il::support::SourceLoc loc);

    /// @brief Emit checked integer negation for @p val producing type @p ty.
    Value emitCheckedNeg(Type ty, Value val);

    void emitBr(BasicBlock *target);

    void emitCBr(Value cond, BasicBlock *t, BasicBlock *f);

    Value emitCallRet(Type ty, const std::string &callee, const std::vector<Value> &args);

    void emitCall(const std::string &callee, const std::vector<Value> &args);

    /// @brief Request runtime helper and emit call in a single operation.
    /// @details Combines requestHelper() and emitCallRet() to reduce boilerplate in
    ///          lowering code that needs to call runtime functions.
    /// @param feature Runtime feature to request (ensures helper is linked).
    /// @param callee Name of the runtime function to call.
    /// @param returnType Return type of the runtime function.
    /// @param args Arguments to pass to the runtime function.
    /// @return Value representing the call result.
    Value emitRuntimeHelper(il::runtime::RuntimeFeature feature,
                            const std::string &callee,
                            Type returnType,
                            const std::vector<Value> &args);

    /// @brief Emit an indirect call where the callee is a value operand.
    Value emitCallIndirectRet(Type ty, Value callee, const std::vector<Value> &args);

    /// @brief Emit a void-typed indirect call where the callee is a value operand.
    void emitCallIndirect(Value callee, const std::vector<Value> &args);

    Value emitConstStr(const std::string &globalName);
    void storeArray(Value slot, Value value, AstType elementType = AstType::I64);
    void storeArray(Value slot, Value value, AstType elementType, bool isObjectArray);
    void releaseArrayLocals(const std::unordered_set<std::string> &paramNames);
    void releaseArrayParams(const std::unordered_set<std::string> &paramNames);
    void releaseObjectLocals(const std::unordered_set<std::string> &paramNames);
    void releaseObjectParams(const std::unordered_set<std::string> &paramNames);
    void releaseDeferredTemps();
    void clearDeferredTemps();
    // Defer releases for temporaries
    void deferReleaseStr(Value v);
    void deferReleaseObj(Value v, const std::string &className = {});

    void emitTrap();

    void emitTrapFromErr(Value errCode);

    void emitEhPush(BasicBlock *handler);
    void emitEhPop();
    void emitEhPopForReturn();
    void clearActiveErrorHandler();
    BasicBlock *ensureErrorHandlerBlock(int targetLine);

    void emitRet(Value v);

    void emitRetVoid();

    std::string getStringLabel(const std::string &s);

    unsigned nextTempId();

    std::string nextFallbackBlockLabel();

    lower::Emitter &emitter() noexcept;
    const lower::Emitter &emitter() const noexcept;

    ArrayAccess lowerArrayAccess(const ArrayExpr &expr, ArrayAccessKind kind);

    void emitProgram(const Program &prog);

    void ensureGosubStack();

    std::unique_ptr<ProgramLowering> programLowering;
    std::unique_ptr<ProcedureLowering> procedureLowering;
    std::unique_ptr<StatementLowering> statementLowering;

    build::IRBuilder *builder{nullptr};
    Module *mod{nullptr};
    NameMangler mangler;
    SymbolTable symbolTable_;        ///< Unified symbol table.
    SymbolTable::SymbolMap &symbols; ///< Legacy alias (references symbolTable_.raw()).
    StringTable stringTable_;        ///< String literal interning.
    il::support::SourceLoc curLoc{}; ///< current source location for emitted IR
    bool boundsChecks{false};
    static constexpr int kGosubStackDepth = 128;
    size_t nextStringId{0}; ///< @deprecated Use stringTable_ instead.
    size_t nextFallbackBlockId{0};
    std::unordered_map<std::string, ProcedureSignature> procSignatures;
    std::unordered_map<std::string, std::string> procNameAliases;

    std::unordered_map<const Stmt *, int> stmtVirtualLines_;
    int synthLineBase_{-1000000000};
    int synthSeq_{0};

    ProcedureContext context_;

    std::unique_ptr<lower::Emitter> emitter_;
    std::unique_ptr<TypeCoercionEngine> coercionEngine_;

    DiagnosticEmitter *diagnosticEmitter_{nullptr};
    const SemanticAnalyzer *semanticAnalyzer_{nullptr};

    // Names of module-level variables referenced inside procedures; used to
    // route main's accesses to runtime-backed storage for cross-proc sharing.
    std::unordered_set<std::string> crossProcGlobals_;

  public:
    /// @brief Mark a module-level variable as accessed from within a procedure.
    /// @details Variables marked cross-proc are routed through runtime-backed
    ///          storage so that main and procedure scopes share the same value.
    /// @param name BASIC symbol name to mark.
    void markCrossProcGlobal(const std::string &name)
    {
        crossProcGlobals_.insert(name);
    }

    /// @brief Check whether a variable has been marked as cross-procedure global.
    /// @param name BASIC symbol name to query.
    /// @return True when the symbol was previously marked via markCrossProcGlobal.
    bool isCrossProcGlobal(const std::string &name) const
    {
        return crossProcGlobals_.find(name) != crossProcGlobals_.end();
    }

    // runtime requirement tracking
    using RuntimeFeature = il::runtime::RuntimeFeature;

    RuntimeHelperTracker runtimeTracker;

    // Re-export ManualRuntimeHelper from LowererRuntimeHelpers.hpp
    using ManualRuntimeHelper = ::il::frontends::basic::ManualRuntimeHelper;
    /// @brief Total number of manual runtime helpers tracked by the lowerer.
    static constexpr std::size_t manualRuntimeHelperCount = kManualRuntimeHelperCount;

    /// @brief Map a ManualRuntimeHelper enum to its array index.
    /// @param helper The manual runtime helper to index.
    /// @return Zero-based index suitable for array subscript.
    static constexpr std::size_t manualRuntimeHelperIndex(ManualRuntimeHelper helper) noexcept
    {
        return ::il::frontends::basic::manualRuntimeHelperIndex(helper);
    }

    ManualHelperRequirements manualHelperRequirements_{};

    /// @brief Flag a manual runtime helper as required for the current compilation.
    /// @param helper The manual runtime helper to require.
    void setManualHelperRequired(ManualRuntimeHelper helper);

    /// @brief Check whether a manual runtime helper has been flagged as required.
    /// @param helper The manual runtime helper to query.
    /// @return True when the helper was previously required.
    [[nodiscard]] bool isManualHelperRequired(ManualRuntimeHelper helper) const;

    /// @brief Reset all manual helper requirement flags.
    void resetManualHelpers();

    void requireTrap();
    void requireArrayI32New();
    void requireArrayI32Resize();
    void requireArrayI32Len();
    void requireArrayI32Get();
    void requireArrayI32Set();
    void requireArrayI32Retain();
    void requireArrayI32Release();
    void requireArrayOobPanic();
    // I64 array helpers (for LONG arrays)
    void requireArrayI64New();
    void requireArrayI64Resize();
    void requireArrayI64Len();
    void requireArrayI64Get();
    void requireArrayI64Set();
    void requireArrayI64Retain();
    void requireArrayI64Release();
    // F64 array helpers (for SINGLE/DOUBLE arrays)
    void requireArrayF64New();
    void requireArrayF64Resize();
    void requireArrayF64Len();
    void requireArrayF64Get();
    void requireArrayF64Set();
    void requireArrayF64Retain();
    void requireArrayF64Release();

    void requireArrayStrAlloc();
    void requireArrayStrRelease();
    void requireArrayStrGet();
    void requireArrayStrPut();
    void requireArrayStrLen();
    // Object array helpers
    void requireArrayObjNew();
    void requireArrayObjLen();
    void requireArrayObjGet();
    void requireArrayObjPut();
    void requireArrayObjResize();
    void requireArrayObjRelease();
    void requireOpenErrVstr();
    void requireCloseErr();
    void requireSeekChErr();
    void requireWriteChErr();
    void requirePrintlnChErr();
    void requireLineInputChErr();
    // --- begin: require declarations ---
    void requireEofCh();
    void requireLofCh();
    void requireLocCh();
    // Module-level globals helpers
    void requireModvarAddrI64();
    void requireModvarAddrF64();
    void requireModvarAddrI1();
    void requireModvarAddrPtr();
    void requireModvarAddrStr();
    // --- end: require declarations ---
    void requireStrRetainMaybe();
    void requireStrReleaseMaybe();
    void requireSleepMs();
    void requireTimerMs();
    /// @brief Request that a runtime feature be declared and linked.
    /// @param feature Runtime feature to make available in the emitted module.
    void requestHelper(RuntimeFeature feature);

    /// @brief Check whether a runtime feature has been requested.
    /// @param feature Runtime feature to query.
    /// @return True when the feature was previously requested.
    bool isHelperNeeded(RuntimeFeature feature) const;

    /// @brief Record that a runtime feature is used without immediately declaring it.
    /// @param feature Runtime feature to track for later declaration.
    void trackRuntime(RuntimeFeature feature);

    /// @brief Emit IL declarations for all runtime helpers that have been requested.
    /// @param b IRBuilder used to emit function declarations into the module.
    void declareRequiredRuntime(build::IRBuilder &b);
#include "frontends/basic/LowerScan.hpp"
  public:
    // Re-export OOP types from LowererTypes.hpp
    using ClassLayout = ::il::frontends::basic::ClassLayout;
    using MemberFieldAccess = ::il::frontends::basic::MemberFieldAccess;
    using FieldScope = ::il::frontends::basic::FieldScope;

  private:
    /// @brief Scan program OOP constructs to populate class layouts and runtime requests.
    void scanOOP(const Program &prog);

    /// @brief Emit constructor, destructor, and method bodies for CLASS declarations.
    void emitOopDeclsAndBodies(const Program &prog);

    void emitClassConstructor(const ClassDecl &klass, const ConstructorDecl &ctor);

    void emitClassDestructor(const ClassDecl &klass, const DestructorDecl *userDtor);

    void emitClassMethod(const ClassDecl &klass, const MethodDecl &method);

    /// @brief Emit IL for a BASIC class method, supplying an explicit body.
    /// @details Variant used by PROPERTY synthesis to reuse accessor bodies
    ///          without attempting to transfer ownership of AST nodes.
    void emitClassMethodWithBody(const ClassDecl &klass,
                                 const MethodDecl &method,
                                 const std::vector<const Stmt *> &bodyStmts);

    unsigned materializeSelfSlot(const std::string &className, Function &fn);

    Value loadSelfPointer(unsigned slotId);

    void emitFieldReleaseSequence(Value selfPtr, const ClassLayout &layout);

    /// @brief I/O statement lowering subsystem.
    std::unique_ptr<IoStatementLowerer> ioStmtLowerer_;

    /// @brief Control flow statement lowering subsystem.
    std::unique_ptr<ControlStatementLowerer> ctrlStmtLowerer_;

    /// @brief Runtime statement lowering subsystem.
    std::unique_ptr<RuntimeStatementLowerer> runtimeStmtLowerer_;

    /// @brief Cached layout table indexed by class or TYPE name.
    std::unordered_map<std::string, ClassLayout, StringHash, std::equal_to<>> classLayouts_;

    /// @brief Indexed CLASS metadata collected during scanning.
    OopIndex oopIndex_;

    /// @brief OOP lowering context for passing through the pipeline.
    std::unique_ptr<OopLoweringContext> oopContext_;

    // Field scopes are now managed internally by symbolTable_.

    /// @brief Active namespace path segments used for qualification during lowering.
    std::vector<std::string> nsStack_;

    /// @brief Stack of fully-qualified class names for access checking during lowering.
    std::vector<std::string> classStack_;

    /// @brief Determine the BASIC class associated with an object expression.
    [[nodiscard]] std::string resolveObjectClass(const Expr &expr) const;

    /// @brief Resolve pointer and type information for a member access expression.
    [[nodiscard]] std::optional<MemberFieldAccess> resolveMemberField(const MemberAccessExpr &expr);
    [[nodiscard]] std::optional<MemberFieldAccess> resolveMemberField(const MemberAccessExpr &expr,
                                                                      OopLoweringContext &ctx);

    [[nodiscard]] std::optional<MemberFieldAccess> resolveImplicitField(std::string_view name,
                                                                        il::support::SourceLoc loc);
    [[nodiscard]] std::optional<MemberFieldAccess> resolveImplicitField(std::string_view name,
                                                                        il::support::SourceLoc loc,
                                                                        OopLoweringContext &ctx);
    [[nodiscard]] std::optional<VariableStorage> resolveVariableStorage(std::string_view name,
                                                                        il::support::SourceLoc loc);
    [[nodiscard]] const FieldScope *activeFieldScope() const;

    // --- BUG-109: Canonical class-layout resolution helpers ---
  public:
    /// @brief Resolve a class layout by canonicalizing name (qualified/unqualified, casing).
    /// @param className Class name as discovered (may be qualified or different casing).
    /// @return Pointer to layout when found; nullptr otherwise.
    [[nodiscard]] const ClassLayout *findClassLayout(std::string_view className) const;

  private:
    /// @brief Compute the canonical layout key (unqualified, declared casing) for a class name.
    /// @details Uses the OOP index to normalize qualified names and casing, then returns the
    ///          unqualified identifier used as the key in classLayouts_. Falls back to the last
    ///          dotted segment when lookup fails.
    [[nodiscard]] std::string canonicalLayoutKey(std::string_view className) const;

  public:
    /// @brief Ensure a symbol entry exists, creating one with defaults if absent.
    /// @param name BASIC symbol name (case-insensitive lookup).
    /// @return Mutable reference to the existing or newly-created SymbolInfo.
    SymbolInfo &ensureSymbol(std::string_view name);

    /// @brief Find a symbol by name.
    /// @param name BASIC symbol name (case-insensitive lookup).
    /// @return Pointer to the SymbolInfo, or nullptr when not found.
    SymbolInfo *findSymbol(std::string_view name);

    /// @brief Find a symbol by name (const overload).
    /// @param name BASIC symbol name (case-insensitive lookup).
    /// @return Pointer to the SymbolInfo, or nullptr when not found.
    const SymbolInfo *findSymbol(std::string_view name) const;

    /// @brief Check whether a name corresponds to a field in the active class scope.
    /// @param name Identifier to check against the current field scope.
    /// @return True when the name matches a field in the active class layout.
    [[nodiscard]] bool isFieldInScope(std::string_view name) const;

    /// @brief Push a new field scope for the given class during method lowering.
    /// @param className Fully-qualified class name whose fields become visible.
    void pushFieldScope(const std::string &className);

    /// @brief Pop the current field scope after completing class method lowering.
    void popFieldScope();

    /// @brief Mark a symbol as referenced during lowering.
    /// @param name BASIC symbol name to mark.
    void markSymbolReferenced(std::string_view name);

    /// @brief Mark a symbol as an array variable.
    /// @param name BASIC symbol name to mark.
    void markArray(std::string_view name);

    /// @brief Mark a symbol as a STATIC procedure-local variable.
    /// @param name BASIC symbol name to mark.
    void markStatic(std::string_view name);

    /// @brief Set the explicit BASIC type for a symbol.
    /// @param name BASIC symbol name to update.
    /// @param type BASIC type to assign.
    void setSymbolType(std::string_view name, AstType type);

  private:
    SlotType getSlotType(std::string_view name) const;

    void resetSymbolState();

    // --- Module-level object array typing cache (BUG-097) ---
    /// @brief Map of module-level array names to their element class (qualified).
    /// @details Populated from the main-body variable discovery so procedure
    ///          scopes can recover object element types for global arrays even
    ///          after per-procedure symbol resets.
    std::unordered_map<std::string, std::string, StringHash, std::equal_to<>>
        moduleObjArrayElemClass_;
    /// @brief BUG-107 fix: Cache for module-level scalar object types.
    std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> moduleObjectClass_;
    /// @brief BUG-OOP-011 fix: Set of module-level string array names.
    std::unordered_set<std::string, StringHash, std::equal_to<>> moduleStrArrayNames_;
    /// @brief BUG-BAS-002 fix: Track current procedure parameter names.
    /// @details Parameters should not be resolved via moduleObjectClass_ cache
    ///          to prevent module-level object variables from polluting parameter types.
    std::unordered_set<std::string, StringHash, std::equal_to<>> currentProcParamNames_;

  public:
    /// @brief Register a parameter name for the current procedure.
    /// @details Called during parameter initialization to prevent module-level
    ///          object variable lookups from overriding parameter types.
    void registerProcParam(std::string_view name)
    {
        if (!name.empty())
            currentProcParamNames_.emplace(name);
    }

    /// @brief Clear current procedure parameter tracking.
    void clearProcParams()
    {
        currentProcParamNames_.clear();
    }

    /// @brief Check if a name is a parameter in the current procedure.
    [[nodiscard]] bool isProcParam(std::string_view name) const
    {
        return currentProcParamNames_.find(name) != currentProcParamNames_.end();
    }

  public:
    /// @brief Clear cached module-level object array and scalar object typing.
    void clearModuleObjectArrayCache()
    {
        moduleObjArrayElemClass_.clear();
        moduleObjectClass_.clear();
        moduleStrArrayNames_.clear();
    }

    /// @brief Scan AST for module-level DIM object arrays and cache their types.
    /// @details Called before lowering procedures so object array element classes
    ///          are available for method dispatch resolution in procedure bodies.
    ///          BUG-097 fix: Populate cache from AST rather than symbol table.
    /// @param main Main body statements from the Program AST.
    void cacheModuleObjectArraysFromAST(const std::vector<StmtPtr> &main);

    /// @brief Snapshot object-array typings from current symbol table.
    /// @details Intended to be called after collecting main variables so
    ///          global DIM object arrays are recorded for later procedure use.
    void cacheModuleObjectArraysFromSymbols();

    /// @brief Lookup element class for a module-level array by name.
    /// @return Qualified class name when known, empty otherwise.
    [[nodiscard]] std::string lookupModuleArrayElemClass(std::string_view name) const;

    /// @brief BUG-OOP-011 fix: Check if name is a module-level string array.
    /// @return True if the name is a cached module-level string array.
    [[nodiscard]] bool isModuleStrArray(std::string_view name) const;

  public:
    /// @brief Lookup a cached procedure signature by BASIC name.
    /// @return Pointer to the signature when present, nullptr otherwise.
    const ProcedureSignature *findProcSignature(const std::string &name) const;

    /// @brief Resolve the emitted callee name to a qualified form when known.
    [[nodiscard]] std::string resolveCalleeName(const std::string &name) const;

    /// @brief Lookup the AST return type recorded for a class method.
    std::optional<::il::frontends::basic::Type> findMethodReturnType(
        std::string_view className, std::string_view methodName) const;

    /// @brief Lookup the return class name for a method that returns an object.
    /// @return Qualified class name if method returns object, empty string otherwise.
    [[nodiscard]] std::string findMethodReturnClassName(std::string_view className,
                                                        std::string_view methodName) const;

    /// @brief Lookup the AST type for a class field.
    std::optional<::il::frontends::basic::Type> findFieldType(std::string_view className,
                                                              std::string_view fieldName) const;

    /// @brief Check if a class field is an array.
    [[nodiscard]] bool isFieldArray(std::string_view className, std::string_view fieldName) const;

    /// @brief Access the mutable procedure-level lowering context.
    /// @return Reference to the active ProcedureContext.
    [[nodiscard]] ProcedureContext &context() noexcept;

    /// @brief Access the immutable procedure-level lowering context.
    /// @return Const reference to the active ProcedureContext.
    [[nodiscard]] const ProcedureContext &context() const noexcept;

    // Class access control context -----------------------------------------------------
    /// @brief Push the fully-qualified class name currently being lowered.
    void pushClass(const std::string &qname)
    {
        classStack_.push_back(qname);
    }

    /// @brief Pop the current class lowering context.
    void popClass()
    {
        if (!classStack_.empty())
            classStack_.pop_back();
    }

    /// @brief Return the active class lowering context, or empty when not in a class.
    [[nodiscard]] std::string currentClass() const
    {
        return classStack_.empty() ? std::string() : classStack_.back();
    }
};

} // namespace il::frontends::basic

#include "frontends/basic/LoweringPipeline.hpp"
