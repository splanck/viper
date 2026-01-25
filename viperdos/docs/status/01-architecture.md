# Architecture Subsystem (AArch64)

**Status:** Complete for QEMU virt platform (hybrid kernel) with UEFI boot
**Location:** `kernel/arch/aarch64/`
**SLOC:** ~3,600

## Overview

The architecture subsystem provides low-level AArch64 support for ViperDOS, targeting the QEMU `virt`
machine. The hybrid kernel provides filesystem, networking, and TLS directly in the kernel, with only display
servers (consoled, displayd) running in user space.

**Build Configuration:**

- `VIPER_KERNEL_ENABLE_FS=1` (kernel filesystem)
- `VIPER_KERNEL_ENABLE_NET=1` (kernel networking)
- `VIPER_KERNEL_ENABLE_TLS=1` (kernel TLS 1.3)

---

## Components

### 1. Boot (`boot.S`)

**Status:** Complete with multicore support and dual boot modes

**Implemented:**

- Entry point `_start` for kernel boot
- Stack setup (16KB kernel stack for boot CPU)
- BSS section zeroing
- Jump to `kernel_main` C++ entry point
- **Dual Boot Modes:**
    - UEFI boot via VBoot bootloader (receives `VBootInfo*` in x0)
    - Direct QEMU `-kernel` mode (receives DTB pointer in x0)
- **FPU/SIMD Access**: CPACR_EL1.FPEN enabled for EL1 and EL0
- **Secondary CPU Entry**: `secondary_entry` for PSCI-booted CPUs
- **Per-CPU Stacks**: 16KB stack per CPU (4 CPUs supported)
- **Alignment Check Disabled**: Allows unaligned access for network structures
- **Boot Parameter Detection**: Detects VBoot magic vs DTB pointer

**Not Implemented:**

- EL2/EL3 to EL1 transition (relies on QEMU or VBoot bootloader)

---

### 2. MMU (`mmu.cpp`, `mmu.hpp`)

**Status:** Complete with 4-level page tables and per-process address spaces

**Implemented:**

- 4KB page granule configuration
- 48-bit virtual address space (TCR_EL1.T0SZ = 16)
- MAIR_EL1 configuration:
    - Attr0: Device-nGnRnE (for MMIO)
    - Attr1: Normal Write-Back Write-Allocate (for RAM)
    - Attr2: Normal Non-cacheable
- Identity mapping of first 2GB using 1GB block descriptors:
    - `0x00000000-0x3FFFFFFF`: Device memory (MMIO region)
    - `0x40000000-0x7FFFFFFF`: Normal memory (RAM on QEMU virt)
- TTBR0_EL1 configuration for user/kernel tables
- Full 4-level page table management (L0-L3) via VMM
- Per-process address spaces with ASID isolation
- TLB invalidation (per-ASID and per-page)
- Data and instruction cache enablement
- **Demand Paging**: VMA-based fault handling for heap/stack
- **Copy-on-Write**: Page sharing with COW fault handling
- **User Fault Recovery**: Graceful task termination on invalid access

**Not Implemented:**

- TTBR1_EL1 for kernel higher-half mapping
- Kernel Address Space Layout Randomization (KASLR)

---

### 3. GIC - Generic Interrupt Controller (`gic.cpp`, `gic.hpp`)

**Status:** Complete with GICv2/GICv3 dual support

**Implemented:**

- **Automatic Version Detection**: Reads GICD_PIDR2 to detect GICv2 or GICv3
- GIC Distributor (GICD) initialization at `0x08000000`
- **GICv2 Support:**
    - CPU Interface (GICC) at `0x08010000`
    - Memory-mapped IAR/EOIR for interrupt handling
- **GICv3 Support:**
    - Redistributor (GICR) at `0x080A0000` (per-CPU, 128KB each)
    - System register interface (ICC_* registers)
    - Affinity-based interrupt routing
    - Redistributor wake-up sequence
- Interrupt enable/disable per IRQ
- Priority configuration (default 0xA0)
- Level-triggered interrupt configuration
- IRQ handler registration (callback-based)
- IRQ acknowledgment and EOI (End of Interrupt)
- Spurious interrupt detection (IRQ 1020+)
- **SGI Support**: Software Generated Interrupts for IPI
- **User-space IRQ registration**: For display server drivers

**Not Implemented:**

- Interrupt priority preemption
- FIQ handling (currently unused)
- MSI (Message Signaled Interrupts)

---

### 4. Timer (`timer.cpp`, `timer.hpp`)

**Status:** Complete with high-resolution support

**Implemented:**

- ARM architected timer (EL1 physical timer)
- 1000 Hz tick rate (1ms resolution) for scheduling
- Timer frequency detection via CNTFRQ_EL0
- Compare value programming (CNTP_CVAL_EL0)
- Timer control (CNTP_CTL_EL0)
- Global tick counter
- **High-Resolution Time Functions:**
    - `now()` - Raw counter timestamp (16ns resolution on QEMU)
    - `get_ns()` / `get_us()` / `get_ms()` - Precise time since boot
    - `ticks_to_ns()` / `ns_to_ticks()` - Conversion with 128-bit precision
- **High-Resolution Delays:**
    - `delay_ns()` / `delay_us()` - Busy-wait for short delays
    - `delay_ms()` - WFI-based for power efficiency
    - `wait_until()` - Wait for specific timestamp
- **One-Shot Timer Support:**
    - `schedule_oneshot()` - Schedule callback at deadline
    - `cancel_oneshot()` - Cancel scheduled timer
- **Per-CPU Timer**: Secondary CPUs initialize their own timer
- **Timer Wheel**: O(1) timeout management

**Not Implemented:**

- Virtual timer support
- Watchdog timer

---

### 5. Exceptions (`exceptions.cpp`, `exceptions.hpp`, `exceptions.S`)

**Status:** Comprehensive exception handling with syscall dispatch

**Implemented:**

- Exception vector table installation (VBAR_EL1)
- Exception frame save/restore (all GPRs, SP, LR, ELR, SPSR, ESR, FAR)
- Kernel-mode exception handling:
    - Synchronous exceptions (data abort, instruction abort, etc.)
    - IRQ routing to GIC handler
    - FIQ (logged but unused)
    - SError (system error) with panic
- User-mode (EL0) exception handling:
    - SVC (syscall) dispatch
    - **Data/instruction abort with VMA-based demand paging**
    - **COW fault handling for shared pages**
    - **Graceful task termination on invalid access**
    - IRQ handling from user mode
- Exception class decoding for diagnostics
- Full register dump on panic

---

## Syscall Table (~90 syscalls)

The kernel provides syscalls for all services including filesystem, networking, and TLS.

### Task/Process (0x00-0x0F)

| Number | Name              | Description             |
|--------|-------------------|-------------------------|
| 0x00   | task_yield        | Yield CPU to scheduler  |
| 0x01   | task_exit         | Terminate task          |
| 0x02   | task_current      | Get current task ID     |
| 0x03   | task_spawn        | Spawn new process       |
| 0x05   | task_list         | List running tasks      |
| 0x06   | task_set_priority | Set task priority       |
| 0x07   | task_get_priority | Get task priority       |
| 0x08   | wait              | Wait for any child      |
| 0x09   | waitpid           | Wait for specific child |
| 0x0A   | sbrk              | Adjust program break    |
| 0x0B   | fork              | Fork process            |
| 0x0C   | task_spawn_shm    | Spawn from SHM          |

### IPC Channels (0x10-0x1F)

| Number | Name           | Description          |
|--------|----------------|----------------------|
| 0x10   | channel_create | Create IPC channel   |
| 0x11   | channel_send   | Send on channel      |
| 0x12   | channel_recv   | Receive from channel |
| 0x13   | channel_close  | Close channel        |

### Polling (0x20-0x2F)

| Number | Name        | Description          |
|--------|-------------|----------------------|
| 0x20   | poll_create | Create poll set      |
| 0x21   | poll_add    | Add to poll set      |
| 0x22   | poll_remove | Remove from poll set |
| 0x23   | poll_wait   | Wait on poll set     |

### Timer (0x30-0x3F)

| Number | Name     | Description           |
|--------|----------|-----------------------|
| 0x30   | time_now | Get current time (ms) |
| 0x31   | sleep    | Sleep for duration    |

### File I/O (0x40-0x4F)

| Number | Name  | Description                     |
|--------|-------|---------------------------------|
| 0x40   | open  | Open file (path)                |
| 0x41   | close | Close file descriptor           |
| 0x42   | read  | Read from fd                    |
| 0x43   | write | Write to fd                     |
| 0x44   | lseek | Seek in file                    |
| 0x45   | stat  | Get file info (path)            |
| 0x46   | fstat | Get file info (fd)              |
| 0x47   | dup   | Duplicate fd                    |
| 0x48   | dup2  | Duplicate fd to specific number |

### Networking (0x50-0x5F)

| Number | Name           | Description       |
|--------|----------------|-------------------|
| 0x50   | socket_create  | Create TCP socket |
| 0x51   | socket_connect | Connect to server |
| 0x52   | socket_send    | Send data         |
| 0x53   | socket_recv    | Receive data      |
| 0x54   | socket_close   | Close socket      |
| 0x55   | dns_resolve    | Resolve hostname  |

### Directory/FS (0x60-0x6F)

| Number | Name     | Description              |
|--------|----------|--------------------------|
| 0x60   | readdir  | Read directory entries   |
| 0x61   | mkdir    | Create directory         |
| 0x62   | rmdir    | Remove directory         |
| 0x63   | unlink   | Delete file              |
| 0x64   | rename   | Rename file/directory    |
| 0x65   | symlink  | Create symbolic link     |
| 0x66   | readlink | Read symlink target      |
| 0x67   | getcwd   | Get current directory    |
| 0x68   | chdir    | Change current directory |

### Capabilities (0x70-0x7F)

| Number | Name       | Description                |
|--------|------------|----------------------------|
| 0x70   | cap_derive | Derive with reduced rights |
| 0x71   | cap_revoke | Revoke capability          |
| 0x72   | cap_query  | Query capability info      |
| 0x73   | cap_list   | List all capabilities      |

### Handle-based FS (0x80-0x8F)

| Number | Name          | Description                 |
|--------|---------------|-----------------------------|
| 0x80   | fs_open_root  | Get root directory handle   |
| 0x81   | fs_open       | Open relative to handle     |
| 0x82   | io_read       | Read from file handle       |
| 0x83   | io_write      | Write to file handle        |
| 0x84   | io_seek       | Seek in file handle         |
| 0x85   | fs_read_dir   | Read directory entry        |
| 0x86   | fs_close      | Close handle                |
| 0x87   | fs_rewind_dir | Reset directory enumeration |

### Signals (0x90-0x9F)

| Number | Name        | Description                |
|--------|-------------|----------------------------|
| 0x90   | sigaction   | Set signal handler         |
| 0x91   | sigprocmask | Set/get blocked signals    |
| 0x92   | sigreturn   | Return from signal handler |
| 0x93   | kill        | Send signal to task        |
| 0x94   | sigpending  | Get pending signals        |

### Process Info (0xA0-0xAF)

| Number | Name     | Description           |
|--------|----------|-----------------------|
| 0xA0   | getpid   | Get process ID        |
| 0xA1   | getppid  | Get parent process ID |
| 0xA2   | getpgid  | Get process group     |
| 0xA3   | setpgid  | Set process group     |
| 0xA4   | getsid   | Get session ID        |
| 0xA5   | setsid   | Create new session    |
| 0xA6   | get_args | Get process arguments |

### Assigns (0xC0-0xCF)

| Number | Name           | Description           |
|--------|----------------|-----------------------|
| 0xC0   | assign_set     | Create logical assign |
| 0xC1   | assign_get     | Get assign handle     |
| 0xC2   | assign_remove  | Remove assign         |
| 0xC3   | assign_list    | List all assigns      |
| 0xC4   | assign_resolve | Resolve assign path   |

### TLS (0xD0-0xDF)

| Number | Name          | Description            |
|--------|---------------|------------------------|
| 0xD0   | tls_create    | Create TLS session     |
| 0xD1   | tls_handshake | Perform TLS handshake  |
| 0xD2   | tls_send      | Send encrypted data    |
| 0xD3   | tls_recv      | Receive decrypted data |
| 0xD4   | tls_close     | Close TLS session      |
| 0xD5   | tls_info      | Get session info       |

### System Info (0xE0-0xEF)

| Number | Name        | Description            |
|--------|-------------|------------------------|
| 0xE0   | mem_info    | Get memory statistics  |
| 0xE1   | net_stats   | Get network statistics |
| 0xE2   | ping        | ICMP ping              |
| 0xE3   | device_list | List devices           |

### Console (0xF0-0xFF)

| Number | Name        | Description             |
|--------|-------------|-------------------------|
| 0xF0   | debug_print | Print string to console |
| 0xF1   | getchar     | Read character          |
| 0xF2   | putchar     | Write character         |
| 0xF3   | uptime      | Get system uptime       |

### Device Primitives (0x100-0x10F)

| Number | Name           | Description            |
|--------|----------------|------------------------|
| 0x100  | map_device     | Map device MMIO region |
| 0x101  | irq_register   | Register for IRQ       |
| 0x102  | irq_wait       | Wait for IRQ           |
| 0x103  | irq_ack        | Acknowledge IRQ        |
| 0x104  | dma_alloc      | Allocate DMA memory    |
| 0x105  | dma_free       | Free DMA memory        |
| 0x106  | virt_to_phys   | Get physical address   |
| 0x107  | device_enum    | Enumerate devices      |
| 0x108  | irq_unregister | Unregister IRQ         |

### Shared Memory (0x110-0x11F)

| Number | Name       | Description          |
|--------|------------|----------------------|
| 0x110  | shm_create | Create shared memory |
| 0x111  | shm_map    | Map shared memory    |
| 0x112  | shm_unmap  | Unmap shared memory  |
| 0x113  | shm_close  | Close SHM handle     |

---

## Signal Delivery

**Implemented:**

- POSIX-like signal numbers (SIGHUP through SIGSYS)
- Hardware fault signals: SIGSEGV, SIGBUS, SIGILL, SIGFPE
- Signal delivery via `signal::send_signal()` and `signal::deliver_fault_signal()`
- Fault info includes: address (FAR), PC (ELR), ESR, and human-readable kind
- Default actions: terminate, ignore, stop, continue
- Per-task signal state: handlers[32], blocked mask, pending mask
- Signal checking on syscall return

**Not Implemented:**

- Full user-space signal handler invocation (needs trampoline)

---

## Architecture Summary

```
┌─────────────────────────────────────────────────────────────┐
│                    Exception Vectors                         │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐         │
│  │  Sync   │  │   IRQ   │  │   FIQ   │  │ SError  │         │
│  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘         │
│       │            │            │            │               │
│       ▼            ▼            ▼            ▼               │
│  ┌─────────────────────────────────────────────────┐        │
│  │           Exception Handler (C++)               │        │
│  │  - Syscall dispatch (~90 syscalls)              │        │
│  │  - GIC IRQ handling (v2/v3)                     │        │
│  │  - Signal delivery (SIGSEGV, etc.)              │        │
│  │  - Fault diagnostics + user recovery            │        │
│  └─────────────────────────────────────────────────┘        │
└─────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        ▼                     ▼                     ▼
   ┌─────────┐          ┌─────────┐          ┌─────────┐
   │   MMU   │          │   GIC   │          │  Timer  │
   │ 4-level │          │ v2/v3   │          │ Hi-Res  │
   │ ASID    │          │ SGI/IPI │          │ 16ns    │
   └─────────┘          └─────────┘          └─────────┘
        │
        ▼
   ┌─────────┐
   │   CPU   │
   │ PSCI    │
   │ 4-core  │
   └─────────┘
```

---

## Files

| File             | Lines | Description                                       |
|------------------|-------|---------------------------------------------------|
| `boot.S`         | ~140  | Kernel entry, secondary CPU entry, per-CPU stacks |
| `exceptions.S`   | ~350  | Vector table and save/restore                     |
| `exceptions.cpp` | ~405  | Exception handlers, fault routing                 |
| `exceptions.hpp` | ~190  | Exception frame definition                        |
| `gic.cpp`        | ~600  | GICv2/v3 driver with auto-detection               |
| `gic.hpp`        | ~150  | GIC interface                                     |
| `mmu.cpp`        | ~415  | MMU configuration                                 |
| `mmu.hpp`        | ~110  | MMU interface                                     |
| `timer.cpp`      | ~480  | Timer driver with high-resolution support         |
| `timer.hpp`      | ~240  | Timer interface with one-shot support             |
| `cpu.cpp`        | ~275  | Per-CPU data, PSCI boot, IPI support              |
| `cpu.hpp`        | ~120  | CPU interface                                     |

Note: Syscall dispatch is in `kernel/syscall/table.cpp` (~4,000 lines).

---

## Priority Recommendations: Next 5 Steps

### 1. User-Space Signal Handler Trampoline

**Impact:** POSIX-compatible signal handling

- Implement sigaction() user context save/restore
- Signal frame on user stack with return trampoline
- Call user handler with proper signal info (siginfo_t)
- Enable graceful SIGTERM, SIGINT, SIGSEGV handling

### 2. TTBR1 Kernel Higher-Half Mapping

**Impact:** Standard kernel memory model

- Map kernel at 0xFFFF... in higher-half
- Separate TTBR1 for kernel address space
- Enables kernel ASLR in future
- Better separation of kernel/user memory

### 3. Debug Exception Support

**Impact:** Enables GDB stub and breakpoints

- Implement BRK instruction handling for software breakpoints
- Hardware breakpoint registers (DBGBCR/DBGBVR)
- Single-step via MDSCR_EL1.SS
- Foundation for kernel debugger integration

### 4. FPU Context Switch

**Impact:** Proper floating-point support across tasks

- Save/restore SIMD registers (V0-V31) on context switch
- Lazy FPU save optimization (only save if used)
- FPCR/FPSR status register preservation
- Required for correct math in multi-tasking

### 5. ACPI Table Parsing

**Impact:** Hardware discovery on real systems

- Parse RSDP, XSDT, MADT for CPU/interrupt info
- ACPI from UEFI ConfigurationTable
- Enables proper device enumeration
- Foundation for power management (DSDT/SSDT)
