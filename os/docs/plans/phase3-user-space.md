# ViperOS Phase 3: User Space

## Detailed Implementation Plan (C++)

**Duration:** 12 weeks (Months 7-9)  
**Goal:** First user-space Viper executing code  
**Milestone:** "Hello World" printed from user space  
**Prerequisites:** Phase 2 complete (tasks, scheduler, channels, polling)

---

## Executive Summary

Phase 3 breaks through the kernel barrier. We create isolated user-space processes called Vipers, each with its own
address space and capability table. The kernel becomes a true microkernel, mediating all access to system resources
through capabilities.

Key components:

1. **Vipers** — Isolated processes with private address spaces (TTBR0)
2. **Address Spaces** — User page tables, ASID management, memory mapping
3. **Capability Tables** — Type-safe handle validation with generation counters
4. **KHeap** — Kernel-managed memory objects accessible from user space
5. **EL0/EL1 Transitions** — Proper user-mode exception handling
6. **vinit** — First user-space process, loaded by kernel at boot

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         User Space (EL0)                         │
├─────────────────────────────────────────────────────────────────┤
│   Viper A (ASID=1)               Viper B (ASID=2)               │
│   ┌────────────────────┐        ┌────────────────────┐         │
│   │ TTBR0_A            │        │ TTBR0_B            │         │
│   │ Code   0x10000     │        │ Code   0x10000     │         │
│   │ Data   0x10000000  │        │ Data   0x10000000  │         │
│   │ Heap   0x100000000 │        │ Heap   0x100000000 │         │
│   │ Stack  0x7FFFFFFF0000│      │ Stack  0x7FFFFFFF0000│       │
│   │                    │        │                    │         │
│   │ CapTable: [0..N]   │        │ CapTable: [0..M]   │         │
│   └────────────────────┘        └────────────────────┘         │
├─────────────────────────────────────────────────────────────────┤
│                          SVC #0                                  │
├─────────────────────────────────────────────────────────────────┤
│                       Kernel Space (EL1)                         │
│   TTBR1 (shared): 0xFFFF_0000_0000_0000+                        │
│   - Kernel code, HHDM, heap, task stacks                        │
└─────────────────────────────────────────────────────────────────┘
```

---

## Memory Layout

```
User Virtual Address Space (per-Viper, 48-bit):

0x0000_0000_0000_0000  ─┬─ NULL guard page (unmapped)
                        │
0x0000_0000_0001_0000  ─┼─ Code segment (.text) ─────── PROT_READ|EXEC
                        │
0x0000_0000_1000_0000  ─┼─ Data segment (.data,.bss) ── PROT_READ|WRITE
                        │
0x0000_0001_0000_0000  ─┼─ Heap base (grows up) ─────── PROT_READ|WRITE
                        │   KHeap objects mapped here
                        │
0x0000_7FFF_0000_0000  ─┼─ Stack region
                        │   Main stack at top, grows down
                        │
0x0000_7FFF_FFFF_0000  ─┴─ Initial SP (16-byte aligned)
```

---

## Project Structure

```
kernel/
├── viper/
│   ├── viper.cpp/.hpp           # VViper lifecycle
│   ├── address_space.cpp/.hpp   # User page tables
│   └── loader.cpp/.hpp          # Load executable into Viper
├── cap/
│   ├── table.cpp/.hpp           # Capability table
│   ├── handle.hpp               # Handle encoding (index + generation)
│   └── rights.hpp               # CAP_READ, CAP_WRITE, etc.
├── kheap/
│   ├── object.cpp/.hpp          # VKHeapDescriptor
│   ├── string.cpp/.hpp          # String objects
│   ├── array.cpp/.hpp           # Array objects
│   ├── blob.cpp/.hpp            # Blob objects
│   └── syscalls.cpp             # HeapAlloc, HeapRelease, etc.
├── syscall/
│   ├── user_entry.S             # EL0→EL1 trap entry
│   ├── user_return.S            # EL1→EL0 return (eret)
│   ├── viper_syscalls.cpp       # ViperSpawn, ViperExit
│   └── cap_syscalls.cpp         # CapDerive, CapQuery
├── arch/aarch64/
│   ├── asid.cpp/.hpp            # ASID allocator
│   └── user.hpp                 # TTBR0 switching, EL0 helpers
└── init/
    ├── vinit_blob.S             # Embedded vinit binary
    └── vinit.cpp                # Load and start vinit
```

---

## Milestones

| # | Milestone               | Duration    | Deliverable                       |
|---|-------------------------|-------------|-----------------------------------|
| 1 | VViper & Address Spaces | Weeks 1-2   | TTBR0 switching, user page tables |
| 2 | Capability Tables       | Weeks 3-4   | Proper handle validation          |
| 3 | EL0/EL1 Transitions     | Weeks 5-6   | User-mode entry/exit              |
| 4 | KHeap Objects           | Weeks 7-8   | Heap allocation syscalls          |
| 5 | Loader & vinit          | Weeks 9-10  | First user process                |
| 6 | Hello World Test        | Weeks 11-12 | End-to-end validation             |

---

## Milestone 1: VViper & Address Spaces

### 1.1 VViper Structure

```cpp
// kernel/viper/viper.hpp
#pragma once

#include "../lib/types.hpp"
#include "../sched/task.hpp"

namespace viper::cap { struct Table; }

namespace viper::proc {

enum class ViperState : u32 {
    Invalid = 0,
    Creating,
    Running,
    Exiting,
    Zombie,
};

struct Viper {
    // Identity
    u64 id;
    const char* name;
    
    // Address space
    PhysAddr ttbr0;         // Page table root
    u16 asid;               // TLB tag
    
    // Capabilities
    cap::Table* cap_table;
    
    // Tasks belonging to this Viper
    sched::Task* task_list;
    u32 task_count;
    
    // Process tree
    Viper* parent;
    Viper* first_child;
    Viper* next_sibling;
    
    // State
    ViperState state;
    i32 exit_code;
    
    // Heap tracking
    VirtAddr heap_start;
    VirtAddr heap_break;    // Current top of heap
    
    // Resource limits
    u64 memory_used;
    u64 memory_limit;
    
    // Global list linkage
    Viper* next_all;
};

// User address space layout
constexpr u64 USER_NULL_GUARD = 0x0000'0000'0000'0000ULL;
constexpr u64 USER_CODE_BASE  = 0x0000'0000'0001'0000ULL;  // 64KB
constexpr u64 USER_DATA_BASE  = 0x0000'0000'1000'0000ULL;  // 256MB
constexpr u64 USER_HEAP_BASE  = 0x0000'0001'0000'0000ULL;  // 4GB
constexpr u64 USER_STACK_TOP  = 0x0000'7FFF'FFFF'0000ULL;
constexpr u64 USER_STACK_SIZE = 1 * 1024 * 1024;           // 1MB

// Default limits
constexpr u64 DEFAULT_MEMORY_LIMIT = 64 * 1024 * 1024;     // 64MB
constexpr u32 DEFAULT_HANDLE_LIMIT = 1024;

// Viper management
void init();
Result<Viper*, i32> create(Viper* parent, const char* name);
void destroy(Viper* v);
Viper* current();
void set_current(Viper* v);

// Memory operations
Result<VirtAddr, i32> map_user(Viper* v, VirtAddr hint, usize size, u32 prot);
void unmap_user(Viper* v, VirtAddr addr, usize size);
Result<VirtAddr, i32> sbrk(Viper* v, isize increment);

} // namespace viper::proc
```

### 1.2 Address Space Class

```cpp
// kernel/viper/address_space.hpp
#pragma once

#include "../lib/types.hpp"

namespace viper::proc {

// Protection flags
constexpr u32 PROT_NONE  = 0;
constexpr u32 PROT_READ  = 1 << 0;
constexpr u32 PROT_WRITE = 1 << 1;
constexpr u32 PROT_EXEC  = 1 << 2;

class AddressSpace {
public:
    bool init();
    void destroy();
    
    // Map physical pages to virtual address
    bool map(VirtAddr virt, PhysAddr phys, usize size, u32 prot);
    
    // Unmap virtual address range
    void unmap(VirtAddr virt, usize size);
    
    // Allocate physical pages and map them
    Result<VirtAddr, i32> alloc_map(VirtAddr virt, usize size, u32 prot);
    
    // Translate virtual to physical
    Optional<PhysAddr> translate(VirtAddr virt);
    
    PhysAddr root() const { return root_; }
    u16 asid() const { return asid_; }
    
private:
    PhysAddr root_{0};
    u16 asid_{0};
    
    u64* get_or_alloc_table(u64* parent, int index);
};

// ASID management
void asid_init();
u16 asid_alloc();
void asid_free(u16 asid);

} // namespace viper::proc
```

### 1.3 Address Space Implementation

```cpp
// kernel/viper/address_space.cpp
#include "address_space.hpp"
#include "../mm/pmm.hpp"
#include "../lib/string.hpp"
#include "../include/error.hpp"

namespace viper::proc {

namespace {
    // AArch64 page table entry bits
    constexpr u64 PTE_VALID   = 1ULL << 0;
    constexpr u64 PTE_TABLE   = 1ULL << 1;   // Non-leaf
    constexpr u64 PTE_PAGE    = 1ULL << 1;   // Leaf (L3)
    constexpr u64 PTE_AF      = 1ULL << 10;  // Access flag
    constexpr u64 PTE_SH      = 3ULL << 8;   // Inner shareable
    constexpr u64 PTE_AP_EL0  = 1ULL << 6;   // EL0 accessible
    constexpr u64 PTE_AP_RO   = 2ULL << 6;   // Read-only
    constexpr u64 PTE_UXN     = 1ULL << 54;  // User no-execute
    constexpr u64 PTE_PXN     = 1ULL << 53;  // Privileged no-execute
    constexpr u64 PTE_ADDR    = 0x0000'FFFF'FFFF'F000ULL;
    
    // ASID state
    constexpr u16 MAX_ASID = 256;
    u64 asid_bitmap[4] = {};
    u16 asid_next = 1;
}

void asid_init() {
    memset(asid_bitmap, 0, sizeof(asid_bitmap));
    asid_next = 1;  // 0 reserved for kernel
}

u16 asid_alloc() {
    for (u16 i = 0; i < MAX_ASID; i++) {
        u16 asid = (asid_next + i) % MAX_ASID;
        if (asid == 0) continue;
        
        if (!(asid_bitmap[asid / 64] & (1ULL << (asid % 64)))) {
            asid_bitmap[asid / 64] |= (1ULL << (asid % 64));
            asid_next = (asid + 1) % MAX_ASID;
            return asid;
        }
    }
    return 0;
}

void asid_free(u16 asid) {
    if (asid > 0 && asid < MAX_ASID) {
        asid_bitmap[asid / 64] &= ~(1ULL << (asid % 64));
    }
}

bool AddressSpace::init() {
    asid_ = asid_alloc();
    if (asid_ == 0) return false;
    
    auto r = pmm::alloc_page();
    if (!r.is_ok()) {
        asid_free(asid_);
        return false;
    }
    
    root_ = r.unwrap();
    u64* table = pmm::phys_to_virt(root_).as_ptr<u64>();
    memset(table, 0, 4096);
    
    return true;
}

void AddressSpace::destroy() {
    // TODO: Walk and free all page tables
    if (root_.raw()) {
        pmm::free_page(root_);
    }
    if (asid_) {
        asid_free(asid_);
    }
}

u64* AddressSpace::get_or_alloc_table(u64* parent, int index) {
    if (!(parent[index] & PTE_VALID)) {
        auto r = pmm::alloc_page();
        if (!r.is_ok()) return nullptr;
        
        u64* child = pmm::phys_to_virt(r.unwrap()).as_ptr<u64>();
        memset(child, 0, 4096);
        parent[index] = r.unwrap().raw() | PTE_VALID | PTE_TABLE;
    }
    return pmm::phys_to_virt(PhysAddr{parent[index] & PTE_ADDR}).as_ptr<u64>();
}

bool AddressSpace::map(VirtAddr virt, PhysAddr phys, usize size, u32 prot) {
    u64* l0 = pmm::phys_to_virt(root_).as_ptr<u64>();
    usize pages = (size + 4095) / 4096;
    
    for (usize i = 0; i < pages; i++) {
        u64 va = virt.raw() + i * 4096;
        u64 pa = phys.raw() + i * 4096;
        
        int i0 = (va >> 39) & 0x1FF;
        int i1 = (va >> 30) & 0x1FF;
        int i2 = (va >> 21) & 0x1FF;
        int i3 = (va >> 12) & 0x1FF;
        
        u64* l1 = get_or_alloc_table(l0, i0);
        if (!l1) return false;
        u64* l2 = get_or_alloc_table(l1, i1);
        if (!l2) return false;
        u64* l3 = get_or_alloc_table(l2, i2);
        if (!l3) return false;
        
        u64 pte = pa | PTE_VALID | PTE_PAGE | PTE_AF | PTE_SH | PTE_AP_EL0;
        if (!(prot & PROT_WRITE)) pte |= PTE_AP_RO;
        if (!(prot & PROT_EXEC))  pte |= PTE_UXN | PTE_PXN;
        
        l3[i3] = pte;
    }
    
    // Invalidate TLB
    for (usize i = 0; i < pages; i++) {
        u64 va = virt.raw() + i * 4096;
        asm volatile("tlbi vale1is, %0" :: "r"((va >> 12) | (u64(asid_) << 48)));
    }
    asm volatile("dsb sy; isb");
    
    return true;
}

void AddressSpace::unmap(VirtAddr virt, usize size) {
    u64* l0 = pmm::phys_to_virt(root_).as_ptr<u64>();
    usize pages = (size + 4095) / 4096;
    
    for (usize i = 0; i < pages; i++) {
        u64 va = virt.raw() + i * 4096;
        
        int i0 = (va >> 39) & 0x1FF;
        int i1 = (va >> 30) & 0x1FF;
        int i2 = (va >> 21) & 0x1FF;
        int i3 = (va >> 12) & 0x1FF;
        
        if (!(l0[i0] & PTE_VALID)) continue;
        u64* l1 = pmm::phys_to_virt(PhysAddr{l0[i0] & PTE_ADDR}).as_ptr<u64>();
        
        if (!(l1[i1] & PTE_VALID)) continue;
        u64* l2 = pmm::phys_to_virt(PhysAddr{l1[i1] & PTE_ADDR}).as_ptr<u64>();
        
        if (!(l2[i2] & PTE_VALID)) continue;
        u64* l3 = pmm::phys_to_virt(PhysAddr{l2[i2] & PTE_ADDR}).as_ptr<u64>();
        
        l3[i3] = 0;
        asm volatile("tlbi vale1is, %0" :: "r"((va >> 12) | (u64(asid_) << 48)));
    }
    asm volatile("dsb sy; isb");
}

Result<VirtAddr, i32> AddressSpace::alloc_map(VirtAddr virt, usize size, u32 prot) {
    usize pages = (size + 4095) / 4096;
    
    auto r = pmm::alloc_pages(pages);
    if (!r.is_ok()) return Result<VirtAddr, i32>::failure(VERR_OUT_OF_MEMORY);
    
    // Zero pages
    memset(pmm::phys_to_virt(r.unwrap()).as_ptr<void>(), 0, pages * 4096);
    
    if (!map(virt, r.unwrap(), size, prot)) {
        pmm::free_pages(r.unwrap(), pages);
        return Result<VirtAddr, i32>::failure(VERR_OUT_OF_MEMORY);
    }
    
    return Result<VirtAddr, i32>::success(virt);
}

Optional<PhysAddr> AddressSpace::translate(VirtAddr virt) {
    u64* l0 = pmm::phys_to_virt(root_).as_ptr<u64>();
    u64 va = virt.raw();
    
    int i0 = (va >> 39) & 0x1FF;
    int i1 = (va >> 30) & 0x1FF;
    int i2 = (va >> 21) & 0x1FF;
    int i3 = (va >> 12) & 0x1FF;
    
    if (!(l0[i0] & PTE_VALID)) return Optional<PhysAddr>::none();
    u64* l1 = pmm::phys_to_virt(PhysAddr{l0[i0] & PTE_ADDR}).as_ptr<u64>();
    
    if (!(l1[i1] & PTE_VALID)) return Optional<PhysAddr>::none();
    u64* l2 = pmm::phys_to_virt(PhysAddr{l1[i1] & PTE_ADDR}).as_ptr<u64>();
    
    if (!(l2[i2] & PTE_VALID)) return Optional<PhysAddr>::none();
    u64* l3 = pmm::phys_to_virt(PhysAddr{l2[i2] & PTE_ADDR}).as_ptr<u64>();
    
    if (!(l3[i3] & PTE_VALID)) return Optional<PhysAddr>::none();
    
    return Optional<PhysAddr>::some(PhysAddr{(l3[i3] & PTE_ADDR) | (va & 0xFFF)});
}

} // namespace viper::proc
```

### 1.4 TTBR0 Switching

```cpp
// kernel/arch/aarch64/user.hpp
#pragma once

#include "../../lib/types.hpp"

namespace viper::arch {

// Switch user address space
inline void switch_user_space(PhysAddr ttbr0, u16 asid) {
    u64 val = ttbr0.raw() | (u64(asid) << 48);
    asm volatile(
        "msr ttbr0_el1, %0  \n"
        "isb                \n"
        :: "r"(val) : "memory"
    );
}

// Invalidate all TLB entries for an ASID
inline void tlb_flush_asid(u16 asid) {
    asm volatile(
        "tlbi aside1is, %0  \n"
        "dsb sy             \n"
        "isb                \n"
        :: "r"(u64(asid) << 48)
    );
}

} // namespace viper::arch
```

---

## Milestone 2: Capability Tables

### 2.1 Handle Encoding

```cpp
// kernel/cap/handle.hpp
#pragma once

#include "../lib/types.hpp"

namespace viper::cap {

// Handle = 24-bit index + 8-bit generation
// Generation detects use-after-free

using Handle = u32;

constexpr Handle HANDLE_INVALID = 0xFFFFFFFF;

constexpr u32 INDEX_MASK = 0x00FFFFFF;
constexpr u32 GEN_SHIFT  = 24;
constexpr u32 GEN_MASK   = 0xFF;

inline u32 handle_index(Handle h) { return h & INDEX_MASK; }
inline u8  handle_gen(Handle h)   { return (h >> GEN_SHIFT) & GEN_MASK; }
inline Handle make_handle(u32 index, u8 gen) {
    return (index & INDEX_MASK) | (u32(gen) << GEN_SHIFT);
}

} // namespace viper::cap
```

### 2.2 Rights

```cpp
// kernel/cap/rights.hpp
#pragma once

#include "../lib/types.hpp"

namespace viper::cap {

enum Rights : u32 {
    CAP_NONE     = 0,
    CAP_READ     = 1 << 0,
    CAP_WRITE    = 1 << 1,
    CAP_EXECUTE  = 1 << 2,
    CAP_LIST     = 1 << 3,
    CAP_CREATE   = 1 << 4,
    CAP_DELETE   = 1 << 5,
    CAP_DERIVE   = 1 << 6,
    CAP_TRANSFER = 1 << 7,
    CAP_SPAWN    = 1 << 8,
    
    CAP_ALL = 0xFFFFFFFF,
};

inline Rights operator|(Rights a, Rights b) {
    return static_cast<Rights>(static_cast<u32>(a) | static_cast<u32>(b));
}

inline Rights operator&(Rights a, Rights b) {
    return static_cast<Rights>(static_cast<u32>(a) & static_cast<u32>(b));
}

} // namespace viper::cap
```

### 2.3 Capability Table

```cpp
// kernel/cap/table.hpp
#pragma once

#include "../lib/types.hpp"
#include "handle.hpp"
#include "rights.hpp"

namespace viper::cap {

// Object kinds (from spec)
enum class Kind : u16 {
    Invalid   = 0,
    String    = 1,
    Array     = 2,
    Blob      = 3,
    Channel   = 16,
    Poll      = 17,
    Timer     = 18,
    Task      = 19,
    Viper     = 20,
    File      = 21,
    Directory = 22,
    Surface   = 23,
    Input     = 24,
};

struct Entry {
    void* object;       // Pointer to kernel object
    u32 rights;         // Rights bitmap
    Kind kind;          // Object type
    u8 generation;      // For ABA detection
    u8 _pad;
};

class Table {
public:
    static constexpr usize DEFAULT_CAPACITY = 256;
    
    bool init(usize capacity = DEFAULT_CAPACITY);
    void destroy();
    
    // Allocate a new handle for an object
    Result<Handle, i32> insert(void* object, Kind kind, Rights rights);
    
    // Look up and validate a handle
    Entry* get(Handle h);
    Entry* get_checked(Handle h, Kind expected_kind);
    Entry* get_with_rights(Handle h, Kind kind, Rights required);
    
    // Release a handle
    void remove(Handle h);
    
    // Derive a new handle with reduced rights
    Result<Handle, i32> derive(Handle h, Rights new_rights);
    
    usize count() const { return count_; }
    usize capacity() const { return capacity_; }
    
private:
    Entry* entries_ = nullptr;
    usize capacity_ = 0;
    usize count_ = 0;
    u32 free_head_ = 0;  // Free list head (index)
};

} // namespace viper::cap
```

### 2.4 Capability Table Implementation

```cpp
// kernel/cap/table.cpp
#include "table.hpp"
#include "../mm/heap.hpp"
#include "../include/error.hpp"
#include "../lib/string.hpp"

namespace viper::cap {

bool Table::init(usize capacity) {
    entries_ = static_cast<Entry*>(heap::alloc(capacity * sizeof(Entry)));
    if (!entries_) return false;
    
    memset(entries_, 0, capacity * sizeof(Entry));
    capacity_ = capacity;
    count_ = 0;
    
    // Build free list (use object pointer as next index)
    for (usize i = 0; i < capacity - 1; i++) {
        entries_[i].object = reinterpret_cast<void*>(i + 1);
        entries_[i].kind = Kind::Invalid;
    }
    entries_[capacity - 1].object = reinterpret_cast<void*>(0xFFFFFFFF);
    free_head_ = 0;
    
    return true;
}

void Table::destroy() {
    if (entries_) {
        heap::free(entries_);
        entries_ = nullptr;
    }
}

Result<Handle, i32> Table::insert(void* object, Kind kind, Rights rights) {
    if (free_head_ == 0xFFFFFFFF) {
        return Result<Handle, i32>::failure(VERR_OUT_OF_MEMORY);
    }
    
    u32 index = free_head_;
    Entry& e = entries_[index];
    
    // Advance free list
    free_head_ = static_cast<u32>(reinterpret_cast<uintptr_t>(e.object));
    
    // Fill entry
    e.object = object;
    e.kind = kind;
    e.rights = static_cast<u32>(rights);
    // Generation already set, incremented on remove
    
    count_++;
    
    return Result<Handle, i32>::success(make_handle(index, e.generation));
}

Entry* Table::get(Handle h) {
    u32 index = handle_index(h);
    u8 gen = handle_gen(h);
    
    if (index >= capacity_) return nullptr;
    
    Entry& e = entries_[index];
    if (e.kind == Kind::Invalid) return nullptr;
    if (e.generation != gen) return nullptr;
    
    return &e;
}

Entry* Table::get_checked(Handle h, Kind expected_kind) {
    Entry* e = get(h);
    if (!e) return nullptr;
    if (e->kind != expected_kind) return nullptr;
    return e;
}

Entry* Table::get_with_rights(Handle h, Kind kind, Rights required) {
    Entry* e = get_checked(h, kind);
    if (!e) return nullptr;
    if ((e->rights & static_cast<u32>(required)) != static_cast<u32>(required)) {
        return nullptr;
    }
    return e;
}

void Table::remove(Handle h) {
    u32 index = handle_index(h);
    if (index >= capacity_) return;
    
    Entry& e = entries_[index];
    if (e.kind == Kind::Invalid) return;
    
    // Increment generation for next use
    e.generation++;
    e.kind = Kind::Invalid;
    e.rights = 0;
    
    // Add to free list
    e.object = reinterpret_cast<void*>(free_head_);
    free_head_ = index;
    
    count_--;
}

Result<Handle, i32> Table::derive(Handle h, Rights new_rights) {
    Entry* e = get(h);
    if (!e) return Result<Handle, i32>::failure(VERR_INVALID_HANDLE);
    
    // Must have DERIVE right
    if (!(e->rights & static_cast<u32>(CAP_DERIVE))) {
        return Result<Handle, i32>::failure(VERR_PERMISSION);
    }
    
    // New rights can only remove, not add
    Rights actual = static_cast<Rights>(e->rights & static_cast<u32>(new_rights));
    
    return insert(e->object, e->kind, actual);
}

} // namespace viper::cap
```

---

## Milestone 3: User/Kernel Transitions

### 3.1 User-Mode Entry Assembly

```asm
// kernel/syscall/user_entry.S

.section .text

// Exception vector for sync exceptions from EL0 (AArch64)
// This handles SVC (syscalls)
.global el0_sync_entry
.align 7
el0_sync_entry:
    // Save all user registers to kernel stack
    sub     sp, sp, #288
    
    // Save x0-x30
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]
    
    // Save SP_EL0, ELR_EL1, SPSR_EL1
    mrs     x10, sp_el0
    mrs     x11, elr_el1
    mrs     x12, spsr_el1
    stp     x10, x11, [sp, #248]
    str     x12, [sp, #264]
    
    // Check exception class (ESR_EL1)
    mrs     x10, esr_el1
    lsr     x10, x10, #26
    cmp     x10, #0x15          // EC = 0x15 = SVC from AArch64
    b.ne    el0_sync_other
    
    // Syscall: x8 = syscall number, x0-x5 = args
    // Call C++ handler
    mov     x0, sp              // Pointer to saved registers
    bl      syscall_from_user
    
    // x0 now contains error code
    // Store it back for return
    str     x0, [sp, #0]
    
    b       el0_return

el0_sync_other:
    // Other exception (fault, etc.)
    mov     x0, sp
    bl      user_exception_handler
    b       el0_return


// Return to user mode
.global el0_return
el0_return:
    // Restore SP_EL0, ELR_EL1, SPSR_EL1
    ldp     x10, x11, [sp, #248]
    ldr     x12, [sp, #264]
    msr     sp_el0, x10
    msr     elr_el1, x11
    msr     spsr_el1, x12
    
    // Restore x0-x30
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30, [sp, #240]
    
    add     sp, sp, #288
    eret


// IRQ from EL0
.global el0_irq_entry
.align 7
el0_irq_entry:
    sub     sp, sp, #288
    // ... save registers same as above ...
    
    mov     x0, sp
    bl      irq_from_user
    
    b       el0_return
```

### 3.2 User Syscall Dispatcher

```cpp
// kernel/syscall/user_dispatch.cpp
#include "dispatch.hpp"
#include "../viper/viper.hpp"
#include "../cap/table.hpp"
#include "../include/error.hpp"

namespace viper::syscall {

struct UserRegs {
    u64 x[31];
    u64 sp;
    u64 pc;
    u64 pstate;
};

extern "C" void syscall_from_user(UserRegs* regs) {
    u64 syscall_num = regs->x[8];
    
    // Get current Viper
    auto* viper = proc::current();
    if (!viper) {
        regs->x[0] = VERR_INVALID_HANDLE;
        return;
    }
    
    // Dispatch based on syscall number
    SyscallResult result = dispatch_user(
        viper,
        syscall_num,
        regs->x[0], regs->x[1], regs->x[2],
        regs->x[3], regs->x[4], regs->x[5]
    );
    
    // Store results (X0=error, X1-X3=return values)
    regs->x[0] = result.error;
    regs->x[1] = result.result0;
    regs->x[2] = result.result1;
    regs->x[3] = result.result2;
}

extern "C" void user_exception_handler(UserRegs* regs) {
    u64 esr, far;
    asm volatile("mrs %0, esr_el1" : "=r"(esr));
    asm volatile("mrs %0, far_el1" : "=r"(far));
    
    u32 ec = (esr >> 26) & 0x3F;
    
    kprintf("User exception: EC=%d, FAR=0x%lx, PC=0x%lx\n", ec, far, regs->pc);
    
    // For now, terminate the Viper
    auto* viper = proc::current();
    if (viper) {
        viper->state = proc::ViperState::Exiting;
        viper->exit_code = -1;
    }
    
    // Switch to another task
    sched::scheduler::schedule();
}

} // namespace viper::syscall
```

### 3.3 Enter User Mode

```cpp
// kernel/arch/aarch64/user.hpp (additions)

// Enter user mode for the first time
[[noreturn]] inline void enter_user_mode(u64 entry, u64 stack_top, u64 arg) {
    asm volatile(
        "msr    sp_el0, %0      \n"
        "msr    elr_el1, %1     \n"
        "mov    x0, %2          \n"
        "mov    x1, xzr         \n"
        "mov    x2, xzr         \n"
        "mov    x3, xzr         \n"
        "msr    spsr_el1, xzr   \n"  // EL0, interrupts enabled
        "eret                   \n"
        :: "r"(stack_top), "r"(entry), "r"(arg)
        : "x0", "x1", "x2", "x3", "memory"
    );
    __builtin_unreachable();
}

} // namespace viper::arch
```

---

## Milestone 4: KHeap Objects

### 4.1 KHeap Descriptor

```cpp
// kernel/kheap/object.hpp
#pragma once

#include "../lib/types.hpp"
#include "../cap/table.hpp"

namespace viper::kheap {

constexpr u32 KHEAP_MAGIC = 0x44484B56;  // "VKHD"

struct Descriptor {
    u32 magic;
    cap::Kind kind;
    u16 flags;
    u64 refcount;
    u64 len;           // Current length
    u64 cap;           // Capacity
    u64 owner_viper;
    PhysAddr payload_phys;
    VirtAddr payload_user;  // Where mapped in user space (0 if not mapped)
};

// Flags
constexpr u16 KHEAP_MAPPED = 1 << 0;

// Object management
Result<Descriptor*, i32> alloc(cap::Kind kind, usize size);
void retain(Descriptor* d);
void release(Descriptor* d);

// Map into user space
Result<VirtAddr, i32> map_to_user(Descriptor* d, proc::Viper* v);
void unmap_from_user(Descriptor* d, proc::Viper* v);

// Get user pointer (returns 0 if not mapped)
VirtAddr get_user_buffer(Descriptor* d);

} // namespace viper::kheap
```

### 4.2 KHeap Implementation

```cpp
// kernel/kheap/object.cpp
#include "object.hpp"
#include "../mm/pmm.hpp"
#include "../mm/heap.hpp"
#include "../viper/viper.hpp"
#include "../lib/string.hpp"

namespace viper::kheap {

Result<Descriptor*, i32> alloc(cap::Kind kind, usize size) {
    // Allocate descriptor (in kernel heap)
    auto* d = new Descriptor;
    if (!d) return Result<Descriptor*, i32>::failure(VERR_OUT_OF_MEMORY);
    
    // Round up to page size
    usize pages = (size + 4095) / 4096;
    usize actual_size = pages * 4096;
    
    // Allocate physical pages for payload
    auto phys_result = pmm::alloc_pages(pages);
    if (!phys_result.is_ok()) {
        delete d;
        return Result<Descriptor*, i32>::failure(VERR_OUT_OF_MEMORY);
    }
    
    // Zero the memory
    memset(pmm::phys_to_virt(phys_result.unwrap()).as_ptr<void>(), 0, actual_size);
    
    // Initialize descriptor
    d->magic = KHEAP_MAGIC;
    d->kind = kind;
    d->flags = 0;
    d->refcount = 1;
    d->len = 0;
    d->cap = actual_size;
    d->owner_viper = 0;
    d->payload_phys = phys_result.unwrap();
    d->payload_user = VirtAddr{0};
    
    return Result<Descriptor*, i32>::success(d);
}

void retain(Descriptor* d) {
    if (d && d->magic == KHEAP_MAGIC) {
        d->refcount++;
    }
}

void release(Descriptor* d) {
    if (!d || d->magic != KHEAP_MAGIC) return;
    
    d->refcount--;
    if (d->refcount == 0) {
        // Free physical pages
        usize pages = (d->cap + 4095) / 4096;
        pmm::free_pages(d->payload_phys, pages);
        
        d->magic = 0;
        delete d;
    }
}

Result<VirtAddr, i32> map_to_user(Descriptor* d, proc::Viper* v) {
    if (!d || !v) return Result<VirtAddr, i32>::failure(VERR_INVALID_ARG);
    
    // Find free space in user heap region
    VirtAddr addr = v->heap_break;
    usize size = d->cap;
    
    // Map pages
    // TODO: Use Viper's address space
    // For now, simplified
    
    d->payload_user = addr;
    d->flags |= KHEAP_MAPPED;
    
    // Extend heap break
    v->heap_break = addr + size;
    
    return Result<VirtAddr, i32>::success(addr);
}

VirtAddr get_user_buffer(Descriptor* d) {
    if (!d || !(d->flags & KHEAP_MAPPED)) {
        return VirtAddr{0};
    }
    return d->payload_user;
}

} // namespace viper::kheap
```

### 4.3 Heap Syscalls

```cpp
// kernel/kheap/syscalls.cpp
#include "../syscall/dispatch.hpp"
#include "object.hpp"
#include "../viper/viper.hpp"
#include "../cap/table.hpp"

namespace viper::syscall {

// HeapAlloc(kind, size) -> handle
SyscallResult sys_heap_alloc(proc::Viper* v, u64 kind, u64 size) {
    auto result = kheap::alloc(static_cast<cap::Kind>(kind), size);
    if (!result.is_ok()) {
        return {result.get_error(), 0, 0, 0};
    }
    
    auto* desc = result.unwrap();
    desc->owner_viper = v->id;
    
    // Insert into capability table
    auto handle_result = v->cap_table->insert(
        desc,
        static_cast<cap::Kind>(kind),
        cap::CAP_READ | cap::CAP_WRITE | cap::CAP_DERIVE | cap::CAP_TRANSFER
    );
    
    if (!handle_result.is_ok()) {
        kheap::release(desc);
        return {handle_result.get_error(), 0, 0, 0};
    }
    
    return {VOK, handle_result.unwrap(), 0, 0};
}

// HeapRetain(handle)
SyscallResult sys_heap_retain(proc::Viper* v, cap::Handle h) {
    auto* entry = v->cap_table->get(h);
    if (!entry) return {VERR_INVALID_HANDLE, 0, 0, 0};
    
    auto* desc = static_cast<kheap::Descriptor*>(entry->object);
    kheap::retain(desc);
    
    return {VOK, 0, 0, 0};
}

// HeapRelease(handle)
SyscallResult sys_heap_release(proc::Viper* v, cap::Handle h) {
    auto* entry = v->cap_table->get(h);
    if (!entry) return {VERR_INVALID_HANDLE, 0, 0, 0};
    
    auto* desc = static_cast<kheap::Descriptor*>(entry->object);
    kheap::release(desc);
    
    // Remove from table
    v->cap_table->remove(h);
    
    return {VOK, 0, 0, 0};
}

// HeapGetLen(handle) -> length
SyscallResult sys_heap_get_len(proc::Viper* v, cap::Handle h) {
    auto* entry = v->cap_table->get(h);
    if (!entry) return {VERR_INVALID_HANDLE, 0, 0, 0};
    
    auto* desc = static_cast<kheap::Descriptor*>(entry->object);
    return {VOK, desc->len, 0, 0};
}

// HeapSetLen(handle, len)
SyscallResult sys_heap_set_len(proc::Viper* v, cap::Handle h, u64 len) {
    auto* entry = v->cap_table->get_with_rights(
        h, entry->kind, cap::CAP_WRITE
    );
    if (!entry) return {VERR_INVALID_HANDLE, 0, 0, 0};
    
    auto* desc = static_cast<kheap::Descriptor*>(entry->object);
    if (len > desc->cap) return {VERR_INVALID_ARG, 0, 0, 0};
    
    desc->len = len;
    return {VOK, 0, 0, 0};
}

// HeapGetBuffer(handle) -> user_ptr
SyscallResult sys_heap_get_buffer(proc::Viper* v, cap::Handle h) {
    auto* entry = v->cap_table->get(h);
    if (!entry) return {VERR_INVALID_HANDLE, 0, 0, 0};
    
    auto* desc = static_cast<kheap::Descriptor*>(entry->object);
    
    // Map if not already mapped
    if (!(desc->flags & kheap::KHEAP_MAPPED)) {
        auto result = kheap::map_to_user(desc, v);
        if (!result.is_ok()) {
            return {result.get_error(), 0, 0, 0};
        }
    }
    
    return {VOK, desc->payload_user.raw(), 0, 0};
}

} // namespace viper::syscall
```

---

## Milestone 5: Loader & vinit

### 5.1 Simple Executable Format

```cpp
// kernel/viper/loader.hpp
#pragma once

#include "../lib/types.hpp"

namespace viper::proc {

// Minimal executable header for Phase 3
// (Full VPR format in Phase 4)
struct ExeHeader {
    u32 magic;          // "VPR\0"
    u32 version;
    u32 entry_offset;   // Entry point offset from code start
    u32 code_size;
    u32 data_size;
    u32 bss_size;
};

constexpr u32 VPR_MAGIC = 0x00525056;  // "VPR\0"

// Load executable into Viper
Result<VirtAddr, i32> load_executable(
    Viper* v,
    const u8* data,
    usize size
);

} // namespace viper::proc
```

### 5.2 Loader Implementation

```cpp
// kernel/viper/loader.cpp
#include "loader.hpp"
#include "address_space.hpp"
#include "../mm/pmm.hpp"
#include "../lib/string.hpp"

namespace viper::proc {

Result<VirtAddr, i32> load_executable(Viper* v, const u8* data, usize size) {
    if (size < sizeof(ExeHeader)) {
        return Result<VirtAddr, i32>::failure(VERR_INVALID_ARG);
    }
    
    auto* hdr = reinterpret_cast<const ExeHeader*>(data);
    
    if (hdr->magic != VPR_MAGIC) {
        return Result<VirtAddr, i32>::failure(VERR_INVALID_ARG);
    }
    
    usize code_pages = (hdr->code_size + 4095) / 4096;
    usize data_pages = (hdr->data_size + hdr->bss_size + 4095) / 4096;
    
    // Get address space from Viper
    AddressSpace as;
    as.root_ = v->ttbr0;  // Simplified access
    as.asid_ = v->asid;
    
    // Allocate and map code section
    auto code_result = as.alloc_map(
        VirtAddr{USER_CODE_BASE},
        code_pages * 4096,
        PROT_READ | PROT_EXEC
    );
    if (!code_result.is_ok()) {
        return Result<VirtAddr, i32>::failure(code_result.get_error());
    }
    
    // Copy code (through HHDM)
    auto code_phys = as.translate(VirtAddr{USER_CODE_BASE});
    if (code_phys.has_value()) {
        void* code_ptr = pmm::phys_to_virt(*code_phys).as_ptr<void>();
        memcpy(code_ptr, data + sizeof(ExeHeader), hdr->code_size);
    }
    
    // Allocate and map data section
    if (data_pages > 0) {
        auto data_result = as.alloc_map(
            VirtAddr{USER_DATA_BASE},
            data_pages * 4096,
            PROT_READ | PROT_WRITE
        );
        if (!data_result.is_ok()) {
            // Cleanup code section
            as.unmap(VirtAddr{USER_CODE_BASE}, code_pages * 4096);
            return Result<VirtAddr, i32>::failure(data_result.get_error());
        }
        
        // Copy data
        auto data_phys = as.translate(VirtAddr{USER_DATA_BASE});
        if (data_phys.has_value()) {
            void* data_ptr = pmm::phys_to_virt(*data_phys).as_ptr<void>();
            memcpy(data_ptr, 
                   data + sizeof(ExeHeader) + hdr->code_size,
                   hdr->data_size);
            // BSS is already zeroed by alloc_map
        }
    }
    
    // Set up heap
    v->heap_start = VirtAddr{USER_HEAP_BASE};
    v->heap_break = VirtAddr{USER_HEAP_BASE};
    
    // Allocate stack
    auto stack_result = as.alloc_map(
        VirtAddr{USER_STACK_TOP - USER_STACK_SIZE},
        USER_STACK_SIZE,
        PROT_READ | PROT_WRITE
    );
    if (!stack_result.is_ok()) {
        // Cleanup
        as.unmap(VirtAddr{USER_CODE_BASE}, code_pages * 4096);
        as.unmap(VirtAddr{USER_DATA_BASE}, data_pages * 4096);
        return Result<VirtAddr, i32>::failure(stack_result.get_error());
    }
    
    // Return entry point
    VirtAddr entry{USER_CODE_BASE + hdr->entry_offset};
    return Result<VirtAddr, i32>::success(entry);
}

} // namespace viper::proc
```

### 5.3 vinit Setup

```cpp
// kernel/init/vinit.cpp
#include "../viper/viper.hpp"
#include "../viper/loader.hpp"
#include "../sched/task.hpp"
#include "../arch/aarch64/user.hpp"
#include "../lib/format.hpp"

// Embedded vinit binary (assembled from vinit_blob.S)
extern "C" {
    extern const u8 _binary_vinit_start[];
    extern const u8 _binary_vinit_end[];
}

namespace viper::init {

void start_vinit() {
    kprintf("Starting vinit...\n");
    
    // Create first Viper
    auto viper_result = proc::create(nullptr, "vinit");
    if (!viper_result.is_ok()) {
        panic("Failed to create vinit Viper");
    }
    
    auto* vinit = viper_result.unwrap();
    
    // Load executable
    usize vinit_size = _binary_vinit_end - _binary_vinit_start;
    auto entry_result = proc::load_executable(vinit, _binary_vinit_start, vinit_size);
    if (!entry_result.is_ok()) {
        panic("Failed to load vinit executable");
    }
    
    VirtAddr entry = entry_result.unwrap();
    kprintf("vinit loaded, entry at 0x%lx\n", entry.raw());
    
    // Create initial task
    // This task will enter user mode
    auto task_result = sched::create_user_task(
        vinit,
        entry,
        VirtAddr{proc::USER_STACK_TOP},
        0  // arg
    );
    
    if (!task_result.is_ok()) {
        panic("Failed to create vinit task");
    }
    
    kprintf("vinit task created, ID=%lu\n", task_result.unwrap()->id);
}

} // namespace viper::init
```

### 5.4 User Task Entry

```cpp
// kernel/sched/task.cpp (addition)

Result<Task*, i32> create_user_task(
    proc::Viper* viper,
    VirtAddr entry,
    VirtAddr stack_top,
    u64 arg
) {
    Task* task = allocate_task();
    if (!task) return Result<Task*, i32>::failure(VERR_OUT_OF_MEMORY);
    
    // Allocate kernel stack
    // ... (same as kernel task)
    
    task->id = next_task_id++;
    task->viper = viper;
    task->state = TaskState::Ready;
    task->flags = 0;  // Not TASK_FLAG_KERNEL
    task->time_slice = TIME_SLICE_DEFAULT;
    
    // Set up to enter user mode
    task->trap_frame.pc = entry.raw();
    task->trap_frame.sp = stack_top.raw();
    task->trap_frame.x[0] = arg;
    task->trap_frame.pstate = 0;  // EL0, interrupts enabled
    
    // Add to Viper's task list
    task->next_sibling = viper->task_list;
    viper->task_list = task;
    viper->task_count++;
    
    // Add to scheduler
    scheduler::enqueue(task);
    
    return Result<Task*, i32>::success(task);
}
```

---

## Milestone 6: Hello World Test

### 6.1 Minimal vinit (User-Space Assembly)

```asm
// vinit.S - First user-space program

.section .text
.global _start

_start:
    // Print "Hello from user space!"
    // Using DebugPrint syscall
    
    adr     x1, message
    mov     x2, #22             // length
    mov     x8, #0x00F0         // VSYS_DebugPrint
    svc     #0
    
    // Exit
    mov     x0, #0              // exit code
    mov     x8, #0x0101         // VSYS_ViperExit
    svc     #0

hang:
    wfi
    b       hang

.section .rodata
message:
    .ascii "Hello from user space!"
```

### 6.2 Build vinit Binary

```makefile
# Build vinit as raw binary embedded in kernel

vinit.o: vinit.S
	$(AS) -o $@ $<

vinit.bin: vinit.o
	$(OBJCOPY) -O binary $< $@

vinit_blob.o: vinit.bin
	$(OBJCOPY) -I binary -O elf64-littleaarch64 \
		--rename-section .data=.rodata \
		--redefine-sym _binary_vinit_bin_start=_binary_vinit_start \
		--redefine-sym _binary_vinit_bin_end=_binary_vinit_end \
		$< $@
```

### 6.3 Integration Test

```cpp
// kernel/test/user_test.cpp

namespace viper::test {

void run_user_space_test() {
    kprintf("\n=== User Space Test ===\n");
    
    // Start vinit
    init::start_vinit();
    
    // Let scheduler run
    // vinit will print message and exit
    
    kprintf("User space test initiated, scheduler running...\n");
}

} // namespace viper::test
```

### 6.4 Updated Kernel Main

```cpp
// kernel/main.cpp

void kernel_main(VBootInfo* info) {
    // ... Phase 1 & 2 initialization ...
    
    // === Phase 3: User Space ===
    kprintf("[BOOT] Initializing Viper subsystem...\n");
    proc::init();
    proc::asid_init();
    
    kprintf("[BOOT] Initializing capability system...\n");
    // Cap system is per-Viper, initialized on create
    
    kprintf("[BOOT] Starting vinit...\n");
    init::start_vinit();
    
    kprintf("Boot complete. Entering scheduler.\n");
    
    // Start scheduling - vinit will run in user mode
    sched::scheduler::run();
    
    // Never reached
}
```

---

## Weekly Schedule

| Week | Focus             | Deliverables                      |
|------|-------------------|-----------------------------------|
| 1    | VViper structure  | Viper creation, state machine     |
| 2    | Address spaces    | User page tables, ASID, mapping   |
| 3    | Capability table  | Handle encoding, table operations |
| 4    | Rights checking   | Derive, revoke, query             |
| 5    | User trap entry   | EL0→EL1 save/restore              |
| 6    | User trap return  | EL1→EL0 eret, syscall results     |
| 7    | KHeap descriptors | Allocation, refcounting           |
| 8    | KHeap mapping     | User-visible buffers              |
| 9    | Loader            | Parse header, map sections        |
| 10   | vinit             | Build, embed, load                |
| 11   | Integration       | End-to-end path                   |
| 12   | Testing           | Stability, edge cases             |

---

## Definition of Done (Phase 3)

- [ ] Viper creation allocates address space with unique ASID
- [ ] TTBR0 switches correctly between Vipers
- [ ] Capability table validates handles with generation
- [ ] SVC from EL0 routes to syscall dispatcher
- [ ] Syscall returns correct values in X0-X3
- [ ] KHeap objects can be allocated and accessed from user space
- [ ] vinit loads and executes in user mode
- [ ] "Hello from user space!" appears on console
- [ ] User task can exit cleanly
- [ ] System remains stable after user exit

---

## Phase 4 Preview

Phase 4 (Filesystem & Shell) builds on Phase 3:

1. **ViperFS** — On-disk filesystem, inodes, directories
2. **virtio-blk** — Block device driver
3. **vinit from disk** — Load from SYS:vinit instead of embedded
4. **vsh** — Interactive shell Viper
5. **Path resolution** — Assigns (SYS:, HOME:, etc.)
6. **ViperSpawn** — Launch new Vipers from executables

Phase 3's capability system gates all filesystem access.
Phase 3's address spaces isolate shell from kernel.
Phase 3's KHeap provides buffers for file I/O.

---

*"Crossing into user space is the moment the kernel becomes real."*
