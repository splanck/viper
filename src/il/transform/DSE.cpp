//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements a conservative dead-store elimination pass. Operates per basic
// block using a backward scan with a small alias-aware kill set. Calls with
// possible Mod/Ref clobber the set. Loads clear the specific address from the
// kill set. Only removes stores to addresses that are later overwritten before
// any intervening read or escaping operation.
//
//===----------------------------------------------------------------------===//

#include "il/transform/DSE.hpp"

#include "il/analysis/BasicAA.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"

#include <unordered_set>

using namespace il::core;

namespace il::transform
{

namespace
{
struct Addr
{
    // We track by Value; for temps use id; for others we retain the Value itself.
    Value v;
};

struct AddrHash
{
    size_t operator()(const Addr &a) const noexcept
    {
        // Hash by kind and id/str
        size_t h = static_cast<size_t>(a.v.kind) * 1315423911u;
        switch (a.v.kind)
        {
            case Value::Kind::Temp:
                h ^= static_cast<size_t>(a.v.id + 0x9e3779b97f4a7c15ULL);
                break;
            case Value::Kind::NullPtr:
                h ^= 0xdeadbeefULL;
                break;
            case Value::Kind::GlobalAddr:
            case Value::Kind::ConstStr:
                h ^= std::hash<std::string>{}(a.v.str);
                break;
            default:
                h ^= 0x1234567ULL;
                break;
        }
        return h;
    }
};

struct AddrEq
{
    bool operator()(const Addr &a, const Addr &b) const noexcept
    {
        // Exact match only; potential aliasing is handled via AA when needed.
        if (a.v.kind != b.v.kind)
            return false;
        using K = Value::Kind;
        switch (a.v.kind)
        {
            case K::Temp:
                return a.v.id == b.v.id;
            case K::GlobalAddr:
            case K::ConstStr:
                return a.v.str == b.v.str;
            case K::NullPtr:
                return true;
            default:
                return false;
        }
    }
};

inline bool isStoreToTempPtr(const Instr &I)
{
    return I.op == Opcode::Store && !I.operands.empty() && I.operands[0].kind == Value::Kind::Temp;
}

inline bool isLoadFromTempPtr(const Instr &I)
{
    return I.op == Opcode::Load && !I.operands.empty() && I.operands[0].kind == Value::Kind::Temp;
}

} // namespace

bool runDSE(Function &F, AnalysisManager &AM)
{
    // Acquire BasicAA when available
    viper::analysis::BasicAA &AA = AM.getFunctionResult<viper::analysis::BasicAA>("basic-aa", F);
    bool changed = false;

    for (auto &B : F.blocks)
    {
        // Backward scan in the block
        std::unordered_set<Addr, AddrHash, AddrEq> killed;

        for (int i = static_cast<int>(B.instructions.size()) - 1; i >= 0; --i)
        {
            Instr &I = B.instructions[static_cast<std::size_t>(i)];

            // Loads block further elimination for the specific address
            if (isLoadFromTempPtr(I))
            {
                Addr a{I.operands[0]};
                killed.erase(a);
                continue;
            }

            // Calls: conservative clobber when may Mod/Ref
            if (I.op == Opcode::Call || I.op == Opcode::CallIndirect)
            {
                auto mr = AA.modRef(I);
                if (mr != viper::analysis::ModRefResult::NoModRef)
                    killed.clear();
                continue;
            }

            // Store: if the address is already killed by a later store and no read intervened,
            // then this store is dead.
            if (isStoreToTempPtr(I))
            {
                Addr a{I.operands[0]};
                bool dead = false;

                // Quick-path exact match
                if (killed.find(a) != killed.end())
                {
                    dead = true;
                }
                else
                {
                    // Check aliasing against the killed set using BasicAA
                    for (const auto &k : killed)
                    {
                        if (AA.alias(a.v, k.v) != viper::analysis::AliasResult::NoAlias)
                        {
                            // An aliasing later store exists; current is dead
                            dead = true;
                            break;
                        }
                    }
                }

                if (dead)
                {
                    B.instructions.erase(B.instructions.begin() + static_cast<std::size_t>(i));
                    changed = true;
                    // Note: do not advance i (the loop decrements i) — keep indices consistent
                    continue;
                }
                // Not dead: mark this address as killed for earlier stores
                killed.insert(a);
                continue;
            }
        }
    }

    return changed;
}

} // namespace il::transform
