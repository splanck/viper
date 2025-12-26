# ViperOS Phase 1: Graphics Boot

## Detailed Implementation Plan

**Duration:** 12 weeks  
**Goal:** Boot to graphical console displaying "Hello from ViperOS"  
**Architecture:** AArch64 exclusively  
**Development Platform:** QEMU virt machine

---

## Executive Summary

Phase 1 establishes the foundation: a bootloader that loads the kernel, a kernel that initializes hardware and displays
graphics, and the infrastructure to build and test it all. By the end, we have a working kernel that boots on QEMU,
shows a splash screen, prints text to a graphical console, and responds to timer interrupts.

This is the hardest phase because everything is new. There's no existing infrastructure to lean on. Every line of code
must be written, every tool configured, every assumption validated.

---

## Project Structure

```
viperos/
├── CMakeLists.txt              # Top-level build
├── cmake/
│   └── aarch64-toolchain.cmake # Cross-compilation
├── vboot/                       # UEFI bootloader
│   ├── CMakeLists.txt
│   ├── main.c                   # UEFI entry point
│   ├── elf.c                    # ELF loader
│   ├── memory.c                 # Memory map handling
│   ├── graphics.c               # GOP framebuffer setup
│   └── paging.c                 # Initial page tables
├── kernel/                      # Microkernel
│   ├── CMakeLists.txt
│   ├── entry.S                  # Assembly entry point
│   ├── main.c                   # C entry, initialization
│   ├── arch/
│   │   └── aarch64/
│   │       ├── boot.S           # Early boot assembly
│   │       ├── mmu.c            # Page table management
│   │       ├── exceptions.S     # Exception vectors
│   │       ├── exceptions.c     # Exception handlers
│   │       ├── gic.c            # Interrupt controller
│   │       └── timer.c          # Architected timer
│   ├── console/
│   │   ├── gcon.c               # Graphics console
│   │   ├── font.c               # Baked-in font data
│   │   └── serial.c             # PL011 UART
│   ├── mm/
│   │   ├── pmm.c                # Physical memory manager
│   │   └── vmm.c                # Virtual memory manager
│   └── lib/
│       ├── string.c             # memcpy, memset, strlen, etc.
│       └── printf.c             # kprintf implementation
├── scripts/
│   ├── run-qemu.sh              # Launch QEMU
│   ├── make-esp.sh              # Build ESP image
│   └── test.sh                  # Automated tests
└── tests/
    └── boot/
        └── test-boot.sh         # Boot verification
```

---

## Implementation Milestones

### Milestone 1: Build Infrastructure (Week 1)

### Milestone 2: UEFI Bootloader (Weeks 2-3)

### Milestone 3: Kernel Entry & Serial (Week 4)

### Milestone 4: Graphics Console (Weeks 5-6)

### Milestone 5: Memory Management (Weeks 7-8)

### Milestone 6: Exceptions & Interrupts (Weeks 9-10)

### Milestone 7: Timer & Integration (Weeks 11-12)

---

## Milestone 1: Build Infrastructure

**Duration:** Week 1  
**Deliverable:** Can cross-compile C for AArch64, build UEFI applications, create bootable ESP image, run in QEMU

### Tasks

#### 1.1 Development Environment Setup

```bash
# Required packages (Ubuntu/Debian)
sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    gcc-aarch64-linux-gnu \
    qemu-system-aarch64 \
    qemu-efi-aarch64 \
    mtools \
    dosfstools \
    gdb-multiarch
```

**Verification:** All tools installed and accessible from PATH.

#### 1.2 CMake Toolchain File

```cmake
# cmake/aarch64-toolchain.cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_ASM_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_OBJCOPY aarch64-linux-gnu-objcopy)

set(CMAKE_C_FLAGS_INIT "-ffreestanding -nostdlib -mcpu=cortex-a72")
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -fno-stack-protector")
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -mgeneral-regs-only")

set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -static")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

**Verification:** `cmake -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake ..` succeeds.

#### 1.3 Top-Level CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(ViperOS VERSION 0.1.0 LANGUAGES C ASM)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build vboot (UEFI bootloader)
add_subdirectory(vboot)

# Build kernel
add_subdirectory(kernel)

# Custom target to build ESP image
add_custom_target(esp
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/make-esp.sh
    DEPENDS vboot kernel
    COMMENT "Building ESP boot image"
)

# Custom target to run in QEMU
add_custom_target(run
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/run-qemu.sh
    DEPENDS esp
    COMMENT "Running ViperOS in QEMU"
)
```

#### 1.4 QEMU Launch Script

```bash
#!/bin/bash
# scripts/run-qemu.sh
set -e

BUILD_DIR="${BUILD_DIR:-build}"
MEMORY="${MEMORY:-128M}"
MODE="${MODE:-gui}"

# Find AAVMF firmware
AAVMF=""
for path in \
    "/usr/share/AAVMF/AAVMF_CODE.fd" \
    "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd" \
    "/usr/share/edk2/aarch64/QEMU_EFI.fd"; do
    if [[ -f "$path" ]]; then
        AAVMF="$path"
        break
    fi
done

if [[ -z "$AAVMF" ]]; then
    echo "Error: AAVMF firmware not found"
    exit 1
fi

DISPLAY_OPTS="-display sdl"
if [[ "$MODE" == "headless" ]]; then
    DISPLAY_OPTS="-display none -vnc :0"
fi

DEBUG_OPTS=""
if [[ "$1" == "--debug" ]]; then
    DEBUG_OPTS="-s -S"
    echo "Waiting for GDB on localhost:1234..."
fi

exec qemu-system-aarch64 \
    -machine virt \
    -cpu cortex-a72 \
    -m "$MEMORY" \
    -drive if=pflash,format=raw,readonly=on,file="$AAVMF" \
    -drive format=raw,file="$BUILD_DIR/esp.img" \
    -serial stdio \
    $DISPLAY_OPTS \
    $DEBUG_OPTS \
    -no-reboot
```

#### 1.5 ESP Image Creation Script

```bash
#!/bin/bash
# scripts/make-esp.sh
set -e

BUILD_DIR="${BUILD_DIR:-build}"
ESP_SIZE_MB=64
ESP_IMG="$BUILD_DIR/esp.img"

# Create empty image
dd if=/dev/zero of="$ESP_IMG" bs=1M count=$ESP_SIZE_MB 2>/dev/null

# Create FAT32 filesystem
mkfs.vfat -F 32 "$ESP_IMG"

# Create directory structure and copy files
mmd -i "$ESP_IMG" ::EFI
mmd -i "$ESP_IMG" ::EFI/BOOT
mmd -i "$ESP_IMG" ::viperos

# Copy bootloader
mcopy -i "$ESP_IMG" "$BUILD_DIR/vboot/BOOTAA64.EFI" ::EFI/BOOT/

# Copy kernel
mcopy -i "$ESP_IMG" "$BUILD_DIR/kernel/kernel.elf" ::viperos/

echo "ESP image created: $ESP_IMG"
```

#### 1.6 Verification Test

Create a minimal "hello world" that proves the toolchain works:

```c
// test/toolchain-test.c
void _start(void) {
    volatile unsigned int *uart = (unsigned int *)0x09000000;
    const char *msg = "Toolchain works!\n";
    while (*msg) {
        *uart = *msg++;
    }
    for (;;) __asm__("wfi");
}
```

Build and run in QEMU with a minimal linker script. If "Toolchain works!" appears on serial, Milestone 1 is complete.

**Exit Criteria:**

- [ ] Cross-compiler produces AArch64 binaries
- [ ] QEMU launches with AAVMF firmware
- [ ] ESP image can be created and mounted
- [ ] Minimal test program runs and produces serial output

---

## Milestone 2: UEFI Bootloader (vboot)

**Duration:** Weeks 2-3  
**Deliverable:** UEFI application that loads kernel ELF, sets up page tables, obtains framebuffer, and jumps to kernel

### Tasks

#### 2.1 UEFI Application Skeleton

```c
// vboot/main.c
#include <efi.h>
#include <efilib.h>

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    Print(L"vboot: ViperOS Bootloader v0.1\r\n");
    
    // TODO: Load kernel
    // TODO: Get memory map
    // TODO: Get framebuffer
    // TODO: Set up page tables
    // TODO: Exit boot services
    // TODO: Jump to kernel
    
    Print(L"vboot: Halting (not implemented)\r\n");
    for (;;) __asm__("wfi");
    
    return EFI_SUCCESS;
}
```

**Build:** Use gnu-efi or build against EDK2 headers. The bootloader is a PE32+ executable.

```cmake
# vboot/CMakeLists.txt
add_executable(vboot
    main.c
    elf.c
    memory.c
    graphics.c
    paging.c
)

target_include_directories(vboot PRIVATE ${GNU_EFI_INCLUDE})
target_link_libraries(vboot ${GNU_EFI_LIB})

# Convert to UEFI PE format
add_custom_command(TARGET vboot POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary vboot BOOTAA64.EFI
)
```

#### 2.2 Kernel ELF Loading

```c
// vboot/elf.c
#include "elf.h"
#include "vboot.h"

EFI_STATUS load_kernel(EFI_FILE_PROTOCOL *root, const CHAR16 *path, 
                       UINT64 *entry_point, UINT64 *phys_base, UINT64 *size) {
    EFI_FILE_PROTOCOL *file;
    EFI_STATUS status;
    
    // Open kernel file
    status = root->Open(root, &file, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) return status;
    
    // Read ELF header
    Elf64_Ehdr ehdr;
    UINTN ehdr_size = sizeof(ehdr);
    status = file->Read(file, &ehdr_size, &ehdr);
    if (EFI_ERROR(status)) return status;
    
    // Validate ELF magic
    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        return EFI_INVALID_PARAMETER;
    }
    
    // Validate AArch64
    if (ehdr.e_machine != EM_AARCH64) {
        return EFI_UNSUPPORTED;
    }
    
    *entry_point = ehdr.e_entry;
    
    // Load program headers
    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr phdr;
        file->SetPosition(file, ehdr.e_phoff + i * ehdr.e_phentsize);
        UINTN phdr_size = sizeof(phdr);
        file->Read(file, &phdr_size, &phdr);
        
        if (phdr.p_type == PT_LOAD) {
            // Allocate physical memory
            EFI_PHYSICAL_ADDRESS addr = phdr.p_paddr;
            UINTN pages = (phdr.p_memsz + 0xFFF) / 0x1000;
            
            status = gBS->AllocatePages(AllocateAddress, EfiLoaderData, 
                                        pages, &addr);
            if (EFI_ERROR(status)) return status;
            
            // Read segment data
            file->SetPosition(file, phdr.p_offset);
            UINTN seg_size = phdr.p_filesz;
            file->Read(file, &seg_size, (void *)addr);
            
            // Zero BSS
            if (phdr.p_memsz > phdr.p_filesz) {
                gBS->SetMem((void *)(addr + phdr.p_filesz), 
                           phdr.p_memsz - phdr.p_filesz, 0);
            }
            
            // Track extent
            if (*phys_base == 0 || addr < *phys_base) *phys_base = addr;
            if (addr + phdr.p_memsz > *phys_base + *size) {
                *size = (addr + phdr.p_memsz) - *phys_base;
            }
        }
    }
    
    file->Close(file);
    return EFI_SUCCESS;
}
```

#### 2.3 Framebuffer Acquisition

```c
// vboot/graphics.c
#include "vboot.h"

EFI_STATUS get_framebuffer(VBootFramebuffer *fb) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    EFI_STATUS status;
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    
    status = gBS->LocateProtocol(&gop_guid, NULL, (void **)&gop);
    if (EFI_ERROR(status)) {
        Print(L"vboot: GOP not available\r\n");
        return status;
    }
    
    // Find a suitable mode (prefer 1024x768 or higher)
    UINT32 best_mode = gop->Mode->Mode;
    for (UINT32 i = 0; i < gop->Mode->MaxMode; i++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
        UINTN info_size;
        gop->QueryMode(gop, i, &info_size, &info);
        
        if (info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor ||
            info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
            if (info->HorizontalResolution >= 1024 && 
                info->VerticalResolution >= 768) {
                best_mode = i;
            }
        }
    }
    
    // Set mode
    status = gop->SetMode(gop, best_mode);
    if (EFI_ERROR(status)) return status;
    
    // Fill in framebuffer info
    fb->base = gop->Mode->FrameBufferBase;
    fb->width = gop->Mode->Info->HorizontalResolution;
    fb->height = gop->Mode->Info->VerticalResolution;
    fb->pitch = gop->Mode->Info->PixelsPerScanLine * 4;
    fb->bpp = 32;
    fb->pixel_format = (gop->Mode->Info->PixelFormat == 
                        PixelBlueGreenRedReserved8BitPerColor) ? 0 : 1;
    
    Print(L"vboot: Framebuffer %dx%d @ 0x%lx\r\n", 
          fb->width, fb->height, fb->base);
    
    return EFI_SUCCESS;
}
```

#### 2.4 Memory Map Acquisition

```c
// vboot/memory.c
#include "vboot.h"

EFI_STATUS get_memory_map(VBootInfo *info) {
    EFI_STATUS status;
    UINTN map_size = 0;
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN map_key;
    UINTN desc_size;
    UINT32 desc_version;
    
    // Get required size
    status = gBS->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_version);
    if (status != EFI_BUFFER_TOO_SMALL) return status;
    
    // Allocate buffer (add extra space for the allocation itself)
    map_size += desc_size * 4;
    status = gBS->AllocatePool(EfiLoaderData, map_size, (void **)&map);
    if (EFI_ERROR(status)) return status;
    
    // Get actual map
    status = gBS->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_version);
    if (EFI_ERROR(status)) {
        gBS->FreePool(map);
        return status;
    }
    
    // Convert to VBootMemoryRegion format
    info->memory_region_count = 0;
    UINT8 *ptr = (UINT8 *)map;
    UINT8 *end = ptr + map_size;
    
    while (ptr < end && info->memory_region_count < VBOOT_MAX_MEMORY_REGIONS) {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)ptr;
        VBootMemoryRegion *region = &info->memory_regions[info->memory_region_count];
        
        region->base = desc->PhysicalStart;
        region->size = desc->NumberOfPages * 4096;
        
        switch (desc->Type) {
            case EfiConventionalMemory:
            case EfiBootServicesCode:
            case EfiBootServicesData:
                region->type = 1;  // Usable
                break;
            case EfiACPIReclaimMemory:
            case EfiACPIMemoryNVS:
                region->type = 3;  // ACPI
                break;
            case EfiMemoryMappedIO:
            case EfiMemoryMappedIOPortSpace:
                region->type = 4;  // MMIO
                break;
            default:
                region->type = 2;  // Reserved
                break;
        }
        
        info->memory_region_count++;
        ptr += desc_size;
    }
    
    return EFI_SUCCESS;
}
```

#### 2.5 Page Table Setup

```c
// vboot/paging.c
#include "vboot.h"

// Page table constants for 4KB granule
#define PAGE_SIZE       0x1000
#define PAGE_SHIFT      12
#define ENTRIES_PER_TABLE 512

// Page table entry flags
#define PTE_VALID       (1ULL << 0)
#define PTE_TABLE       (1ULL << 1)
#define PTE_BLOCK       (0ULL << 1)
#define PTE_AF          (1ULL << 10)
#define PTE_SH_INNER    (3ULL << 8)
#define PTE_AP_RW       (0ULL << 6)
#define PTE_ATTR_NORMAL (0ULL << 2)  // MAIR index 0
#define PTE_ATTR_DEVICE (1ULL << 2)  // MAIR index 1

typedef UINT64 pte_t;

static pte_t *alloc_table(void) {
    EFI_PHYSICAL_ADDRESS addr;
    gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &addr);
    gBS->SetMem((void *)addr, PAGE_SIZE, 0);
    return (pte_t *)addr;
}

EFI_STATUS setup_page_tables(VBootInfo *info, UINT64 kernel_phys, 
                              UINT64 kernel_size, UINT64 *ttbr0, UINT64 *ttbr1) {
    // Allocate root tables
    pte_t *ttbr0_l0 = alloc_table();  // User/identity
    pte_t *ttbr1_l0 = alloc_table();  // Kernel
    
    // --- TTBR0: Identity map first 1GB for boot ---
    pte_t *ttbr0_l1 = alloc_table();
    ttbr0_l0[0] = (UINT64)ttbr0_l1 | PTE_VALID | PTE_TABLE;
    
    // 1GB block mapping at physical 0
    ttbr0_l1[0] = 0 | PTE_VALID | PTE_BLOCK | PTE_AF | PTE_SH_INNER | 
                  PTE_AP_RW | PTE_ATTR_NORMAL;
    
    // --- TTBR1: Kernel mappings ---
    
    // HHDM at 0xFFFF_0000_0000_0000
    // Map all physical memory we know about
    pte_t *hhdm_l1 = alloc_table();
    ttbr1_l0[0] = (UINT64)hhdm_l1 | PTE_VALID | PTE_TABLE;
    
    // Map first 4GB as HHDM (sufficient for boot)
    for (int i = 0; i < 4; i++) {
        hhdm_l1[i] = ((UINT64)i << 30) | PTE_VALID | PTE_BLOCK | PTE_AF | 
                     PTE_SH_INNER | PTE_AP_RW | PTE_ATTR_NORMAL;
    }
    
    // Kernel at 0xFFFF_FFFF_0000_0000
    pte_t *kernel_l1 = alloc_table();
    pte_t *kernel_l2 = alloc_table();
    ttbr1_l0[511] = (UINT64)kernel_l1 | PTE_VALID | PTE_TABLE;
    kernel_l1[510] = (UINT64)kernel_l2 | PTE_VALID | PTE_TABLE;
    
    // Map kernel pages (2MB blocks for simplicity)
    UINT64 kernel_pages = (kernel_size + 0x1FFFFF) / 0x200000;
    for (UINT64 i = 0; i < kernel_pages && i < 512; i++) {
        kernel_l2[i] = (kernel_phys + i * 0x200000) | PTE_VALID | PTE_BLOCK | 
                       PTE_AF | PTE_SH_INNER | PTE_AP_RW | PTE_ATTR_NORMAL;
    }
    
    info->hhdm_base = 0xFFFF000000000000ULL;
    info->kernel_virt_base = 0xFFFFFFFF00000000ULL;
    
    *ttbr0 = (UINT64)ttbr0_l0;
    *ttbr1 = (UINT64)ttbr1_l0;
    
    return EFI_SUCCESS;
}
```

#### 2.6 Kernel Entry

```c
// vboot/main.c - final jump to kernel

typedef void (*kernel_entry_fn)(VBootInfo *);

void jump_to_kernel(UINT64 entry, VBootInfo *info, UINT64 ttbr0, UINT64 ttbr1) {
    // Set up MAIR
    UINT64 mair = (0xFF << 0) |   // Attr0: Normal, write-back
                  (0x00 << 8);    // Attr1: Device-nGnRnE
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair));
    
    // Set up TCR
    UINT64 tcr = (16ULL << 0) |   // T0SZ = 16 (48-bit VA)
                 (16ULL << 16) |  // T1SZ = 16
                 (1ULL << 8) |    // TTBR0 inner shareable
                 (1ULL << 10) |   // TTBR0 outer shareable
                 (1ULL << 24) |   // TTBR1 inner shareable
                 (1ULL << 26) |   // TTBR1 outer shareable
                 (2ULL << 30);    // 4KB granule
    __asm__ volatile("msr tcr_el1, %0" :: "r"(tcr));
    
    // Set TTBR0/TTBR1
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(ttbr0));
    __asm__ volatile("msr ttbr1_el1, %0" :: "r"(ttbr1));
    
    // Invalidate TLB
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
    
    // Enable MMU
    UINT64 sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1 << 0);   // M - MMU enable
    sctlr |= (1 << 2);   // C - Data cache enable
    sctlr |= (1 << 12);  // I - Instruction cache enable
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
    __asm__ volatile("isb");
    
    // Jump to kernel (using virtual address)
    UINT64 kernel_virt = 0xFFFFFFFF00000000ULL + (entry & 0xFFFFFF);
    kernel_entry_fn kernel = (kernel_entry_fn)kernel_virt;
    kernel(info);
    
    // Should never reach here
    for (;;) __asm__("wfi");
}
```

**Exit Criteria:**

- [ ] vboot builds as valid UEFI application
- [ ] vboot loads kernel ELF from ESP
- [ ] vboot obtains memory map and framebuffer
- [ ] vboot sets up page tables
- [ ] vboot jumps to kernel entry point
- [ ] Kernel receives valid VBootInfo structure

---

## Milestone 3: Kernel Entry & Serial Output

**Duration:** Week 4  
**Deliverable:** Kernel boots, initializes serial, prints "Hello from ViperOS" to UART

### Tasks

#### 3.1 Kernel Entry Point (Assembly)

```asm
// kernel/arch/aarch64/boot.S
.section .text.boot
.global _start

_start:
    // x0 = VBootInfo* (from vboot)
    
    // Set up kernel stack
    adrp    x1, kernel_stack_top
    add     x1, x1, :lo12:kernel_stack_top
    mov     sp, x1
    
    // Clear BSS
    adrp    x1, __bss_start
    add     x1, x1, :lo12:__bss_start
    adrp    x2, __bss_end
    add     x2, x2, :lo12:__bss_end
1:
    cmp     x1, x2
    b.ge    2f
    str     xzr, [x1], #8
    b       1b
2:
    // Call C entry point
    bl      kernel_main
    
    // Halt if we return
3:
    wfi
    b       3b

.section .bss
.align 16
kernel_stack_bottom:
    .space 16384          // 16KB stack
kernel_stack_top:
```

#### 3.2 Kernel Linker Script

```ld
/* kernel/kernel.ld */
OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)

KERNEL_VIRT_BASE = 0xFFFFFFFF00000000;
KERNEL_PHYS_BASE = 0x100000;  /* 1MB */

SECTIONS {
    . = KERNEL_VIRT_BASE;
    
    .text : AT(KERNEL_PHYS_BASE) {
        __text_start = .;
        *(.text.boot)
        *(.text .text.*)
        __text_end = .;
    }
    
    .rodata : {
        __rodata_start = .;
        *(.rodata .rodata.*)
        __rodata_end = .;
    }
    
    .data : {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    }
    
    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }
    
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}
```

#### 3.3 PL011 UART Driver

```c
// kernel/console/serial.c
#include <stdint.h>
#include "serial.h"

// PL011 registers (QEMU virt machine)
#define UART_BASE   0x09000000
#define UART_DR     (*(volatile uint32_t *)(UART_BASE + 0x00))
#define UART_FR     (*(volatile uint32_t *)(UART_BASE + 0x18))
#define UART_FR_TXFF (1 << 5)  // Transmit FIFO full

static uint64_t uart_base_virt;

void serial_init(uint64_t hhdm_base) {
    // UART is at physical 0x09000000, map via HHDM
    uart_base_virt = hhdm_base + UART_BASE;
    // QEMU's PL011 is already initialized by firmware
}

void serial_putc(char c) {
    volatile uint32_t *dr = (volatile uint32_t *)(uart_base_virt + 0x00);
    volatile uint32_t *fr = (volatile uint32_t *)(uart_base_virt + 0x18);
    
    // Wait for transmit FIFO to have space
    while (*fr & UART_FR_TXFF)
        ;
    
    *dr = c;
}

void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}
```

#### 3.4 Minimal kprintf

```c
// kernel/lib/printf.c
#include <stdarg.h>
#include <stdint.h>
#include "printf.h"
#include "serial.h"
#include "gcon.h"

static void print_char(char c) {
    serial_putc(c);
    gcon_putc(c);
}

static void print_string(const char *s) {
    while (*s) {
        if (*s == '\n') print_char('\r');
        print_char(*s++);
    }
}

static void print_hex(uint64_t value, int width) {
    static const char hex[] = "0123456789abcdef";
    char buf[17];
    int i = 16;
    buf[i--] = '\0';
    
    do {
        buf[i--] = hex[value & 0xF];
        value >>= 4;
        width--;
    } while (value || width > 0);
    
    print_string(&buf[i + 1]);
}

static void print_dec(int64_t value) {
    if (value < 0) {
        print_char('-');
        value = -value;
    }
    
    char buf[21];
    int i = 20;
    buf[i--] = '\0';
    
    do {
        buf[i--] = '0' + (value % 10);
        value /= 10;
    } while (value);
    
    print_string(&buf[i + 1]);
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    
    while (*fmt) {
        if (*fmt != '%') {
            if (*fmt == '\n') print_char('\r');
            print_char(*fmt++);
            continue;
        }
        
        fmt++;  // Skip '%'
        
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }
        
        switch (*fmt++) {
            case 's':
                print_string(va_arg(ap, const char *));
                break;
            case 'd':
                print_dec(va_arg(ap, int));
                break;
            case 'x':
                print_hex(va_arg(ap, unsigned int), width);
                break;
            case 'l':
                if (*fmt == 'x') {
                    fmt++;
                    print_hex(va_arg(ap, uint64_t), width);
                }
                break;
            case 'p':
                print_string("0x");
                print_hex(va_arg(ap, uint64_t), 16);
                break;
            case '%':
                print_char('%');
                break;
        }
    }
    
    va_end(ap);
}
```

#### 3.5 Kernel Main

```c
// kernel/main.c
#include <stdint.h>
#include "vboot.h"
#include "serial.h"
#include "printf.h"

void kernel_main(VBootInfo *info) {
    // Validate boot info
    if (info->magic != VBOOT_MAGIC) {
        // Can't even print - just halt
        for (;;) __asm__("wfi");
    }
    
    // Initialize serial first (for debugging)
    serial_init(info->hhdm_base);
    
    kprintf("\n");
    kprintf("===========================================\n");
    kprintf("  ViperOS v0.1.0 - AArch64\n");
    kprintf("===========================================\n");
    kprintf("\n");
    
    kprintf("Boot info at %p\n", info);
    kprintf("HHDM base:   %p\n", info->hhdm_base);
    kprintf("Memory regions: %d\n", info->memory_region_count);
    kprintf("Framebuffer: %dx%d @ %p\n", 
            info->framebuffer.width,
            info->framebuffer.height,
            info->framebuffer.base);
    
    kprintf("\nHello from ViperOS!\n");
    
    // Halt
    kprintf("\nKernel halting.\n");
    for (;;) __asm__("wfi");
}
```

**Exit Criteria:**

- [ ] Kernel boots from vboot
- [ ] Serial output works
- [ ] "Hello from ViperOS!" appears on serial console
- [ ] Boot info is parsed and displayed correctly

---

## Milestone 4: Graphics Console

**Duration:** Weeks 5-6  
**Deliverable:** Graphical text console with boot splash, kprintf outputs to screen

### Tasks

#### 4.1 Baked-In Font Data

```c
// kernel/console/font.c
#include <stdint.h>

// 8x16 bitmap font (VGA-style)
// Each glyph is 16 bytes (one byte per row, 8 pixels per byte)

const uint8_t font_8x16[128][16] = {
    // ASCII 0-31: Control characters (empty)
    [0 ... 31] = {0},
    
    // ASCII 32: Space
    [32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    
    // ASCII 33: !
    [33] = {0x00, 0x00, 0x18, 0x3C, 0x3C, 0x3C, 0x18, 0x18,
            0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00},
    
    // ... (full font data would be ~2KB)
    // For development, generate from existing font or use PSF
    
    // ASCII 65: A
    [65] = {0x00, 0x00, 0x10, 0x38, 0x6C, 0xC6, 0xC6, 0xFE,
            0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00},
    
    // ... continue for all printable ASCII
};

#define FONT_WIDTH  8
#define FONT_HEIGHT 16
```

#### 4.2 Graphics Console Implementation

```c
// kernel/console/gcon.c
#include <stdint.h>
#include "vboot.h"
#include "gcon.h"
#include "font.h"

// ViperOS colors
#define VIPER_GREEN      0xFF00AA44
#define VIPER_DARK_BROWN 0xFF1A1208
#define VIPER_YELLOW     0xFFFFDD00
#define VIPER_WHITE      0xFFEEEEEE
#define VIPER_RED        0xFFCC3333

static struct {
    uint32_t *framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;  // In pixels
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t cols;
    uint32_t rows;
    int initialized;
} gcon;

void gcon_init(const VBootFramebuffer *fb, uint64_t hhdm_base) {
    gcon.framebuffer = (uint32_t *)(hhdm_base + fb->base);
    gcon.width = fb->width;
    gcon.height = fb->height;
    gcon.pitch = fb->pitch / 4;  // Convert bytes to pixels
    gcon.fg_color = VIPER_GREEN;
    gcon.bg_color = VIPER_DARK_BROWN;
    gcon.cursor_x = 0;
    gcon.cursor_y = 0;
    gcon.cols = fb->width / FONT_WIDTH;
    gcon.rows = fb->height / FONT_HEIGHT;
    gcon.initialized = 1;
    
    // Clear screen
    gcon_clear();
}

void gcon_clear(void) {
    if (!gcon.initialized) return;
    
    for (uint32_t y = 0; y < gcon.height; y++) {
        for (uint32_t x = 0; x < gcon.width; x++) {
            gcon.framebuffer[y * gcon.pitch + x] = gcon.bg_color;
        }
    }
    gcon.cursor_x = 0;
    gcon.cursor_y = 0;
}

void gcon_set_colors(uint32_t fg, uint32_t bg) {
    gcon.fg_color = fg;
    gcon.bg_color = bg;
}

static void gcon_draw_char(uint32_t cx, uint32_t cy, char c) {
    if (c < 0 || c > 127) c = '?';
    
    const uint8_t *glyph = font_8x16[(int)c];
    uint32_t px = cx * FONT_WIDTH;
    uint32_t py = cy * FONT_HEIGHT;
    
    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_WIDTH; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? gcon.fg_color : gcon.bg_color;
            gcon.framebuffer[(py + row) * gcon.pitch + (px + col)] = color;
        }
    }
}

static void gcon_scroll(void) {
    // Scroll up one line
    uint32_t line_pixels = FONT_HEIGHT * gcon.pitch;
    uint32_t total_pixels = (gcon.height - FONT_HEIGHT) * gcon.pitch;
    
    // Move lines up
    uint32_t *dst = gcon.framebuffer;
    uint32_t *src = gcon.framebuffer + line_pixels;
    for (uint32_t i = 0; i < total_pixels; i++) {
        dst[i] = src[i];
    }
    
    // Clear bottom line
    uint32_t *bottom = gcon.framebuffer + total_pixels;
    for (uint32_t i = 0; i < line_pixels; i++) {
        bottom[i] = gcon.bg_color;
    }
    
    gcon.cursor_y--;
}

void gcon_putc(char c) {
    if (!gcon.initialized) return;
    
    if (c == '\n') {
        gcon.cursor_x = 0;
        gcon.cursor_y++;
    } else if (c == '\r') {
        gcon.cursor_x = 0;
    } else if (c == '\t') {
        gcon.cursor_x = (gcon.cursor_x + 8) & ~7;
    } else if (c >= ' ') {
        gcon_draw_char(gcon.cursor_x, gcon.cursor_y, c);
        gcon.cursor_x++;
    }
    
    // Wrap at end of line
    if (gcon.cursor_x >= gcon.cols) {
        gcon.cursor_x = 0;
        gcon.cursor_y++;
    }
    
    // Scroll if needed
    while (gcon.cursor_y >= gcon.rows) {
        gcon_scroll();
    }
}

void gcon_puts(const char *s) {
    while (*s) {
        gcon_putc(*s++);
    }
}
```

#### 4.3 Boot Splash

```c
// kernel/console/splash.c
#include "gcon.h"

// Simple ASCII art splash (will be replaced with proper logo)
static const char *splash_art[] = {
    "",
    "",
    "        ╔═══════════════════════════════════════╗",
    "        ║                                       ║",
    "        ║           ░▒▓█ VIPER █▓▒░             ║",
    "        ║               ══════                  ║",
    "        ║                 OS                    ║",
    "        ║                                       ║",
    "        ╚═══════════════════════════════════════╝",
    "",
    NULL
};

void gcon_draw_splash(void) {
    gcon_clear();
    
    // Center vertically (roughly)
    int start_row = 10;
    gcon_set_cursor(0, start_row);
    
    for (int i = 0; splash_art[i] != NULL; i++) {
        // Center horizontally
        int len = 0;
        const char *s = splash_art[i];
        while (*s) { len++; s++; }
        
        int start_col = (gcon.cols - len) / 2;
        if (start_col < 0) start_col = 0;
        
        gcon_set_cursor(start_col, start_row + i);
        gcon_puts(splash_art[i]);
    }
}

// Call from kernel_main after gcon_init
```

#### 4.4 Update kernel_main

```c
// kernel/main.c (updated)
void kernel_main(VBootInfo *info) {
    if (info->magic != VBOOT_MAGIC) {
        for (;;) __asm__("wfi");
    }
    
    // Initialize serial
    serial_init(info->hhdm_base);
    serial_puts("\nvboot -> kernel handoff complete\n");
    
    // Initialize graphics console
    gcon_init(&info->framebuffer, info->hhdm_base);
    
    // Show splash
    gcon_draw_splash();
    
    // Brief delay (busy wait for now)
    for (volatile int i = 0; i < 50000000; i++);
    
    // Clear and show boot messages
    gcon_clear();
    
    kprintf("===========================================\n");
    kprintf("  ViperOS v0.1.0 - AArch64\n");
    kprintf("===========================================\n");
    kprintf("\n");
    kprintf("Framebuffer: %dx%d\n", 
            info->framebuffer.width, info->framebuffer.height);
    kprintf("Memory regions: %d\n", info->memory_region_count);
    kprintf("\n");
    kprintf("Hello from ViperOS!\n");
    
    for (;;) __asm__("wfi");
}
```

**Exit Criteria:**

- [ ] Graphics console displays text
- [ ] Boot splash appears on screen
- [ ] Text scrolls when reaching bottom
- [ ] Colors match ViperOS palette (green on dark brown)

---

## Milestone 5: Memory Management

**Duration:** Weeks 7-8  
**Deliverable:** Physical and virtual memory allocators, kernel heap

### Tasks

#### 5.1 Physical Memory Manager

```c
// kernel/mm/pmm.c
#include <stdint.h>
#include "vboot.h"
#include "pmm.h"

#define PAGE_SIZE 4096

// Simple bitmap allocator
static uint64_t *bitmap;
static uint64_t bitmap_size;  // In bits
static uint64_t total_pages;
static uint64_t free_pages;
static uint64_t hhdm_base;

void pmm_init(const VBootInfo *info) {
    hhdm_base = info->hhdm_base;
    
    // Find highest usable address
    uint64_t highest = 0;
    for (uint32_t i = 0; i < info->memory_region_count; i++) {
        const VBootMemoryRegion *r = &info->memory_regions[i];
        if (r->type == 1) {  // Usable
            uint64_t end = r->base + r->size;
            if (end > highest) highest = end;
        }
    }
    
    total_pages = highest / PAGE_SIZE;
    bitmap_size = (total_pages + 63) / 64;  // 64 bits per uint64_t
    
    // Find space for bitmap in usable memory
    uint64_t bitmap_bytes = bitmap_size * 8;
    for (uint32_t i = 0; i < info->memory_region_count; i++) {
        const VBootMemoryRegion *r = &info->memory_regions[i];
        if (r->type == 1 && r->size >= bitmap_bytes) {
            bitmap = (uint64_t *)(hhdm_base + r->base);
            break;
        }
    }
    
    // Mark all pages as used initially
    for (uint64_t i = 0; i < bitmap_size; i++) {
        bitmap[i] = ~0ULL;
    }
    
    // Mark usable regions as free
    free_pages = 0;
    for (uint32_t i = 0; i < info->memory_region_count; i++) {
        const VBootMemoryRegion *r = &info->memory_regions[i];
        if (r->type == 1) {
            uint64_t start_page = (r->base + PAGE_SIZE - 1) / PAGE_SIZE;
            uint64_t end_page = (r->base + r->size) / PAGE_SIZE;
            for (uint64_t p = start_page; p < end_page; p++) {
                pmm_free_page(p * PAGE_SIZE);
            }
        }
    }
    
    // Mark bitmap pages as used
    uint64_t bitmap_phys = (uint64_t)bitmap - hhdm_base;
    uint64_t bitmap_pages = (bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < bitmap_pages; i++) {
        pmm_alloc_page();  // Just consume pages from start
    }
    
    kprintf("PMM: %ld pages free (%ld MB)\n", 
            free_pages, (free_pages * PAGE_SIZE) / (1024 * 1024));
}

uint64_t pmm_alloc_page(void) {
    for (uint64_t i = 0; i < bitmap_size; i++) {
        if (bitmap[i] != ~0ULL) {
            for (int bit = 0; bit < 64; bit++) {
                if (!(bitmap[i] & (1ULL << bit))) {
                    bitmap[i] |= (1ULL << bit);
                    free_pages--;
                    return (i * 64 + bit) * PAGE_SIZE;
                }
            }
        }
    }
    return 0;  // Out of memory
}

void pmm_free_page(uint64_t phys_addr) {
    uint64_t page = phys_addr / PAGE_SIZE;
    uint64_t idx = page / 64;
    uint64_t bit = page % 64;
    
    if (!(bitmap[idx] & (1ULL << bit))) {
        return;  // Already free (double-free)
    }
    
    bitmap[idx] &= ~(1ULL << bit);
    free_pages++;
}

void *pmm_to_virt(uint64_t phys) {
    return (void *)(hhdm_base + phys);
}

uint64_t pmm_to_phys(void *virt) {
    return (uint64_t)virt - hhdm_base;
}
```

#### 5.2 Virtual Memory Manager

```c
// kernel/mm/vmm.c
#include <stdint.h>
#include "vmm.h"
#include "pmm.h"

#define PAGE_SIZE 4096
#define PTE_VALID   (1ULL << 0)
#define PTE_TABLE   (1ULL << 1)
#define PTE_BLOCK   (0ULL << 1)
#define PTE_AF      (1ULL << 10)
#define PTE_SH      (3ULL << 8)
#define PTE_AP_RW   (0ULL << 6)
#define PTE_UXN     (1ULL << 54)
#define PTE_PXN     (1ULL << 53)
#define PTE_ATTR(n) ((n) << 2)

typedef uint64_t pte_t;

static pte_t *kernel_pml4;  // TTBR1 root

static pte_t *get_or_create_table(pte_t *table, int index) {
    if (!(table[index] & PTE_VALID)) {
        uint64_t phys = pmm_alloc_page();
        void *virt = pmm_to_virt(phys);
        for (int i = 0; i < 512; i++) {
            ((pte_t *)virt)[i] = 0;
        }
        table[index] = phys | PTE_VALID | PTE_TABLE;
    }
    
    uint64_t next_phys = table[index] & 0x0000FFFFFFFFF000ULL;
    return (pte_t *)pmm_to_virt(next_phys);
}

void vmm_init(uint64_t ttbr1) {
    kernel_pml4 = (pte_t *)pmm_to_virt(ttbr1);
    kprintf("VMM: Initialized with TTBR1=%p\n", ttbr1);
}

int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    // Extract indices
    int l0_idx = (virt >> 39) & 0x1FF;
    int l1_idx = (virt >> 30) & 0x1FF;
    int l2_idx = (virt >> 21) & 0x1FF;
    int l3_idx = (virt >> 12) & 0x1FF;
    
    pte_t *l1 = get_or_create_table(kernel_pml4, l0_idx);
    pte_t *l2 = get_or_create_table(l1, l1_idx);
    pte_t *l3 = get_or_create_table(l2, l2_idx);
    
    l3[l3_idx] = phys | PTE_VALID | PTE_AF | PTE_SH | flags;
    
    // Invalidate TLB for this address
    __asm__ volatile("tlbi vaae1is, %0" :: "r"(virt >> 12));
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
    
    return 0;
}

void vmm_unmap_page(uint64_t virt) {
    int l0_idx = (virt >> 39) & 0x1FF;
    int l1_idx = (virt >> 30) & 0x1FF;
    int l2_idx = (virt >> 21) & 0x1FF;
    int l3_idx = (virt >> 12) & 0x1FF;
    
    if (!(kernel_pml4[l0_idx] & PTE_VALID)) return;
    pte_t *l1 = (pte_t *)pmm_to_virt(kernel_pml4[l0_idx] & ~0xFFF);
    
    if (!(l1[l1_idx] & PTE_VALID)) return;
    pte_t *l2 = (pte_t *)pmm_to_virt(l1[l1_idx] & ~0xFFF);
    
    if (!(l2[l2_idx] & PTE_VALID)) return;
    pte_t *l3 = (pte_t *)pmm_to_virt(l2[l2_idx] & ~0xFFF);
    
    l3[l3_idx] = 0;
    
    __asm__ volatile("tlbi vaae1is, %0" :: "r"(virt >> 12));
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
}
```

#### 5.3 Kernel Heap

```c
// kernel/mm/kheap.c
#include <stdint.h>
#include "kheap.h"
#include "pmm.h"
#include "vmm.h"

#define KHEAP_START 0xFFFF800000000000ULL
#define KHEAP_SIZE  (256 * 1024 * 1024)  // 256MB

// Simple bump allocator for Phase 1
static uint64_t heap_next;
static uint64_t heap_end;

void kheap_init(void) {
    heap_next = KHEAP_START;
    heap_end = KHEAP_START;
    kprintf("Kernel heap initialized at %p\n", KHEAP_START);
}

static void expand_heap(uint64_t needed) {
    while (heap_end < heap_next + needed) {
        uint64_t phys = pmm_alloc_page();
        if (phys == 0) {
            kprintf("PANIC: Out of memory\n");
            for (;;) __asm__("wfi");
        }
        vmm_map_page(heap_end, phys, PTE_AP_RW | PTE_ATTR(0));
        heap_end += 4096;
    }
}

void *kmalloc(uint64_t size) {
    // Align to 16 bytes
    size = (size + 15) & ~15ULL;
    
    expand_heap(size);
    
    void *ptr = (void *)heap_next;
    heap_next += size;
    
    return ptr;
}

void kfree(void *ptr) {
    // Bump allocator doesn't free
    // Real allocator in Phase 2
    (void)ptr;
}
```

**Exit Criteria:**

- [ ] Physical page allocator works
- [ ] Virtual memory mapping works
- [ ] Kernel heap allocates memory
- [ ] No memory corruption during boot

---

## Milestone 6: Exceptions & Interrupts

**Duration:** Weeks 9-10  
**Deliverable:** Exception vectors installed, GIC initialized, can handle interrupts

### Tasks

#### 6.1 Exception Vectors

```asm
// kernel/arch/aarch64/exceptions.S
.section .text

.macro SAVE_REGS
    sub     sp, sp, #272
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
    stp     x30, xzr, [sp, #240]
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x0, x1, [sp, #256]
.endm

.macro RESTORE_REGS
    ldp     x0, x1, [sp, #256]
    msr     elr_el1, x0
    msr     spsr_el1, x1
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
    add     sp, sp, #272
.endm

.align 11
.global exception_vectors
exception_vectors:
    // Current EL with SP0
    .align 7
    b       sync_handler
    .align 7
    b       irq_handler
    .align 7
    b       fiq_handler
    .align 7
    b       serror_handler

    // Current EL with SPx
    .align 7
    b       sync_handler
    .align 7
    b       irq_handler
    .align 7
    b       fiq_handler
    .align 7
    b       serror_handler

    // Lower EL using AArch64
    .align 7
    b       sync_handler
    .align 7
    b       irq_handler
    .align 7
    b       fiq_handler
    .align 7
    b       serror_handler

    // Lower EL using AArch32
    .align 7
    b       sync_handler
    .align 7
    b       irq_handler
    .align 7
    b       fiq_handler
    .align 7
    b       serror_handler

sync_handler:
    SAVE_REGS
    mov     x0, sp
    bl      handle_sync_exception
    RESTORE_REGS
    eret

irq_handler:
    SAVE_REGS
    mov     x0, sp
    bl      handle_irq
    RESTORE_REGS
    eret

fiq_handler:
    SAVE_REGS
    mov     x0, sp
    bl      handle_fiq
    RESTORE_REGS
    eret

serror_handler:
    SAVE_REGS
    mov     x0, sp
    bl      handle_serror
    RESTORE_REGS
    eret
```

#### 6.2 Exception Handlers

```c
// kernel/arch/aarch64/exceptions.c
#include <stdint.h>
#include "exceptions.h"
#include "printf.h"
#include "gic.h"

typedef struct {
    uint64_t x[31];
    uint64_t sp;
    uint64_t elr;
    uint64_t spsr;
} ExceptionFrame;

void handle_sync_exception(ExceptionFrame *frame) {
    uint64_t esr, far;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));
    
    uint32_t ec = (esr >> 26) & 0x3F;
    
    // Check for SVC (syscall) - will be used in Phase 3
    if (ec == 0x15) {
        // Syscall handling (not implemented yet)
        kprintf("Syscall from %p\n", frame->elr);
        return;
    }
    
    // Panic on unexpected exception
    kprintf("\n");
    kprintf("========== KERNEL PANIC ==========\n");
    kprintf("Synchronous Exception\n");
    kprintf("ESR_EL1: 0x%08x (EC=%d)\n", esr, ec);
    kprintf("FAR_EL1: 0x%016lx\n", far);
    kprintf("ELR_EL1: 0x%016lx\n", frame->elr);
    kprintf("SPSR_EL1: 0x%016lx\n", frame->spsr);
    kprintf("\nRegisters:\n");
    for (int i = 0; i < 31; i += 2) {
        kprintf("X%02d: 0x%016lx  X%02d: 0x%016lx\n",
                i, frame->x[i], i+1, frame->x[i+1]);
    }
    kprintf("==================================\n");
    
    for (;;) __asm__("wfi");
}

void handle_irq(ExceptionFrame *frame) {
    (void)frame;
    gic_handle_irq();
}

void handle_fiq(ExceptionFrame *frame) {
    (void)frame;
    kprintf("FIQ received (unexpected)\n");
}

void handle_serror(ExceptionFrame *frame) {
    kprintf("SError received at %p\n", frame->elr);
    for (;;) __asm__("wfi");
}

void exceptions_init(void) {
    extern char exception_vectors[];
    __asm__ volatile("msr vbar_el1, %0" :: "r"(exception_vectors));
    __asm__ volatile("isb");
    kprintf("Exception vectors installed at %p\n", exception_vectors);
}
```

#### 6.3 GIC Driver

```c
// kernel/arch/aarch64/gic.c
#include <stdint.h>
#include "gic.h"
#include "printf.h"

// GICv2 registers (QEMU virt)
#define GICD_BASE   0x08000000
#define GICC_BASE   0x08010000

// Distributor registers
#define GICD_CTLR       (*(volatile uint32_t *)(gicd_base + 0x000))
#define GICD_ISENABLER(n) (*(volatile uint32_t *)(gicd_base + 0x100 + 4*(n)))
#define GICD_ICENABLER(n) (*(volatile uint32_t *)(gicd_base + 0x180 + 4*(n)))
#define GICD_IPRIORITYR(n) (*(volatile uint32_t *)(gicd_base + 0x400 + 4*(n)))
#define GICD_ITARGETSR(n) (*(volatile uint32_t *)(gicd_base + 0x800 + 4*(n)))

// CPU interface registers
#define GICC_CTLR   (*(volatile uint32_t *)(gicc_base + 0x000))
#define GICC_PMR    (*(volatile uint32_t *)(gicc_base + 0x004))
#define GICC_IAR    (*(volatile uint32_t *)(gicc_base + 0x00C))
#define GICC_EOIR   (*(volatile uint32_t *)(gicc_base + 0x010))

static uint64_t gicd_base;
static uint64_t gicc_base;

typedef void (*irq_handler_t)(void);
static irq_handler_t irq_handlers[1024];

void gic_init(uint64_t hhdm_base) {
    gicd_base = hhdm_base + GICD_BASE;
    gicc_base = hhdm_base + GICC_BASE;
    
    // Disable distributor
    GICD_CTLR = 0;
    
    // Configure all SPIs to lowest priority, target CPU0
    for (int i = 32; i < 1024; i += 4) {
        GICD_IPRIORITYR(i/4) = 0xA0A0A0A0;
        GICD_ITARGETSR(i/4) = 0x01010101;
    }
    
    // Disable all SPIs
    for (int i = 1; i < 32; i++) {
        GICD_ICENABLER(i) = 0xFFFFFFFF;
    }
    
    // Enable distributor
    GICD_CTLR = 1;
    
    // Configure CPU interface
    GICC_PMR = 0xFF;  // Allow all priorities
    GICC_CTLR = 1;    // Enable CPU interface
    
    kprintf("GIC initialized (GICD=%p, GICC=%p)\n", gicd_base, gicc_base);
}

void gic_enable_irq(uint32_t irq) {
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    GICD_ISENABLER(reg) = (1 << bit);
}

void gic_disable_irq(uint32_t irq) {
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    GICD_ICENABLER(reg) = (1 << bit);
}

void gic_set_handler(uint32_t irq, irq_handler_t handler) {
    irq_handlers[irq] = handler;
}

void gic_handle_irq(void) {
    uint32_t iar = GICC_IAR;
    uint32_t irq = iar & 0x3FF;
    
    if (irq == 1023) {
        // Spurious interrupt
        return;
    }
    
    if (irq_handlers[irq]) {
        irq_handlers[irq]();
    } else {
        kprintf("Unhandled IRQ %d\n", irq);
    }
    
    // End of interrupt
    GICC_EOIR = iar;
}

void enable_interrupts(void) {
    __asm__ volatile("msr daifclr, #2");  // Clear I bit
}

void disable_interrupts(void) {
    __asm__ volatile("msr daifset, #2");  // Set I bit
}
```

**Exit Criteria:**

- [ ] Exception vectors installed
- [ ] GIC initialized and accepting interrupts
- [ ] Kernel panic displays useful information
- [ ] Can enable/disable interrupts

---

## Milestone 7: Timer & Integration

**Duration:** Weeks 11-12  
**Deliverable:** Timer interrupts firing, complete boot sequence, stable system

### Tasks

#### 7.1 ARM Architected Timer

```c
// kernel/arch/aarch64/timer.c
#include <stdint.h>
#include "timer.h"
#include "gic.h"
#include "printf.h"

#define TIMER_IRQ 30  // Physical timer IRQ (PPI)

static uint64_t timer_freq;
static uint64_t ticks;

static void timer_irq_handler(void) {
    ticks++;
    
    // Acknowledge and reschedule
    uint64_t cval;
    __asm__ volatile("mrs %0, cntp_cval_el0" : "=r"(cval));
    cval += timer_freq / 1000;  // 1ms
    __asm__ volatile("msr cntp_cval_el0, %0" :: "r"(cval));
    
    // Print tick every second (for debugging)
    if (ticks % 1000 == 0) {
        kprintf("Timer: %ld seconds\n", ticks / 1000);
    }
}

void timer_init(void) {
    // Get timer frequency
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(timer_freq));
    kprintf("Timer frequency: %ld Hz\n", timer_freq);
    
    ticks = 0;
    
    // Set up first deadline
    uint64_t cnt;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(cnt));
    __asm__ volatile("msr cntp_cval_el0, %0" :: "r"(cnt + timer_freq / 1000));
    
    // Enable timer
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(1ULL));
    
    // Set up interrupt handler and enable
    gic_set_handler(TIMER_IRQ, timer_irq_handler);
    gic_enable_irq(TIMER_IRQ);
    
    kprintf("Timer initialized (1000 Hz)\n");
}

uint64_t timer_get_ticks(void) {
    return ticks;
}

uint64_t timer_get_ns(void) {
    uint64_t cnt;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(cnt));
    return (cnt * 1000000000ULL) / timer_freq;
}
```

#### 7.2 Complete Boot Sequence

```c
// kernel/main.c (final version for Phase 1)
#include <stdint.h>
#include "vboot.h"
#include "serial.h"
#include "gcon.h"
#include "printf.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "exceptions.h"
#include "gic.h"
#include "timer.h"

void kernel_main(VBootInfo *info) {
    // === Stage 1: Validate boot info ===
    if (info->magic != VBOOT_MAGIC) {
        for (;;) __asm__("wfi");
    }
    
    // === Stage 2: Early console (serial) ===
    serial_init(info->hhdm_base);
    serial_puts("\n[BOOT] ViperOS kernel starting...\n");
    
    // === Stage 3: Graphics console ===
    gcon_init(&info->framebuffer, info->hhdm_base);
    gcon_draw_splash();
    
    // Brief splash display
    for (volatile int i = 0; i < 30000000; i++);
    gcon_clear();
    
    kprintf("===========================================\n");
    kprintf("  ViperOS v0.1.0 - AArch64\n");
    kprintf("===========================================\n\n");
    
    // === Stage 4: Memory management ===
    kprintf("[BOOT] Initializing memory management...\n");
    pmm_init(info);
    vmm_init(/* get TTBR1 from info or current register */);
    kheap_init();
    
    // === Stage 5: Exceptions ===
    kprintf("[BOOT] Installing exception handlers...\n");
    exceptions_init();
    
    // === Stage 6: Interrupt controller ===
    kprintf("[BOOT] Initializing GIC...\n");
    gic_init(info->hhdm_base);
    
    // === Stage 7: Timer ===
    kprintf("[BOOT] Initializing timer...\n");
    timer_init();
    
    // === Stage 8: Enable interrupts ===
    kprintf("[BOOT] Enabling interrupts...\n");
    enable_interrupts();
    
    // === Boot complete ===
    kprintf("\n");
    kprintf("==========================================\n");
    kprintf("  Boot complete! Hello from ViperOS!\n");
    kprintf("==========================================\n");
    kprintf("\n");
    kprintf("System info:\n");
    kprintf("  Framebuffer: %dx%d\n", 
            info->framebuffer.width, info->framebuffer.height);
    kprintf("  Memory regions: %d\n", info->memory_region_count);
    kprintf("\n");
    kprintf("Kernel idle loop running.\n");
    kprintf("Timer ticks will appear every second.\n");
    kprintf("\n");
    
    // Idle loop
    for (;;) {
        __asm__("wfi");  // Wait for interrupt
    }
}
```

#### 7.3 Automated Boot Test

```bash
#!/bin/bash
# tests/boot/test-boot.sh
set -e

TIMEOUT=60
BUILD_DIR="${BUILD_DIR:-build}"
LOG_FILE="$BUILD_DIR/boot-test.log"

echo "Running boot test..."

# Start QEMU in headless mode, capture output
timeout $TIMEOUT \
    qemu-system-aarch64 \
    -machine virt \
    -cpu cortex-a72 \
    -m 128M \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.fd \
    -drive format=raw,file="$BUILD_DIR/esp.img" \
    -serial file:"$LOG_FILE" \
    -display none \
    -no-reboot \
    &

QEMU_PID=$!

# Wait for expected output
for i in $(seq 1 $TIMEOUT); do
    if grep -q "Hello from ViperOS" "$LOG_FILE" 2>/dev/null; then
        echo "PASS: Kernel booted successfully"
        kill $QEMU_PID 2>/dev/null || true
        exit 0
    fi
    if grep -q "KERNEL PANIC" "$LOG_FILE" 2>/dev/null; then
        echo "FAIL: Kernel panic"
        cat "$LOG_FILE"
        kill $QEMU_PID 2>/dev/null || true
        exit 1
    fi
    sleep 1
done

echo "FAIL: Timeout waiting for boot"
kill $QEMU_PID 2>/dev/null || true
cat "$LOG_FILE"
exit 1
```

**Exit Criteria:**

- [ ] Timer interrupts fire at 1000 Hz
- [ ] System remains stable in idle loop
- [ ] Automated boot test passes
- [ ] "Hello from ViperOS" displayed graphically

---

## Weekly Schedule

| Week | Focus                | Key Deliverables                      |
|------|----------------------|---------------------------------------|
| 1    | Build Infrastructure | Toolchain, QEMU scripts, ESP creation |
| 2    | vboot: ELF loading   | Load kernel from ESP                  |
| 3    | vboot: Page tables   | MMU setup, framebuffer, kernel jump   |
| 4    | Kernel entry         | Serial output, kprintf                |
| 5    | Graphics console     | Text rendering, colors                |
| 6    | Boot splash          | Logo display, polish                  |
| 7    | Physical memory      | Page allocator                        |
| 8    | Virtual memory       | Page table management, heap           |
| 9    | Exceptions           | Vector table, handlers                |
| 10   | GIC                  | Interrupt controller                  |
| 11   | Timer                | Periodic interrupts                   |
| 12   | Integration          | Testing, stability, documentation     |

---

## Risk Mitigation

| Risk                | Mitigation                                           |
|---------------------|------------------------------------------------------|
| UEFI complexity     | Start with minimal vboot, add features incrementally |
| Page table bugs     | Test extensively in QEMU with GDB                    |
| GIC configuration   | Follow QEMU virt machine documentation exactly       |
| Timer issues        | Use QEMU's `-d int` to debug interrupt delivery      |
| Font rendering bugs | Start with a small subset of characters              |

---

## Definition of Done (Phase 1)

- [ ] `make` produces bootable ESP image
- [ ] QEMU boots to graphical console
- [ ] Splash screen displays for ~500ms
- [ ] "Hello from ViperOS" appears on screen
- [ ] Timer ticks logged every second
- [ ] System stable for 5+ minutes
- [ ] `make test` passes automated boot test
- [ ] Code compiles with -Wall -Wextra -Werror
- [ ] All source files have appropriate headers
- [ ] README documents build and run process

---

## Next Steps (Phase 2 Preview)

Phase 2 (Multitasking) will build on Phase 1:

1. **VTask structure** — Using the spec's definition with time_slice
2. **Context switching** — Save/restore registers between tasks
3. **Scheduler** — Round-robin ready queue
4. **Channels** — Non-blocking IPC with cap passing
5. **PollWait** — The only blocking syscall

Phase 1's timer infrastructure will drive preemptive scheduling.
Phase 1's exception handlers will be extended for syscall dispatch.
Phase 1's memory management will allocate task stacks and kernel objects.

---

*"The first boot is the hardest. Everything after builds on that foundation."*
