// File: src/frontends/basic/LoweringContext.hpp
// Purpose: Declares state container used in BASIC-to-IL lowering.
// Key invariants: None.
// Ownership/Lifetime: Does not own referenced module.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/AST.hpp"
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace il
{

namespace core
{
struct BasicBlock;
class Function;
} // namespace core

namespace build
{
class IRBuilder;
} // namespace build
} // namespace il

namespace il::frontends::basic
{

/// @brief Tracks mappings needed during BASIC lowering.
/// @invariant Each variable, line, and string literal is unique in its map.
/// @ownership Holds references to IR structures owned elsewhere.
class LoweringContext
{
  public:
    /// @brief Reset state for lowering a new program and bind @p builder.
    /// @ownership Builder is referenced but not owned.
    void beginProgram(build::IRBuilder &builder);

    /// @brief Clear per-procedure state prior to discovering locals/blocks.
    void beginProcedure();

    /// @brief Bind the function currently being lowered.
    /// @ownership Function is referenced but not owned.
    void bindFunction(core::Function &function);

    /// @brief Track that BASIC variable @p name exists in current procedure.
    void registerVariable(const std::string &name);

    /// @brief Mark BASIC array @p name for pointer/length storage.
    void markArray(const std::string &name);

    /// @brief Record the BASIC type associated with @p name.
    void recordVarType(const std::string &name, Type type);

    /// @brief Lookup previously recorded BASIC type for @p name.
    std::optional<Type> lookupVarType(const std::string &name) const;

    /// @brief Remember stack slot id @p slot for BASIC variable @p name.
    void recordVarSlot(const std::string &name, unsigned slot);

    /// @brief Lookup stack slot id previously recorded for @p name.
    std::optional<unsigned> lookupVarSlot(const std::string &name) const;

    /// @brief Remember array length slot id @p slot for BASIC array @p name.
    void recordArrayLengthSlot(const std::string &name, unsigned slot);

    /// @brief Lookup array length slot id for BASIC array @p name.
    std::optional<unsigned> lookupArrayLengthSlot(const std::string &name) const;

    /// @brief Map BASIC line number @p line to emitted IL block @p block.
    void registerLineBlock(int line, std::size_t blockIndex);

    /// @brief Retrieve block associated with BASIC line @p line if present.
    core::BasicBlock *lookupLineBlock(int line) const;

    /// @brief Deduplicate string literal @p value and ensure global emission.
    std::string internString(const std::string &value);

    /// @brief Access discovered scalar/array identifiers for current procedure.
    const std::unordered_set<std::string> &variables() const;
    const std::unordered_set<std::string> &arrays() const;

  private:
    build::IRBuilder *builder{nullptr}; ///< Builder producing module IR.
    core::Function *function{nullptr};  ///< Function currently being lowered.

    std::unordered_set<std::string> varNames; ///< All variables seen.
    std::unordered_set<std::string> arrayNames; ///< Array identifiers.
    std::unordered_map<std::string, Type> varTypes; ///< BASIC types per var.
    std::unordered_map<std::string, unsigned> varSlots; ///< Stack slot ids.
    std::unordered_map<std::string, unsigned> arrayLenSlots; ///< Array lengths.
    std::unordered_map<int, std::size_t> lineBlocks; ///< Line to block index.
    std::unordered_map<std::string, std::string> strings; ///< Literal cache.
    unsigned nextStringId{0}; ///< Counter for string globals.
};

} // namespace il::frontends::basic
