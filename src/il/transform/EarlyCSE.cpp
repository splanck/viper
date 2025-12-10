//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements a simple within-block common subexpression elimination. Only
// handles a subset of pure opcodes (integer/float arithmetic, bitwise, compares)
// and avoids memory operations, control flow, and calls. Commutative ops are
// normalized to improve hit rate.
//
//===----------------------------------------------------------------------===//

#include "il/transform/EarlyCSE.hpp"

#include "il/transform/ValueKey.hpp"

#include "il/core/Instr.hpp"
#include "il/utils/Utils.hpp"

#include <unordered_map>

using namespace il::core;

namespace il::transform
{

namespace
{
} // namespace

bool runEarlyCSE(Function &F)
{
    bool changed = false;
    for (auto &B : F.blocks)
    {
        std::unordered_map<ValueKey, Value, ValueKeyHash> table;
        for (std::size_t idx = 0; idx < B.instructions.size();)
        {
            Instr &I = B.instructions[idx];
            auto key = makeValueKey(I);
            if (!key)
            {
                ++idx;
                continue;
            }
            auto it = table.find(*key);
            if (it != table.end())
            {
                const Value existing = it->second;
                viper::il::replaceAllUses(F, *I.result, existing);
                B.instructions.erase(B.instructions.begin() + static_cast<long>(idx));
                changed = true;
                continue; // don't advance idx
            }
            table.emplace(std::move(*key), Value::temp(*I.result));
            ++idx;
        }
    }
    return changed;
}

} // namespace il::transform
