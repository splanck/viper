//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the IRBuilder class, which provides a high-level API for
// constructing IL modules programmatically. IRBuilder is the primary interface
// used by frontend compilers to generate IL code from source languages.
//
// The IRBuilder class manages the insertion point (current basic block) and
// provides fluent methods for emitting instructions, managing control flow,
// and tracking SSA temporaries. It enforces structural invariants like block
// termination and simplifies common patterns like creating branches, calls,
// and arithmetic operations.
//
// Key Capabilities:
// - Module construction: Add externs, globals, and functions
// - Block management: Create blocks, set insertion points, track terminators
// - Instruction emission: Arithmetic, comparisons, memory ops, control flow
// - SSA management: Automatic temporary ID assignment and tracking
// - Type safety: Type-aware instruction constructors
// - Source locations: Attach line/column info for diagnostics
//
// Typical Usage Pattern:
//   Module m;
//   IRBuilder builder(m);
//   auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
//   auto &entry = builder.createBlock(fn, "entry");
//   builder.setInsertPoint(entry);
//   auto result = builder.add(builder.constInt(10), builder.constInt(32));
//   builder.ret(result);
//
// Design Philosophy:
// - Stateful: Maintains insertion point for sequential code generation
// - Fluent: Methods return values that can be immediately used as operands
// - Safe: Validates block termination and SSA invariants
// - Minimal: Focused on IR construction, not analysis or transformation
//
// The IRBuilder does NOT own the Module it operates on. The caller must ensure
// the Module outlives all builder operations. This allows multiple builders to
// work on the same module (though not concurrently on the same function).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include "support/source_location.hpp"
#include <optional>
#include <unordered_map>
#include <vector>

namespace il::build
{

/// @brief Helper to construct IL modules and enforce block termination.
class IRBuilder
{
  public:
    /// @brief Create builder operating on module @p m.
    /// @param m Module to mutate.
    explicit IRBuilder(il::core::Module &m);

    /// @brief Add an external function declaration.
    /// @param name Symbol name.
    /// @param ret Return type.
    /// @param params Parameter types.
    /// @return Reference to newly added extern.
    il::core::Extern &addExtern(const std::string &name,
                                il::core::Type ret,
                                const std::vector<il::core::Type> &params);

    /// @brief Add a global variable.
    /// @param name Global identifier.
    /// @param type Variable type.
    /// @param init Optional initializer (empty for zero-initialized).
    /// @return Reference to newly added global.
    il::core::Global &addGlobal(const std::string &name,
                                il::core::Type type,
                                const std::string &init = "");

    /// @brief Add a global string constant.
    /// @param name Global identifier.
    /// @param value UTF-8 string literal.
    /// @return Reference to newly added global.
    il::core::Global &addGlobalStr(const std::string &name, const std::string &value);

    /// @brief Begin definition of function @p name.
    /// @param name Function identifier.
    /// @param ret Return type.
    /// @param params Parameter list.
    /// @return Reference to created function.
    il::core::Function &startFunction(const std::string &name,
                                      il::core::Type ret,
                                      const std::vector<il::core::Param> &params);

    /// @brief Create a basic block with optional parameters.
    /// @param fn Function receiving the block.
    /// @param label Block label.
    /// @param params Block parameter list.
    /// @return Reference to new block.
    il::core::BasicBlock &createBlock(il::core::Function &fn,
                                      const std::string &label,
                                      const std::vector<il::core::Param> &params = {});

    /// @brief Backward-compatible helper without parameters.
    il::core::BasicBlock &addBlock(il::core::Function &fn, const std::string &label);

    /// @brief Insert a basic block at a specific index in @p fn.
    /// @param fn Function receiving the block.
    /// @param idx Zero-based insertion index into @ref il::core::Function::blocks.
    /// @param label Block label.
    /// @return Reference to the inserted block.
    /// @note This variant creates a parameter-less block and does not change the
    ///       current insertion point. Use when block order must precede an
    ///       existing block such as a synthetic exit block.
    il::core::BasicBlock &insertBlock(il::core::Function &fn, size_t idx, const std::string &label);

    /// @brief Access parameter @p idx of block @p bb as a value.
    il::core::Value blockParam(il::core::BasicBlock &bb, unsigned idx);

    /// @brief Emit unconditional branch to @p dst with arguments @p args.
    void br(il::core::BasicBlock &dst, const std::vector<il::core::Value> &args = {});

    /// @brief Emit conditional branch.
    void cbr(il::core::Value cond,
             il::core::BasicBlock &t,
             const std::vector<il::core::Value> &targs,
             il::core::BasicBlock &f,
             const std::vector<il::core::Value> &fargs);

    /// @brief Set current insertion point to block @p bb.
    /// @param bb Target block.
    void setInsertPoint(il::core::BasicBlock &bb);

    /// @brief Emit reference to global string @p globalName.
    /// @param globalName Name of global string.
    /// @return Value representing constant string pointer.
    il::core::Value emitConstStr(const std::string &globalName, il::support::SourceLoc loc);

    /// @brief Emit call to function @p callee with arguments @p args.
    /// @param callee Name of function to call.
    /// @param args Argument values.
    /// @param dst Optional destination value to store result.
    /// @param loc Source location for diagnostics.
    /// @pre @p callee must have been previously registered via registerCallee().
    /// @throws std::logic_error if @p callee is unknown (programming error).
    void emitCall(const std::string &callee,
                  const std::vector<il::core::Value> &args,
                  const std::optional<il::core::Value> &dst,
                  il::support::SourceLoc loc);

    /// @brief Emit return from current function.
    /// @param v Optional return value.
    void emitRet(const std::optional<il::core::Value> &v, il::support::SourceLoc loc);

    /// @brief Emit resume that rethrows the current error within the same handler.
    /// @param token Resume token supplied by the active handler.
    /// @param loc Source location for diagnostics.
    void emitResumeSame(il::core::Value token, il::support::SourceLoc loc);

    /// @brief Emit resume that propagates to the next enclosing handler.
    /// @param token Resume token supplied by the active handler.
    /// @param loc Source location for diagnostics.
    void emitResumeNext(il::core::Value token, il::support::SourceLoc loc);

    /// @brief Emit resume to a specific handler block label.
    /// @param token Resume token supplied by the active handler.
    /// @param target Handler block receiving control.
    /// @param loc Source location for diagnostics.
    void emitResumeLabel(il::core::Value token,
                         il::core::BasicBlock &target,
                         il::support::SourceLoc loc);

    /// @brief Reserve the next SSA temporary identifier for the active function.
    /// @return Newly assigned temporary id.
    unsigned reserveTempId();

  private:
    il::core::Module &mod;                   ///< Module being constructed
    il::core::Function *curFunc{nullptr};    ///< Current function
    il::core::BasicBlock *curBlock{nullptr}; ///< Current insertion block
    unsigned nextTemp{0};                    ///< Next temporary id
    std::unordered_map<std::string, il::core::Type>
        calleeReturnTypes; ///< Cached return types keyed by callee name

    /// @brief Append instruction @p instr to current block.
    /// @param instr Instruction to append.
    /// @return Reference to inserted instruction.
    il::core::Instr &append(il::core::Instr instr);

    /// @brief Check if opcode @p op terminates a block.
    /// @param op Opcode to test.
    /// @return True if op is a terminator.
    bool isTerminator(il::core::Opcode op) const;
};

} // namespace il::build
