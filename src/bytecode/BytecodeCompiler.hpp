//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/bytecode/BytecodeCompiler.hpp
// Purpose: Compiles IL modules into compact bytecode for the Viper bytecode VM.
// Key invariants: SSA values are deterministically mapped to local slots.
//                 Block linearization preserves fall-through for the common case.
//                 All branch offsets are resolved before the function is finalized.
// Ownership: Produces BytecodeModule instances; does not take ownership of input
//            IL modules.
// Lifetime: Compiler state is transient per compile() call; the resulting
//           BytecodeModule outlives the compiler.
// Links: Bytecode.hpp, BytecodeModule.hpp, il/core/Module.hpp
//
//===----------------------------------------------------------------------===//
//
// This file defines the BytecodeCompiler which transforms IL modules into
// compact bytecode for fast interpretation. The compiler performs:
// - SSA to locals mapping
// - Block linearization
// - Constant pool building
// - Bytecode instruction emission
// - Branch offset resolution

#pragma once

#include "bytecode/Bytecode.hpp"
#include "bytecode/BytecodeModule.hpp"
#include "il/core/Module.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper
{
namespace bytecode
{

/// @brief Compiler that transforms IL modules into bytecode for the Viper VM.
/// @details The BytecodeCompiler lowers an IL module's functions into compact
///          bytecode by performing SSA-to-locals mapping, basic-block
///          linearization, constant pool construction, instruction emission,
///          and branch fixup resolution. The resulting BytecodeModule is
///          self-contained and ready for execution by BytecodeVM.
class BytecodeCompiler
{
  public:
    /// @brief Compile an entire IL module to a bytecode module.
    /// @details Iterates over all functions in the IL module, compiles each
    ///          one into a BytecodeFunction, builds the shared constant pools
    ///          (i64, f64, string), and assembles the result into a
    ///          BytecodeModule with resolved branch offsets.
    /// @param ilModule The IL module to compile.
    /// @return A fully compiled BytecodeModule ready for execution.
    BytecodeModule compile(const il::core::Module &ilModule);

  private:
    /// @brief The bytecode module being built during compilation.
    BytecodeModule module_;

    /// @brief The IL module being compiled (used for global lookups).
    const il::core::Module *ilModule_;

    /// @brief Pointer to the BytecodeFunction currently being compiled.
    BytecodeFunction *currentFunc_;

    /// @brief Mapping from SSA value IDs to local variable slot indices.
    /// @details Populated per-function by buildSSAToLocalsMap().
    std::unordered_map<uint32_t, uint32_t> ssaToLocal_;

    /// @brief Next available local slot index for the current function.
    uint32_t nextLocal_;

    /// @brief Mapping from block labels to their bytecode offsets.
    /// @details Populated during block emission and consumed during branch fixup.
    std::unordered_map<std::string, uint32_t> blockOffsets_;

    /// @brief Mapping from block labels to the SSA IDs of their block parameters.
    /// @details Used to emit stores for branch arguments that feed block parameters.
    std::unordered_map<std::string, std::vector<uint32_t>> blockParamIds_;

    /// @brief A pending branch fixup requiring offset resolution after all blocks
    ///        have been emitted.
    struct BranchFixup
    {
        uint32_t codeOffset;     ///< Index into the code vector where the offset is stored.
        std::string targetLabel; ///< Target block label to resolve.
        bool isLong;             ///< True if the offset is 24-bit; false for 16-bit.
        bool isRaw; ///< True if the offset is stored as a raw i32 (not encoded in opcode).
    };

    /// @brief List of branch fixups accumulated during function compilation.
    std::vector<BranchFixup> pendingBranches_;

    /// @brief Current operand stack depth during emission (for max stack calculation).
    int32_t currentStackDepth_;

    /// @brief Maximum operand stack depth observed during compilation of the current function.
    int32_t maxStackDepth_;

    /// @brief Compile a single IL function into a BytecodeFunction.
    /// @details Builds the SSA-to-locals map, linearizes blocks, emits bytecode
    ///          for each block, and resolves branch fixups.
    /// @param fn The IL function to compile.
    void compileFunction(const il::core::Function &fn);

    /// @brief Build the SSA value ID to local slot mapping for a function.
    /// @details Assigns each SSA value (parameters, instruction results) a
    ///          unique local slot index. Parameters occupy the first N slots.
    /// @param fn The IL function whose SSA values are to be mapped.
    void buildSSAToLocalsMap(const il::core::Function &fn);

    /// @brief Linearize basic blocks into an ordered sequence for emission.
    /// @details Orders blocks so that the most likely fall-through successor
    ///          immediately follows its predecessor, minimizing jump instructions.
    /// @param fn The IL function whose blocks are to be linearized.
    /// @return An ordered vector of basic block pointers for sequential emission.
    std::vector<const il::core::BasicBlock *> linearizeBlocks(const il::core::Function &fn);

    /// @brief Compile all instructions in a basic block.
    /// @details Records the block's bytecode offset, emits code for each
    ///          instruction, and handles block parameter stores.
    /// @param block The basic block to compile.
    void compileBlock(const il::core::BasicBlock &block);

    /// @brief Compile a single IL instruction into one or more bytecode instructions.
    /// @param instr The IL instruction to compile.
    void compileInstr(const il::core::Instr &instr);

    /// @brief Emit bytecode to push an IL value onto the operand stack.
    /// @details Handles constants (immediates), SSA references (local loads),
    ///          and global references.
    /// @param val The IL value to push.
    void pushValue(const il::core::Value &val);

    /// @brief Pop TOS and store the result into the local slot for the instruction's SSA ID.
    /// @param instr The IL instruction whose result is being stored.
    void storeResult(const il::core::Instr &instr);

    /// @brief Emit a raw 32-bit instruction word into the current function's code.
    /// @param instr The pre-encoded 32-bit instruction word.
    void emit(uint32_t instr);

    /// @brief Emit a zero-argument bytecode instruction.
    /// @param op The opcode to emit.
    void emit(BCOpcode op);

    /// @brief Emit a bytecode instruction with one unsigned 8-bit argument.
    /// @param op  The opcode to emit.
    /// @param arg The 8-bit unsigned argument.
    void emit8(BCOpcode op, uint8_t arg);

    /// @brief Emit a bytecode instruction with one signed 8-bit argument.
    /// @param op  The opcode to emit.
    /// @param arg The signed 8-bit argument.
    void emitI8(BCOpcode op, int8_t arg);

    /// @brief Emit a bytecode instruction with one unsigned 16-bit argument.
    /// @param op  The opcode to emit.
    /// @param arg The 16-bit unsigned argument.
    void emit16(BCOpcode op, uint16_t arg);

    /// @brief Emit a bytecode instruction with one signed 16-bit argument.
    /// @param op  The opcode to emit.
    /// @param arg The signed 16-bit argument.
    void emitI16(BCOpcode op, int16_t arg);

    /// @brief Emit a bytecode instruction with two unsigned 8-bit arguments.
    /// @param op   The opcode to emit.
    /// @param arg0 First 8-bit unsigned argument.
    /// @param arg1 Second 8-bit unsigned argument.
    void emit88(BCOpcode op, uint8_t arg0, uint8_t arg1);

    /// @brief Emit a branch instruction with a pending fixup for the target label.
    /// @details The target offset is left as a placeholder and resolved later
    ///          by resolveBranches(). Uses a 16-bit offset encoding.
    /// @param op    The branch opcode (JUMP, JUMP_IF_TRUE, JUMP_IF_FALSE).
    /// @param label The target basic block label.
    void emitBranch(BCOpcode op, const std::string &label);

    /// @brief Emit a long branch instruction with a pending fixup for the target label.
    /// @details Uses a 24-bit offset encoding for blocks farther than 16-bit range.
    /// @param op    The branch opcode (JUMP_LONG).
    /// @param label The target basic block label.
    void emitBranchLong(BCOpcode op, const std::string &label);

    /// @brief Resolve all pending branch fixups by patching target offsets.
    /// @details Called once after all blocks have been emitted. Computes the
    ///          signed offset from the branch instruction to the target block
    ///          and encodes it into the previously emitted placeholder.
    void resolveBranches();

    /// @brief Record that the operand stack grows by @p count entries.
    /// @details Updates currentStackDepth_ and maxStackDepth_ for accurate
    ///          max-stack calculation in the compiled function.
    /// @param count Number of stack entries pushed (default 1).
    void pushStack(int32_t count = 1);

    /// @brief Record that the operand stack shrinks by @p count entries.
    /// @param count Number of stack entries popped (default 1).
    void popStack(int32_t count = 1);

    /// @brief Get or create a local variable slot for an SSA value ID.
    /// @details Returns the existing mapping if present; otherwise allocates
    ///          a new slot and records the mapping.
    /// @param ssaId The SSA value identifier.
    /// @return The local slot index assigned to this SSA value.
    uint32_t getLocal(uint32_t ssaId);

    /// @brief Emit a LOAD_LOCAL or LOAD_LOCAL_W instruction based on slot index size.
    /// @details Selects the narrow (8-bit index) or wide (16-bit index) variant
    ///          automatically based on whether the index fits in 8 bits.
    /// @param local The local variable slot index to load.
    void emitLoadLocal(uint32_t local);

    /// @brief Emit a STORE_LOCAL or STORE_LOCAL_W instruction based on slot index size.
    /// @details Selects the narrow (8-bit index) or wide (16-bit index) variant
    ///          automatically based on whether the index fits in 8 bits.
    /// @param local The local variable slot index to store to.
    void emitStoreLocal(uint32_t local);

    /// @brief Compile an IL arithmetic instruction into corresponding bytecode.
    /// @param instr The IL arithmetic instruction (add, sub, mul, div, rem, neg).
    void compileArithmetic(const il::core::Instr &instr);

    /// @brief Compile an IL comparison instruction into corresponding bytecode.
    /// @param instr The IL comparison instruction (eq, ne, lt, le, gt, ge).
    void compileComparison(const il::core::Instr &instr);

    /// @brief Compile an IL type conversion instruction into corresponding bytecode.
    /// @param instr The IL conversion instruction (i64_to_f64, f64_to_i64, etc.).
    void compileConversion(const il::core::Instr &instr);

    /// @brief Compile an IL bitwise operation instruction into corresponding bytecode.
    /// @param instr The IL bitwise instruction (and, or, xor, not, shl, shr).
    void compileBitwise(const il::core::Instr &instr);

    /// @brief Compile an IL memory operation instruction into corresponding bytecode.
    /// @param instr The IL memory instruction (alloca, gep, load, store).
    void compileMemory(const il::core::Instr &instr);

    /// @brief Compile an IL call instruction into corresponding bytecode.
    /// @param instr The IL call instruction (direct, native, or indirect).
    void compileCall(const il::core::Instr &instr);

    /// @brief Compile an IL branch instruction into corresponding bytecode.
    /// @param instr The IL branch instruction (unconditional, conditional, switch).
    void compileBranch(const il::core::Instr &instr);

    /// @brief Compile an IL return instruction into corresponding bytecode.
    /// @param instr The IL return instruction (return value or return void).
    void compileReturn(const il::core::Instr &instr);
};

} // namespace bytecode
} // namespace viper
