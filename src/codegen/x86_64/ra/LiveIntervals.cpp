//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/ra/LiveIntervals.cpp
// Purpose: Implement the lightweight live interval analysis that feeds the
//          linear-scan allocator. The analysis walks each machine instruction
//          in program order and records first/last touch positions for virtual
//          registers.
// Key invariants: Instruction indices are monotonically increasing per
//                 function; repeated invocations rebuild the analysis state
//                 deterministically.
// Ownership/Lifetime: Operates on a const reference to Machine IR without
//                     mutating it. Interval results are stored in value-owned
//                     containers on the analysis instance.
// Links: src/codegen/x86_64/ra/LiveIntervals.hpp
//
//===----------------------------------------------------------------------===//

#include "LiveIntervals.hpp"

#include <limits>

namespace viper::codegen::x64::ra
{

namespace
{

/// @brief Update an interval with a new observation position.
void updateInterval(LiveInterval &interval, std::size_t pos)
{
    if (interval.start == 0U && interval.end == 0U)
    {
        interval.start = pos;
        interval.end = pos + 1U;
        return;
    }
    interval.start = std::min(interval.start, pos);
    interval.end = std::max(interval.end, pos + 1U);
}

} // namespace

void LiveIntervals::run(const MFunction &func)
{
    intervals_.clear();

    std::size_t index = 0U;
    for (const auto &block : func.blocks)
    {
        for (const auto &instr : block.instructions)
        {
            for (const auto &operand : instr.operands)
            {
                if (const auto *reg = std::get_if<OpReg>(&operand); reg && !reg->isPhys)
                {
                    auto &interval = intervals_[reg->idOrPhys];
                    if (interval.vreg == 0U && interval.end == 0U)
                    {
                        interval.vreg = reg->idOrPhys;
                        interval.cls = reg->cls;
                        interval.start = index;
                        interval.end = index + 1U;
                    }
                    else
                    {
                        updateInterval(interval, index);
                    }
                }
                else if (const auto *mem = std::get_if<OpMem>(&operand))
                {
                    if (!mem->base.isPhys)
                    {
                        auto &interval = intervals_[mem->base.idOrPhys];
                        if (interval.vreg == 0U && interval.end == 0U)
                        {
                            interval.vreg = mem->base.idOrPhys;
                            interval.cls = mem->base.cls;
                            interval.start = index;
                            interval.end = index + 1U;
                        }
                        else
                        {
                            updateInterval(interval, index);
                        }
                    }
                }
            }
            ++index;
        }
    }
}

const LiveInterval *LiveIntervals::lookup(uint16_t vreg) const noexcept
{
    auto it = intervals_.find(vreg);
    if (it == intervals_.end())
    {
        return nullptr;
    }
    return &it->second;
}

} // namespace viper::codegen::x64::ra
