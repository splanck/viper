/**
 * @file main.c
 * @brief VBoot - ViperOS UEFI Bootloader
 *
 * @details
 * This bootloader implements the complete UEFI boot sequence:
 * 1. Locate the ESP filesystem
 * 2. Load kernel.elf from the ESP
 * 3. Parse ELF headers and allocate pages for segments
 * 4. Gather UEFI memory map
 * 5. Locate GOP framebuffer
 * 6. Fill VBootInfo structure
 * 7. Call ExitBootServices
 * 8. Jump to kernel entry with x0 = &VBootInfo
 */

#include "efi.h"
#include "vboot.h"

// Global EFI pointers
EFI_SYSTEM_TABLE *gST = NULL;
EFI_BOOT_SERVICES *gBS = NULL;
EFI_HANDLE gImageHandle = NULL;

// =============================================================================
// ELF64 Definitions
// =============================================================================

/**
 * @name ELF constants and structures
 * @brief Minimal ELF64 definitions used to load the kernel image.
 *
 * @details
 * `vboot` loads the kernel as an ELF64 executable. To avoid pulling in an ELF
 * parsing library, the bootloader defines the small subset of ELF structures
 * needed to locate program headers and load PT_LOAD segments.
 * @{
 */

/** @brief ELF magic value (`0x7F 'E' 'L' 'F'`) used to validate the file. */
#define ELF_MAGIC 0x464C457F

/**
 * @brief ELF64 file header.
 *
 * @details
 * This structure describes the overall ELF file and points to the program
 * header table via `e_phoff`/`e_phnum`. Only a subset of fields are used by the
 * bootloader.
 */
typedef struct
{
    UINT8 e_ident[16];
    UINT16 e_type;
    UINT16 e_machine;
    UINT32 e_version;
    UINT64 e_entry;
    UINT64 e_phoff;
    UINT64 e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize;
    UINT16 e_phentsize;
    UINT16 e_phnum;
    UINT16 e_shentsize;
    UINT16 e_shnum;
    UINT16 e_shstrndx;
} Elf64_Ehdr;

/**
 * @brief ELF64 program header.
 *
 * @details
 * Each program header describes one loadable segment. The bootloader iterates
 * the program headers and loads only `PT_LOAD` segments.
 */
typedef struct
{
    UINT32 p_type;
    UINT32 p_flags;
    UINT64 p_offset;
    UINT64 p_vaddr;
    UINT64 p_paddr;
    UINT64 p_filesz;
    UINT64 p_memsz;
    UINT64 p_align;
} Elf64_Phdr;

/** @brief Program header type indicating a loadable segment. */
#define PT_LOAD 1
/** @brief ELF e_machine value for AArch64. */
#define EM_AARCH64 183

/** @} */

// =============================================================================
// Console Output Functions
// =============================================================================

/**
 * @brief Print a UTF-16 string to the UEFI console.
 *
 * @details
 * This helper uses `gST->ConOut->OutputString` to write to the firmware console.
 * It is safe to call only while UEFI boot services are active; after
 * `ExitBootServices` the firmware console is no longer available.
 *
 * @param str NUL-terminated UTF-16 string to print.
 */
static void print(const CHAR16 *str)
{
    if (gST && gST->ConOut)
    {
        gST->ConOut->OutputString(gST->ConOut, (CHAR16 *)str);
    }
}

/**
 * @brief Print a UTF-16 string followed by CRLF.
 *
 * @details
 * UEFI console output typically expects CRLF (`\\r\\n`) line endings.
 *
 * @param str NUL-terminated UTF-16 string to print.
 */
static void println(const CHAR16 *str)
{
    print(str);
    print(L"\r\n");
}

/**
 * @brief Print a 64-bit value in hexadecimal with a `0x` prefix.
 *
 * @details
 * Produces a fixed-width 16-digit uppercase hexadecimal representation, which
 * is useful for printing addresses and firmware status codes.
 *
 * @param value Value to print.
 */
static void print_hex(UINT64 value)
{
    CHAR16 buf[17];
    static const CHAR16 hex[] = L"0123456789ABCDEF";

    for (int i = 15; i >= 0; i--)
    {
        buf[i] = hex[value & 0xF];
        value >>= 4;
    }
    buf[16] = 0;
    print(L"0x");
    print(buf);
}

/**
 * @brief Print a 64-bit value in decimal.
 *
 * @details
 * Converts the number to a UTF-16 string in a small stack buffer and prints it.
 * This is used for diagnostic output such as sizes and counts.
 *
 * @param value Value to print.
 */
static void print_dec(UINT64 value)
{
    CHAR16 buf[21];
    int i = 20;
    buf[i] = 0;

    if (value == 0)
    {
        print(L"0");
        return;
    }

    while (value > 0 && i > 0)
    {
        buf[--i] = L'0' + (value % 10);
        value /= 10;
    }
    print(&buf[i]);
}

/**
 * @brief Print an `EFI_STATUS` value in a readable form.
 *
 * @details
 * Currently prints the numeric value in hexadecimal. This is intended for
 * bring-up diagnostics where a full mapping of status codes is unnecessary.
 *
 * @param status Status code to display.
 */
static void print_status(EFI_STATUS status)
{
    print(L"Status: ");
    print_hex(status);
    println(L"");
}

/**
 * @name Simple memory operations
 * @brief Minimal replacements for libc routines used by the bootloader.
 *
 * @details
 * The bootloader runs in a freestanding environment. These helpers avoid
 * relying on a standard C library and are sufficient for small buffers.
 * @{
 */

/**
 * @brief Fill a buffer with a byte value.
 *
 * @param dst Destination buffer.
 * @param val Byte value to write.
 * @param size Number of bytes to set.
 */
static void memset8(void *dst, UINT8 val, UINTN size)
{
    UINT8 *d = (UINT8 *)dst;
    while (size--)
        *d++ = val;
}

/**
 * @brief Copy bytes from `src` to `dst`.
 *
 * @details
 * The ranges must not overlap. For bootloader use, this is sufficient for
 * copying ELF segment contents into freshly allocated memory.
 *
 * @param dst Destination buffer.
 * @param src Source buffer.
 * @param size Number of bytes to copy.
 */
static void memcpy8(void *dst, const void *src, UINTN size)
{
    UINT8 *d = (UINT8 *)dst;
    const UINT8 *s = (const UINT8 *)src;
    while (size--)
        *d++ = *s++;
}

/** @} */

// =============================================================================
// File System Functions
// =============================================================================

/**
 * @brief Open the EFI System Partition (ESP) volume root.
 *
 * @details
 * The bootloader needs a filesystem handle to load `kernel.elf`. In a full UEFI
 * implementation, the typical flow is:
 * - Obtain the Loaded Image Protocol for this image to get its `DeviceHandle`.
 * - Use that device handle to open the Simple File System Protocol.
 * - Call `OpenVolume` to obtain a root directory handle (`EFI_FILE_PROTOCOL*`).
 *
 * This implementation uses `LocateProtocol` for simplicity and may need to be
 * refined (e.g., using `HandleProtocol`) for strict correctness across
 * firmwares.
 *
 * @param root Output pointer receiving the volume root directory handle.
 * @return `EFI_SUCCESS` on success, or an error `EFI_STATUS` on failure.
 */
static EFI_STATUS open_volume(EFI_FILE_PROTOCOL **root)
{
    EFI_STATUS status;
    EFI_GUID lip_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID sfsp_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;

    // Get loaded image protocol to find our device
    status = gBS->LocateProtocol(&lip_guid, NULL, (void **)&loaded_image);
    if (EFI_ERROR(status))
    {
        println(L"[!] Failed to get Loaded Image Protocol");
        return status;
    }

    // Get file system protocol from our device
    // We need to use HandleProtocol, not LocateProtocol
    // Cast LocateProtocol to handle this - in real code use HandleProtocol
    status = gBS->LocateProtocol(&sfsp_guid, NULL, (void **)&fs);
    if (EFI_ERROR(status))
    {
        println(L"[!] Failed to get Simple File System Protocol");
        return status;
    }

    // Open root volume
    status = fs->OpenVolume(fs, root);
    if (EFI_ERROR(status))
    {
        println(L"[!] Failed to open volume");
        return status;
    }

    return EFI_SUCCESS;
}

/**
 * @brief Load a file from the ESP into a newly allocated buffer.
 *
 * @details
 * Opens `path` relative to the `root` directory, allocates a buffer using
 * `AllocatePool`, and reads up to the initial allocation size.
 *
 * Limitations:
 * - The current implementation allocates a fixed 4 MiB buffer and reads once.
 *   If the file is larger than the buffer, the read will be truncated.
 * - A production bootloader would typically query file size using `GetInfo`
 *   and allocate exactly the required size (or reallocate while reading).
 *
 * @param root Root directory handle.
 * @param path UTF-16 path relative to the root (e.g., `L\"\\\\viperos\\\\kernel.elf\"`).
 * @param buffer Output pointer receiving the allocated buffer on success.
 * @param size Output pointer receiving the number of bytes read.
 * @return `EFI_SUCCESS` on success, or an error `EFI_STATUS` on failure.
 */
static EFI_STATUS load_file(EFI_FILE_PROTOCOL *root, const CHAR16 *path, void **buffer, UINTN *size)
{
    EFI_STATUS status;
    EFI_FILE_PROTOCOL *file;

    print(L"[*] Loading: ");
    println(path);

    // Open file
    status = root->Open(root, &file, (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status))
    {
        print(L"[!] Failed to open file: ");
        println(path);
        return status;
    }

    // Get file size by seeking to end
    // First, read a small amount to determine size via file info
    // For simplicity, we'll read in chunks and reallocate
    // A better approach would be to use GetInfo

    // Allocate initial buffer (4MB should be enough for kernel)
    UINTN buf_size = 4 * 1024 * 1024;
    status = gBS->AllocatePool(EfiLoaderData, buf_size, buffer);
    if (EFI_ERROR(status))
    {
        println(L"[!] Failed to allocate file buffer");
        file->Close(file);
        return status;
    }

    // Read file
    UINTN read_size = buf_size;
    status = file->Read(file, &read_size, *buffer);
    if (EFI_ERROR(status))
    {
        println(L"[!] Failed to read file");
        gBS->FreePool(*buffer);
        file->Close(file);
        return status;
    }

    *size = read_size;
    file->Close(file);

    print(L"    Read ");
    print_dec(read_size);
    println(L" bytes");

    return EFI_SUCCESS;
}

// =============================================================================
// ELF Loading
// =============================================================================

/**
 * @brief Parse an ELF64 image and load PT_LOAD segments into memory.
 *
 * @details
 * The bootloader expects `elf_data` to point to an in-memory copy of an ELF64
 * executable. It validates:
 * - The ELF magic value.
 * - The target machine (`EM_AARCH64`).
 *
 * For each `PT_LOAD` program header:
 * - Compute the number of pages required by `p_memsz`.
 * - Request pages from UEFI. The code first attempts to allocate at the
 *   segment's physical address (`AllocateAddress`), then falls back to an
 *   unconstrained allocation (`AllocateAnyPages`) if necessary.
 * - Zero the full `p_memsz` range.
 * - Copy `p_filesz` bytes from the ELF file at `p_offset` into the allocated
 *   memory.
 *
 * On success, `*entry_point` is set to the ELF entry address (`e_entry`). The
 * kernel is then entered via this entry point after `ExitBootServices`.
 *
 * @param elf_data Pointer to the ELF file contents in memory.
 * @param elf_size Size of the ELF buffer in bytes (currently unused).
 * @param entry_point Output pointer receiving the ELF entry point address.
 * @return `EFI_SUCCESS` on success, or an error `EFI_STATUS` on failure.
 */
static EFI_STATUS load_elf(void *elf_data,
                           UINTN elf_size __attribute__((unused)),
                           UINT64 *entry_point)
{
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)elf_data;

    // Verify ELF magic
    if (*(UINT32 *)ehdr->e_ident != ELF_MAGIC)
    {
        println(L"[!] Invalid ELF magic");
        return EFI_LOAD_ERROR;
    }

    // Verify AArch64
    if (ehdr->e_machine != EM_AARCH64)
    {
        println(L"[!] Not an AArch64 ELF");
        return EFI_LOAD_ERROR;
    }

    print(L"[*] ELF entry point: ");
    print_hex(ehdr->e_entry);
    println(L"");

    print(L"[*] Program headers: ");
    print_dec(ehdr->e_phnum);
    println(L"");

    // Process program headers
    Elf64_Phdr *phdr = (Elf64_Phdr *)((UINT8 *)elf_data + ehdr->e_phoff);

    for (UINT16 i = 0; i < ehdr->e_phnum; i++)
    {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        print(L"    Segment ");
        print_dec(i);
        print(L": vaddr=");
        print_hex(phdr[i].p_vaddr);
        print(L" filesz=");
        print_dec(phdr[i].p_filesz);
        print(L" memsz=");
        print_dec(phdr[i].p_memsz);
        println(L"");

        // Calculate number of pages needed
        UINTN pages = (phdr[i].p_memsz + 4095) / 4096;
        EFI_PHYSICAL_ADDRESS segment_addr = phdr[i].p_paddr;

        // Allocate pages at the specified physical address
        EFI_STATUS status =
            gBS->AllocatePages(AllocateAddress, EfiLoaderData, pages, &segment_addr);

        if (EFI_ERROR(status))
        {
            // Try allocating anywhere if specific address fails
            print(L"    [!] AllocateAddress failed, trying any address: ");
            print_status(status);

            segment_addr = phdr[i].p_paddr;
            status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &segment_addr);

            if (EFI_ERROR(status))
            {
                println(L"    [!] Failed to allocate segment pages");
                return status;
            }
        }

        // Zero the memory first
        memset8((void *)segment_addr, 0, pages * 4096);

        // Copy segment data
        if (phdr[i].p_filesz > 0)
        {
            memcpy8((void *)segment_addr, (UINT8 *)elf_data + phdr[i].p_offset, phdr[i].p_filesz);
        }

        print(L"    Loaded at: ");
        print_hex(segment_addr);
        println(L"");
    }

    *entry_point = ehdr->e_entry;
    return EFI_SUCCESS;
}

// =============================================================================
// Graphics Output Protocol
// =============================================================================

/**
 * @brief Query GOP to obtain framebuffer information for the kernel.
 *
 * @details
 * Locates the UEFI Graphics Output Protocol and fills the provided
 * @ref VBootFramebuffer structure with:
 * - Framebuffer physical base address.
 * - Resolution and stride.
 * - Bits-per-pixel and pixel format encoding used by `vboot`.
 *
 * If GOP is not available, the function sets `fb->base` to 0 and returns
 * success so the kernel can continue without early graphics.
 *
 * @param fb Output framebuffer structure to fill.
 * @return `EFI_SUCCESS` on success, or an error `EFI_STATUS` if a fatal error occurs.
 */
static EFI_STATUS get_framebuffer(VBootFramebuffer *fb)
{
    EFI_STATUS status;
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;

    status = gBS->LocateProtocol(&gop_guid, NULL, (void **)&gop);
    if (EFI_ERROR(status))
    {
        println(L"[!] GOP not available");
        // Not fatal - kernel can work without framebuffer
        fb->base = 0;
        return EFI_SUCCESS;
    }

    fb->base = gop->Mode->FrameBufferBase;
    fb->width = gop->Mode->Info->HorizontalResolution;
    fb->height = gop->Mode->Info->VerticalResolution;
    fb->pitch = gop->Mode->Info->PixelsPerScanLine * 4; // Assume 32bpp
    fb->bpp = 32;

    // Determine pixel format
    switch (gop->Mode->Info->PixelFormat)
    {
        case PixelRedGreenBlueReserved8BitPerColor:
            fb->pixel_format = 1; // RGB
            break;
        case PixelBlueGreenRedReserved8BitPerColor:
            fb->pixel_format = 0; // BGR
            break;
        default:
            fb->pixel_format = 0; // Default to BGR
            break;
    }

    print(L"[*] Framebuffer: ");
    print_dec(fb->width);
    print(L"x");
    print_dec(fb->height);
    print(L" @ ");
    print_hex(fb->base);
    println(L"");

    return EFI_SUCCESS;
}

// =============================================================================
// Memory Map
// =============================================================================

/**
 * @brief Convert a UEFI memory type into a simplified `VBOOT_MEMORY_*` type.
 *
 * @details
 * UEFI memory map entries have many categories. The kernel typically needs a
 * smaller classification:
 * - Usable RAM that can be added to the physical memory manager.
 * - Reserved regions that must not be allocated.
 * - MMIO regions for device registers.
 * - ACPI/firmware regions that may be treated specially.
 *
 * @param efi_type `EFI_MEMORY_DESCRIPTOR::Type` value.
 * @return One of the `VBOOT_MEMORY_*` constants.
 */
static UINT32 convert_memory_type(UINT32 efi_type)
{
    switch (efi_type)
    {
        case EfiConventionalMemory:
        case EfiLoaderCode:
        case EfiLoaderData:
        case EfiBootServicesCode:
        case EfiBootServicesData:
            return VBOOT_MEMORY_USABLE;

        case EfiACPIReclaimMemory:
        case EfiACPIMemoryNVS:
            return VBOOT_MEMORY_ACPI;

        case EfiMemoryMappedIO:
        case EfiMemoryMappedIOPortSpace:
            return VBOOT_MEMORY_MMIO;

        default:
            return VBOOT_MEMORY_RESERVED;
    }
}

/**
 * @brief Retrieve the UEFI memory map and populate the boot info memory regions.
 *
 * @details
 * UEFI requires the OS loader to retrieve the current memory map and pass the
 * associated `MapKey` back to `ExitBootServices`. This function:
 * - Calls `GetMemoryMap` once with a null buffer to learn the required size.
 * - Allocates a buffer with extra slack because the map can change while
 *   allocating.
 * - Calls `GetMemoryMap` again to retrieve the actual map.
 * - Converts a subset of entries into the simplified @ref VBootMemoryRegion
 *   array within @ref VBootInfo.
 *
 * The raw UEFI memory map buffer is returned to the caller so it can be reused
 * when retrying `ExitBootServices`.
 *
 * @param info Boot info structure to populate.
 * @param map_key Output `MapKey` value required by `ExitBootServices`.
 * @param map_out Output pointer receiving the allocated raw map buffer.
 * @param map_size_out Output pointer receiving the raw map buffer size.
 * @return `EFI_SUCCESS` on success, or an error `EFI_STATUS` on failure.
 */
static EFI_STATUS get_memory_map(VBootInfo *info,
                                 UINTN *map_key,
                                 EFI_MEMORY_DESCRIPTOR **map_out,
                                 UINTN *map_size_out)
{
    EFI_STATUS status;
    UINTN map_size = 0;
    UINTN desc_size;
    UINT32 desc_version;
    EFI_MEMORY_DESCRIPTOR *map = NULL;

    // Get required size
    status = gBS->GetMemoryMap(&map_size, NULL, map_key, &desc_size, &desc_version);
    if (status != EFI_BUFFER_TOO_SMALL)
    {
        println(L"[!] GetMemoryMap failed to return size");
        return status;
    }

    // Add extra space for map changes during allocation
    map_size += desc_size * 8;

    // Allocate buffer for memory map
    status = gBS->AllocatePool(EfiLoaderData, map_size, (void **)&map);
    if (EFI_ERROR(status))
    {
        println(L"[!] Failed to allocate memory map buffer");
        return status;
    }

    // Get actual memory map
    status = gBS->GetMemoryMap(&map_size, map, map_key, &desc_size, &desc_version);
    if (EFI_ERROR(status))
    {
        println(L"[!] GetMemoryMap failed");
        gBS->FreePool(map);
        return status;
    }

    // Convert to VBootInfo format
    UINTN num_entries = map_size / desc_size;
    UINT32 region_count = 0;

    print(L"[*] Memory map: ");
    print_dec(num_entries);
    println(L" entries");

    for (UINTN i = 0; i < num_entries && region_count < VBOOT_MAX_MEMORY_REGIONS; i++)
    {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + i * desc_size);

        // Only include usable memory in simplified map
        UINT32 type = convert_memory_type(desc->Type);
        if (type == VBOOT_MEMORY_USABLE || type == VBOOT_MEMORY_ACPI)
        {
            info->memory_regions[region_count].base = desc->PhysicalStart;
            info->memory_regions[region_count].size = desc->NumberOfPages * 4096;
            info->memory_regions[region_count].type = type;
            info->memory_regions[region_count].reserved = 0;
            region_count++;
        }
    }

    info->memory_region_count = region_count;
    *map_out = map;
    *map_size_out = map_size;

    print(L"    Usable regions: ");
    print_dec(region_count);
    println(L"");

    return EFI_SUCCESS;
}

// =============================================================================
// Main Entry Point
// =============================================================================

/**
 * @brief Boot information block passed to the kernel.
 *
 * @details
 * This structure must remain valid after calling `ExitBootServices`, so it is
 * defined as a static object with a stable address. It is aligned to 4 KiB to
 * make it easy to map and to keep it distinct from other allocations.
 */
static VBootInfo boot_info __attribute__((aligned(4096)));

/**
 * @brief Kernel entry point function pointer type.
 *
 * @details
 * The kernel entry is called after `ExitBootServices`. The AArch64 calling
 * convention passes the first argument in x0, so `vboot` passes the address of
 * @ref boot_info as the sole parameter.
 */
typedef void (*KernelEntry)(VBootInfo *);

/**
 * @brief UEFI application entry point for `vboot`.
 *
 * @details
 * The firmware calls this function to start the bootloader. The implementation
 * performs a bring-up oriented boot flow:
 * 1. Initialize global pointers (`gST`, `gBS`, `gImageHandle`).
 * 2. Open the boot volume (ESP) and read the kernel ELF file into memory.
 * 3. Parse the ELF and load all `PT_LOAD` segments into allocated pages.
 * 4. Query GOP framebuffer information.
 * 5. Retrieve the UEFI memory map and populate `boot_info`.
 * 6. Call `ExitBootServices` (retrying if the map changes).
 * 7. Disable interrupts and jump to the kernel entry point, passing `boot_info`.
 *
 * After `ExitBootServices` returns successfully, no further UEFI service calls
 * are permitted. The bootloader must not attempt console output, allocations,
 * or protocol operations beyond that point.
 *
 * On failure, the bootloader prints an error message (when still possible) and
 * halts the CPU.
 *
 * @param ImageHandle Handle identifying the loaded bootloader image.
 * @param SystemTable Pointer to the UEFI system table.
 * @return `EFI_LOAD_ERROR` if boot fails (unreachable on success).
 */
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS status;

    // Store global pointers
    gST = SystemTable;
    gBS = SystemTable->BootServices;
    gImageHandle = ImageHandle;

    // Clear screen
    if (gST->ConOut && gST->ConOut->ClearScreen)
    {
        gST->ConOut->ClearScreen(gST->ConOut);
    }

    // Print banner
    println(L"");
    println(L"========================================");
    println(L"  VBoot - ViperOS Bootloader v0.2");
    println(L"========================================");
    println(L"");

    // Print system info
    print(L"Firmware Vendor: ");
    if (gST->FirmwareVendor)
    {
        println(gST->FirmwareVendor);
    }
    else
    {
        println(L"(unknown)");
    }
    println(L"");

    // Initialize boot info
    memset8(&boot_info, 0, sizeof(boot_info));
    boot_info.magic = VBOOT_MAGIC;

    // Step 1: Open ESP volume
    println(L"[*] Opening ESP volume...");
    EFI_FILE_PROTOCOL *root;
    status = open_volume(&root);
    if (EFI_ERROR(status))
    {
        println(L"[!] Failed to open ESP volume");
        goto halt;
    }
    println(L"    Volume opened successfully");

    // Step 2: Load kernel
    println(L"");
    println(L"[*] Loading kernel...");
    void *kernel_data;
    UINTN kernel_size;
    status = load_file(root, L"\\viperos\\kernel.elf", &kernel_data, &kernel_size);
    if (EFI_ERROR(status))
    {
        // Try alternate paths
        status = load_file(root, L"\\EFI\\BOOT\\kernel.elf", &kernel_data, &kernel_size);
        if (EFI_ERROR(status))
        {
            status = load_file(root, L"\\kernel.elf", &kernel_data, &kernel_size);
            if (EFI_ERROR(status))
            {
                println(L"[!] Failed to load kernel.elf");
                goto halt;
            }
        }
    }

    // Close volume (done with file system)
    root->Close(root);

    // Step 3: Parse and load ELF
    println(L"");
    println(L"[*] Parsing ELF...");
    UINT64 kernel_entry;
    status = load_elf(kernel_data, kernel_size, &kernel_entry);
    if (EFI_ERROR(status))
    {
        println(L"[!] Failed to load ELF");
        goto halt;
    }

    // Free kernel file buffer (data is now copied to target addresses)
    gBS->FreePool(kernel_data);

    // Record kernel info
    boot_info.kernel_phys_base = 0x40000000; // Standard QEMU virt base
    boot_info.kernel_virt_base = 0x40000000; // Identity mapped for now
    boot_info.kernel_size = kernel_size;

    // Step 4: Get framebuffer
    println(L"");
    println(L"[*] Getting framebuffer...");
    status = get_framebuffer(&boot_info.framebuffer);
    // Not fatal if this fails

    // Step 5: Get memory map (must be done last before ExitBootServices)
    println(L"");
    println(L"[*] Getting memory map...");
    UINTN map_key = 0;
    EFI_MEMORY_DESCRIPTOR *memory_map = NULL;
    UINTN memory_map_size = 0;
    status = get_memory_map(&boot_info, &map_key, &memory_map, &memory_map_size);
    if (EFI_ERROR(status))
    {
        println(L"[!] Failed to get memory map");
        goto halt;
    }

    // Step 6: Exit boot services
    println(L"");
    println(L"[*] Exiting boot services...");

    // We may need to retry GetMemoryMap + ExitBootServices
    // because the map can change between calls
    for (int retry = 0; retry < 3; retry++)
    {
        status = gBS->ExitBootServices(gImageHandle, map_key);
        if (!EFI_ERROR(status))
        {
            break;
        }

        // Memory map changed, get it again
        UINTN new_size = memory_map_size + 4096;
        UINTN desc_size;
        UINT32 desc_version;
        status = gBS->GetMemoryMap(&new_size, memory_map, &map_key, &desc_size, &desc_version);
        if (EFI_ERROR(status))
        {
            break;
        }
        memory_map_size = new_size;
    }

    if (EFI_ERROR(status))
    {
        // Can't print anymore after attempting ExitBootServices
        goto halt;
    }

    // =========================================================================
    // Boot services are now gone - no more UEFI calls!
    // =========================================================================

    // Step 7: Jump to kernel
    // Kernel entry point expects VBootInfo* in x0
    KernelEntry kernel = (KernelEntry)kernel_entry;

    // Disable interrupts before jumping
    __asm__ volatile("msr daifset, #0xf");

    // Jump to kernel
    kernel(&boot_info);

    // Should never return
halt:
    println(L"");
    println(L"[!] Boot failed - halting");
    for (;;)
    {
        __asm__ volatile("wfi");
    }

    return EFI_LOAD_ERROR;
}
