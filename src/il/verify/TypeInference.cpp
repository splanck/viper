//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/verify/TypeInference.cpp
// Purpose: Implement the verifier-side type inference utility that tracks SSA
//          temporaries and validates operand usage.
// Key invariants: The tracked "defined" set stays in sync with the temporary
//                 type map; every recorded id has a type and undefined ids are
//                 absent from both containers.
// Ownership/Lifetime: The helper borrows all storage from callers (maps, sets,
//                     diagnostic streams) and never assumes ownership.
// Links: docs/il-guide.md#reference, docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "il/verify/TypeInference.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include "il/verify/DiagFormat.hpp"
#include "support/diag_expected.hpp"
#include <sstream>
#include <string_view>

using namespace il::core;

namespace il::verify
{

namespace
{

/// @brief Render an instruction's operands and labels into a textual snippet.
/// @details Concatenates the operands and label targets into a whitespace
///          separated string that mirrors the IL textual representation.  The
///          helper is used by diagnostic builders to provide context around
///          failing operands without duplicating formatting logic in each call
///          site.
/// @param instr Instruction whose operands should be rendered.
/// @return Space-delimited string describing operands and successor labels.
std::string formatOperands(const Instr &instr)
{
    std::ostringstream os;
    for (const auto &op : instr.operands)
        os << " " << toString(op);
    for (size_t i = 0; i < instr.labels.size(); ++i)
        os << " label " << instr.labels[i];
    return os.str();
}

} // namespace

/// @brief Render @p instr into the concise single-line format used for diagnostics.
/// @param instr Instruction being rendered.
/// @return String containing the printable opcode, result id and operands.
/// @note Pure helper that only inspects @p instr; it does not depend on verifier state.
std::string makeSnippet(const Instr &instr)
{
    std::ostringstream os;
    if (instr.result)
        os << "%" << *instr.result << " = ";
    os << toString(instr.op) << formatOperands(instr);
    return os.str();
}

/// @brief Build a helper bound to caller-provided temporary tracking state.
/// @param temps Reference to the map describing known temporary types. The helper
///        updates this map whenever it observes a new definition.
/// @param defined Reference to the set tracking which temporaries are considered
///        defined. All mutating helpers update this set in lock-step with @p temps
///        to preserve the invariant that every defined id has a type entry.
/// @note Invariant: @ref defined_ is always a subset of the keys present in
///       @ref temps_. Both containers are required to outlive the helper.
TypeInference::TypeInference(std::unordered_map<unsigned, Type> &temps,
                             std::unordered_set<unsigned> &defined)
    : temps_(temps), defined_(defined)
{
}

/// @brief Inspect the static type associated with @p value.
/// @param value Value to query; may reference a temporary or a literal constant.
/// @param missing Optional out-parameter toggled when the referenced temporary is
///        not present in @ref temps_. The flag is left untouched for non-temporaries.
/// @return The inferred type or void when the temporary is unknown.
/// @note Invariant: queries do not mutate @ref temps_ nor @ref defined_, keeping
///       lookup operations side-effect free.
Type TypeInference::valueType(const Value &value, bool *missing) const
{
    switch (value.kind)
    {
        case Value::Kind::Temp:
        {
            auto it = temps_.find(value.id);
            if (it != temps_.end())
                return it->second;
            if (missing)
                *missing = true;
            return Type(Type::Kind::Void);
        }
        case Value::Kind::ConstInt:
            return Type(value.isBool ? Type::Kind::I1 : Type::Kind::I64);
        case Value::Kind::ConstFloat:
            return Type(Type::Kind::F64);
        case Value::Kind::ConstStr:
            return Type(Type::Kind::Str);
        case Value::Kind::GlobalAddr:
        case Value::Kind::NullPtr:
            return Type(Type::Kind::Ptr);
    }
    return Type(Type::Kind::Void);
}

/// @brief Compute the storage width for a primitive IL type.
/// @param kind Enumerator describing the type whose size is requested.
/// @return Size in bytes or zero if the type has no storage.
/// @note This helper does not consult or mutate verifier state.
size_t TypeInference::typeSize(Type::Kind kind)
{
    switch (kind)
    {
        case Type::Kind::I1:
            return 1;
        case Type::Kind::I16:
            return 2;
        case Type::Kind::I32:
            return 4;
        case Type::Kind::I64:
        case Type::Kind::F64:
        case Type::Kind::Ptr:
        case Type::Kind::Str:
        case Type::Kind::ResumeTok:
            return 8;
        case Type::Kind::Error:
            return 24;
        case Type::Kind::Void:
            return 0;
    }
    return 0;
}

/// @brief Record the result type of @p instr, updating the tracked temporaries.
/// @param instr Instruction whose SSA result is being defined.
/// @param type Type assigned to the result temporary.
/// @note Invariant: whenever a result id is tracked in @ref defined_, a matching
///       entry exists in @ref temps_ so future queries yield a concrete type.
void TypeInference::recordResult(const Instr &instr, Type type)
{
    if (instr.result)
    {
        temps_[*instr.result] = type;
        defined_.insert(*instr.result);
    }
}

/// @brief Ensure each operand of @p instr is both known and defined.
/// @param fn Enclosing function, used to render diagnostic context.
/// @param bb Basic block owning @p instr for additional context.
/// @param instr Instruction whose operands are being validated.
/// @return Empty result on success; otherwise contains a diagnostic describing the
///         first undefined/unknown temporary encountered.
/// @note Errors are signaled via @ref il::support::Expected by returning an error
///       payload populated with a formatted diagnostic.
/// @note Invariant: relies on the contract that @ref temps_ and @ref defined_
///       describe the same set of temporaries; they are only read in this helper.
il::support::Expected<void> TypeInference::ensureOperandsDefined_E(const Function &fn,
                                                                   const BasicBlock &bb,
                                                                   const Instr &instr) const
{
    for (const auto &op : instr.operands)
    {
        if (op.kind != Value::Kind::Temp)
            continue;

        bool missing = false;
        valueType(op, &missing);
        const bool undefined = !isDefined(op.id);

        if (!missing && !undefined)
            continue;

        std::string id = std::to_string(op.id);
        if (missing && undefined)
        {
            return il::support::Expected<void>{il::support::makeError(
                instr.loc,
                formatInstrDiag(
                    fn, bb, instr, "unknown temp %" + id + "; use before def of %" + id))};
        }
        if (missing)
            return il::support::Expected<void>{il::support::makeError(
                instr.loc, formatInstrDiag(fn, bb, instr, "unknown temp %" + id))};
        return il::support::Expected<void>{il::support::makeError(
            instr.loc, formatInstrDiag(fn, bb, instr, "use before def of %" + id))};
    }
    return {};
}

/// @brief Wrapper around @ref ensureOperandsDefined_E that prints diagnostics.
/// @param fn Enclosing function used for diagnostics.
/// @param bb Owning basic block used for diagnostics.
/// @param instr Instruction whose operands are checked.
/// @param err Stream receiving the formatted diagnostic text on failure.
/// @return True when all operands are defined, false otherwise.
/// @note Errors are emitted by writing a diagnostic message to @p err.
bool TypeInference::ensureOperandsDefined(const Function &fn,
                                          const BasicBlock &bb,
                                          const Instr &instr,
                                          std::ostream &err) const
{
    if (auto result = ensureOperandsDefined_E(fn, bb, instr); !result)
    {
        il::support::printDiag(result.error(), err);
        return false;
    }
    return true;
}

/// @brief Predeclare a temporary definition, synchronizing the tracked state.
/// @param id Temporary identifier to insert.
/// @param type Type to associate with the definition.
/// @note Invariant: updates @ref temps_ and @ref defined_ together so readers
///       observe a consistent view of available temporaries.
void TypeInference::addTemp(unsigned id, Type type)
{
    temps_[id] = type;
    defined_.insert(id);
}

/// @brief Remove a temporary definition from the tracked state.
/// @param id Temporary identifier to erase.
/// @note Invariant: erases the id from both @ref temps_ and @ref defined_ to avoid
///       stale entries that could report a definition without a type.
void TypeInference::removeTemp(unsigned id)
{
    temps_.erase(id);
    defined_.erase(id);
}

/// @brief Determine whether @p id is currently marked as defined.
/// @param id Temporary identifier to query.
/// @return True when the identifier has been inserted into @ref defined_.
/// @note Does not consult @ref temps_; callers should still respect the invariant
///       that defined ids also have a type mapping.
bool TypeInference::isDefined(unsigned id) const
{
    return defined_.count(id) != 0;
}

} // namespace il::verify
