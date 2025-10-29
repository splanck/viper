//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/ra/Coalescer.cpp
// Purpose: Lower PX_COPY bundles into executable move sequences while
//          respecting spill state managed by the linear-scan allocator.
// Key invariants: Scratch registers allocated to break cycles are released back
//                 to the allocator after use; generated code preserves the
//                 semantics of the original parallel copy.
// Ownership/Lifetime: Operates on Machine IR provided by the allocator without
//                     taking ownership.
// Links: src/codegen/x86_64/ra/Coalescer.hpp
//
//===----------------------------------------------------------------------===//

#include "Coalescer.hpp"

#include "Allocator.hpp"
#include "Spiller.hpp"

#include <algorithm>

/// @file
/// @brief Lowers PX_COPY pseudo instructions into executable move sequences.
/// @details The coalescer collaborates with the linear-scan allocator and
///          spiller to materialise register moves, managing scratch registers and
///          spill reloads to preserve the semantics of the original parallel
///          copy bundles.

namespace viper::codegen::x64::ra
{

namespace
{

struct ScratchRelease
{
    PhysReg phys{PhysReg::RAX};
    RegClass cls{RegClass::GPR};
};

/// @brief Wrap a physical register in a Machine IR operand.
/// @details The helper normalises construction of register operands so both the
///          coalescer and the spiller share identical operand layouts.  It
///          forwards to @ref makePhysRegOperand while performing the
///          `PhysReg`→`uint16_t` cast mandated by the MIR representation.
/// @param cls Register class describing the operand encoding.
/// @param reg Physical register identifier being wrapped.
/// @return Machine operand referencing @p reg.
[[nodiscard]] Operand makePhysOperand(RegClass cls, PhysReg reg)
{
    return makePhysRegOperand(cls, static_cast<uint16_t>(reg));
}

} // namespace

/// @brief Construct a coalescer tied to a specific allocator and spiller.
/// @details The constructor stores references to the linear-scan allocator and
///          spiller so future copy lowering can request scratch registers and
///          emit loads/stores.  No MIR is mutated during construction; the
///          coalescer simply captures the collaborators it will use later.
/// @param allocator Linear-scan allocator supplying register state.
/// @param spiller Spiller responsible for materialising loads and stores.
Coalescer::Coalescer(LinearScanAllocator &allocator, Spiller &spiller)
    : allocator_(allocator), spiller_(spiller)
{
}

/// @brief Expand a @c PX_COPY pseudo into executable machine instructions.
/// @details The algorithm proceeds in three phases:
///          1. Analyse the pseudo operands and build @ref CopyTask entries that
///             describe the source and destination for each pair.
///          2. Emit prefix instructions that materialise spilled or unmapped
///             values by requesting temporary registers from the allocator and
///             issuing loads where necessary.
///          3. Consume the copy tasks while respecting dependency cycles,
///             breaking them via scratch registers and ensuring every temporary
///             is released back to the allocator.
///          The resulting instruction stream is appended to @p out in the order
///          it should execute.
/// @param instr @c PX_COPY instruction to lower.
/// @param out Vector receiving the lowered instruction sequence.
void Coalescer::lower(const MInstr &instr, std::vector<MInstr> &out)
{
    std::vector<MInstr> prefix{};
    std::vector<ScratchRelease> scratch{};
    std::vector<CopyTask> tasks{};

    for (std::size_t i = 0; i + 1 < instr.operands.size(); i += 2)
    {
        const auto &dstOp = instr.operands[i];
        const auto &srcOp = instr.operands[i + 1];

        const auto *dstReg = std::get_if<OpReg>(&dstOp);
        const auto *srcReg = std::get_if<OpReg>(&srcOp);
        if (!dstReg || !srcReg)
        {
            continue; // Phase A: expect register pairs only.
        }

        CopyTask task{};
        task.cls = dstReg->cls;
        task.destVReg.reset();

        if (dstReg->isPhys)
        {
            task.destKind = CopyTask::DestKind::Reg;
            task.destReg = static_cast<PhysReg>(dstReg->idOrPhys);
        }
        else
        {
            auto &dstState = allocator_.stateFor(dstReg->cls, dstReg->idOrPhys);
            task.destVReg = dstReg->idOrPhys;
            if (dstState.spill.needsSpill)
            {
                spiller_.ensureSpillSlot(dstState.cls, dstState.spill);
                task.destKind = CopyTask::DestKind::Mem;
                task.destSlot = dstState.spill.slot;
            }
            else
            {
                if (!dstState.hasPhys)
                {
                    const PhysReg phys = allocator_.takeRegister(dstState.cls, prefix);
                    dstState.hasPhys = true;
                    dstState.phys = phys;
                    allocator_.addActive(dstState.cls, dstReg->idOrPhys);
                    allocator_.result_.vregToPhys[dstReg->idOrPhys] = phys;
                }
                task.destKind = CopyTask::DestKind::Reg;
                task.destReg = dstState.phys;
            }
        }

        if (srcReg->isPhys)
        {
            task.src.kind = CopySource::Kind::Reg;
            task.src.reg = static_cast<PhysReg>(srcReg->idOrPhys);
        }
        else
        {
            auto &srcState = allocator_.stateFor(srcReg->cls, srcReg->idOrPhys);
            if (srcState.spill.needsSpill)
            {
                spiller_.ensureSpillSlot(srcState.cls, srcState.spill);
                const PhysReg scratchReg = allocator_.takeRegister(srcState.cls, prefix);
                prefix.push_back(spiller_.makeLoad(srcState.cls, scratchReg, srcState.spill));
                scratch.push_back(ScratchRelease{scratchReg, srcState.cls});
                task.src.kind = CopySource::Kind::Reg;
                task.src.reg = scratchReg;
            }
            else
            {
                if (!srcState.hasPhys)
                {
                    const PhysReg phys = allocator_.takeRegister(srcState.cls, prefix);
                    srcState.hasPhys = true;
                    srcState.phys = phys;
                    allocator_.addActive(srcState.cls, srcReg->idOrPhys);
                    allocator_.result_.vregToPhys[srcReg->idOrPhys] = phys;
                }
                task.src.kind = CopySource::Kind::Reg;
                task.src.reg = srcState.phys;
            }
        }

        tasks.push_back(task);
    }

    for (auto &pre : prefix)
    {
        out.push_back(std::move(pre));
    }

    std::vector<MInstr> generated{};
    generated.reserve(tasks.size());

    while (!tasks.empty())
    {
        bool progress = false;
        for (std::size_t i = 0; i < tasks.size(); ++i)
        {
            const auto task = tasks[i];
            bool canEmit = false;
            if (task.destKind == CopyTask::DestKind::Mem)
            {
                canEmit = true;
            }
            else if (task.src.kind == CopySource::Kind::Mem)
            {
                canEmit = true;
            }
            else if (task.src.kind == CopySource::Kind::Reg)
            {
                bool srcIsDest = false;
                for (const auto &other : tasks)
                {
                    if (other.destKind == CopyTask::DestKind::Reg && other.destReg == task.src.reg)
                    {
                        srcIsDest = true;
                        break;
                    }
                }
                canEmit = !srcIsDest || task.destReg == task.src.reg;
            }

            if (!canEmit)
            {
                continue;
            }

            emitCopyTask(task, generated);
            tasks.erase(tasks.begin() + static_cast<long>(i));
            progress = true;
            break;
        }

        if (progress)
        {
            continue;
        }

        auto it = std::find_if(tasks.begin(),
                               tasks.end(),
                               [](const CopyTask &t)
                               {
                                   return t.destKind == CopyTask::DestKind::Reg &&
                                          t.src.kind == CopySource::Kind::Reg;
                               });
        if (it == tasks.end())
        {
            break;
        }

        CopyTask cycleTask = *it;
        const PhysReg srcReg = cycleTask.src.reg;

        std::vector<MInstr> tmpPrefix{};
        const PhysReg temp = allocator_.takeRegister(cycleTask.cls, tmpPrefix);
        for (auto &pre : tmpPrefix)
        {
            generated.push_back(std::move(pre));
        }
        generated.push_back(allocator_.makeMove(cycleTask.cls, temp, srcReg));
        for (auto &pending : tasks)
        {
            if (pending.src.kind == CopySource::Kind::Reg && pending.src.reg == srcReg)
            {
                pending.src.reg = temp;
            }
        }
        scratch.push_back(ScratchRelease{temp, cycleTask.cls});
    }

    for (auto &instrOut : generated)
    {
        out.push_back(std::move(instrOut));
    }

    for (const auto &rel : scratch)
    {
        allocator_.releaseRegister(rel.phys, rel.cls);
    }
}

/// @brief Materialise one lowered copy task.
/// @details Depending on whether the destination resides in memory or a
///          register, the helper either emits direct moves or synthesises loads
///          into scratch registers before performing the store.  Memory-to-memory
///          copies are handled by loading into a temporary first so the target
///          architecture never observes illegal instructions.  Any temporaries
///          acquired during the call are released once their final use is
///          emitted.
/// @param task Copy description built during @ref lower.
/// @param generated Output buffer receiving the materialised instructions.
void Coalescer::emitCopyTask(const CopyTask &task, std::vector<MInstr> &generated)
{
    if (task.destKind == CopyTask::DestKind::Mem)
    {
        if (task.src.kind == CopySource::Kind::Reg)
        {
            generated.push_back(spiller_.makeStore(task.cls, SpillPlan{true, task.destSlot}, task.src.reg));
        }
        else
        {
            std::vector<MInstr> tmpPrefix{};
            const PhysReg tmp = allocator_.takeRegister(task.cls, tmpPrefix);
            for (auto &pre : tmpPrefix)
            {
                generated.push_back(std::move(pre));
            }
            generated.push_back(spiller_.makeLoad(task.cls, tmp, SpillPlan{true, task.src.slot}));
            generated.push_back(spiller_.makeStore(task.cls, SpillPlan{true, task.destSlot}, tmp));
            allocator_.releaseRegister(tmp, task.cls);
        }
        return;
    }

    if (task.src.kind == CopySource::Kind::Reg)
    {
        generated.push_back(allocator_.makeMove(task.cls, task.destReg, task.src.reg));
    }
    else
    {
        generated.push_back(spiller_.makeLoad(task.cls, task.destReg, SpillPlan{true, task.src.slot}));
    }
}

} // namespace viper::codegen::x64::ra
