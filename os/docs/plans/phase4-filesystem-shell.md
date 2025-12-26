# ViperOS Phase 4: Filesystem & Shell

## Detailed Implementation Plan (C++)

**Duration:** 12 weeks (Months 10-12)  
**Goal:** Boot from disk to interactive shell  
**Milestone:** `SYS:>` prompt accepting commands  
**Prerequisites:** Phase 3 complete (user space, capabilities, KHeap)

---

## Executive Summary

Phase 4 makes ViperOS a usable operating system. We implement disk I/O, a proper filesystem, and an interactive shell.
By the end, you can boot from disk, navigate directories, run programs, and execute scripts.

Key components:

1. **virtio-blk** — Block device driver for QEMU disk access
2. **ViperFS** — On-disk filesystem with inodes and directories
3. **VFS Layer** — Filesystem abstraction with open file table
4. **File Syscalls** — FsOpen, FsRead, FsWrite, FsReadDir, etc.
5. **vinit from disk** — Load first process from SYS:viper/vinit.vpr
6. **Assign System** — Named directory capabilities (SYS:, C:, HOME:)
7. **vsh Shell** — Interactive command line with builtins
8. **Core Commands** — Dir, List, Type, Copy, Delete, etc.

---

## Architecture

```
┌────────────────────────────────────────────────────────────────────┐
│                          User Space                                 │
├────────────────────────────────────────────────────────────────────┤
│   vsh (shell)              Commands                                │
│   ┌──────────────┐        ┌──────────────────────────────────┐    │
│   │ Prompt       │        │ dir.vpr  list.vpr  copy.vpr  ... │    │
│   │ Parse        │        └──────────────────────────────────┘    │
│   │ Execute      │                                                 │
│   │ Builtins     │        Assigns: SYS: → handle_0                │
│   └──────────────┘                 C:   → handle_1                │
│                                    HOME: → handle_2               │
├────────────────────────────────────────────────────────────────────┤
│                          Syscalls                                   │
│   FsOpen  FsRead  FsWrite  FsReadDir  FsCreate  FsDelete  ...     │
├────────────────────────────────────────────────────────────────────┤
│                          Kernel                                     │
├────────────────────────────────────────────────────────────────────┤
│   VFS Layer                                                        │
│   ┌──────────────────────────────────────────────────────────┐    │
│   │ Open File Table                                           │    │
│   │ Mount Table                                               │    │
│   │ Path Resolution                                           │    │
│   └──────────────────────────────────────────────────────────┘    │
│                              │                                      │
│   ViperFS Driver             │                                      │
│   ┌──────────────────────────┴────────────────────────────────┐   │
│   │ Superblock  Inodes  Directories  Block Allocation         │   │
│   └──────────────────────────┬────────────────────────────────┘   │
│                              │                                      │
│   Block Cache                │                                      │
│   ┌──────────────────────────┴────────────────────────────────┐   │
│   │ LRU Cache  Dirty Tracking  Write-back                     │   │
│   └──────────────────────────┬────────────────────────────────┘   │
│                              │                                      │
│   virtio-blk Driver          │                                      │
│   ┌──────────────────────────┴────────────────────────────────┐   │
│   │ Virtqueue  Request/Response  DMA Buffers                  │   │
│   └───────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────────┘
```

---

## Project Structure

```
kernel/
├── drivers/
│   └── virtio/
│       ├── virtio.cpp/.hpp         # Common virtio infrastructure
│       ├── virtqueue.cpp/.hpp      # Virtqueue implementation
│       └── blk.cpp/.hpp            # virtio-blk driver
├── fs/
│   ├── vfs.cpp/.hpp                # Virtual filesystem layer
│   ├── file.cpp/.hpp               # Open file objects
│   ├── mount.cpp/.hpp              # Mount table
│   ├── viperfs/
│   │   ├── viperfs.cpp/.hpp        # ViperFS main
│   │   ├── superblock.cpp/.hpp     # Superblock operations
│   │   ├── inode.cpp/.hpp          # Inode management
│   │   ├── dir.cpp/.hpp            # Directory operations
│   │   ├── alloc.cpp/.hpp          # Block allocation
│   │   └── format.hpp              # On-disk structures
│   └── cache.cpp/.hpp              # Block cache
├── syscall/
│   └── fs_syscalls.cpp             # Filesystem syscalls
└── init/
    └── disk_init.cpp               # Load vinit from disk

user/
├── vinit/
│   └── vinit.cpp                   # Init process
├── vsh/
│   ├── vsh.cpp                     # Shell main
│   ├── parse.cpp/.hpp              # Command parser
│   ├── execute.cpp/.hpp            # Command execution
│   ├── builtins.cpp/.hpp           # Built-in commands
│   └── assign.cpp/.hpp             # Assign management
├── cmd/
│   ├── dir.cpp                     # Dir command
│   ├── list.cpp                    # List command
│   ├── type.cpp                    # Type command
│   ├── copy.cpp                    # Copy command
│   ├── delete.cpp                  # Delete command
│   ├── makedir.cpp                 # MakeDir command
│   ├── rename.cpp                  # Rename command
│   ├── assign.cpp                  # Assign command
│   ├── echo.cpp                    # Echo command
│   ├── version.cpp                 # Version command
│   └── ...                         # Other commands
└── lib/
    ├── vsys.cpp/.hpp               # Syscall wrappers
    ├── vio.cpp/.hpp                # File I/O library
    └── vstring.cpp/.hpp            # String utilities
```

---

## Milestones

| # | Milestone             | Duration    | Deliverable               |
|---|-----------------------|-------------|---------------------------|
| 1 | virtio Infrastructure | Week 1      | Virtqueue, device init    |
| 2 | virtio-blk Driver     | Week 2      | Block read/write          |
| 3 | Block Cache           | Week 3      | LRU cache, write-back     |
| 4 | ViperFS Read-Only     | Weeks 4-5   | Superblock, inodes, dirs  |
| 5 | ViperFS Read-Write    | Week 6      | Create, delete, write     |
| 6 | VFS & File Syscalls   | Weeks 7-8   | Open file table, syscalls |
| 7 | vinit from Disk       | Week 9      | Boot from filesystem      |
| 8 | vsh Shell             | Weeks 10-11 | Parser, builtins, assigns |
| 9 | Commands & Polish     | Week 12     | Core commands, testing    |

---

## Milestone 1: virtio Infrastructure

**Duration:** Week 1  
**Deliverable:** Virtqueue implementation, device discovery

### 1.1 Virtio Device Discovery

```cpp
// kernel/drivers/virtio/virtio.hpp
#pragma once

#include "../../lib/types.hpp"

namespace viper::virtio {

// MMIO register offsets
constexpr u32 VIRTIO_MMIO_MAGIC          = 0x000;
constexpr u32 VIRTIO_MMIO_VERSION        = 0x004;
constexpr u32 VIRTIO_MMIO_DEVICE_ID      = 0x008;
constexpr u32 VIRTIO_MMIO_VENDOR_ID      = 0x00C;
constexpr u32 VIRTIO_MMIO_DEVICE_FEATURES = 0x010;
constexpr u32 VIRTIO_MMIO_DRIVER_FEATURES = 0x020;
constexpr u32 VIRTIO_MMIO_QUEUE_SEL      = 0x030;
constexpr u32 VIRTIO_MMIO_QUEUE_NUM_MAX  = 0x034;
constexpr u32 VIRTIO_MMIO_QUEUE_NUM      = 0x038;
constexpr u32 VIRTIO_MMIO_QUEUE_READY    = 0x044;
constexpr u32 VIRTIO_MMIO_QUEUE_NOTIFY   = 0x050;
constexpr u32 VIRTIO_MMIO_INTERRUPT_STATUS = 0x060;
constexpr u32 VIRTIO_MMIO_INTERRUPT_ACK  = 0x064;
constexpr u32 VIRTIO_MMIO_STATUS         = 0x070;
constexpr u32 VIRTIO_MMIO_QUEUE_DESC_LOW = 0x080;
constexpr u32 VIRTIO_MMIO_QUEUE_DESC_HIGH = 0x084;
constexpr u32 VIRTIO_MMIO_QUEUE_AVAIL_LOW = 0x090;
constexpr u32 VIRTIO_MMIO_QUEUE_AVAIL_HIGH = 0x094;
constexpr u32 VIRTIO_MMIO_QUEUE_USED_LOW = 0x0A0;
constexpr u32 VIRTIO_MMIO_QUEUE_USED_HIGH = 0x0A4;
constexpr u32 VIRTIO_MMIO_CONFIG         = 0x100;

// Device status bits
constexpr u32 VIRTIO_STATUS_ACKNOWLEDGE = 1;
constexpr u32 VIRTIO_STATUS_DRIVER      = 2;
constexpr u32 VIRTIO_STATUS_DRIVER_OK   = 4;
constexpr u32 VIRTIO_STATUS_FEATURES_OK = 8;
constexpr u32 VIRTIO_STATUS_FAILED      = 128;

// Device types
constexpr u32 VIRTIO_DEV_NET   = 1;
constexpr u32 VIRTIO_DEV_BLK   = 2;
constexpr u32 VIRTIO_DEV_GPU   = 16;
constexpr u32 VIRTIO_DEV_INPUT = 18;

// Magic value
constexpr u32 VIRTIO_MAGIC = 0x74726976;  // "virt"

class Device {
public:
    bool init(VirtAddr base);
    
    u32 read32(u32 offset);
    void write32(u32 offset, u32 value);
    
    u32 device_id() const { return device_id_; }
    VirtAddr base() const { return base_; }
    
    bool negotiate_features(u64 required, u64 supported);
    void set_status(u32 status);
    u32 get_status();
    
protected:
    VirtAddr base_{0};
    u32 device_id_{0};
};

// Probe virtio devices on QEMU virt machine
void probe_devices();

// Get device by type (returns first matching)
Device* get_device(u32 type);

} // namespace viper::virtio
```

### 1.2 Virtqueue Implementation

```cpp
// kernel/drivers/virtio/virtqueue.hpp
#pragma once

#include "../../lib/types.hpp"
#include "../../mm/pmm.hpp"

namespace viper::virtio {

// Descriptor flags
constexpr u16 VRING_DESC_F_NEXT     = 1;
constexpr u16 VRING_DESC_F_WRITE    = 2;  // Device writes (vs reads)
constexpr u16 VRING_DESC_F_INDIRECT = 4;

// Descriptor table entry
struct VringDesc {
    u64 addr;       // Physical address
    u32 len;
    u16 flags;
    u16 next;
};

// Available ring
struct VringAvail {
    u16 flags;
    u16 idx;
    u16 ring[];     // Queue size entries
};

// Used ring entry
struct VringUsedElem {
    u32 id;         // Descriptor chain head
    u32 len;        // Bytes written by device
};

// Used ring
struct VringUsed {
    u16 flags;
    u16 idx;
    VringUsedElem ring[];
};

class Virtqueue {
public:
    bool init(Device* dev, u32 queue_idx, u32 size);
    void destroy();
    
    // Allocate a descriptor chain
    // Returns head index, or -1 on failure
    i32 alloc_desc();
    void free_desc(u32 idx);
    
    // Add buffer to descriptor
    void set_desc(u32 idx, PhysAddr addr, u32 len, u16 flags, u32 next);
    
    // Submit descriptor chain to device
    void submit(u32 head);
    
    // Notify device
    void kick();
    
    // Process completed requests
    // Returns head index of completed chain, or -1 if none
    i32 poll_used();
    
    // Wait for completion (blocking)
    void wait_used();
    
    u32 size() const { return size_; }
    
private:
    Device* dev_ = nullptr;
    u32 queue_idx_ = 0;
    u32 size_ = 0;
    
    VringDesc* desc_ = nullptr;
    VringAvail* avail_ = nullptr;
    VringUsed* used_ = nullptr;
    
    PhysAddr desc_phys_{0};
    PhysAddr avail_phys_{0};
    PhysAddr used_phys_{0};
    
    u16 free_head_ = 0;
    u16 num_free_ = 0;
    u16 last_used_idx_ = 0;
    
    // Free list embedded in descriptors
    u8* free_map_ = nullptr;
};

} // namespace viper::virtio
```

### 1.3 Virtqueue Implementation

```cpp
// kernel/drivers/virtio/virtqueue.cpp
#include "virtqueue.hpp"
#include "virtio.hpp"
#include "../mm/pmm.hpp"
#include "../lib/string.hpp"

namespace viper::virtio {

bool Virtqueue::init(Device* dev, u32 queue_idx, u32 size) {
    dev_ = dev;
    queue_idx_ = queue_idx;
    
    // Select queue
    dev->write32(VIRTIO_MMIO_QUEUE_SEL, queue_idx);
    
    // Check max size
    u32 max_size = dev->read32(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max_size == 0) return false;
    if (size > max_size) size = max_size;
    size_ = size;
    
    // Allocate descriptor table (16 bytes per entry)
    usize desc_size = size * sizeof(VringDesc);
    usize desc_pages = (desc_size + 4095) / 4096;
    auto desc_result = pmm::alloc_pages(desc_pages);
    if (!desc_result.is_ok()) return false;
    desc_phys_ = desc_result.unwrap();
    desc_ = pmm::phys_to_virt(desc_phys_).as_ptr<VringDesc>();
    memset(desc_, 0, desc_pages * 4096);
    
    // Allocate available ring (6 + 2*size bytes)
    usize avail_size = 6 + 2 * size;
    usize avail_pages = (avail_size + 4095) / 4096;
    auto avail_result = pmm::alloc_pages(avail_pages);
    if (!avail_result.is_ok()) return false;
    avail_phys_ = avail_result.unwrap();
    avail_ = pmm::phys_to_virt(avail_phys_).as_ptr<VringAvail>();
    memset(avail_, 0, avail_pages * 4096);
    
    // Allocate used ring (6 + 8*size bytes)
    usize used_size = 6 + 8 * size;
    usize used_pages = (used_size + 4095) / 4096;
    auto used_result = pmm::alloc_pages(used_pages);
    if (!used_result.is_ok()) return false;
    used_phys_ = used_result.unwrap();
    used_ = pmm::phys_to_virt(used_phys_).as_ptr<VringUsed>();
    memset(used_, 0, used_pages * 4096);
    
    // Set queue size
    dev->write32(VIRTIO_MMIO_QUEUE_NUM, size);
    
    // Set queue addresses
    dev->write32(VIRTIO_MMIO_QUEUE_DESC_LOW, desc_phys_.raw() & 0xFFFFFFFF);
    dev->write32(VIRTIO_MMIO_QUEUE_DESC_HIGH, desc_phys_.raw() >> 32);
    dev->write32(VIRTIO_MMIO_QUEUE_AVAIL_LOW, avail_phys_.raw() & 0xFFFFFFFF);
    dev->write32(VIRTIO_MMIO_QUEUE_AVAIL_HIGH, avail_phys_.raw() >> 32);
    dev->write32(VIRTIO_MMIO_QUEUE_USED_LOW, used_phys_.raw() & 0xFFFFFFFF);
    dev->write32(VIRTIO_MMIO_QUEUE_USED_HIGH, used_phys_.raw() >> 32);
    
    // Enable queue
    dev->write32(VIRTIO_MMIO_QUEUE_READY, 1);
    
    // Initialize free list
    for (u32 i = 0; i < size - 1; i++) {
        desc_[i].next = i + 1;
    }
    desc_[size - 1].next = 0xFFFF;
    free_head_ = 0;
    num_free_ = size;
    
    return true;
}

i32 Virtqueue::alloc_desc() {
    if (num_free_ == 0) return -1;
    
    u32 idx = free_head_;
    free_head_ = desc_[idx].next;
    num_free_--;
    
    return idx;
}

void Virtqueue::free_desc(u32 idx) {
    desc_[idx].next = free_head_;
    desc_[idx].flags = 0;
    free_head_ = idx;
    num_free_++;
}

void Virtqueue::set_desc(u32 idx, PhysAddr addr, u32 len, u16 flags, u32 next) {
    desc_[idx].addr = addr.raw();
    desc_[idx].len = len;
    desc_[idx].flags = flags;
    desc_[idx].next = next;
}

void Virtqueue::submit(u32 head) {
    u16 avail_idx = avail_->idx;
    avail_->ring[avail_idx % size_] = head;
    
    // Memory barrier
    asm volatile("dmb sy" ::: "memory");
    
    avail_->idx = avail_idx + 1;
}

void Virtqueue::kick() {
    asm volatile("dmb sy" ::: "memory");
    dev_->write32(VIRTIO_MMIO_QUEUE_NOTIFY, queue_idx_);
}

i32 Virtqueue::poll_used() {
    asm volatile("dmb sy" ::: "memory");
    
    if (last_used_idx_ == used_->idx) {
        return -1;
    }
    
    u32 idx = last_used_idx_ % size_;
    u32 head = used_->ring[idx].id;
    last_used_idx_++;
    
    return head;
}

} // namespace viper::virtio
```

---

## Milestone 2: virtio-blk Driver

**Duration:** Week 2  
**Deliverable:** Block read/write operations

### 2.1 virtio-blk Device

```cpp
// kernel/drivers/virtio/blk.hpp
#pragma once

#include "virtio.hpp"
#include "virtqueue.hpp"

namespace viper::virtio {

// Block request types
constexpr u32 VIRTIO_BLK_T_IN  = 0;  // Read
constexpr u32 VIRTIO_BLK_T_OUT = 1;  // Write

// Block request status
constexpr u8 VIRTIO_BLK_S_OK     = 0;
constexpr u8 VIRTIO_BLK_S_IOERR  = 1;
constexpr u8 VIRTIO_BLK_S_UNSUPP = 2;

// Block request header
struct BlkReqHeader {
    u32 type;
    u32 reserved;
    u64 sector;
};

// Block device configuration
struct BlkConfig {
    u64 capacity;     // Sectors
    u32 size_max;
    u32 seg_max;
    struct {
        u16 cylinders;
        u8 heads;
        u8 sectors;
    } geometry;
    u32 blk_size;     // Block size (usually 512)
};

class BlkDevice : public Device {
public:
    bool init(VirtAddr base);
    
    // Synchronous block I/O
    i32 read_sectors(u64 sector, u32 count, void* buf);
    i32 write_sectors(u64 sector, u32 count, const void* buf);
    
    u64 capacity() const { return config_.capacity; }
    u32 block_size() const { return config_.blk_size ? config_.blk_size : 512; }
    
private:
    BlkConfig config_;
    Virtqueue vq_;
    
    // Request buffers (pre-allocated)
    static constexpr usize MAX_REQUESTS = 8;
    struct Request {
        BlkReqHeader header;
        u8 status;
        bool in_use;
    };
    Request requests_[MAX_REQUESTS];
    PhysAddr requests_phys_;
    
    i32 do_request(u32 type, u64 sector, u32 count, void* buf);
};

// Global block device
BlkDevice* blk_device();
void blk_init();

} // namespace viper::virtio
```

### 2.2 virtio-blk Implementation

```cpp
// kernel/drivers/virtio/blk.cpp
#include "blk.hpp"
#include "../../lib/format.hpp"
#include "../../lib/string.hpp"
#include "../../mm/pmm.hpp"

namespace viper::virtio {

namespace {
    BlkDevice blk_dev;
}

BlkDevice* blk_device() { return &blk_dev; }

bool BlkDevice::init(VirtAddr base) {
    if (!Device::init(base)) return false;
    
    if (device_id_ != VIRTIO_DEV_BLK) {
        kprintf("virtio-blk: Wrong device type %u\n", device_id_);
        return false;
    }
    
    // Reset device
    write32(VIRTIO_MMIO_STATUS, 0);
    
    // Acknowledge
    set_status(VIRTIO_STATUS_ACKNOWLEDGE);
    set_status(get_status() | VIRTIO_STATUS_DRIVER);
    
    // Read config
    config_.capacity = read32(VIRTIO_MMIO_CONFIG + 0) |
                       (u64(read32(VIRTIO_MMIO_CONFIG + 4)) << 32);
    config_.blk_size = 512;  // Default
    
    kprintf("virtio-blk: capacity = %lu sectors (%lu MB)\n",
            config_.capacity, (config_.capacity * 512) / (1024 * 1024));
    
    // Negotiate features (minimal for now)
    if (!negotiate_features(0, 0)) {
        return false;
    }
    
    // Init virtqueue
    if (!vq_.init(this, 0, 128)) {
        kprintf("virtio-blk: Failed to init virtqueue\n");
        return false;
    }
    
    // Allocate request buffers
    auto req_result = pmm::alloc_page();
    if (!req_result.is_ok()) return false;
    requests_phys_ = req_result.unwrap();
    memset(requests_, 0, sizeof(requests_));
    
    // Driver ready
    set_status(get_status() | VIRTIO_STATUS_DRIVER_OK);
    
    kprintf("virtio-blk: Initialized\n");
    return true;
}

i32 BlkDevice::do_request(u32 type, u64 sector, u32 count, void* buf) {
    // Find free request slot
    int req_idx = -1;
    for (int i = 0; i < MAX_REQUESTS; i++) {
        if (!requests_[i].in_use) {
            req_idx = i;
            break;
        }
    }
    if (req_idx < 0) return -1;
    
    Request& req = requests_[req_idx];
    req.in_use = true;
    req.header.type = type;
    req.header.reserved = 0;
    req.header.sector = sector;
    req.status = 0xFF;  // Invalid
    
    // Get physical address of request
    PhysAddr header_phys = requests_phys_ + req_idx * sizeof(Request);
    PhysAddr status_phys = header_phys + offsetof(Request, status);
    
    // Get physical address of data buffer
    // For now, assume buf is in HHDM
    PhysAddr buf_phys = pmm::virt_to_phys(VirtAddr{reinterpret_cast<u64>(buf)});
    u32 buf_len = count * 512;
    
    // Allocate descriptors
    i32 desc0 = vq_.alloc_desc();
    i32 desc1 = vq_.alloc_desc();
    i32 desc2 = vq_.alloc_desc();
    
    if (desc0 < 0 || desc1 < 0 || desc2 < 0) {
        if (desc0 >= 0) vq_.free_desc(desc0);
        if (desc1 >= 0) vq_.free_desc(desc1);
        if (desc2 >= 0) vq_.free_desc(desc2);
        req.in_use = false;
        return -1;
    }
    
    // Set up descriptor chain
    // Descriptor 0: Request header (device reads)
    vq_.set_desc(desc0, header_phys, sizeof(BlkReqHeader),
                 VRING_DESC_F_NEXT, desc1);
    
    // Descriptor 1: Data buffer
    u16 data_flags = VRING_DESC_F_NEXT;
    if (type == VIRTIO_BLK_T_IN) {
        data_flags |= VRING_DESC_F_WRITE;  // Device writes to buffer
    }
    vq_.set_desc(desc1, buf_phys, buf_len, data_flags, desc2);
    
    // Descriptor 2: Status (device writes)
    vq_.set_desc(desc2, status_phys, 1, VRING_DESC_F_WRITE, 0);
    
    // Submit and kick
    vq_.submit(desc0);
    vq_.kick();
    
    // Wait for completion (polling)
    while (true) {
        i32 completed = vq_.poll_used();
        if (completed == desc0) {
            break;
        }
        // Could yield here
        asm volatile("yield");
    }
    
    // Free descriptors
    vq_.free_desc(desc0);
    vq_.free_desc(desc1);
    vq_.free_desc(desc2);
    
    // Check status
    u8 status = req.status;
    req.in_use = false;
    
    if (status != VIRTIO_BLK_S_OK) {
        kprintf("virtio-blk: Request failed, status=%u\n", status);
        return -1;
    }
    
    return 0;
}

i32 BlkDevice::read_sectors(u64 sector, u32 count, void* buf) {
    return do_request(VIRTIO_BLK_T_IN, sector, count, buf);
}

i32 BlkDevice::write_sectors(u64 sector, u32 count, const void* buf) {
    return do_request(VIRTIO_BLK_T_OUT, sector, count, const_cast<void*>(buf));
}

void blk_init() {
    // Find virtio-blk device
    // QEMU virt machine has virtio at 0x0a000000+
    for (u64 addr = 0x0a000000; addr < 0x0a004000; addr += 0x200) {
        VirtAddr va = pmm::phys_to_virt(PhysAddr{addr});
        
        u32 magic = *reinterpret_cast<volatile u32*>(va.raw());
        if (magic != VIRTIO_MAGIC) continue;
        
        u32 dev_id = *reinterpret_cast<volatile u32*>(va.raw() + 8);
        if (dev_id == VIRTIO_DEV_BLK) {
            if (blk_dev.init(va)) {
                return;
            }
        }
    }
    
    kprintf("virtio-blk: No device found\n");
}

} // namespace viper::virtio
```

---

## Milestone 3: Block Cache

**Duration:** Week 3  
**Deliverable:** LRU block cache with write-back

### 3.1 Block Cache Interface

```cpp
// kernel/fs/cache.hpp
#pragma once

#include "../lib/types.hpp"

namespace viper::fs {

constexpr usize BLOCK_SIZE = 4096;
constexpr usize CACHE_SIZE = 256;  // 1MB cache

struct CacheBlock {
    u64 block_num;
    u8 data[BLOCK_SIZE];
    bool valid;
    bool dirty;
    u32 refcount;
    CacheBlock* lru_prev;
    CacheBlock* lru_next;
    CacheBlock* hash_next;
};

class BlockCache {
public:
    bool init();
    
    // Get a block (reads from disk if not cached)
    CacheBlock* get(u64 block_num);
    
    // Get a block for writing (marks dirty)
    CacheBlock* get_for_write(u64 block_num);
    
    // Release a block
    void release(CacheBlock* block);
    
    // Flush dirty blocks to disk
    void sync();
    
    // Flush a specific block
    void sync_block(CacheBlock* block);
    
private:
    CacheBlock blocks_[CACHE_SIZE];
    CacheBlock* lru_head_ = nullptr;
    CacheBlock* lru_tail_ = nullptr;
    
    // Hash table for fast lookup
    static constexpr usize HASH_SIZE = 128;
    CacheBlock* hash_[HASH_SIZE] = {};
    
    CacheBlock* find(u64 block_num);
    CacheBlock* evict();
    void touch(CacheBlock* block);
    u32 hash(u64 block_num);
};

// Global cache
BlockCache& cache();

} // namespace viper::fs
```

---

## Milestone 4: ViperFS Read-Only

**Duration:** Weeks 4-5  
**Deliverable:** Read superblock, inodes, directories

### 4.1 On-Disk Structures

```cpp
// kernel/fs/viperfs/format.hpp
#pragma once

#include "../../lib/types.hpp"

namespace viper::fs::viperfs {

constexpr u32 VIPERFS_MAGIC = 0x53465056;  // "VPFS"
constexpr u32 VIPERFS_VERSION = 1;
constexpr u64 BLOCK_SIZE = 4096;
constexpr u64 INODE_SIZE = 256;

// Superblock (block 0)
struct Superblock {
    u32 magic;
    u32 version;
    u64 block_size;
    u64 total_blocks;
    u64 free_blocks;
    u64 inode_count;
    u64 root_inode;
    u64 journal_start;    // Unused in v0
    u64 journal_size;     // 0 in v0
    u64 bitmap_start;
    u64 inode_table_start;
    u64 data_start;
    u8 uuid[16];
    char label[64];
    u8 _reserved[3800];
};
static_assert(sizeof(Superblock) == 4096);

// Inode
struct Inode {
    u64 inode_num;
    u32 mode;             // Type + permissions
    u32 flags;
    u64 size;
    u64 blocks;           // Blocks allocated
    u64 atime, mtime, ctime;
    u64 direct[12];       // Direct block pointers
    u64 indirect;         // Single indirect
    u64 double_indirect;
    u64 triple_indirect;
    u64 generation;
    u8 _reserved[48];
};
static_assert(sizeof(Inode) == 256);

// Inode mode bits
constexpr u32 S_IFMT   = 0xF000;  // Type mask
constexpr u32 S_IFREG  = 0x8000;  // Regular file
constexpr u32 S_IFDIR  = 0x4000;  // Directory
constexpr u32 S_IFLNK  = 0xA000;  // Symlink

// Directory entry (variable size)
struct DirEntry {
    u64 inode;
    u16 rec_len;          // Total entry length
    u8 name_len;
    u8 file_type;
    char name[];          // Null-terminated
};

// File types in directory entries
constexpr u8 FT_UNKNOWN = 0;
constexpr u8 FT_REG     = 1;
constexpr u8 FT_DIR     = 2;
constexpr u8 FT_LNK     = 7;

} // namespace viper::fs::viperfs
```

### 4.2 ViperFS Class

```cpp
// kernel/fs/viperfs/viperfs.hpp
#pragma once

#include "format.hpp"
#include "../cache.hpp"

namespace viper::fs::viperfs {

class ViperFS {
public:
    bool mount();
    void unmount();
    
    // Inode operations
    Result<Inode*, i32> read_inode(u64 ino);
    void release_inode(Inode* inode);
    
    // Directory operations
    Result<u64, i32> lookup(Inode* dir, const char* name, usize name_len);
    i32 readdir(Inode* dir, u64 offset, 
                void (*callback)(const char* name, u64 ino, u8 type, void* ctx),
                void* ctx);
    
    // File data
    i32 read_data(Inode* inode, u64 offset, void* buf, usize len);
    
    // Info
    u64 root_inode() const { return sb_.root_inode; }
    const char* label() const { return sb_.label; }
    
private:
    Superblock sb_;
    bool mounted_ = false;
    
    u64 inode_block(u64 ino);
    u64 get_block_ptr(Inode* inode, u64 block_idx);
};

// Global filesystem
ViperFS& viperfs();

} // namespace viper::fs::viperfs
```

### 4.3 ViperFS Implementation

```cpp
// kernel/fs/viperfs/viperfs.cpp
#include "viperfs.hpp"
#include "../cache.hpp"
#include "../../drivers/virtio/blk.hpp"
#include "../../lib/format.hpp"
#include "../../lib/string.hpp"

namespace viper::fs::viperfs {

namespace {
    ViperFS fs;
}

ViperFS& viperfs() { return fs; }

bool ViperFS::mount() {
    // Read superblock
    auto* block = cache().get(0);
    if (!block) return false;
    
    memcpy(&sb_, block->data, sizeof(Superblock));
    cache().release(block);
    
    // Validate
    if (sb_.magic != VIPERFS_MAGIC) {
        kprintf("ViperFS: Invalid magic 0x%08x\n", sb_.magic);
        return false;
    }
    
    if (sb_.version != VIPERFS_VERSION) {
        kprintf("ViperFS: Unknown version %u\n", sb_.version);
        return false;
    }
    
    kprintf("ViperFS: Mounted '%s'\n", sb_.label);
    kprintf("  Blocks: %lu total, %lu free\n", sb_.total_blocks, sb_.free_blocks);
    kprintf("  Inodes: %lu\n", sb_.inode_count);
    
    mounted_ = true;
    return true;
}

u64 ViperFS::inode_block(u64 ino) {
    u64 inodes_per_block = BLOCK_SIZE / INODE_SIZE;
    return sb_.inode_table_start + ino / inodes_per_block;
}

Result<Inode*, i32> ViperFS::read_inode(u64 ino) {
    u64 block_num = inode_block(ino);
    u64 offset = (ino % (BLOCK_SIZE / INODE_SIZE)) * INODE_SIZE;
    
    auto* block = cache().get(block_num);
    if (!block) {
        return Result<Inode*, i32>::failure(VERR_IO);
    }
    
    // Copy inode data
    auto* inode = new Inode;
    memcpy(inode, block->data + offset, sizeof(Inode));
    cache().release(block);
    
    return Result<Inode*, i32>::success(inode);
}

void ViperFS::release_inode(Inode* inode) {
    delete inode;
}

Result<u64, i32> ViperFS::lookup(Inode* dir, const char* name, usize name_len) {
    if ((dir->mode & S_IFMT) != S_IFDIR) {
        return Result<u64, i32>::failure(VERR_INVALID_ARG);
    }
    
    u64 result_ino = 0;
    bool found = false;
    
    readdir(dir, 0, [](const char* entry_name, u64 ino, u8, void* ctx) {
        auto* state = static_cast<std::pair<const char*, u64*>*>(ctx);
        if (strcmp(entry_name, state->first) == 0) {
            *state->second = ino;
        }
    }, /* ctx with name and result */);
    
    // Simplified: linear scan
    u64 offset = 0;
    while (offset < dir->size) {
        u8 buf[BLOCK_SIZE];
        i32 r = read_data(dir, offset, buf, BLOCK_SIZE);
        if (r < 0) break;
        
        usize pos = 0;
        while (pos < BLOCK_SIZE) {
            auto* entry = reinterpret_cast<DirEntry*>(buf + pos);
            if (entry->rec_len == 0) break;
            
            if (entry->inode != 0 && 
                entry->name_len == name_len &&
                memcmp(entry->name, name, name_len) == 0) {
                return Result<u64, i32>::success(entry->inode);
            }
            
            pos += entry->rec_len;
        }
        offset += BLOCK_SIZE;
    }
    
    return Result<u64, i32>::failure(VERR_NOT_FOUND);
}

u64 ViperFS::get_block_ptr(Inode* inode, u64 block_idx) {
    if (block_idx < 12) {
        return inode->direct[block_idx];
    }
    
    block_idx -= 12;
    u64 ptrs_per_block = BLOCK_SIZE / sizeof(u64);
    
    if (block_idx < ptrs_per_block) {
        // Single indirect
        if (inode->indirect == 0) return 0;
        auto* block = cache().get(inode->indirect);
        if (!block) return 0;
        u64 ptr = reinterpret_cast<u64*>(block->data)[block_idx];
        cache().release(block);
        return ptr;
    }
    
    // Double/triple indirect omitted for brevity
    return 0;
}

i32 ViperFS::read_data(Inode* inode, u64 offset, void* buf, usize len) {
    if (offset >= inode->size) return 0;
    if (offset + len > inode->size) {
        len = inode->size - offset;
    }
    
    u8* dst = static_cast<u8*>(buf);
    usize remaining = len;
    
    while (remaining > 0) {
        u64 block_idx = offset / BLOCK_SIZE;
        u64 block_off = offset % BLOCK_SIZE;
        usize to_read = BLOCK_SIZE - block_off;
        if (to_read > remaining) to_read = remaining;
        
        u64 block_num = get_block_ptr(inode, block_idx);
        if (block_num == 0) {
            // Sparse: zero fill
            memset(dst, 0, to_read);
        } else {
            auto* block = cache().get(sb_.data_start + block_num - 1);
            if (!block) return -1;
            memcpy(dst, block->data + block_off, to_read);
            cache().release(block);
        }
        
        dst += to_read;
        offset += to_read;
        remaining -= to_read;
    }
    
    return len;
}

} // namespace viper::fs::viperfs
```

---

## Milestone 5: ViperFS Read-Write

**Duration:** Week 6  
**Deliverable:** Create, write, delete operations

### 5.1 Block Allocation

```cpp
// kernel/fs/viperfs/alloc.hpp
#pragma once

#include "format.hpp"

namespace viper::fs::viperfs {

class BlockAllocator {
public:
    void init(Superblock* sb);
    
    // Allocate a data block
    Result<u64, i32> alloc_block();
    
    // Free a data block
    void free_block(u64 block);
    
    // Allocate an inode
    Result<u64, i32> alloc_inode();
    
    // Free an inode
    void free_inode(u64 ino);
    
private:
    Superblock* sb_;
    
    // Bitmap operations
    bool bitmap_test(u64 bit);
    void bitmap_set(u64 bit);
    void bitmap_clear(u64 bit);
};

} // namespace viper::fs::viperfs
```

### 5.2 Write Operations

```cpp
// kernel/fs/viperfs/viperfs.hpp (additions)

class ViperFS {
public:
    // ... read operations ...
    
    // Write operations
    Result<u64, i32> create(Inode* parent, const char* name, usize name_len, u32 mode);
    i32 write_data(Inode* inode, u64 offset, const void* buf, usize len);
    i32 truncate(Inode* inode, u64 new_size);
    i32 unlink(Inode* parent, const char* name, usize name_len);
    i32 mkdir(Inode* parent, const char* name, usize name_len);
    
    // Sync
    void sync_inode(Inode* inode);
    void sync_all();
    
private:
    BlockAllocator alloc_;
    
    i32 add_dir_entry(Inode* dir, u64 ino, const char* name, usize name_len, u8 type);
    i32 remove_dir_entry(Inode* dir, const char* name, usize name_len);
};
```

---

## Milestone 6: VFS & File Syscalls

**Duration:** Weeks 7-8  
**Deliverable:** VFS layer, open file table, syscalls

### 6.1 VFS Structures

```cpp
// kernel/fs/vfs.hpp
#pragma once

#include "../lib/types.hpp"
#include "../cap/table.hpp"

namespace viper::fs {

// Open file flags
constexpr u32 VFS_READ     = 1 << 0;
constexpr u32 VFS_WRITE    = 1 << 1;
constexpr u32 VFS_CREATE   = 1 << 2;
constexpr u32 VFS_EXCL     = 1 << 3;
constexpr u32 VFS_TRUNC    = 1 << 4;
constexpr u32 VFS_APPEND   = 1 << 5;

// File info
struct FileInfo {
    u64 size;
    u64 atime, mtime, ctime;
    u32 mode;
    u32 flags;
};

// Directory entry (user-visible)
struct DirInfo {
    char name[256];
    u16 kind;       // VKIND_FILE or VKIND_DIRECTORY
    u16 flags;
    u32 _pad;
    u64 size;
    u64 mtime;
};

// Open file object (kernel)
struct OpenFile {
    u64 inode_num;
    void* inode;        // ViperFS inode
    u32 flags;
    u64 position;
    cap::Kind kind;     // FILE or DIRECTORY
};

// VFS operations
void init();

// File operations
Result<OpenFile*, i32> open(u64 dir_ino, const char* name, usize len, u32 flags);
i32 read(OpenFile* file, void* buf, usize len);
i32 write(OpenFile* file, const void* buf, usize len);
i64 seek(OpenFile* file, i64 offset, int whence);
i32 stat(OpenFile* file, FileInfo* info);
void close(OpenFile* file);

// Directory operations
Result<OpenFile*, i32> open_root();
i32 readdir(OpenFile* dir, DirInfo* entries, usize max_entries);
Result<OpenFile*, i32> create(OpenFile* parent, const char* name, usize len, cap::Kind kind);
i32 unlink(OpenFile* parent, const char* name, usize len);

} // namespace viper::fs
```

### 6.2 File Syscalls

```cpp
// kernel/syscall/fs_syscalls.cpp
#include "dispatch.hpp"
#include "../fs/vfs.hpp"
#include "../viper/viper.hpp"
#include "../cap/table.hpp"

namespace viper::syscall {

// FsOpenRoot(volume) -> dir_handle
SyscallResult sys_fs_open_root(proc::Viper* v, u32 volume) {
    // For Phase 4, volume 0 = boot disk
    if (volume != 0) {
        return {VERR_INVALID_ARG, 0, 0, 0};
    }
    
    auto result = fs::open_root();
    if (!result.is_ok()) {
        return {result.get_error(), 0, 0, 0};
    }
    
    auto* file = result.unwrap();
    
    auto handle = v->cap_table->insert(
        file,
        cap::Kind::Directory,
        cap::CAP_READ | cap::CAP_LIST
    );
    
    if (!handle.is_ok()) {
        fs::close(file);
        return {handle.get_error(), 0, 0, 0};
    }
    
    return {VOK, handle.unwrap(), 0, 0};
}

// FsOpen(dir, name, name_len, flags) -> handle, kind
SyscallResult sys_fs_open(proc::Viper* v, u32 dir_h, u64 name_ptr, u64 name_len, u32 flags) {
    auto* dir_entry = v->cap_table->get_checked(dir_h, cap::Kind::Directory);
    if (!dir_entry) return {VERR_INVALID_HANDLE, 0, 0, 0};
    
    auto* dir_file = static_cast<fs::OpenFile*>(dir_entry->object);
    
    // Copy name from user space
    char name[256];
    if (name_len > 255) return {VERR_INVALID_ARG, 0, 0, 0};
    // TODO: proper user memory copy
    memcpy(name, reinterpret_cast<void*>(name_ptr), name_len);
    name[name_len] = '\0';
    
    auto result = fs::open(dir_file->inode_num, name, name_len, flags);
    if (!result.is_ok()) {
        return {result.get_error(), 0, 0, 0};
    }
    
    auto* file = result.unwrap();
    cap::Rights rights = cap::CAP_NONE;
    if (flags & fs::VFS_READ) rights = rights | cap::CAP_READ;
    if (flags & fs::VFS_WRITE) rights = rights | cap::CAP_WRITE;
    
    auto handle = v->cap_table->insert(file, file->kind, rights);
    if (!handle.is_ok()) {
        fs::close(file);
        return {handle.get_error(), 0, 0, 0};
    }
    
    return {VOK, handle.unwrap(), static_cast<u64>(file->kind), 0};
}

// IORead(handle, buf, len) -> bytes_read
SyscallResult sys_io_read(proc::Viper* v, u32 h, u64 buf, u64 len) {
    auto* entry = v->cap_table->get_with_rights(h, cap::Kind::File, cap::CAP_READ);
    if (!entry) return {VERR_INVALID_HANDLE, 0, 0, 0};
    
    auto* file = static_cast<fs::OpenFile*>(entry->object);
    
    // TODO: proper user buffer handling
    i32 n = fs::read(file, reinterpret_cast<void*>(buf), len);
    if (n < 0) return {n, 0, 0, 0};
    
    return {VOK, static_cast<u64>(n), 0, 0};
}

// IOWrite(handle, data, len) -> bytes_written
SyscallResult sys_io_write(proc::Viper* v, u32 h, u64 data, u64 len) {
    auto* entry = v->cap_table->get_with_rights(h, cap::Kind::File, cap::CAP_WRITE);
    if (!entry) return {VERR_INVALID_HANDLE, 0, 0, 0};
    
    auto* file = static_cast<fs::OpenFile*>(entry->object);
    
    i32 n = fs::write(file, reinterpret_cast<void*>(data), len);
    if (n < 0) return {n, 0, 0, 0};
    
    return {VOK, static_cast<u64>(n), 0, 0};
}

// FsReadDir(dir, buf, max_entries) -> num_entries
SyscallResult sys_fs_readdir(proc::Viper* v, u32 h, u64 buf, u64 max) {
    auto* entry = v->cap_table->get_with_rights(h, cap::Kind::Directory, cap::CAP_LIST);
    if (!entry) return {VERR_INVALID_HANDLE, 0, 0, 0};
    
    auto* dir = static_cast<fs::OpenFile*>(entry->object);
    
    i32 n = fs::readdir(dir, reinterpret_cast<fs::DirInfo*>(buf), max);
    if (n < 0) return {n, 0, 0, 0};
    
    return {VOK, static_cast<u64>(n), 0, 0};
}

// FsClose(handle)
SyscallResult sys_fs_close(proc::Viper* v, u32 h) {
    auto* entry = v->cap_table->get(h);
    if (!entry) return {VERR_INVALID_HANDLE, 0, 0, 0};
    
    if (entry->kind != cap::Kind::File && entry->kind != cap::Kind::Directory) {
        return {VERR_INVALID_HANDLE, 0, 0, 0};
    }
    
    auto* file = static_cast<fs::OpenFile*>(entry->object);
    fs::close(file);
    v->cap_table->remove(h);
    
    return {VOK, 0, 0, 0};
}

} // namespace viper::syscall
```

---

## Milestone 7: vinit from Disk

**Duration:** Week 9  
**Deliverable:** Boot process loads from SYS:

### 7.1 Disk Boot

```cpp
// kernel/init/disk_init.cpp
#include "../viper/viper.hpp"
#include "../viper/loader.hpp"
#include "../fs/vfs.hpp"
#include "../fs/viperfs/viperfs.hpp"
#include "../lib/format.hpp"

namespace viper::init {

void boot_from_disk() {
    kprintf("[BOOT] Loading vinit from disk...\n");
    
    // Open root directory
    auto root_result = fs::open_root();
    if (!root_result.is_ok()) {
        panic("Failed to open root directory");
    }
    auto* root = root_result.unwrap();
    
    // Navigate to SYS:viper/vinit.vpr
    auto viper_result = fs::open(root->inode_num, "viper", 5, fs::VFS_READ);
    if (!viper_result.is_ok()) {
        panic("Failed to open SYS:viper directory");
    }
    auto* viper_dir = viper_result.unwrap();
    
    auto vinit_result = fs::open(viper_dir->inode_num, "vinit.vpr", 9, fs::VFS_READ);
    if (!vinit_result.is_ok()) {
        panic("Failed to open SYS:viper/vinit.vpr");
    }
    auto* vinit_file = vinit_result.unwrap();
    
    // Read executable into memory
    fs::FileInfo info;
    fs::stat(vinit_file, &info);
    
    u8* exe_data = new u8[info.size];
    i32 n = fs::read(vinit_file, exe_data, info.size);
    if (n != static_cast<i32>(info.size)) {
        panic("Failed to read vinit.vpr");
    }
    
    fs::close(vinit_file);
    fs::close(viper_dir);
    fs::close(root);
    
    // Create vinit Viper
    auto viper_create = proc::create(nullptr, "vinit");
    if (!viper_create.is_ok()) {
        panic("Failed to create vinit Viper");
    }
    auto* vinit = viper_create.unwrap();
    
    // Load executable
    auto entry = proc::load_executable(vinit, exe_data, info.size);
    if (!entry.is_ok()) {
        panic("Failed to load vinit executable");
    }
    
    delete[] exe_data;
    
    // Grant initial capabilities
    // - Root directory handle
    auto root2 = fs::open_root();
    vinit->cap_table->insert(root2.unwrap(), cap::Kind::Directory,
                             cap::CAP_READ | cap::CAP_LIST | cap::CAP_CREATE);
    
    // Create initial task
    auto task = sched::create_user_task(vinit, entry.unwrap(),
                                        VirtAddr{proc::USER_STACK_TOP}, 0);
    if (!task.is_ok()) {
        panic("Failed to create vinit task");
    }
    
    kprintf("[BOOT] vinit loaded, entering user space\n");
}

} // namespace viper::init
```

---

## Milestone 8: vsh Shell

**Duration:** Weeks 10-11  
**Deliverable:** Interactive command line

### 8.1 Shell Main (User Space)

```cpp
// user/vsh/vsh.cpp
#include "../lib/vsys.hpp"
#include "../lib/vio.hpp"
#include "parse.hpp"
#include "execute.hpp"
#include "builtins.hpp"
#include "assign.hpp"

namespace {
    char line_buffer[1024];
    char cwd_path[256] = "SYS:";
}

void print_prompt() {
    vsys::debug_print(cwd_path, strlen(cwd_path));
    vsys::debug_print("> ", 2);
}

int main() {
    vsys::debug_print("ViperOS Shell v0.1\n", 19);
    
    // Initialize assigns
    assign::init();
    assign::set("SYS", /* root dir handle from vinit */);
    assign::set("C", /* SYS:c directory handle */);
    
    while (true) {
        print_prompt();
        
        // Read line
        int len = vio::readline(line_buffer, sizeof(line_buffer));
        if (len < 0) break;  // EOF
        if (len == 0) continue;
        
        // Parse command
        Command cmd;
        if (!parse::parse_line(line_buffer, &cmd)) {
            vsys::debug_print("Parse error\n", 12);
            continue;
        }
        
        // Check builtins
        if (builtins::is_builtin(cmd.name)) {
            builtins::execute(cmd);
            continue;
        }
        
        // Execute external command
        int rc = execute::run(cmd);
        if (rc != 0) {
            // Print error
        }
    }
    
    return 0;
}
```

### 8.2 Assign System

```cpp
// user/vsh/assign.hpp
#pragma once

#include <stdint.h>

namespace assign {

void init();

// Set an assign (e.g., "SYS" -> dir_handle)
void set(const char* name, uint32_t handle);

// Get assign handle (returns 0 if not found)
uint32_t get(const char* name);

// Remove an assign
void remove(const char* name);

// List all assigns
void list();

// Resolve a path like "SYS:c/dir.vpr"
// Returns file handle, or 0 on error
uint32_t resolve_path(const char* path);

} // namespace assign
```

### 8.3 Command Parser

```cpp
// user/vsh/parse.hpp
#pragma once

struct Command {
    char* name;
    char* args[32];
    int argc;
    char* input_redirect;   // < file
    char* output_redirect;  // > file
    bool append;            // >> file
    bool background;        // &
};

namespace parse {

bool parse_line(char* line, Command* cmd);
void free_command(Command* cmd);

} // namespace parse
```

### 8.4 Built-in Commands

```cpp
// user/vsh/builtins.cpp
#include "builtins.hpp"
#include "assign.hpp"
#include "../lib/vsys.hpp"

namespace builtins {

bool is_builtin(const char* name) {
    const char* builtins[] = {
        "cd", "set", "unset", "alias", "history",
        "quit", "endshell", nullptr
    };
    
    for (int i = 0; builtins[i]; i++) {
        if (strcmp(name, builtins[i]) == 0) return true;
    }
    return false;
}

int execute(const Command& cmd) {
    if (strcmp(cmd.name, "cd") == 0) {
        return cmd_cd(cmd);
    }
    if (strcmp(cmd.name, "quit") == 0 || strcmp(cmd.name, "endshell") == 0) {
        vsys::viper_exit(cmd.argc > 1 ? atoi(cmd.args[1]) : 0);
    }
    // ... other builtins
    return 0;
}

int cmd_cd(const Command& cmd) {
    if (cmd.argc < 2) {
        // Print current directory
        return 0;
    }
    
    // Resolve new path
    uint32_t new_dir = assign::resolve_path(cmd.args[1]);
    if (new_dir == 0) {
        vsys::debug_print("Directory not found\n", 20);
        return 10;
    }
    
    // Update current directory
    // ...
    return 0;
}

} // namespace builtins
```

---

## Milestone 9: Commands & Polish

**Duration:** Week 12  
**Deliverable:** Core external commands

### 9.1 Dir Command

```cpp
// user/cmd/dir.cpp
#include "../lib/vsys.hpp"
#include "../lib/vio.hpp"

int main(int argc, char** argv) {
    uint32_t dir_handle;
    
    if (argc > 1) {
        dir_handle = resolve_path(argv[1]);
    } else {
        // Use current directory
        dir_handle = get_current_dir();
    }
    
    if (dir_handle == 0) {
        print("Directory not found\n");
        return 10;
    }
    
    // Read directory entries
    DirInfo entries[64];
    int n = vsys::fs_readdir(dir_handle, entries, 64);
    
    for (int i = 0; i < n; i++) {
        if (entries[i].kind == VKIND_DIRECTORY) {
            print_colored(entries[i].name, COLOR_WHITE);
            print(" (dir)\n");
        } else {
            print(entries[i].name);
            print("\n");
        }
    }
    
    return 0;
}
```

### 9.2 Type Command

```cpp
// user/cmd/type.cpp
#include "../lib/vsys.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        print("Usage: Type <filename>\n");
        return 5;
    }
    
    uint32_t file = resolve_and_open(argv[1], VFS_READ);
    if (file == 0) {
        print("File not found\n");
        return 10;
    }
    
    char buf[4096];
    int n;
    while ((n = vsys::io_read(file, buf, sizeof(buf))) > 0) {
        vsys::debug_print(buf, n);
    }
    
    vsys::fs_close(file);
    return 0;
}
```

### 9.3 Copy Command

```cpp
// user/cmd/copy.cpp
#include "../lib/vsys.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        print("Usage: Copy <source> <dest>\n");
        return 5;
    }
    
    uint32_t src = resolve_and_open(argv[1], VFS_READ);
    if (src == 0) {
        print("Source not found\n");
        return 10;
    }
    
    uint32_t dst = resolve_and_open(argv[2], VFS_WRITE | VFS_CREATE);
    if (dst == 0) {
        vsys::fs_close(src);
        print("Cannot create destination\n");
        return 10;
    }
    
    char buf[4096];
    int n;
    while ((n = vsys::io_read(src, buf, sizeof(buf))) > 0) {
        vsys::io_write(dst, buf, n);
    }
    
    vsys::fs_close(src);
    vsys::fs_close(dst);
    
    print("Copied\n");
    return 0;
}
```

---

## Weekly Schedule

| Week | Focus                     | Deliverables                |
|------|---------------------------|-----------------------------|
| 1    | virtio infrastructure     | Virtqueue, device discovery |
| 2    | virtio-blk                | Block read/write            |
| 3    | Block cache               | LRU cache, dirty tracking   |
| 4    | ViperFS superblock/inodes | Mount, read inodes          |
| 5    | ViperFS directories       | Lookup, readdir             |
| 6    | ViperFS write             | Create, write, delete       |
| 7    | VFS layer                 | Open file table, mount      |
| 8    | File syscalls             | FsOpen, IORead, IOWrite     |
| 9    | vinit from disk           | Boot from filesystem        |
| 10   | vsh parser                | Command line parsing        |
| 11   | vsh execution             | Builtins, external commands |
| 12   | Commands & polish         | Dir, List, Type, Copy       |

---

## Definition of Done (Phase 4)

- [ ] virtio-blk can read/write sectors
- [ ] ViperFS mounts and reads files
- [ ] ViperFS can create and write files
- [ ] VFS provides unified file operations
- [ ] File syscalls work from user space
- [ ] vinit loads from SYS:viper/vinit.vpr
- [ ] vsh displays `SYS:>` prompt
- [ ] Assigns (SYS:, C:) work correctly
- [ ] Dir, List, Type, Copy commands work
- [ ] Can navigate directories with Cd
- [ ] System stable for 30+ minutes of shell use

---

## Disk Image Creation

For testing, create a ViperFS disk image:

```bash
#!/bin/bash
# scripts/make-disk.sh

# Create 64MB disk image
dd if=/dev/zero of=build/disk.img bs=1M count=64

# Format with ViperFS (custom tool)
./tools/mkfs.viperfs build/disk.img "ViperOS"

# Copy system files
./tools/vipercopy build/disk.img SYS:viper/vinit.vpr user/vinit/vinit.vpr
./tools/vipercopy build/disk.img SYS:viper/vsh.vpr user/vsh/vsh.vpr
./tools/vipercopy build/disk.img SYS:c/dir.vpr user/cmd/dir.vpr
./tools/vipercopy build/disk.img SYS:c/type.vpr user/cmd/type.vpr
./tools/vipercopy build/disk.img SYS:c/copy.vpr user/cmd/copy.vpr
```

---

## Phase 5 Preview

Phase 5 (Input & Polish) adds interactivity:

1. **virtio-input** — Keyboard/mouse driver
2. **Line editing** — Backspace, cursor movement
3. **Command history** — Up/down arrow
4. **Tab completion** — Path and command completion
5. **More commands** — Search, Sort, Status, etc.
6. **Font system** — Loadable fonts

Phase 4's shell becomes truly interactive with keyboard input.

---

*"A shell prompt is where an OS becomes real to its users."*
