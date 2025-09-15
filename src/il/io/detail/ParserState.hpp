// File: src/il/io/detail/ParserState.hpp
// Purpose: Declares shared parser state used by IL parsing helpers.
// Key invariants: Tracks current function/block context consistently.
// Ownership/Lifetime: Borrows module reference; does not own pointed-to data.
// Links: docs/il-spec.md
#pragma once

#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace il::io::detail
{

using il::core::BasicBlock;
using il::core::Function;
using il::core::Module;

/// @brief Aggregated state shared across parsing routines.
struct ParserState
{
    /// @brief Module being populated by the parser.
    Module &m;

    /// @brief Function currently under construction, nullptr when outside.
    Function *curFn = nullptr;

    /// @brief Basic block currently accepting instructions, nullptr if none.
    BasicBlock *curBB = nullptr;

    /// @brief Mapping from SSA names to numeric identifiers.
    std::unordered_map<std::string, unsigned> tempIds;

    /// @brief Next unused SSA id for value assignment.
    unsigned nextTemp = 0;

    /// @brief Current source line for diagnostics.
    unsigned lineNo = 0;

    /// @brief Active source location recorded via .loc directives.
    il::support::SourceLoc curLoc{};

    /// @brief Expected parameter count for each known basic block label.
    std::unordered_map<std::string, size_t> blockParamCount;

    /// @brief Flag toggled when fatal parsing errors occur.
    bool hasError = false;

    /// @brief Deferred branch resolution metadata.
    struct PendingBr
    {
        std::string label; ///< Referenced block label.
        size_t args = 0;   ///< Number of provided arguments.
        unsigned line = 0; ///< Source line where branch appeared.
    };

    /// @brief Branches that target labels not yet defined.
    std::vector<PendingBr> pendingBrs;

    /// @brief Construct state for the supplied module.
    explicit ParserState(Module &mod) : m(mod) {}
};

} // namespace il::io::detail
