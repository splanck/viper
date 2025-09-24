// File: src/il/build/IRBuilder.hpp
// Purpose: Declares helper class for building IL modules.
// Key invariants: None.
// Ownership/Lifetime: Does not own the module it modifies.
// Links: docs/il-guide.md#reference
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

using namespace il::core;

/// @brief Helper to construct IL modules and enforce block termination.
class IRBuilder
{
  public:
    /// @brief Create builder operating on module @p m.
    /// @param m Module to mutate.
    explicit IRBuilder(Module &m);

    /// @brief Add an external function declaration.
    /// @param name Symbol name.
    /// @param ret Return type.
    /// @param params Parameter types.
    /// @return Reference to newly added extern.
    Extern &addExtern(const std::string &name, Type ret, const std::vector<Type> &params);

    /// @brief Add a global string constant.
    /// @param name Global identifier.
    /// @param value UTF-8 string literal.
    /// @return Reference to newly added global.
    Global &addGlobalStr(const std::string &name, const std::string &value);

    /// @brief Begin definition of function @p name.
    /// @param name Function identifier.
    /// @param ret Return type.
    /// @param params Parameter list.
    /// @return Reference to created function.
    Function &startFunction(const std::string &name, Type ret, const std::vector<Param> &params);

    /// @brief Create a basic block with optional parameters.
    /// @param fn Function receiving the block.
    /// @param label Block label.
    /// @param params Block parameter list.
    /// @return Reference to new block.
    BasicBlock &createBlock(Function &fn,
                            const std::string &label,
                            const std::vector<Param> &params = {});

    /// @brief Backward-compatible helper without parameters.
    BasicBlock &addBlock(Function &fn, const std::string &label);

    /// @brief Access parameter @p idx of block @p bb as a value.
    Value blockParam(BasicBlock &bb, unsigned idx);

    /// @brief Emit unconditional branch to @p dst with arguments @p args.
    void br(BasicBlock &dst, const std::vector<Value> &args = {});

    /// @brief Emit conditional branch.
    void cbr(Value cond,
             BasicBlock &t,
             const std::vector<Value> &targs,
             BasicBlock &f,
             const std::vector<Value> &fargs);

    /// @brief Set current insertion point to block @p bb.
    /// @param bb Target block.
    void setInsertPoint(BasicBlock &bb);

    /// @brief Emit reference to global string @p globalName.
    /// @param globalName Name of global string.
    /// @return Value representing constant string pointer.
    Value emitConstStr(const std::string &globalName, il::support::SourceLoc loc);

    /// @brief Emit call to function @p callee with arguments @p args.
    /// @param callee Name of function to call.
    /// @param args Argument values.
    /// @param dst Optional destination value to store result.
    void emitCall(const std::string &callee,
                  const std::vector<Value> &args,
                  const std::optional<Value> &dst,
                  il::support::SourceLoc loc);

    /// @brief Emit return from current function.
    /// @param v Optional return value.
    void emitRet(const std::optional<Value> &v, il::support::SourceLoc loc);

    /// @brief Reserve the next SSA temporary identifier for the active function.
    /// @return Newly assigned temporary id.
    unsigned reserveTempId();

  private:
    Module &mod;                   ///< Module being constructed
    Function *curFunc{nullptr};    ///< Current function
    BasicBlock *curBlock{nullptr}; ///< Current insertion block
    unsigned nextTemp{0};          ///< Next temporary id
    std::unordered_map<std::string, Type>
        calleeReturnTypes; ///< Cached return types keyed by callee name

    /// @brief Append instruction @p instr to current block.
    /// @param instr Instruction to append.
    /// @return Reference to inserted instruction.
    Instr &append(Instr instr);

    /// @brief Check if opcode @p op terminates a block.
    /// @param op Opcode to test.
    /// @return True if op is a terminator.
    bool isTerminator(Opcode op) const;
};

} // namespace il::build
