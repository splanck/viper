// File: src/codegen/common/ArgNormalize.hpp
// Purpose: Small header-only helpers to normalize IL parameter indices into
//          canonical working registers for target-specific emitters.

#pragma once

#include <cstddef>

namespace viper::codegen::common
{

template <typename TargetInfoT, typename PhysRegT, typename EmitterT>
inline void normalize_rr_to_x0_x1(EmitterT &emit,
                                  const TargetInfoT &ti,
                                  std::size_t lhsIndex,
                                  std::size_t rhsIndex,
                                  PhysRegT scratch,
                                  PhysRegT dst0,
                                  PhysRegT dst1,
                                  std::ostream &os)
{
    const auto &order = ti.intArgOrder;
    const PhysRegT src0 = order[lhsIndex];
    const PhysRegT src1 = order[rhsIndex];
    emit.emitMovRR(os, scratch == dst0 ? dst1 : scratch, src1); // move rhs to scratch
    emit.emitMovRR(os, dst0, src0);
    emit.emitMovRR(os, dst1, scratch);
}

template <typename TargetInfoT, typename PhysRegT, typename EmitterT>
inline void move_param_to_x0(EmitterT &emit,
                             const TargetInfoT &ti,
                             std::size_t index,
                             PhysRegT dst0,
                             std::ostream &os)
{
    const auto &order = ti.intArgOrder;
    const PhysRegT src = order[index];
    if (src != dst0)
        emit.emitMovRR(os, dst0, src);
}

} // namespace viper::codegen::common
