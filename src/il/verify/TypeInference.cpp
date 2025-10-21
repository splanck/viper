//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the shared type inference helpers used by the IL verifier.  The
// translation unit centralises routines that project operand types, record new
// SSA definitions, and validate operand availability so verification passes can
// focus on structural checks rather than bookkeeping.  All state is borrowed
// from the caller which keeps the helpers free of hidden side effects.
//
//===----------------------------------------------------------------------===//
//
// @file
// @brief Type inference and operand validation utilities for the IL verifier.
// @details The helpers in this translation unit operate on caller-provided
//          maps describing SSA temporaries and their inferred types.  They keep
//          the “temporaries” and “defined” sets in lock-step, provide formatting
//          helpers for diagnostics, and expose both error-reporting and
//          fallible APIs so callers can pick their preferred control flow.
//
// Links: docs/il-guide.md#reference

#include "il/verify/TypeInference.hpp"
#include "il/verify/DiagFormat.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include "support/diag_expected.hpp"
#include <sstream>
#include <string_view>

using namespace il::core;

namespace il::verify
{

namespace
{

/// @brief Render the operand and label payload of an instruction.
///
/// @details Serialises each operand and successor label into a flat string that
///          matches the formatting expected by diagnostic helpers.  The
///          resulting text is appended to the opcode and optional result to
///          produce a single-line snippet describing the instruction.
///
/// @param instr Instruction being rendered.
/// @return Space-prefixed payload that lists operands and successor labels.
static std::string formatOperands(const Instr &instr)
{
    std::ostringstream os;
    for (const auto &op : instr.operands)
        os << " " << toString(op);
    for (size_t i = 0; i < instr.labels.size(); ++i)
        os << " label " << instr.labels[i];
    return os.str();
}

} // namespace

/// @brief Render @p instr into the concise single-line format used for
///        diagnostics.
///
/// @details Prepends the SSA result when present, appends the opcode and
///          delegates to @ref formatOperands to capture operands and successor
///          labels.  The helper is purely functional so repeated invocations are
///          deterministic.
///
/// @param instr Instruction being rendered.
/// @return Printable snippet suitable for inclusion in verifier diagnostics.
std::string makeSnippet(const Instr &instr)
{
    std::ostringstream os;
    if (instr.result)
        os << "%" << *instr.result << " = ";
    os << toString(instr.op) << formatOperands(instr);
    return os.str();
}

/// @brief Construct a type inference helper that mirrors caller-owned state.
///
/// @details The helper stores references to the caller-maintained temporary map
///          and defined-id set.  All mutating operations keep the containers in
///          sync so that every defined id has an associated type entry.
///
/// @param temps Reference to the map describing known temporary types.
/// @param defined Reference to the set tracking defined SSA ids.
TypeInference::TypeInference(std::unordered_map<unsigned, Type> &temps,
                             std::unordered_set<unsigned> &defined)
    : temps_(temps), defined_(defined)
{
}

/// @brief Inspect the static type associated with a value.
///
/// @details Temporaries are looked up in the tracked map.  Missing entries set
///          @p missing to @c true when the flag is provided.  Literal values are
///          translated directly into their canonical IL types.
///
/// @param value Value to query; may reference a temporary or literal constant.
/// @param missing Optional out-parameter toggled when a temporary is unknown.
/// @return Inferred IL type or void when no information is available.
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
///
/// @details Converts the enumerator into a byte width using the canonical
///          layout expected by the verifier.  Void and unknown kinds yield zero
///          so callers can treat the result as a safe upper bound.
///
/// @param kind Enumerator describing the type whose size is requested.
/// @return Byte width of the type or zero when it lacks storage.
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

/// @brief Record the result type for an instruction that defines an SSA id.
///
/// @details When the instruction declares a result id, the helper inserts the
///          provided @p type into the temporary map and marks the id as defined.
///          Instructions without results are ignored.
///
/// @param instr Instruction whose result id is being updated.
/// @param type Type assigned to the result temporary.
void TypeInference::recordResult(const Instr &instr, Type type)
{
    if (instr.result)
    {
        temps_[*instr.result] = type;
        defined_.insert(*instr.result);
    }
}

/// @brief Validate that every operand used by an instruction is known and
///        defined.
///
/// @details Walks each operand, checking that temporaries have entries in the
///          tracked map and have been marked as defined.  When a violation is
///          discovered the helper produces a formatted diagnostic tied to the
///          enclosing function and block.
///
/// @param fn Function containing the instruction.
/// @param bb Basic block owning the instruction.
/// @param instr Instruction whose operands are being verified.
/// @return Empty expected on success; otherwise contains a diagnostic error.
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
                formatInstrDiag(fn, bb, instr,
                                "unknown temp %" + id + "; use before def of %" + id))};
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
///
/// @details Invokes the fallible helper and streams any produced diagnostic to
///          @p err.  Callers that prefer status codes without exceptions can use
///          this convenience wrapper.
///
/// @param fn Function containing the instruction.
/// @param bb Basic block owning the instruction.
/// @param instr Instruction whose operands are being verified.
/// @param err Output stream receiving diagnostic messages when validation fails.
/// @return @c true when operands are defined; @c false otherwise.
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

/// @brief Insert a new temporary definition into the tracked state.
///
/// @details Updates both the temporary map and defined-id set so future queries
///          observe a consistent view of available temporaries.
///
/// @param id Temporary identifier being declared.
/// @param type Inferred type associated with the identifier.
void TypeInference::addTemp(unsigned id, Type type)
{
    temps_[id] = type;
    defined_.insert(id);
}

/// @brief Remove a temporary definition from the tracked state.
///
/// @details Erases the identifier from both the temporary map and the defined
///          set, preventing stale entries from reporting a type without a
///          definition.
///
/// @param id Temporary identifier to erase.
void TypeInference::removeTemp(unsigned id)
{
    temps_.erase(id);
    defined_.erase(id);
}

/// @brief Query whether a temporary identifier is currently defined.
///
/// @param id Identifier to inspect.
/// @return @c true when the identifier is present in the defined-id set.
bool TypeInference::isDefined(unsigned id) const
{
    return defined_.count(id) != 0;
}

} // namespace il::verify
