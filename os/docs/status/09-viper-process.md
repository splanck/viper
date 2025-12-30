# Viper Process Model and Capabilities

**Status:** Complete user-space process execution with VMA tracking
**Location:** `kernel/viper/`, `kernel/cap/`
**SLOC:** ~1,100

## Overview

The Viper subsystem provides ViperOS's user-space process abstraction. Each "Viper" is an in-kernel process representation that owns an address space, a capability table, and a set of tasks. The capability system provides fine-grained access control to kernel objects.

---

## Components

### 1. Viper Process (`viper/viper.cpp`, `viper.hpp`)

**Status:** Complete process management

**Implemented:**
- Fixed-size process table (64 processes)
- Process lifecycle states
- Parent/child process hierarchy
- Per-process address space (TTBR0 + ASID)
- Per-process capability table
- **Per-process VMA list for demand paging**
- Heap tracking (break pointer)
- Resource accounting (memory usage)
- Global process list (for enumeration)
- Current process pointer
- **Process wait/exit/zombie lifecycle**
- **Wait queue for parent waiting on children**

**Process States:**
| State | Value | Description |
|-------|-------|-------------|
| Invalid | 0 | Slot unused |
| Creating | 1 | Initializing |
| Running | 2 | Active process |
| Exiting | 3 | Shutting down |
| Zombie | 4 | Waiting for reap |

**Viper Structure:**
| Field | Type | Description |
|-------|------|-------------|
| id | u64 | Unique process ID |
| name[32] | char | Human-readable name |
| ttbr0 | u64 | Page table root |
| asid | u16 | Address Space ID |
| cap_table | Table* | Capability table |
| task_list | Task* | Tasks in process |
| task_count | u32 | Number of tasks |
| parent | Viper* | Parent process |
| first_child | Viper* | Child list head |
| next_sibling | Viper* | Sibling link |
| state | ViperState | Lifecycle state |
| exit_code | i32 | Exit status |
| pgid | u64 | Process group ID |
| sid | u64 | Session ID |
| is_session_leader | bool | Session leader flag |
| heap_start | u64 | Heap base address |
| heap_break | u64 | Current break |
| memory_used | u64 | Usage accounting |
| memory_limit | u64 | Resource limit |
| vma_list | VmaList | VMA tracking for demand paging |
| wait_queue | WaitQueue | For waitpid blocking |

**API:**
| Function | Description |
|----------|-------------|
| `init()` | Initialize subsystem |
| `create(parent, name)` | Create process |
| `destroy(v)` | Destroy process |
| `current()` | Get current process (per-CPU aware) |
| `set_current(v)` | Set current process (per-CPU) |
| `find(id)` | Find by ID |
| `fork()` | Fork current process with COW |
| `get_address_space(v)` | Get AddressSpace |
| `current_cap_table()` | Get current cap table |
| `getpgid(pid)` | Get process group ID |
| `setpgid(pid, pgid)` | Set process group ID |
| `getsid(pid)` | Get session ID |
| `setsid()` | Create new session |

**Constants:**
- `MAX_VIPERS`: 64 processes
- `DEFAULT_MEMORY_LIMIT`: 64MB
- `DEFAULT_HANDLE_LIMIT`: 1024

---

### 2. Address Space (`viper/address_space.cpp`, `address_space.hpp`)

**Status:** Complete per-process virtual memory with COW support

**Implemented:**
- ASID allocator (256 ASIDs, 0 reserved)
- L0-L3 4-level page table management
- Page mapping with protection flags
- Physical page allocation and mapping
- Address translation (walk tables)
- TLB invalidation per-ASID and per-page
- TTBR0 switching for address space changes
- Kernel mapping copy into user tables
- **Recursive page table cleanup on destroy**
- **Copy-on-write page table cloning**
- **Protection flag manipulation for COW**

**Protection Flags:**
| Flag | Value | Description |
|------|-------|-------------|
| NONE | 0 | No access |
| READ | 1 | Readable |
| WRITE | 2 | Writable |
| EXEC | 4 | Executable |
| RW | 3 | Read/write |
| RX | 5 | Read/execute |
| RWX | 7 | All access |

**AddressSpace Class:**
| Method | Description |
|--------|-------------|
| `init()` | Allocate ASID and root table |
| `destroy()` | Free tables, release ASID |
| `map(virt, phys, size, prot)` | Map physical pages |
| `unmap(virt, size)` | Remove mappings |
| `alloc_map(virt, size, prot)` | Allocate and map |
| `translate(virt)` | Virtual to physical |
| `root()` | Get TTBR0 value |
| `asid()` | Get ASID |

**User Address Layout:**
```
0x0000'7FFF'FFFF'0000  ┌─────────────────────────┐
                       │     User Stack          │
                       │     (grows down)        │
                       │     ~1MB default        │
0x0000'0001'0000'0000  ├─────────────────────────┤ (4GB)
                       │     User Heap           │
                       │     (grows up)          │
0x0000'0000'C000'0000  ├─────────────────────────┤ (3GB)
                       │     User Data           │
0x0000'0000'8000'0000  ├─────────────────────────┤ (2GB)
                       │     User Code           │
0x0000'0000'0000'0000  └─────────────────────────┘
```

---

### 3. Capability Table (`cap/table.cpp`, `table.hpp`)

**Status:** Complete handle-based access control

**Implemented:**
- Fixed-capacity entry array (256 default)
- Free list for slot management
- Handle encoding (24-bit index + 8-bit generation)
- Object kind tagging
- Rights mask per entry
- Handle derivation with reduced rights
- Generation counter for stale handle detection
- Typed and rights-checked lookups

**Handle Format:**
```
┌────────────────────────────────────┐
│  Generation (8 bits) │ Index (24 bits) │
└────────────────────────────────────┘
```

**Entry Structure:**
| Field | Type | Description |
|-------|------|-------------|
| object | void* | Kernel object pointer |
| rights | u32 | Rights bitmask |
| parent_index | u32 | Parent capability index (for revocation propagation) |
| kind | Kind | Object type tag |
| generation | u8 | Stale handle detection |

**Object Kinds:**
| Kind | Value | Description |
|------|-------|-------------|
| Invalid | 0 | Unused slot |
| String | 1 | String object |
| Array | 2 | Array object |
| Blob | 3 | Binary blob |
| Channel | 16 | IPC channel |
| Poll | 17 | Poll set |
| Timer | 18 | Timer handle |
| Task | 19 | Task reference |
| Viper | 20 | Process reference |
| File | 21 | File handle |
| Directory | 22 | Directory handle |
| Surface | 23 | Graphics surface |
| Input | 24 | Input device |

**Table API:**
| Method | Description |
|--------|-------------|
| `init(capacity)` | Initialize table |
| `destroy()` | Free table memory |
| `insert(obj, kind, rights)` | Create handle |
| `get(h)` | Lookup entry |
| `get_checked(h, kind)` | Lookup with type check |
| `get_with_rights(h, kind, rights)` | Lookup with auth |
| `remove(h)` | Free handle (no propagation) |
| `revoke(h)` | Revoke handle and all derived handles |
| `derive(h, new_rights)` | Create derived handle with parent tracking |

---

### 4. Rights (`cap/rights.hpp`)

**Status:** Complete rights bitmask system

**Implemented:**
- Rights as bitmask flags
- Common right combinations
- Rights checking helper

**Rights Flags:**
| Right | Value | Description |
|-------|-------|-------------|
| CAP_NONE | 0 | No rights |
| CAP_READ | 1 | Read access |
| CAP_WRITE | 2 | Write access |
| CAP_EXECUTE | 4 | Execute code |
| CAP_LIST | 8 | List contents |
| CAP_CREATE | 16 | Create children |
| CAP_DELETE | 32 | Delete objects |
| CAP_DERIVE | 64 | Create derived caps |
| CAP_TRANSFER | 128 | Transfer via IPC |
| CAP_SPAWN | 256 | Spawn processes |
| CAP_TRAVERSE | 512 | Directory traversal |
| CAP_RW | 3 | Read + Write |
| CAP_RWX | 7 | Read + Write + Execute |
| CAP_ALL | 0xFFFFFFFF | All rights |

---

### 5. Handle Encoding (`cap/handle.hpp`)

**Status:** Complete handle utilities

**Implemented:**
- 32-bit opaque handle type
- Index extraction (bits 0-23)
- Generation extraction (bits 24-31)
- Handle construction helper
- Invalid handle sentinel

**Constants:**
- `HANDLE_INVALID`: 0xFFFFFFFF
- `INDEX_MASK`: 0x00FFFFFF (16M entries max)
- `GEN_SHIFT`: 24

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    User Space (EL0)                          │
│    syscall: cap_derive, cap_revoke, cap_query, cap_list      │
└──────────────────────────────┬──────────────────────────────┘
                               │ SVC
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                   Syscall Handler                            │
│              Uses viper::current_cap_table()                 │
└──────────────────────────────┬──────────────────────────────┘
                               │
        ┌──────────────────────┴──────────────────────┐
        ▼                                              ▼
┌─────────────────────┐                    ┌─────────────────────┐
│       Viper         │                    │   Capability Table  │
│  (Process Control   │◄──────────────────►│   (256 entries)     │
│   Block)            │   cap_table        │                     │
│                     │                    │ ┌─────────────────┐ │
│ • id, name          │                    │ │ Entry 0         │ │
│ • ttbr0, asid       │                    │ │ object, rights  │ │
│ • heap_break        │                    │ │ kind, gen       │ │
│ • parent/children   │                    │ ├─────────────────┤ │
│ • task_list         │                    │ │ Entry 1         │ │
└──────────┬──────────┘                    │ │ ...             │ │
           │                               │ └─────────────────┘ │
           ▼                               └─────────────────────┘
┌─────────────────────┐
│   Address Space     │
│  (TTBR0 + ASID)     │
│                     │
│ • L0-L3 page tables │
│ • map/unmap pages   │
│ • alloc_map         │
└─────────────────────┘
```

---

## Capability Derivation

Capabilities support least-privilege via derivation:

```
Original Handle:
  rights = CAP_READ | CAP_WRITE | CAP_DERIVE

derive(h, CAP_READ | CAP_DERIVE):
  new_rights = original_rights & requested_rights
             = (CAP_READ | CAP_WRITE | CAP_DERIVE) & (CAP_READ | CAP_DERIVE)
             = CAP_READ | CAP_DERIVE

Result: New handle with read-only access (plus ability to further derive)
```

---

## Syscall Interface

Capability syscalls:

| Syscall | Number | Description |
|---------|--------|-------------|
| cap_derive | 0x70 | Derive with reduced rights |
| cap_revoke | 0x71 | Revoke capability |
| cap_query | 0x72 | Query capability info |
| cap_list | 0x73 | List all capabilities |

Process syscalls:

| Syscall | Number | Description |
|---------|--------|-------------|
| fork | 0x0B | Fork process with COW |
| getpid | 0xA0 | Get process ID |
| getppid | 0xA1 | Get parent process ID |
| getpgid | 0xA2 | Get process group ID |
| setpgid | 0xA3 | Set process group ID |
| getsid | 0xA4 | Get session ID |
| setsid | 0xA5 | Create new session |

Handle-based file syscalls:

| Syscall | Number | Description |
|---------|--------|-------------|
| fs_open_root | 0x80 | Get root directory handle |
| fs_open | 0x81 | Open relative to handle |
| io_read | 0x82 | Read from file handle |
| io_write | 0x83 | Write to file handle |
| io_seek | 0x84 | Seek in file handle |
| fs_readdir | 0x85 | Read directory entry |
| fs_close | 0x86 | Close handle |
| fs_rewinddir | 0x87 | Reset directory |

---

## Files

| File | Lines | Description |
|------|-------|-------------|
| `viper/viper.cpp` | ~717 | Process management + wait/exit |
| `viper/viper.hpp` | ~342 | Process interface |
| `viper/address_space.cpp` | ~640 | Address space impl + COW |
| `viper/address_space.hpp` | ~357 | Address space interface |
| `cap/table.cpp` | ~202 | Capability table |
| `cap/table.hpp` | ~230 | Table interface |
| `cap/rights.hpp` | ~104 | Rights definitions |
| `cap/handle.hpp` | ~73 | Handle encoding |

---

## Priority Recommendations

1. ~~**High:** Add per-CPU current process for SMP~~ ✅ **Completed** - Per-CPU current_viper in CpuData
2. ~~**Medium:** Implement process groups/sessions~~ ✅ **Completed** - pgid, sid, is_session_leader fields + syscalls
3. ~~**Medium:** Add capability revocation propagation~~ ✅ **Completed** - parent_index tracking + recursive revoke()
4. ~~**Low:** Add fork() syscall using COW~~ ✅ **Completed** - SYS_FORK syscall (0x0B)

## Recent Additions

- **Per-CPU current process**: CpuData now has current_viper field for SMP support
- **Process groups/sessions**: Added pgid, sid, is_session_leader fields; syscalls getpid, getppid, getpgid, setpgid, getsid, setsid (0xA0-0xA5)
- **Capability revocation propagation**: Entry now has parent_index field; revoke() recursively invalidates derived handles
- **Fork syscall**: SYS_FORK (0x0B) creates child process with COW page sharing
