/**
 * @file fdt.hpp
 * @brief Flattened Device Tree (FDT) parser interface.
 *
 * @details
 * Provides minimal FDT parsing capability to extract memory regions from
 * the device tree blob passed by QEMU or other bootloaders. This parser
 * focuses on the /memory node to determine RAM layout.
 *
 * The FDT format is defined by the Devicetree Specification:
 * https://www.devicetree.org/specifications/
 */
#pragma once

#include "../include/types.hpp"

namespace fdt {

/** @brief FDT magic number (big-endian: 0xD00DFEED). */
constexpr u32 FDT_MAGIC = 0xD00DFEED;

/** @brief Maximum memory regions to extract. */
constexpr u32 MAX_MEMORY_REGIONS = 8;

/** @brief Maximum reserved memory regions. */
constexpr u32 MAX_RESERVED_REGIONS = 16;

/**
 * @brief A memory region extracted from the FDT.
 */
struct MemoryRegion {
    u64 base; ///< Physical base address
    u64 size; ///< Size in bytes
};

/**
 * @brief Parsed memory layout from FDT.
 */
struct MemoryLayout {
    MemoryRegion regions[MAX_MEMORY_REGIONS];
    u32 region_count;

    MemoryRegion reserved[MAX_RESERVED_REGIONS];
    u32 reserved_count;

    u64 initrd_start; ///< Initial ramdisk start (if present)
    u64 initrd_end;   ///< Initial ramdisk end (if present)
};

/**
 * @brief Validate an FDT header.
 *
 * @param fdt_base Pointer to potential FDT blob.
 * @return true if the pointer appears to be a valid FDT.
 */
bool is_valid(const void *fdt_base);

/**
 * @brief Get the total size of the FDT blob.
 *
 * @param fdt_base Pointer to validated FDT blob.
 * @return Total size in bytes, or 0 if invalid.
 */
u32 get_size(const void *fdt_base);

/**
 * @brief Parse memory layout from FDT.
 *
 * @details
 * Extracts /memory node reg properties and /reserved-memory entries.
 * Also checks /chosen for initrd-start/initrd-end.
 *
 * @param fdt_base Pointer to validated FDT blob.
 * @param out Output memory layout structure.
 * @return true on success, false on parse error.
 */
bool parse_memory(const void *fdt_base, MemoryLayout *out);

/**
 * @brief Find a property value by path.
 *
 * @param fdt_base Pointer to validated FDT blob.
 * @param path Node path (e.g., "/memory").
 * @param prop Property name (e.g., "reg").
 * @param out_data Output pointer to property data.
 * @param out_len Output property length in bytes.
 * @return true if found, false otherwise.
 */
bool find_property(
    const void *fdt_base, const char *path, const char *prop, const void **out_data, u32 *out_len);

/**
 * @brief Get a string property value.
 *
 * @param fdt_base Pointer to validated FDT blob.
 * @param path Node path.
 * @param prop Property name.
 * @return Property string value, or nullptr if not found.
 */
const char *get_string_prop(const void *fdt_base, const char *path, const char *prop);

/**
 * @brief Get a 32-bit cell property value.
 *
 * @param fdt_base Pointer to validated FDT blob.
 * @param path Node path.
 * @param prop Property name.
 * @param default_val Value to return if property not found.
 * @return Property value or default.
 */
u32 get_u32_prop(const void *fdt_base, const char *path, const char *prop, u32 default_val);

/**
 * @brief Debug: dump FDT structure to serial console.
 *
 * @param fdt_base Pointer to validated FDT blob.
 */
void dump(const void *fdt_base);

} // namespace fdt
