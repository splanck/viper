// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// BytecodeModule.hpp - Bytecode module and function data structures
//
// This file defines the data structures that represent a compiled bytecode
// module ready for execution by the bytecode VM. BytecodeModule contains
// all the information needed to execute a program: functions, constant pools,
// native function references, and optional debug information.

#pragma once

#include "bytecode/Bytecode.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper {
namespace bytecode {

/// Information about a local variable for debugging
struct LocalVarInfo {
    std::string name;     // Original variable name
    uint32_t localIdx;    // Index in locals array
    uint32_t startPc;     // First PC where variable is live
    uint32_t endPc;       // Last PC where variable is live (exclusive)
};

/// Exception handler range within a function
struct ExceptionRange {
    uint32_t startPc;     // Range start (inclusive)
    uint32_t endPc;       // Range end (exclusive)
    uint32_t handlerPc;   // Handler entry point
};

/// Switch table entry
struct SwitchEntry {
    int64_t value;        // Case value
    uint32_t targetPc;    // Target PC for this case
};

/// Switch table for SWITCH opcode
struct SwitchTable {
    uint32_t defaultPc;   // Default target PC
    std::vector<SwitchEntry> entries;  // Case entries
};

/// A compiled bytecode function
struct BytecodeFunction {
    std::string name;                    // Function name
    uint32_t numParams;                  // Number of parameters
    uint32_t numLocals;                  // Total locals (params + temps)
    uint32_t maxStack;                   // Maximum operand stack depth
    uint32_t allocaSize;                 // Maximum alloca bytes needed
    bool hasReturn;                      // True if function returns a value

    std::vector<uint32_t> code;          // Bytecode instructions

    // Exception handling
    std::vector<ExceptionRange> exceptionRanges;

    // Switch tables (referenced by SWITCH instructions)
    std::vector<SwitchTable> switchTables;

    // Debug info (optional)
    std::vector<LocalVarInfo> localVars;
    uint32_t sourceFileIdx;              // Index into module's source file list
    std::vector<uint32_t> lineTable;     // PC -> source line mapping
};

/// Reference to a native/runtime function
struct NativeFuncRef {
    std::string name;     // Function name (e.g., "Viper.Terminal.Say")
    uint32_t paramCount;  // Number of parameters
    bool hasReturn;       // True if function returns a value
};

/// Global variable information
struct GlobalInfo {
    std::string name;     // Global name
    uint32_t size;        // Size in bytes
    uint32_t align;       // Alignment requirement
    std::vector<uint8_t> initData;  // Initial data (empty = zero-initialized)
};

/// Source file reference for debug info
struct SourceFileInfo {
    std::string path;     // File path
    uint32_t checksum;    // Optional checksum for validation
};

/// A compiled bytecode module
struct BytecodeModule {
    // Header
    uint32_t magic;       // kBytecodeModuleMagic
    uint32_t version;     // kBytecodeVersion
    uint32_t flags;       // Feature flags

    // Constant pools
    std::vector<int64_t> i64Pool;
    std::vector<double> f64Pool;
    std::vector<std::string> stringPool;

    // Functions
    std::vector<BytecodeFunction> functions;
    std::unordered_map<std::string, uint32_t> functionIndex;

    // Native function references
    std::vector<NativeFuncRef> nativeFuncs;
    std::unordered_map<std::string, uint32_t> nativeFuncIndex;

    // Globals
    std::vector<GlobalInfo> globals;
    std::unordered_map<std::string, uint32_t> globalIndex;

    // Debug info (optional)
    std::vector<SourceFileInfo> sourceFiles;

    // Module initialization
    BytecodeModule() :
        magic(kBytecodeModuleMagic),
        version(kBytecodeVersion),
        flags(0) {}

    // Find a function by name
    const BytecodeFunction* findFunction(const std::string& name) const {
        auto it = functionIndex.find(name);
        if (it != functionIndex.end()) {
            return &functions[it->second];
        }
        return nullptr;
    }

    // Add a function and update index
    uint32_t addFunction(BytecodeFunction fn) {
        uint32_t idx = static_cast<uint32_t>(functions.size());
        functionIndex[fn.name] = idx;
        functions.push_back(std::move(fn));
        return idx;
    }

    // Add an i64 constant to pool, returning index
    uint32_t addI64(int64_t value) {
        // Check if already exists
        for (size_t i = 0; i < i64Pool.size(); ++i) {
            if (i64Pool[i] == value) {
                return static_cast<uint32_t>(i);
            }
        }
        uint32_t idx = static_cast<uint32_t>(i64Pool.size());
        i64Pool.push_back(value);
        return idx;
    }

    // Add an f64 constant to pool, returning index
    uint32_t addF64(double value) {
        // Check if already exists (bitwise comparison for NaN)
        for (size_t i = 0; i < f64Pool.size(); ++i) {
            uint64_t* a = reinterpret_cast<uint64_t*>(&f64Pool[i]);
            uint64_t* b = reinterpret_cast<uint64_t*>(&value);
            if (*a == *b) {
                return static_cast<uint32_t>(i);
            }
        }
        uint32_t idx = static_cast<uint32_t>(f64Pool.size());
        f64Pool.push_back(value);
        return idx;
    }

    // Add a string constant to pool, returning index
    uint32_t addString(const std::string& value) {
        // Check if already exists
        for (size_t i = 0; i < stringPool.size(); ++i) {
            if (stringPool[i] == value) {
                return static_cast<uint32_t>(i);
            }
        }
        uint32_t idx = static_cast<uint32_t>(stringPool.size());
        stringPool.push_back(value);
        return idx;
    }

    // Add a native function reference, returning index
    uint32_t addNativeFunc(const std::string& name, uint32_t paramCount, bool hasReturn) {
        auto it = nativeFuncIndex.find(name);
        if (it != nativeFuncIndex.end()) {
            return it->second;
        }
        uint32_t idx = static_cast<uint32_t>(nativeFuncs.size());
        nativeFuncIndex[name] = idx;
        nativeFuncs.push_back({name, paramCount, hasReturn});
        return idx;
    }
};

} // namespace bytecode
} // namespace viper
