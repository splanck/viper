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

/// @file
/// @brief Implements a lightweight within-block common subexpression elimination pass.
/// @details Scans each basic block independently, building a map from
///          normalized expression keys to their dominating result values. When
///          a repeated pure expression is encountered, all uses are replaced
///          with the original result and the redundant instruction is removed.

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

/// @brief Run early common subexpression elimination on a function.
/// @details Processes each basic block independently to avoid cross-block
///          analysis. The pass only folds instructions that pass
///          @ref makeValueKey (side-effect free, non-trapping, and
///          non-memory). Operand normalization handles commutative opcodes.
/// @param F Function to optimize in place.
/// @return True if any redundant instruction was removed; false otherwise.
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
