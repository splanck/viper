// File: src/il/verify/TypeInference.cpp
// Purpose: Implements IL verifier type inference and operand validation helpers.
// Key invariants: Maintains consistency between temporary maps and defined sets.
// Ownership/Lifetime: Operates on storage owned by Verifier callers.
// Links: docs/il-spec.md

#include "il/verify/TypeInference.hpp"
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

std::string formatOperands(const Instr &instr)
{
    std::ostringstream os;
    for (const auto &op : instr.operands)
        os << " " << toString(op);
    for (size_t i = 0; i < instr.labels.size(); ++i)
        os << " label " << instr.labels[i];
    return os.str();
}

std::string formatInstrDiag(const Function &fn,
                            const BasicBlock &bb,
                            const Instr &instr,
                            std::string_view message)
{
    std::ostringstream oss;
    oss << fn.name << ":" << bb.label << ": " << makeSnippet(instr);
    if (!message.empty())
        oss << ": " << message;
    return oss.str();
}

} // namespace

std::string makeSnippet(const Instr &instr)
{
    std::ostringstream os;
    if (instr.result)
        os << "%" << *instr.result << " = ";
    os << toString(instr.op) << formatOperands(instr);
    return os.str();
}

TypeInference::TypeInference(std::unordered_map<unsigned, Type> &temps,
                             std::unordered_set<unsigned> &defined)
    : temps_(temps), defined_(defined)
{
}

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
            return Type(Type::Kind::I64);
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

size_t TypeInference::typeSize(Type::Kind kind)
{
    switch (kind)
    {
        case Type::Kind::I1:
            return 1;
        case Type::Kind::I64:
        case Type::Kind::F64:
        case Type::Kind::Ptr:
        case Type::Kind::Str:
            return 8;
        case Type::Kind::Void:
            return 0;
    }
    return 0;
}

void TypeInference::recordResult(const Instr &instr, Type type)
{
    if (instr.result)
    {
        temps_[*instr.result] = type;
        defined_.insert(*instr.result);
    }
}

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

void TypeInference::addTemp(unsigned id, Type type)
{
    temps_[id] = type;
    defined_.insert(id);
}

void TypeInference::removeTemp(unsigned id)
{
    temps_.erase(id);
    defined_.erase(id);
}

bool TypeInference::isDefined(unsigned id) const
{
    return defined_.count(id) != 0;
}

} // namespace il::verify
