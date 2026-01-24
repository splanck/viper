//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/device.cpp
// Purpose: Device management syscall handlers (0x100-0x10F).
//
//===----------------------------------------------------------------------===//

#include "handlers_internal.hpp"
#include "../../arch/aarch64/gic.hpp"
#include "../../cap/handle.hpp"
#include "../../cap/rights.hpp"
#include "../../kobj/shm.hpp"
#include "../../lib/spinlock.hpp"
#include "../../mm/pmm.hpp"
#include "../../sched/scheduler.hpp"
#include "../../sched/task.hpp"
#include "../../viper/address_space.hpp"
#include "../../viper/viper.hpp"

using kernel::Spinlock;
using kernel::SpinlockGuard;

namespace syscall
{

// =============================================================================
// IRQ State Management
// =============================================================================

struct IrqState
{
    u32 owner_task_id;
    u32 owner_viper_id;
    sched::WaitQueue waiters;
    bool pending;
    bool enabled;
    Spinlock lock;
};

static IrqState irq_states[gic::MAX_IRQS];
static bool irq_states_initialized = false;

static void init_irq_states()
{
    if (irq_states_initialized)
        return;

    for (u32 i = 0; i < gic::MAX_IRQS; i++)
    {
        irq_states[i].owner_task_id = 0;
        irq_states[i].owner_viper_id = 0;
        sched::wait_init(&irq_states[i].waiters);
        irq_states[i].pending = false;
        irq_states[i].enabled = false;
    }
    irq_states_initialized = true;
}

static void user_irq_handler(u32 irq)
{
    if (irq >= gic::MAX_IRQS)
        return;

    if (!irq_states_initialized)
    {
        gic::disable_irq(irq);
        return;
    }

    IrqState *state = &irq_states[irq];
    SpinlockGuard guard(state->lock);

    if (state->owner_task_id == 0)
    {
        gic::disable_irq(irq);
        state->enabled = false;
        return;
    }

    gic::disable_irq(irq);
    state->enabled = false;
    state->pending = true;
    sched::wait_wake_one(&state->waiters);
}

// =============================================================================
// Known Device Regions
// =============================================================================

struct DeviceMmioRegion
{
    const char *name;
    u64 phys_base;
    u64 size;
    u32 irq;
};

static const DeviceMmioRegion known_devices[] = {
    {"uart0", 0x09000000, 0x1000, 33},   {"rtc", 0x09010000, 0x1000, 34},
    {"gpio", 0x09030000, 0x1000, 35},    {"virtio0", 0x0a000000, 0x200, 48},
    {"virtio1", 0x0a000200, 0x200, 49},  {"virtio2", 0x0a000400, 0x200, 50},
    {"virtio3", 0x0a000600, 0x200, 51},  {"virtio4", 0x0a000800, 0x200, 52},
    {"virtio5", 0x0a000a00, 0x200, 53},  {"virtio6", 0x0a000c00, 0x200, 54},
    {"virtio7", 0x0a000e00, 0x200, 55},  {"virtio8", 0x0a001000, 0x200, 56},
    {"virtio9", 0x0a001200, 0x200, 57},  {"virtio10", 0x0a001400, 0x200, 58},
    {"virtio11", 0x0a001600, 0x200, 59}, {"virtio12", 0x0a001800, 0x200, 60},
    {"virtio13", 0x0a001a00, 0x200, 61}, {"virtio14", 0x0a001c00, 0x200, 62},
    {"virtio15", 0x0a001e00, 0x200, 63}, {"virtio16", 0x0a002000, 0x200, 64},
    {"virtio17", 0x0a002200, 0x200, 65}, {"virtio18", 0x0a002400, 0x200, 66},
    {"virtio19", 0x0a002600, 0x200, 67}, {"virtio20", 0x0a002800, 0x200, 68},
    {"virtio21", 0x0a002a00, 0x200, 69}, {"virtio22", 0x0a002c00, 0x200, 70},
    {"virtio23", 0x0a002e00, 0x200, 71}, {"virtio24", 0x0a003000, 0x200, 72},
    {"virtio25", 0x0a003200, 0x200, 73}, {"virtio26", 0x0a003400, 0x200, 74},
    {"virtio27", 0x0a003600, 0x200, 75}, {"virtio28", 0x0a003800, 0x200, 76},
    {"virtio29", 0x0a003a00, 0x200, 77}, {"virtio30", 0x0a003c00, 0x200, 78},
    {"virtio31", 0x0a003e00, 0x200, 79},
};
static constexpr u32 KNOWN_DEVICE_COUNT = sizeof(known_devices) / sizeof(known_devices[0]);

static bool has_device_cap(viper::Viper *v, cap::Rights required)
{
    if (!v || !v->cap_table)
        return false;

    for (usize i = 0; i < v->cap_table->capacity(); i++)
    {
        cap::Entry *e = v->cap_table->entry_at(i);
        if (!e || e->kind == cap::Kind::Invalid)
            continue;
        if (e->kind != cap::Kind::Device)
            continue;
        if (cap::has_rights(e->rights, required))
            return true;
    }

    return false;
}

// =============================================================================
// DMA Allocation Tracking
// =============================================================================

struct DmaAllocation
{
    u64 phys_addr;
    u64 virt_addr;
    u64 size;
    u32 owner_viper_id;
    bool in_use;
};

static constexpr u32 MAX_DMA_ALLOCATIONS = 64;
static DmaAllocation dma_allocations[MAX_DMA_ALLOCATIONS];
static Spinlock dma_lock;
static bool dma_initialized = false;

static void init_dma_allocations()
{
    if (dma_initialized)
        return;
    for (u32 i = 0; i < MAX_DMA_ALLOCATIONS; i++)
    {
        dma_allocations[i].in_use = false;
    }
    dma_initialized = true;
}

// =============================================================================
// Shared Memory Tracking
// =============================================================================

struct ShmMapping
{
    u32 owner_viper_id;
    u64 virt_addr;
    u64 size;
    kobj::SharedMemory *shm;
    bool in_use;
};

static constexpr u32 MAX_SHM_MAPPINGS = 256;
static ShmMapping shm_mappings[MAX_SHM_MAPPINGS];
static Spinlock shm_lock;
static bool shm_mappings_initialized = false;

static void init_shm_mappings()
{
    if (shm_mappings_initialized)
        return;
    for (u32 i = 0; i < MAX_SHM_MAPPINGS; i++)
    {
        shm_mappings[i].in_use = false;
        shm_mappings[i].owner_viper_id = 0;
        shm_mappings[i].virt_addr = 0;
        shm_mappings[i].size = 0;
        shm_mappings[i].shm = nullptr;
    }
    shm_mappings_initialized = true;
}

static bool track_shm_mapping(u32 viper_id, u64 virt_addr, u64 size, kobj::SharedMemory *shm)
{
    init_shm_mappings();
    SpinlockGuard guard(shm_lock);

    for (u32 i = 0; i < MAX_SHM_MAPPINGS; i++)
    {
        if (shm_mappings[i].in_use && shm_mappings[i].owner_viper_id == viper_id &&
            shm_mappings[i].virt_addr == virt_addr)
        {
            return false;
        }
    }

    for (u32 i = 0; i < MAX_SHM_MAPPINGS; i++)
    {
        if (!shm_mappings[i].in_use)
        {
            shm_mappings[i].in_use = true;
            shm_mappings[i].owner_viper_id = viper_id;
            shm_mappings[i].virt_addr = virt_addr;
            shm_mappings[i].size = size;
            shm_mappings[i].shm = shm;
            return true;
        }
    }

    return false;
}

static bool untrack_shm_mapping(u32 viper_id,
                                u64 virt_addr,
                                u64 *out_size,
                                kobj::SharedMemory **out_shm)
{
    init_shm_mappings();
    SpinlockGuard guard(shm_lock);

    for (u32 i = 0; i < MAX_SHM_MAPPINGS; i++)
    {
        if (!shm_mappings[i].in_use)
            continue;
        if (shm_mappings[i].owner_viper_id != viper_id)
            continue;
        if (shm_mappings[i].virt_addr != virt_addr)
            continue;

        if (out_size)
            *out_size = shm_mappings[i].size;
        if (out_shm)
            *out_shm = shm_mappings[i].shm;

        shm_mappings[i].in_use = false;
        shm_mappings[i].owner_viper_id = 0;
        shm_mappings[i].virt_addr = 0;
        shm_mappings[i].size = 0;
        shm_mappings[i].shm = nullptr;
        return true;
    }

    return false;
}

// =============================================================================
// Device Syscall Handlers
// =============================================================================

SyscallResult sys_map_device(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    u64 phys_addr = a0;
    u64 size = a1;
    u64 user_virt = a2;

    if (size == 0 || size > 16 * 1024 * 1024)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    viper::Viper *v = viper::current();
    if (!v || !v->cap_table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    if (!has_device_cap(v, cap::CAP_DEVICE_ACCESS))
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    bool valid_device = false;
    for (u32 i = 0; i < KNOWN_DEVICE_COUNT; i++)
    {
        if (phys_addr >= known_devices[i].phys_base &&
            phys_addr + size <= known_devices[i].phys_base + known_devices[i].size)
        {
            valid_device = true;
            break;
        }
    }

    if (!valid_device)
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    if (user_virt == 0)
    {
        user_virt = 0x100000000ULL + (phys_addr & 0x0FFFFFFFULL);
    }

    u64 phys_aligned = pmm::page_align_down(phys_addr);
    u64 virt_aligned = pmm::page_align_down(user_virt);
    u64 size_aligned = pmm::page_align_up(size + (phys_addr - phys_aligned));

    if (!as->map(virt_aligned, phys_aligned, size_aligned, viper::prot::RW))
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    return SyscallResult::ok(virt_aligned + (phys_addr - phys_aligned));
}

SyscallResult sys_irq_register(u64 a0, u64, u64, u64, u64, u64)
{
    u32 irq = static_cast<u32>(a0);

    if (irq < 32 || irq >= gic::MAX_IRQS)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    viper::Viper *v = viper::current();
    task::Task *t = task::current();
    if (!v || !t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    if (!has_device_cap(v, cap::CAP_IRQ_ACCESS))
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    init_irq_states();

    IrqState *state = &irq_states[irq];
    SpinlockGuard guard(state->lock);

    if (gic::has_handler(irq))
    {
        return SyscallResult::err(error::VERR_BUSY);
    }

    if (state->owner_task_id != 0)
    {
        return SyscallResult::err(error::VERR_BUSY);
    }

    state->owner_task_id = t->id;
    state->owner_viper_id = v->id;
    state->pending = false;
    state->enabled = true;

    gic::register_handler(irq, user_irq_handler);
    gic::enable_irq(irq);

    return SyscallResult::ok();
}

SyscallResult sys_irq_wait(u64 a0, u64 a1, u64, u64, u64, u64)
{
    u32 irq = static_cast<u32>(a0);
    u64 timeout_ms = a1;
    (void)timeout_ms;

    if (irq < 32 || irq >= gic::MAX_IRQS)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    task::Task *t = task::current();
    viper::Viper *v = viper::current();
    if (!t || !v)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    init_irq_states();

    IrqState *state = &irq_states[irq];

    {
        SpinlockGuard guard(state->lock);
        if (state->owner_task_id != t->id)
        {
            return SyscallResult::err(error::VERR_PERMISSION);
        }

        if (state->pending)
        {
            state->pending = false;
            return SyscallResult::ok();
        }

        sched::wait_enqueue(&state->waiters, t);
    }

    task::yield();

    {
        SpinlockGuard guard(state->lock);
        if (state->pending)
        {
            state->pending = false;
            return SyscallResult::ok();
        }
    }

    return SyscallResult::ok();
}

SyscallResult sys_irq_ack(u64 a0, u64, u64, u64, u64, u64)
{
    u32 irq = static_cast<u32>(a0);

    if (irq < 32 || irq >= gic::MAX_IRQS)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    task::Task *t = task::current();
    if (!t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    init_irq_states();

    IrqState *state = &irq_states[irq];
    SpinlockGuard guard(state->lock);

    if (state->owner_task_id != t->id)
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    state->enabled = true;
    gic::enable_irq(irq);

    return SyscallResult::ok();
}

SyscallResult sys_irq_unregister(u64 a0, u64, u64, u64, u64, u64)
{
    u32 irq = static_cast<u32>(a0);

    if (irq < 32 || irq >= gic::MAX_IRQS)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    task::Task *t = task::current();
    if (!t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    init_irq_states();

    IrqState *state = &irq_states[irq];
    SpinlockGuard guard(state->lock);

    if (state->owner_task_id != t->id)
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    gic::disable_irq(irq);
    gic::register_handler(irq, nullptr);

    state->owner_task_id = 0;
    state->owner_viper_id = 0;
    state->pending = false;
    state->enabled = false;

    sched::wait_wake_all(&state->waiters);

    return SyscallResult::ok();
}

SyscallResult sys_dma_alloc(u64 a0, u64 a1, u64, u64, u64, u64)
{
    u64 size = a0;
    u64 *phys_out = reinterpret_cast<u64 *>(a1);

    if (size == 0 || size > 16 * 1024 * 1024)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    if (phys_out && !validate_user_write(phys_out, sizeof(u64)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    viper::Viper *v = viper::current();
    if (!v)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    if (!has_device_cap(v, cap::CAP_DMA_ACCESS))
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    init_dma_allocations();

    u64 num_pages = (size + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;
    u64 phys_addr = pmm::alloc_pages(num_pages);
    if (phys_addr == 0)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as)
    {
        pmm::free_pages(phys_addr, num_pages);
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    u64 virt_addr = 0x200000000ULL;

    SpinlockGuard guard(dma_lock);
    u32 slot = MAX_DMA_ALLOCATIONS;
    for (u32 i = 0; i < MAX_DMA_ALLOCATIONS; i++)
    {
        if (!dma_allocations[i].in_use)
        {
            slot = i;
            break;
        }
        if (dma_allocations[i].virt_addr + dma_allocations[i].size > virt_addr)
        {
            virt_addr = pmm::page_align_up(dma_allocations[i].virt_addr + dma_allocations[i].size);
        }
    }

    if (slot == MAX_DMA_ALLOCATIONS)
    {
        pmm::free_pages(phys_addr, num_pages);
        return SyscallResult::err(error::VERR_NO_RESOURCE);
    }

    if (!as->map(virt_addr, phys_addr, num_pages * pmm::PAGE_SIZE, viper::prot::RW))
    {
        pmm::free_pages(phys_addr, num_pages);
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    dma_allocations[slot].phys_addr = phys_addr;
    dma_allocations[slot].virt_addr = virt_addr;
    dma_allocations[slot].size = num_pages * pmm::PAGE_SIZE;
    dma_allocations[slot].owner_viper_id = v->id;
    dma_allocations[slot].in_use = true;

    if (phys_out)
    {
        *phys_out = phys_addr;
    }

    return SyscallResult::ok(virt_addr);
}

SyscallResult sys_dma_free(u64 a0, u64, u64, u64, u64, u64)
{
    u64 virt_addr = a0;

    viper::Viper *v = viper::current();
    if (!v)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    init_dma_allocations();

    SpinlockGuard guard(dma_lock);

    u32 slot = MAX_DMA_ALLOCATIONS;
    for (u32 i = 0; i < MAX_DMA_ALLOCATIONS; i++)
    {
        if (dma_allocations[i].in_use && dma_allocations[i].virt_addr == virt_addr &&
            dma_allocations[i].owner_viper_id == v->id)
        {
            slot = i;
            break;
        }
    }

    if (slot == MAX_DMA_ALLOCATIONS)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    viper::AddressSpace *as = viper::get_address_space(v);
    if (as)
    {
        as->unmap(virt_addr, dma_allocations[slot].size);
    }

    u64 num_pages = dma_allocations[slot].size / pmm::PAGE_SIZE;
    pmm::free_pages(dma_allocations[slot].phys_addr, num_pages);

    dma_allocations[slot].in_use = false;

    return SyscallResult::ok();
}

SyscallResult sys_virt_to_phys(u64 a0, u64, u64, u64, u64, u64)
{
    u64 virt_addr = a0;

    viper::Viper *v = viper::current();
    if (!v)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    if (!has_device_cap(v, cap::CAP_DMA_ACCESS))
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    u64 phys_addr = as->translate(virt_addr);
    if (phys_addr == 0)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    return SyscallResult::ok(phys_addr);
}

SyscallResult sys_device_enum(u64 a0, u64 a1, u64, u64, u64, u64)
{
    struct DeviceEnumInfo
    {
        char name[32];
        u64 phys_addr;
        u64 size;
        u32 irq;
        u32 flags;
    };

    DeviceEnumInfo *devices = reinterpret_cast<DeviceEnumInfo *>(a0);
    u32 max_count = static_cast<u32>(a1);

    if (max_count > 0)
    {
        usize byte_size;
        if (__builtin_mul_overflow(static_cast<usize>(max_count), sizeof(DeviceEnumInfo), &byte_size))
        {
            return SyscallResult::err(error::VERR_INVALID_ARG);
        }
        if (!validate_user_write(devices, byte_size))
        {
            return SyscallResult::err(error::VERR_INVALID_ARG);
        }
    }

    if (!devices)
    {
        return SyscallResult::ok(KNOWN_DEVICE_COUNT);
    }

    u32 count = 0;
    for (u32 i = 0; i < KNOWN_DEVICE_COUNT && count < max_count; i++)
    {
        const char *src = known_devices[i].name;
        usize j = 0;
        while (j < 31 && src[j])
        {
            devices[count].name[j] = src[j];
            j++;
        }
        devices[count].name[j] = '\0';

        devices[count].phys_addr = known_devices[i].phys_base;
        devices[count].size = known_devices[i].size;
        devices[count].irq = known_devices[i].irq;
        devices[count].flags = 1;
        count++;
    }

    return SyscallResult::ok(count);
}

// =============================================================================
// Shared Memory Syscalls
// =============================================================================

SyscallResult sys_shm_create(u64 a0, u64, u64, u64, u64, u64)
{
    u64 size = a0;

    if (size == 0 || size > 64 * 1024 * 1024)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    viper::Viper *v = viper::current();
    if (!v || !v->cap_table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    kobj::SharedMemory *shm = kobj::SharedMemory::create(size);
    if (!shm)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as)
    {
        delete shm;
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    u64 virt_base = 0x7000000000ULL;
    u64 virt_addr = 0;
    u64 aligned_size = pmm::page_align_up(size);

    for (u64 try_addr = virt_base; try_addr < 0x8000000000ULL; try_addr += aligned_size)
    {
        if (as->translate(try_addr) == 0)
        {
            virt_addr = try_addr;
            break;
        }
    }

    if (virt_addr == 0)
    {
        delete shm;
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    if (!as->map(virt_addr, shm->phys_addr(), aligned_size, viper::prot::RW))
    {
        delete shm;
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    shm->set_creator_virt(virt_addr);

    cap::Handle handle = v->cap_table->insert(
        shm, cap::Kind::SharedMemory, cap::CAP_READ | cap::CAP_WRITE | cap::CAP_TRANSFER);
    if (handle == cap::HANDLE_INVALID)
    {
        as->unmap(virt_addr, aligned_size);
        delete shm;
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    if (!track_shm_mapping(v->id, virt_addr, aligned_size, shm))
    {
        v->cap_table->remove(handle);
        as->unmap(virt_addr, aligned_size);
        delete shm;
        return SyscallResult::err(error::VERR_NO_RESOURCE);
    }
    shm->ref();

    SyscallResult result;
    result.verr = 0;
    result.res0 = handle;
    result.res1 = virt_addr;
    result.res2 = shm->size();
    return result;
}

SyscallResult sys_shm_map(u64 a0, u64, u64, u64, u64, u64)
{
    cap::Handle handle = static_cast<cap::Handle>(a0);

    viper::Viper *v = viper::current();
    if (!v || !v->cap_table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    cap::Entry *entry = v->cap_table->get_checked(handle, cap::Kind::SharedMemory);
    if (!entry)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    if (!cap::has_rights(entry->rights, cap::CAP_READ))
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    kobj::SharedMemory *shm = static_cast<kobj::SharedMemory *>(entry->object);
    if (!shm)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    u64 virt_base = 0x7000000000ULL;
    u64 virt_addr = 0;
    u64 aligned_size = shm->size();

    for (u64 try_addr = virt_base; try_addr < 0x8000000000ULL; try_addr += aligned_size)
    {
        if (as->translate(try_addr) == 0)
        {
            virt_addr = try_addr;
            break;
        }
    }

    if (virt_addr == 0)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    u32 prot = viper::prot::READ;
    if (cap::has_rights(entry->rights, cap::CAP_WRITE))
    {
        prot |= viper::prot::WRITE;
    }

    if (!as->map(virt_addr, shm->phys_addr(), aligned_size, prot))
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    if (!track_shm_mapping(v->id, virt_addr, aligned_size, shm))
    {
        as->unmap(virt_addr, aligned_size);
        return SyscallResult::err(error::VERR_NO_RESOURCE);
    }

    shm->ref();

    SyscallResult result;
    result.verr = 0;
    result.res0 = virt_addr;
    result.res1 = shm->size();
    return result;
}

SyscallResult sys_shm_unmap(u64 a0, u64, u64, u64, u64, u64)
{
    u64 virt_addr = a0;

    viper::Viper *v = viper::current();
    if (!v)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    u64 size = 0;
    kobj::SharedMemory *shm = nullptr;
    if (!untrack_shm_mapping(v->id, virt_addr, &size, &shm) || size == 0 || !shm)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    as->unmap(virt_addr, size);
    kobj::release(shm);

    return SyscallResult::ok();
}

SyscallResult sys_shm_close(u64 a0, u64, u64, u64, u64, u64)
{
    cap::Handle handle = static_cast<cap::Handle>(a0);

    viper::Viper *v = viper::current();
    if (!v || !v->cap_table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    cap::Entry *entry = v->cap_table->get_checked(handle, cap::Kind::SharedMemory);
    if (!entry)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    kobj::SharedMemory *shm = static_cast<kobj::SharedMemory *>(entry->object);
    v->cap_table->remove(handle);
    kobj::release(shm);
    return SyscallResult::ok();
}

} // namespace syscall
