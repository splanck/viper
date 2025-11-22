//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/internal/io/ParserState.hpp
// Purpose: Declares shared parser state for IL text parsing components. 
// Key invariants: Tracks module/function context while parsing input.
// Ownership/Lifetime: Holds references to externally owned module data.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/fwd.hpp"
#include "support/source_location.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace il::io::detail
{

/// @brief Mutable context shared among IL parser helpers.
struct ParserState
{
    /// @brief Module being populated while parsing proceeds.
    il::core::Module &m;

    /// @brief Function currently under construction or nullptr at module scope.
    il::core::Function *curFn = nullptr;

    /// @brief Basic block currently receiving parsed instructions.
    il::core::BasicBlock *curBB = nullptr;

    /// @brief Mapping from SSA value names to their numeric identifiers.
    std::unordered_map<std::string, unsigned> tempIds;

    /// @brief Next SSA identifier to assign to a new temporary.
    unsigned nextTemp = 0;

    /// @brief Line number of the input being processed.
    unsigned lineNo = 0;

    /// @brief Source location tracked via `.loc` directives.
    il::support::SourceLoc curLoc{};

    /// @brief Expected parameter count for each basic block label.
    std::unordered_map<std::string, size_t> blockParamCount;

    /// @brief Record of forward branches awaiting resolution.
    struct PendingBr
    {
        /// @brief Target label referenced before its definition.
        std::string label;
        /// @brief Number of arguments supplied with the branch.
        size_t args = 0;
        /// @brief Line where the unresolved branch appeared.
        unsigned line = 0;
    };

    /// @brief Collection of outstanding branch targets to validate later.
    std::vector<PendingBr> pendingBrs;

    /// @brief Tracks whether the module declared its IL version directive.
    bool sawVersion = false;

    /// @brief Construct parser state for the provided module.
    explicit ParserState(il::core::Module &mod);
};

} // namespace il::io::detail
