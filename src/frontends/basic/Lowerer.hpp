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
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::frontends::basic
{

/// @brief Lowers BASIC AST into IL Module.
/// @invariant Generates deterministic block names per procedure using BlockNamer.
/// @ownership Owns produced Module; uses IRBuilder for structure emission.
class Lowerer
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

        explicit BlockNamer(std::string p) : proc(std::move(p)) {}

        std::string entry() const
        {
            return "entry_" + proc;
        }

        std::string ret() const
        {
            return "ret_" + proc;
        }

        std::string line(int line) const
        {
            return "L" + std::to_string(line) + "_" + proc;
        }

        unsigned nextIf()
        {
            return ifCounter++;
        }

        std::string ifTest(unsigned id) const
        {
            return "if_test_" + std::to_string(id) + "_" + proc;
        }

        std::string ifThen(unsigned id) const
        {
            return "if_then_" + std::to_string(id) + "_" + proc;
        }

        std::string ifElse(unsigned id) const
        {
            return "if_else_" + std::to_string(id) + "_" + proc;
        }

        std::string ifEnd(unsigned id) const
        {
            return "if_end_" + std::to_string(id) + "_" + proc;
        }

        unsigned nextWhile()
        {
            return loopCounter++;
        }

        std::string whileHead(unsigned id) const
        {
            return "while_head_" + std::to_string(id) + "_" + proc;
        }

        std::string whileBody(unsigned id) const
        {
            return "while_body_" + std::to_string(id) + "_" + proc;
        }

        std::string whileEnd(unsigned id) const
        {
            return "while_end_" + std::to_string(id) + "_" + proc;
        }

        unsigned nextFor()
        {
            return loopCounter++;
        }

        /// @brief Allocate next sequential ID for a call continuation.
        unsigned nextCall()
        {
            return loopCounter++;
        }

        std::string forHead(unsigned id) const
        {
            return "for_head_" + std::to_string(id) + "_" + proc;
        }

        std::string forBody(unsigned id) const
        {
            return "for_body_" + std::to_string(id) + "_" + proc;
        }

        std::string forInc(unsigned id) const
        {
            return "for_inc_" + std::to_string(id) + "_" + proc;
        }

        std::string forEnd(unsigned id) const
        {
            return "for_end_" + std::to_string(id) + "_" + proc;
        }

        /// @brief Build label for a synthetic call continuation block.
        std::string callCont(unsigned id) const
        {
            return "call_cont_" + std::to_string(id) + "_" + proc;
        }

        std::string generic(const std::string &hint)
        {
            auto &n = genericCounters[hint];
            std::string label = hint + "_" + std::to_string(n++) + "_" + proc;
            return label;
        }

        std::string tag(const std::string &base) const
        {
            return base + "_" + proc;
        }
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
    bool boundsChecks{false};
    unsigned boundsCheckId{0};

    Value *boolBranchSlotPtr{nullptr};

    // runtime requirement tracking
    bool needInputLine{false};
    bool needRtToInt{false};
    bool needRtIntToStr{false};
    bool needRtF64ToStr{false};
    bool needAlloc{false};
    bool needRtStrEq{false};
    bool needRtConcat{false};
    bool needRtLeft{false};
    bool needRtRight{false};
    bool needRtMid2{false};
    bool needRtMid3{false};
    bool needRtInstr2{false};
    bool needRtInstr3{false};
    bool needRtLtrim{false};
    bool needRtRtrim{false};
    bool needRtTrim{false};
    bool needRtUcase{false};
    bool needRtLcase{false};
    bool needRtChr{false};
    bool needRtAsc{false};

#include "frontends/basic/LowerRuntime.hpp"
#include "frontends/basic/LowerScan.hpp"
};

} // namespace il::frontends::basic
