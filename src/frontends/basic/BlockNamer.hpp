// File: src/frontends/basic/BlockNamer.hpp
// Purpose: Per-procedure helper generating deterministic block labels.
// Key invariants: Counters reset per procedure; labels stable across runs.
// Ownership/Lifetime: Owned by lowering routine; not shared.
// Links: docs/lowering.md
#pragma once

#include <string>
#include <unordered_map>

namespace il::frontends::basic
{

/// @brief Generates deterministic block names for a single procedure.
/// @invariant Each procedure gets an independent counter per shape ensuring
///            stable IL required by golden tests.
class BlockNamer
{
  public:
    explicit BlockNamer(std::string proc) : procName(std::move(proc)) {}

    /// @brief Name for the entry block.
    std::string entry() const
    {
        return "entry_" + procName;
    }

    /// @brief Name for the return block.
    std::string ret() const
    {
        return "ret_" + procName;
    }

    /// @brief Name for a source line block.
    std::string line(int line) const
    {
        return "L" + std::to_string(line) + "_" + procName;
    }

    struct IfNames
    {
        std::string thenBB;
        std::string elseBB;
        std::string endBB;
    };

    /// @brief Allocate labels for a single IF/ELSE construct.
    IfNames nextIf()
    {
        unsigned k = ifCounter++;
        std::string suffix = "_" + std::to_string(k) + "_" + procName;
        return {"if_then" + suffix, "if_else" + suffix, "if_end" + suffix};
    }

    struct WhileNames
    {
        std::string head;
        std::string body;
        std::string end;
    };

    /// @brief Allocate labels for a WHILE loop.
    WhileNames nextWhile()
    {
        unsigned k = whileCounter++;
        std::string s = "_" + std::to_string(k) + "_" + procName;
        return {"while_head" + s, "while_body" + s, "while_end" + s};
    }

    struct ForNames
    {
        std::string head;
        std::string body;
        std::string inc;
        std::string end;
    };

    /// @brief Allocate labels for a FOR loop.
    ForNames nextFor()
    {
        unsigned k = forCounter++;
        std::string s = "_" + std::to_string(k) + "_" + procName;
        return {"for_head" + s, "for_body" + s, "for_inc" + s, "for_end" + s};
    }

    /// @brief Generic unique label for miscellaneous blocks.
    std::string unique(const std::string &hint)
    {
        unsigned k = otherCounters[hint]++;
        return hint + "_" + std::to_string(k) + "_" + procName;
    }

  private:
    std::string procName;
    unsigned ifCounter = 0;
    unsigned whileCounter = 0;
    unsigned forCounter = 0;
    std::unordered_map<std::string, unsigned> otherCounters;
};

} // namespace il::frontends::basic
