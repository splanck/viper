// File: src/il/verify/TypeInference.hpp
// Purpose: Declares utilities for IL verifier type inference and operand validation.
// Key invariants: Tracks temporary definitions and exposes queries for operand types.
// Ownership/Lifetime: Non-owning views over caller-provided maps/sets.
// Links: docs/il-spec.md
#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace il::verify
{

/// @brief Render an instruction to a short single-line snippet for diagnostics.
/// @param instr Instruction whose textual form is requested.
/// @return Human-readable representation used in error messages.
std::string makeSnippet(const il::core::Instr &instr);

/// @brief Helper providing operand type queries and definition tracking for verification.
class TypeInference
{
  public:
    /// @brief Construct a type inference helper backed by caller storage.
    /// @param temps Mapping from temporary id to statically inferred type.
    /// @param defined Set of temporaries considered defined at the current program point.
    TypeInference(std::unordered_map<unsigned, il::core::Type> &temps,
                  std::unordered_set<unsigned> &defined);

    /// @brief Return the static type of a value.
    /// @param value Value to inspect.
    /// @param missing Optional flag set when the referenced temporary is undefined.
    /// @return The inferred type or void when unknown.
    il::core::Type valueType(const il::core::Value &value, bool *missing = nullptr) const;

    /// @brief Compute the size in bytes of a primitive type.
    /// @param kind Type enumerator to inspect.
    /// @return Byte width or zero when the size is unknown.
    static size_t typeSize(il::core::Type::Kind kind);

    /// @brief Register the result of an instruction.
    /// @param instr Instruction whose result is being defined.
    /// @param type Static type assigned to the result temporary.
    void recordResult(const il::core::Instr &instr, il::core::Type type);

    /// @brief Verify that all operands of an instruction are defined.
    /// @param fn Enclosing function used for diagnostics.
    /// @param bb Owning basic block used for diagnostics.
    /// @param instr Instruction whose operands are checked.
    /// @param err Stream receiving diagnostics.
    /// @return True if every operand refers to a known and defined temporary.
    bool ensureOperandsDefined(const il::core::Function &fn,
                               const il::core::BasicBlock &bb,
                               const il::core::Instr &instr,
                               std::ostream &err) const;

    /// @brief Mark a temporary as pre-defined with a given type.
    /// @param id Temporary identifier.
    /// @param type Static type to associate with the id.
    void addTemp(unsigned id, il::core::Type type);

    /// @brief Remove a temporary definition inserted via @ref addTemp.
    /// @param id Temporary identifier to erase.
    void removeTemp(unsigned id);

    /// @brief Check whether a temporary is currently defined.
    /// @param id Temporary identifier.
    /// @return True when @p id is considered defined.
    bool isDefined(unsigned id) const;

  private:
    std::unordered_map<unsigned, il::core::Type> &temps_;
    std::unordered_set<unsigned> &defined_;
};

} // namespace il::verify
