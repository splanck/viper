# ViperOS Microkernel Migration Plan

**Version:** 1.0
**Status:** Proposed
**Author:** Generated from codebase analysis
**Date:** 2024

---

## Executive Summary

This document provides a comprehensive plan to migrate ViperOS from its current monolithic architecture (~50k kernel lines) to a true microkernel architecture (~8k kernel lines). The migration moves device drivers, filesystem, and network stack to user-space servers while preserving the existing capability-based IPC infrastructure.

**Current State:** Monolithic kernel with all services in EL1
**Target State:** Microkernel with user-space servers communicating via IPC

**Estimated Effort:** 3-5 days based on current development velocity

---

## Table of Contents

1. [Current Architecture Analysis](#1-current-architecture-analysis)
2. [Target Microkernel Architecture](#2-target-microkernel-architecture)
3. [New Kernel Primitives Required](#3-new-kernel-primitives-required)
4. [Phase 1: Kernel Infrastructure](#phase-1-kernel-infrastructure)
5. [Phase 2: Device Driver Framework](#phase-2-device-driver-framework)
6. [Phase 3: Block Device Server](#phase-3-block-device-server)
7. [Phase 4: Filesystem Server](#phase-4-filesystem-server)
8. [Phase 5: Network Server](#phase-5-network-server)
9. [Phase 6: Cleanup and Optimization](#phase-6-cleanup-and-optimization)
10. [IPC Protocol Specifications](#ipc-protocol-specifications)
11. [Migration Checklist](#migration-checklist)

---

## 1. Current Architecture Analysis

### 1.1 Kernel Size Breakdown

| Subsystem | Lines | Location | Target |
|-----------|-------|----------|--------|
| Network Stack | 14,649 | `kernel/net/` | User-space |
| Drivers (VirtIO) | 5,268 | `kernel/drivers/virtio/` | User-space |
| Filesystem | 5,328 | `kernel/fs/` | User-space |
| Memory Management | 5,323 | `kernel/mm/` | **Keep in kernel** |
| Console | 3,544 | `kernel/console/` | User-space (optional) |
| Architecture | 3,039 | `kernel/arch/` | **Keep in kernel** |
| Syscall | 2,847 | `kernel/syscall/` | **Keep in kernel** |
| Scheduler | 2,870 | `kernel/sched/` | **Keep in kernel** |
| IPC | 2,507 | `kernel/ipc/` | **Keep in kernel** |
| Viper/Caps | 2,946 | `kernel/viper/`, `kernel/cap/` | **Keep in kernel** |
| **Total** | **~50,000** | | **~8,000 remain** |

### 1.2 Current Monolithic Flow

```
┌─────────────────────────────────────────────────────────────┐
│                     User Space (vinit)                       │
│                      syscall interface                       │
├─────────────────────────────────────────────────────────────┤
│                         KERNEL                               │
│  ┌─────────┬─────────┬─────────┬─────────┬─────────┐       │
│  │ TCP/IP  │ ViperFS │ VirtIO  │ VirtIO  │ VirtIO  │       │
│  │  Stack  │   +VFS  │   Net   │   Blk   │   GPU   │       │
│  │ 14.6k   │  5.3k   │  1.0k   │  0.8k   │  0.8k   │       │
│  └────┬────┴────┬────┴────┬────┴────┬────┴────┬────┘       │
│       └─────────┴─────────┴─────────┴─────────┘             │
│                    Direct function calls                     │
└─────────────────────────────────────────────────────────────┘
```

### 1.3 What Works Well (Keep These)

1. **IPC Channels** - Already support capability transfer
   - 64 channels, 256-byte messages, 4 handles/message
   - Non-blocking with `VERR_WOULD_BLOCK`
   - Per-task capability tables

2. **Address Spaces** - Per-process isolation ready
   - ASID-tagged page tables (256 ASIDs)
   - Full 4-level page table management
   - COW support for fork()

3. **Capability System** - Handle-based access control
   - Rights checking (READ, WRITE, DERIVE, TRANSFER)
   - Handle derivation with reduced rights
   - Revocation support

4. **Syscall Interface** - Clean table-driven dispatch
   - 50+ syscalls already defined
   - User pointer validation
   - Multi-value returns

---

## 2. Target Microkernel Architecture

### 2.1 Final Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     User Applications                        │
│                    (vinit, utilities)                        │
└──────────────────────────┬──────────────────────────────────┘
                           │ IPC (channels)
┌──────────────────────────┼──────────────────────────────────┐
│                    Service Layer                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │   FS Server │  │  Net Server │  │  Blk Server │         │
│  │  (viperfs)  │  │  (tcp/ip)   │  │ (virtio-blk)│         │
│  │   ~5.3k     │  │   ~14.6k    │  │   ~0.8k     │         │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘         │
│         │                │                │                  │
│         │    IPC         │    IPC         │   Device Access  │
│         └────────────────┴────────────────┘                  │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────┴──────────────────────────────────┐
│                    MICROKERNEL (~8k)                         │
│  ┌─────────┬─────────┬─────────┬─────────┬─────────┐       │
│  │Scheduler│   IPC   │  Memory │  Caps   │ DevMgr  │       │
│  │ + Tasks │Channels │   Mgmt  │ Tables  │  (new)  │       │
│  └─────────┴─────────┴─────────┴─────────┴─────────┘       │
│                    Exception Handling                        │
│                    GIC + Timer + MMU                         │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 Server Responsibilities

| Server | Responsibilities |
|--------|------------------|
| **Block Server** | VirtIO-blk driver, block I/O requests |
| **Net Server** | VirtIO-net driver, Ethernet, IP, TCP, UDP, DNS, TLS |
| **FS Server** | ViperFS, VFS, path resolution, file descriptors |
| **Console Server** | Serial I/O, graphics console (optional, can stay in kernel) |

### 2.3 IPC Flow Example (File Read)

```
Application                 FS Server               Block Server
    │                           │                        │
    │ FS_READ(fd, buf, len)     │                        │
    │ ────────────────────────> │                        │
    │                           │ BLK_READ(sector, n)    │
    │                           │ ─────────────────────> │
    │                           │                        │ [DMA from disk]
    │                           │ <───────────────────── │
    │                           │ BLK_READ_REPLY(data)   │
    │ <──────────────────────── │                        │
    │ FS_READ_REPLY(data)       │                        │
```

---

## 3. New Kernel Primitives Required

### 3.1 Device Memory Mapping

**Purpose:** Allow user-space drivers to access device MMIO regions.

```cpp
// New syscall: SYS_MAP_DEVICE (0x100)
// Maps physical device memory into calling process's address space
//
// Args:
//   a0: physical address (must be device region, not RAM)
//   a1: size (page-aligned)
//   a2: virtual address hint (0 = kernel chooses)
//
// Returns:
//   x0: VOK or error
//   x1: mapped virtual address
//
// Security: Only processes with CAP_DEVICE_ACCESS can call this

i64 sys_map_device(u64 phys_addr, u64 size, u64 virt_hint);
```

**Implementation:**
```cpp
// kernel/syscall/device.cpp
SyscallResult sys_map_device(u64 phys, u64 size, u64 hint, u64, u64, u64) {
    // 1. Check caller has CAP_DEVICE_ACCESS capability
    if (!current_has_cap(CAP_DEVICE_ACCESS))
        return SyscallResult::err(VERR_PERMISSION);

    // 2. Validate physical address is in device range (not RAM)
    if (!is_device_region(phys, size))
        return SyscallResult::err(VERR_INVALID_ARG);

    // 3. Align size to page boundary
    size = page_align_up(size);

    // 4. Find free virtual address range in user space
    u64 virt = hint ? hint : find_free_vma(size);
    if (!virt)
        return SyscallResult::err(VERR_OUT_OF_MEMORY);

    // 5. Map with device memory attributes (non-cacheable, non-bufferable)
    AddressSpace *as = viper::current()->address_space();
    if (!as->map_device(virt, phys, size))
        return SyscallResult::err(VERR_OUT_OF_MEMORY);

    // 6. Create VMA for tracking
    viper::current()->vma_list.add(virt, virt + size,
        prot::READ | prot::WRITE | prot::DEVICE, VmaType::DEVICE);

    return SyscallResult::ok(virt);
}
```

**Device Region Table:**
```cpp
// Known device MMIO regions for QEMU virt machine
struct DeviceRegion {
    u64 start;
    u64 end;
    const char *name;
};

constexpr DeviceRegion device_regions[] = {
    {0x08000000, 0x08020000, "GIC"},           // Reserved for kernel
    {0x09000000, 0x09001000, "UART"},          // Can grant to console server
    {0x0a000000, 0x0a004000, "VirtIO MMIO"},   // VirtIO devices
    {0x10000000, 0x3eff0000, "PCIe MMIO"},     // PCI (if used)
};
```

### 3.2 IRQ Wait/Notification

**Purpose:** Allow user-space drivers to wait for and handle device interrupts.

```cpp
// New syscall: SYS_IRQ_WAIT (0x101)
// Blocks until specified IRQ fires, or timeout
//
// Args:
//   a0: IRQ number
//   a1: timeout_ms (-1 = infinite, 0 = poll)
//
// Returns:
//   x0: VOK (IRQ fired), VERR_TIMEOUT, or error
//
// Security: Must own IRQ via SYS_IRQ_REGISTER

i64 sys_irq_wait(u32 irq, i64 timeout_ms);
```

```cpp
// New syscall: SYS_IRQ_REGISTER (0x102)
// Register to receive a specific IRQ
//
// Args:
//   a0: IRQ number
//
// Returns:
//   x0: VOK or error (VERR_BUSY if already owned)
//
// Security: Only processes with CAP_DEVICE_ACCESS

i64 sys_irq_register(u32 irq);
```

```cpp
// New syscall: SYS_IRQ_ACK (0x103)
// Acknowledge IRQ after handling (required before next wait)
//
// Args:
//   a0: IRQ number
//
// Returns:
//   x0: VOK or error

i64 sys_irq_ack(u32 irq);
```

**Implementation:**
```cpp
// kernel/syscall/irq.cpp

// Per-IRQ ownership and wait state
struct IrqState {
    u32 owner_task_id;          // 0 = unowned
    sched::WaitQueue waiters;   // Tasks waiting for this IRQ
    bool pending;               // IRQ fired but not yet delivered
    Spinlock lock;
};

static IrqState irq_states[256];

SyscallResult sys_irq_register(u64 irq, u64, u64, u64, u64, u64) {
    if (irq >= 256 || irq < 32)  // SPIs only (32-255), not SGIs/PPIs
        return SyscallResult::err(VERR_INVALID_ARG);

    if (!current_has_cap(CAP_DEVICE_ACCESS))
        return SyscallResult::err(VERR_PERMISSION);

    SpinlockGuard guard(irq_states[irq].lock);

    if (irq_states[irq].owner_task_id != 0)
        return SyscallResult::err(VERR_BUSY);

    irq_states[irq].owner_task_id = task::current()->id;
    irq_states[irq].pending = false;

    // Enable IRQ in GIC
    gic::enable_irq(irq);
    gic::register_handler(irq, user_irq_handler);

    return SyscallResult::ok();
}

SyscallResult sys_irq_wait(u64 irq, u64 timeout_ms, u64, u64, u64, u64) {
    if (irq >= 256)
        return SyscallResult::err(VERR_INVALID_ARG);

    SpinlockGuard guard(irq_states[irq].lock);

    // Must own this IRQ
    if (irq_states[irq].owner_task_id != task::current()->id)
        return SyscallResult::err(VERR_PERMISSION);

    // Check if already pending
    if (irq_states[irq].pending) {
        irq_states[irq].pending = false;
        return SyscallResult::ok();
    }

    // Non-blocking poll
    if (timeout_ms == 0)
        return SyscallResult::err(VERR_WOULD_BLOCK);

    // Block until IRQ or timeout
    sched::wait_enqueue(&irq_states[irq].waiters, task::current());
    guard.release();

    // TODO: Add timeout support
    scheduler::schedule();

    return SyscallResult::ok();
}

// Called from GIC interrupt handler
void user_irq_handler(u32 irq) {
    SpinlockGuard guard(irq_states[irq].lock);
    irq_states[irq].pending = true;
    sched::wait_wake_one(&irq_states[irq].waiters);
    // Mask IRQ until user acknowledges
    gic::disable_irq(irq);
}

SyscallResult sys_irq_ack(u64 irq, u64, u64, u64, u64, u64) {
    if (irq >= 256)
        return SyscallResult::err(VERR_INVALID_ARG);

    SpinlockGuard guard(irq_states[irq].lock);

    if (irq_states[irq].owner_task_id != task::current()->id)
        return SyscallResult::err(VERR_PERMISSION);

    // Re-enable IRQ
    gic::enable_irq(irq);

    return SyscallResult::ok();
}
```

### 3.3 DMA Buffer Allocation

**Purpose:** Allocate physically contiguous, DMA-accessible memory for device buffers.

```cpp
// New syscall: SYS_DMA_ALLOC (0x104)
// Allocates physically contiguous memory for DMA
//
// Args:
//   a0: size (bytes, will be page-aligned)
//   a1: flags (0 = normal, 1 = 32-bit addressable)
//
// Returns:
//   x0: VOK or error
//   x1: virtual address
//   x2: physical address (for programming device)
//
// Security: Only processes with CAP_DEVICE_ACCESS

i64 sys_dma_alloc(u64 size, u64 flags);
```

```cpp
// New syscall: SYS_DMA_FREE (0x105)
// Frees DMA buffer
//
// Args:
//   a0: virtual address (from sys_dma_alloc)
//
// Returns:
//   x0: VOK or error

i64 sys_dma_free(u64 virt_addr);
```

**Implementation:**
```cpp
SyscallResult sys_dma_alloc(u64 size, u64 flags, u64, u64, u64, u64) {
    if (!current_has_cap(CAP_DEVICE_ACCESS))
        return SyscallResult::err(VERR_PERMISSION);

    size = page_align_up(size);
    u64 pages = size / PAGE_SIZE;

    // Allocate contiguous physical pages
    u64 phys = pmm::alloc_pages(pages);
    if (!phys)
        return SyscallResult::err(VERR_OUT_OF_MEMORY);

    // Map into user address space
    u64 virt = find_free_vma(size);
    if (!virt) {
        pmm::free_pages(phys, pages);
        return SyscallResult::err(VERR_OUT_OF_MEMORY);
    }

    AddressSpace *as = viper::current()->address_space();
    // Map as uncached for DMA coherence
    if (!as->map_dma(virt, phys, size)) {
        pmm::free_pages(phys, pages);
        return SyscallResult::err(VERR_OUT_OF_MEMORY);
    }

    // Track allocation for cleanup
    viper::current()->vma_list.add_dma(virt, virt + size, phys);

    SyscallResult result;
    result.verr = 0;
    result.res0 = virt;
    result.res1 = phys;
    return result;
}
```

### 3.4 Physical Address Query

**Purpose:** Allow user-space to get physical address of mapped memory (for device programming).

```cpp
// New syscall: SYS_VIRT_TO_PHYS (0x106)
// Translates virtual address to physical (for DMA programming)
//
// Args:
//   a0: virtual address
//
// Returns:
//   x0: VOK or error (VERR_NOT_FOUND if not mapped)
//   x1: physical address
//
// Security: Only for addresses in caller's address space

i64 sys_virt_to_phys(u64 virt_addr);
```

### 3.5 Capability Extensions

Add new capability rights for device access:

```cpp
// cap/rights.hpp additions
namespace cap {
    // Existing rights...

    // New device-related rights
    constexpr u32 CAP_DEVICE_ACCESS = (1 << 10);  // Can map device memory
    constexpr u32 CAP_IRQ_ACCESS = (1 << 11);     // Can register IRQs
    constexpr u32 CAP_DMA_ACCESS = (1 << 12);     // Can allocate DMA buffers
}
```

### 3.6 New Syscall Number Assignments

```cpp
// syscall_nums.hpp additions

// Device Management (0x100-0x10F)
SYS_MAP_DEVICE      = 0x100,  // Map device MMIO into user space
SYS_IRQ_REGISTER    = 0x101,  // Register for IRQ delivery
SYS_IRQ_WAIT        = 0x102,  // Wait for IRQ
SYS_IRQ_ACK         = 0x103,  // Acknowledge IRQ
SYS_DMA_ALLOC       = 0x104,  // Allocate DMA buffer
SYS_DMA_FREE        = 0x105,  // Free DMA buffer
SYS_VIRT_TO_PHYS    = 0x106,  // Translate VA to PA
SYS_DEVICE_ENUM     = 0x107,  // Enumerate devices
```

---

## Phase 1: Kernel Infrastructure

**Goal:** Add device access primitives without changing existing functionality.

**Duration:** ~4 hours

### Tasks

1. **Add new capability rights** (`kernel/cap/rights.hpp`)
   - Add `CAP_DEVICE_ACCESS`, `CAP_IRQ_ACCESS`, `CAP_DMA_ACCESS`

2. **Implement SYS_MAP_DEVICE** (`kernel/syscall/device.cpp`)
   - Device region validation
   - Address space mapping with ATTR_DEVICE
   - VMA tracking

3. **Implement IRQ syscalls** (`kernel/syscall/irq.cpp`)
   - IRQ ownership table
   - Wait queue per IRQ
   - Handler registration

4. **Implement DMA syscalls** (`kernel/syscall/dma.cpp`)
   - Contiguous allocation
   - DMA mapping attributes
   - Physical address return

5. **Add SYS_VIRT_TO_PHYS** (`kernel/syscall/mem.cpp`)
   - Page table walk
   - Security checks

6. **Update syscall table** (`kernel/syscall/table.cpp`)
   - Add new handlers
   - Update syscall_nums.hpp

### Validation

```bash
# Test device mapping from user space
$ test_device_map 0x0a000000 0x200  # Should map VirtIO device
$ test_irq_wait 48                   # Should wait for VirtIO IRQ
```

---

## Phase 2: Device Driver Framework

**Goal:** Create shared infrastructure for user-space drivers.

**Duration:** ~3 hours

### 2.1 User-Space VirtIO Base Library

Create `user/libvirtio/` with common VirtIO infrastructure:

```cpp
// user/libvirtio/virtio.hpp

#pragma once
#include <stdint.h>

namespace virtio {

// VirtIO MMIO register offsets (same as kernel)
namespace reg {
    constexpr u32 MAGIC = 0x000;
    constexpr u32 VERSION = 0x004;
    constexpr u32 DEVICE_ID = 0x008;
    constexpr u32 VENDOR_ID = 0x00C;
    constexpr u32 DEVICE_FEATURES = 0x010;
    constexpr u32 DRIVER_FEATURES = 0x020;
    constexpr u32 QUEUE_SEL = 0x030;
    constexpr u32 QUEUE_NUM_MAX = 0x034;
    constexpr u32 QUEUE_NUM = 0x038;
    constexpr u32 QUEUE_READY = 0x044;
    constexpr u32 QUEUE_NOTIFY = 0x050;
    constexpr u32 INTERRUPT_STATUS = 0x060;
    constexpr u32 INTERRUPT_ACK = 0x064;
    constexpr u32 STATUS = 0x070;
    constexpr u32 QUEUE_DESC_LOW = 0x080;
    constexpr u32 QUEUE_DESC_HIGH = 0x084;
    constexpr u32 QUEUE_AVAIL_LOW = 0x090;
    constexpr u32 QUEUE_AVAIL_HIGH = 0x094;
    constexpr u32 QUEUE_USED_LOW = 0x0A0;
    constexpr u32 QUEUE_USED_HIGH = 0x0A4;
    constexpr u32 CONFIG = 0x100;
}

// Status bits
namespace status {
    constexpr u32 ACKNOWLEDGE = 1;
    constexpr u32 DRIVER = 2;
    constexpr u32 DRIVER_OK = 4;
    constexpr u32 FEATURES_OK = 8;
    constexpr u32 FAILED = 128;
}

// Virtqueue structures (same as kernel)
struct VringDesc {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
};

struct VringAvail {
    u16 flags;
    u16 idx;
    u16 ring[];
};

struct VringUsedElem {
    u32 id;
    u32 len;
};

struct VringUsed {
    u16 flags;
    u16 idx;
    VringUsedElem ring[];
};

// User-space device class
class Device {
public:
    bool init(u64 mmio_phys, u32 irq_num);
    void reset();
    bool negotiate_features(u64 required, u64 supported);
    void set_status(u32 status);
    u32 get_status();

    // MMIO access
    u32 read32(u32 offset);
    void write32(u32 offset, u32 value);
    u8 read_config8(u32 offset);

protected:
    volatile u32 *mmio_;
    u64 mmio_phys_;
    u32 irq_;
};

// User-space virtqueue
class Virtqueue {
public:
    bool init(Device *dev, u32 queue_idx, u32 size);
    void destroy();

    i32 alloc_desc();
    void free_desc(u32 idx);

    void submit(u32 head);
    void kick();
    i32 poll_used();
    u32 last_used_len();

    VringDesc *desc(u32 idx) { return &desc_[idx]; }

private:
    Device *dev_;
    VringDesc *desc_;
    VringAvail *avail_;
    VringUsed *used_;
    u32 size_;
    u32 queue_idx_;
    u32 free_head_;
    u32 num_free_;
    u16 last_used_idx_;
    u32 last_used_len_;

    // DMA buffers
    u64 desc_phys_;
    u64 avail_phys_;
    u64 used_phys_;
};

} // namespace virtio
```

### 2.2 Virtqueue Implementation

```cpp
// user/libvirtio/virtqueue.cpp

bool Virtqueue::init(Device *dev, u32 queue_idx, u32 size) {
    dev_ = dev;
    queue_idx_ = queue_idx;
    size_ = size;

    // Calculate memory requirements
    usize desc_size = sizeof(VringDesc) * size;
    usize avail_size = sizeof(VringAvail) + sizeof(u16) * size;
    usize used_size = sizeof(VringUsed) + sizeof(VringUsedElem) * size;

    // Allocate DMA memory for descriptor table
    u64 desc_virt, desc_phys;
    if (sys_dma_alloc(desc_size, 0, &desc_virt, &desc_phys) < 0)
        return false;
    desc_ = reinterpret_cast<VringDesc *>(desc_virt);
    desc_phys_ = desc_phys;

    // Allocate DMA memory for available ring
    u64 avail_virt, avail_phys;
    if (sys_dma_alloc(avail_size, 0, &avail_virt, &avail_phys) < 0)
        return false;
    avail_ = reinterpret_cast<VringAvail *>(avail_virt);
    avail_phys_ = avail_phys;

    // Allocate DMA memory for used ring
    u64 used_virt, used_phys;
    if (sys_dma_alloc(used_size, 0, &used_virt, &used_phys) < 0)
        return false;
    used_ = reinterpret_cast<VringUsed *>(used_virt);
    used_phys_ = used_phys;

    // Initialize free list
    for (u32 i = 0; i < size_ - 1; i++) {
        desc_[i].next = i + 1;
        desc_[i].flags = VRING_DESC_F_NEXT;
    }
    desc_[size_ - 1].next = 0xFFFF;
    free_head_ = 0;
    num_free_ = size_;
    last_used_idx_ = 0;

    // Configure virtqueue in device
    dev_->write32(reg::QUEUE_SEL, queue_idx_);
    dev_->write32(reg::QUEUE_NUM, size_);
    dev_->write32(reg::QUEUE_DESC_LOW, desc_phys_ & 0xFFFFFFFF);
    dev_->write32(reg::QUEUE_DESC_HIGH, desc_phys_ >> 32);
    dev_->write32(reg::QUEUE_AVAIL_LOW, avail_phys_ & 0xFFFFFFFF);
    dev_->write32(reg::QUEUE_AVAIL_HIGH, avail_phys_ >> 32);
    dev_->write32(reg::QUEUE_USED_LOW, used_phys_ & 0xFFFFFFFF);
    dev_->write32(reg::QUEUE_USED_HIGH, used_phys_ >> 32);
    dev_->write32(reg::QUEUE_READY, 1);

    return true;
}

void Virtqueue::submit(u32 head) {
    avail_->ring[avail_->idx % size_] = head;
    __asm__ volatile("dmb sy" ::: "memory");
    avail_->idx++;
}

void Virtqueue::kick() {
    __asm__ volatile("dmb sy" ::: "memory");
    dev_->write32(reg::QUEUE_NOTIFY, queue_idx_);
}

i32 Virtqueue::poll_used() {
    __asm__ volatile("dmb sy" ::: "memory");
    if (last_used_idx_ == used_->idx)
        return -1;

    u32 ring_idx = last_used_idx_ % size_;
    u32 head = used_->ring[ring_idx].id;
    last_used_len_ = used_->ring[ring_idx].len;
    last_used_idx_++;
    return head;
}
```

---

## Phase 3: Block Device Server

**Goal:** Move VirtIO-blk driver to user-space server.

**Duration:** ~3 hours

### 3.1 Block Server Protocol

```cpp
// Protocol messages (256 bytes max per IPC message)

// Request types
enum BlkMsgType : u32 {
    BLK_READ = 1,
    BLK_WRITE = 2,
    BLK_FLUSH = 3,
    BLK_INFO = 4,

    BLK_READ_REPLY = 0x81,
    BLK_WRITE_REPLY = 0x82,
    BLK_FLUSH_REPLY = 0x83,
    BLK_INFO_REPLY = 0x84,
};

// BLK_READ request (client → server)
struct BlkReadReq {
    u32 type;           // BLK_READ
    u32 request_id;     // For matching replies
    u64 sector;         // Starting sector
    u32 count;          // Number of sectors
    u32 _pad;
};

// BLK_READ reply (server → client)
// Data is transferred via shared memory handle
struct BlkReadReply {
    u32 type;           // BLK_READ_REPLY
    u32 request_id;
    i32 status;         // 0 = success, negative = error
    u32 bytes_read;
    // handle[0] = shared memory with data (if status == 0)
};

// BLK_WRITE request (client → server)
// Data passed via shared memory handle
struct BlkWriteReq {
    u32 type;           // BLK_WRITE
    u32 request_id;
    u64 sector;
    u32 count;
    u32 _pad;
    // handle[0] = shared memory with data to write
};

// BLK_INFO reply
struct BlkInfoReply {
    u32 type;           // BLK_INFO_REPLY
    u32 request_id;
    u64 total_sectors;
    u32 sector_size;    // Usually 512
    u32 max_request;    // Max sectors per request
};
```

### 3.2 Block Server Implementation

```cpp
// user/servers/blkd/main.cpp

#include <libvirtio/virtio.hpp>
#include <libvirtio/blk.hpp>
#include <sys/ipc.h>

class BlkServer {
public:
    bool init() {
        // 1. Find VirtIO-blk device
        u64 mmio_phys = 0;
        u32 irq = 0;
        if (!find_virtio_device(VIRTIO_BLK, &mmio_phys, &irq))
            return false;

        // 2. Map device MMIO
        u64 mmio_virt;
        if (sys_map_device(mmio_phys, 0x200, 0, &mmio_virt) < 0)
            return false;

        // 3. Register for IRQ
        if (sys_irq_register(irq) < 0)
            return false;

        // 4. Initialize device
        device_.init(mmio_virt, mmio_phys, irq);
        if (!device_.setup())
            return false;

        // 5. Create service channel
        if (channel_create(&service_channel_) < 0)
            return false;

        // 6. Register with name service (assign)
        sys_assign_set("BLKD:", service_channel_.recv_handle);

        return true;
    }

    void run() {
        while (true) {
            // Wait for request or IRQ
            PollEvent events[2];
            events[0].handle = service_channel_.recv_handle;
            events[0].events = POLL_READ;
            events[1].handle = HANDLE_IRQ(irq_);
            events[1].events = POLL_READ;

            poll_wait(events, 2, -1);

            if (events[0].triggered & POLL_READ) {
                handle_request();
            }

            if (events[1].triggered & POLL_READ) {
                device_.handle_interrupt();
                sys_irq_ack(irq_);
            }
        }
    }

private:
    void handle_request() {
        u8 buf[256];
        cap::Handle handles[4];
        u32 handle_count = 4;

        i64 len = channel_recv(service_channel_.recv_handle,
                               buf, sizeof(buf), handles, &handle_count);
        if (len < 0) return;

        u32 type = *reinterpret_cast<u32 *>(buf);

        switch (type) {
            case BLK_READ:
                handle_read(reinterpret_cast<BlkReadReq *>(buf), handles, handle_count);
                break;
            case BLK_WRITE:
                handle_write(reinterpret_cast<BlkWriteReq *>(buf), handles, handle_count);
                break;
            case BLK_FLUSH:
                handle_flush(reinterpret_cast<BlkFlushReq *>(buf));
                break;
            case BLK_INFO:
                handle_info(reinterpret_cast<BlkInfoReq *>(buf));
                break;
        }
    }

    void handle_read(BlkReadReq *req, cap::Handle *handles, u32 handle_count) {
        // Allocate buffer
        usize size = req->count * 512;
        u64 buf_virt, buf_phys;
        sys_dma_alloc(size, 0, &buf_virt, &buf_phys);

        // Perform read via VirtIO
        i32 status = device_.read_sectors(req->sector, req->count,
                                          reinterpret_cast<void *>(buf_virt));

        // Create shared memory handle for data
        cap::Handle data_handle = create_shared_memory(buf_virt, size);

        // Send reply
        BlkReadReply reply;
        reply.type = BLK_READ_REPLY;
        reply.request_id = req->request_id;
        reply.status = status;
        reply.bytes_read = (status == 0) ? size : 0;

        channel_send(service_channel_.send_handle,
                     &reply, sizeof(reply), &data_handle, 1);
    }

    virtio::BlkDevice device_;
    ChannelPair service_channel_;
    u32 irq_;
};

int main() {
    BlkServer server;
    if (!server.init()) {
        sys_debug_print("blkd: init failed\n");
        return 1;
    }

    sys_debug_print("blkd: running\n");
    server.run();
    return 0;
}
```

### 3.3 Kernel Block Shim

During transition, provide kernel-side shim that forwards to user-space server:

```cpp
// kernel/fs/blk_client.cpp
// Temporary shim: kernel code calls this, which IPCs to blkd

namespace blk {

static cap::Handle blkd_channel = HANDLE_INVALID;

bool init_client() {
    // Get blkd service channel
    blkd_channel = sys_assign_resolve("BLKD:");
    return blkd_channel != HANDLE_INVALID;
}

i32 read_sectors(u64 sector, u32 count, void *buf) {
    BlkReadReq req;
    req.type = BLK_READ;
    req.request_id = next_request_id++;
    req.sector = sector;
    req.count = count;

    channel::send(blkd_channel, &req, sizeof(req), nullptr, 0);

    // Wait for reply
    BlkReadReply reply;
    cap::Handle data_handle;
    u32 handle_count = 1;
    channel::recv(blkd_channel, &reply, sizeof(reply), &data_handle, &handle_count);

    if (reply.status != 0)
        return reply.status;

    // Copy data from shared memory
    void *shared = cap_to_ptr(data_handle);
    memcpy(buf, shared, reply.bytes_read);
    cap_close(data_handle);

    return 0;
}

} // namespace blk
```

---

## Phase 4: Filesystem Server

**Goal:** Move ViperFS and VFS to user-space server.

**Duration:** ~4 hours

### 4.1 Filesystem Server Protocol

```cpp
// FS protocol messages

enum FsMsgType : u32 {
    // File operations
    FS_OPEN = 1,
    FS_CLOSE = 2,
    FS_READ = 3,
    FS_WRITE = 4,
    FS_SEEK = 5,
    FS_STAT = 6,
    FS_FSTAT = 7,

    // Directory operations
    FS_READDIR = 10,
    FS_MKDIR = 11,
    FS_RMDIR = 12,
    FS_UNLINK = 13,
    FS_RENAME = 14,

    // Replies
    FS_OPEN_REPLY = 0x81,
    FS_READ_REPLY = 0x83,
    // ... etc
};

// FS_OPEN request
struct FsOpenReq {
    u32 type;           // FS_OPEN
    u32 request_id;
    u32 flags;          // O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, etc.
    u16 path_len;
    char path[242];     // Path within message
};

// FS_OPEN reply
struct FsOpenReply {
    u32 type;           // FS_OPEN_REPLY
    u32 request_id;
    i32 status;
    u32 file_id;        // Server-side file ID (not kernel FD)
    // handle[0] = file capability for direct access
};

// FS_READ request
struct FsReadReq {
    u32 type;           // FS_READ
    u32 request_id;
    u32 file_id;
    u32 count;
    u64 offset;         // -1 = use current position
};

// FS_READ reply
struct FsReadReply {
    u32 type;           // FS_READ_REPLY
    u32 request_id;
    i32 status;
    u32 bytes_read;
    // handle[0] = shared memory with data (for large reads)
    // OR data inline for small reads:
    u8 data[240];       // Inline data for reads < 240 bytes
};
```

### 4.2 Filesystem Server Structure

```cpp
// user/servers/fsd/main.cpp

class FsServer {
public:
    bool init() {
        // 1. Get block device service
        blk_channel_ = sys_assign_resolve("BLKD:");
        if (blk_channel_ == HANDLE_INVALID)
            return false;

        // 2. Mount ViperFS
        if (!viperfs_.mount(&blk_client_))
            return false;

        // 3. Create service channel
        if (channel_create(&service_channel_) < 0)
            return false;

        // 4. Register with name service
        sys_assign_set("FSD:", service_channel_.recv_handle);

        return true;
    }

    void run() {
        while (true) {
            u8 buf[256];
            cap::Handle handles[4];
            u32 handle_count = 4;
            cap::Handle reply_channel;

            // Receive includes reply channel as handle[0]
            i64 len = channel_recv(service_channel_.recv_handle,
                                   buf, sizeof(buf), handles, &handle_count);
            if (len < 0) continue;

            reply_channel = handles[0];

            u32 type = *reinterpret_cast<u32 *>(buf);
            dispatch(type, buf, len, reply_channel, &handles[1], handle_count - 1);
        }
    }

private:
    void dispatch(u32 type, void *msg, usize len,
                  cap::Handle reply, cap::Handle *handles, u32 handle_count) {
        switch (type) {
            case FS_OPEN:
                handle_open(static_cast<FsOpenReq *>(msg), reply);
                break;
            case FS_READ:
                handle_read(static_cast<FsReadReq *>(msg), reply);
                break;
            case FS_WRITE:
                handle_write(static_cast<FsWriteReq *>(msg), reply, handles, handle_count);
                break;
            // ... other operations
        }
    }

    void handle_open(FsOpenReq *req, cap::Handle reply) {
        // Resolve path
        char path[256];
        memcpy(path, req->path, req->path_len);
        path[req->path_len] = '\0';

        // Open via ViperFS
        i32 status = 0;
        u64 inode = viperfs_.lookup_path(path);
        if (inode == 0) {
            if (req->flags & O_CREAT) {
                inode = viperfs_.create_file(path);
            } else {
                status = -ENOENT;
            }
        }

        // Allocate file descriptor
        u32 file_id = INVALID_FILE_ID;
        if (status == 0) {
            file_id = alloc_file(inode, req->flags);
        }

        // Send reply
        FsOpenReply reply_msg;
        reply_msg.type = FS_OPEN_REPLY;
        reply_msg.request_id = req->request_id;
        reply_msg.status = status;
        reply_msg.file_id = file_id;

        channel_send(reply, &reply_msg, sizeof(reply_msg), nullptr, 0);
    }

    void handle_read(FsReadReq *req, cap::Handle reply) {
        FileEntry *file = get_file(req->file_id);
        if (!file) {
            send_error_reply(reply, req->request_id, -EBADF);
            return;
        }

        // Determine offset
        u64 offset = (req->offset == (u64)-1) ? file->offset : req->offset;

        // Read data
        u8 *buf;
        cap::Handle data_handle = HANDLE_INVALID;
        bool inline_data = (req->count <= 240);

        FsReadReply reply_msg;
        reply_msg.type = FS_READ_REPLY;
        reply_msg.request_id = req->request_id;

        if (inline_data) {
            buf = reply_msg.data;
        } else {
            // Allocate shared memory for large read
            u64 virt, phys;
            sys_dma_alloc(req->count, 0, &virt, &phys);
            buf = reinterpret_cast<u8 *>(virt);
            data_handle = create_shared_memory(virt, req->count);
        }

        i64 bytes = viperfs_.read_data(file->inode, offset, buf, req->count);

        if (bytes >= 0) {
            reply_msg.status = 0;
            reply_msg.bytes_read = bytes;
            file->offset = offset + bytes;
        } else {
            reply_msg.status = bytes;
            reply_msg.bytes_read = 0;
        }

        if (inline_data) {
            channel_send(reply, &reply_msg, sizeof(reply_msg), nullptr, 0);
        } else {
            channel_send(reply, &reply_msg, sizeof(reply_msg) - 240,
                         &data_handle, 1);
        }
    }

    ViperFS viperfs_;
    BlkClient blk_client_;
    cap::Handle blk_channel_;
    ChannelPair service_channel_;

    // Per-client file tables
    struct FileEntry {
        u64 inode;
        u64 offset;
        u32 flags;
        bool in_use;
    };
    FileEntry files_[256];
};
```

### 4.3 Kernel VFS Shim

```cpp
// kernel/fs/vfs_client.cpp
// Forwards VFS syscalls to fsd server

namespace vfs {

static cap::Handle fsd_channel = HANDLE_INVALID;

i32 open(const char *path, u32 flags) {
    FsOpenReq req;
    req.type = FS_OPEN;
    req.request_id = next_id++;
    req.flags = flags;
    req.path_len = strlen(path);
    memcpy(req.path, path, req.path_len);

    // Create reply channel
    ChannelPair reply_pair;
    channel_create(&reply_pair);

    // Send request with reply channel
    channel_send(fsd_channel, &req, sizeof(req), &reply_pair.recv_handle, 1);

    // Wait for reply
    FsOpenReply reply;
    channel_recv(reply_pair.recv_handle, &reply, sizeof(reply), nullptr, nullptr);
    channel_close(reply_pair.recv_handle);
    channel_close(reply_pair.send_handle);

    if (reply.status != 0)
        return reply.status;

    // Allocate kernel FD mapping to server file_id
    return alloc_fd_mapping(reply.file_id);
}

} // namespace vfs
```

---

## Phase 5: Network Server

**Goal:** Move TCP/IP stack and VirtIO-net driver to user-space server.

**Duration:** ~5 hours

### 5.1 Network Server Protocol

```cpp
// Network protocol messages

enum NetMsgType : u32 {
    // Socket operations
    NET_SOCKET_CREATE = 1,
    NET_SOCKET_CONNECT = 2,
    NET_SOCKET_BIND = 3,
    NET_SOCKET_LISTEN = 4,
    NET_SOCKET_ACCEPT = 5,
    NET_SOCKET_SEND = 6,
    NET_SOCKET_RECV = 7,
    NET_SOCKET_CLOSE = 8,

    // DNS
    NET_DNS_RESOLVE = 20,

    // TLS
    NET_TLS_CREATE = 30,
    NET_TLS_HANDSHAKE = 31,
    NET_TLS_SEND = 32,
    NET_TLS_RECV = 33,
    NET_TLS_CLOSE = 34,

    // Diagnostics
    NET_PING = 40,
    NET_STATS = 41,

    // Replies (type | 0x80)
};

// NET_SOCKET_CREATE request
struct NetSocketCreateReq {
    u32 type;           // NET_SOCKET_CREATE
    u32 request_id;
    u32 domain;         // AF_INET = 2
    u32 sock_type;      // SOCK_STREAM = 1, SOCK_DGRAM = 2
    u32 protocol;       // 0 = default
};

// NET_SOCKET_CONNECT request
struct NetSocketConnectReq {
    u32 type;           // NET_SOCKET_CONNECT
    u32 request_id;
    u32 socket_id;
    u32 ip;             // IPv4 in network byte order
    u16 port;           // Port in network byte order
    u16 _pad;
};

// NET_SOCKET_SEND request
struct NetSocketSendReq {
    u32 type;           // NET_SOCKET_SEND
    u32 request_id;
    u32 socket_id;
    u32 len;
    // Data follows inline (up to 240 bytes) or via handle
};

// NET_DNS_RESOLVE request
struct NetDnsResolveReq {
    u32 type;           // NET_DNS_RESOLVE
    u32 request_id;
    u16 hostname_len;
    char hostname[250];
};
```

### 5.2 Network Server Structure

```cpp
// user/servers/netd/main.cpp

class NetServer {
public:
    bool init() {
        // 1. Find VirtIO-net device
        u64 mmio_phys = 0;
        u32 irq = 0;
        if (!find_virtio_device(VIRTIO_NET, &mmio_phys, &irq))
            return false;

        // 2. Map device and register IRQ
        u64 mmio_virt;
        sys_map_device(mmio_phys, 0x200, 0, &mmio_virt);
        sys_irq_register(irq);
        irq_ = irq;

        // 3. Initialize VirtIO-net device
        if (!net_device_.init(mmio_virt, mmio_phys, irq))
            return false;

        // 4. Initialize network stack layers
        eth_.init(&net_device_);
        arp_.init(&eth_);
        ip_.init(&eth_, &arp_);
        tcp_.init(&ip_);
        udp_.init(&ip_);
        dns_.init(&udp_);

        // 5. Configure network interface
        // TODO: DHCP or static config
        ip_.set_address(Ipv4Addr(10, 0, 2, 15));
        ip_.set_netmask(Ipv4Addr(255, 255, 255, 0));
        ip_.set_gateway(Ipv4Addr(10, 0, 2, 2));
        dns_.set_server(Ipv4Addr(10, 0, 2, 3));

        // 6. Create service channel
        channel_create(&service_channel_);
        sys_assign_set("NETD:", service_channel_.recv_handle);

        return true;
    }

    void run() {
        while (true) {
            PollEvent events[2];
            events[0].handle = service_channel_.recv_handle;
            events[0].events = POLL_READ;
            events[1].handle = HANDLE_IRQ(irq_);
            events[1].events = POLL_READ;

            i32 ready = poll_wait(events, 2, 10);  // 10ms timeout for timers

            // Handle network RX
            if (events[1].triggered & POLL_READ) {
                net_device_.poll_rx();
                process_rx_packets();
                sys_irq_ack(irq_);
            }

            // Handle client requests
            if (events[0].triggered & POLL_READ) {
                handle_request();
            }

            // Process TCP timers
            tcp_.check_retransmit();
        }
    }

private:
    void process_rx_packets() {
        while (true) {
            u8 buf[2048];
            i32 len = net_device_.receive(buf, sizeof(buf));
            if (len <= 0) break;

            eth_.rx_frame(buf, len);
        }
    }

    void handle_request() {
        u8 buf[256];
        cap::Handle handles[4];
        u32 handle_count = 4;

        i64 len = channel_recv(service_channel_.recv_handle,
                               buf, sizeof(buf), handles, &handle_count);
        if (len < 0) return;

        cap::Handle reply = handles[0];
        u32 type = *reinterpret_cast<u32 *>(buf);

        switch (type) {
            case NET_SOCKET_CREATE:
                handle_socket_create(buf, reply);
                break;
            case NET_SOCKET_CONNECT:
                handle_socket_connect(buf, reply);
                break;
            case NET_SOCKET_SEND:
                handle_socket_send(buf, reply, &handles[1], handle_count - 1);
                break;
            case NET_SOCKET_RECV:
                handle_socket_recv(buf, reply);
                break;
            case NET_DNS_RESOLVE:
                handle_dns_resolve(buf, reply);
                break;
            // ... etc
        }
    }

    void handle_socket_create(void *msg, cap::Handle reply) {
        auto *req = static_cast<NetSocketCreateReq *>(msg);

        i32 sock_id = -1;
        if (req->sock_type == SOCK_STREAM) {
            sock_id = tcp_.socket_create();
        } else if (req->sock_type == SOCK_DGRAM) {
            sock_id = udp_.socket_create();
        }

        NetSocketCreateReply reply_msg;
        reply_msg.type = NET_SOCKET_CREATE | 0x80;
        reply_msg.request_id = req->request_id;
        reply_msg.status = (sock_id >= 0) ? 0 : -1;
        reply_msg.socket_id = sock_id;

        channel_send(reply, &reply_msg, sizeof(reply_msg), nullptr, 0);
    }

    void handle_dns_resolve(void *msg, cap::Handle reply) {
        auto *req = static_cast<NetDnsResolveReq *>(msg);

        char hostname[256];
        memcpy(hostname, req->hostname, req->hostname_len);
        hostname[req->hostname_len] = '\0';

        Ipv4Addr result;
        bool ok = dns_.resolve(hostname, &result, 5000);

        NetDnsResolveReply reply_msg;
        reply_msg.type = NET_DNS_RESOLVE | 0x80;
        reply_msg.request_id = req->request_id;
        reply_msg.status = ok ? 0 : -1;
        reply_msg.ip = ok ? result.to_u32() : 0;

        channel_send(reply, &reply_msg, sizeof(reply_msg), nullptr, 0);
    }

    virtio::NetDevice net_device_;
    EthernetLayer eth_;
    ArpLayer arp_;
    Ipv4Layer ip_;
    TcpLayer tcp_;
    UdpLayer udp_;
    DnsResolver dns_;

    ChannelPair service_channel_;
    u32 irq_;
};
```

---

## Phase 6: Cleanup and Optimization

**Goal:** Remove dead kernel code, optimize IPC paths.

**Duration:** ~2 hours

### 6.1 Kernel Code Removal

After servers are stable, remove:

```
kernel/net/          → Entire directory (14.6k lines)
kernel/drivers/virtio/blk.*   → Block driver
kernel/drivers/virtio/net.*   → Network driver
kernel/fs/viperfs/   → ViperFS implementation
kernel/fs/vfs/       → VFS layer (keep minimal shim)
```

### 6.2 Optimizations

1. **Fast-path IPC for small messages**
   - Copy small messages directly instead of using shared memory
   - Inline data in channel messages for < 240 bytes

2. **Zero-copy for large transfers**
   - Map shared memory directly into receiver's address space
   - Use COW for read-only data sharing

3. **IRQ coalescing**
   - Batch interrupt notifications
   - Use interrupt moderation in VirtIO devices

4. **Connection pooling in FS server**
   - Reuse block server connections
   - Cache inode lookups

---

## IPC Protocol Specifications

### Message Format

All IPC messages follow this general format:

```
┌─────────────────────────────────────────────────────────────┐
│  Type (4 bytes)  │  Request ID (4 bytes)  │  Payload...    │
└─────────────────────────────────────────────────────────────┘
```

- **Type**: Message type identifier (request or reply)
- **Request ID**: Unique ID for matching replies to requests
- **Payload**: Type-specific data

### Handle Transfer Convention

- **handles[0]**: Always the reply channel (for requests)
- **handles[1-3]**: Operation-specific data handles

### Error Handling

All replies include a `status` field:
- `0`: Success
- `< 0`: Error code (matches kernel error codes)

### Async vs Sync Operations

Most operations are **synchronous** (request-reply):
1. Client creates reply channel
2. Client sends request with reply channel handle
3. Client blocks on reply channel
4. Server processes and sends reply
5. Client receives reply and closes reply channel

**Async operations** (notifications):
- Server can send unsolicited messages on client's event channel
- Used for: connection close, data arrival, etc.

---

## Migration Checklist

### Phase 1: Kernel Infrastructure
- [ ] Add CAP_DEVICE_ACCESS, CAP_IRQ_ACCESS, CAP_DMA_ACCESS to rights.hpp
- [ ] Implement sys_map_device()
- [ ] Implement sys_irq_register(), sys_irq_wait(), sys_irq_ack()
- [ ] Implement sys_dma_alloc(), sys_dma_free()
- [ ] Implement sys_virt_to_phys()
- [ ] Update syscall table with new syscalls
- [ ] Test device mapping from user space

### Phase 2: Device Driver Framework
- [ ] Create user/libvirtio/ directory
- [ ] Port virtio.hpp/cpp to user-space
- [ ] Port virtqueue.hpp/cpp to user-space
- [ ] Create user-space DMA allocation wrappers
- [ ] Test basic VirtIO device probe from user space

### Phase 3: Block Device Server
- [ ] Create user/servers/blkd/
- [ ] Port virtio-blk driver to user space
- [ ] Implement BLK protocol messages
- [ ] Create kernel blk_client shim
- [ ] Test filesystem with user-space block driver
- [ ] Remove kernel virtio-blk code

### Phase 4: Filesystem Server
- [ ] Create user/servers/fsd/
- [ ] Port ViperFS to user space
- [ ] Port VFS path resolution to user space
- [ ] Implement FS protocol messages
- [ ] Create kernel vfs_client shim
- [ ] Update syscall handlers to use shim
- [ ] Test file operations via server
- [ ] Remove kernel ViperFS/VFS code

### Phase 5: Network Server
- [ ] Create user/servers/netd/
- [ ] Port virtio-net driver to user space
- [ ] Port TCP/IP stack to user space
- [ ] Port DNS resolver to user space
- [ ] Port TLS implementation to user space
- [ ] Implement NET protocol messages
- [ ] Create kernel net_client shim
- [ ] Update syscall handlers to use shim
- [ ] Test network operations via server
- [ ] Remove kernel network code

### Phase 6: Cleanup
- [ ] Remove all dead kernel code
- [ ] Update documentation
- [ ] Performance testing
- [ ] Optimize hot paths

---

## Appendix A: File Structure After Migration

```
os/
├── kernel/                      # ~8k lines (microkernel)
│   ├── arch/aarch64/           # CPU, MMU, GIC, timer, exceptions
│   ├── mm/                     # PMM, VMM, slab, heap, fault handling
│   ├── sched/                  # Scheduler, tasks, signals
│   ├── ipc/                    # Channels, poll, pollsets
│   ├── cap/                    # Capability tables
│   ├── viper/                  # Process management
│   ├── syscall/                # Syscall dispatch + shims
│   └── console/                # Serial output (minimal)
│
├── user/
│   ├── libc/                   # Standard C library
│   ├── libvirtio/              # VirtIO user-space library
│   ├── servers/
│   │   ├── blkd/              # Block device server
│   │   ├── fsd/               # Filesystem server
│   │   └── netd/              # Network server
│   ├── vinit/                  # Init process / shell
│   └── apps/                   # User applications
│
└── docs/
    └── microkernel_plan.md     # This document
```

---

## Appendix B: Performance Considerations

### IPC Overhead

Expected overhead per IPC round-trip:
- Channel send: ~200 cycles
- Context switch: ~500 cycles
- Channel recv: ~200 cycles
- **Total: ~1-2 microseconds**

### Mitigation Strategies

1. **Batching**: Combine multiple small operations
2. **Caching**: FS server caches metadata and data blocks
3. **Async I/O**: Don't block on every request
4. **Large transfers**: Use shared memory for bulk data

### Benchmarks to Run

1. File read throughput (sequential)
2. File read latency (small random reads)
3. Network throughput (TCP bulk transfer)
4. Network latency (ping)
5. DNS resolution time
6. TLS handshake time

---

## Appendix C: Security Model

### Capability-Based Access Control

All server access is capability-controlled:
- Applications receive service handles from init process
- Handles can be derived with reduced rights
- Revocation propagates to derived handles

### Server Isolation

Each server runs in separate address space:
- Cannot directly access other servers' memory
- Cannot access device memory without explicit grant
- Cannot handle IRQs without registration

### Privilege Levels

| Component | Privilege | Device Access | IRQ Access |
|-----------|-----------|---------------|------------|
| Kernel | EL1 | Full | Full |
| blkd | EL0 + CAP | VirtIO-blk MMIO | VirtIO-blk IRQ |
| netd | EL0 + CAP | VirtIO-net MMIO | VirtIO-net IRQ |
| fsd | EL0 | None | None |
| Applications | EL0 | None | None |

---

*End of Microkernel Migration Plan*
