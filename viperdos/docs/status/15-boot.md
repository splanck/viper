# Boot Infrastructure

**Status:** Complete
**SLOC:** ~1,700 (VBoot) + ~400 (kernel boot)

## Overview

ViperDOS implements a complete UEFI boot infrastructure with a custom bootloader (VBoot) and a two-disk architecture for
clean separation of system and user content.

---

## VBoot UEFI Bootloader

### Purpose

VBoot is a custom UEFI bootloader that loads the ViperDOS kernel on AArch64 systems. It's implemented as a freestanding
UEFI application with no external dependencies.

### Location

```
os/vboot/
├── main.c          # Boot logic (966 lines)
├── efi.h           # Minimal UEFI types/protocols (632 lines)
├── vboot.h         # Boot info structure
├── crt0.S          # AArch64 UEFI entry stub (44 lines)
├── vboot.ld        # Linker script for PE conversion
├── fix_pe.py       # PE header fixup script
└── CMakeLists.txt  # Build configuration
```

### Boot Sequence

1. **UEFI Firmware** loads VBoot from `/EFI/BOOT/BOOTAA64.EFI` on ESP
2. **Locate ESP** - Opens EFI System Partition via `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL`
3. **Load Kernel** - Reads `\viperdos\kernel.sys` into 4MB buffer
4. **Parse ELF** - Validates ELF64 header, processes PT_LOAD segments
5. **Allocate Memory** - Uses `AllocatePages` for each segment's physical address
6. **Copy Segments** - Loads code/data to their target addresses
7. **Cache Flush** - D$ flush + I$ invalidation (critical for AArch64)
8. **Gather Boot Info**:
    - UEFI memory map via `GetMemoryMap`
    - GOP framebuffer info via `EFI_GRAPHICS_OUTPUT_PROTOCOL`
    - Kernel physical base and size
9. **Exit Boot Services** - Calls `ExitBootServices` with retry logic
10. **Jump to Kernel** - Disables interrupts, calls kernel entry with `VBootInfo*` in x0

### VBootInfo Structure

```c
struct VBootInfo {
    uint64_t magic;               // "VIPER\0" (0x564950455200ULL)
    uint64_t hhdm_base;           // Higher-half direct map base
    uint64_t kernel_phys_base;    // Kernel load address
    uint64_t kernel_virt_base;    // Kernel virtual address
    uint64_t kernel_size;         // Kernel size in bytes
    uint64_t ttbr0;               // TTBR0 (identity map)
    uint64_t ttbr1;               // TTBR1 (kernel map)
    VBootFramebuffer framebuffer; // GOP framebuffer info
    uint32_t memory_region_count; // Number of memory regions
    VBootMemoryRegion memory_regions[64];
};

struct VBootFramebuffer {
    uint64_t base;      // Physical address
    uint32_t width;     // Pixels
    uint32_t height;    // Pixels
    uint32_t pitch;     // Bytes per scanline
    uint32_t bpp;       // Bits per pixel (32)
    uint32_t format;    // Pixel format (BGR/RGB)
};

struct VBootMemoryRegion {
    uint64_t base;      // Physical base address
    uint64_t size;      // Size in bytes
    uint32_t type;      // Region type
};
```

### Memory Map Conversion

VBoot converts UEFI's detailed memory types into simplified categories:

| UEFI Type                  | VBoot Type |
|----------------------------|------------|
| EfiConventionalMemory      | USABLE_RAM |
| EfiLoaderCode/Data         | USABLE_RAM |
| EfiBootServicesCode/Data   | USABLE_RAM |
| EfiACPIReclaimMemory       | ACPI       |
| EfiACPIMemoryNVS           | ACPI       |
| EfiMemoryMappedIO          | MMIO       |
| EfiMemoryMappedIOPortSpace | MMIO       |
| All others                 | RESERVED   |

### Cache Coherency

VBoot includes critical AArch64 cache management after loading kernel code:

```c
__asm__ volatile(
    "dsb sy\n"        // Data synchronization barrier
    "ic ialluis\n"    // Invalidate all I$ to PoU
    "dsb sy\n"        // Ensure IC invalidation completes
    "isb\n"           // Instruction synchronization barrier
);
```

This ensures the instruction cache sees newly loaded kernel code.

### Build Process

```bash
# Compile for Windows AArch64 (UEFI target)
clang -target aarch64-unknown-windows -ffreestanding \
      -fno-stack-protector -fshort-wchar -O2 -c main.c

# Link with lld-link
lld-link /subsystem:efi_application /entry:_start \
         /nodefaultlib /out:BOOTAA64.EFI main.obj crt0.obj
```

Output: `BOOTAA64.EFI` - PE32+ executable (EFI application)

---

## Two-Disk Architecture

### Design Philosophy

ViperDOS uses separate disks for system and user content to enable:

1. **Clean Separation** - System servers vs. user programs
2. **Graceful Degradation** - Boot to shell without user disk
3. **Independent Development** - Rebuild user programs without touching system
4. **Security Contexts** - Different access mechanisms for system vs. user files

### Disk Layout

#### ESP (esp.img) - 40MB GPT Disk

**UEFI mode only.** Contains bootloader and kernel.

```
esp.img (GPT)
└── Partition 1 (FAT32, ESP type)
    ├── EFI/
    │   └── BOOT/
    │       └── BOOTAA64.EFI    # VBoot bootloader
    ├── viperdos/
    │   └── kernel.sys          # Kernel ELF binary
    └── startup.nsh             # UEFI shell auto-boot script
```

#### System Disk (sys.img) - 2MB ViperFS

Contains core system files. Accessed by kernel VFS during boot.

```
sys.img (ViperFS)
├── vinit.sys       # Init process and shell
├── consoled.sys    # GUI terminal server
└── displayd.sys    # Display server (window manager)
```

#### User Disk (user.img) - 8MB ViperFS

Contains user programs and data. Accessed via kernel VFS.

```
user.img (ViperFS)
├── c/              # User programs
│   ├── hello.prg
│   ├── edit.prg
│   ├── ssh.prg
│   ├── sftp.prg
│   ├── ping.prg
│   ├── netstat.prg
│   ├── sysinfo.prg
│   └── ...
├── certs/
│   └── roots.der   # TLS certificate bundle
├── s/              # System utilities
└── t/              # Temporary files
```

### Disk Index Mapping

Depending on boot mode, disk indices vary:

**UEFI Boot Mode:**
| Index | Image | Purpose |
|-------|-------|---------|
| 0 | esp.img | ESP (bootable) |
| 1 | sys.img | System servers |
| 2 | user.img | User programs |

**Direct Boot Mode (QEMU -kernel):**
| Index | Image | Purpose |
|-------|-------|---------|
| 0 | sys.img | System servers |
| 1 | user.img | User programs |

### Boot Image Creation

The build script creates disk images:

```bash
# Create ESP (UEFI mode)
dd if=/dev/zero of=esp.img bs=1M count=40
sgdisk -n 1:0:0 -t 1:ef00 esp.img
mkfs.vfat -F 32 /dev/loop0p1
mmd -i esp.img ::EFI ::EFI/BOOT ::viperdos
mcopy -i esp.img BOOTAA64.EFI ::EFI/BOOT/
mcopy -i esp.img kernel.sys ::viperdos/

# Create system disk
mkfs.ziafs -s 2097152 sys.img
# Copy servers...

# Create user disk
mkfs.ziafs -s 8388608 user.img
# Copy programs...
```

---

## Kernel Boot Process

### Entry Point (boot.S)

Location: `kernel/arch/aarch64/boot.S`

```asm
_start:
    // Save boot parameter (VBootInfo* or DTB*)
    mov     x20, x0

    // Select EL1 stack
    msr     SPSel, #1

    // Setup stack pointer
    adrp    x0, kernel_stack_top
    add     x0, x0, :lo12:kernel_stack_top
    mov     sp, x0

    // Disable alignment checking (for network structs)
    mrs     x0, SCTLR_EL1
    bic     x0, x0, #(1 << 1)   // Clear A bit
    msr     SCTLR_EL1, x0
    isb

    // Enable FPU/SIMD
    mov     x0, #(0b11 << 20)
    msr     CPACR_EL1, x0
    isb

    // Clear BSS section
    adrp    x0, __bss_start
    adrp    x1, __bss_end
    bl      memzero

    // Call C++ kernel entry
    mov     x0, x20
    bl      kernel_main

    // Should not return
    b       halt_loop
```

### Kernel Main (main.cpp)

Location: `kernel/main.cpp`

Initialization sequence:

1. **Serial Console** - First output for debugging
2. **Boot Info Parsing** - Detect VBoot or DTB boot
3. **Framebuffer Setup**:
    - UEFI path: Use GOP framebuffer from VBootInfo
    - QEMU path: Create ramfb via fw_cfg
4. **Graphics Console** - Text rendering with font
5. **Memory Management**:
    - PMM initialization with memory map
    - VMM page table setup
    - Buddy allocator for page runs
    - Slab allocator for kernel objects
    - Kernel heap for dynamic allocation
6. **Exceptions/Interrupts**:
    - Install exception vectors
    - GIC initialization (v2 or v3)
    - Timer setup (1kHz tick)
7. **Core Services**:
    - Task subsystem initialization
    - Scheduler with priority queues
    - IPC channels
    - Polling infrastructure
    - Capability system
8. **Device Discovery**:
    - VirtIO MMIO device scan
    - Block device detection
    - Network device detection
9. **Filesystem**:
    - Block cache initialization
    - VFS layer setup
    - ViperFS mount (system disk)
    - Assign system configuration
10. **User Space**:
    - Enable MMU for user mode
    - Create first process
    - Load and execute vinit

### Boot Method Detection

```cpp
// kernel/boot/bootinfo.cpp
void init(const void* boot_info) {
    if (viper::vboot::is_valid(boot_info)) {
        // VBoot UEFI path
        parse_vboot(static_cast<const viper::vboot::Info*>(boot_info));
    } else {
        // QEMU direct boot path (x0 = DTB pointer)
        setup_qemu_defaults(boot_info);
    }
}
```

---

## Boot Modes

### UEFI Mode (Default)

```bash
./scripts/build_viperdos.sh
# or
./scripts/build_viperdos.sh --uefi
```

Flow:

```
QEMU → EDK2 Firmware → esp.img → VBoot → kernel.sys
```

QEMU configuration:

```
-drive file=esp.img,if=pflash,format=raw,index=0
-drive file=sys.img,if=virtio,format=raw,index=1
-drive file=user.img,if=virtio,format=raw,index=2
```

### Direct Mode

```bash
./scripts/build_viperdos.sh --direct
```

Flow:

```
QEMU → kernel.sys (loaded directly at 0x40000000)
```

QEMU configuration:

```
-kernel build/kernel.sys
-drive file=sys.img,if=virtio,format=raw,index=0
-drive file=user.img,if=virtio,format=raw,index=1
```

### Debug Mode

```bash
./scripts/build_viperdos.sh --debug
```

Adds GDB server on port 1234:

```
-s -S   # -s = GDB server, -S = pause on start
```

Connect with:

```bash
aarch64-elf-gdb build/kernel.sys
(gdb) target remote :1234
(gdb) continue
```

---

## Build Script

### Location

`scripts/build_viperdos.sh` (691 lines)

### Features

1. **Prerequisite Detection** - Auto-installs missing tools
2. **Clean Builds** - Always rebuilds from scratch
3. **Dual Boot Modes** - UEFI or direct kernel boot
4. **Display Modes** - Graphics or serial-only
5. **Debug Support** - GDB integration
6. **Test Mode** - Run CTest before launch

### Usage

```bash
./scripts/build_viperdos.sh [options]

Options:
  --uefi      UEFI boot via VBoot (default)
  --direct    Direct kernel boot
  --serial    Serial console only
  --debug     Enable GDB on port 1234
  --test      Run tests before launch
  --help      Show usage
```

### Build Steps

1. **Check Prerequisites**:
    - QEMU (qemu-system-aarch64)
    - CMake
    - Clang (LLVM, not Apple)
    - AArch64 cross tools
    - UEFI tools (sgdisk, mtools)

2. **CMake Configuration**:
   ```bash
   cmake -S . -B build \
         -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-clang-toolchain.cmake
   ```

3. **Build All**:
   ```bash
   cmake --build build --parallel
   ```

4. **Create Disk Images**:
    - Generate TLS certificates
    - Create sys.img with mkfs.ziafs
    - Create user.img with mkfs.ziafs
    - Create esp.img (UEFI mode)

5. **Launch QEMU**:
   ```bash
   qemu-system-aarch64 \
       -machine virt \
       -cpu cortex-a72 \
       -smp 4 \
       -m 128M \
       -device ramfb \
       -device virtio-keyboard-device \
       -device virtio-mouse-device \
       [disk configuration] \
       [boot configuration]
   ```

---

## Key Files

| File                         | Purpose                           |
|------------------------------|-----------------------------------|
| `vboot/main.c`               | UEFI bootloader implementation    |
| `vboot/efi.h`                | Minimal UEFI protocol definitions |
| `vboot/vboot.h`              | Boot info structure               |
| `vboot/crt0.S`               | UEFI entry point stub             |
| `kernel/arch/aarch64/boot.S` | Kernel entry point                |
| `kernel/boot/bootinfo.cpp`   | Boot info parsing                 |
| `kernel/include/vboot.hpp`   | VBootInfo mirror for kernel       |
| `kernel/main.cpp`            | Kernel initialization             |
| `scripts/build_viperdos.sh`  | Build orchestration               |

---

## Implementation Notes

### Why Custom Bootloader?

1. **Control** - Full control over boot process
2. **Simplicity** - No external dependencies (GRUB, systemd-boot)
3. **Learning** - Educational value of implementing UEFI
4. **Size** - Minimal footprint (~15KB binary)

### Cache Coherency

AArch64 has separate instruction and data caches. After loading kernel code into memory, VBoot must:

1. Flush data cache to ensure writes are visible
2. Invalidate instruction cache to discard stale cached instructions
3. Use barriers to ensure ordering

Without this, the CPU may execute stale or garbage instructions.

### ExitBootServices Retry

UEFI's `ExitBootServices` can fail if the memory map changed between `GetMemoryMap` and `ExitBootServices` calls. VBoot
handles this with retry logic:

```c
do {
    status = gBS->GetMemoryMap(&map_size, map, &map_key, ...);
    status = gBS->ExitBootServices(image_handle, map_key);
} while (status == EFI_INVALID_PARAMETER);
```

---

## What's Working

- Complete UEFI boot flow
- ELF64 kernel loading with segment mapping
- GOP framebuffer passthrough
- Memory map conversion
- Cache coherency handling
- Dual boot modes (UEFI and direct)
- Two-disk architecture
- Automatic disk image creation

## What's Missing

- Secure Boot support
- UEFI runtime services
- ACPI table parsing
- Multi-boot (kernel selection menu)
- Network boot (PXE/HTTP)

---

## Priority Recommendations: Next 5 Steps

### 1. ACPI Table Parsing

**Impact:** Hardware discovery on real systems

- Parse RSDP from UEFI ConfigurationTable
- Extract CPU count from MADT
- Interrupt routing from MADT/GTDT
- Foundation for power management

### 2. Secure Boot Support

**Impact:** Boot chain integrity

- Sign VBoot with Microsoft or custom key
- Validate kernel signature before loading
- Shim integration for distribution
- Required for many real hardware deployments

### 3. Boot Menu (Multi-Boot)

**Impact:** Kernel version selection

- Simple text-mode menu in VBoot
- List available kernel versions
- Timeout with default selection
- Useful for development and recovery

### 4. UEFI Runtime Services

**Impact:** System integration features

- Access UEFI variables (boot order, etc.)
- Runtime memory mapping preservation
- ResetSystem() for clean reboot
- SetVirtualAddressMap() for kernel

### 5. Network Boot (PXE/HTTP)

**Impact:** Diskless deployment

- UEFI PXE network driver discovery
- HTTP boot using UEFI SimpleNetwork
- TFTP fallback for legacy servers
- Enables centralized OS deployment
