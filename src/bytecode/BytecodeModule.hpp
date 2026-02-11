//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/bytecode/BytecodeModule.hpp
// Purpose: Data structures for compiled bytecode modules and functions.
// Key invariants: Constant pool entries are deduplicated (same value -> same index).
//                 Function indices are stable after insertion.
//                 Module magic and version are set at construction time.
// Ownership: BytecodeModule owns all contained functions, pools, and metadata.
// Lifetime: Created by BytecodeCompiler; consumed by BytecodeVM. The module
//           must outlive any VM executing it.
// Links: Bytecode.hpp, BytecodeCompiler.hpp, BytecodeVM.hpp
//
//===----------------------------------------------------------------------===//
//
// This file defines the data structures that represent a compiled bytecode
// module ready for execution by the bytecode VM. BytecodeModule contains
// all the information needed to execute a program: functions, constant pools,
// native function references, and optional debug information.

#pragma once

#include "bytecode/Bytecode.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper
{
namespace bytecode
{

namespace detail
{
/// @brief Find or add a value to a pool with deduplication.
/// @details Performs a linear scan for an existing match. If found, returns
///          its index; otherwise appends the value and returns the new index.
///          This ensures the same constant value always maps to the same pool index.
/// @tparam T Element type stored in the pool.
/// @tparam Eq Equality predicate type for comparing elements.
/// @param pool  The pool vector to search and potentially append to.
/// @param value The value to find or add.
/// @param eq    Equality comparison function.
/// @return Index of the value in the pool (existing or newly appended).
template <typename T, typename Eq>
inline uint32_t findOrAddToPool(std::vector<T> &pool, const T &value, Eq eq)
{
    for (size_t i = 0; i < pool.size(); ++i)
    {
        if (eq(pool[i], value))
        {
            return static_cast<uint32_t>(i);
        }
    }
    uint32_t idx = static_cast<uint32_t>(pool.size());
    pool.push_back(value);
    return idx;
}
} // namespace detail

/// @brief Debug information about a local variable within a bytecode function.
/// @details Maps a source-level variable name to its runtime local slot and
///          the PC range during which it is live (for debugger display).
struct LocalVarInfo
{
    std::string name;  ///< Original source-level variable name.
    uint32_t localIdx; ///< Index in the function's locals array.
    uint32_t startPc;  ///< First PC where the variable is live (inclusive).
    uint32_t endPc;    ///< Last PC where the variable is live (exclusive).
};

/// @brief An exception handler range within a bytecode function.
/// @details Defines a protected PC region and the handler entry point to
///          jump to when a trap occurs within that region.
struct ExceptionRange
{
    uint32_t startPc;   ///< Range start PC (inclusive).
    uint32_t endPc;     ///< Range end PC (exclusive).
    uint32_t handlerPc; ///< Handler entry point PC.
};

/// @brief A single case entry in a switch table.
struct SwitchEntry
{
    int64_t value;     ///< The case value to match against.
    uint32_t targetPc; ///< Target PC when this case matches.
};

/// @brief Switch table for the SWITCH opcode.
/// @details Contains a default target and a list of case entries. The VM
///          performs a linear scan or binary search over entries to find
///          a matching case value.
struct SwitchTable
{
    uint32_t defaultPc;               ///< Default target PC when no case matches.
    std::vector<SwitchEntry> entries; ///< Ordered list of case entries.
};

/// @brief A compiled bytecode function ready for execution.
/// @details Contains the bytecode instruction stream, local/stack sizing info,
///          exception handler ranges, switch tables, and optional debug metadata.
struct BytecodeFunction
{
    std::string name;    ///< Fully qualified function name.
    uint32_t numParams;  ///< Number of parameters (mapped to first N locals).
    uint32_t numLocals;  ///< Total local slots (parameters + temporaries).
    uint32_t maxStack;   ///< Maximum operand stack depth required during execution.
    uint32_t allocaSize; ///< Maximum alloca bytes needed by the function.
    bool hasReturn;      ///< True if the function returns a value; false for void.

    std::vector<uint32_t> code; ///< Bytecode instruction stream (32-bit words).

    /// @brief Exception handler ranges active in this function.
    std::vector<ExceptionRange> exceptionRanges;

    /// @brief Switch tables referenced by SWITCH instructions in this function.
    std::vector<SwitchTable> switchTables;

    // Debug info (optional)
    std::vector<LocalVarInfo> localVars; ///< Local variable debug information.
    uint32_t sourceFileIdx;              ///< Index into the module's source file list.
    std::vector<uint32_t> lineTable;     ///< PC-to-source-line mapping (indexed by PC).
};

/// @brief Reference to a native/runtime function callable from bytecode.
/// @details Stores the name and signature information needed to locate and
///          invoke a native function through the RuntimeBridge or registered handlers.
struct NativeFuncRef
{
    std::string name;    ///< Function name (e.g., "Viper.Terminal.Say").
    uint32_t paramCount; ///< Number of parameters the function expects.
    bool hasReturn;      ///< True if the function returns a value.
};

/// @brief Information about a global variable in the bytecode module.
/// @details Globals are laid out as contiguous BCSlot entries. Each GlobalInfo
///          describes the name, size, alignment, and optional initial data.
struct GlobalInfo
{
    std::string name;              ///< Fully qualified global variable name.
    uint32_t size;                 ///< Size of the global in bytes.
    uint32_t align;                ///< Alignment requirement in bytes.
    std::vector<uint8_t> initData; ///< Initial data bytes (empty means zero-initialized).
};

/// @brief Source file reference for debug information.
struct SourceFileInfo
{
    std::string path;  ///< File path of the source file.
    uint32_t checksum; ///< Optional checksum for validation (0 if unused).
};

/// @brief A compiled bytecode module containing all data needed for execution.
/// @details The BytecodeModule is the top-level container produced by
///          BytecodeCompiler and consumed by BytecodeVM. It holds the constant
///          pools (i64, f64, string), compiled functions, native function
///          references, global variable descriptors, and optional debug info.
///
///          Constant pool entries are deduplicated: adding the same value twice
///          returns the same pool index. Function and native function references
///          are indexed by name for O(1) lookup.
struct BytecodeModule
{
    // Header
    uint32_t magic;   ///< Module magic number (must equal kBytecodeModuleMagic).
    uint32_t version; ///< Bytecode format version (must equal kBytecodeVersion).
    uint32_t flags;   ///< Feature flags (reserved for future use).

    // Constant pools
    std::vector<int64_t> i64Pool;        ///< Deduplicated pool of 64-bit integer constants.
    std::vector<double> f64Pool;         ///< Deduplicated pool of 64-bit floating-point constants.
    std::vector<std::string> stringPool; ///< Deduplicated pool of string constants.

    // Functions
    std::vector<BytecodeFunction> functions;                 ///< All compiled bytecode functions.
    std::unordered_map<std::string, uint32_t> functionIndex; ///< Function name to index mapping.

    // Native function references
    std::vector<NativeFuncRef> nativeFuncs; ///< Native function descriptors.
    std::unordered_map<std::string, uint32_t>
        nativeFuncIndex; ///< Native function name to index mapping.

    // Globals
    std::vector<GlobalInfo> globals;                       ///< Global variable descriptors.
    std::unordered_map<std::string, uint32_t> globalIndex; ///< Global name to index mapping.

    // Debug info (optional)
    std::vector<SourceFileInfo> sourceFiles; ///< Source file references for debug info.

    /// @brief Construct a new BytecodeModule with default header values.
    /// @details Initializes magic to kBytecodeModuleMagic, version to
    ///          kBytecodeVersion, and flags to 0.
    BytecodeModule() : magic(kBytecodeModuleMagic), version(kBytecodeVersion), flags(0) {}

    /// @brief Find a compiled function by its fully qualified name.
    /// @param name The function name to search for.
    /// @return Pointer to the BytecodeFunction if found; nullptr otherwise.
    const BytecodeFunction *findFunction(const std::string &name) const
    {
        auto it = functionIndex.find(name);
        if (it != functionIndex.end())
        {
            return &functions[it->second];
        }
        return nullptr;
    }

    /// @brief Add a compiled function to the module and update the name index.
    /// @param fn The BytecodeFunction to add (moved into the module).
    /// @return The index of the newly added function in the functions vector.
    uint32_t addFunction(BytecodeFunction fn)
    {
        uint32_t idx = static_cast<uint32_t>(functions.size());
        functionIndex[fn.name] = idx;
        functions.push_back(std::move(fn));
        return idx;
    }

    /// @brief Add a 64-bit integer constant to the pool, deduplicating by value.
    /// @param value The integer constant to add.
    /// @return The pool index of the (possibly pre-existing) constant.
    uint32_t addI64(int64_t value)
    {
        return detail::findOrAddToPool(i64Pool, value, std::equal_to<int64_t>{});
    }

    /// @brief Add a 64-bit floating-point constant to the pool, deduplicating by bitwise
    /// comparison.
    /// @details Uses bitwise comparison (not IEEE equality) so that distinct NaN
    ///          representations are stored separately while +0.0 and -0.0 remain distinct.
    /// @param value The double constant to add.
    /// @return The pool index of the (possibly pre-existing) constant.
    uint32_t addF64(double value)
    {
        auto bitwiseEq = [](double a, double b)
        {
            uint64_t *pa = reinterpret_cast<uint64_t *>(&a);
            uint64_t *pb = reinterpret_cast<uint64_t *>(&b);
            return *pa == *pb;
        };
        return detail::findOrAddToPool(f64Pool, value, bitwiseEq);
    }

    /// @brief Add a string constant to the pool, deduplicating by value.
    /// @param value The string constant to add.
    /// @return The pool index of the (possibly pre-existing) string.
    uint32_t addString(const std::string &value)
    {
        return detail::findOrAddToPool(stringPool, value, std::equal_to<std::string>{});
    }

    /// @brief Add a native function reference, deduplicating by name.
    /// @details If a native function with the same name is already registered,
    ///          returns the existing index without creating a duplicate.
    /// @param name       The fully qualified native function name.
    /// @param paramCount Number of parameters the function expects.
    /// @param hasReturn  True if the function returns a value.
    /// @return The index of the native function reference.
    uint32_t addNativeFunc(const std::string &name, uint32_t paramCount, bool hasReturn)
    {
        auto it = nativeFuncIndex.find(name);
        if (it != nativeFuncIndex.end())
        {
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
