//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements a dominator-tree based Global Value Numbering pass with a simple
// Redundant Load Elimination. We conservatively match pure, side-effect-free
// instructions by opcode/type/operands (with commutative normalization) and
// reuse dominating results. For loads, we memoise (ptr,type) reads and reuse
// when no intervening clobber occurs (based on BasicAA and coarse memory
// effects). We traverse blocks in dominator-tree preorder and thread a local
// state to children so information flows along dominating paths only.
//
//===----------------------------------------------------------------------===//

#include "il/transform/GVN.hpp"

#include "il/transform/AnalysisManager.hpp"

#include "il/analysis/BasicAA.hpp"
#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/transform/analysis/Liveness.hpp" // for CFGInfo

#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include "il/utils/Utils.hpp"

#include <algorithm>
#include <cassert>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace il::core;

namespace il::transform
{

namespace
{
// Lightweight value hashing/equality to support expression keys and load keys.
struct ValueHash
{
    size_t operator()(const Value &v) const noexcept
    {
        size_t h = static_cast<size_t>(v.kind) * 1469598103934665603ULL;
        switch (v.kind)
        {
            case Value::Kind::Temp:
                h ^= static_cast<size_t>(v.id) + 0x9e3779b97f4a7c15ULL;
                break;
            case Value::Kind::ConstInt:
                h ^= static_cast<size_t>(v.i64) ^ (v.isBool ? 0xBEEF : 0);
                break;
            case Value::Kind::ConstFloat:
            {
                union
                {
                    double d;
                    unsigned long long u;
                } u{};

                u.d = v.f64;
                h ^= static_cast<size_t>(u.u);
                break;
            }
            case Value::Kind::ConstStr:
            case Value::Kind::GlobalAddr:
                h ^= std::hash<std::string>{}(v.str);
                break;
            case Value::Kind::NullPtr:
                h ^= 0xabcdefULL;
                break;
        }
        return h;
    }
};

struct ValueEq
{
    bool operator()(const Value &a, const Value &b) const noexcept
    {
        if (a.kind != b.kind)
            return false;
        switch (a.kind)
        {
            case Value::Kind::Temp:
                return a.id == b.id;
            case Value::Kind::ConstInt:
                return a.i64 == b.i64 && a.isBool == b.isBool;
            case Value::Kind::ConstFloat:
                return a.f64 == b.f64;
            case Value::Kind::ConstStr:
            case Value::Kind::GlobalAddr:
                return a.str == b.str;
            case Value::Kind::NullPtr:
                return true;
        }
        return false;
    }
};

inline bool isCommutative(Opcode op)
{
    switch (op)
    {
        case Opcode::Add:
        case Opcode::Mul:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::ICmpEq:
        case Opcode::ICmpNe:
        case Opcode::FAdd:
        case Opcode::FMul:
        case Opcode::FCmpEQ:
        case Opcode::FCmpNE:
            return true;
        default:
            return false;
    }
}

inline bool isPureCandidate(const Instr &I)
{
    const auto &meta = getOpcodeInfo(I.op);
    if (meta.isTerminator || meta.hasSideEffects)
        return false;
    // Avoid memory operations; loads handled separately.
    if (hasMemoryRead(I.op) || hasMemoryWrite(I.op))
        return false;
    if (!I.labels.empty() || !I.brArgs.empty())
        return false;
    if (!I.result)
        return false;
    return true;
}

struct ExprKey
{
    Opcode op;
    Type::Kind type;
    std::vector<Value> ops; // normalized

    bool operator==(const ExprKey &o) const noexcept
    {
        if (op != o.op || type != o.type || ops.size() != o.ops.size())
            return false;
        ValueEq eq;
        for (std::size_t i = 0; i < ops.size(); ++i)
        {
            if (!eq(ops[i], o.ops[i]))
                return false;
        }
        return true;
    }
};

struct ExprKeyHash
{
    size_t operator()(const ExprKey &k) const noexcept
    {
        size_t h = static_cast<size_t>(k.op) * 1099511628211ULL ^ static_cast<size_t>(k.type);
        ValueHash hv;
        for (const auto &v : k.ops)
        {
            h ^= hv(v) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        }
        return h;
    }
};

inline std::vector<Value> normalizeOperands(const Instr &I)
{
    std::vector<Value> ops = I.operands;
    if (isCommutative(I.op) && ops.size() >= 2)
    {
        auto rank = [](const Value &v) -> std::tuple<int, unsigned, std::string>
        {
            // Prefer temporaries first by ID, then constants by a stable key
            switch (v.kind)
            {
                case Value::Kind::Temp:
                    return {2, v.id, {}};
                case Value::Kind::ConstInt:
                    return {1, static_cast<unsigned>(v.i64 ^ (v.isBool ? 1u : 0u)), {}};
                case Value::Kind::ConstFloat:
                {
                    union
                    {
                        double d;
                        unsigned long long u;
                    } u{};
                    u.d = v.f64;
                    return {1, static_cast<unsigned>(u.u), {}};
                }
                case Value::Kind::GlobalAddr:
                case Value::Kind::ConstStr:
                    return {0, 0u, v.str};
                case Value::Kind::NullPtr:
                    return {0, 0u, std::string("null")};
            }
            return {0, 0u, std::string{}};
        };
        if (!(rank(ops[0]) >= rank(ops[1])))
            std::swap(ops[0], ops[1]);
    }
    return ops;
}

struct LoadKey
{
    Value ptr;
    Type::Kind type;

    bool operator==(const LoadKey &o) const noexcept
    {
        ValueEq eq;
        return type == o.type && eq(ptr, o.ptr);
    }
};

struct LoadKeyHash
{
    size_t operator()(const LoadKey &k) const noexcept
    {
        ValueHash hv;
        return hv(k.ptr) ^ (static_cast<size_t>(k.type) * 0x9e3779b97f4a7c15ULL);
    }
};

struct State
{
    std::unordered_map<ExprKey, Value, ExprKeyHash> exprs;
    std::unordered_map<LoadKey, Value, LoadKeyHash> loads;
};

void visitBlock(Function &F,
                BasicBlock *B,
                const viper::analysis::DomTree &DT,
                viper::analysis::BasicAA &AA,
                State state,
                bool &changed)
{
    for (std::size_t idx = 0; idx < B->instructions.size();)
    {
        Instr &I = B->instructions[idx];

        // Redundant Load Elimination
        if (I.op == Opcode::Load && I.result && !I.operands.empty())
        {
            const Value &ptr = I.operands[0];
            LoadKey key{ptr, I.type.kind};

            // Try exact match first
            auto it = state.loads.find(key);
            if (it != state.loads.end())
            {
                viper::il::replaceAllUses(F, *I.result, it->second);
                B->instructions.erase(B->instructions.begin() + static_cast<long>(idx));
                changed = true;
                continue; // don't advance idx
            }

            // Otherwise, scan for alias-equivalent entries (MustAlias)
            bool replaced = false;
            for (const auto &kv : state.loads)
            {
                if (kv.first.type != key.type)
                    continue;
                if (AA.alias(kv.first.ptr, key.ptr) == viper::analysis::AliasResult::MustAlias)
                {
                    viper::il::replaceAllUses(F, *I.result, kv.second);
                    B->instructions.erase(B->instructions.begin() + static_cast<long>(idx));
                    changed = true;
                    replaced = true;
                    break;
                }
            }
            if (replaced)
                continue;

            // Record available load
            state.loads.emplace(key, Value::temp(*I.result));
            ++idx;
            continue;
        }

        // Memory clobber: stores or other writes invalidate relevant loads
        if (I.op == Opcode::Store && I.operands.size() >= 2)
        {
            const Value &stPtr = I.operands[0];
            for (auto it = state.loads.begin(); it != state.loads.end();)
            {
                if (AA.alias(it->first.ptr, stPtr) != viper::analysis::AliasResult::NoAlias)
                    it = state.loads.erase(it);
                else
                    ++it;
            }
            ++idx;
            continue;
        }

        if (I.op == Opcode::Call || I.op == Opcode::CallIndirect)
        {
            auto mr = AA.modRef(I);
            if (mr != viper::analysis::ModRefResult::NoModRef &&
                mr != viper::analysis::ModRefResult::Ref)
            {
                state.loads.clear();
            }
            ++idx;
            continue;
        }

        // Other known writes invalidate all memoised loads. Be careful to not
        // treat Unknown (e.g. branch/ret) as a write.
        {
            using il::core::MemoryEffects;
            auto me = memoryEffects(I.op);
            if (me == MemoryEffects::Write || me == MemoryEffects::ReadWrite)
            {
                state.loads.clear();
                ++idx;
                continue;
            }
        }

        // Pure expression GVN
        if (isPureCandidate(I))
        {
            ExprKey k{I.op, I.type.kind, normalizeOperands(I)};
            auto found = state.exprs.find(k);
            if (found != state.exprs.end())
            {
                viper::il::replaceAllUses(F, *I.result, found->second);
                B->instructions.erase(B->instructions.begin() + static_cast<long>(idx));
                changed = true;
                continue;
            }
            state.exprs.emplace(std::move(k), Value::temp(*I.result));
            ++idx;
            continue;
        }

        // Default: advance
        ++idx;
    }

    // Recurse to children in dominator-tree preorder
    auto it = DT.children.find(B);
    if (it != DT.children.end())
    {
        for (auto *Child : it->second)
        {
            visitBlock(F, Child, DT, AA, state, changed);
        }
    }
}

} // namespace

std::string_view GVN::id() const
{
    return "gvn";
}

PreservedAnalyses GVN::run(Function &function, AnalysisManager &analysis)
{
    // Query required analyses
    (void)analysis.getFunctionResult<il::transform::CFGInfo>("cfg", function); // ensure available
    auto &dom = analysis.getFunctionResult<viper::analysis::DomTree>("dominators", function);
    auto &aa = analysis.getFunctionResult<viper::analysis::BasicAA>("basic-aa", function);

    bool changed = false;

    if (function.blocks.empty())
        return PreservedAnalyses::all();

    State state;

    // Start at entry block
    visitBlock(function, &function.blocks.front(), dom, aa, state, changed);

    if (!changed)
        return PreservedAnalyses::all();

    PreservedAnalyses p;
    p.preserveAllModules();
    return p;
}

void registerGVNPass(PassRegistry &registry)
{
    registry.registerFunctionPass("gvn", []() { return std::make_unique<GVN>(); });
}

} // namespace il::transform
