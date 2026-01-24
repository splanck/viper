/**
 * @file fdt.cpp
 * @brief Flattened Device Tree (FDT) parser implementation.
 *
 * @details
 * Minimal FDT parser for extracting memory regions. The FDT format uses
 * big-endian encoding for all multi-byte values.
 */

#include "fdt.hpp"
#include "../console/serial.hpp"

namespace fdt
{

namespace
{

// FDT structure block tokens
constexpr u32 FDT_BEGIN_NODE = 0x00000001;
constexpr u32 FDT_END_NODE = 0x00000002;
constexpr u32 FDT_PROP = 0x00000003;
constexpr u32 FDT_NOP = 0x00000004;
constexpr u32 FDT_END = 0x00000009;

/**
 * @brief FDT header structure (all fields big-endian).
 */
struct FdtHeader
{
    u32 magic;
    u32 totalsize;
    u32 off_dt_struct;
    u32 off_dt_strings;
    u32 off_mem_rsvmap;
    u32 version;
    u32 last_comp_version;
    u32 boot_cpuid_phys;
    u32 size_dt_strings;
    u32 size_dt_struct;
};

/**
 * @brief Convert big-endian u32 to host byte order.
 */
inline u32 be32_to_cpu(u32 val)
{
    return ((val & 0xFF000000) >> 24) | ((val & 0x00FF0000) >> 8) | ((val & 0x0000FF00) << 8) |
           ((val & 0x000000FF) << 24);
}

/**
 * @brief Convert big-endian u64 to host byte order.
 */
inline u64 be64_to_cpu(u64 val)
{
    u32 hi = be32_to_cpu(static_cast<u32>(val >> 32));
    u32 lo = be32_to_cpu(static_cast<u32>(val & 0xFFFFFFFF));
    return (static_cast<u64>(lo) << 32) | hi;
}

/**
 * @brief Read a big-endian u32 from memory.
 */
inline u32 read_be32(const void *ptr)
{
    return be32_to_cpu(*static_cast<const u32 *>(ptr));
}

/**
 * @brief Read a big-endian u64 from memory.
 */
inline u64 read_be64(const void *ptr)
{
    return be64_to_cpu(*static_cast<const u64 *>(ptr));
}

/**
 * @brief Align offset up to 4-byte boundary.
 */
inline u32 align4(u32 offset)
{
    return (offset + 3) & ~3u;
}

/**
 * @brief String comparison.
 */
bool str_eq(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (*a++ != *b++)
            return false;
    }
    return *a == *b;
}

/**
 * @brief String length.
 */
u32 str_len(const char *s)
{
    u32 len = 0;
    while (*s++)
        len++;
    return len;
}

/**
 * @brief Check if path component matches.
 */
bool path_match(const char *node_name, const char *component)
{
    // Node names can have @address suffix which we ignore
    while (*component)
    {
        if (*node_name == '\0' || *node_name == '@')
            return *component == '\0';
        if (*node_name++ != *component++)
            return false;
    }
    // Component matched, node can have @suffix or be exact match
    return *node_name == '\0' || *node_name == '@';
}

/**
 * @brief Get header from FDT.
 */
const FdtHeader *get_header(const void *fdt_base)
{
    return static_cast<const FdtHeader *>(fdt_base);
}

/**
 * @brief Get strings block pointer.
 */
const char *get_strings(const void *fdt_base)
{
    const auto *hdr = get_header(fdt_base);
    return static_cast<const char *>(fdt_base) + be32_to_cpu(hdr->off_dt_strings);
}

/**
 * @brief Get structure block pointer.
 */
const u8 *get_struct(const void *fdt_base)
{
    const auto *hdr = get_header(fdt_base);
    return static_cast<const u8 *>(fdt_base) + be32_to_cpu(hdr->off_dt_struct);
}

/**
 * @brief Parse state for walking the FDT.
 */
struct ParseState
{
    const u8 *struct_base;
    const char *strings;
    u32 offset;
    u32 struct_size;
    int depth;
};

/**
 * @brief Read next token from structure block.
 */
u32 next_token(ParseState &state)
{
    if (state.offset >= state.struct_size)
        return FDT_END;

    u32 token = read_be32(state.struct_base + state.offset);
    state.offset += 4;

    // Skip NOP tokens
    while (token == FDT_NOP && state.offset < state.struct_size)
    {
        token = read_be32(state.struct_base + state.offset);
        state.offset += 4;
    }

    return token;
}

/**
 * @brief Get node name after FDT_BEGIN_NODE.
 */
const char *get_node_name(ParseState &state)
{
    const char *name = reinterpret_cast<const char *>(state.struct_base + state.offset);
    u32 len = str_len(name);
    state.offset = align4(state.offset + len + 1);
    return name;
}

/**
 * @brief Get property info after FDT_PROP.
 */
void get_property(ParseState &state, const char **out_name, const void **out_data, u32 *out_len)
{
    u32 len = read_be32(state.struct_base + state.offset);
    u32 nameoff = read_be32(state.struct_base + state.offset + 4);
    state.offset += 8;

    *out_name = state.strings + nameoff;
    *out_data = state.struct_base + state.offset;
    *out_len = len;

    state.offset = align4(state.offset + len);
}

} // namespace

/** @copydoc fdt::is_valid */
bool is_valid(const void *fdt_base)
{
    if (!fdt_base)
        return false;

    const auto *hdr = get_header(fdt_base);
    u32 magic = be32_to_cpu(hdr->magic);

    return magic == FDT_MAGIC;
}

/** @copydoc fdt::get_size */
u32 get_size(const void *fdt_base)
{
    if (!is_valid(fdt_base))
        return 0;

    const auto *hdr = get_header(fdt_base);
    return be32_to_cpu(hdr->totalsize);
}

/** @copydoc fdt::find_property */
bool find_property(
    const void *fdt_base, const char *path, const char *prop, const void **out_data, u32 *out_len)
{
    if (!is_valid(fdt_base) || !path || !prop)
        return false;

    const auto *hdr = get_header(fdt_base);

    ParseState state;
    state.struct_base = get_struct(fdt_base);
    state.strings = get_strings(fdt_base);
    state.offset = 0;
    state.struct_size = be32_to_cpu(hdr->size_dt_struct);
    state.depth = 0;

    // Skip leading slash
    if (*path == '/')
        path++;

    // Parse path components
    const char *path_ptr = path;
    int target_depth = 0;
    bool in_target = false;

    // Count target depth
    for (const char *p = path; *p; p++)
    {
        if (*p == '/')
            target_depth++;
    }
    if (*path)
        target_depth++; // Non-empty path has at least one component

    while (true)
    {
        u32 token = next_token(state);

        switch (token)
        {
            case FDT_BEGIN_NODE:
            {
                const char *name = get_node_name(state);
                state.depth++;

                // Check if this node is on our path
                if (state.depth <= target_depth)
                {
                    // Get current path component
                    const char *component = path_ptr;
                    while (*path_ptr && *path_ptr != '/')
                        path_ptr++;

                    if (path_match(name, component))
                    {
                        if (*path_ptr == '/')
                            path_ptr++;
                        if (state.depth == target_depth)
                        {
                            in_target = true;
                        }
                    }
                }
                break;
            }

            case FDT_END_NODE:
                if (in_target && state.depth == target_depth)
                {
                    // Left target node without finding property
                    return false;
                }
                state.depth--;
                break;

            case FDT_PROP:
            {
                const char *pname;
                const void *pdata;
                u32 plen;
                get_property(state, &pname, &pdata, &plen);

                if (in_target && str_eq(pname, prop))
                {
                    *out_data = pdata;
                    *out_len = plen;
                    return true;
                }
                break;
            }

            case FDT_END:
                return false;

            default:
                // Unknown token, bail
                return false;
        }
    }
}

/** @copydoc fdt::get_string_prop */
const char *get_string_prop(const void *fdt_base, const char *path, const char *prop)
{
    const void *data;
    u32 len;
    if (find_property(fdt_base, path, prop, &data, &len))
    {
        return static_cast<const char *>(data);
    }
    return nullptr;
}

/** @copydoc fdt::get_u32_prop */
u32 get_u32_prop(const void *fdt_base, const char *path, const char *prop, u32 default_val)
{
    const void *data;
    u32 len;
    if (find_property(fdt_base, path, prop, &data, &len) && len >= 4)
    {
        return read_be32(data);
    }
    return default_val;
}

// =============================================================================
// Memory Parsing Helpers
// =============================================================================

/**
 * @brief Parse memory reservation block.
 *
 * @param fdt_base FDT base pointer.
 * @param out Output memory layout structure.
 */
static void parse_reserved_regions(const void *fdt_base, MemoryLayout *out)
{
    const auto *hdr = get_header(fdt_base);
    const u8 *rsvmap = static_cast<const u8 *>(fdt_base) + be32_to_cpu(hdr->off_mem_rsvmap);

    while (out->reserved_count < MAX_RESERVED_REGIONS)
    {
        u64 addr = read_be64(rsvmap);
        u64 size = read_be64(rsvmap + 8);
        rsvmap += 16;

        if (addr == 0 && size == 0)
            break;

        out->reserved[out->reserved_count].base = addr;
        out->reserved[out->reserved_count].size = size;
        out->reserved_count++;
    }
}

/**
 * @brief Parse a memory "reg" property into memory regions.
 *
 * @param pdata Property data.
 * @param plen Property length.
 * @param address_cells Number of address cells.
 * @param size_cells Number of size cells.
 * @param out Output memory layout structure.
 */
static void parse_reg_property(
    const void *pdata, u32 plen, u32 address_cells, u32 size_cells, MemoryLayout *out)
{
    u32 cell_size = (address_cells + size_cells) * 4;
    u32 entries = plen / cell_size;

    const u8 *ptr = static_cast<const u8 *>(pdata);
    for (u32 i = 0; i < entries && out->region_count < MAX_MEMORY_REGIONS; i++)
    {
        u64 base = 0;
        u64 size = 0;

        // Read address (1 or 2 cells)
        if (address_cells == 2)
        {
            base = read_be64(ptr);
            ptr += 8;
        }
        else
        {
            base = read_be32(ptr);
            ptr += 4;
        }

        // Read size (1 or 2 cells)
        if (size_cells == 2)
        {
            size = read_be64(ptr);
            ptr += 8;
        }
        else
        {
            size = read_be32(ptr);
            ptr += 4;
        }

        out->regions[out->region_count].base = base;
        out->regions[out->region_count].size = size;
        out->region_count++;
    }
}

/**
 * @brief Parse initrd properties from /chosen node.
 *
 * @param pname Property name.
 * @param pdata Property data.
 * @param plen Property length.
 * @param out Output memory layout structure.
 */
static void parse_initrd_property(const char *pname, const void *pdata, u32 plen, MemoryLayout *out)
{
    if (str_eq(pname, "linux,initrd-start") && plen >= 4)
    {
        out->initrd_start = (plen >= 8) ? read_be64(pdata) : read_be32(pdata);
    }
    else if (str_eq(pname, "linux,initrd-end") && plen >= 4)
    {
        out->initrd_end = (plen >= 8) ? read_be64(pdata) : read_be32(pdata);
    }
}

// =============================================================================
// Memory Parsing Main Entry Point
// =============================================================================

/** @copydoc fdt::parse_memory */
bool parse_memory(const void *fdt_base, MemoryLayout *out)
{
    if (!is_valid(fdt_base) || !out)
        return false;

    // Initialize output
    out->region_count = 0;
    out->reserved_count = 0;
    out->initrd_start = 0;
    out->initrd_end = 0;

    // Parse memory reservation block
    parse_reserved_regions(fdt_base, out);

    // Walk structure to find /memory nodes
    const auto *hdr = get_header(fdt_base);
    ParseState state;
    state.struct_base = get_struct(fdt_base);
    state.strings = get_strings(fdt_base);
    state.offset = 0;
    state.struct_size = be32_to_cpu(hdr->size_dt_struct);
    state.depth = 0;

    bool in_memory = false;
    bool in_chosen = false;
    u32 address_cells = 2;
    u32 size_cells = 1;

    while (true)
    {
        u32 token = next_token(state);

        switch (token)
        {
            case FDT_BEGIN_NODE:
            {
                const char *name = get_node_name(state);
                state.depth++;
                if (state.depth == 1)
                {
                    in_memory = path_match(name, "memory");
                    in_chosen = path_match(name, "chosen");
                }
                break;
            }

            case FDT_END_NODE:
                if (state.depth == 1)
                {
                    in_memory = false;
                    in_chosen = false;
                }
                state.depth--;
                break;

            case FDT_PROP:
            {
                const char *pname;
                const void *pdata;
                u32 plen;
                get_property(state, &pname, &pdata, &plen);

                // Root node cell properties
                if (state.depth == 1)
                {
                    if (str_eq(pname, "#address-cells") && plen >= 4)
                        address_cells = read_be32(pdata);
                    else if (str_eq(pname, "#size-cells") && plen >= 4)
                        size_cells = read_be32(pdata);
                }

                // Memory node reg property
                if (in_memory && str_eq(pname, "reg"))
                {
                    parse_reg_property(pdata, plen, address_cells, size_cells, out);
                }

                // Chosen node initrd properties
                if (in_chosen)
                {
                    parse_initrd_property(pname, pdata, plen, out);
                }
                break;
            }

            case FDT_END:
                goto done;

            default:
                break;
        }
    }

done:
    serial::puts("[fdt] Parsed ");
    serial::put_dec(out->region_count);
    serial::puts(" memory region(s), ");
    serial::put_dec(out->reserved_count);
    serial::puts(" reserved region(s)\n");

    return out->region_count > 0;
}

/** @copydoc fdt::dump */
void dump(const void *fdt_base)
{
    if (!is_valid(fdt_base))
    {
        serial::puts("[fdt] Invalid FDT\n");
        return;
    }

    const auto *hdr = get_header(fdt_base);

    serial::puts("[fdt] FDT dump:\n");
    serial::puts("  magic: 0x");
    serial::put_hex(be32_to_cpu(hdr->magic));
    serial::puts("\n  totalsize: ");
    serial::put_dec(be32_to_cpu(hdr->totalsize));
    serial::puts("\n  version: ");
    serial::put_dec(be32_to_cpu(hdr->version));
    serial::puts("\n  struct offset: ");
    serial::put_dec(be32_to_cpu(hdr->off_dt_struct));
    serial::puts("\n  strings offset: ");
    serial::put_dec(be32_to_cpu(hdr->off_dt_strings));
    serial::puts("\n");
}

} // namespace fdt
