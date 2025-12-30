# Architecture Subsystem (AArch64)

**Status:** Complete for QEMU virt platform
**Location:** `kernel/arch/aarch64/`
**SLOC:** ~2,500

## Overview

The architecture subsystem provides low-level AArch64 support for the ViperOS kernel, targeting the QEMU `virt` machine. It handles CPU initialization, memory management unit configuration, interrupt handling, and timer services.

## Components

### 1. Boot (`boot.S`)

**Status:** Complete for QEMU direct boot

**Implemented:**
- Entry point `_start` for kernel boot
- Stack setup (16KB kernel stack)
- BSS section zeroing
- Jump to `kernel_main` C++ entry point
- EL1 execution assumed (QEMU `-kernel` mode)

**Not Implemented:**
- EL2/EL3 to EL1 transition (relies on QEMU or bootloader)
- Multicore boot (only CPU0 supported)
- Secondary CPU wake-up via PSCI

**Recommendations:**
- Add multicore support with per-CPU stacks
- Implement PSCI calls for secondary CPU startup
- Add EL2→EL1 transition for hypervisor-hosted scenarios

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

**Recommendations:**
- Add TTBR1 support for proper kernel/user split
- Consider KASLR for security

---

### 3. GIC - Generic Interrupt Controller (`gic.cpp`, `gic.hpp`)

**Status:** Fully functional GICv2 implementation

**Implemented:**
- GIC Distributor (GICD) initialization at `0x08000000`
- GIC CPU Interface (GICC) initialization at `0x08010000`
- Interrupt enable/disable per IRQ
- Priority configuration (default 0xA0)
- All SPIs targeted to CPU0
- Level-triggered interrupt configuration
- IRQ handler registration (callback-based)
- IRQ acknowledgment and EOI (End of Interrupt)
- Spurious interrupt detection (IRQ 1020+)

**Not Implemented:**
- GICv3 support (needed for newer hardware)
- SGI (Software Generated Interrupts) for IPI
- Multicore interrupt distribution
- Interrupt priority preemption
- FIQ handling (currently unused)
- MSI (Message Signaled Interrupts)

**Recommendations:**
- Add GICv3 support for modern ARM platforms
- Implement SGIs for inter-processor interrupts
- Add proper FIQ support or document that it's unused
- Consider interrupt priority tuning for real-time use cases

---

### 4. Timer (`timer.cpp`, `timer.hpp`)

**Status:** Fully functional, 1000 Hz tick rate

**Implemented:**
- ARM architected timer (EL1 physical timer)
- 1000 Hz tick rate (1ms resolution)
- Timer frequency detection via CNTFRQ_EL0
- Compare value programming (CNTP_CVAL_EL0)
- Timer control (CNTP_CTL_EL0)
- Global tick counter
- Millisecond and nanosecond time queries
- Blocking delay function (`delay_ms`)
- Per-tick callbacks:
  - Input polling
  - Network polling
  - Sleep timer expiration checks
  - Scheduler tick and preemption

**Not Implemented:**
- High-resolution timers (beyond 1ms)
- Per-CPU timers for multicore
- Timer wheel for efficient timeout management
- Virtual timer support
- Watchdog timer

**Recommendations:**
- Implement timer wheel for O(1) timeout operations
- Add high-resolution timer support
- Consider tickless operation for power efficiency
- Add per-CPU timer for multicore scheduling

---

### 5. Exceptions (`exceptions.cpp`, `exceptions.hpp`, `exceptions.S`)

**Status:** Comprehensive exception handling with syscall dispatch and user fault recovery

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

**Complete Syscall Table:**

| Number | Name | Description |
|--------|------|-------------|
| **Task/Process** |||
| 0x01 | exit | Terminate task |
| 0x02 | wait | Wait for child task |
| 0x03 | waitpid | Wait for specific child |
| 0x05 | task_list | List running tasks |
| 0x06 | task_set_priority | Set task priority |
| 0x07 | task_get_priority | Get task priority |
| **IPC** |||
| 0x10 | channel_create | Create IPC channel |
| 0x11 | channel_send | Send on channel |
| 0x12 | channel_recv | Receive from channel |
| 0x13 | channel_close | Close channel |
| **Polling** |||
| 0x20 | poll_create | Create poll set |
| 0x21 | poll_add | Add to poll set |
| 0x22 | poll_remove | Remove from poll set |
| 0x23 | poll_wait | Wait on poll set |
| **Timer** |||
| 0x30 | time_now | Get current time (ms) |
| 0x31 | sleep | Sleep for duration |
| **File I/O (Path-based)** |||
| 0x40 | open | Open file (path) |
| 0x41 | close | Close file descriptor |
| 0x42 | read | Read from fd |
| 0x43 | write | Write to fd |
| 0x44 | lseek | Seek in file |
| 0x45 | stat | Get file info (path) |
| 0x46 | fstat | Get file info (fd) |
| 0x47 | dup | Duplicate fd |
| 0x48 | dup2 | Duplicate fd to specific number |
| **Symlinks** |||
| 0x50 | symlink | Create symbolic link |
| 0x51 | readlink | Read symlink target |
| **Directory Operations** |||
| 0x60 | readdir | Read directory entries |
| 0x61 | mkdir | Create directory |
| 0x62 | rmdir | Remove directory |
| 0x63 | unlink | Delete file |
| 0x64 | rename | Rename file/directory |
| 0x67 | getcwd | Get current directory |
| 0x68 | chdir | Change current directory |
| **Capabilities** |||
| 0x70 | cap_derive | Derive with reduced rights |
| 0x71 | cap_revoke | Revoke capability |
| 0x72 | cap_query | Query capability info |
| 0x73 | cap_list | List all capabilities |
| **File I/O (Handle-based)** |||
| 0x80 | fs_open_root | Get root directory handle |
| 0x81 | fs_open | Open relative to handle |
| 0x82 | io_read | Read from file handle |
| 0x83 | io_write | Write to file handle |
| 0x84 | io_seek | Seek in file handle |
| 0x85 | fs_readdir | Read directory entry |
| 0x86 | fs_close | Close handle |
| 0x87 | fs_rewinddir | Reset directory enumeration |
| **Assigns** |||
| 0x90 | assign_set | Create logical assign |
| 0x91 | assign_get | Get assign handle |
| 0x92 | assign_remove | Remove assign |
| 0x93 | assign_list | List all assigns |
| 0x94 | assign_resolve | Resolve assign path |
| **Networking** |||
| 0xA0 | socket_create | Create TCP socket |
| 0xA1 | socket_bind | Bind socket to port |
| 0xA2 | socket_listen | Listen for connections |
| 0xA3 | socket_accept | Accept connection |
| 0xA4 | socket_connect | Connect to server |
| 0xA5 | socket_send | Send data |
| 0xA6 | socket_recv | Receive data |
| 0xA7 | socket_close | Close socket |
| 0xA8 | dns_resolve | Resolve hostname |
| **TLS** |||
| 0xB0 | tls_create | Create TLS session |
| 0xB1 | tls_handshake | Perform TLS handshake |
| 0xB2 | tls_send | Send encrypted data |
| 0xB3 | tls_recv | Receive decrypted data |
| 0xB4 | tls_close | Close TLS session |
| 0xB5 | tls_info | Get session info |
| **Memory** |||
| 0xC0 | sbrk | Adjust program break |
| **System Info** |||
| 0xE0 | mem_info | Get memory statistics |
| 0xE1 | net_stats | Get network statistics |
| **Console** |||
| 0xF0 | debug_print | Print string to console |
| 0xF1 | getchar | Read character |
| 0xF2 | putchar | Write character |
| 0xF3 | uptime | Get system uptime |

**Not Implemented:**
- Signal delivery to user processes
- Debug exception handling (BRK, single-step)
- Floating-point/SIMD exception handling
- Nested exception support

**Recommendations:**
- Implement signal delivery for SIGSEGV, SIGILL, etc.
- Move syscall dispatch to separate module
- Add debug exception support for debugger integration

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
│  │  - Syscall dispatch                             │        │
│  │  - GIC IRQ handling                             │        │
│  │  - Fault diagnostics                            │        │
│  └─────────────────────────────────────────────────┘        │
└─────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        ▼                     ▼                     ▼
   ┌─────────┐          ┌─────────┐          ┌─────────┐
   │   MMU   │          │   GIC   │          │  Timer  │
   │ Identity│          │  GICv2  │          │ 1000 Hz │
   │ Mapping │          │  IRQ    │          │  Tick   │
   └─────────┘          └─────────┘          └─────────┘
```

## Testing

The architecture subsystem is tested via:
- `qemu_kernel_boot` - Verifies kernel starts and prints banner
- `qemu_scheduler_start` - Verifies scheduler starts (timer working)
- All other QEMU tests implicitly test exception handling

## Files

| File | Lines | Description |
|------|-------|-------------|
| `boot.S` | ~50 | Kernel entry point |
| `exceptions.S` | ~200 | Vector table and save/restore |
| `exceptions.cpp` | ~1300 | Exception handlers, syscall dispatch |
| `exceptions.hpp` | ~80 | Exception frame definition |
| `gic.cpp` | ~215 | GICv2 driver |
| `gic.hpp` | ~50 | GIC interface |
| `mmu.cpp` | ~280 | MMU configuration |
| `mmu.hpp` | ~60 | MMU interface |
| `timer.cpp` | ~230 | Timer driver |
| `timer.hpp` | ~50 | Timer interface |

## Priority Recommendations

1. **High:** Move syscall dispatch out of exceptions.cpp to dedicated module
2. **High:** Add multicore support (boot secondary CPUs)
3. **Medium:** Implement signal delivery for SIGSEGV, SIGILL, etc.
4. **Medium:** Add TTBR1 support for kernel higher-half mapping
5. **Low:** GICv3 support for newer platforms
6. **Low:** High-resolution timers
