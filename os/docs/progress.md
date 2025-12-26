# ViperOS Phase 1: Implementation Progress

**Goal:** Boot to graphical console displaying "Hello from ViperOS"
**Architecture:** AArch64 exclusively
**Platform:** QEMU virt machine
**Languages:** C++ (kernel), C (UEFI bootloader)

---

## Overview

| Milestone | Description             | Status      |
|-----------|-------------------------|-------------|
| 1         | Build Infrastructure    | ✅ Complete  |
| 2         | UEFI Bootloader (vboot) | ⏸️ Deferred |
| 3         | Kernel Entry & Serial   | ✅ Complete  |
| 4         | Graphics Console        | ✅ Complete  |
| 5         | Memory Management       | ✅ Complete  |
| 6         | Exceptions & Interrupts | ✅ Complete  |
| 7         | Timer & Integration     | ✅ Complete  |

**Status Legend:** ⬜ Not Started | 🔄 In Progress | ✅ Complete | ❌ Blocked

---

## Milestone 1: Build Infrastructure

### 1.1 Development Environment Setup

- [ ] Install build-essential
- [ ] Install cmake (3.16+)
- [ ] Install ninja-build
- [ ] Install gcc-aarch64-linux-gnu (C cross-compiler for vboot)
- [ ] Install g++-aarch64-linux-gnu (C++ cross-compiler for kernel)
- [ ] Install qemu-system-aarch64
- [ ] Install qemu-efi-aarch64 (AAVMF firmware)
- [ ] Install mtools (for FAT filesystem manipulation)
- [ ] Install dosfstools
- [ ] Install gdb-multiarch
- [ ] Verify all tools accessible from PATH

### 1.2 Project Directory Structure

- [ ] Create top-level `CMakeLists.txt`
- [ ] Create `cmake/` directory
- [ ] Create `vboot/` directory with `CMakeLists.txt`
- [ ] Create `kernel/` directory with `CMakeLists.txt`
- [ ] Create `kernel/arch/aarch64/` directory
- [ ] Create `kernel/console/` directory
- [ ] Create `kernel/mm/` directory
- [ ] Create `kernel/lib/` directory
- [ ] Create `kernel/include/` directory
- [ ] Create `scripts/` directory
- [ ] Create `tests/boot/` directory

### 1.3 CMake Toolchain Configuration

- [ ] Create `cmake/aarch64-toolchain.cmake`
- [ ] Set CMAKE_SYSTEM_NAME to Generic
- [ ] Set CMAKE_SYSTEM_PROCESSOR to aarch64
- [ ] Configure C compiler (aarch64-linux-gnu-gcc)
- [ ] Configure C++ compiler (aarch64-linux-gnu-g++)
- [ ] Configure ASM compiler
- [ ] Configure objcopy
- [ ] Set common flags (-ffreestanding -nostdlib -mcpu=cortex-a72)
- [ ] Set warning flags (-Wall -Wextra -Werror)
- [ ] Set -fno-stack-protector
- [ ] Set -mgeneral-regs-only
- [ ] Set C++ specific flags (-fno-exceptions -fno-rtti -fno-threadsafe-statics)
- [ ] Set C++ standard flag (-std=c++20 or -std=c++23)
- [ ] Verify toolchain file works with cmake

### 1.4 Top-Level Build Configuration

- [ ] Create CMakeLists.txt with project definition
- [ ] Enable languages: C, CXX, ASM
- [ ] Set C standard to C17 (for vboot)
- [ ] Set C++ standard to C++20 or C++23 (for kernel)
- [ ] Disable C++ standard library (freestanding)
- [ ] Add vboot subdirectory
- [ ] Add kernel subdirectory
- [ ] Create custom target for ESP image (`esp`)
- [ ] Create custom target for QEMU run (`run`)

### 1.5 QEMU Launch Script

- [ ] Create `scripts/run-qemu.sh`
- [ ] Make script executable
- [ ] Implement AAVMF firmware detection (multiple paths)
- [ ] Configure QEMU virt machine
- [ ] Configure cortex-a72 CPU
- [ ] Configure memory (128M default)
- [ ] Configure pflash for UEFI firmware
- [ ] Configure ESP image drive
- [ ] Configure serial to stdio
- [ ] Implement display mode selection (gui/headless)
- [ ] Implement GDB debug mode (--debug flag)
- [ ] Add -no-reboot flag

### 1.6 ESP Image Creation Script

- [ ] Create `scripts/make-esp.sh`
- [ ] Make script executable
- [ ] Create empty 64MB image with dd
- [ ] Create FAT32 filesystem with mkfs.vfat
- [ ] Create EFI/BOOT directory structure
- [ ] Create viperos directory
- [ ] Copy BOOTAA64.EFI to EFI/BOOT/
- [ ] Copy kernel.elf to viperos/

### 1.7 Toolchain Verification Test

- [ ] Create minimal C++ test program (writes to UART)
- [ ] Create minimal linker script for test
- [ ] Build test with cross-compiler
- [ ] Verify no C++ runtime dependencies
- [ ] Run test in QEMU
- [ ] Verify "Toolchain works!" appears on serial

### 1.8 Milestone 1 Exit Criteria

- [ ] Cross-compiler produces AArch64 binaries
- [ ] C++ compiles without standard library
- [ ] QEMU launches with AAVMF firmware
- [ ] ESP image can be created and mounted
- [ ] Minimal test program runs and produces serial output

---

## Milestone 2: UEFI Bootloader (vboot)

> **Note:** vboot remains in C as UEFI development typically uses C with gnu-efi or EDK2.

### 2.1 UEFI Application Skeleton

- [ ] Create `vboot/main.c` with efi_main entry
- [ ] Set up gnu-efi or EDK2 headers
- [ ] Initialize UEFI library
- [ ] Print bootloader banner
- [ ] Create vboot CMakeLists.txt
- [ ] Configure PE32+ output format
- [ ] Verify vboot builds and UEFI prints message

### 2.2 VBoot Header Definitions

- [ ] Create `vboot/vboot.h` header
- [ ] Define VBOOT_MAGIC constant
- [ ] Define VBootFramebuffer structure
- [ ] Define VBootMemoryRegion structure
- [ ] Define VBOOT_MAX_MEMORY_REGIONS
- [ ] Define VBootInfo structure

### 2.3 Kernel ELF Loading

- [ ] Create `vboot/elf.c`
- [ ] Create `vboot/elf.h` with ELF64 structures
- [ ] Implement load_kernel function
- [ ] Open kernel file from filesystem
- [ ] Read and validate ELF header magic
- [ ] Validate ELF is AArch64
- [ ] Parse program headers
- [ ] Allocate physical memory for PT_LOAD segments
- [ ] Load segment data from file
- [ ] Zero BSS regions (memsz > filesz)
- [ ] Track kernel physical extent
- [ ] Return entry point address
- [ ] Test: kernel ELF loads without errors

### 2.4 Memory Map Acquisition

- [ ] Create `vboot/memory.c`
- [ ] Implement get_memory_map function
- [ ] Call GetMemoryMap with NULL to get size
- [ ] Allocate buffer for memory map
- [ ] Call GetMemoryMap to get actual map
- [ ] Convert EFI memory types to VBoot types
- [ ] Identify usable memory (EfiConventionalMemory, etc.)
- [ ] Identify reserved memory
- [ ] Identify ACPI memory
- [ ] Identify MMIO regions
- [ ] Store map key for ExitBootServices
- [ ] Test: memory map displays correct regions

### 2.5 Framebuffer Acquisition

- [ ] Create `vboot/graphics.c`
- [ ] Implement get_framebuffer function
- [ ] Locate GOP (Graphics Output Protocol)
- [ ] Query available video modes
- [ ] Find suitable mode (1024x768 or higher)
- [ ] Verify pixel format (BGR or RGB 8-bit)
- [ ] Set video mode
- [ ] Extract framebuffer base address
- [ ] Extract width, height, pitch
- [ ] Store pixel format info
- [ ] Test: framebuffer info displays correctly

### 2.6 Page Table Setup

- [ ] Create `vboot/paging.c`
- [ ] Define page table constants (4KB granule)
- [ ] Define PTE flags (VALID, TABLE, BLOCK, AF, SH, AP, ATTR)
- [ ] Implement alloc_table helper (allocate zeroed page)
- [ ] Implement setup_page_tables function
- [ ] Allocate TTBR0 root table (user/identity)
- [ ] Allocate TTBR1 root table (kernel)
- [ ] Create identity map for first 1GB in TTBR0
- [ ] Create HHDM at 0xFFFF_0000_0000_0000 (first 4GB)
- [ ] Map kernel at 0xFFFF_FFFF_0000_0000
- [ ] Use 2MB blocks for efficiency
- [ ] Store HHDM base in VBootInfo
- [ ] Store kernel virtual base in VBootInfo
- [ ] Return TTBR0 and TTBR1 values

### 2.7 Kernel Jump

- [ ] Implement jump_to_kernel function
- [ ] Set MAIR_EL1 (memory attributes)
- [ ] Set TCR_EL1 (translation control)
- [ ] Set TTBR0_EL1
- [ ] Set TTBR1_EL1
- [ ] Invalidate TLB (tlbi vmalle1is)
- [ ] Issue DSB and ISB barriers
- [ ] Enable MMU in SCTLR_EL1
- [ ] Enable data cache
- [ ] Enable instruction cache
- [ ] Issue ISB barrier
- [ ] Compute kernel virtual address from entry
- [ ] Cast to function pointer and call with VBootInfo

### 2.8 Exit Boot Services

- [ ] Call ExitBootServices with memory map key
- [ ] Handle EFI_INVALID_PARAMETER (re-get map if needed)
- [ ] Retry ExitBootServices with new key
- [ ] Verify clean exit from UEFI

### 2.9 Milestone 2 Exit Criteria

- [ ] vboot builds as valid UEFI application
- [ ] vboot loads kernel ELF from ESP
- [ ] vboot obtains memory map and framebuffer
- [ ] vboot sets up page tables
- [ ] vboot jumps to kernel entry point
- [ ] Kernel receives valid VBootInfo structure

---

## Milestone 3: Kernel Entry & Serial Output

> **Note:** Kernel code is C++ with `extern "C"` linkage for assembly interfaces.

### 3.1 Kernel Linker Script

- [x] Create `kernel/kernel.ld`
- [x] Set OUTPUT_FORMAT to elf64-littleaarch64
- [x] Set OUTPUT_ARCH to aarch64
- [x] Set ENTRY to _start
- [x] Define KERNEL_VIRT_BASE (0x40000000 - using physical for now)
- [x] Define KERNEL_PHYS_BASE (0x40000000)
- [x] Create .text section (with .text.boot first)
- [x] Create .rodata section
- [x] Create .data section
- [x] Create .bss section with __bss_start/__bss_end
- [x] Add /DISCARD/ for .comment and .note

### 3.2 Kernel Entry Point (Assembly)

- [x] Create `kernel/arch/aarch64/boot.S`
- [x] Define _start in .text.boot section
- [x] Preserve x0 (VBootInfo pointer)
- [x] Load kernel stack top address
- [x] Set stack pointer (sp)
- [x] Clear BSS (load __bss_start, __bss_end)
- [x] Loop to zero BSS
- [x] Call kernel_main with x0 (extern "C" function)
- [x] Create infinite wfi loop after return
- [x] Define kernel_stack (16KB) in .bss

### 3.3 VBoot Info Header (Kernel Side)

- [ ] Create `kernel/include/vboot.hpp` (deferred - using QEMU -kernel)
- [ ] Use `#pragma once`
- [ ] Include `<cstdint>` (or define fixed-width types)
- [ ] Define VBOOT_MAGIC constant as constexpr
- [ ] Define VBootFramebuffer struct
- [ ] Define VBootMemoryRegion struct
- [ ] Define VBootInfo struct
- [ ] Wrap declarations in `extern "C"` block for ABI compatibility

### 3.4 Types Header

- [x] Create `kernel/include/types.hpp`
- [x] Define u8, u16, u32, u64 type aliases
- [x] Define i8, i16, i32, i64 type aliases
- [x] Define usize, isize type aliases
- [x] Define uintptr type alias

### 3.5 PL011 UART Driver

- [x] Create `kernel/console/serial.cpp`
- [x] Create `kernel/console/serial.hpp`
- [x] Define UART base address constant (0x09000000)
- [x] Define UART register offsets as constexpr
- [x] Create Serial class or namespace
- [ ] Store HHDM-adjusted UART address (deferred - no HHDM yet)
- [x] Implement init() function
- [x] Implement putc(char c) function
- [x] Wait for TX FIFO space
- [x] Write character to data register
- [x] Implement puts(const char* s) function
- [x] Handle \n -> \r\n conversion
- [x] Implement put_hex() and put_dec() helpers

### 3.6 String Library

- [ ] Create `kernel/lib/string.cpp`
- [ ] Create `kernel/lib/string.hpp`
- [ ] Implement memcpy (extern "C")
- [ ] Implement memset (extern "C")
- [ ] Implement memmove (extern "C")
- [ ] Implement memcmp (extern "C")
- [ ] Implement strlen
- [ ] Implement strcmp
- [ ] Implement strncmp
- [ ] Implement strcpy
- [ ] Implement strncpy

### 3.7 Printf Implementation

- [ ] Create `kernel/lib/printf.cpp`
- [ ] Create `kernel/lib/printf.hpp`
- [ ] Implement print_char helper (to serial and gcon)
- [ ] Implement print_string helper
- [ ] Implement print_hex (with width support)
- [ ] Implement print_dec (signed integers)
- [ ] Implement print_unsigned (unsigned integers)
- [ ] Implement kprintf with variadic template or va_list
- [ ] Support %s (string)
- [ ] Support %d (signed decimal)
- [ ] Support %u (unsigned decimal)
- [ ] Support %x (hex, with width)
- [ ] Support %lx (64-bit hex)
- [ ] Support %p (pointer)
- [ ] Support %% (literal percent)
- [ ] Handle \n -> \r\n conversion

### 3.8 Kernel Main

- [x] Create `kernel/main.cpp`
- [x] Include necessary headers
- [x] Define kernel_main as `extern "C"` void kernel_main(void*)
- [ ] Validate VBootInfo magic (deferred - no vboot yet)
- [ ] Halt if invalid (deferred)
- [x] Call serial init
- [x] Print boot banner
- [x] Print VBootInfo address
- [ ] Print HHDM base (deferred)
- [ ] Print memory region count (deferred)
- [ ] Print framebuffer dimensions (deferred)
- [x] Print "Hello from ViperOS!"
- [x] Enter infinite wfi loop

### 3.9 Kernel CMakeLists.txt

- [x] Create `kernel/CMakeLists.txt`
- [x] Add assembly sources (boot.S)
- [x] Add C++ sources (.cpp files)
- [x] Set include directories
- [x] Set C++ standard to C++20
- [x] Add -fno-exceptions -fno-rtti flags
- [x] Link with kernel.ld linker script
- [x] Output kernel.elf

### 3.10 Milestone 3 Exit Criteria

- [x] Kernel boots (using QEMU -kernel)
- [x] Serial output works
- [x] "Hello from ViperOS!" appears on serial console
- [ ] Boot info is parsed and displayed correctly (deferred - no vboot)

---

## Milestone 4: Graphics Console

### 4.1 Font Data

- [ ] Create `kernel/console/font.cpp`
- [ ] Create `kernel/console/font.hpp`
- [ ] Define FONT_WIDTH (8) as constexpr
- [ ] Define FONT_HEIGHT (16) as constexpr
- [ ] Create font_8x16 array (128 characters × 16 bytes)
- [ ] Add glyphs for space and control chars (0-31)
- [ ] Add glyphs for punctuation (32-64)
- [ ] Add glyphs for uppercase letters (65-90)
- [ ] Add glyphs for more punctuation (91-96)
- [ ] Add glyphs for lowercase letters (97-122)
- [ ] Add glyphs for remaining ASCII (123-127)

### 4.2 Graphics Console Core

- [ ] Create `kernel/console/gcon.cpp`
- [ ] Create `kernel/console/gcon.hpp`
- [ ] Define ViperOS color palette as constexpr
    - [ ] VIPER_GREEN (0xFF00AA44)
    - [ ] VIPER_DARK_BROWN (0xFF1A1208)
    - [ ] VIPER_YELLOW (0xFFFFDD00)
    - [ ] VIPER_WHITE (0xFFEEEEEE)
    - [ ] VIPER_RED (0xFFCC3333)
- [ ] Create GraphicsConsole class (or namespace with state)
    - [ ] Framebuffer pointer
    - [ ] Width, height, pitch
    - [ ] Foreground/background colors
    - [ ] Cursor X/Y position
    - [ ] Columns/rows count
    - [ ] Initialized flag
- [ ] Implement init(framebuffer, hhdm_base)
- [ ] Compute HHDM-adjusted framebuffer address
- [ ] Compute columns and rows from resolution
- [ ] Set default colors
- [ ] Clear screen

### 4.3 Character Drawing

- [ ] Implement draw_char(cx, cy, char) private method
- [ ] Look up glyph in font table
- [ ] Calculate pixel coordinates from character position
- [ ] Draw 8×16 pixels based on glyph bitmap
- [ ] Use foreground color for set bits
- [ ] Use background color for clear bits

### 4.4 Text Output

- [ ] Implement putc(char c)
- [ ] Handle newline (\n) - move to next line
- [ ] Handle carriage return (\r)
- [ ] Handle tab (\t) - align to 8 columns
- [ ] Handle printable characters
- [ ] Update cursor position
- [ ] Handle line wrap
- [ ] Trigger scroll when needed
- [ ] Implement puts(const char* s)

### 4.5 Scrolling

- [ ] Implement scroll() private method
- [ ] Calculate line height in pixels
- [ ] Move all lines up by one text line
- [ ] Clear bottom line with background color
- [ ] Adjust cursor Y position

### 4.6 Screen Utilities

- [ ] Implement clear()
- [ ] Fill entire framebuffer with background color
- [ ] Reset cursor to (0, 0)
- [ ] Implement set_colors(fg, bg)
- [ ] Implement set_cursor(x, y)
- [ ] Implement get_cursor() -> (x, y)

### 4.7 Boot Splash

- [ ] Create `kernel/console/splash.cpp`
- [ ] Create `kernel/console/splash.hpp`
- [ ] Design ASCII art ViperOS logo as constexpr array
- [ ] Implement draw_splash()
- [ ] Clear screen
- [ ] Calculate vertical centering
- [ ] Calculate horizontal centering for each line
- [ ] Draw splash art with appropriate colors

### 4.8 Update Printf

- [ ] Modify print_char in printf.cpp
- [ ] Output to both serial and gcon
- [ ] Only output to gcon if initialized

### 4.9 Update kernel_main

- [ ] Add gcon init call after serial init
- [ ] Call draw_splash()
- [ ] Add brief delay for splash visibility
- [ ] Call clear()
- [ ] Continue with boot messages

### 4.10 Milestone 4 Exit Criteria

- [ ] Graphics console displays text
- [ ] Boot splash appears on screen
- [ ] Text scrolls when reaching bottom
- [ ] Colors match ViperOS palette (green on dark brown)

---

## Milestone 5: Memory Management

### 5.1 Physical Memory Manager (PMM)

- [ ] Create `kernel/mm/pmm.cpp`
- [ ] Create `kernel/mm/pmm.hpp`
- [ ] Define PAGE_SIZE as constexpr (4096)
- [ ] Create PhysicalMemoryManager class (or namespace)
- [ ] Store bitmap pointer and size
- [ ] Store total/free page counts
- [ ] Store HHDM base

### 5.2 PMM Initialization

- [ ] Implement init(VBootInfo*)
- [ ] Scan memory regions to find highest address
- [ ] Calculate total pages
- [ ] Calculate bitmap size
- [ ] Find space for bitmap in usable memory
- [ ] Initialize bitmap (all pages used)
- [ ] Mark usable regions as free
- [ ] Mark bitmap pages as used
- [ ] Print free memory statistics

### 5.3 PMM Allocation

- [ ] Implement alloc_page() -> u64
- [ ] Search bitmap for free page
- [ ] Mark page as used
- [ ] Decrement free count
- [ ] Return physical address
- [ ] Handle out-of-memory (return 0)
- [ ] Implement alloc_pages(count) -> u64
- [ ] Find contiguous free pages
- [ ] Mark all as used
- [ ] Return base address

### 5.4 PMM Deallocation

- [ ] Implement free_page(phys_addr)
- [ ] Calculate page index
- [ ] Clear bit in bitmap
- [ ] Increment free count
- [ ] Handle double-free detection
- [ ] Implement free_pages(phys_addr, count)

### 5.5 PMM Utilities

- [ ] Implement to_virt(phys) -> void*
- [ ] Add HHDM base to physical address
- [ ] Implement to_phys(virt) -> u64
- [ ] Subtract HHDM base from virtual address
- [ ] Implement get_free_count() -> u64
- [ ] Implement get_total_count() -> u64

### 5.6 Virtual Memory Manager (VMM)

- [ ] Create `kernel/mm/vmm.cpp`
- [ ] Create `kernel/mm/vmm.hpp`
- [ ] Define PTE flags as constexpr
    - [ ] PTE_VALID
    - [ ] PTE_TABLE
    - [ ] PTE_BLOCK
    - [ ] PTE_AF (access flag)
    - [ ] PTE_SH (shareability)
    - [ ] PTE_AP_RW (read/write)
    - [ ] PTE_UXN (user execute never)
    - [ ] PTE_PXN (privileged execute never)
    - [ ] PTE_ATTR macro
- [ ] Create VirtualMemoryManager class (or namespace)
- [ ] Store kernel page table root (TTBR1)

### 5.7 VMM Page Table Walking

- [ ] Implement get_or_create_table helper
- [ ] Check if entry is valid
- [ ] If not, allocate new page table
- [ ] Zero new table
- [ ] Install table entry
- [ ] Return next level table pointer

### 5.8 VMM Mapping

- [ ] Implement init(ttbr1)
- [ ] Store kernel page table root
- [ ] Implement map_page(virt, phys, flags) -> int
- [ ] Extract L0, L1, L2, L3 indices from virtual address
- [ ] Walk/create page tables
- [ ] Install final PTE with physical address and flags
- [ ] Invalidate TLB for address
- [ ] Issue DSB and ISB barriers

### 5.9 VMM Unmapping

- [ ] Implement unmap_page(virt)
- [ ] Walk page tables
- [ ] Clear PTE if found
- [ ] Invalidate TLB
- [ ] Issue barriers
- [ ] Note: don't free intermediate tables yet

### 5.10 Kernel Heap

- [ ] Create `kernel/mm/kheap.cpp`
- [ ] Create `kernel/mm/kheap.hpp`
- [ ] Define KHEAP_START as constexpr (0xFFFF800000000000)
- [ ] Define KHEAP_SIZE as constexpr (256MB)
- [ ] Store heap_next and heap_end pointers

### 5.11 Heap Implementation

- [ ] Implement init()
- [ ] Initialize heap_next to KHEAP_START
- [ ] Initialize heap_end to KHEAP_START
- [ ] Implement expand_heap(needed) private helper
- [ ] Allocate physical pages as needed
- [ ] Map pages into heap region
- [ ] Update heap_end
- [ ] Implement kmalloc(size) -> void*
- [ ] Align size to 16 bytes
- [ ] Expand heap if needed
- [ ] Return pointer and advance heap_next
- [ ] Implement kfree(ptr)
- [ ] No-op for bump allocator (Phase 1)

### 5.12 C++ new/delete Operators (Optional)

- [ ] Implement global operator new(size_t) -> void*
- [ ] Call kmalloc
- [ ] Implement global operator delete(void*)
- [ ] Call kfree
- [ ] Implement placement new if needed

### 5.13 Update kernel_main

- [ ] Add pmm init call
- [ ] Add vmm init call (need to capture TTBR1 from vboot)
- [ ] Add kheap init call
- [ ] Test allocations work

### 5.14 Milestone 5 Exit Criteria

- [ ] Physical page allocator works
- [ ] Virtual memory mapping works
- [ ] Kernel heap allocates memory
- [ ] No memory corruption during boot

---

## Milestone 6: Exceptions & Interrupts

### 6.1 Exception Vectors (Assembly)

- [ ] Create `kernel/arch/aarch64/exceptions.S`
- [ ] Create SAVE_REGS macro
    - [ ] Allocate stack frame (272 bytes)
    - [ ] Save x0-x30
    - [ ] Save ELR_EL1
    - [ ] Save SPSR_EL1
- [ ] Create RESTORE_REGS macro
    - [ ] Restore ELR_EL1 and SPSR_EL1
    - [ ] Restore x0-x30
    - [ ] Deallocate stack frame

### 6.2 Vector Table

- [ ] Define exception_vectors (2KB aligned)
- [ ] Current EL with SP0 handlers (4 entries)
    - [ ] Synchronous
    - [ ] IRQ
    - [ ] FIQ
    - [ ] SError
- [ ] Current EL with SPx handlers (4 entries)
- [ ] Lower EL AArch64 handlers (4 entries)
- [ ] Lower EL AArch32 handlers (4 entries)
- [ ] Each entry 128-byte aligned

### 6.3 Exception Handler Stubs

- [ ] Implement sync_handler
    - [ ] SAVE_REGS
    - [ ] Call handle_sync_exception (extern "C")
    - [ ] RESTORE_REGS
    - [ ] ERET
- [ ] Implement irq_handler
- [ ] Implement fiq_handler
- [ ] Implement serror_handler

### 6.4 Exception Handlers (C++)

- [ ] Create `kernel/arch/aarch64/exceptions.cpp`
- [ ] Create `kernel/arch/aarch64/exceptions.hpp`
- [ ] Define ExceptionFrame struct

### 6.5 Synchronous Exception Handler

- [ ] Implement handle_sync_exception(frame) as extern "C"
- [ ] Read ESR_EL1 (exception syndrome)
- [ ] Read FAR_EL1 (fault address)
- [ ] Extract exception class (EC) from ESR
- [ ] Check for SVC (EC=0x15) - placeholder for syscalls
- [ ] Print panic information for other exceptions
    - [ ] ESR value and EC
    - [ ] FAR value
    - [ ] ELR value
    - [ ] SPSR value
    - [ ] All registers
- [ ] Halt on panic

### 6.6 Other Exception Handlers

- [ ] Implement handle_irq(frame) as extern "C"
    - [ ] Call gic_handle_irq()
- [ ] Implement handle_fiq(frame) as extern "C"
    - [ ] Print warning (unexpected)
- [ ] Implement handle_serror(frame) as extern "C"
    - [ ] Print error and halt

### 6.7 Exception Initialization

- [ ] Implement init()
- [ ] Load exception_vectors address
- [ ] Write to VBAR_EL1
- [ ] Issue ISB barrier
- [ ] Print confirmation

### 6.8 GIC Driver

- [ ] Create `kernel/arch/aarch64/gic.cpp`
- [ ] Create `kernel/arch/aarch64/gic.hpp`
- [ ] Define GICD_BASE as constexpr (0x08000000)
- [ ] Define GICC_BASE as constexpr (0x08010000)
- [ ] Create GIC class or namespace

### 6.9 GIC Register Definitions

- [ ] Define register offset constants
- [ ] Create register accessor helpers or macros
- [ ] GICD_CTLR, GICD_ISENABLER, GICD_ICENABLER
- [ ] GICD_IPRIORITYR, GICD_ITARGETSR
- [ ] GICC_CTLR, GICC_PMR, GICC_IAR, GICC_EOIR

### 6.10 GIC Initialization

- [ ] Store HHDM-adjusted GIC addresses
- [ ] Implement init(hhdm_base)
- [ ] Disable distributor
- [ ] Set all SPI priorities to lowest
- [ ] Set all SPI targets to CPU0
- [ ] Disable all SPIs initially
- [ ] Enable distributor
- [ ] Set CPU interface priority mask (allow all)
- [ ] Enable CPU interface
- [ ] Print confirmation

### 6.11 GIC IRQ Management

- [ ] Create IRQ handler table (array of function pointers)
- [ ] Implement enable_irq(irq)
- [ ] Implement disable_irq(irq)
- [ ] Implement set_handler(irq, handler)
- [ ] Implement handle_irq()
    - [ ] Read IAR to get IRQ number
    - [ ] Check for spurious (1023)
    - [ ] Call registered handler
    - [ ] Write EOIR to acknowledge

### 6.12 Interrupt Control

- [ ] Implement enable_interrupts()
    - [ ] Clear DAIF I bit
- [ ] Implement disable_interrupts()
    - [ ] Set DAIF I bit

### 6.13 Update kernel_main

- [ ] Add exceptions init call
- [ ] Add gic init call

### 6.14 Milestone 6 Exit Criteria

- [ ] Exception vectors installed
- [ ] GIC initialized and accepting interrupts
- [ ] Kernel panic displays useful information
- [ ] Can enable/disable interrupts

---

## Milestone 7: Timer & Integration

### 7.1 ARM Architected Timer Driver

- [ ] Create `kernel/arch/aarch64/timer.cpp`
- [ ] Create `kernel/arch/aarch64/timer.hpp`
- [ ] Define TIMER_IRQ as constexpr (30 for physical timer PPI)
- [ ] Create Timer class or namespace
- [ ] Store timer frequency
- [ ] Store tick count

### 7.2 Timer IRQ Handler

- [ ] Implement timer_irq_handler()
- [ ] Increment tick counter
- [ ] Read current compare value
- [ ] Add interval for next tick (freq/1000 for 1ms)
- [ ] Write new compare value
- [ ] Optional: print debug every 1000 ticks

### 7.3 Timer Initialization

- [ ] Implement init()
- [ ] Read CNTFRQ_EL0 (timer frequency)
- [ ] Print frequency
- [ ] Initialize tick counter
- [ ] Read current counter value
- [ ] Set initial compare value (1ms ahead)
- [ ] Enable timer (CNTP_CTL_EL0)
- [ ] Register IRQ handler with GIC
- [ ] Enable timer IRQ in GIC
- [ ] Print confirmation

### 7.4 Timer Utilities

- [ ] Implement get_ticks() -> u64
- [ ] Implement get_ns() -> u64
    - [ ] Read CNTPCT_EL0
    - [ ] Convert to nanoseconds using frequency
- [ ] Implement delay_ms(ms) - optional

### 7.5 Complete Boot Sequence

- [ ] Update kernel_main with all subsystems
- [ ] Stage 1: Validate boot info
- [ ] Stage 2: Early console (serial init)
- [ ] Stage 3: Graphics console (gcon init, splash)
- [ ] Stage 4: Memory management (pmm init, vmm init, kheap init)
- [ ] Stage 5: Exceptions (exceptions init)
- [ ] Stage 6: GIC (gic init)
- [ ] Stage 7: Timer (timer init)
- [ ] Stage 8: Enable interrupts
- [ ] Print boot complete message
- [ ] Print system info
- [ ] Enter idle loop (wfi)

### 7.6 Automated Boot Test

- [ ] Create `tests/boot/test-boot.sh`
- [ ] Make script executable
- [ ] Set timeout (60 seconds)
- [ ] Start QEMU in headless mode
- [ ] Capture serial output to file
- [ ] Wait for "Hello from ViperOS"
- [ ] Check for "KERNEL PANIC"
- [ ] Report PASS/FAIL
- [ ] Kill QEMU on completion
- [ ] Add test target to CMake

### 7.7 Integration Testing

- [ ] Run full boot sequence
- [ ] Verify splash displays for ~500ms
- [ ] Verify text console appears
- [ ] Verify timer ticks print every second
- [ ] Verify system stable for 5+ minutes
- [ ] Test with different memory sizes
- [ ] Test GDB attach works

### 7.8 Code Quality

- [ ] All files compile with -Wall -Wextra -Werror
- [ ] No compiler warnings
- [ ] All source files have appropriate headers
- [ ] Code follows consistent style
- [ ] Comments explain non-obvious logic
- [ ] No use of C++ exceptions or RTTI

### 7.9 Documentation

- [ ] Update README with build instructions
- [ ] Document required packages
- [ ] Document QEMU run command
- [ ] Document GDB debugging steps
- [ ] Document project structure

### 7.10 Milestone 7 Exit Criteria

- [ ] Timer interrupts fire at 1000 Hz
- [ ] System remains stable in idle loop
- [ ] Automated boot test passes
- [ ] "Hello from ViperOS" displayed graphically

---

## Phase 1 Definition of Done

### Build & Test

- [ ] `make` produces bootable ESP image
- [ ] `make run` launches QEMU with graphics
- [ ] `make test` passes automated boot test

### Functionality

- [ ] QEMU boots to graphical console
- [ ] Splash screen displays for ~500ms
- [ ] "Hello from ViperOS!" appears on screen
- [ ] Timer ticks logged every second
- [ ] System stable for 5+ minutes

### Code Quality

- [ ] Compiles with -Wall -Wextra -Werror
- [ ] C++ kernel uses -fno-exceptions -fno-rtti
- [ ] All source files have appropriate headers
- [ ] README documents build and run process

---

## File Checklist

### CMake Files

- [ ] `CMakeLists.txt`
- [ ] `cmake/aarch64-toolchain.cmake`
- [ ] `vboot/CMakeLists.txt`
- [ ] `kernel/CMakeLists.txt`

### Scripts

- [ ] `scripts/run-qemu.sh`
- [ ] `scripts/make-esp.sh`
- [ ] `tests/boot/test-boot.sh`

### Bootloader (vboot) - C

- [ ] `vboot/main.c`
- [ ] `vboot/elf.c`
- [ ] `vboot/elf.h`
- [ ] `vboot/memory.c`
- [ ] `vboot/graphics.c`
- [ ] `vboot/paging.c`
- [ ] `vboot/vboot.h`

### Kernel Core - C++

- [ ] `kernel/main.cpp`
- [ ] `kernel/kernel.ld`

### Kernel Headers - C++

- [ ] `kernel/include/types.hpp`
- [ ] `kernel/include/vboot.hpp`

### Kernel Architecture (aarch64)

- [ ] `kernel/arch/aarch64/boot.S`
- [ ] `kernel/arch/aarch64/exceptions.S`
- [ ] `kernel/arch/aarch64/exceptions.cpp`
- [ ] `kernel/arch/aarch64/exceptions.hpp`
- [ ] `kernel/arch/aarch64/gic.cpp`
- [ ] `kernel/arch/aarch64/gic.hpp`
- [ ] `kernel/arch/aarch64/timer.cpp`
- [ ] `kernel/arch/aarch64/timer.hpp`

### Kernel Console - C++

- [ ] `kernel/console/serial.cpp`
- [ ] `kernel/console/serial.hpp`
- [ ] `kernel/console/gcon.cpp`
- [ ] `kernel/console/gcon.hpp`
- [ ] `kernel/console/font.cpp`
- [ ] `kernel/console/font.hpp`
- [ ] `kernel/console/splash.cpp`
- [ ] `kernel/console/splash.hpp`

### Kernel Memory Management - C++

- [ ] `kernel/mm/pmm.cpp`
- [ ] `kernel/mm/pmm.hpp`
- [ ] `kernel/mm/vmm.cpp`
- [ ] `kernel/mm/vmm.hpp`
- [ ] `kernel/mm/kheap.cpp`
- [ ] `kernel/mm/kheap.hpp`

### Kernel Library - C++

- [ ] `kernel/lib/string.cpp`
- [ ] `kernel/lib/string.hpp`
- [ ] `kernel/lib/printf.cpp`
- [ ] `kernel/lib/printf.hpp`

---

## Notes

_Use this section to track issues, decisions, and learnings during implementation._

### C++ Freestanding Considerations

- No C++ standard library available
- Must use -fno-exceptions -fno-rtti
- No global constructors/destructors without explicit init
- memcpy/memset must be provided for compiler-generated code
- Placement new may be needed for object initialization

### Issues Encountered

- PE32+ conversion for UEFI on macOS using objcopy failed to produce valid executables
- UEFI shell showed "Script Error Status: Unsupported" for converted binaries

### Decisions Made

- Deferred UEFI bootloader (vboot) development - using QEMU -kernel for direct kernel loading
- Using aarch64-elf-gcc (Homebrew) instead of aarch64-linux-gnu-gcc on macOS
- Kernel loads at 0x40000000 (QEMU virt default) instead of 0x100000
- Using physical addresses for now (no HHDM until vboot is implemented)

### Learnings

- QEMU's PL011 UART at 0x09000000 works without explicit initialization
- gtimeout from coreutils is needed on macOS (no native timeout command)
- Cross-compiler installation: `brew install aarch64-elf-gcc`
- fw_cfg selector must be written in big-endian format on ARM
- ramfb requires DMA writes to configure the framebuffer (regular fw_cfg writes don't work)
- fw_cfg DMA uses a descriptor structure with big-endian fields
- AArch64 exception vector entries must be exactly 128 bytes each - use trampolines if handlers exceed this
- Must set SPSel=1 before setting up kernel stack to use SP_EL1 for kernel mode
- ARM architected timer (IRQ 30) fires at 1000 Hz for 1ms tick precision
- GICv2 CPU interface at 0x08010000 handles interrupt acknowledgement and EOI

---

*Last updated: 2025-12-25 (Phase 5 complete, Phase 6 added)*

---

# Phase 2: Multitasking

**Goal:** Multiple kernel tasks with IPC
**Milestone:** Two tasks ping-pong messages over a channel
**Prerequisites:** Phase 1 complete (boot, console, PMM, VMM, heap, GIC, timer)

---

## Overview

| Milestone | Description            | Status     |
|-----------|------------------------|------------|
| 1         | Task Infrastructure    | ✅ Complete |
| 2         | Context Switching      | ✅ Complete |
| 3         | Scheduler              | ✅ Complete |
| 4         | Syscall Infrastructure | ✅ Complete |
| 5         | Channels (IPC)         | ✅ Complete |
| 6         | Poll & Timers          | ✅ Complete |
| 7         | Integration & Testing  | ✅ Complete |

---

## Milestone 1: Task Infrastructure

### 1.1 Task States & Structure

- [x] Create `kernel/sched/task.hpp`
- [x] Define TaskState enum (Invalid, Ready, Running, Blocked, Exited)
- [x] Define TaskContext struct (x19-x28, x29/fp, x30/lr, sp)
- [x] Define Task struct (id, name, context, trap_frame, kernel stack, state, flags)
- [x] Define task flags (TASK_FLAG_KERNEL, TASK_FLAG_IDLE)
- [x] Define KERNEL_STACK_SIZE (16KB)
- [x] Define TIME_SLICE_DEFAULT (10ms at 1000Hz)

### 1.2 Task Management

- [x] Create `kernel/sched/task.cpp`
- [x] Implement task::init() - create idle task
- [x] Implement task::allocate_task() - allocate Task structure
- [x] Implement task::free_task() - free kernel stack and Task
- [x] Implement task::create() - allocate, setup stack, enqueue
- [x] Implement task::current() - return current task pointer
- [x] Implement task::exit() - mark exited, schedule
- [x] Implement task::yield() - give up time slice, schedule

### 1.3 Task Entry Trampoline

- [x] Create `kernel/sched/context.S`
- [x] Implement task_entry_trampoline (call entry with arg)
- [x] Implement task_exit_wrapper (call exit on return)

### 1.4 Milestone 1 Exit Criteria

- [x] Task structure defined with all required fields
- [x] Kernel stack allocation working
- [x] Task creation and setup functional
- [x] Entry trampoline correctly calls task function

---

## Milestone 2: Context Switching

### 2.1 Context Switch Assembly

- [x] Implement context_switch(old_ctx, new_ctx) in context.S
- [x] Save callee-saved registers (x19-x28) to old context
- [x] Save frame pointer (x29) and link register (x30)
- [x] Save stack pointer
- [x] Load all registers from new context
- [x] Return to new context via ret

### 2.2 Trap Frame Save/Restore

- [x] Implement save_trap_frame() for syscall/interrupt entry
- [x] Save x0-x30 to trap frame
- [x] Save SP_EL0, ELR_EL1, SPSR_EL1
- [x] Implement restore_trap_frame() for return to task
- [x] Restore all registers and eret

### 2.3 Milestone 2 Exit Criteria

- [x] context_switch saves/restores all callee-saved registers
- [x] Tasks can switch between each other
- [x] Stack pointer is correctly saved/restored
- [x] Return address (x30) is correctly handled

---

## Milestone 3: Scheduler

### 3.1 Scheduler Implementation

- [x] Create `kernel/sched/scheduler.hpp`
- [x] Create `kernel/sched/scheduler.cpp`
- [x] Implement ready queue (FIFO linked list)
- [x] Implement scheduler::init()
- [x] Implement scheduler::enqueue() - add task to ready queue
- [x] Implement scheduler::dequeue() - remove from ready queue
- [x] Implement scheduler::schedule() - pick next task, context switch
- [x] Implement scheduler::tick() - decrement time slice
- [x] Implement scheduler::preempt() - check and preempt if time expired

### 3.2 Wait Queues

- [x] Blocking implemented directly in channel/poll (simplified)
- [x] Tasks block by setting state to Blocked and yielding
- [x] Wake by setting state to Ready and enqueueing

### 3.3 Timer Integration

- [x] Update timer IRQ handler to call scheduler::tick()
- [x] Call scheduler::preempt() on each tick
- [x] Verify preemption interrupts long-running tasks

### 3.4 Milestone 3 Exit Criteria

- [x] Round-robin scheduling works
- [x] Timer preemption interrupts long-running tasks
- [x] Blocking/waking works for IPC
- [x] Test shows interleaved output from multiple tasks

---

## Milestone 4: Syscall Infrastructure

### 4.1 Syscall Numbers

- [x] Create `kernel/include/syscall_nums.hpp`
- [x] Define Task syscalls (TaskYield, TaskExit, TaskCurrent)
- [x] Define Channel syscalls (ChannelCreate, ChannelSend, ChannelRecv, ChannelClose)
- [x] Define Time syscalls (TimeNow, Sleep)
- [x] Define Debug syscalls (DebugPrint)

### 4.2 Error Codes

- [x] Create `kernel/include/error.hpp`
- [x] Define VOK (0)
- [x] Define VERR_* error codes (INVALID_HANDLE, INVALID_ARG, OUT_OF_MEMORY, etc.)

### 4.3 Syscall Entry

- [x] Route SVC exceptions to syscall dispatcher in exceptions.cpp
- [x] Pass syscall number (X8) and args (X0-X5) to dispatcher
- [x] Return results in X0

### 4.4 Syscall Dispatcher

- [x] Create `kernel/syscall/dispatch.hpp`
- [x] Create `kernel/syscall/dispatch.cpp`
- [x] Implement dispatch() - switch on syscall number
- [x] Route to appropriate syscall handler

### 4.5 Task Syscalls

- [x] Implement sys_task_yield()
- [x] Implement sys_task_exit()
- [x] Implement sys_task_current()

### 4.6 Milestone 4 Exit Criteria

- [x] SVC exceptions route to syscall dispatcher
- [x] Syscall number in X8 dispatches correctly
- [x] Arguments passed in X0-X5
- [x] Results returned in X0
- [x] TaskYield, TaskExit, TaskCurrent work

---

## Milestone 5: Channels (IPC)

### 5.1 Message Buffer

- [x] Create `kernel/ipc/channel.hpp`
- [x] Define MAX_MSG_SIZE (256 bytes)
- [x] Define Message struct with circular buffer
- [x] Implement blocking send/receive

### 5.2 Channel Implementation

- [x] Create `kernel/ipc/channel.cpp`
- [x] Define Channel struct (buffer, read/write indices, blocked tasks)
- [x] Implement channel::create() - allocate channel
- [x] Implement channel::send() - queue message, wake receivers
- [x] Implement channel::recv() - dequeue message, wake senders
- [x] Implement channel::close() - mark closed, wake waiters

### 5.3 Channel Syscalls

- [x] Add channel syscalls to dispatch.cpp
- [x] Implement sys_channel_create()
- [x] Implement sys_channel_send()
- [x] Implement sys_channel_recv()
- [x] Implement sys_channel_close()

### 5.4 Milestone 5 Exit Criteria

- [x] ChannelCreate returns channel ID
- [x] ChannelSend queues message (blocking if full)
- [x] ChannelRecv dequeues message (blocking if empty)
- [x] Ping-pong test works with two channels

---

## Milestone 6: Poll & Timers

### 6.1 Poll Infrastructure

- [x] Create `kernel/ipc/poll.hpp`
- [x] Create `kernel/ipc/poll.cpp`
- [x] Define Timer struct (deadline, expired, waiter)
- [x] Implement timer management

### 6.2 Sleep Implementation

- [x] Implement poll::sleep_ms() - create timer, block until expired
- [x] Implement poll::time_now_ms() - get current time
- [x] Implement poll::check_timers() - wake expired timer waiters

### 6.3 Timer Integration

- [x] Call poll::check_timers() from timer IRQ handler
- [x] Blocked tasks wake when timers expire

### 6.4 Time Syscalls

- [x] Add SYS_TIME_NOW to dispatcher
- [x] Add SYS_SLEEP to dispatcher
- [x] Implement sys_time_now()
- [x] Implement sys_sleep()

### 6.5 Milestone 6 Exit Criteria

- [x] Sleep syscall blocks for specified duration
- [x] Tasks wake after timer expires
- [x] Time syscall returns current tick count

---

## Milestone 7: Integration & Testing

### 7.1 Ping-Pong Test

- [x] Integrated ping-pong test in main.cpp
- [x] Create two channels (ping->pong, pong->ping)
- [x] Create ping task - sends "PING", waits for "PONG"
- [x] Create pong task - receives "PING", sends "PONG"
- [x] Run 3 message exchanges with 500ms delays
- [x] Verify interleaved output

### 7.2 Scheduler Test

- [x] Tasks switch correctly via timer preemption
- [x] Tasks switch correctly via blocking IPC
- [x] Tasks switch correctly via sleep

### 7.3 Update Kernel Main

- [x] Add task system init
- [x] Add scheduler init
- [x] Add channel init
- [x] Add poll init
- [x] Create test tasks and run scheduler

### 7.4 Milestone 7 Exit Criteria

- [x] Two tasks exchange 3 messages each direction
- [x] No deadlocks or crashes
- [x] Output shows interleaved ping/pong with sleeps
- [x] System remains stable after test

---

## Phase 2 Definition of Done

### Functionality

- [x] Tasks can be created and scheduled
- [x] Context switching preserves all registers
- [x] Timer preemption works at 1000Hz
- [x] SVC syscalls dispatch correctly
- [x] Channels send/receive messages
- [x] Sleep blocks and wakes on timer expiry
- [x] Ping-pong test passes (3 exchanges with delays)

### Stability

- [x] System stable after test completion
- [x] Tasks exit cleanly

### Code Quality

- [x] Compiles with `-Wall -Wextra -Werror`
- [x] All source files have appropriate headers
- [x] Code follows consistent style

---

## Phase 2 File Checklist

### Scheduler (kernel/sched/)

- [x] `task.cpp` / `task.hpp`
- [x] `scheduler.cpp` / `scheduler.hpp`
- [x] `context.S`

### IPC (kernel/ipc/)

- [x] `channel.cpp` / `channel.hpp`
- [x] `poll.cpp` / `poll.hpp`

### Syscall (kernel/syscall/)

- [x] `dispatch.cpp` / `dispatch.hpp`

### Include (kernel/include/)

- [x] `syscall_nums.hpp`
- [x] `error.hpp`
- [x] `syscall.hpp` (user-space syscall wrappers)

---

# Phase 3: User Space

**Goal:** First user-space Viper executing code
**Milestone:** "Hello World" printed from user space
**Prerequisites:** Phase 2 complete (tasks, scheduler, channels, polling)

---

## Overview

| Milestone | Description             | Status     |
|-----------|-------------------------|------------|
| 1         | VViper & Address Spaces | ✅ Complete |
| 2         | Capability Tables       | ✅ Complete |
| 3         | EL0/EL1 Transitions     | ✅ Complete |
| 4         | KHeap Objects           | ✅ Complete |
| 5         | Loader & vinit          | ✅ Complete |
| 6         | Hello World Test        | ✅ Complete |

---

## Milestone 1: VViper & Address Spaces

### 1.1 VViper Structure

- [x] Create `kernel/viper/viper.hpp`
- [x] Define ViperState enum (Invalid, Creating, Running, Exiting, Zombie)
- [x] Define Viper struct (id, name, ttbr0, asid, cap_table, tasks, state)
- [x] Define user address space layout constants

### 1.2 Address Space Class

- [x] Create `kernel/viper/address_space.hpp`
- [x] Create `kernel/viper/address_space.cpp`
- [x] Define protection flags (PROT_READ, PROT_WRITE, PROT_EXEC)
- [x] Implement AddressSpace::init() - allocate ASID and root table
- [x] Implement AddressSpace::destroy() - free tables and ASID
- [x] Implement AddressSpace::map() - map physical to virtual
- [x] Implement AddressSpace::unmap()
- [x] Implement AddressSpace::alloc_map() - allocate and map pages
- [x] Implement AddressSpace::translate() - virtual to physical

### 1.3 ASID Management

- [x] Implement asid_init() in address_space.cpp
- [x] Implement asid_alloc() - bitmap-based allocation
- [x] Implement asid_free()

### 1.4 TTBR0 Switching

- [x] Implement switch_address_space(ttbr0, asid) in address_space.cpp
- [x] Implement tlb_flush_asid()
- [x] Implement tlb_flush_page()

### 1.5 Milestone 1 Exit Criteria

- [x] Viper structure created with ASID
- [x] User page tables allocated and managed
- [x] TTBR0 switching works between Vipers

---

## Milestone 2: Capability Tables

### 2.1 Handle Encoding

- [x] Create `kernel/cap/handle.hpp`
- [x] Define Handle type (24-bit index + 8-bit generation)
- [x] Implement handle_index(), handle_gen(), make_handle()

### 2.2 Rights

- [x] Create `kernel/cap/rights.hpp`
- [x] Define Rights enum (CAP_READ, CAP_WRITE, CAP_EXECUTE, etc.)

### 2.3 Capability Table

- [x] Create `kernel/cap/table.hpp`
- [x] Create `kernel/cap/table.cpp`
- [x] Define Entry struct (object, rights, kind, generation)
- [x] Define Kind enum (String, Array, Blob, Channel, etc.)
- [x] Implement Table::init() - allocate entries
- [x] Implement Table::insert() - allocate handle
- [x] Implement Table::get() - validate and return entry
- [x] Implement Table::get_checked() - validate kind
- [x] Implement Table::get_with_rights() - validate rights
- [x] Implement Table::remove() - increment generation
- [x] Implement Table::derive() - create handle with reduced rights

### 2.4 Milestone 2 Exit Criteria

- [x] Handle allocation with generation counter
- [x] Type-safe handle validation
- [x] Rights checking works
- [x] Derive creates restricted handles

---

## Milestone 3: EL0/EL1 Transitions

### 3.1 MMU Configuration

- [x] Create `kernel/arch/aarch64/mmu.hpp`
- [x] Create `kernel/arch/aarch64/mmu.cpp`
- [x] Implement kernel identity-mapped page tables (1GB blocks)
- [x] Configure TCR_EL1, MAIR_EL1, TTBR0_EL1
- [x] Enable MMU with proper page table setup

### 3.2 User-Mode Entry Assembly

- [x] Update `kernel/arch/aarch64/exceptions.S` with EL0 handlers
- [x] Implement vec_el0_sync (save registers, dispatch SVC)
- [x] Implement vec_el0_irq (save registers, handle IRQ)
- [x] Implement enter_user_mode (set SP_EL0, ELR_EL1, SPSR_EL1, eret)

### 3.3 User Syscall Dispatcher

- [x] Update `kernel/arch/aarch64/exceptions.cpp` with EL0 handlers
- [x] Implement handle_el0_sync() - dispatch syscalls from user
- [x] Implement handle_el0_irq() - handle IRQs from user mode
- [x] Handle debug_print (0xF0) and exit (0x01) syscalls

### 3.4 Milestone 3 Exit Criteria

- [x] MMU enabled with kernel identity mapping
- [x] SVC from EL0 routes to dispatcher
- [x] Results returned in X0
- [x] Faults handled gracefully
- [x] Can enter user mode and return via syscall

---

## Milestone 4: KHeap Objects

### 4.1 KObject Base Class

- [x] Create `kernel/kobj/object.hpp`
- [x] Define Object base class with reference counting
- [x] Implement ref(), unref(), release()
- [x] Implement type-safe casting via as<T>()

### 4.2 Blob Object

- [x] Create `kernel/kobj/blob.hpp`
- [x] Create `kernel/kobj/blob.cpp`
- [x] Implement Blob::create() - allocate physical pages
- [x] Implement destructor - free pages
- [x] Provide data(), size(), phys() accessors

### 4.3 Channel Object

- [x] Create `kernel/kobj/channel.hpp`
- [x] Create `kernel/kobj/channel.cpp`
- [x] Implement Channel::create() - wrap low-level channel
- [x] Implement destructor - close channel
- [x] Delegate send/recv to underlying channel

### 4.4 Milestone 4 Exit Criteria

- [x] KHeap objects allocate and free correctly
- [x] Refcounting works
- [x] Objects tracked via capability table

---

## Milestone 5: Loader & vinit

### 5.1 ELF Loader

- [x] Create `kernel/loader/elf.hpp`
- [x] Create `kernel/loader/elf.cpp`
- [x] Define ELF64 header structures
- [x] Implement validate_header() - check magic, arch
- [x] Implement flags_to_prot() - ELF flags to protection

### 5.2 Loader Implementation

- [x] Create `kernel/loader/loader.hpp`
- [x] Create `kernel/loader/loader.cpp`
- [x] Implement load_elf() - parse headers, map segments
- [x] Map code sections with RX permissions
- [x] Map data sections with RW permissions
- [x] Zero BSS regions
- [x] Cache maintenance for executable code

### 5.3 User Address Space Layout

- [x] Define USER_CODE_BASE at 2GB (above kernel mappings)
- [x] Define USER_STACK_TOP at ~128TB
- [x] User L1 table inherits kernel L1[0]/L1[1] for device/RAM access

### 5.4 Milestone 5 Exit Criteria

- [x] ELF loader parses headers correctly
- [x] Code and data mapped into user space
- [x] Stack allocated for user task

---

## Milestone 6: Hello World Test

### 6.1 Embedded User Program

- [x] Create embedded user program in main.cpp
- [x] User program: adr x0, message; mov x8, #0xF0; svc #0; mov x8, #1; mov x0, #0; svc #0
- [x] Message: "Hello from user space!\n"

### 6.2 Integration Test

- [x] Create Viper with address space
- [x] Map user code at 0x80000000
- [x] Map user stack
- [x] Switch to user address space
- [x] Enter user mode via eret
- [x] Handle debug_print syscall from user
- [x] Handle exit syscall from user

### 6.3 Milestone 6 Exit Criteria

- [x] User program loads and executes in EL0
- [x] Syscalls work from user space
- [x] "Hello from user space!" appears on console
- [x] User task exits cleanly with code 0
- [x] System remains stable

---

## Phase 3 Definition of Done

### Functionality

- [x] Viper creation allocates address space with unique ASID
- [x] TTBR0 switches correctly between Vipers
- [x] Capability table validates handles with generation
- [x] SVC from EL0 routes to syscall dispatcher
- [x] Syscall returns correct values in X0
- [x] KHeap objects can be allocated via capability table
- [x] User program loads and executes in user mode
- [x] "Hello from user space!" appears on console
- [x] User task can exit cleanly
- [x] System remains stable after user exit

### Code Quality

- [x] Compiles with `-Wall -Wextra -Werror`
- [x] All source files have appropriate headers
- [x] Code follows consistent style

---

## Phase 3 File Checklist

### Viper (kernel/viper/)

- [x] `viper.cpp` / `viper.hpp`
- [x] `address_space.cpp` / `address_space.hpp`

### Capability (kernel/cap/)

- [x] `table.cpp` / `table.hpp`
- [x] `handle.hpp`
- [x] `rights.hpp`

### KObj (kernel/kobj/)

- [x] `object.hpp`
- [x] `blob.cpp` / `blob.hpp`
- [x] `channel.cpp` / `channel.hpp`

### Loader (kernel/loader/)

- [x] `elf.cpp` / `elf.hpp`
- [x] `loader.cpp` / `loader.hpp`

### Architecture (kernel/arch/aarch64/)

- [x] `mmu.cpp` / `mmu.hpp`
- [x] Updated `exceptions.S` with EL0 handlers
- [x] Updated `exceptions.cpp` with EL0 dispatch

---

## Notes

### Key Technical Decisions (Phase 3)

- MMU enabled with 1GB block mappings for kernel (0-1GB device, 1-2GB RAM)
- User address space starts at 2GB to avoid conflict with kernel blocks
- User L1 table copies kernel's L1[0]/L1[1] entries for kernel access during syscalls
- ASID support with 256 available ASIDs (0 reserved for kernel)
- Embedded user program for testing (ELF loader ready for future use)

### Test Output

```
[kernel] Entering user mode at 0x80000000 with SP=0x7fffffff0000
Hello from user space!
[syscall] User exit with code 0
[kernel] User program completed successfully!
```

---

# Phase 4: Filesystem & Shell

**Goal:** Boot from disk to interactive shell
**Milestone:** `SYS:>` prompt accepting commands
**Prerequisites:** Phase 3 complete (user space, capabilities, KHeap)

---

## Overview

| Milestone | Description           | Status     |
|-----------|-----------------------|------------|
| 1         | virtio Infrastructure | ✅ Complete |
| 2         | virtio-blk Driver     | ✅ Complete |
| 3         | Block Cache           | ✅ Complete |
| 4         | ViperFS Read-Only     | ✅ Complete |
| 5         | ViperFS Read-Write    | ✅ Complete |
| 6         | VFS & File Syscalls   | ✅ Complete |
| 7         | vinit from Disk       | ✅ Complete |
| 8         | vsh Shell             | ✅ Complete |
| 9         | Commands & Polish     | ✅ Complete |

---

## Milestone 1: virtio Infrastructure

### 1.1 Virtio Device Discovery

- [x] Create `kernel/drivers/virtio/virtio.hpp`
- [x] Create `kernel/drivers/virtio/virtio.cpp`
- [x] Define MMIO register offsets
- [x] Define device status bits
- [x] Define device types (NET, BLK, GPU, INPUT)
- [x] Implement Device class (init, read32, write32)
- [x] Implement probe_devices() - scan virtio MMIO range
- [x] Implement get_device(type) - find device by type

### 1.2 Virtqueue Implementation

- [x] Create `kernel/drivers/virtio/virtqueue.hpp`
- [x] Create `kernel/drivers/virtio/virtqueue.cpp`
- [x] Define VringDesc, VringAvail, VringUsed structures
- [x] Implement Virtqueue::init() - allocate rings, configure device
- [x] Implement alloc_desc() / free_desc()
- [x] Implement set_desc() - configure descriptor
- [x] Implement submit() - add to available ring
- [x] Implement kick() - notify device
- [x] Implement poll_used() - check for completions

### 1.3 Milestone 1 Exit Criteria

- [x] Virtio devices discovered at boot
- [x] Virtqueue allocates and configures correctly
- [x] Device handshake works (ACKNOWLEDGE, DRIVER, DRIVER_OK)

---

## Milestone 2: virtio-blk Driver

### 2.1 Block Device

- [x] Create `kernel/drivers/virtio/blk.hpp`
- [x] Create `kernel/drivers/virtio/blk.cpp`
- [x] Define BlkReqHeader structure
- [x] Define BlkConfig structure
- [x] Implement BlkDevice::init() - read config, setup virtqueue
- [x] Implement read_sectors() - synchronous read
- [x] Implement write_sectors() - synchronous write

### 2.2 Milestone 2 Exit Criteria

- [x] virtio-blk device initializes
- [x] Can read sectors from disk
- [x] Can write sectors to disk

---

## Milestone 3: Block Cache

### 3.1 Cache Implementation

- [x] Create `kernel/fs/cache.hpp`
- [x] Create `kernel/fs/cache.cpp`
- [x] Define CacheBlock structure (data, dirty, refcount, LRU links)
- [x] Implement init() - allocate cache blocks
- [x] Implement get() - read block (load from disk if needed)
- [x] Implement get_for_write() - mark dirty
- [x] Implement release() - decrement refcount
- [x] Implement sync() - write dirty blocks
- [x] Implement LRU eviction

### 3.2 Milestone 3 Exit Criteria

- [x] Block cache reduces disk reads
- [x] Dirty blocks written on sync
- [x] LRU eviction works under memory pressure

---

## Milestone 4: ViperFS Read-Only

### 4.1 On-Disk Structures

- [x] Create `kernel/fs/viperfs/format.hpp`
- [x] Define Superblock structure (magic, version, block counts, etc.)
- [x] Define Inode structure (mode, size, blocks, pointers)
- [x] Define DirEntry structure (inode, name)
- [x] Define file type constants

### 4.2 ViperFS Mount

- [x] Create `kernel/fs/viperfs/viperfs.hpp`
- [x] Create `kernel/fs/viperfs/viperfs.cpp`
- [x] Implement mount() - read and validate superblock
- [x] Implement read_inode() - load inode from disk
- [x] Implement lookup() - find entry in directory
- [x] Implement readdir() - list directory entries
- [x] Implement read_data() - read file content

### 4.3 Milestone 4 Exit Criteria

- [x] ViperFS mounts successfully
- [x] Can read superblock and inodes
- [x] Can list directories
- [x] Can read file contents

---

## Milestone 5: ViperFS Read-Write

### 5.1 Block Allocation

- [x] Create `kernel/fs/viperfs/alloc.hpp`
- [x] Create `kernel/fs/viperfs/alloc.cpp`
- [x] Implement alloc_block() - allocate from bitmap
- [x] Implement free_block() - mark as free
- [x] Implement alloc_inode() / free_inode()

### 5.2 Write Operations

- [x] Implement create() - create file or directory
- [x] Implement write_data() - write file content
- [x] Implement truncate() - change file size
- [x] Implement unlink() - remove file
- [x] Implement mkdir() - create directory
- [x] Implement sync_inode() - write inode to disk

### 5.3 Milestone 5 Exit Criteria

- [x] Can create new files
- [x] Can write to files
- [x] Can delete files
- [x] Can create directories

---

## Milestone 6: VFS & File Syscalls

### 6.1 VFS Layer

- [x] Create `kernel/fs/vfs/vfs.hpp`
- [x] Create `kernel/fs/vfs/vfs.cpp`
- [x] Define OpenFile structure (inode, position, flags)
- [x] Define FileInfo, DirInfo structures
- [x] Implement open() - create open file object
- [x] Implement read() / write() - file I/O
- [x] Implement seek() - change position
- [x] Implement close() - release file
- [x] Implement stat() - get file info
- [x] Implement getdents() - read directory entries

### 6.2 File Syscalls

- [x] Added syscalls in exceptions.cpp
- [x] Implement SYS_OPEN (0x40) - open file
- [x] Implement SYS_CLOSE (0x41) - close file
- [x] Implement SYS_READ (0x42) - read file
- [x] Implement SYS_WRITE (0x43) - write file
- [x] Implement SYS_STAT (0x44) - get file info
- [x] Implement SYS_READDIR (0x45) - list directory

### 6.3 Milestone 6 Exit Criteria

- [x] VFS provides unified file operations
- [x] File handles work via FD table
- [x] Can open, read, write, close from user space

---

## Milestone 7: vinit from Disk

### 7.1 Disk Boot

- [x] Create `kernel/loader/loader.cpp`
- [x] Implement load_from_disk()
- [x] Mount root filesystem
- [x] Open /vinit.elf from disk
- [x] Read executable into memory
- [x] Create vinit Viper with address space
- [x] Load and execute ELF

### 7.2 Milestone 7 Exit Criteria

- [x] vinit loads from disk instead of embedded
- [x] Boot process creates vinit Viper
- [x] vinit executes successfully
- [x] Fixed mkfs.viperfs for indirect blocks (files > 48KB)

---

## Milestone 8: vsh Shell

### 8.1 Shell Main

- [x] Create `user/vinit/vinit.cpp` with built-in shell
- [x] Implement main loop (prompt, read, parse, execute)
- [x] Implement readline with echo and backspace
- [x] Implement command parsing

### 8.2 Console I/O Syscalls

- [x] Implement SYS_GETCHAR (0xF1) - read character
- [x] Implement SYS_PUTCHAR (0xF2) - write character
- [x] Output to both serial and graphics console

### 8.3 Milestone 8 Exit Criteria

- [x] Shell displays prompt (vsh>)
- [x] Can parse commands and arguments
- [x] Built-in commands work (help, echo, clear, uname, uptime, exit)
- [x] Input/output works on both serial and graphics console

---

## Milestone 9: Commands & Polish

### 9.1 Core Commands

- [x] Implement help command
- [x] Implement echo command
- [x] Implement clear command (ANSI escape)
- [x] Implement uname command
- [x] Implement uptime command (placeholder)
- [x] Implement ls command - list directory
- [x] Implement cat command - display file contents

### 9.2 User Library

- [x] Create `user/syscall.hpp` - syscall wrappers
- [x] Define Stat and DirEnt structures
- [x] Implement open, close, read, write, stat, readdir wrappers

### 9.3 Disk Image Tools

- [x] Create `tools/mkfs.viperfs.cpp` - format disk with files
- [x] Support indirect blocks for files > 48KB

### 9.4 Milestone 9 Exit Criteria

- [x] Core commands work (ls, cat, help, echo, clear)
- [x] System stable for extended shell use
- [x] Graphics console output working

---

## Phase 4 Definition of Done

### Functionality

- [x] virtio-blk can read/write sectors
- [x] ViperFS mounts and reads files
- [x] ViperFS can create and write files
- [x] VFS provides unified file operations
- [x] File syscalls work from user space
- [x] vinit loads from /vinit.elf on disk
- [x] vsh displays `vsh>` prompt
- [x] ls, cat, help, echo, clear commands work
- [x] System stable for extended shell use
- [x] Graphics console (ramfb) output working

### Code Quality

- [x] Compiles with `-Wall -Wextra -Werror`
- [x] All source files have appropriate headers
- [x] Code follows consistent style

---

## Phase 4 File Checklist

### Drivers (kernel/drivers/virtio/)

- [x] `virtio.cpp` / `virtio.hpp`
- [x] `virtqueue.cpp` / `virtqueue.hpp`
- [x] `blk.cpp` / `blk.hpp`

### Filesystem (kernel/fs/)

- [x] `vfs/vfs.cpp` / `vfs.hpp`
- [x] `cache.cpp` / `cache.hpp`
- [x] `viperfs/viperfs.cpp` / `viperfs.hpp`
- [x] `viperfs/format.hpp`

### Loader (kernel/loader/)

- [x] `loader.cpp` / `loader.hpp`

### Syscalls (kernel/arch/aarch64/)

- [x] `exceptions.cpp` - file I/O and console syscalls

### User Space (user/)

- [x] `vinit/vinit.cpp` - init process with built-in shell
- [x] `syscall.hpp` - syscall wrappers

### Tools (tools/)

- [x] `mkfs.viperfs.cpp` - filesystem creation tool

---

---

# Phase 5: Input & Polish

**Goal:** Complete interactive shell experience
**Milestone:** Full keyboard input, line editing, command history, tab completion
**Prerequisites:** Phase 4 complete (filesystem, shell, commands)

---

## Overview

| Milestone | Description          | Status     |
|-----------|----------------------|------------|
| 1         | virtio-input Driver  | ✅ Complete |
| 2         | Input Subsystem      | ✅ Complete |
| 3         | Key Translation      | ✅ Complete |
| 4         | Console Input        | ✅ Complete |
| 5         | Line Editor          | ✅ Complete |
| 6         | History & Completion | ✅ Complete |
| 7         | Font System          | ✅ Complete |
| 8         | Commands & Polish    | ✅ Complete |

---

## Milestone 1: virtio-input Driver

### 1.1 virtio-input Device

- [x] Create `kernel/drivers/virtio/input.hpp`
- [x] Create `kernel/drivers/virtio/input.cpp`
- [x] Define config select values
- [x] Define event types (EV_SYN, EV_KEY, EV_REL, EV_ABS)
- [x] Define VirtioInputEvent structure
- [x] Implement InputDevice class
- [x] Implement device discovery and initialization
- [x] Implement event polling from virtqueue

### 1.2 Milestone 1 Exit Criteria

- [x] Keyboard device discovered and initialized
- [x] Mouse device discovered and initialized
- [x] Raw events received from QEMU

---

## Milestone 2: Input Subsystem

### 2.1 Event Queue

- [x] Create `kernel/input/input.hpp`
- [x] Create `kernel/input/input.cpp`
- [x] Define InputEvent structure
- [x] Implement EventQueue ring buffer
- [x] Implement character buffer for ASCII translation

### 2.2 Input Integration

- [x] Implement poll() function called from timer interrupt
- [x] Implement getchar() for syscall use
- [x] Implement has_char() for non-blocking checks

### 2.3 Milestone 2 Exit Criteria

- [x] Event queue buffers input events
- [x] Character buffer for translated ASCII
- [x] Polling integrated with timer interrupt

---

## Milestone 3: Key Translation

### 3.1 Key Codes

- [x] Create `kernel/input/keycodes.hpp`
- [x] Define USB HID key codes
- [x] Define modifier flags (Shift, Ctrl, Alt, Meta, Caps Lock)

### 3.2 Keyboard Processing

- [x] Implement key_to_ascii() - translate to character
- [x] Implement is_modifier() / modifier_bit()
- [x] Handle Caps Lock toggle
- [x] Generate ANSI escape sequences for special keys

### 3.3 Milestone 3 Exit Criteria

- [x] HID codes translated to ASCII
- [x] Modifier keys tracked correctly
- [x] Shift produces uppercase letters
- [x] Arrow keys generate escape sequences

---

## Milestone 4: Console Input

### 4.1 Console Input Integration

- [x] Update SYS_GETCHAR syscall handler
- [x] Use input::getchar() with serial fallback
- [x] Input available from both keyboard and serial

### 4.2 Milestone 4 Exit Criteria

- [x] getchar() works from user space
- [x] Keyboard input processed correctly
- [x] Serial input still works as fallback

---

## Milestone 5: Line Editor

### 5.1 Line Editor Core

- [x] Implement in `user/vinit/vinit.cpp`
- [x] Implement buffer management with cursor tracking
- [x] Implement insert_char() at cursor position
- [x] Implement backspace() with line redraw
- [x] Implement delete_char() (Delete key)

### 5.2 Cursor Movement

- [x] Implement Left/Right arrow movement
- [x] Implement Home/End keys
- [x] Implement Ctrl+A (Home) and Ctrl+E (End)
- [x] ANSI escape sequences for cursor control

### 5.3 Line Operations

- [x] Implement Ctrl+K (kill to end)
- [x] Implement Ctrl+U (kill line)
- [x] Implement redraw_line_from() helper
- [x] Handle Ctrl+C interrupt

### 5.4 Milestone 5 Exit Criteria

- [x] Insert and delete at cursor position
- [x] Cursor movement with arrow keys
- [x] Home/End keys work
- [x] Ctrl+K, Ctrl+U work

---

## Milestone 6: History & Completion

### 6.1 Command History

- [x] Implement history ring buffer (16 entries)
- [x] Implement history_add() - add command to history
- [x] Implement history navigation with Up/Down arrows
- [x] Save current line when navigating history
- [x] Implement `history` command to show history

### 6.2 Tab Completion

- [x] Implement command tab completion
- [x] Calculate common prefix for multiple matches
- [x] Display matches if ambiguous
- [x] Complete unique matches automatically

### 6.3 Milestone 6 Exit Criteria

- [x] Up/Down arrows navigate history
- [x] Tab completes commands
- [x] Multiple matches displayed
- [x] History command shows recent commands

---

## Milestone 7: Font System

### 7.1 Built-in Font

- [x] 8x16 VGA-style bitmap font in `kernel/console/font.cpp`
- [x] Full ASCII character set (32-127)
- [x] Font rendering integrated with graphics console

### 7.2 Milestone 7 Exit Criteria

- [x] Console renders text with bitmap font
- [x] All printable ASCII characters supported

---

## Milestone 8: Commands & Polish

### 8.1 Commands

- [x] Implement working `uptime` command (with SYS_UPTIME syscall)
- [x] Update `help` command with line editing info
- [x] All existing commands work (ls, cat, echo, clear, uname)

### 8.2 Shell Integration

- [x] Line editor integrated into shell
- [x] History navigation working
- [x] Tab completion working
- [x] Handle Ctrl+C interrupt

### 8.3 Milestone 8 Exit Criteria

- [x] Shell feels responsive and polished
- [x] Line editing works smoothly
- [x] History and completion enhance usability

---

## Phase 5 Definition of Done

### Functionality

- [x] Keyboard input works in QEMU
- [x] Mouse events received (consumed but not used yet)
- [x] Line editing with cursor movement
- [x] Backspace and Delete work
- [x] Ctrl+A/E for Home/End
- [x] Ctrl+K/U for kill lines
- [x] Up/Down arrow for history
- [x] Tab completion for commands
- [x] Working uptime command
- [x] Shell feels responsive and usable

### Code Quality

- [x] Compiles with `-Wall -Wextra -Werror`
- [x] All source files have appropriate headers
- [x] Code follows consistent style

---

## Phase 5 File Checklist

### Drivers (kernel/drivers/virtio/)

- [x] `input.cpp` / `input.hpp`

### Input (kernel/input/)

- [x] `input.cpp` / `input.hpp`
- [x] `keycodes.hpp`

### Console (kernel/console/)

- [x] `font.cpp` / `font.hpp` (built-in font)

### Syscall

- [x] `exceptions.cpp` updated with SYS_UPTIME

### User Space (user/)

- [x] `vinit/vinit.cpp` - line editor, history, tab completion
- [x] `syscall.hpp` - uptime syscall wrapper

---

## Notes

### Phase 5 Implementation

- virtio-input driver receives HID events from QEMU (keyboard and mouse)
- Input subsystem translates HID codes to ASCII and escape sequences
- Timer interrupt polls for input events (1000 Hz)
- Line editor implemented directly in vinit.cpp shell
- History and tab completion also in vinit.cpp
- Font uses existing VGA-style 8x16 bitmap font
- QEMU requires special chardev configuration for virtio-input compatibility

---

---

# Phase 6: Networking

**Goal:** Network connectivity
**Milestone:** Fetch a webpage from ViperOS
**Prerequisites:** Phase 5 complete (input, line editing, polished shell)

---

## Overview

| Milestone | Description       | Status        |
|-----------|-------------------|---------------|
| 1         | virtio-net Driver | ⬜ Not Started |
| 2         | Ethernet & ARP    | ⬜ Not Started |
| 3         | IPv4 & ICMP       | ⬜ Not Started |
| 4         | UDP               | ⬜ Not Started |
| 5         | TCP               | ⬜ Not Started |
| 6         | Socket Syscalls   | ⬜ Not Started |
| 7         | DNS Resolver      | ⬜ Not Started |
| 8         | HTTP & Commands   | ⬜ Not Started |

---

## Milestone 1: virtio-net Driver

### 1.1 virtio-net Device

- [ ] Create `kernel/drivers/virtio/net.hpp`
- [ ] Create `kernel/drivers/virtio/net.cpp`
- [ ] Define virtio-net feature bits and header
- [ ] Define VirtioNetConfig structure
- [ ] Implement NetDevice class
- [ ] Implement device discovery and initialization
- [ ] Implement RX buffer management
- [ ] Implement transmit() - send Ethernet frames
- [ ] Implement receive() - receive Ethernet frames

### 1.2 Packet Buffer

- [ ] Create `kernel/net/pbuf.hpp`
- [ ] Create `kernel/net/pbuf.cpp`
- [ ] Implement PacketBuffer class
- [ ] Implement alloc/free
- [ ] Implement prepend_header/consume_header
- [ ] Implement physical address for DMA

### 1.3 Milestone 1 Exit Criteria

- [ ] virtio-net device discovered and initialized
- [ ] Can transmit Ethernet frames
- [ ] Can receive Ethernet frames
- [ ] MAC address read from device

---

## Milestone 2: Ethernet & ARP

### 2.1 Ethernet

- [ ] Create `kernel/net/eth/ethernet.hpp`
- [ ] Create `kernel/net/eth/ethernet.cpp`
- [ ] Define EthernetHeader structure
- [ ] Define ethertype constants
- [ ] Implement rx_frame() - process incoming frames
- [ ] Implement tx_frame() - send frames

### 2.2 ARP

- [ ] Create `kernel/net/eth/arp.hpp`
- [ ] Create `kernel/net/eth/arp.cpp`
- [ ] Define ArpHeader structure
- [ ] Implement ARP cache with timeout
- [ ] Implement rx_packet() - process ARP packets
- [ ] Implement resolve() - IP to MAC resolution
- [ ] Implement send_request() - send ARP requests
- [ ] Handle ARP replies

### 2.3 Milestone 2 Exit Criteria

- [ ] Ethernet frames dispatched by ethertype
- [ ] ARP cache stores IP-to-MAC mappings
- [ ] ARP requests sent for unknown IPs
- [ ] ARP replies update cache

---

## Milestone 3: IPv4 & ICMP

### 3.1 IPv4

- [ ] Create `kernel/net/ip/ipv4.hpp`
- [ ] Create `kernel/net/ip/ipv4.cpp`
- [ ] Define IPv4Header structure
- [ ] Define protocol constants (ICMP, TCP, UDP)
- [ ] Implement rx_packet() - process IP packets
- [ ] Implement tx_packet() - send IP packets
- [ ] Implement checksum calculation
- [ ] Implement basic routing (gateway for non-local)

### 3.2 ICMP

- [ ] Create `kernel/net/ip/icmp.hpp`
- [ ] Create `kernel/net/ip/icmp.cpp`
- [ ] Define IcmpHeader structure
- [ ] Implement rx_packet() - handle echo request/reply
- [ ] Implement send_echo_request() - ping
- [ ] Implement echo callback for RTT measurement

### 3.3 Network Interface

- [ ] Create `kernel/net/netif.hpp`
- [ ] Create `kernel/net/netif.cpp`
- [ ] Implement get_ip(), set_ip()
- [ ] Implement get_netmask(), get_gateway()
- [ ] QEMU default: 10.0.2.15/24, gateway 10.0.2.2

### 3.4 Milestone 3 Exit Criteria

- [ ] IP packets routed correctly
- [ ] ping 10.0.2.2 (gateway) works
- [ ] Echo replies received
- [ ] RTT displayed

---

## Milestone 4: UDP

### 4.1 UDP Protocol

- [ ] Create `kernel/net/transport/udp.hpp`
- [ ] Create `kernel/net/transport/udp.cpp`
- [ ] Define UdpHeader structure
- [ ] Implement rx_packet() - demux to sockets
- [ ] Implement tx_packet() - send datagrams
- [ ] Implement bind() - bind local port
- [ ] Implement recv() - receive datagrams
- [ ] Implement unbind()

### 4.2 Milestone 4 Exit Criteria

- [ ] UDP packets sent and received
- [ ] Port binding works
- [ ] Ready for DNS

---

## Milestone 5: TCP

### 5.1 TCP Protocol

- [ ] Create `kernel/net/transport/tcp.hpp`
- [ ] Create `kernel/net/transport/tcp.cpp`
- [ ] Define TcpHeader structure
- [ ] Define TCP flags (SYN, ACK, FIN, RST, PSH)
- [ ] Define TcpState enum (all 11 states)
- [ ] Define TcpCb (control block) structure

### 5.2 TCP Connection Setup

- [ ] Implement socket_create()
- [ ] Implement socket_connect() - active open, 3-way handshake
- [ ] Implement socket_bind()
- [ ] Implement socket_listen()
- [ ] Implement socket_accept() - passive open
- [ ] Handle SYN, SYN-ACK, ACK

### 5.3 TCP Data Transfer

- [ ] Implement socket_send() - queue data
- [ ] Implement socket_recv() - dequeue data
- [ ] Implement send/receive buffers
- [ ] Implement sliding window
- [ ] Implement ACK generation
- [ ] Implement retransmission timer

### 5.4 TCP Connection Teardown

- [ ] Implement socket_close() - initiate close
- [ ] Handle FIN, FIN-ACK
- [ ] Implement TIME_WAIT state
- [ ] Clean up TCB after close

### 5.5 Milestone 5 Exit Criteria

- [ ] TCP connections established (3-way handshake)
- [ ] Data transferred reliably
- [ ] Connections closed cleanly (4-way handshake)
- [ ] Retransmission works

---

## Milestone 6: Socket Syscalls

### 6.1 Socket Syscalls

- [ ] Create `kernel/syscall/net_syscalls.cpp`
- [ ] Implement VSYS_NetSocket (0x00A0)
- [ ] Implement VSYS_NetBind (0x00A1)
- [ ] Implement VSYS_NetConnect (0x00A2)
- [ ] Implement VSYS_NetListen (0x00A3)
- [ ] Implement VSYS_NetAccept (0x00A4)
- [ ] Implement VSYS_NetSend (0x00A5)
- [ ] Implement VSYS_NetRecv (0x00A6)
- [ ] Implement VSYS_NetClose (0x00A7)

### 6.2 User-Space Socket Wrapper

- [ ] Create `user/lib/vnet/socket.hpp`
- [ ] Create `user/lib/vnet/socket.cpp`
- [ ] Implement TcpStream class
- [ ] Implement UdpSocket class

### 6.3 Milestone 6 Exit Criteria

- [ ] Sockets work from user space
- [ ] Connect, send, recv, close work
- [ ] Capability-based socket handles

---

## Milestone 7: DNS Resolver

### 7.1 DNS Resolver

- [ ] Create `kernel/net/dns/resolver.hpp`
- [ ] Create `kernel/net/dns/resolver.cpp`
- [ ] Implement DNS query encoding
- [ ] Implement DNS response parsing
- [ ] Implement resolve() - blocking resolution
- [ ] Implement DNS cache

### 7.2 DNS Syscall

- [ ] Implement VSYS_DnsResolve (0x00B0)
- [ ] Create user-space wrapper

### 7.3 Milestone 7 Exit Criteria

- [ ] DNS queries sent to 8.8.8.8 (or 10.0.2.3)
- [ ] A records parsed correctly
- [ ] Hostnames resolved to IPs
- [ ] Results cached

---

## Milestone 8: HTTP & Commands

### 8.1 HTTP Client

- [ ] Create `user/lib/vnet/http.hpp`
- [ ] Create `user/lib/vnet/http.cpp`
- [ ] Implement URL parsing
- [ ] Implement HTTP GET request
- [ ] Implement response parsing
- [ ] Handle chunked encoding (optional)

### 8.2 Network Commands

- [ ] Create `user/cmd/ping.cpp`
- [ ] Create `user/cmd/wget.cpp`
- [ ] Create `user/cmd/ifconfig.cpp`
- [ ] Create `user/cmd/nslookup.cpp`
- [ ] Create `user/cmd/netstat.cpp` (optional)

### 8.3 Milestone 8 Exit Criteria

- [ ] ping command works
- [ ] wget fetches web pages
- [ ] ifconfig shows network info
- [ ] nslookup resolves names

---

## Phase 6 Definition of Done

### Functionality

- [ ] virtio-net sends/receives Ethernet frames
- [ ] ARP resolves IP to MAC
- [ ] IPv4 routes packets correctly
- [ ] ICMP ping works (`ping 10.0.2.2`)
- [ ] UDP sends/receives datagrams
- [ ] TCP establishes connections
- [ ] TCP transfers data reliably
- [ ] TCP closes connections cleanly
- [ ] Socket syscalls work from user space
- [ ] DNS resolves hostnames
- [ ] HTTP GET fetches pages
- [ ] `wget http://example.com` succeeds
- [ ] Network stable under load
- [ ] No memory leaks in network stack

### Code Quality

- [ ] Compiles with `-Wall -Wextra -Werror`
- [ ] All source files have appropriate headers
- [ ] Code follows consistent style

---

## Phase 6 File Checklist

### Drivers (kernel/drivers/virtio/)

- [ ] `net.cpp` / `net.hpp`

### Network Core (kernel/net/)

- [ ] `netif.cpp` / `netif.hpp`
- [ ] `pbuf.cpp` / `pbuf.hpp`

### Ethernet (kernel/net/eth/)

- [ ] `ethernet.cpp` / `ethernet.hpp`
- [ ] `arp.cpp` / `arp.hpp`

### IP (kernel/net/ip/)

- [ ] `ipv4.cpp` / `ipv4.hpp`
- [ ] `icmp.cpp` / `icmp.hpp`
- [ ] `route.cpp` / `route.hpp`

### Transport (kernel/net/transport/)

- [ ] `udp.cpp` / `udp.hpp`
- [ ] `tcp.cpp` / `tcp.hpp`

### Socket (kernel/net/socket/)

- [ ] `socket.cpp` / `socket.hpp`
- [ ] `tcp_socket.cpp` / `tcp_socket.hpp`
- [ ] `udp_socket.cpp` / `udp_socket.hpp`

### DNS (kernel/net/dns/)

- [ ] `resolver.cpp` / `resolver.hpp`

### Syscalls (kernel/syscall/)

- [ ] `net_syscalls.cpp`

### User Library (user/lib/vnet/)

- [ ] `socket.cpp` / `socket.hpp`
- [ ] `tcp.cpp` / `tcp.hpp`
- [ ] `udp.cpp` / `udp.hpp`
- [ ] `dns.cpp` / `dns.hpp`
- [ ] `http.cpp` / `http.hpp`
- [ ] `url.cpp` / `url.hpp`

### User Commands (user/cmd/)

- [ ] `ping.cpp`
- [ ] `wget.cpp`
- [ ] `ifconfig.cpp`
- [ ] `nslookup.cpp`
- [ ] `netstat.cpp`

---

## Notes

### Network Architecture

- virtio-net driver receives/transmits Ethernet frames
- Ethernet layer demuxes by ethertype (ARP, IPv4)
- ARP manages IP-to-MAC resolution with cache
- IPv4 routes packets (local or via gateway)
- ICMP handles ping echo request/reply
- UDP provides connectionless datagrams
- TCP provides reliable byte streams with state machine
- Socket layer provides user-space API
- DNS resolver queries name servers
- HTTP client makes web requests

### QEMU Network Configuration

```bash
qemu-system-aarch64 ... \
    -netdev user,id=net0 \
    -device virtio-net-device,netdev=net0
```

Default addresses:

- IP: 10.0.2.15
- Netmask: 255.255.255.0
- Gateway: 10.0.2.2
- DNS: 10.0.2.3

---

## v1.0 Complete After Phase 6!

With Phase 6 complete, ViperOS v1.0 is achieved:

| Feature    | Status                    |
|------------|---------------------------|
| Boot       | ✅ Graphical console       |
| Memory     | ✅ PMM, VMM, heap          |
| Tasks      | ✅ Preemptive multitasking |
| IPC        | ✅ Channels, polling       |
| User Space | ✅ Vipers, capabilities    |
| Filesystem | ✅ ViperFS, VFS            |
| Shell      | ✅ vsh with editing        |
| Input      | ✅ Keyboard, line editor   |
| Network    | ⬜ TCP/IP, HTTP            |

**ViperOS will be able to:**

- Boot to a graphical shell
- Run multiple programs
- Read/write files
- Accept keyboard input
- Connect to the internet
- Fetch web pages
