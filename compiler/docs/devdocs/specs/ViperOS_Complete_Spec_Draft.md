# ViperOS — Technical Specification v1.2

## Complete Implementation-Ready Specification

**Version:** 1.2  
**Date:** November 2025  
**Status:** Implementation-Ready (v0 Scope Locked)

---

## Table of Contents

### Part I: Core Architecture

| # | Section                                           | Description                                 |
|---|---------------------------------------------------|---------------------------------------------|
| 1 | [Overview](#1-overview)                           | What ViperOS is, target platforms, goals    |
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
| 16 | [Hardware Abstraction Layer](#16-hardware-abstraction-layer) | Arch and platform interfaces            |

### Part III: User Space

| #  | Section                                          | Description                             |
|----|--------------------------------------------------|-----------------------------------------|
| 17 | [Installed System](#17-installed-system)         | Complete file listing for fresh install |
| 18 | [Directory Layout](#18-directory-layout)         | Filesystem hierarchy, drive naming      |
| 19 | [Standard Library](#19-standard-library)         | Viper.* namespace hierarchy             |
| 20 | [Core Utilities](#20-core-utilities)             | Programs that ship with ViperOS         |
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
| A | [Color Reference](#appendix-a-color-reference) | ViperOS color palette           |
| B | [Quick Reference](#appendix-b-quick-reference) | Syscalls, directories, commands |

---

# Part I: Core Architecture

---

## 1. Overview

### 1.1 What is ViperOS?

ViperOS is a custom operating system designed from scratch to natively host the Viper Platform. Unlike traditional
operating systems that treat applications as opaque binaries, ViperOS understands Viper IL as its native execution
format.

```
┌────────────────────────────────────────────────────────────────┐
│  Viper Application                                             │
├────────────────────────────────────────────────────────────────┤
│  Viper.System Library (VCALL wrappers)                        │
├────────────────────────────────────────────────────────────────┤
│  VCALL Boundary (syscall/svc instruction)                     │
├────────────────────────────────────────────────────────────────┤
│  ViperOS Microkernel                                           │
│  (Scheduler, Memory, IPC, Capabilities, HAL)                  │
└────────────────────────────────────────────────────────────────┘
```

### 1.2 Target Platforms

- **Primary:** x86-64 (QEMU, then real hardware)
- **Secondary:** AArch64 (QEMU virt)

### 1.3 Non-Goals for v0

- POSIX compatibility
- Running existing binaries
- Multi-user support
- Full SMP (designed for it, but v0 is single-core)

---

## 2. Design Principles

### 2.1 Viper-Native

Viper IL is the syscall boundary. Applications don't link against libc; they call kernel services directly through the
`VCALL` IL primitive.

### 2.2 Capability-Based Security

No ambient authority. Programs start with zero capabilities and receive only what they're explicitly granted.

### 2.3 Async-First I/O

No blocking syscalls except `PollWait`. Operations either complete immediately or return `WOULD_BLOCK`.

### 2.4 Microkernel Architecture

The kernel is small (<50,000 lines target). It handles scheduling, memory, IPC, capabilities, and HAL. Everything else
runs in user space.

### 2.5 Shallow Type Awareness

The kernel understands a fixed set of ~8 built-in Viper types. User-defined types are opaque.

### 2.6 Graphics-First

ViperOS boots directly into graphics mode. The console is graphical from the first moment. Serial output is for
debugging only.

---

## 3. Boot Architecture

### 3.1 Boot Sequence Overview

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│  Firmware   │───▶│   vboot     │───▶│   Kernel    │───▶│   vinit     │
│  (UEFI)     │    │ (bootloader)│    │  (ring 0)   │    │ (user space)│
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
3. Sets up initial page tables
4. Exits UEFI boot services
5. Jumps to kernel

#### 3.2.1 ESP Layout

```
ESP (FAT32)
├── EFI/
│   └── BOOT/
│       ├── BOOTX64.EFI      # vboot for x86-64
│       └── BOOTAA64.EFI     # vboot for AArch64
└── viperos/
    ├── kernel.elf           # Kernel image
    ├── splash.raw           # Boot splash image (optional)
    └── initrd.tar           # Initial ramdisk
```

#### 3.2.2 Kernel Loading

```
Kernel ELF:
  Entry point:     0xFFFF_FFFF_8010_0000 (virtual)
  Load address:    0x0000_0000_0010_0000 (physical, 1MB)
```

#### 3.2.3 Page Table Setup

```
PML4[0]   → Identity map (first 1GB, temporary)
PML4[256] → HHDM at 0xFFFF_8000_0000_0000
PML4[511] → Kernel at 0xFFFF_FFFF_8000_0000
```

### 3.3 Boot Handoff

#### 3.3.1 x86-64 Entry State

```
RDI = VBootInfo* (physical address)
RSP = valid 16KB stack
Paging enabled, interrupts disabled
```

#### 3.3.2 AArch64 Entry State

```
X0 = VBootInfo* (physical address)
SP = valid 16KB stack
MMU enabled, interrupts masked
```

### 3.4 VBootInfo Structure

```c
#define VBOOT_MAGIC 0x564F4F5442495056ULL
#define VBOOT_MAX_MEMORY_REGIONS 256

typedef struct {
    uint64_t magic;
    uint32_t version_major, version_minor;
    
    uint32_t memory_region_count;
    uint32_t _pad0;
    VBootMemoryRegion memory_regions[VBOOT_MAX_MEMORY_REGIONS];
    
    VBootFramebuffer framebuffer;
    
    uint64_t hhdm_base;
    uint64_t kernel_phys_base, kernel_virt_base, kernel_size;
    uint64_t initrd_phys, initrd_size;
    uint64_t rsdp_address;  // x86-64
    uint64_t dtb_address;   // AArch64
    
    char cmdline[256];
    uint64_t boot_timestamp_ns;
} VBootInfo;  // 6456 bytes
```

### 3.5 Kernel Initialization Order

1. Validate VBootInfo magic
2. **Initialize graphics console** (framebuffer text output)
3. **Display boot splash** (ViperOS logo)
4. Print boot messages to graphical console
5. Parse memory map
6. Set up GDT/IDT (x86)
7. Initialize physical allocator
8. Initialize virtual memory
9. Remove identity mapping
10. Initialize interrupts
11. Initialize scheduler
12. Start vinit

---

## 4. Graphics Console

### 4.1 Overview

ViperOS boots directly into graphics mode. The graphical console provides text output from the earliest moments of boot.
Serial output is maintained in parallel for debugging.

### 4.2 Color Scheme

```c
// ViperOS color palette
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

A simple ViperOS logo displayed immediately after graphics console initialization:

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

**v0:** Single baked-in 8x16 bitmap font (VGA-style monospace).

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
- Serial port (development/logging)

```c
void kprintf(const char* fmt, ...) {
    // Format string...
    gcon_puts(buffer);   // To screen
    serial_puts(buffer); // To serial (if available)
}
```

### 4.7 Panic Screen

On kernel panic:

```
┌────────────────────────────────────────────────────────────────┐
│ ████████████████████████████████████████████████████████████████│
│ █                                                              █│
│ █                     VIPEROS PANIC                            █│
│ █                                                              █│
│ █  Error: Page fault in kernel mode                            █│
│ █  Code:  0x0000000E                                           █│
│ █                                                              █│
│ █  RIP: 0xFFFFFFFF80102345                                     █│
│ █  RSP: 0xFFFFFFFF80150000                                     █│
│ █  CR2: 0x0000000000001000                                     █│
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

```
User Space:
0x0000_0000_0000_0000    Null guard
0x0000_0000_0000_1000    User space start
0x0000_1000_0000_0000    Code
0x0000_5000_0000_0000    LHeap
0x0000_7000_0000_0000    KHeap
0x0000_7FFF_FFFF_F000    Stack top

Kernel Space:
0xFFFF_8000_0000_0000    HHDM
0xFFFF_C000_0000_0000    Kernel heap
0xFFFF_FFFF_8000_0000    Kernel image
```

### 5.2 Dual Heap Model

**LHeap:** User-space, non-atomic refcounts, no syscalls.

**KHeap:** Kernel-managed, atomic refcounts, for shared objects.

### 5.3 KHeap Header

```c
typedef struct VHeapHeader {
    uint32_t magic;           // "VIPR"
    uint16_t kind;
    uint16_t elem_kind;
    uint32_t flags;
    uint32_t _reserved0;
    _Atomic uint64_t refcnt;
    uint64_t len, cap;
    uint64_t owner_viper;
    uint64_t _reserved1, _reserved2;
} VHeapHeader;  // 64 bytes
```

### 5.4 Handle Representation

32-bit indices into per-Viper capability table.

---

## 6. Kernel Type Awareness

### 6.1 Built-in Kinds

```c
enum VHeapKind {
    VKIND_INVALID, VKIND_STRING, VKIND_ARRAY, VKIND_BLOB,
    VKIND_SURFACE, VKIND_CHANNEL, VKIND_FILE, VKIND_DIRECTORY
};
```

User types are opaque to kernel.

---

## 7. Process Model

### 7.1 Vipers

Isolated execution environments with address space, capabilities, and tasks.

### 7.2 Tasks

1:1 with kernel threads. NOT green threads.

### 7.3 Structured Concurrency

Children cannot outlive parents.

### 7.4 Scheduler (v0)

Round-robin, timer preemption, no priorities.

---

## 8. Syscall Semantics

### 8.1 Golden Rule

All syscalls return immediately except `PollWait`.

### 8.2 Error Codes

```c
enum VError {
    VOK = 0,
    VERR_INVALID_HANDLE = -1,
    VERR_INVALID_ARG = -2,
    VERR_OUT_OF_MEMORY = -3,
    VERR_PERMISSION = -4,
    VERR_WOULD_BLOCK = -5,
    VERR_CHANNEL_CLOSED = -6,
    // ...
};
```

### 8.3 Calling Convention

**x86-64:** rax=syscall#, rdi/rsi/rdx/r10/r8/r9=args

**AArch64:** x8=syscall#, x0-x5=args

---

## 9. Capability System

### 9.1 Rights

```c
enum CapRights {
    CAP_READ, CAP_WRITE, CAP_EXECUTE,
    CAP_LIST, CAP_CREATE, CAP_DELETE,
    CAP_DERIVE, CAP_TRANSFER, CAP_SPAWN,
    CAP_IOPORT, CAP_IRQ, CAP_DMA
};
```

### 9.2 KHeap Objects Are Capabilities

Every KHeap object lives behind a capability.

---

# Part II: Subsystems

---

## 10. IPC: Channels

Non-blocking send/receive. Returns WOULD_BLOCK if can't complete.

---

## 11. Async I/O & Polling

### 11.1 VPollEvent

```c
struct VPollEvent {
    Handle handle;
    uint32_t events;
    int32_t status;
    uint64_t token;
    uint64_t result;
};  // 32 bytes
```

### 11.2 Poll Flags

VPOLL_READABLE, VPOLL_WRITABLE, VPOLL_ERROR, VPOLL_HANGUP, VPOLL_TIMER, VPOLL_IO_DONE, VPOLL_TASK_EXIT, VPOLL_VIPER_EXIT

---

## 12. ViperFS

Capability-based, content-addressed, journaled. Bytes only (typing is runtime).

---

## 13. Graphics

Single framebuffer (v0). Software rendering.

---

## 14. Input

Poll-based keyboard/mouse. US layout only for v0.

---

## 15. Bootstrap & Drivers

Serial, timer in kernel. Block, framebuffer move to user-space later.

---

## 16. Hardware Abstraction Layer

Architecture (context switch, interrupts) and Platform (timer, serial) interfaces.

---

# Part III: User Space

---

## 17. Installed System

This section defines the exact files present on a freshly installed ViperOS system. This is the minimal bootable system.

### 17.1 Complete File Manifest

```
SYS:                                    # Boot device (D0:\)
│
├── kernel.elf                          # Kernel binary
│
├── l\                                  # Libs (handlers, devices)
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
│   ├── assign.vpr                      # Create logical device
│   ├── ask.vpr                         # Prompt for input
│   ├── avail.vpr                       # Show memory
│   ├── break.vpr                       # Stop a task
│   ├── cls.vpr                         # Clear screen
│   ├── copy.vpr                        # Copy files
│   ├── date.vpr                        # Show/set date
│   ├── delete.vpr                      # Delete files
│   ├── dir.vpr                         # Brief directory listing
│   ├── echo.vpr                        # Print text
│   ├── endshell.vpr                    # Close shell
│   ├── execute.vpr                     # Run script
│   ├── info.vpr                        # Device information
│   ├── list.vpr                        # Detailed directory listing
│   ├── makedir.vpr                     # Create directory
│   ├── newshell.vpr                    # Open new shell
│   ├── path.vpr                        # Show/modify search path
│   ├── protect.vpr                     # Set file flags
│   ├── reboot.vpr                      # Restart system
│   ├── rename.vpr                      # Rename/move files
│   ├── run.vpr                         # Run in background
│   ├── search.vpr                      # Find text in files
│   ├── shutdown.vpr                    # Shut down system
│   ├── sort.vpr                        # Sort lines
│   ├── status.vpr                      # Show running tasks
│   ├── time.vpr                        # Show/set time
│   ├── type.vpr                        # Display file contents
│   ├── version.vpr                     # Show version
│   └── wait.vpr                        # Wait/delay
│
├── s\                                  # Startup scripts
│   ├── startup-sequence                # Boot script
│   └── shell-startup                   # Shell init
│
├── prefs\                              # Preferences
│   └── system.vcfg                     # System configuration
│
├── fonts\                              # System fonts
│   └── topaz.vfont                     # Default console font (8x16)
│
├── viper\                              # System services
│   ├── vinit.vpr                       # Init process
│   ├── vsh.vpr                         # Shell
│   └── vlog.vpr                        # System logger
│
├── t\                                  # Temporary files
│   └── (empty, cleared on boot)
│
└── home\                               # User home directories
    └── default\                        # Default user
        └── .vshrc                      # User shell config
```

### 17.2 Logical Device Assigns

ViperOS uses logical device names that map to physical paths:

| Device   | Points To        | Purpose          |
|----------|------------------|------------------|
| `SYS:`   | D0:\             | Boot device root |
| `C:`     | SYS:c            | Commands         |
| `S:`     | SYS:s            | Startup scripts  |
| `L:`     | SYS:l            | Handlers/devices |
| `LIBS:`  | SYS:libs         | Viper libraries  |
| `FONTS:` | SYS:fonts        | System fonts     |
| `PREFS:` | SYS:prefs        | Preferences      |
| `T:`     | SYS:t            | Temporary files  |
| `HOME:`  | SYS:home\default | User home        |
| `RAM:`   | (memory)         | RAM disk         |
| `D0:`    | (physical)       | First disk       |
| `D1:`    | (physical)       | Second disk      |

Users can create additional assigns with the `Assign` command.

### 17.3 File Count Summary

| Directory   | Files        | Purpose            |
|-------------|--------------|--------------------|
| `SYS:`      | 1            | Kernel             |
| `SYS:libs`  | 11           | Standard libraries |
| `SYS:c`     | 29           | Commands           |
| `SYS:s`     | 2            | Startup scripts    |
| `SYS:prefs` | 1            | Configuration      |
| `SYS:fonts` | 1            | Console font       |
| `SYS:viper` | 3            | System services    |
| `HOME:`     | 1            | User shell config  |
| **Total**   | **49 files** |                    |

### 17.4 Disk Space (Estimated)

| Component          | Size        |
|--------------------|-------------|
| Kernel             | ~200 KB     |
| System services    | ~50 KB      |
| Standard libraries | ~300 KB     |
| Commands           | ~200 KB     |
| Fonts              | ~4 KB       |
| Config/scripts     | ~5 KB       |
| **Total**          | **~760 KB** |

Minimum disk requirement: 2 MB (with room for logs and user files).

### 17.5 ESP (EFI System Partition)

The ESP is separate from the ViperOS filesystem:

```
ESP (FAT32, ~64 MB)
├── EFI\
│   └── BOOT\
│       └── BOOTX64.EFI         # vboot bootloader
└── viperos\
    └── kernel.elf              # Kernel (copied from SYS:)
```

---

## 18. Directory Layout

### 18.1 Device-Oriented Hierarchy

ViperOS uses logical devices rather than a single rooted tree. Every path begins with a device name followed
by a colon.

| Device      | Purpose                | Writable  |
|-------------|------------------------|-----------|
| `SYS:`      | Boot device root       | Partially |
| `SYS:c`     | System commands        | No        |
| `SYS:s`     | Startup scripts        | No        |
| `SYS:l`     | Handlers/devices       | No        |
| `SYS:libs`  | Viper libraries        | No        |
| `SYS:fonts` | System fonts           | No        |
| `SYS:prefs` | Preferences            | Yes       |
| `SYS:viper` | System services        | No        |
| `SYS:t`     | Temporary files        | Yes       |
| `SYS:home`  | User directories       | Yes       |
| `HOME:`     | Current user home      | Yes       |
| `T:`        | Temp (alias for SYS:t) | Yes       |
| `RAM:`      | RAM disk               | Yes       |

### 18.2 Physical vs Logical Devices

**Physical devices** map directly to hardware:

| Device | Description            |
|--------|------------------------|
| `D0:`  | First disk (boot disk) |
| `D1:`  | Second disk            |
| `D2:`  | Third disk             |
| `SER:` | Serial port            |
| `CON:` | Console                |

**Logical devices** are assigns that point to paths:

| Device   | Default Assignment  |
|----------|---------------------|
| `SYS:`   | D0:\                |
| `C:`     | SYS:c               |
| `S:`     | SYS:s               |
| `L:`     | SYS:l               |
| `LIBS:`  | SYS:libs            |
| `FONTS:` | SYS:fonts           |
| `PREFS:` | SYS:prefs           |
| `T:`     | SYS:t               |
| `HOME:`  | SYS:home\default    |
| `RAM:`   | (built-in RAM disk) |

### 18.3 The Assign Command

Users can create, modify, or remove logical devices:

```
> assign                          # List all assigns
> assign WORK: D1:projects        # Create new assign
> assign WORK: D1:other           # Change existing
> assign WORK:                    # Remove assign (trailing colon, no path)
> assign add WORK: D0:backup      # Add path to existing (multi-assign)
```

### 18.4 Path Syntax

```
SYS:c\copy.vpr          # Full path with device
c:copy.vpr              # Device-relative (C: assign)
copy.vpr                # Search PATH for command
myfile.txt              # Current directory
\subdir\file.txt        # Relative to current device root
```

Path separator is backslash (`\`). Device names end with colon (`:`).

### 18.5 Path Search Order

When a command is entered without a path:

1. Check if it's a built-in shell command
2. Search current directory
3. Search each directory in the command path (set via `Path` command)
4. Default path: `C:`

### 18.6 Special Directories

| Directory | Purpose                                 |
|-----------|-----------------------------------------|
| `S:`      | Startup scripts (startup-sequence)      |
| `L:`      | Device handlers and libraries           |
| `T:`      | Temporary files (cleared on boot)       |
| `RAM:`    | Fast temporary storage (lost on reboot) |

---

## 19. Standard Library

### 19.1 Namespace Hierarchy

```
Viper.
├── Core           # Base types, memory, errors
├── System         # Syscalls, handles, capabilities
├── IO             # File I/O, streams, paths
├── Text           # String, StringBuilder, UTF-8
├── Collections    # Array, List, Map, Set, Queue
├── Console        # Text input/output
├── Graphics       # Drawing, colors, fonts
├── Math           # Math functions, random
├── Time           # Time, duration, timers
├── Async          # Tasks, channels, polling
├── Serialize      # Binary/text serialization
└── Net            # Networking (Phase 6)
```

### 19.2 Viper.Core

```viper
// Fundamental types
type Bool, Byte, Int16, Int32, Int64, Float32, Float64
type UInt16, UInt32, UInt64
type Char    // UTF-32 code point
type String  // Immutable UTF-8

// Memory
class Ref[T]       // Reference-counted pointer
class WeakRef[T]   // Weak reference
class Box[T]       // Unique ownership

// Errors
enum Result[T, E]  // Ok(T) | Err(E)
class Error        // Base error type
```

### 19.3 Viper.System

```viper
// Syscall wrappers
class Handle
class Capability
struct ViperId
struct TaskId

// Heap operations
func heapAlloc(kind, size) -> Handle
func heapRetain(handle) -> Result
func heapRelease(handle) -> Result

// Process operations
func viperSpawn(module, caps) -> ViperId
func viperExit(code) -> Never
func taskSpawn(entry) -> TaskId
```

### 19.4 Viper.IO

```viper
// Paths
class Path
    func join(other: Path) -> Path
    func parent() -> Path?
    func filename() -> String?
    func extension() -> String?

// Files
class File
    static func open(path: Path, mode: FileMode) -> Result[File, IOError]
    async func read(buffer: Array[Byte]) -> Result[Int, IOError]
    async func write(data: Array[Byte]) -> Result[Int, IOError]
    func close()

// Directories
class Directory
    static func open(path: Path) -> Result[Directory, IOError]
    func list() -> Iterator[DirectoryEntry]
    func create(name: String) -> Result[Directory, IOError]

enum FileMode { Read, Write, ReadWrite, Append, Create, Truncate }
```

### 19.5 Viper.Text

```viper
// Strings
class String
    func length() -> Int
    func charAt(index: Int) -> Char
    func substring(start: Int, end: Int) -> String
    func split(separator: String) -> Array[String]
    func trim() -> String
    func toUpper() -> String
    func toLower() -> String

// Building strings
class StringBuilder
    func append(s: String) -> Self
    func appendLine(s: String) -> Self
    func toString() -> String

// Formatting
func format(template: String, args: ...) -> String
```

### 19.6 Viper.Collections

```viper
class Array[T]
    func length() -> Int
    func get(index: Int) -> T?
    func set(index: Int, value: T)
    func push(value: T)
    func pop() -> T?
    func slice(start: Int, end: Int) -> Array[T]

class List[T]        // Linked list
class Map[K, V]      // Hash map
class Set[T]         // Hash set
class Queue[T]       // FIFO queue
class Stack[T]       // LIFO stack
```

### 19.7 Viper.Console

```viper
// Text I/O
func print(s: String)
func println(s: String)
func readLine() -> String
func readChar() -> Char

// Formatting
func printf(format: String, args: ...)

// Screen control
func clear()
func setCursor(x: Int, y: Int)
func setColor(fg: Color, bg: Color)
func getSize() -> (width: Int, height: Int)
```

### 19.8 Viper.Graphics

```viper
// Colors
struct Color
    static let black, white, red, green, blue, ...
    static let viperGreen = Color(0x00, 0xAA, 0x44)
    static let darkBrown = Color(0x1A, 0x12, 0x08)
    
    static func rgb(r: Byte, g: Byte, b: Byte) -> Color

// Drawing context
class Canvas
    func clear(color: Color)
    func setPixel(x: Int, y: Int, color: Color)
    func drawLine(x1, y1, x2, y2: Int, color: Color)
    func drawRect(x, y, w, h: Int, color: Color)
    func fillRect(x, y, w, h: Int, color: Color)
    func drawText(x, y: Int, text: String, font: Font, color: Color)

// Screen
class Screen
    static func acquire() -> Result[Screen, Error]
    func width() -> Int
    func height() -> Int
    func canvas() -> Canvas
    func present()
```

### 19.9 Viper.Time

```viper
struct Instant
    static func now() -> Instant
    func elapsed() -> Duration

struct Duration
    static func fromNanos(n: Int64) -> Duration
    static func fromMillis(n: Int64) -> Duration
    static func fromSecs(n: Int64) -> Duration
    func toNanos() -> Int64
    func toMillis() -> Int64

async func sleep(duration: Duration)
```

### 19.10 Viper.Async

```viper
// Tasks
func spawn(entry: async func() -> T) -> Task[T]
async func join(task: Task[T]) -> T

// Channels
class Channel[T]
    static func create(capacity: Int) -> Channel[T]
    async func send(value: T) -> Result[(), ChannelError]
    async func recv() -> Result[T, ChannelError]
    func close()

// Polling
class PollSet
    func add(handle: Handle, events: PollEvents)
    func remove(handle: Handle)
    async func wait(timeout: Duration?) -> Array[PollEvent]
```

---

## 20. Core Utilities

These are the 29 commands that ship in `C:` (SYS:c) with every ViperOS installation.

### 20.1 File Commands

| Command   | Synopsis                | Description                           |
|-----------|-------------------------|---------------------------------------|
| `Dir`     | `dir [path]`            | Brief directory listing               |
| `List`    | `list [path]`           | Detailed listing with sizes and dates |
| `Type`    | `type file`             | Display file contents                 |
| `Copy`    | `copy from to [all]`    | Copy files                            |
| `Delete`  | `delete file [file...]` | Delete files                          |
| `Rename`  | `rename old new`        | Rename or move file                   |
| `MakeDir` | `makedir name`          | Create directory                      |
| `Protect` | `protect file [flags]`  | Set file protection flags             |

### 20.2 Information Commands

| Command   | Synopsis        | Description                  |
|-----------|-----------------|------------------------------|
| `Info`    | `info [device]` | Show device/disk information |
| `Avail`   | `avail [chip    | fast                         |total]` | Show memory available |
| `Status`  | `status [task]` | Show running tasks           |
| `Version` | `version`       | Show ViperOS version         |

### 20.3 Text Commands

| Command  | Synopsis              | Description              |
|----------|-----------------------|--------------------------|
| `Echo`   | `echo [text]`         | Print text to output     |
| `Ask`    | `ask prompt`          | Prompt user for yes/no   |
| `Search` | `search pattern file` | Search for text in files |
| `Sort`   | `sort [from] [to]`    | Sort lines               |

### 20.4 Device & Path Commands

| Command  | Synopsis                | Description                        |
|----------|-------------------------|------------------------------------|
| `Assign` | `assign [name: [path]]` | Create/list/remove logical devices |
| `Path`   | `path [dir] [add        | remove]`                           | Show or modify command path |
| `Cd`     | `cd path`               | Change current directory           |

### 20.5 Execution Commands

| Command   | Synopsis         | Description                      |
|-----------|------------------|----------------------------------|
| `Run`     | `run command`    | Run command in background        |
| `Execute` | `execute script` | Run script file                  |
| `Wait`    | `wait [secs]`    | Wait for time or background task |
| `Break`   | `break [task]`   | Send break signal to task        |

### 20.6 Shell Commands

| Command    | Synopsis          | Description           |
|------------|-------------------|-----------------------|
| `Cls`      | `cls`             | Clear screen          |
| `NewShell` | `newshell [con:]` | Open new shell window |
| `EndShell` | `endshell`        | Close current shell   |

### 20.7 System Commands

| Command    | Synopsis      | Description             |
|------------|---------------|-------------------------|
| `Date`     | `date [date]` | Show or set system date |
| `Time`     | `time [time]` | Show or set system time |
| `Shutdown` | `shutdown`    | Shut down system        |
| `Reboot`   | `reboot`      | Restart system          |

### 20.8 Return Codes

All commands return standard codes (accessible via `$RC` variable):

| Code | Meaning                   |
|------|---------------------------|
| 0    | OK (success)              |
| 5    | WARN (warning, non-fatal) |
| 10   | ERROR (operation failed)  |
| 20   | FAIL (complete failure)   |

### 20.9 File Protection Flags

The `Protect` command sets permission flags:

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

Example: `protect myfile.vpr +e-d` (add execute, remove delete)

### 20.10 Example Session

```
SYS:> version
ViperOS 0.1.0 (November 2025)
Viper Platform Runtime

SYS:> avail
Type        Available    In-Use
chip           96 MB      32 MB
total         128 MB      32 MB

SYS:> info
Unit      Size    Used    Free   Full  Name
SYS:     64 MB    2 MB   62 MB     3%  System

SYS:> assign
Volumes:
D0 [Mounted]

Assigns:
SYS:    D0:
C:      SYS:c
S:      SYS:s
L:      SYS:l
LIBS:   SYS:libs
FONTS:  SYS:fonts
T:      SYS:t
HOME:   SYS:home\default

SYS:> assign WORK: D0:home\default\projects
WORK: assigned

SYS:> dir C:
assign      avail       break       cd          cls
copy        date        delete      dir         echo
endshell    execute     info        list        makedir
newshell    path        protect     reboot      rename
run         search      shutdown    sort        status
time        type        version     wait
29 files

SYS:> list SYS:viper
Directory "SYS:viper" on November 26, 2025

vinit.vpr                 12,450  ----rwed  26-Nov-25 10:30:00
vsh.vpr                    8,200  ----rwed  26-Nov-25 10:30:00
vlog.vpr                   6,100  ----rwed  26-Nov-25 10:30:00
3 files - 26,750 bytes used

SYS:> status
Task ID  Status   Pri  Name
      1  Running    0  vinit
      2  Waiting   -5  vlog
      3  Running    0  vsh

SYS:> run myprog
[1] myprog started

SYS:> echo "Hello from ViperOS!"
Hello from ViperOS!

SYS:> ask "Continue?"
Continue? (y/n) y

SYS:> cd HOME:
HOME:> shutdown
Shutting down...
```

---

## 21. Shell (vsh)

### 21.1 Prompt

The shell displays the current device and directory:

```
SYS:> _
HOME:> _
WORK:projects> _
D1:data\logs> _
```

Format: `{device}:{path}> `

The prompt can be customized via the `PROMPT` environment variable.

### 21.2 Built-in Commands

These commands are built into the shell (not external programs):

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

### 21.4 Script Syntax

Scripts use the following syntax:

```
; This is a comment

; Simple commands
Echo "Starting backup..."
Copy WORK: TO BACKUP: ALL

; Variables
Set name "ViperOS"
Echo "Welcome to $name"

; Conditionals
If EXISTS S:user-startup
  Execute S:user-startup
Else
  Echo "No user startup"
EndIf

; Return codes
If WARN
  Echo "Warning occurred"
EndIf

If ERROR
  Echo "Error occurred"
  Quit 10
EndIf

; Labels and skip
Lab start
Echo "Processing..."
If NOT EXISTS T:done
  ; do work
  Skip start
EndIf

; Arguments in scripts ($1, $2, etc)
Echo "First argument: $1"
```

### 21.5 Redirection & Pipes

```
; Output redirection
List >RAM:dirlist.txt
Dir >>RAM:dirlist.txt      ; Append

; Input redirection  
Sort <RAM:unsorted.txt >RAM:sorted.txt

; Pipes (v0.2+)
List | Search ".vpr"
```

### 21.6 Startup Sequence

On boot, the shell executes:

1. `S:startup-sequence` (system startup)
2. `S:shell-startup` (shell initialization)
3. `S:user-startup` (user customization, if exists)

Example `S:startup-sequence`:

```
; ViperOS Startup Sequence
Echo "ViperOS starting..."

; Set up assigns
Assign LIBS: SYS:libs
Assign FONTS: SYS:fonts
Assign C: SYS:c
Assign T: SYS:t

; Set path
Path C: ADD

; Start system services
Run >NIL: SYS:viper\vlog.vpr

Echo "Welcome to ViperOS!"
```

### 21.7 Colors

The shell uses the standard ViperOS color scheme:

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

A simple, readable configuration format designed for ViperOS.

#### 22.1.1 Basic Syntax

```vcfg
# This is a comment

# Simple key-value pairs
hostname = "viperos"
version = 1
enabled = true
timeout = 30.5

# Strings (double quotes)
message = "Hello, World!"
path = "\home\default"

# Numbers
port = 8080
ratio = 3.14159

# Booleans
debug = true
logging = false

# Null
optional = null
```

#### 22.1.2 Sections

```vcfg
# Sections group related settings
[display]
width = 1024
height = 768
fullscreen = true

[network]
dhcp = true
hostname = "viperos"

# Nested sections with dot notation
[network.dns]
primary = "8.8.8.8"
secondary = "8.8.4.4"
```

#### 22.1.3 Arrays

```vcfg
# Inline arrays
colors = ["red", "green", "blue"]
ports = [80, 443, 8080]

# Multi-line arrays
services = [
    "vlog",
    "vsh",
    "vnet"
]
```

#### 22.1.4 Tables (Objects)

```vcfg
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

#### 22.1.5 Full Example: system.vcfg

```vcfg
# ViperOS System Configuration

hostname = "viperos"
timezone = "UTC"

[display]
resolution = "auto"
font = "topaz"

[display.colors]
foreground = 0x00AA44
background = 0x1A1208

[console]
rows = 48
columns = 128
history_size = 1000

[boot]
splash = true
splash_timeout = 500
verbose = false

[services]
autostart = ["vlog"]

[memory]
# Reserved for kernel heap (MB)
kernel_heap = 64

[debug]
serial = true
serial_port = 0x3F8
```

### 22.2 Grammar (Formal)

```
config     = { line }
line       = comment | assignment | section | table_array | NEWLINE
comment    = "#" { ANY } NEWLINE
assignment = key "=" value NEWLINE
section    = "[" key { "." key } "]" NEWLINE
table_array = "[[" key { "." key } "]]" NEWLINE

key        = IDENT | STRING
value      = string | number | boolean | null | array | inline_table

string     = '"' { CHAR } '"'
number     = ["-"] DIGITS ["." DIGITS]
boolean    = "true" | "false"
null       = "null"
array      = "[" [ value { "," value } ] "]"
inline_table = "{" [ assignment { "," assignment } ] "}"
```

---

## 23. File Formats

### 23.1 Executable (.vpr)

Viper Program - compiled IL module.

```
Header:
  magic:      "VPR\0" (4 bytes)
  version:    uint16 (IL version)
  flags:      uint16
  entry:      uint32 (offset to entry point)
  
Sections:
  .code       IL bytecode
  .data       Static data
  .rodata     Read-only data
  .reloc      Relocations
  .imports    Imported symbols
  .exports    Exported symbols
  .debug      Debug info (optional)
```

### 23.2 Library (.vlib)

Viper Library - shared code module.

```
Header:
  magic:      "VLIB" (4 bytes)
  version:    uint16
  flags:      uint16
  
Sections:
  .code       IL bytecode
  .exports    Exported symbols
  .types      Type definitions
  .debug      Debug info (optional)
```

### 23.3 Font (.vfont)

Bitmap font for graphical console.

```
Header:
  magic:      "FONT" (4 bytes)
  width:      uint8 (glyph width in pixels)
  height:     uint8 (glyph height in pixels)
  num_glyphs: uint16
  flags:      uint32
  
Data:
  glyph[num_glyphs][height]  // 1 bit per pixel, row-major
```

### 23.4 Configuration (.vcfg)

Text format as defined in Section 22.

### 23.5 Package (.vpack)

Future format for distributing applications.

```
Archive containing:
  manifest.vcfg     # Package metadata
  bin/              # Executables
  lib/              # Libraries
  cfg/              # Default config
  doc/              # Documentation
```

---

# Part IV: Development

---

## 24. Syscall Reference

```c
enum VSyscall {
    // Memory (0x00xx)
    VSYS_HeapAlloc = 0x0001,
    VSYS_HeapRetain = 0x0002,
    VSYS_HeapRelease = 0x0003,
    VSYS_HeapGetLen = 0x0004,
    VSYS_HeapSetLen = 0x0005,
    VSYS_HeapWrap = 0x0006,
    
    // Tasks (0x001x)
    VSYS_TaskSpawn = 0x0010,
    VSYS_TaskYield = 0x0011,
    VSYS_TaskExit = 0x0012,
    VSYS_TaskCancel = 0x0013,
    VSYS_TaskJoin = 0x0014,
    VSYS_TaskCurrent = 0x0015,
    VSYS_ViperCurrent = 0x0016,
    
    // Channels (0x002x)
    VSYS_ChannelCreate = 0x0020,
    VSYS_ChannelSend = 0x0021,
    VSYS_ChannelRecv = 0x0022,
    VSYS_ChannelClose = 0x0023,
    
    // Capabilities (0x003x)
    VSYS_CapDerive = 0x0030,
    VSYS_CapRevoke = 0x0031,
    VSYS_CapTransfer = 0x0032,
    VSYS_CapQuery = 0x0033,
    
    // I/O (0x004x)
    VSYS_IORead = 0x0040,
    VSYS_IOWrite = 0x0041,
    VSYS_IOControl = 0x0042,
    VSYS_IOClose = 0x0043,
    
    // Polling (0x005x)
    VSYS_PollCreate = 0x0050,
    VSYS_PollAdd = 0x0051,
    VSYS_PollRemove = 0x0052,
    VSYS_PollWait = 0x0053,
    
    // Time (0x006x)
    VSYS_TimeNow = 0x0060,
    VSYS_TimeSleep = 0x0061,
    VSYS_TimerCreate = 0x0063,
    VSYS_TimerCancel = 0x0064,
    
    // Graphics (0x007x)
    VSYS_SurfaceAcquire = 0x0070,
    VSYS_SurfaceRelease = 0x0071,
    VSYS_SurfacePresent = 0x0072,
    VSYS_SurfaceGetBuffer = 0x0073,
    
    // Input (0x008x)
    VSYS_InputPoll = 0x0080,
    VSYS_InputGetHandle = 0x0081,
    
    // Debug (0x00Fx)
    VSYS_DebugPrint = 0x00F0,
    VSYS_DebugBreak = 0x00F1,
    VSYS_DebugPanic = 0x00F2,
    
    // Viper (0x010x)
    VSYS_ViperSpawn = 0x0100,
    VSYS_ViperExit = 0x0101,
    VSYS_ViperWait = 0x0102,
};
```

---

## 25. Testing Infrastructure

### 25.1 QEMU Modes

ViperOS can run in QEMU in two modes:

#### 25.1.1 Graphical Mode (Development)

```bash
./scripts/run-qemu.sh --gui
# or just:
./scripts/run-qemu.sh
```

Opens QEMU with:

- SDL/GTK window showing ViperOS display
- Serial output mirrored to terminal
- Interactive keyboard/mouse input
- Full boot splash and graphics console

#### 25.1.2 Headless Mode (Automated Testing)

```bash
./scripts/run-qemu.sh --headless
```

Opens QEMU with:

- No display window (`-display none`)
- Serial output to stdout
- VNC available for debugging (`-vnc :0`)
- Scriptable via QEMU monitor
- Exit codes for test automation

### 25.2 Test Framework

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

### 25.3 Test Script Example

```bash
#!/bin/bash
# test-boot.sh - Verify kernel boots successfully

TIMEOUT=30
EXPECTED="ViperOS v"

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

### 25.4 QEMU Script: run-qemu.sh

```bash
#!/bin/bash
set -e

# Defaults
MODE="gui"
ARCH="x86_64"
MEMORY="128M"
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
        --arch)
            ARCH="$2"
            shift 2
            ;;
        --memory)
            MEMORY="$2"
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

# Find OVMF firmware
if [[ "$ARCH" == "x86_64" ]]; then
    OVMF="/usr/share/OVMF/OVMF_CODE.fd"
    QEMU="qemu-system-x86_64"
    MACHINE="q35"
else
    OVMF="/usr/share/AAVMF/AAVMF_CODE.fd"
    QEMU="qemu-system-aarch64"
    MACHINE="virt"
fi

# Build display options
if [[ "$MODE" == "headless" ]]; then
    DISPLAY_OPTS="-display none -vnc :0"
else
    DISPLAY_OPTS="-display sdl"
fi

# Run QEMU
exec $QEMU \
    -machine $MACHINE \
    -cpu max \
    -m $MEMORY \
    -drive if=pflash,format=raw,readonly=on,file=$OVMF \
    -drive format=raw,file=build/esp.img \
    -serial $SERIAL \
    $DISPLAY_OPTS \
    $DEBUG \
    -no-reboot
```

### 25.5 Continuous Integration

```yaml
# .github/workflows/test.yml
name: ViperOS Tests

on: [push, pull_request]

jobs:
  build-and-test:
    runs-on: ubuntu-latest
    
    steps:
      - uses: actions/checkout@v3
      
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y qemu-system-x86 ovmf build-essential
          
      - name: Build kernel
        run: |
          mkdir build && cd build
          cmake ..
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

### 25.6 Debug Mode

```bash
# Start QEMU paused, waiting for debugger
./scripts/run-qemu.sh --debug

# In another terminal:
gdb build/kernel/kernel.elf
(gdb) target remote :1234
(gdb) break kernel_entry
(gdb) continue
```

---

## 26. Development Phases

### Phase 1: Graphics Boot (Months 1-3)

**Goal:** Boot to graphical console with ViperOS logo.

Deliverables:

- vboot bootloader
- Kernel boots on QEMU
- Graphics console (8x16 font)
- Boot splash
- kprintf to screen + serial
- Memory management basics

**Milestone:** "Hello from ViperOS" displayed graphically.

### Phase 2: Multitasking (Months 4-6)

**Goal:** Multiple tasks, IPC.

Deliverables:

- Task struct, scheduler
- Context switching
- Channels
- PollWait
- Timer interrupts

**Milestone:** Two tasks ping-pong messages.

### Phase 3: User Space (Months 7-9)

**Goal:** First user-space Viper.

Deliverables:

- Per-Viper address spaces
- Capability tables
- KHeap syscalls
- Syscall entry/exit
- vinit (kernel-loaded)

**Milestone:** Hello world in user space.

### Phase 4: Filesystem & Shell (Months 10-12)

**Goal:** Boot from disk, interactive shell.

Deliverables:

- ViperFS implementation
- vinit, vsh from disk
- Retro-style commands (Dir, List, Copy, etc.)
- Assign and path system
- Configuration loading

**Milestone:** Boot to `SYS:>` prompt.

### Phase 5: Polish & Input (Months 13-15)

**Goal:** Usable interactive system.

Deliverables:

- Keyboard driver
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

**Milestone:** Fetch webpage from ViperOS.

---

## 27. Design Decisions Summary

| Question              | Decision                                |
|-----------------------|-----------------------------------------|
| Heritage              | **Retro-inspired** (not UNIX, not DOS)  |
| Boot display          | Graphics-first (framebuffer console)    |
| Boot splash           | Simple ViperOS logo                     |
| Console colors        | Green on dark brown                     |
| Panic colors          | Yellow on green                         |
| Shell prompt          | `SYS:>` (device:path format)            |
| Device naming         | Logical assigns (SYS:, HOME:, C:, etc.) |
| Physical drives       | D0:, D1:, D2:, ...                      |
| Path separator        | Backslash `\`                           |
| Commands directory    | `C:` (SYS:c)                            |
| Startup scripts       | `S:` (SYS:s)                            |
| Config format         | ViperConfig (.vcfg) - custom            |
| Font (v0)             | Topaz - baked-in 8x16 bitmap            |
| Testing               | QEMU graphical + headless               |
| Boot protocol         | Custom vboot + VBootInfo                |
| Kernel type awareness | Shallow (8 kinds)                       |
| Tasks                 | 1:1 kernel threads                      |
| Blocking              | Only PollWait                           |
| Heap                  | KHeap/LHeap split                       |
| Scheduler             | Round-robin                             |
| Return codes          | 0=OK, 5=WARN, 10=ERROR, 20=FAIL         |

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

| Category | Syscalls                                     |
|----------|----------------------------------------------|
| Memory   | HeapAlloc, HeapRetain, HeapRelease, HeapWrap |
| Tasks    | TaskSpawn, TaskYield, TaskExit, TaskJoin     |
| Channels | ChannelCreate, ChannelSend, ChannelRecv      |
| I/O      | IORead, IOWrite, IOControl, IOClose          |
| Poll     | PollCreate, PollAdd, PollWait                |
| Graphics | SurfaceAcquire, SurfacePresent               |

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

---
