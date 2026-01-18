// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// BytecodeCompiler.hpp - Compiles IL modules to bytecode
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

namespace viper {
namespace bytecode {

/// Compiler that transforms IL modules to bytecode
class BytecodeCompiler {
public:
    /// Compile an IL module to bytecode
    BytecodeModule compile(const il::core::Module& ilModule);

private:
    // Current module being built
    BytecodeModule module_;

    // IL module being compiled (for global lookups)
    const il::core::Module* ilModule_;

    // Current function being compiled
    BytecodeFunction* currentFunc_;

    // SSA value ID -> local slot mapping for current function
    std::unordered_map<uint32_t, uint32_t> ssaToLocal_;
    uint32_t nextLocal_;

    // Block label -> bytecode offset mapping
    std::unordered_map<std::string, uint32_t> blockOffsets_;

    // Block label -> parameter IDs mapping (for storing branch arguments)
    std::unordered_map<std::string, std::vector<uint32_t>> blockParamIds_;

    // Pending branch fixups: (code offset, target label)
    struct BranchFixup {
        uint32_t codeOffset;      // Index into code vector
        std::string targetLabel;  // Target block label
        bool isLong;              // True if 24-bit offset, false for 16-bit
        bool isRaw;               // True if offset is stored as raw i32, not encoded in opcode
    };
    std::vector<BranchFixup> pendingBranches_;

    // Stack depth tracking for max stack calculation
    int32_t currentStackDepth_;
    int32_t maxStackDepth_;

    // Compile a single function
    void compileFunction(const il::core::Function& fn);

    // Map SSA values to local slots
    void buildSSAToLocalsMap(const il::core::Function& fn);

    // Linearize basic blocks for bytecode emission
    std::vector<const il::core::BasicBlock*> linearizeBlocks(
        const il::core::Function& fn);

    // Compile a basic block
    void compileBlock(const il::core::BasicBlock& block);

    // Compile a single instruction
    void compileInstr(const il::core::Instr& instr);

    // Push a value onto the operand stack (emit load bytecode)
    void pushValue(const il::core::Value& val);

    // Pop a value and store to local (emit store bytecode)
    void storeResult(const il::core::Instr& instr);

    // Emit bytecode instruction
    void emit(uint32_t instr);
    void emit(BCOpcode op);
    void emit8(BCOpcode op, uint8_t arg);
    void emitI8(BCOpcode op, int8_t arg);
    void emit16(BCOpcode op, uint16_t arg);
    void emitI16(BCOpcode op, int16_t arg);
    void emit88(BCOpcode op, uint8_t arg0, uint8_t arg1);

    // Emit branch with pending fixup
    void emitBranch(BCOpcode op, const std::string& label);
    void emitBranchLong(BCOpcode op, const std::string& label);

    // Resolve all pending branch fixups
    void resolveBranches();

    // Stack tracking
    void pushStack(int32_t count = 1);
    void popStack(int32_t count = 1);

    // Get or create local slot for SSA value
    uint32_t getLocal(uint32_t ssaId);

    // Local variable load/store with automatic wide variant selection
    void emitLoadLocal(uint32_t local);
    void emitStoreLocal(uint32_t local);

    // Opcode translation helpers
    void compileArithmetic(const il::core::Instr& instr);
    void compileComparison(const il::core::Instr& instr);
    void compileConversion(const il::core::Instr& instr);
    void compileBitwise(const il::core::Instr& instr);
    void compileMemory(const il::core::Instr& instr);
    void compileCall(const il::core::Instr& instr);
    void compileBranch(const il::core::Instr& instr);
    void compileReturn(const il::core::Instr& instr);
};

} // namespace bytecode
} // namespace viper
