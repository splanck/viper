/**
 * @file bootinfo.cpp
 * @brief Boot information parser and abstraction layer.
 *
 * @details
 * This module parses the boot information passed to the kernel and provides
 * a unified interface regardless of boot method (UEFI VBoot vs QEMU direct).
 *
 * For VBoot (UEFI):
 * - Validates VBootInfo magic number
 * - Extracts GOP framebuffer info
 * - Extracts UEFI memory map
 *
 * For QEMU direct boot (-kernel):
 * - Treats x0 as DTB pointer
 * - Uses hardcoded QEMU virt machine defaults
 * - Framebuffer will be configured via ramfb later
 */

#include "bootinfo.hpp"
#include "../console/serial.hpp"
#include "../dtb/fdt.hpp"
#include "../include/vboot.hpp"

// Linker-provided symbols
extern "C"
{
    extern u8 __kernel_start[];
    extern u8 __kernel_end[];
}

namespace boot
{

namespace
{
/**
 * @brief Parsed boot information snapshot.
 *
 * @details
 * Populated exactly once by @ref boot::init and then treated as read-only.
 * A separate @ref g_initialized flag tracks whether initialization has
 * completed.
 */
Info g_boot_info = {};

/**
 * @brief Whether @ref boot::init has been called.
 *
 * @details
 * The bootinfo API is expected to be initialized very early. This flag is
 * currently used as a sanity marker for debugging.
 */
bool g_initialized = false;

/**
 * @brief Parse a `VBootInfo` structure provided by the UEFI bootloader.
 *
 * @details
 * This helper assumes the caller has already validated the input pointer
 * using @ref viper::vboot::is_valid. It extracts:
 * - Kernel physical base and image size as reported by the bootloader.
 * - GOP framebuffer information (when available).
 * - A simplified memory map (capped to @ref MAX_MEMORY_REGIONS entries).
 *
 * The DTB pointer is set to null because UEFI boot does not provide a DTB
 * by default.
 *
 * @param vboot Pointer to a validated boot info structure.
 */
void parse_vboot(const viper::vboot::Info *vboot)
{
    g_boot_info.method = Method::VBoot;
    g_boot_info.dtb = nullptr;

    // Copy kernel info
    g_boot_info.kernel_phys_base = vboot->kernel_phys_base;
    g_boot_info.kernel_size = vboot->kernel_size;

    // Copy framebuffer info from GOP
    if (vboot->framebuffer.base != 0)
    {
        g_boot_info.framebuffer.base = vboot->framebuffer.base;
        g_boot_info.framebuffer.width = vboot->framebuffer.width;
        g_boot_info.framebuffer.height = vboot->framebuffer.height;
        g_boot_info.framebuffer.pitch = vboot->framebuffer.pitch;
        g_boot_info.framebuffer.bpp = vboot->framebuffer.bpp;
        g_boot_info.framebuffer.format =
            vboot->framebuffer.pixel_format == 0 ? PixelFormat::BGR : PixelFormat::RGB;
    }

    // Copy memory regions from UEFI memory map
    g_boot_info.memory_region_count = vboot->memory_region_count;
    if (g_boot_info.memory_region_count > MAX_MEMORY_REGIONS)
    {
        g_boot_info.memory_region_count = MAX_MEMORY_REGIONS;
    }

    for (u32 i = 0; i < g_boot_info.memory_region_count; i++)
    {
        const auto &src = vboot->memory_regions[i];
        auto &dst = g_boot_info.memory_regions[i];
        dst.base = src.base;
        dst.size = src.size;
        dst.type = static_cast<MemoryType>(src.type);
    }
}

/**
 * @brief Set up boot info from FDT or use conservative defaults.
 *
 * @details
 * When booted via QEMU `-kernel`, the kernel receives a DTB pointer in x0.
 * This function attempts to parse the FDT to extract memory regions. If
 * parsing fails, it falls back to hardcoded defaults for the QEMU `virt`
 * machine.
 *
 * The framebuffer is left empty because GOP is not available; a RAM
 * framebuffer (ramfb) may be configured later by a device driver.
 *
 * Kernel physical base and size are derived from linker-provided symbols.
 *
 * @param dtb Pointer to the device tree blob passed by the boot environment.
 */
void setup_qemu_defaults(void *dtb)
{
    g_boot_info.method = Method::QemuDirect;
    g_boot_info.dtb = dtb;

    // No GOP framebuffer - will use ramfb
    g_boot_info.framebuffer = {};

    // Kernel info from linker symbols
    g_boot_info.kernel_phys_base = reinterpret_cast<u64>(__kernel_start);
    g_boot_info.kernel_size =
        reinterpret_cast<u64>(__kernel_end) - reinterpret_cast<u64>(__kernel_start);

    // Try to parse memory layout from FDT
    fdt::MemoryLayout fdt_layout;
    if (fdt::is_valid(dtb) && fdt::parse_memory(dtb, &fdt_layout))
    {
        serial::puts("[bootinfo] Using FDT memory layout\n");

        // Copy memory regions from FDT
        g_boot_info.memory_region_count = 0;
        for (u32 i = 0; i < fdt_layout.region_count && i < MAX_MEMORY_REGIONS; i++)
        {
            g_boot_info.memory_regions[i].base = fdt_layout.regions[i].base;
            g_boot_info.memory_regions[i].size = fdt_layout.regions[i].size;
            g_boot_info.memory_regions[i].type = MemoryType::Usable;
            g_boot_info.memory_region_count++;
        }

        // Mark reserved regions
        for (u32 i = 0; i < fdt_layout.reserved_count; i++)
        {
            // Add reserved regions as separate entries if we have room
            if (g_boot_info.memory_region_count < MAX_MEMORY_REGIONS)
            {
                u32 idx = g_boot_info.memory_region_count++;
                g_boot_info.memory_regions[idx].base = fdt_layout.reserved[i].base;
                g_boot_info.memory_regions[idx].size = fdt_layout.reserved[i].size;
                g_boot_info.memory_regions[idx].type = MemoryType::Reserved;
            }
        }
    }
    else
    {
        // Fall back to QEMU virt machine defaults
        serial::puts("[bootinfo] FDT parse failed, using QEMU defaults\n");

        constexpr u64 QEMU_VIRT_RAM_BASE = 0x40000000;
        constexpr u64 QEMU_VIRT_RAM_SIZE = 128 * 1024 * 1024; // 128 MB

        g_boot_info.memory_region_count = 1;
        g_boot_info.memory_regions[0].base = QEMU_VIRT_RAM_BASE;
        g_boot_info.memory_regions[0].size = QEMU_VIRT_RAM_SIZE;
        g_boot_info.memory_regions[0].type = MemoryType::Usable;
    }
}
} // namespace

/** @copydoc boot::init */
void init(void *boot_info)
{
    // Clear boot info
    g_boot_info = {};

    // Check if this is a valid VBootInfo structure
    if (viper::vboot::is_valid(boot_info))
    {
        const auto *vboot = static_cast<const viper::vboot::Info *>(boot_info);
        parse_vboot(vboot);
    }
    else
    {
        // Treat as DTB pointer, use QEMU defaults
        setup_qemu_defaults(boot_info);
    }

    g_initialized = true;
}

/** @copydoc boot::get_info */
const Info &get_info()
{
    return g_boot_info;
}

/** @copydoc boot::get_method */
Method get_method()
{
    return g_boot_info.method;
}

/** @copydoc boot::get_framebuffer */
const Framebuffer &get_framebuffer()
{
    return g_boot_info.framebuffer;
}

/** @copydoc boot::has_uefi_framebuffer */
bool has_uefi_framebuffer()
{
    return g_boot_info.method == Method::VBoot && g_boot_info.framebuffer.is_valid();
}

/** @copydoc boot::get_memory_region_count */
u32 get_memory_region_count()
{
    return g_boot_info.memory_region_count;
}

/** @copydoc boot::get_memory_region */
const MemoryRegion *get_memory_region(u32 index)
{
    if (index >= g_boot_info.memory_region_count)
    {
        return nullptr;
    }
    return &g_boot_info.memory_regions[index];
}

/** @copydoc boot::get_total_usable_memory */
u64 get_total_usable_memory()
{
    u64 total = 0;
    for (u32 i = 0; i < g_boot_info.memory_region_count; i++)
    {
        if (g_boot_info.memory_regions[i].type == MemoryType::Usable)
        {
            total += g_boot_info.memory_regions[i].size;
        }
    }
    return total;
}

/** @copydoc boot::get_ram_region */
bool get_ram_region(u64 &out_base, u64 &out_size)
{
    // Find the largest usable memory region
    u64 largest_base = 0;
    u64 largest_size = 0;

    for (u32 i = 0; i < g_boot_info.memory_region_count; i++)
    {
        const auto &region = g_boot_info.memory_regions[i];
        if (region.type == MemoryType::Usable && region.size > largest_size)
        {
            largest_base = region.base;
            largest_size = region.size;
        }
    }

    if (largest_size > 0)
    {
        out_base = largest_base;
        out_size = largest_size;
        return true;
    }

    return false;
}

/** @copydoc boot::dump */
void dump()
{
    serial::puts("[bootinfo] Boot method: ");
    switch (g_boot_info.method)
    {
        case Method::Unknown:
            serial::puts("Unknown\n");
            break;
        case Method::QemuDirect:
            serial::puts("QEMU direct (-kernel)\n");
            serial::puts("[bootinfo] DTB pointer: ");
            serial::put_hex(reinterpret_cast<u64>(g_boot_info.dtb));
            serial::puts("\n");
            break;
        case Method::VBoot:
            serial::puts("VBoot (UEFI)\n");
            break;
    }

    serial::puts("[bootinfo] Kernel phys base: ");
    serial::put_hex(g_boot_info.kernel_phys_base);
    serial::puts("\n");
    serial::puts("[bootinfo] Kernel size: ");
    serial::put_dec(static_cast<i64>(g_boot_info.kernel_size));
    serial::puts(" bytes\n");

    if (g_boot_info.framebuffer.is_valid())
    {
        serial::puts("[bootinfo] Framebuffer: ");
        serial::put_dec(g_boot_info.framebuffer.width);
        serial::puts("x");
        serial::put_dec(g_boot_info.framebuffer.height);
        serial::puts("x");
        serial::put_dec(g_boot_info.framebuffer.bpp);
        serial::puts(" @ ");
        serial::put_hex(g_boot_info.framebuffer.base);
        serial::puts(" (");
        serial::puts(g_boot_info.framebuffer.format == PixelFormat::BGR ? "BGR" : "RGB");
        serial::puts(")\n");
    }
    else
    {
        serial::puts("[bootinfo] Framebuffer: none (will use ramfb)\n");
    }

    serial::puts("[bootinfo] Memory regions: ");
    serial::put_dec(g_boot_info.memory_region_count);
    serial::puts("\n");

    for (u32 i = 0; i < g_boot_info.memory_region_count; i++)
    {
        const auto &region = g_boot_info.memory_regions[i];
        serial::puts("  [");
        serial::put_dec(i);
        serial::puts("] ");
        serial::put_hex(region.base);
        serial::puts(" - ");
        serial::put_hex(region.base + region.size);
        serial::puts(" (");
        serial::put_dec(static_cast<i64>(region.size / (1024 * 1024)));
        serial::puts(" MB) ");

        switch (region.type)
        {
            case MemoryType::Usable:
                serial::puts("usable");
                break;
            case MemoryType::Reserved:
                serial::puts("reserved");
                break;
            case MemoryType::Acpi:
                serial::puts("ACPI");
                break;
            case MemoryType::Mmio:
                serial::puts("MMIO");
                break;
            default:
                serial::puts("unknown");
                break;
        }
        serial::puts("\n");
    }

    serial::puts("[bootinfo] Total usable memory: ");
    serial::put_dec(static_cast<i64>(get_total_usable_memory() / (1024 * 1024)));
    serial::puts(" MB\n");
}

} // namespace boot
