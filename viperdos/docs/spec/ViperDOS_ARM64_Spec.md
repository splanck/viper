# ViperDOS — Technical Specification v1.4

## Complete Implementation-Ready Specification (ARM64)

**Version:** 1.4  
**Date:** December 2025  
**Status:** Implementation-Ready (v0 Scope Locked)  
**Architecture:** AArch64 (ARM64) Exclusive

### Revision History

| Version | Changes                                                                                                                                                                                                                                                           |
|---------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 1.4     | Fixed all blockers from implementer review: syscall ABI (X0=error always), expanded VObjectKind, capability passing over channels, filesystem syscalls, kernel-only KHeap metadata, descoped journaling, clarified v0 driver model, fixed address/magic constants |
| 1.3     | ARM64 exclusive, Viper BASIC scripting, removed external references                                                                                                                                                                                               |
| 1.2     | Initial implementation-ready draft                                                                                                                                                                                                                                |

---

## Table of Contents

### Part I: Core Architecture

| # | Section                                           | Description                                 |
|---|---------------------------------------------------|---------------------------------------------|
| 1 | [Overview](#1-overview)                           | What ViperDOS is, target platform, goals    |
| 2 | [Design Principles](#2-design-principles)         | Viper-native, capability-based, async-first |
| 3 | [Boot Architecture](#3-boot-architecture)         | UEFI boot, vboot, VBootInfo structure       |
| 4 | [Graphics Console](#4-graphics-console)           | Framebuffer text, colors, splash, panic     |
| 5 | [Memory Architecture](#5-memory-architecture)     | Virtual layout, KHeap/LHeap, handles        |
| 6 | [Kernel Type Awareness](#6-kernel-type-awareness) | Built-in kinds, shallow typing              |
| 7 | [Process Model](#7-process-model)                 | Vipers, Tasks, structured concurrency       |
| 8 | [Syscall Semantics](#8-syscall-semantics)         | Non-blocking rule, error codes, ABI         |
| 9 | [Capability System](#9-capability-system)         | Rights, derivation, transfer                |

### Part II: Subsystems

| #  | Section                                                      | Description                             |
|----|--------------------------------------------------------------|-----------------------------------------|
| 10 | [IPC: Channels](#10-ipc-channels)                            | Message passing, non-blocking send/recv |
| 11 | [Async I/O & Polling](#11-async-io--polling)                 | PollWait, VPollEvent, poll flags        |
| 12 | [ViperFS](#12-viperfs)                                       | Capability-based filesystem             |
| 13 | [Graphics](#13-graphics)                                     | Framebuffer, surfaces                   |
| 14 | [Input](#14-input)                                           | Keyboard, mouse polling                 |
| 15 | [Bootstrap & Drivers](#15-bootstrap--drivers)                | Driver model, vinit                     |
| 16 | [Hardware Abstraction Layer](#16-hardware-abstraction-layer) | Platform interfaces                     |

### Part III: User Space

| #  | Section                                          | Description                             |
|----|--------------------------------------------------|-----------------------------------------|
| 17 | [Installed System](#17-installed-system)         | Complete file listing for fresh install |
| 18 | [Directory Layout](#18-directory-layout)         | Filesystem hierarchy, drive naming      |
| 19 | [Standard Library](#19-standard-library)         | Viper.* namespace hierarchy             |
| 20 | [Core Utilities](#20-core-utilities)             | Programs that ship with ViperDOS        |
| 21 | [Shell (vsh)](#21-shell-vsh)                     | Command shell, prompt, syntax           |
| 22 | [Configuration Format](#22-configuration-format) | ViperConfig (.vcfg) syntax              |
| 23 | [File Formats](#23-file-formats)                 | .vpr, .vlib, .vfont, .vcfg              |

### Part IV: Development

| #  | Section                                                  | Description              |
|----|----------------------------------------------------------|--------------------------|
| 24 | [Syscall Reference](#24-syscall-reference)               | Complete syscall table   |
| 25 | [Testing Infrastructure](#25-testing-infrastructure)     | QEMU modes, test scripts |
| 26 | [Development Phases](#26-development-phases)             | 6-phase roadmap          |
| 27 | [Design Decisions Summary](#27-design-decisions-summary) | Quick reference table    |

### Appendices

| # | Section                                        | Description                     |
|---|------------------------------------------------|---------------------------------|
| A | [Color Reference](#appendix-a-color-reference) | ViperDOS color palette          |
| B | [Quick Reference](#appendix-b-quick-reference) | Syscalls, directories, commands |

---

# Part I: Core Architecture

---

## 1. Overview

### 1.1 What is ViperDOS?

ViperDOS is the native operating system for the Viper Platform. Its purpose is to provide a complete runtime environment
where Viper applications execute directly on hardware, without requiring a host operating system.

Today, Viper applications run on Linux, Windows, and macOS through the Viper runtime. ViperDOS eliminates this
dependency. Applications written in Zia or Viper BASIC compile to Viper IL and execute natively on
ViperDOS, with the kernel providing memory management, scheduling, I/O, and graphics services directly.

```
┌────────────────────────────────────────────────────────────────┐
│  Viper Application (.vpr)                                      │
│  (Zia / Viper BASIC)                                    │
├────────────────────────────────────────────────────────────────┤
│  Viper Runtime Libraries (Viper.*)                            │
├────────────────────────────────────────────────────────────────┤
│  VCALL Boundary (svc instruction)                             │
├────────────────────────────────────────────────────────────────┤
│  ViperDOS Hybrid Kernel                                         │
│  (Scheduler, Memory, IPC, Capabilities, HAL)                  │
├────────────────────────────────────────────────────────────────┤
│  ARM64 Hardware                                                │
└────────────────────────────────────────────────────────────────┘
```

### 1.2 Goals

The ultimate goal of ViperDOS is to fully host the Viper Platform:

- **Self-hosting:** ViperIDE, the Viper compiler, and all development tools run natively on ViperDOS
- **Complete runtime:** All Viper runtime library functionality available through kernel services
- **Native execution:** Viper IL executes directly, with JIT or AOT compilation to ARM64
- **Vertical integration:** From hardware through OS through language through applications

### 1.3 Target Platform

**Architecture:** AArch64 (ARM64) exclusively

**Development:** QEMU virt machine  
**Target Hardware:** ARM64 SoCs (RK3588 and similar)

ViperDOS is a single-architecture operating system. This deliberate constraint enables:

- Simplified codebase with no architecture abstraction overhead
- Optimal code generation for ARM64
- Direct path from QEMU development to real hardware
- Alignment with Viper Computer hardware goals

### 1.4 Non-Goals for v0

- POSIX compatibility
- Running existing binaries
- Multi-user support
- Full SMP (designed for it, but v0 is single-core)
- Support for non-ARM architectures

---

## 2. Design Principles

### 2.1 Viper-Native

The Viper Platform is not a guest on ViperDOS—it is the reason ViperDOS exists. Viper IL is the syscall boundary.
Applications don't link against libc; they call kernel services directly through the `VCALL` IL primitive, which
compiles to the ARM64 `svc` instruction.

### 2.2 Capability-Based Security

No ambient authority. Programs start with zero capabilities and receive only what they're explicitly granted.

### 2.3 Async-First I/O

No blocking syscalls except `PollWait`. Operations either complete immediately or return `WOULD_BLOCK`.

### 2.4 Hybrid Kernel Architecture

The kernel provides scheduling, memory, IPC, capabilities, filesystem, networking, TLS, and device drivers.
Display services (consoled, displayd) run in user space.

### 2.5 Shallow Type Awareness

The kernel understands a fixed set of ~8 built-in Viper types. User-defined types are opaque.

### 2.6 Graphics-First

ViperDOS boots directly into graphics mode. The console is graphical from the first moment. Serial output is for
debugging only.

---

## 3. Boot Architecture

### 3.1 Boot Sequence Overview

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│  Firmware   │───▶│   vboot     │───▶│   Kernel    │───▶│   vinit     │
│  (UEFI)     │    │ (bootloader)│    │  (EL1)      │    │ (user space)│
└─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘
       │                  │                  │
       │                  │                  ├── Display boot splash
       │                  │                  ├── Initialize graphics console
       │                  │                  └── Print boot messages
       │                  │
       │                  └── Set up framebuffer via UEFI GOP
       │
       └── Initialize hardware, GOP available
```

### 3.2 Bootloader (vboot)

vboot is a UEFI application that:

1. Loads kernel ELF from ESP
2. Obtains memory map and framebuffer from UEFI
3. Sets up initial translation tables
4. Exits UEFI boot services
5. Jumps to kernel at EL1

#### 3.2.1 ESP Layout

```
ESP (FAT32)
├── EFI/
│   └── BOOT/
│       └── BOOTAA64.EFI     # vboot for AArch64
└── viperdos/
    ├── kernel.sys           # Kernel image
    ├── splash.raw           # Boot splash image (optional)
    └── initrd.tar           # Initial ramdisk
```

#### 3.2.2 Kernel Loading

```
Kernel ELF:
  Virtual base:    0xFFFF_FFFF_0000_0000
  Entry point:     0xFFFF_FFFF_0010_0000 (1MB into kernel region)
  Physical load:   0x0000_0000_0010_0000 (1MB, below the 1GB identity map)
```

The kernel is linked to run at 0xFFFF_FFFF_0000_0000+ and loaded physically at 1MB. vboot sets up translation tables to
map the kernel region before jumping to the entry point.

#### 3.2.3 Translation Table Setup

ARM64 uses a 4-level translation table with 4KB granule (48-bit VA):

```
TTBR0_EL1 → User space / temporary identity map
  Level 0 [0]     → Level 1 table
    Level 1 [0]   → Identity map physical [0, 1GB) using 1GB block
                    (covers kernel load at 1MB; removed after kernel init)

TTBR1_EL1 → Kernel space (upper VA range, 0xFFFF_...)
  Level 0 [0]     → Level 1 table for HHDM (0xFFFF_0000_...)
    Level 1 [0-n] → Direct map of all physical memory (1GB blocks)
  Level 0 [511]   → Level 1 table for kernel (0xFFFF_FFFF_...)
    Level 1 [0]   → Level 2 table
      Level 2 [...] → Kernel image at 0xFFFF_FFFF_0000_0000+
```

The identity map is temporary: it allows the kernel to execute immediately after vboot jumps to it, before the kernel
switches to using only HHDM+kernel mappings.

Translation table entry format (4KB granule):

```
Bits 63:52  - Upper attributes (UXN, PXN, etc.)
Bits 51:48  - Reserved
Bits 47:12  - Physical address (4KB aligned)
Bits 11:2   - Lower attributes (AF, SH, AP, AttrIndx)
Bit  1      - Table/Block indicator
Bit  0      - Valid bit
```

#### 3.2.4 Memory Attributes (MAIR_EL1)

```c
// MAIR_EL1 configuration
#define MAIR_DEVICE_nGnRnE  0x00  // Attr0: Device memory
#define MAIR_NORMAL_NC      0x44  // Attr1: Normal, non-cacheable
#define MAIR_NORMAL_WB      0xFF  // Attr2: Normal, write-back cacheable

// MAIR_EL1 = Attr2 | Attr1 | Attr0
#define MAIR_VALUE  ((MAIR_NORMAL_WB << 16) | (MAIR_NORMAL_NC << 8) | MAIR_DEVICE_nGnRnE)
```

### 3.3 Boot Handoff

#### 3.3.1 Entry State

```
X0  = VBootInfo* (physical address)
SP  = Valid 16KB stack (physical, identity-mapped)
EL  = EL1 (kernel mode)
MMU = Enabled (translation tables set up by vboot)
I/F = Masked (interrupts disabled)

System registers configured:
  SCTLR_EL1.M   = 1  (MMU enabled)
  SCTLR_EL1.C   = 1  (Data cache enabled)
  SCTLR_EL1.I   = 1  (Instruction cache enabled)
  TCR_EL1       = 48-bit VA, 4KB granule, inner-shareable
  MAIR_EL1      = Device + Normal NC + Normal WB
```

#### 3.3.2 Exception Level Usage

```
EL0 - User space (Viper applications)
EL1 - Kernel (ViperDOS hybrid kernel)
EL2 - Not used (no hypervisor)
EL3 - Firmware only
```

### 3.4 VBootInfo Structure

```c
```c
// Magic: "VIPRBOOT" in little-endian
// V=0x56 I=0x49 P=0x50 R=0x52 B=0x42 O=0x4F O=0x4F T=0x54
#define VBOOT_MAGIC 0x544F4F4252504956ULL

#define VBOOT_MAX_MEMORY_REGIONS 256

typedef struct {
    uint64_t base;
    uint64_t size;
    uint32_t type;      // 1=usable, 2=reserved, 3=ACPI, 4=MMIO
    uint32_t _pad;
} VBootMemoryRegion;

typedef struct {
    uint64_t base;           // Framebuffer physical address
    uint32_t width, height;
    uint32_t pitch;          // Bytes per scanline
    uint32_t bpp;            // Bits per pixel (32)
    uint32_t pixel_format;   // 0=BGRA, 1=RGBA
} VBootFramebuffer;

typedef struct {
    uint64_t magic;
    uint32_t version_major, version_minor;
    
    uint32_t memory_region_count;
    uint32_t _pad0;
    VBootMemoryRegion memory_regions[VBOOT_MAX_MEMORY_REGIONS];
    
    VBootFramebuffer framebuffer;
    
    uint64_t hhdm_base;                    // Higher-half direct map base
    uint64_t kernel_phys_base;
    uint64_t kernel_virt_base;
    uint64_t kernel_size;
    uint64_t initrd_phys, initrd_size;
    uint64_t dtb_address;                  // Device Tree Blob address
    
    char cmdline[256];
    uint64_t boot_timestamp_ns;
} VBootInfo;  // ~6KB
```

### 3.5 Kernel Initialization Order

1. Validate VBootInfo magic
2. **Initialize graphics console** (framebuffer text output)
3. **Display boot splash** (ViperDOS logo)
4. Print boot messages to graphical console
5. Parse memory map
6. Parse device tree (DTB)
7. Set up exception vectors (VBAR_EL1)
8. Initialize physical allocator
9. Initialize virtual memory manager
10. Remove identity mapping from TTBR0_EL1
11. Initialize GIC (Generic Interrupt Controller)
12. Initialize timer (ARM architected timer)
13. Initialize scheduler
14. Start vinit

---

## 4. Graphics Console

### 4.1 Overview

ViperDOS boots directly into graphics mode. The graphical console provides text output from the earliest moments of
boot.
Serial output is maintained in parallel for debugging.

### 4.2 Color Scheme

```c
// ViperDOS color palette
#define VIPER_GREEN       0xFF00AA44  // Primary text color
#define VIPER_DARK_BROWN  0xFF1A1208  // Background
#define VIPER_YELLOW      0xFFFFDD00  // Panic text, warnings
#define VIPER_PANIC_BG    0xFF00AA44  // Panic background (green)
#define VIPER_DIM_GREEN   0xFF006633  // Secondary/dim text
#define VIPER_WHITE       0xFFEEEEEE  // Bright text
#define VIPER_RED         0xFFCC3333  // Errors
```

**Normal console:** Viper green (#00AA44) text on very dark brown (#1A1208) background.

**Panic screen:** Yellow (#FFDD00) text on green (#00AA44) background.

### 4.3 Boot Splash

A simple ViperDOS logo displayed immediately after graphics console initialization:

```
    ╔═══════════════════════════════════════╗
    ║                                       ║
    ║           ░▒▓█ VIPER █▓▒░             ║
    ║               ══════                  ║
    ║                 OS                    ║
    ║                                       ║
    ╚═══════════════════════════════════════╝
```

The splash is displayed for ~500ms or until first boot message, whichever is later.

### 4.4 Font

**v0:** Single baked-in 8x16 bitmap font (Topaz-style monospace).

```c
// Font header
struct VFontHeader {
    uint32_t magic;        // 0x544E4F46 = "FONT"
    uint8_t  width;        // 8
    uint8_t  height;       // 16
    uint16_t num_glyphs;   // 128 for ASCII
    uint32_t flags;        // Reserved
};
// Followed by: uint8_t glyph_data[num_glyphs][height]
// Each byte is 8 pixels (1 bit per pixel, MSB = leftmost)
```

**Console dimensions:** At 1024x768 with 8x16 font = 128 columns × 48 rows.

### 4.5 Graphics Console API

```c
// Kernel-internal API
void gcon_init(const VBootFramebuffer* fb);
void gcon_set_colors(uint32_t fg, uint32_t bg);
void gcon_clear(void);
void gcon_putc(char c);
void gcon_puts(const char* s);
void gcon_set_cursor(uint32_t x, uint32_t y);
void gcon_scroll(void);

// For boot splash
void gcon_draw_splash(void);
void gcon_hide_splash(void);
```

### 4.6 Dual Output

During boot and for debugging, output goes to both:

- Graphics console (user-visible)
- Serial port (development/logging via PL011 UART)

```c
void kprintf(const char* fmt, ...) {
    // Format string...
    gcon_puts(buffer);   // To screen
    serial_puts(buffer); // To serial (PL011)
}
```

### 4.7 Panic Screen

On kernel panic:

```
┌────────────────────────────────────────────────────────────────┐
│ ████████████████████████████████████████████████████████████████│
│ █                                                              █│
│ █                     VIPERDOS PANIC                            █│
│ █                                                              █│
│ █  Exception: Synchronous (Data Abort)                         █│
│ █  ESR_EL1:   0x96000045                                       █│
│ █                                                              █│
│ █  PC:   0xFFFF000000102345                                    █│
│ █  SP:   0xFFFF000000150000                                    █│
│ █  FAR:  0x0000000000001000                                    █│
│ █  LR:   0xFFFF000000101ABC                                    █│
│ █                                                              █│
│ █  X0:  0x0000000000000000  X1:  0x0000000040000000           █│
│ █  X2:  0x0000000000001000  X3:  0x0000000000000001           █│
│ █                                                              █│
│ █  System halted.                                              █│
│ █                                                              █│
│ ████████████████████████████████████████████████████████████████│
└────────────────────────────────────────────────────────────────┘
```

Green (#00AA44) background, yellow (#FFDD00) text.

---

## 5. Memory Architecture

### 5.1 Virtual Memory Layout

ARM64 with 48-bit virtual addresses, 4KB pages:

```
User Space (TTBR0_EL1):
0x0000_0000_0000_0000    Null guard (unmapped)
0x0000_0000_0000_1000    User space start
0x0000_0010_0000_0000    Code region
0x0000_0050_0000_0000    LHeap (user-managed)
0x0000_0070_0000_0000    KHeap (kernel-managed, user-accessible)
0x0000_007F_FFFF_F000    Stack top
0x0000_FFFF_FFFF_FFFF    User space end

Kernel Space (TTBR1_EL1):
0xFFFF_0000_0000_0000    HHDM (Higher-Half Direct Map)
0xFFFF_8000_0000_0000    Kernel heap
0xFFFF_FFFF_0000_0000    Kernel image
0xFFFF_FFFF_FFFF_FFFF    End of address space
```

### 5.2 Dual Heap Model

**LHeap:** User-space managed, non-atomic refcounts, no syscalls needed for allocation. Entirely within user address
space.

**KHeap:** Kernel-managed objects for shared/capability-protected data.

### 5.3 KHeap Architecture

KHeap objects have a split design for security:

```
Kernel Memory (not user-accessible):
┌─────────────────────────────────────┐
│ VKHeapDescriptor                    │
│   - kind, flags, refcount           │
│   - length, capacity                │
│   - owner_viper                     │
│   - payload_pages[]                 │
└─────────────────────────────────────┘
         │
         │ maps to
         ▼
User Memory (read/write as permitted):
┌─────────────────────────────────────┐
│ Payload data only                   │
│ (no metadata, no header)            │
└─────────────────────────────────────┘
```

**Security model:**

- All metadata (kind, refcount, length, owner) lives in kernel memory only.
- User code receives a handle (capability table index), never a pointer to metadata.
- Payload pages are mapped into user space with appropriate permissions.
- User code cannot forge or corrupt object metadata.

```c
// Kernel-side descriptor (never user-visible)
typedef struct VKHeapDescriptor {
    uint32_t magic;           // "VKHD" (0x44484B56)
    uint16_t kind;            // VObjectKind
    uint16_t flags;
    _Atomic uint64_t refcnt;
    uint64_t len;
    uint64_t cap;
    uint64_t owner_viper;
    uintptr_t payload_phys;   // Physical address of payload
    uintptr_t payload_user;   // User virtual address (if mapped)
} VKHeapDescriptor;
```

### 5.4 Handle Representation

32-bit indices into per-Viper capability table. Upper bits encode generation for ABA protection.

```c
typedef uint32_t Handle;

#define HANDLE_INDEX_MASK  0x00FFFFFF
#define HANDLE_GEN_MASK    0xFF000000
#define HANDLE_GEN_SHIFT   24

#define HANDLE_INVALID     0xFFFFFFFF
```

Handles are validated on every syscall:

1. Check index is within table bounds
2. Check generation matches (detects use-after-free)
3. Check object kind matches expected kind for syscall

---

## 6. Kernel Type Awareness

### 6.1 Object Kinds

Every kernel object has a kind tag. This enables type-safe handle validation and prevents type confusion attacks.

```c
enum VObjectKind {
    // Invalid / unallocated
    VKIND_INVALID    = 0,
    
    // Data objects (KHeap-backed, may have user-accessible payload)
    VKIND_STRING     = 1,    // UTF-8 string
    VKIND_ARRAY      = 2,    // Typed array
    VKIND_BLOB       = 3,    // Raw bytes
    
    // Kernel objects (no user-accessible payload, pure handles)
    VKIND_CHANNEL    = 16,   // IPC channel endpoint
    VKIND_POLL       = 17,   // Poll set
    VKIND_TIMER      = 18,   // Timer
    VKIND_TASK       = 19,   // Task handle
    VKIND_VIPER      = 20,   // Viper (process) handle
    VKIND_FILE       = 21,   // Open file
    VKIND_DIRECTORY  = 22,   // Open directory
    VKIND_SURFACE    = 23,   // Graphics surface
    VKIND_INPUT      = 24,   // Input device
    
    // User-defined (opaque to kernel)
    VKIND_USER_BASE  = 0x8000,
};
```

The kernel validates kind on every syscall. Passing a timer handle to `ChannelSend` returns `VERR_INVALID_HANDLE`.

---

## 7. Process Model

### 7.1 Vipers

A Viper is an isolated execution environment containing:

- Address space (TTBR0_EL1 value)
- Capability table
- Task list
- Resource limits

```c
typedef struct VViper {
    uint64_t id;
    uint64_t ttbr0;              // Translation table base
    VCapTable* cap_table;
    VTask* task_list;
    VViper* parent;
    uint32_t state;
    uint32_t exit_code;
} VViper;
```

### 7.2 Tasks

Tasks are 1:1 with kernel threads. NOT green threads.

```c
typedef struct VTask {
    uint64_t id;
    VViper* viper;
    
    // Saved context (on kernel stack during syscall/interrupt)
    uint64_t x[31];              // X0-X30
    uint64_t sp;                 // SP_EL0
    uint64_t pc;                 // ELR_EL1 (return address)
    uint64_t pstate;             // SPSR_EL1
    
    // Kernel state
    void* kernel_stack;
    uint32_t state;              // TASK_RUNNING, TASK_READY, TASK_BLOCKED
    uint32_t flags;
    
    // Scheduling
    VTask* next_ready;
    uint64_t wake_time;          // For timed waits
    uint32_t time_slice;         // Remaining ticks (reset to TIME_SLICE_DEFAULT on schedule)
    uint32_t _pad;
    
    // Structured concurrency
    VTask* parent;
    VTask* first_child;
    VTask* next_sibling;
} VTask;

#define TIME_SLICE_DEFAULT 10    // 10ms at 1000Hz tick rate
```

### 7.3 Structured Concurrency

Child tasks cannot outlive parent tasks. When a parent exits:

1. All children receive cancellation signal
2. Parent blocks until all children exit
3. Child resources are reclaimed

### 7.4 Scheduler (v0)

Round-robin, timer preemption, no priorities.

```c
void scheduler_tick(void) {
    current_task->time_slice--;
    if (current_task->time_slice == 0) {
        schedule();  // Pick next task
    }
}

void schedule(void) {
    VTask* next = ready_queue_pop();
    if (next != current_task) {
        context_switch(current_task, next);
    }
}
```

Timer interrupt: ARM architected timer at 1000 Hz (1ms tick).

---

## 8. Syscall Semantics

### 8.1 Golden Rule

**All syscalls return immediately except `PollWait`.** Operations either succeed, fail, or return `VERR_WOULD_BLOCK`.

This applies universally:

- `TaskJoin` returns `VERR_WOULD_BLOCK` if the task hasn't exited yet. Use `PollWait` with `VPOLL_TASK_EXIT`.
- `ViperWait` returns `VERR_WOULD_BLOCK` if the Viper hasn't exited yet. Use `PollWait` with `VPOLL_VIPER_EXIT`.
- `ChannelRecv` returns `VERR_WOULD_BLOCK` if no message is available. Use `PollWait` with `VPOLL_READABLE`.
- There is no `TimeSleep` syscall. Sleeping is implemented in userspace via `TimerCreate` + `PollWait`.

The only blocking point in the entire system is `PollWait`, which blocks until at least one event fires or timeout
expires.

### 8.2 Error Codes

```c
enum VError {
    VOK                    =  0,
    VERR_INVALID_HANDLE    = -1,
    VERR_INVALID_ARG       = -2,
    VERR_OUT_OF_MEMORY     = -3,
    VERR_PERMISSION        = -4,
    VERR_WOULD_BLOCK       = -5,
    VERR_CHANNEL_CLOSED    = -6,
    VERR_NOT_FOUND         = -7,
    VERR_ALREADY_EXISTS    = -8,
    VERR_IO                = -9,
    VERR_CANCELLED         = -10,
    VERR_TIMEOUT           = -11,
    VERR_NOT_SUPPORTED     = -12,
    VERR_MSG_TOO_LARGE     = -13,   // Message exceeds CHANNEL_MAX_MSG_SIZE
    VERR_BUFFER_TOO_SMALL  = -14,   // Recv buffer too small for message
    VERR_NOT_DIRECTORY     = -15,
    VERR_NOT_FILE          = -16,
    VERR_NOT_EMPTY         = -17,   // Directory not empty
};
```

### 8.3 Calling Convention (AArch64)

```
Syscall invocation:
  X8  = syscall number
  X0  = arg0
  X1  = arg1
  X2  = arg2
  X3  = arg3
  X4  = arg4
  X5  = arg5
  SVC #0

Return (ALWAYS this layout):
  X0  = VError (always; 0 = success, negative = error)
  X1  = result0 (handle, size, or other primary result)
  X2  = result1 (secondary result if needed)
  X3  = result2 (tertiary result if needed)
```

**ABI Rules:**

- X0 is ALWAYS a VError. Never a handle, never ambiguous.
- Handles and sizes are returned in X1-X3.
- Out-pointers are used ONLY for bulk data (arrays, buffers) that cannot fit in registers.
- This prevents handle/error collision: Handle is uint32_t, VError is int64_t, and they never share a register.

### 8.4 Exception Vector Entry

```c
// Vector table alignment: 2KB (11 bits)
// Each entry: 32 instructions (128 bytes)

.align 11
exception_vectors:
    // Current EL with SP0
    .align 7
    b sync_current_sp0
    .align 7
    b irq_current_sp0
    .align 7
    b fiq_current_sp0
    .align 7
    b serror_current_sp0
    
    // Current EL with SPx
    .align 7
    b sync_current_spx
    .align 7
    b irq_current_spx
    // ...
    
    // Lower EL using AArch64
    .align 7
    b sync_lower_a64      // Syscalls land here
    .align 7
    b irq_lower_a64       // User interrupts
    // ...
```

---

## 9. Capability System

### 9.1 Rights

```c
enum CapRights {
    CAP_READ     = 1 << 0,   // Read data
    CAP_WRITE    = 1 << 1,   // Write data
    CAP_EXECUTE  = 1 << 2,   // Execute code
    CAP_LIST     = 1 << 3,   // List directory contents
    CAP_CREATE   = 1 << 4,   // Create children
    CAP_DELETE   = 1 << 5,   // Delete object
    CAP_DERIVE   = 1 << 6,   // Create derived capability
    CAP_TRANSFER = 1 << 7,   // Transfer over channel
    CAP_SPAWN    = 1 << 8,   // Spawn new Viper
};
```

**Note:** CAP_DMA and CAP_IRQ are reserved for future userspace driver support. In v0, all drivers run in the kernel.

### 9.2 Capability Table Entry

```c
typedef struct VCapEntry {
    void* object;            // Pointer to kernel object descriptor
    uint32_t rights;         // CapRights bitmap
    uint16_t kind;           // VObjectKind
    uint16_t generation;     // For handle validation
} VCapEntry;
```

### 9.3 Derivation Rules

- Derived capabilities can only have equal or fewer rights
- CAP_DERIVE right is required to create derived capabilities
- Revocation propagates to all derived capabilities

---

# Part II: Subsystems

---

## 10. IPC: Channels

### 10.1 Overview

Channels provide non-blocking message passing between tasks (within or across Vipers). Messages can include both data
bytes and capability handles.

### 10.2 Channel Operations

```c
// Create a channel pair
// Returns: X0=VError, X1=send_handle, X2=recv_handle
VError ChannelCreate(void);

// Send a message with optional capabilities (non-blocking)
// handles: array of handles to transfer (or NULL)
// num_handles: number of handles (0-4)
// Returns: VOK, VERR_WOULD_BLOCK, VERR_CHANNEL_CLOSED, VERR_MSG_TOO_LARGE
VError ChannelSend(Handle channel, const void* data, size_t len,
                   const Handle* handles, size_t num_handles);

// Receive a message with capabilities (non-blocking)
// handles_out: receives transferred handles (caller provides array)
// num_handles_out: in=capacity, out=actual count received
// Returns: VOK, VERR_WOULD_BLOCK, VERR_CHANNEL_CLOSED, VERR_BUFFER_TOO_SMALL
VError ChannelRecv(Handle channel, void* buf, size_t buf_len, size_t* actual_len,
                   Handle* handles_out, size_t* num_handles_out);

// Close an endpoint
VError ChannelClose(Handle channel);
```

### 10.3 Capability Transfer

When sending handles over a channel:

1. Sender must have CAP_TRANSFER right on each handle being sent
2. Kernel validates all handles before the send commits
3. On successful send, handles are **moved** from sender's cap table (sender loses access)
4. Receiver gets new handle values in their own cap table
5. Rights are preserved during transfer

This enables capability transfer patterns: display servers can send framebuffer handles to clients.

### 10.4 Message Format and Limits

```c
#define CHANNEL_MAX_MSG_SIZE     2048    // Max bytes per message
#define CHANNEL_MAX_MSG_HANDLES  4       // Max handles per message
#define CHANNEL_BUFFER_MSGS      16      // Messages buffered per channel
#define CHANNEL_BUFFER_BYTES     4096    // Total byte buffer size
```

**Rules:**

- Messages are atomic: a message is never partially delivered
- If `len > CHANNEL_MAX_MSG_SIZE`: return `VERR_MSG_TOO_LARGE`
- If recv buffer too small: return `VERR_BUFFER_TOO_SMALL`, set `actual_len` to required size
- If channel buffer full: return `VERR_WOULD_BLOCK`
- Poll with `VPOLL_WRITABLE` to wait for send space
- Poll with `VPOLL_READABLE` to wait for messages

---

## 11. Async I/O & Polling

### 11.1 VPollEvent

```c
struct VPollEvent {
    Handle handle;       // Object being polled
    uint32_t events;     // Requested events (VPOLL_*)
    int32_t status;      // Result status
    uint64_t token;      // User-provided token
    uint64_t result;     // Operation result
};  // 32 bytes
```

### 11.2 Poll Flags

```c
enum VPollFlags {
    VPOLL_READABLE   = 1 << 0,   // Data available to read
    VPOLL_WRITABLE   = 1 << 1,   // Space available to write
    VPOLL_ERROR      = 1 << 2,   // Error condition
    VPOLL_HANGUP     = 1 << 3,   // Peer closed
    VPOLL_TIMER      = 1 << 4,   // Timer expired
    VPOLL_IO_DONE    = 1 << 5,   // Async I/O completed
    VPOLL_TASK_EXIT  = 1 << 6,   // Child task exited
    VPOLL_VIPER_EXIT = 1 << 7,   // Child Viper exited
};
```

### 11.3 Poll Event Semantics

Each event type fills VPollEvent fields with specific meanings:

| Event Flag       | Object Kind | `result` field         | `status` field |
|------------------|-------------|------------------------|----------------|
| VPOLL_READABLE   | Channel     | Bytes available        | VOK            |
| VPOLL_READABLE   | File        | Bytes available        | VOK            |
| VPOLL_WRITABLE   | Channel     | Buffer space available | VOK            |
| VPOLL_WRITABLE   | File        | Always (unless error)  | VOK            |
| VPOLL_ERROR      | Any         | 0                      | Error code     |
| VPOLL_HANGUP     | Channel     | 0                      | VOK            |
| VPOLL_TIMER      | Timer       | Expiration count (>=1) | VOK            |
| VPOLL_TASK_EXIT  | Task        | Exit code              | VOK            |
| VPOLL_VIPER_EXIT | Viper       | Exit code              | VOK            |

The `handle` field always contains the handle that triggered the event.
The `token` field contains the user-provided value from `PollAdd`.

### 11.4 Polling API

```c
// Create a poll set
// Returns: X0=VError, X1=poll_handle
VError PollCreate(void);

// Add handle to poll set
VError PollAdd(Handle poll, Handle target, uint32_t events, uint64_t token);

// Remove handle from poll set
VError PollRemove(Handle poll, Handle target);

// Wait for events (THE ONLY BLOCKING SYSCALL)
// events: user buffer for returned events
// max_events: capacity of events buffer
// timeout_ns: 0=return immediately, UINT64_MAX=wait forever
// Returns: X0=VError, X1=num_events
VError PollWait(Handle poll, VPollEvent* events, size_t max_events, uint64_t timeout_ns);
```

---

## 12. ViperFS

### 12.1 Overview

ViperFS is a capability-based filesystem. Files and directories are accessed through capability handles, not paths with
ambient authority.

**v0 Scope:** Basic read/write filesystem without journaling. Journaling deferred to v0.5+.

### 12.2 Filesystem Syscalls

```c
// Open root directory of a mounted volume
// Returns: X0=VError, X1=dir_handle
VError FsOpenRoot(Handle volume);

// Open file or directory relative to a directory capability
// flags: VFS_READ, VFS_WRITE, VFS_CREATE, VFS_EXCLUSIVE
// Returns: X0=VError, X1=handle, X2=kind (VKIND_FILE or VKIND_DIRECTORY)
VError FsOpen(Handle dir, const char* name, size_t name_len, uint32_t flags);

// Read directory entries
// Returns: X0=VError, X1=num_entries
// Writes VFSDirInfo structs to buffer
VError FsReadDir(Handle dir, VFSDirInfo* buf, size_t max_entries);

// Create file or directory
// kind: VKIND_FILE or VKIND_DIRECTORY
// Returns: X0=VError, X1=handle
VError FsCreate(Handle parent_dir, const char* name, size_t name_len, uint16_t kind);

// Delete file or directory
VError FsDelete(Handle parent_dir, const char* name, size_t name_len);

// Rename/move
VError FsRename(Handle src_dir, const char* src_name, size_t src_len,
                Handle dst_dir, const char* dst_name, size_t dst_len);

// Get file/directory info
// Returns: X0=VError, writes to info struct
VError FsStat(Handle file_or_dir, VFSInfo* info);

// Close handle (also via generic IOClose)
VError FsClose(Handle handle);
```

### 12.3 Directory Info Structure

```c
typedef struct VFSDirInfo {
    char     name[256];       // Null-terminated name
    uint16_t kind;            // VKIND_FILE or VKIND_DIRECTORY
    uint16_t flags;
    uint32_t _pad;
    uint64_t size;
    uint64_t mtime;
} VFSDirInfo;

typedef struct VFSInfo {
    uint64_t size;
    uint64_t atime, mtime, ctime;
    uint32_t mode;
    uint32_t flags;
} VFSInfo;
```

### 12.4 Assigns (Named Directory Capabilities)

Shell assigns like `SYS:`, `C:`, `HOME:` are implemented as named directory capabilities stored in the Viper's
environment. The shell resolves paths like `SYS:c\dir.vpr` by:

1. Looking up `SYS:` assign → gets directory handle
2. Calling `FsOpen(sys_handle, "c", VFS_READ)` → gets c directory handle
3. Calling `FsOpen(c_handle, "dir.vpr", VFS_READ)` → gets file handle

This preserves capability semantics: you can only access paths you have capabilities for.

### 12.5 On-Disk Layout

```
Block 0:        Superblock
Blocks 1-N:     (Reserved for journal, unused in v0)
Blocks N+1-M:   Block bitmap
Blocks M+1-K:   Inode table
Blocks K+1-...: Data blocks
```

### 12.6 Superblock

```c
typedef struct VFSSuperblock {
    uint32_t magic;           // "VPFS" (0x53465056)
    uint32_t version;         // 1 for v0 (no journal)
    uint64_t block_size;      // 4096
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t inode_count;
    uint64_t root_inode;
    uint64_t journal_start;   // Reserved, unused in v0
    uint64_t journal_size;    // 0 in v0
    uint64_t bitmap_start;
    uint64_t inode_table_start;
    uint64_t data_start;
    uint8_t  uuid[16];
    char     label[64];
} VFSSuperblock;
```

### 12.7 Inode

```c
typedef struct VFSInode {
    uint64_t inode_num;
    uint32_t mode;            // File type + permissions
    uint32_t flags;           // VFLAG_* bits
    uint64_t size;
    uint64_t blocks;
    uint64_t atime, mtime, ctime;
    uint64_t direct[12];      // Direct block pointers
    uint64_t indirect;        // Single indirect
    uint64_t double_indirect; // Double indirect
    uint64_t triple_indirect; // Triple indirect
    uint64_t generation;      // For capability validation
} VFSInode;  // 176 bytes
```

### 12.8 Directory Entry (On-Disk)

```c
typedef struct VFSDirEntry {
    uint64_t inode;
    uint16_t rec_len;         // Total entry length
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];          // Variable length, null-terminated
} VFSDirEntry;
```

---

## 13. Graphics

### 13.1 Overview

v0 provides a single framebuffer with software rendering. Hardware acceleration deferred to future versions.

### 13.2 Surface API

```c
// Acquire the display surface
VError SurfaceAcquire(Handle* surface, uint32_t* width, uint32_t* height);

// Get pointer to pixel buffer (mapped into user space)
VError SurfaceGetBuffer(Handle surface, void** buffer, size_t* pitch);

// Present (flip/copy to display)
VError SurfacePresent(Handle surface, uint32_t x, uint32_t y, 
                      uint32_t width, uint32_t height);

// Release the surface
VError SurfaceRelease(Handle surface);
```

### 13.3 Pixel Format

```c
// 32-bit BGRA (native framebuffer format)
typedef struct {
    uint8_t b, g, r, a;
} VPixel;
```

---

## 14. Input

### 14.1 Overview

Poll-based keyboard and mouse input. US keyboard layout only for v0.

### 14.2 Input Polling

```c
// Get input device handle
VError InputGetHandle(uint32_t device_type, Handle* handle);
// device_type: 0=keyboard, 1=mouse

// Poll for input events
VError InputPoll(Handle input, VInputEvent* events, 
                 size_t max_events, size_t* num_events);
```

### 14.3 Input Event Structure

```c
typedef struct VInputEvent {
    uint32_t type;        // VINPUT_KEY, VINPUT_MOUSE_MOVE, etc.
    uint32_t code;        // Key code or button
    int32_t  value;       // Key state, mouse delta, etc.
    uint64_t timestamp;   // Nanoseconds since boot
} VInputEvent;
```

### 14.4 Key Codes

Standard USB HID key codes.

---

## 15. Bootstrap & Drivers

### 15.1 v0 Driver Model

**In v0, all drivers run in the kernel.** This is a pragmatic choice to get a working system before adding the
complexity of userspace driver infrastructure (MMIO mapping, IRQ delivery, DMA primitives).

The kernel provides syscalls for graphics, input, and filesystem that abstract over the underlying drivers. Applications
use these syscalls; they don't talk to drivers directly.

**Future (post-v1.0):** Userspace drivers will require additional kernel primitives:

- MMIO region mapping (capability-gated)
- IRQ subscription and delivery (pollable)
- DMA-safe memory allocation
- Physical address queries (for DMA descriptors)

These primitives are not specified for v0.

### 15.2 Kernel Drivers (v0)

All drivers are built into the kernel:

| Driver       | Purpose        | Notes                      |
|--------------|----------------|----------------------------|
| PL011 UART   | Serial console | Debug output, early boot   |
| ARM Timer    | Scheduler tick | 1000 Hz, architected timer |
| GICv2/GICv3  | Interrupts     | Interrupt controller       |
| virtio-blk   | Block device   | Disk I/O for ViperFS       |
| virtio-gpu   | Framebuffer    | Display, surfaces          |
| virtio-input | Keyboard/mouse | Input events               |

### 15.3 vinit

The first user-space process:

1. Loaded by kernel from initrd
2. Receives initial capabilities (root directory, console surface, input devices)
3. Starts essential services (vlog)
4. Executes startup scripts
5. Spawns vsh (shell)

vinit receives handles for all system resources it needs. It does not discover hardware or load drivers—that's the
kernel's job in v0.

---

## 16. Hardware Abstraction Layer

### 16.1 Platform Interface

```c
// Platform initialization
void platform_init(const VBootInfo* boot_info);

// Timer
void timer_init(uint32_t hz);
uint64_t timer_get_ns(void);
void timer_set_deadline(uint64_t ns);

// Serial (PL011)
void serial_init(uintptr_t base);
void serial_putc(char c);
int  serial_getc(void);  // Returns -1 if no char

// Interrupts (GIC)
void gic_init(uintptr_t dist_base, uintptr_t cpu_base);
void gic_enable_irq(uint32_t irq);
void gic_disable_irq(uint32_t irq);
uint32_t gic_ack_irq(void);
void gic_eoi(uint32_t irq);
```

### 16.2 Exception Handling

```c
// Exception syndrome parsing
typedef struct {
    uint32_t ec;          // Exception class (ESR_EL1[31:26])
    uint32_t iss;         // Instruction-specific syndrome
    uint64_t far;         // Fault address (FAR_EL1)
    uint64_t elr;         // Return address (ELR_EL1)
} VException;

void exception_handler(VException* exc);
```

### 16.3 Device Tree

ViperDOS parses the DTB provided by firmware/vboot to discover:

- Memory regions
- Interrupt controller configuration
- Timer frequency
- Serial port address
- virtio device addresses

---

# Part III: User Space

---

## 17. Installed System

This section defines the exact files present on a freshly installed ViperDOS system.

### 17.1 Complete File Manifest

```
SYS:                                    # Boot device (D0:\)
│
├── kernel.sys                          # Kernel binary (AArch64)
│
├── l\                                  # Handlers (reserved)
│   └── (reserved for future)
│
├── libs\                               # Viper runtime libraries
│   ├── Viper.Core.vlib                 # Core types, memory
│   ├── Viper.System.vlib               # Syscall wrappers
│   ├── Viper.IO.vlib                   # File I/O
│   ├── Viper.Text.vlib                 # Strings, formatting
│   ├── Viper.Collections.vlib          # Data structures
│   ├── Viper.Console.vlib              # Console I/O
│   ├── Viper.Graphics.vlib             # Drawing primitives
│   ├── Viper.Math.vlib                 # Math functions
│   ├── Viper.Time.vlib                 # Time, timers
│   ├── Viper.Async.vlib                # Tasks, channels
│   └── Viper.Serialize.vlib            # Serialization
│
├── c\                                  # Commands
│   ├── assign.vpr
│   ├── avail.vpr
│   ├── break.vpr
│   ├── copy.vpr
│   ├── date.vpr
│   ├── delete.vpr
│   ├── dir.vpr
│   ├── echo.vpr
│   ├── endshell.vpr
│   ├── execute.vpr
│   ├── info.vpr
│   ├── list.vpr
│   ├── makedir.vpr
│   ├── newshell.vpr
│   ├── path.vpr
│   ├── protect.vpr
│   ├── reboot.vpr
│   ├── rename.vpr
│   ├── run.vpr
│   ├── search.vpr
│   ├── shutdown.vpr
│   ├── sort.vpr
│   ├── status.vpr
│   ├── time.vpr
│   ├── type.vpr
│   ├── version.vpr
│   └── wait.vpr
│
├── s\                                  # Startup scripts (Viper BASIC)
│   ├── startup.bas                     # System startup
│   └── shell-startup.bas               # Shell initialization
│
├── t\                                  # Temporary files
│   └── (empty)
│
├── fonts\                              # System fonts
│   └── topaz.vfont                     # Default 8x16 bitmap font
│
├── viper\                              # Core Viper programs
│   ├── vinit.vpr                       # Init process
│   ├── vsh.vpr                         # Shell
│   └── vlog.vpr                        # System logger
│
├── cfg\                                # Configuration
│   └── system.vcfg                     # System settings
│
└── home\                               # User home directories
    └── default\                        # Default user
        └── (empty)
```

---

## 18. Directory Layout

### 18.1 Logical Devices (Assigns)

| Device   | Points To        | Purpose         |
|----------|------------------|-----------------|
| `SYS:`   | D0:\             | Boot device     |
| `C:`     | SYS:c            | Commands        |
| `S:`     | SYS:s            | Startup scripts |
| `L:`     | SYS:l            | Handlers        |
| `LIBS:`  | SYS:libs         | Viper libraries |
| `FONTS:` | SYS:fonts        | System fonts    |
| `T:`     | SYS:t            | Temporary files |
| `HOME:`  | SYS:home\default | User home       |
| `RAM:`   | (memory)         | RAM disk        |

### 18.2 Physical Drives

Physical drives are named `D0:`, `D1:`, `D2:`, etc.

### 18.3 Path Syntax

- Backslash `\` as path separator
- Device prefix with colon: `SYS:c\dir.vpr`
- No device = relative to current directory

---

## 19. Standard Library

### 19.1 Namespace Hierarchy

```
Viper.Core           # Fundamental types, memory
Viper.System         # Syscall wrappers, handles
Viper.IO             # Files, streams, devices
Viper.Text           # Strings, formatting, parsing
Viper.Collections    # List, Map, Set, Queue
Viper.Console        # Text I/O, colors, cursor
Viper.Graphics       # Surfaces, drawing, fonts
Viper.Math           # Numbers, vectors, matrices
Viper.Time           # Clock, timers, duration
Viper.Async          # Tasks, channels, poll
Viper.Serialize      # Binary, text serialization
```

---

## 20. Core Utilities

### 20.1 Commands Overview

| Command  | Purpose                       |
|----------|-------------------------------|
| Assign   | Create/remove logical devices |
| Avail    | Show memory availability      |
| Break    | Send break to task            |
| Copy     | Copy files                    |
| Date     | Show/set date                 |
| Delete   | Delete files                  |
| Dir      | Brief directory listing       |
| Echo     | Print text                    |
| EndShell | Exit current shell            |
| Execute  | Run script                    |
| Info     | Device information            |
| List     | Detailed directory listing    |
| MakeDir  | Create directory              |
| NewShell | Open new shell                |
| Path     | Show/modify command path      |
| Protect  | Set file protection           |
| Reboot   | Restart system                |
| Rename   | Rename/move files             |
| Run      | Start program in background   |
| Search   | Find text in files            |
| Shutdown | Power off system              |
| Sort     | Sort lines                    |
| Status   | Show running tasks            |
| Time     | Show/set time                 |
| Type     | Display file contents         |
| Version  | Show version info             |
| Wait     | Wait for time/condition       |

### 20.2 Return Codes

| Code | Meaning                   |
|------|---------------------------|
| 0    | OK (success)              |
| 5    | WARN (warning, non-fatal) |
| 10   | ERROR (operation failed)  |
| 20   | FAIL (complete failure)   |

### 20.3 File Protection Flags

| Flag | Meaning                                |
|------|----------------------------------------|
| `r`  | Readable                               |
| `w`  | Writable                               |
| `e`  | Executable                             |
| `d`  | Deletable                              |
| `s`  | Script (execute with shell)            |
| `p`  | Pure (reentrant, can be made resident) |
| `a`  | Archived (backed up)                   |
| `h`  | Hidden                                 |

---

## 21. Shell (vsh)

### 21.1 Prompt

```
SYS:> _
HOME:> _
WORK:projects> _
D1:data\logs> _
```

Format: `{device}:{path}> `

### 21.2 Built-in Commands

| Command         | Description                    |
|-----------------|--------------------------------|
| `Cd`            | Change current directory       |
| `Set`           | Set local environment variable |
| `Unset`         | Remove environment variable    |
| `Alias`         | Create command alias           |
| `Unalias`       | Remove alias                   |
| `History`       | Show command history           |
| `If/Else/EndIf` | Conditional execution          |
| `Skip`          | Skip to label                  |
| `Lab`           | Define label                   |
| `Quit`          | Exit shell with return code    |
| `Why`           | Show why last command failed   |
| `Resident`      | Make command memory-resident   |

### 21.3 Environment Variables

| Variable   | Purpose                       |
|------------|-------------------------------|
| `$RC`      | Return code of last command   |
| `$Result`  | Result string of last command |
| `$Process` | Current process number        |
| `$Prompt`  | Shell prompt format           |

### 21.4 Script Syntax (Viper BASIC)

Shell scripts are written in Viper BASIC, the native scripting language of ViperDOS.

```basic
' This is a comment
REM This is also a comment

' Simple commands using Shell()
Shell "Dir SYS:c"
Shell "Copy", "WORK:", "TO", "BACKUP:", "ALL"

' Variables
Dim name As String = "ViperDOS"
Print "Welcome to "; name

' Conditionals
If FileExists("S:user-startup") Then
    Shell "Execute S:user-startup"
Else
    Print "No user startup"
End If

' Return codes from shell commands
Dim rc As Integer = Shell("Copy myfile.txt T:")
If rc >= 10 Then
    Print "Error occurred"
    End rc
End If

' Loops
For i As Integer = 1 To 10
    Print "Processing file "; i
Next

Do While Not FileExists("T:done")
    Shell "ProcessNext"
    Sleep 1000
Loop

' Command line arguments
Print "First argument: "; Args(1)

' Subroutines
Sub Backup(source As String, dest As String)
    Shell "Copy", source, "TO", dest, "ALL"
End Sub

Backup "WORK:", "BACKUP:"
```

### 21.5 Redirection & Pipes

Redirection is handled through Viper BASIC file I/O or shell command syntax:

```basic
' Output redirection via Shell
Shell "List >RAM:dirlist.txt"
Shell "Dir >>RAM:dirlist.txt"       ' Append

' Input redirection
Shell "Sort <RAM:unsorted.txt >RAM:sorted.txt"

' Pipes (v0.2+)
Shell "List | Search .vpr"

' Or use native BASIC file I/O
Open "RAM:output.txt" For Output As #1
Print #1, "Hello from Viper BASIC"
Close #1
```

### 21.6 Startup Sequence

1. `S:startup.bas` (system startup)
2. `S:shell-startup.bas` (shell initialization)
3. `S:user-startup.bas` (user customization, if exists)

Example `S:startup.bas`:

```basic
' ViperDOS Startup Script
Print "ViperDOS starting..."

' Set up assigns
Shell "Assign LIBS: SYS:libs"
Shell "Assign FONTS: SYS:fonts"
Shell "Assign C: SYS:c"
Shell "Assign T: SYS:t"

' Set path
Shell "Path C: ADD"

' Start system services
Shell "Run >NIL: SYS:viper\vlog.vpr"

Print "Welcome to ViperDOS!"
```

### 21.7 Colors

| Element            | Color                 |
|--------------------|-----------------------|
| Prompt             | Viper green (#00AA44) |
| Normal text        | Viper green           |
| Errors             | Red (#CC3333)         |
| Directories (List) | White (#EEEEEE)       |
| Executables (List) | Bright green          |
| Protected files    | Dim green (#006633)   |

### 21.8 Key Bindings

| Key    | Action                     |
|--------|----------------------------|
| ↑/↓    | History navigation         |
| Tab    | Command/path completion    |
| Ctrl+C | Send break to current task |
| Ctrl+D | End of input (EOF)         |
| Ctrl+\ | Quit shell                 |

---

## 22. Configuration Format

### 22.1 ViperConfig (.vcfg)

A simple, readable configuration format designed for ViperDOS.

### 22.2 String Escaping Rules

**Backslash is NOT an escape character.** Backslash is literal in all contexts, allowing natural path representation.

```vcfg
# These are equivalent - backslash is literal
path = "\home\default"
path = "\home\default"

# Supported escape sequences (using $):
message = "Hello$nWorld"      # $n = newline
message = "Tab$there"         # $t = tab
message = "Quote: $q"         # $q = double quote
message = "Dollar: $$"        # $$ = literal $
```

| Escape | Meaning        |
|--------|----------------|
| `$n`   | Newline (0x0A) |
| `$t`   | Tab (0x09)     |
| `$q`   | Double quote   |
| `$$`   | Literal $      |

### 22.3 Syntax

```vcfg
# This is a comment

# Simple key-value pairs
hostname = "viperdos"
version = 1
enabled = true
timeout = 30.5

# Strings (double quotes, backslash is literal)
message = "Hello, World!"
path = "\home\default"

# Sections
[display]
width = 1024
height = 768
fullscreen = true

[network]
dhcp = true
hostname = "viperdos"

# Nested sections
[network.dns]
primary = "8.8.8.8"
secondary = "8.8.4.4"

# Arrays
colors = ["red", "green", "blue"]
ports = [80, 443, 8080]

# Inline table
point = { x = 10, y = 20 }

# Array of tables
[[users]]
name = "admin"
role = "administrator"

[[users]]
name = "guest"
role = "viewer"
```

---

## 23. File Formats

### 23.1 Viper Executable (.vpr)

```c
typedef struct VPRHeader {
    uint32_t magic;           // "VPR\0" (0x00525056)
    uint32_t version;
    uint32_t flags;
    uint32_t entry_point;     // Offset to entry
    uint32_t code_offset;
    uint32_t code_size;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t import_offset;
    uint32_t import_count;
    uint32_t export_offset;
    uint32_t export_count;
} VPRHeader;
```

### 23.2 Viper Library (.vlib)

```c
typedef struct VLibHeader {
    uint32_t magic;           // "VLIB"
    uint32_t version;
    uint32_t flags;
    char     name[64];
    uint32_t export_count;
    // Followed by export table
} VLibHeader;
```

### 23.3 Viper Font (.vfont)

```c
typedef struct VFontHeader {
    uint32_t magic;           // "FONT"
    uint8_t  width;
    uint8_t  height;
    uint16_t num_glyphs;
    uint32_t flags;
    // Followed by glyph bitmap data
} VFontHeader;
```

### 23.4 Configuration (.vcfg)

Text format as defined in Section 22.

---

# Part IV: Development

---

## 24. Syscall Reference

```c
// All syscalls return: X0=VError, X1..X3=results
// This is the canonical ABI. No exceptions.

enum VSyscall {
    // Memory (0x00xx)
    VSYS_HeapAlloc       = 0x0001,  // (kind, size) -> X1=handle
    VSYS_HeapRetain      = 0x0002,  // (handle) -> X0 only
    VSYS_HeapRelease     = 0x0003,  // (handle) -> X0 only
    VSYS_HeapGetLen      = 0x0004,  // (handle) -> X1=length
    VSYS_HeapSetLen      = 0x0005,  // (handle, len) -> X0 only
    VSYS_HeapGetBuffer   = 0x0006,  // (handle) -> X1=user_ptr
    
    // Tasks (0x001x)
    VSYS_TaskSpawn       = 0x0010,  // (entry, arg) -> X1=task_handle
    VSYS_TaskYield       = 0x0011,  // () -> X0 only
    VSYS_TaskExit        = 0x0012,  // (code) -> noreturn
    VSYS_TaskCancel      = 0x0013,  // (handle) -> X0 only
    VSYS_TaskJoin        = 0x0014,  // (handle) -> X0=WOULD_BLOCK or VOK, X1=exit_code
    VSYS_TaskCurrent     = 0x0015,  // () -> X1=handle
    VSYS_ViperCurrent    = 0x0016,  // () -> X1=handle
    
    // Channels (0x002x)
    VSYS_ChannelCreate   = 0x0020,  // () -> X1=send_handle, X2=recv_handle
    VSYS_ChannelSend     = 0x0021,  // (ch, data, len, handles, num_handles) -> X0 only
    VSYS_ChannelRecv     = 0x0022,  // (ch, buf, len, handles_buf, handles_cap) -> X1=data_len, X2=num_handles
    VSYS_ChannelClose    = 0x0023,  // (handle) -> X0 only
    
    // Capabilities (0x003x)
    VSYS_CapDerive       = 0x0030,  // (handle, rights) -> X1=new_handle
    VSYS_CapRevoke       = 0x0031,  // (handle) -> X0 only
    VSYS_CapQuery        = 0x0032,  // (handle) -> X1=rights, X2=kind
    
    // Filesystem (0x004x)
    VSYS_FsOpenRoot      = 0x0040,  // (volume) -> X1=dir_handle
    VSYS_FsOpen          = 0x0041,  // (dir, name, name_len, flags) -> X1=handle, X2=kind
    VSYS_FsReadDir       = 0x0042,  // (dir, buf, max_entries) -> X1=num_entries
    VSYS_FsCreate        = 0x0043,  // (dir, name, name_len, kind) -> X1=handle
    VSYS_FsDelete        = 0x0044,  // (dir, name, name_len) -> X0 only
    VSYS_FsRename        = 0x0045,  // (src_dir, src, src_len, dst_dir, dst, dst_len) -> X0 only
    VSYS_FsStat          = 0x0046,  // (handle, info_ptr) -> X0 only
    VSYS_FsClose         = 0x0047,  // (handle) -> X0 only
    
    // I/O (0x005x)
    VSYS_IORead          = 0x0050,  // (handle, buf, len) -> X1=bytes_read
    VSYS_IOWrite         = 0x0051,  // (handle, data, len) -> X1=bytes_written
    VSYS_IOSeek          = 0x0052,  // (handle, offset, whence) -> X1=new_pos
    VSYS_IOControl       = 0x0053,  // (handle, cmd, arg) -> X1=result
    
    // Polling (0x006x)
    VSYS_PollCreate      = 0x0060,  // () -> X1=poll_handle
    VSYS_PollAdd         = 0x0061,  // (poll, target, events, token) -> X0 only
    VSYS_PollRemove      = 0x0062,  // (poll, target) -> X0 only
    VSYS_PollWait        = 0x0063,  // (poll, events_buf, max, timeout_ns) -> X1=num_events
    
    // Time (0x007x)
    VSYS_TimeNow         = 0x0070,  // () -> X1=nanoseconds
    VSYS_TimerCreate     = 0x0071,  // (deadline_ns) -> X1=timer_handle
    VSYS_TimerCancel     = 0x0072,  // (handle) -> X0 only
    // Note: Sleep is NOT a syscall. Use TimerCreate + PollWait.
    
    // Graphics (0x008x)
    VSYS_SurfaceAcquire  = 0x0080,  // () -> X1=surface, X2=width, X3=height
    VSYS_SurfaceRelease  = 0x0081,  // (surface) -> X0 only
    VSYS_SurfacePresent  = 0x0082,  // (surface, x, y, w, h) -> X0 only
    VSYS_SurfaceGetBuffer= 0x0083,  // (surface) -> X1=buffer_ptr, X2=pitch
    
    // Input (0x009x)
    VSYS_InputGetHandle  = 0x0090,  // (device_type) -> X1=handle
    VSYS_InputPoll       = 0x0091,  // (input, events_buf, max) -> X1=num_events
    
    // Debug (0x00Fx)
    VSYS_DebugPrint      = 0x00F0,  // (string, len) -> X0 only
    VSYS_DebugBreak      = 0x00F1,  // () -> X0 only
    VSYS_DebugPanic      = 0x00F2,  // (message, len) -> noreturn
    
    // Viper (0x010x)
    VSYS_ViperSpawn      = 0x0100,  // (executable, args) -> X1=viper_handle
    VSYS_ViperExit       = 0x0101,  // (code) -> noreturn
    VSYS_ViperWait       = 0x0102,  // (handle) -> X0=WOULD_BLOCK or VOK, X1=exit_code
};
```

---

## 25. Testing Infrastructure

### 25.1 QEMU Setup

ViperDOS development uses QEMU's `virt` machine for AArch64:

```bash
qemu-system-aarch64 \
    -machine virt \
    -cpu cortex-a72 \
    -m 128M \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.fd \
    -drive format=raw,file=build/esp.img \
    -serial stdio \
    -display sdl
```

### 25.2 QEMU Modes

#### 25.2.1 Graphical Mode (Development)

```bash
./scripts/run-qemu.sh --gui
```

Opens QEMU with:

- SDL window showing ViperDOS display
- Serial output mirrored to terminal
- Interactive keyboard/mouse input
- Full boot splash and graphics console

#### 25.2.2 Headless Mode (Automated Testing)

```bash
./scripts/run-qemu.sh --headless
```

Opens QEMU with:

- No display window (`-display none`)
- Serial output to stdout
- VNC available for debugging (`-vnc :0`)
- Scriptable via QEMU monitor
- Exit codes for test automation

### 25.3 Test Framework

```bash
# Run all tests
./scripts/test.sh

# Run specific test suite
./scripts/test.sh --suite boot
./scripts/test.sh --suite memory
./scripts/test.sh --suite syscall

# Run with verbose output
./scripts/test.sh --verbose

# Generate test report
./scripts/test.sh --report junit > results.xml
```

### 25.4 Test Script Example

```bash
#!/bin/bash
# test-boot.sh - Verify kernel boots successfully

TIMEOUT=30
EXPECTED="ViperDOS v"

result=$(timeout $TIMEOUT ./scripts/run-qemu.sh --headless 2>&1)

if echo "$result" | grep -q "$EXPECTED"; then
    echo "PASS: Kernel boot"
    exit 0
else
    echo "FAIL: Kernel boot"
    echo "Output:"
    echo "$result"
    exit 1
fi
```

### 25.5 QEMU Script: run-qemu.sh

```bash
#!/bin/bash
set -e

# Defaults
MODE="gui"
MEMORY="128M"
CPU="cortex-a72"
DEBUG=""
SERIAL="stdio"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --headless)
            MODE="headless"
            shift
            ;;
        --gui)
            MODE="gui"
            shift
            ;;
        --memory)
            MEMORY="$2"
            shift 2
            ;;
        --cpu)
            CPU="$2"
            shift 2
            ;;
        --debug)
            DEBUG="-s -S"
            shift
            ;;
        --serial-file)
            SERIAL="file:$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Find AAVMF firmware
AAVMF="/usr/share/AAVMF/AAVMF_CODE.fd"
if [[ ! -f "$AAVMF" ]]; then
    AAVMF="/usr/share/qemu-efi-aarch64/QEMU_EFI.fd"
fi

# Build display options
if [[ "$MODE" == "headless" ]]; then
    DISPLAY_OPTS="-display none -vnc :0"
else
    DISPLAY_OPTS="-display sdl"
fi

# Run QEMU
exec qemu-system-aarch64 \
    -machine virt \
    -cpu $CPU \
    -m $MEMORY \
    -drive if=pflash,format=raw,readonly=on,file=$AAVMF \
    -drive format=raw,file=build/esp.img \
    -device virtio-blk-device,drive=hd0 \
    -drive id=hd0,format=raw,file=build/disk.img,if=none \
    -device virtio-gpu-device \
    -device virtio-keyboard-device \
    -device virtio-mouse-device \
    -serial $SERIAL \
    $DISPLAY_OPTS \
    $DEBUG \
    -no-reboot
```

### 25.6 Continuous Integration

```yaml
# .github/workflows/test.yml
name: ViperDOS Tests
on: [push, pull_request]

jobs:
  build-and-test:
    runs-on: ubuntu-latest
    
    steps:
      - uses: actions/checkout@v3
      
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y qemu-system-aarch64 qemu-efi-aarch64 build-essential
          
      - name: Install AArch64 toolchain
        run: |
          sudo apt-get install -y gcc-aarch64-linux-gnu
          
      - name: Build kernel
        run: |
          mkdir build && cd build
          cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/aarch64-toolchain.cmake ..
          make
          
      - name: Build boot image
        run: ./scripts/make-esp.sh
        
      - name: Run boot test
        run: ./scripts/test.sh --suite boot
        
      - name: Run memory tests
        run: ./scripts/test.sh --suite memory
        
      - name: Upload test results
        uses: actions/upload-artifact@v3
        with:
          name: test-results
          path: build/test-results/
```

### 25.7 Debug Mode

```bash
# Start QEMU paused, waiting for debugger
./scripts/run-qemu.sh --debug

# In another terminal:
aarch64-linux-gnu-gdb build/kernel/kernel.sys
(gdb) target remote :1234
(gdb) break kernel_entry
(gdb) continue
```

### 25.8 Cross-Compilation Toolchain

```cmake
# cmake/aarch64-toolchain.cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_ASM_COMPILER aarch64-linux-gnu-gcc)

set(CMAKE_C_FLAGS "-mcpu=cortex-a72 -ffreestanding -nostdlib")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-exceptions -fno-rtti")

set(CMAKE_EXE_LINKER_FLAGS "-nostdlib -static")
```

---

## 26. Development Phases

### Phase 1: Graphics Boot (Months 1-3)

**Goal:** Boot to graphical console with ViperDOS logo.

Deliverables:

- vboot bootloader (UEFI, AArch64)
- Kernel boots on QEMU virt
- Graphics console (8x16 font)
- Boot splash
- kprintf to screen + serial (PL011)
- Memory management basics
- Exception vector setup

**Milestone:** "Hello from ViperDOS" displayed graphically.

### Phase 2: Multitasking (Months 4-6)

**Goal:** Multiple tasks, IPC.

Deliverables:

- VTask struct, scheduler
- Context switching (AArch64)
- Channels
- PollWait
- ARM architected timer interrupts
- GIC interrupt handling

**Milestone:** Two tasks ping-pong messages.

### Phase 3: User Space (Months 7-9)

**Goal:** First user-space Viper.

Deliverables:

- Per-Viper address spaces (TTBR0_EL1 switching)
- Capability tables
- KHeap syscalls
- SVC syscall entry/exit
- vinit (kernel-loaded)

**Milestone:** Hello world in user space.

### Phase 4: Filesystem & Shell (Months 10-12)

**Goal:** Boot from disk, interactive shell.

Deliverables:

- ViperFS implementation
- virtio-blk driver
- vinit, vsh from disk
- Core commands (Dir, List, Copy, etc.)
- Assign and path system
- Configuration loading

**Milestone:** Boot to `SYS:>` prompt.

### Phase 5: Polish & Input (Months 13-15)

**Goal:** Usable interactive system.

Deliverables:

- virtio-input driver (keyboard, mouse)
- Line editing in shell
- Command history
- More utilities
- Font system (loadable fonts)

**Milestone:** Complete shell experience.

### Phase 6: Networking (Months 16-18)

**Goal:** Network connectivity.

Deliverables:

- virtio-net driver
- TCP/IP stack
- DNS client
- HTTP client
- Viper.Net library

**Milestone:** Fetch webpage from ViperDOS.

### Beyond v1.0: Self-Hosting

**Goal:** ViperDOS fully hosts the Viper Platform.

The ultimate vision:

- **ViperIDE** runs natively on ViperDOS
- **Viper compiler** builds applications on ViperDOS itself
- **Full runtime library** — all Viper.* namespaces functional
- **Development workflow** — edit, compile, run, debug without leaving ViperDOS
- **Viper Computer** — ViperDOS ships as the default OS on Viper hardware

At this point, ViperDOS becomes self-sustaining: you can develop Viper applications on ViperDOS, for ViperDOS, using
tools
written in Viper languages.

---

## 27. Design Decisions Summary

| Question             | Decision                                |
|----------------------|-----------------------------------------|
| Architecture         | **AArch64 exclusively**                 |
| Heritage             | Clean-slate (not UNIX, not POSIX)       |
| Boot display         | Graphics-first (framebuffer console)    |
| Boot splash          | Simple ViperDOS logo                    |
| Console colors       | Green on dark brown                     |
| Panic colors         | Yellow on green                         |
| Shell prompt         | `SYS:>` (device:path style)             |
| Device naming        | Logical assigns (SYS:, HOME:, C:, etc.) |
| Physical drives      | D0:, D1:, D2:, ...                      |
| Path separator       | Backslash `\`                           |
| Commands directory   | `C:` (SYS:c)                            |
| Startup scripts      | `S:` (SYS:s)                            |
| Scripting language   | Viper BASIC                             |
| Config format        | ViperConfig (.vcfg) - custom            |
| Config escaping      | $ for escapes, backslash is literal     |
| Font (v0)            | Topaz - baked-in 8x16 bitmap            |
| Testing              | QEMU virt + virtio devices              |
| Boot protocol        | Custom vboot + VBootInfo                |
| Object kinds         | Expanded (task, timer, poll, etc.)      |
| Tasks                | 1:1 kernel threads                      |
| Blocking             | Only PollWait (no exceptions)           |
| Heap                 | KHeap/LHeap split, kernel-only metadata |
| Scheduler            | Round-robin, 10ms time slice            |
| Return codes         | 0=OK, 5=WARN, 10=ERROR, 20=FAIL         |
| Syscall ABI          | X0=error always, X1-X3=results          |
| Syscall mechanism    | SVC #0 instruction                      |
| Exception levels     | EL0=user, EL1=kernel                    |
| Timer                | ARM architected timer                   |
| Interrupt controller | GICv2/GICv3                             |
| Serial               | PL011 UART                              |
| Drivers (v0)         | All in kernel (userspace later)         |
| Filesystem (v0)      | No journaling (added in v0.5+)          |
| IPC cap passing      | Yes, via ChannelSend/Recv               |

---

## Appendix A: Color Reference

| Name        | Hex     | RGB           | Usage                |
|-------------|---------|---------------|----------------------|
| Viper Green | #00AA44 | 0, 170, 68    | Primary text         |
| Dark Brown  | #1A1208 | 26, 18, 8     | Background           |
| Yellow      | #FFDD00 | 255, 221, 0   | Warnings, panic text |
| Dim Green   | #006633 | 0, 102, 51    | Secondary text       |
| White       | #EEEEEE | 238, 238, 238 | Bright text          |
| Red         | #CC3333 | 204, 51, 51   | Errors               |
| Panic BG    | #00AA44 | 0, 170, 68    | Panic background     |

---

## Appendix B: Quick Reference

### Default Assigns

| Device   | Points To        | Purpose         |
|----------|------------------|-----------------|
| `SYS:`   | D0:\             | Boot device     |
| `C:`     | SYS:c            | Commands        |
| `S:`     | SYS:s            | Startup scripts |
| `L:`     | SYS:l            | Handlers        |
| `LIBS:`  | SYS:libs         | Viper libraries |
| `FONTS:` | SYS:fonts        | System fonts    |
| `T:`     | SYS:t            | Temporary files |
| `HOME:`  | SYS:home\default | User home       |
| `RAM:`   | (memory)         | RAM disk        |

### Syscall by Category

| Category   | Syscalls                                          |
|------------|---------------------------------------------------|
| Memory     | HeapAlloc, HeapRetain, HeapRelease, HeapGetBuffer |
| Tasks      | TaskSpawn, TaskYield, TaskExit, TaskJoin          |
| Channels   | ChannelCreate, ChannelSend, ChannelRecv           |
| Filesystem | FsOpenRoot, FsOpen, FsReadDir, FsCreate, FsStat   |
| I/O        | IORead, IOWrite, IOSeek, IOControl                |
| Poll       | PollCreate, PollAdd, PollWait                     |
| Time       | TimeNow, TimerCreate, TimerCancel                 |
| Graphics   | SurfaceAcquire, SurfacePresent, SurfaceGetBuffer  |
| Input      | InputGetHandle, InputPoll                         |

### Core Commands (C:)

| Command | Purpose                    |
|---------|----------------------------|
| Dir     | Brief directory listing    |
| List    | Detailed directory listing |
| Type    | Display file contents      |
| Copy    | Copy files                 |
| Delete  | Delete files               |
| Rename  | Rename/move files          |
| MakeDir | Create directory           |
| Info    | Device information         |
| Avail   | Memory available           |
| Status  | Running tasks              |
| Assign  | Manage logical devices     |
| Run     | Background execution       |

### Shell Built-ins

| Command       | Purpose               |
|---------------|-----------------------|
| Cd            | Change directory      |
| Set/Unset     | Environment variables |
| Alias         | Command aliases       |
| If/Else/EndIf | Conditionals          |
| Quit          | Exit with code        |

### AArch64 Syscall ABI

```
Invocation:
  X8  = syscall number
  X0  = arg0
  X1  = arg1
  X2  = arg2
  X3  = arg3
  X4  = arg4
  X5  = arg5
  SVC #0

Return (always):
  X0  = VError (0=success, negative=error)
  X1  = result0 (handle, size, etc.)
  X2  = result1 (if needed)
  X3  = result2 (if needed)
```

---

*"Software should be art."*
