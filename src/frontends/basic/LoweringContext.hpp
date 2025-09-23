// File: src/frontends/basic/LoweringContext.hpp
// Purpose: Declares state container used in BASIC-to-IL lowering.
// Key invariants: None.
// Ownership/Lifetime: Does not own referenced module.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/NameMangler.hpp"
#include <string>
#include <unordered_map>

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
    /// @brief Create a context to lower into @p builder and populate @p func.
    /// @ownership References are non-owning; caller must keep builder and
    /// function alive for the lifetime of this context.
    /// @notes Initializes name mangling and lookup tables used during lowering.
    LoweringContext(build::IRBuilder &builder, core::Function &func);

    /// @brief Get or create stack slot name for BASIC variable @p name.
    std::string getOrCreateSlot(const std::string &name);

    /// @brief Get or create a block for the given BASIC line number.
    core::BasicBlock *getOrCreateBlock(int line);

    /// @brief Get deterministic name for string literal @p value.
    std::string getOrAddString(const std::string &value);

  private:
    /// IR builder used to emit instructions and blocks. Non-owning reference.
    build::IRBuilder &builder;

    /// Function currently being lowered. Non-owning reference to caller-owned
    /// function; builder appends new blocks and instructions to it.
    core::Function &function;

    /// Generates deterministic symbol names for variables and strings. Owned by
    /// the context and lives for its entire duration.
    NameMangler mangler;

    /// Mapping from BASIC variable names to their stack slot identifiers. Owns
    /// the strings it stores but not the variables they represent.
    std::unordered_map<std::string, std::string> varSlots;

    /// BASIC line number to IL basic block mapping. Pointers refer to blocks
    /// owned by @ref function.
    std::unordered_map<int, core::BasicBlock *> blocks;

    /// Deduplicated string literals mapped to generated symbol names. Owns
    /// copies of the literal values.
    std::unordered_map<std::string, std::string> strings;

    /// Monotonic counter used to create unique names for string literals.
    /// Lifetime tied to this context instance.
    unsigned nextStringId{0};
};

} // namespace il::frontends::basic
