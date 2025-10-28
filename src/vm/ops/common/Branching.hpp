//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/vm/ops/common/Branching.hpp
// Purpose: Provide shared helpers for VM branching and switch dispatch logic.
// Key invariants: Targets always reference valid basic blocks and branch
//                 arguments are propagated atomically to destination params.
// Ownership/Lifetime: Helpers borrow frame and instruction state; no ownership
//                     is transferred.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vm/VM.hpp"

#include <span>
#include <vector>

namespace il::vm::detail::ops::common
{

using Scalar = int32_t;

struct Target
{
    const il::core::Instr *instr = nullptr;
    size_t labelIndex = 0;
    const il::core::BasicBlock *block = nullptr;
    std::span<const il::core::Value> args{};
    const il::core::BasicBlock **cursor = nullptr;
    size_t *ip = nullptr;
    const viper::vm::SwitchCacheEntry *cache = nullptr;
    bool valid = false;
};

struct Case
{
    Scalar lower = 0;
    Scalar upper = 0;
    Target target{};
};

Target select_case(Scalar scrutinee, std::span<const Case> table, Target default_tgt);

void jump(Frame &fr, Target target);

Scalar eval_scrutinee(Frame &fr, const il::core::Instr &instr);

Target make_target(const il::core::Instr &instr,
                   size_t labelIndex,
                   const VM::BlockMap &blocks,
                   const il::core::BasicBlock *&bb,
                   size_t &ip,
                   const viper::vm::SwitchCacheEntry *cacheEntry = nullptr);

} // namespace il::vm::detail::ops::common

