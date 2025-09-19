// MIT License. See LICENSE in the project root for full license information.
// File: src/il/verify/TypeInference.hpp
// Purpose: Declares utilities for IL verifier type inference and operand validation.
// Key invariants: Tracks temporary definitions and exposes queries for operand types.
// Ownership/Lifetime: Non-owning views over caller-provided maps/sets.
// Links: docs/il-spec.md
#pragma once

#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/core/fwd.hpp"
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace il::support
{
template <class T> class Expected;
}

namespace il::verify
{

/// @brief Render an instruction to a short single-line snippet for diagnostics.
/// @details The snippet mirrors the verifier trace format and is embedded into
///          operand definition diagnostics produced by @ref TypeInference.
/// @param instr Instruction whose textual form is requested.
/// @return Human-readable representation used in error messages.
std::string makeSnippet(const il::core::Instr &instr);

/// @brief Helper providing operand type queries and definition tracking for verification.
/// @details The helper keeps a shared map of temporary identifiers to inferred
///          types plus a set describing which temporaries have been defined at
///          a given program point.  Verification routines feed instructions to
///          @ref recordResult and query operand state through
///          @ref ensureOperandsDefined_E or @ref ensureOperandsDefined to
///          surface "unknown temporary" and "use before definition" errors.
class TypeInference
{
  public:
    /// @brief Construct a type inference helper backed by caller storage.
    /// @details The helper stores references to the caller-provided temporary
    ///          table and definition set so that verifier passes can share
    ///          state while walking a function.
    /// @param temps Mapping from temporary id to statically inferred type.
    /// @param defined Set of temporaries considered defined at the current program point.
    TypeInference(std::unordered_map<unsigned, il::core::Type> &temps,
                  std::unordered_set<unsigned> &defined);

    /// @brief Return the static type of a value.
    /// @details Const and global operands map to their intrinsic types. For
    ///          temporaries, the helper consults @p temps and optionally marks
    ///          @p missing when an operand references an unknown id so callers
    ///          can emit precise diagnostics.
    /// @param value Value to inspect.
    /// @param missing Optional flag set when the referenced temporary is undefined.
    /// @return The inferred type or void when unknown.
    il::core::Type valueType(const il::core::Value &value, bool *missing = nullptr) const;

    /// @brief Compute the size in bytes of a primitive type tracked by the verifier.
    /// @details Width information is used by verifier checks that must reason
    ///          about operand footprint (e.g., memory operations). Types with
    ///          unknown sizes report zero.
    /// @param kind Type enumerator to inspect.
    /// @return Byte width or zero when the size is unknown.
    static size_t typeSize(il::core::Type::Kind kind);

    /// @brief Register the result of an instruction.
    /// @details If @p instr produces a result temporary the helper updates the
    ///          shared table with the inferred @p type and adds the id to the
    ///          definition set so subsequent operand checks see it as defined.
    /// @param instr Instruction whose result is being defined.
    /// @param type Static type assigned to the result temporary.
    void recordResult(const il::core::Instr &instr, il::core::Type type);

    /// @brief Verify that all operands of an instruction are defined.
    /// @details Streams diagnostics to @p err describing either unknown
    ///          temporaries (ids missing from the type table) or uses that
    ///          precede a definition.  The helper differentiates the two cases
    ///          via @ref valueType and @ref isDefined for precise messaging.
    /// @param fn Enclosing function used for diagnostics.
    /// @param bb Owning basic block used for diagnostics.
    /// @param instr Instruction whose operands are checked.
    /// @param err Stream receiving diagnostics.
    /// @return True if every operand refers to a known and defined temporary.
    bool ensureOperandsDefined(const il::core::Function &fn,
                               const il::core::BasicBlock &bb,
                               const il::core::Instr &instr,
                               std::ostream &err) const;

    /// @brief Verify operand definitions returning a diagnostic payload on failure.
    /// @details Returns a populated diagnostic differentiating between unknown
    ///          temporaries and use-before-definition errors, annotated with
    ///          the formatted instruction snippet produced by @ref makeSnippet.
    /// @param fn Enclosing function used for diagnostics.
    /// @param bb Owning basic block used for diagnostics.
    /// @param instr Instruction whose operands are checked.
    /// @return Empty Expected on success; diagnostic payload when verification fails.
    il::support::Expected<void> ensureOperandsDefined_E(const il::core::Function &fn,
                                                        const il::core::BasicBlock &bb,
                                                        const il::core::Instr &instr) const;

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
