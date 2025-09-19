// File: src/il/verify/InstructionChecker.cpp
// Purpose: Implements helpers that validate non-control IL instructions.
// Key invariants: Relies on TypeInference to keep operand types consistent.
// Ownership/Lifetime: Functions operate on caller-provided structures.
// Links: docs/il-spec.md

#include "il/verify/InstructionChecker.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/verify/Rule.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>

using namespace il::core;

namespace il::verify
{

bool verifyOpcodeSignature(const Function &fn,
                           const BasicBlock &bb,
                           const Instr &instr,
                           std::ostream &err)
{
    const auto &info = getOpcodeInfo(instr.op);
    bool ok = true;
    auto emit = [&](const std::string &msg)
    {
        err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": " << msg << "\n";
        ok = false;
    };

    const bool hasResult = instr.result.has_value();
    switch (info.resultArity)
    {
        case ResultArity::None:
            if (hasResult)
                emit("unexpected result");
            break;
        case ResultArity::One:
            if (!hasResult)
                emit("missing result");
            break;
        case ResultArity::Optional:
            break;
    }

    const size_t operandCount = instr.operands.size();
    const bool variadic = isVariadicOperandCount(info.numOperandsMax);
    if (operandCount < info.numOperandsMin || (!variadic && operandCount > info.numOperandsMax))
    {
        std::ostringstream ss;
        if (info.numOperandsMin == info.numOperandsMax)
        {
            ss << "expected " << static_cast<unsigned>(info.numOperandsMin) << " operand";
            if (info.numOperandsMin != 1)
                ss << 's';
        }
        else if (variadic)
        {
            ss << "expected at least " << static_cast<unsigned>(info.numOperandsMin) << " operand";
            if (info.numOperandsMin != 1)
                ss << 's';
        }
        else
        {
            ss << "expected between " << static_cast<unsigned>(info.numOperandsMin) << " and "
               << static_cast<unsigned>(info.numOperandsMax) << " operands";
        }
        emit(ss.str());
    }

    if (instr.labels.size() != info.numSuccessors)
    {
        std::ostringstream ss;
        ss << "expected " << static_cast<unsigned>(info.numSuccessors) << " successor";
        if (info.numSuccessors != 1)
            ss << 's';
        emit(ss.str());
    }

    if (instr.brArgs.size() > info.numSuccessors)
    {
        std::ostringstream ss;
        ss << "expected at most " << static_cast<unsigned>(info.numSuccessors)
           << " branch argument bundle";
        if (info.numSuccessors != 1)
            ss << 's';
        emit(ss.str());
    }
    else if (!instr.brArgs.empty() && instr.brArgs.size() != info.numSuccessors)
    {
        std::ostringstream ss;
        ss << "expected " << static_cast<unsigned>(info.numSuccessors) << " branch argument bundle";
        if (info.numSuccessors != 1)
            ss << 's';
        ss << ", or none";
        emit(ss.str());
    }

    return ok;
}

namespace
{

struct Context
{
    const Function &fn;
    const BasicBlock &bb;
    const std::unordered_map<std::string, const Extern *> &externs;
    const std::unordered_map<std::string, const Function *> &funcs;
    TypeInference &types;
    std::ostream &err;
};

bool expectAllOperandType(const Context &ctx, const Instr &instr, Type::Kind kind)
{
    bool ok = true;
    for (const auto &op : instr.operands)
        if (ctx.types.valueType(op).kind != kind)
        {
            ctx.err << ctx.fn.name << ":" << ctx.bb.label << ": " << makeSnippet(instr)
                    << ": operand type mismatch\n";
            ok = false;
        }
    return ok;
}

class AllocaRule final : public Rule
{
public:
    explicit AllocaRule(const Context &ctx) : ctx_(ctx) {}

    bool check(const Instr &instr) override
    {
        bool ok = true;
        if (instr.operands.size() < 1)
            return false;
        if (ctx_.types.valueType(instr.operands[0]).kind != Type::Kind::I64)
        {
            ctx_.err << ctx_.fn.name << ":" << ctx_.bb.label << ": " << makeSnippet(instr)
                     << ": size must be i64\n";
            ok = false;
        }
        if (instr.operands[0].kind == Value::Kind::ConstInt)
        {
            long long sz = instr.operands[0].i64;
            if (sz < 0)
            {
                ctx_.err << ctx_.fn.name << ":" << ctx_.bb.label << ": " << makeSnippet(instr)
                         << ": negative alloca size\n";
                ok = false;
            }
            else if (sz > (1LL << 20))
            {
                ctx_.err << ctx_.fn.name << ":" << ctx_.bb.label << ": " << makeSnippet(instr)
                         << ": warning: huge alloca\n";
            }
        }
        ctx_.types.recordResult(instr, Type(Type::Kind::Ptr));
        return ok;
    }

private:
    Context ctx_;
};

class BinaryRule final : public Rule
{
public:
    BinaryRule(const Context &ctx, Type::Kind operandKind, Type resultType)
        : ctx_(ctx), operandKind_(operandKind), resultType_(resultType)
    {
    }

    bool check(const Instr &instr) override
    {
        bool ok = true;
        if (instr.operands.size() < 2)
            return false;
        ok &= expectAllOperandType(ctx_, instr, operandKind_);
        ctx_.types.recordResult(instr, resultType_);
        return ok;
    }

private:
    Context ctx_;
    Type::Kind operandKind_;
    Type resultType_;
};

class UnaryRule final : public Rule
{
public:
    UnaryRule(const Context &ctx, Type::Kind operandKind, Type resultType)
        : ctx_(ctx), operandKind_(operandKind), resultType_(resultType)
    {
    }

    bool check(const Instr &instr) override
    {
        bool ok = true;
        if (instr.operands.size() < 1)
            return false;
        if (ctx_.types.valueType(instr.operands[0]).kind != operandKind_)
        {
            ctx_.err << ctx_.fn.name << ":" << ctx_.bb.label << ": " << makeSnippet(instr)
                     << ": operand type mismatch\n";
            ok = false;
        }
        ctx_.types.recordResult(instr, resultType_);
        return ok;
    }

private:
    Context ctx_;
    Type::Kind operandKind_;
    Type resultType_;
};

class GEPRule final : public Rule
{
public:
    explicit GEPRule(const Context &ctx) : ctx_(ctx) {}

    bool check(const Instr &instr) override
    {
        bool ok = true;
        if (instr.operands.size() < 2)
            return false;
        if (ctx_.types.valueType(instr.operands[0]).kind != Type::Kind::Ptr ||
            ctx_.types.valueType(instr.operands[1]).kind != Type::Kind::I64)
        {
            ctx_.err << ctx_.fn.name << ":" << ctx_.bb.label << ": " << makeSnippet(instr)
                     << ": operand type mismatch\n";
            ok = false;
        }
        ctx_.types.recordResult(instr, Type(Type::Kind::Ptr));
        return ok;
    }

private:
    Context ctx_;
};

class LoadRule final : public Rule
{
public:
    explicit LoadRule(const Context &ctx) : ctx_(ctx) {}

    bool check(const Instr &instr) override
    {
        bool ok = true;
        if (instr.operands.size() < 1)
            return false;
        if (instr.type.kind == Type::Kind::Void)
        {
            ctx_.err << ctx_.fn.name << ":" << ctx_.bb.label << ": " << makeSnippet(instr)
                     << ": void load type\n";
            ok = false;
        }
        if (ctx_.types.valueType(instr.operands[0]).kind != Type::Kind::Ptr)
        {
            ctx_.err << ctx_.fn.name << ":" << ctx_.bb.label << ": " << makeSnippet(instr)
                     << ": pointer type mismatch\n";
            ok = false;
        }
        [[maybe_unused]] size_t sz = TypeInference::typeSize(instr.type.kind);
        ctx_.types.recordResult(instr, instr.type);
        return ok;
    }

private:
    Context ctx_;
};

class StoreRule final : public Rule
{
public:
    explicit StoreRule(const Context &ctx) : ctx_(ctx) {}

    bool check(const Instr &instr) override
    {
        bool ok = true;
        if (instr.operands.size() < 2)
            return false;
        if (instr.type.kind == Type::Kind::Void)
        {
            ctx_.err << ctx_.fn.name << ":" << ctx_.bb.label << ": " << makeSnippet(instr)
                     << ": void store type\n";
            ok = false;
        }
        if (ctx_.types.valueType(instr.operands[0]).kind != Type::Kind::Ptr)
        {
            ctx_.err << ctx_.fn.name << ":" << ctx_.bb.label << ": " << makeSnippet(instr)
                     << ": pointer type mismatch\n";
            ok = false;
        }
        Type valueTy = ctx_.types.valueType(instr.operands[1]);
        bool isBoolConst = instr.type.kind == Type::Kind::I1 &&
                           instr.operands[1].kind == Value::Kind::ConstInt;
        if (isBoolConst)
        {
            long long v = instr.operands[1].i64;
            if (v != 0 && v != 1)
            {
                ctx_.err << ctx_.fn.name << ":" << ctx_.bb.label << ": " << makeSnippet(instr)
                         << ": boolean store expects 0 or 1\n";
                ok = false;
            }
        }
        else if (valueTy.kind != instr.type.kind)
        {
            ctx_.err << ctx_.fn.name << ":" << ctx_.bb.label << ": " << makeSnippet(instr)
                     << ": value type mismatch\n";
            ok = false;
        }
        [[maybe_unused]] size_t sz = TypeInference::typeSize(instr.type.kind);
        return ok;
    }

private:
    Context ctx_;
};

class AddrOfRule final : public Rule
{
public:
    explicit AddrOfRule(const Context &ctx) : ctx_(ctx) {}

    bool check(const Instr &instr) override
    {
        bool ok = true;
        if (instr.operands.size() != 1 || instr.operands[0].kind != Value::Kind::GlobalAddr)
        {
            ctx_.err << ctx_.fn.name << ":" << ctx_.bb.label << ": " << makeSnippet(instr)
                     << ": operand must be global\n";
            ok = false;
        }
        ctx_.types.recordResult(instr, Type(Type::Kind::Ptr));
        return ok;
    }

private:
    Context ctx_;
};

class ConstStrRule final : public Rule
{
public:
    explicit ConstStrRule(const Context &ctx) : ctx_(ctx) {}

    bool check(const Instr &instr) override
    {
        bool ok = true;
        if (instr.operands.size() != 1 || instr.operands[0].kind != Value::Kind::GlobalAddr)
        {
            ctx_.err << ctx_.fn.name << ":" << ctx_.bb.label << ": " << makeSnippet(instr)
                     << ": unknown string global\n";
            ok = false;
        }
        ctx_.types.recordResult(instr, Type(Type::Kind::Str));
        return ok;
    }

private:
    Context ctx_;
};

class ConstNullRule final : public Rule
{
public:
    explicit ConstNullRule(const Context &ctx) : ctx_(ctx) {}

    bool check(const Instr &instr) override
    {
        ctx_.types.recordResult(instr, Type(Type::Kind::Ptr));
        return true;
    }

private:
    Context ctx_;
};

class CallRule final : public Rule
{
public:
    explicit CallRule(const Context &ctx) : ctx_(ctx) {}

    bool check(const Instr &instr) override
    {
        bool ok = true;
        const Extern *sig = nullptr;
        const Function *fnSig = nullptr;
        auto itE = ctx_.externs.find(instr.callee);
        if (itE != ctx_.externs.end())
            sig = itE->second;
        else
        {
            auto itF = ctx_.funcs.find(instr.callee);
            if (itF != ctx_.funcs.end())
                fnSig = itF->second;
        }
        if (!sig && !fnSig)
        {
            ctx_.err << ctx_.fn.name << ":" << ctx_.bb.label << ": " << makeSnippet(instr)
                     << ": unknown callee @" << instr.callee << "\n";
            return false;
        }
        size_t paramCount = sig ? sig->params.size() : fnSig->params.size();
        if (instr.operands.size() != paramCount)
        {
            ctx_.err << ctx_.fn.name << ":" << ctx_.bb.label << ": " << makeSnippet(instr)
                     << ": call arg count mismatch\n";
            ok = false;
        }
        size_t checked = std::min(instr.operands.size(), paramCount);
        for (size_t i = 0; i < checked; ++i)
        {
            Type expected = sig ? sig->params[i] : fnSig->params[i].type;
            if (ctx_.types.valueType(instr.operands[i]).kind != expected.kind)
            {
                ctx_.err << ctx_.fn.name << ":" << ctx_.bb.label << ": " << makeSnippet(instr)
                         << ": call arg type mismatch\n";
                ok = false;
            }
        }
        if (instr.result)
        {
            Type ret = sig ? sig->retType : fnSig->retType;
            ctx_.types.recordResult(instr, ret);
        }
        return ok;
    }

private:
    Context ctx_;
};

class DefaultRule final : public Rule
{
public:
    explicit DefaultRule(const Context &ctx) : ctx_(ctx) {}

    bool check(const Instr &instr) override
    {
        ctx_.types.recordResult(instr, instr.type);
        return true;
    }

private:
    Context ctx_;
};

using RuleFactory = std::function<std::unique_ptr<Rule>(const Context &)>;

RuleFactory makeBinaryRule(Type::Kind operandKind, Type resultType)
{
    return [operandKind, resultType](const Context &ctx)
    { return std::make_unique<BinaryRule>(ctx, operandKind, resultType); };
}

RuleFactory makeUnaryRule(Type::Kind operandKind, Type resultType)
{
    return [operandKind, resultType](const Context &ctx)
    { return std::make_unique<UnaryRule>(ctx, operandKind, resultType); };
}

const std::unordered_map<Opcode, RuleFactory> &ruleTable()
{
    static const std::unordered_map<Opcode, RuleFactory> table = {
        {Opcode::Alloca, [](const Context &ctx) { return std::make_unique<AllocaRule>(ctx); }},
        {Opcode::Add, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I64))},
        {Opcode::Sub, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I64))},
        {Opcode::Mul, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I64))},
        {Opcode::SDiv, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I64))},
        {Opcode::UDiv, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I64))},
        {Opcode::SRem, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I64))},
        {Opcode::URem, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I64))},
        {Opcode::And, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I64))},
        {Opcode::Or, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I64))},
        {Opcode::Xor, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I64))},
        {Opcode::Shl, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I64))},
        {Opcode::LShr, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I64))},
        {Opcode::AShr, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I64))},
        {Opcode::FAdd, makeBinaryRule(Type::Kind::F64, Type(Type::Kind::F64))},
        {Opcode::FSub, makeBinaryRule(Type::Kind::F64, Type(Type::Kind::F64))},
        {Opcode::FMul, makeBinaryRule(Type::Kind::F64, Type(Type::Kind::F64))},
        {Opcode::FDiv, makeBinaryRule(Type::Kind::F64, Type(Type::Kind::F64))},
        {Opcode::ICmpEq, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I1))},
        {Opcode::ICmpNe, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I1))},
        {Opcode::SCmpLT, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I1))},
        {Opcode::SCmpLE, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I1))},
        {Opcode::SCmpGT, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I1))},
        {Opcode::SCmpGE, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I1))},
        {Opcode::UCmpLT, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I1))},
        {Opcode::UCmpLE, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I1))},
        {Opcode::UCmpGT, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I1))},
        {Opcode::UCmpGE, makeBinaryRule(Type::Kind::I64, Type(Type::Kind::I1))},
        {Opcode::FCmpEQ, makeBinaryRule(Type::Kind::F64, Type(Type::Kind::I1))},
        {Opcode::FCmpNE, makeBinaryRule(Type::Kind::F64, Type(Type::Kind::I1))},
        {Opcode::FCmpLT, makeBinaryRule(Type::Kind::F64, Type(Type::Kind::I1))},
        {Opcode::FCmpLE, makeBinaryRule(Type::Kind::F64, Type(Type::Kind::I1))},
        {Opcode::FCmpGT, makeBinaryRule(Type::Kind::F64, Type(Type::Kind::I1))},
        {Opcode::FCmpGE, makeBinaryRule(Type::Kind::F64, Type(Type::Kind::I1))},
        {Opcode::Sitofp, makeUnaryRule(Type::Kind::I64, Type(Type::Kind::F64))},
        {Opcode::Fptosi, makeUnaryRule(Type::Kind::F64, Type(Type::Kind::I64))},
        {Opcode::Zext1, makeUnaryRule(Type::Kind::I1, Type(Type::Kind::I64))},
        {Opcode::Trunc1, makeUnaryRule(Type::Kind::I64, Type(Type::Kind::I1))},
        {Opcode::GEP, [](const Context &ctx) { return std::make_unique<GEPRule>(ctx); }},
        {Opcode::Load, [](const Context &ctx) { return std::make_unique<LoadRule>(ctx); }},
        {Opcode::Store, [](const Context &ctx) { return std::make_unique<StoreRule>(ctx); }},
        {Opcode::AddrOf, [](const Context &ctx) { return std::make_unique<AddrOfRule>(ctx); }},
        {Opcode::ConstStr, [](const Context &ctx) { return std::make_unique<ConstStrRule>(ctx); }},
        {Opcode::ConstNull, [](const Context &ctx) { return std::make_unique<ConstNullRule>(ctx); }},
        {Opcode::Call, [](const Context &ctx) { return std::make_unique<CallRule>(ctx); }},
    };
    return table;
}

} // namespace

bool verifyInstruction(const Function &fn,
                       const BasicBlock &bb,
                       const Instr &instr,
                       const std::unordered_map<std::string, const Extern *> &externs,
                       const std::unordered_map<std::string, const Function *> &funcs,
                       TypeInference &types,
                       std::ostream &err)
{
    Context ctx{fn, bb, externs, funcs, types, err};
    const auto &table = ruleTable();
    auto it = table.find(instr.op);
    if (it == table.end())
    {
        DefaultRule rule(ctx);
        return rule.check(instr);
    }
    std::unique_ptr<Rule> rule = it->second(ctx);
    return rule->check(instr);
}

} // namespace il::verify
