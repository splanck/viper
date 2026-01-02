#include "fwcfg.hpp"
#include "../console/serial.hpp"
#include "../include/constants.hpp"

// Suppress warnings for DMA control constants that document the full interface
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-const-variable"

/**
 * @file fwcfg.cpp
 * @brief QEMU fw_cfg MMIO and DMA access implementation.
 *
 * @details
 * Implements the fw_cfg interface for the QEMU `virt` machine using memory
 * mapped registers. Provides selector-based byte access and a DMA write helper.
 *
 * Endianness:
 * - Many fw_cfg structures and registers use big-endian encoding.
 * - Helper functions convert between CPU endianness and fw_cfg endianness.
 */
namespace fwcfg
{

// QEMU virt machine fw_cfg MMIO addresses (from constants.hpp)
constexpr uintptr FWCFG_BASE = kc::hw::FWCFG_BASE;

// Register offsets (MMIO interface)
constexpr uintptr FWCFG_DATA = 0x00;     // Data register (read/write)
constexpr uintptr FWCFG_SELECTOR = 0x08; // Selector register (write)
constexpr uintptr FWCFG_DMA = 0x10;      // DMA address register (64-bit, big-endian)

// DMA control bits
constexpr u32 FW_CFG_DMA_CTL_ERROR = 0x01;
constexpr u32 FW_CFG_DMA_CTL_READ = 0x02;
constexpr u32 FW_CFG_DMA_CTL_SKIP = 0x04;
constexpr u32 FW_CFG_DMA_CTL_SELECT = 0x08;
constexpr u32 FW_CFG_DMA_CTL_WRITE = 0x10;

// DMA access structure (must be naturally aligned)
struct FWCfgDmaAccess
{
    u32 control; // Big-endian
    u32 length;  // Big-endian
    u64 address; // Big-endian
} __attribute__((packed, aligned(8)));

// Well-known selectors
constexpr u16 FW_CFG_SIGNATURE = 0x0000;
constexpr u16 FW_CFG_ID = 0x0001;
constexpr u16 FW_CFG_FILE_DIR = 0x0019;

// Expected signature "QEMU" (from constants.hpp)
constexpr u32 FWCFG_SIGNATURE_VALUE = kc::magic::FWCFG_QEMU;

// File directory entry structure (as stored in fw_cfg)
struct FWCfgFile
{
    u32 size;   // Size of file in bytes (big-endian)
    u16 select; // Selector value (big-endian)
    u16 reserved;
    char name[56]; // Null-terminated filename
};

// Byte swap helpers
/**
 * @brief Convert a 16-bit value from big-endian to host order.
 */
static inline u16 be16(u16 v)
{
    return (v >> 8) | (v << 8);
}

/**
 * @brief Convert a 32-bit value from big-endian to host order.
 */
static inline u32 be32(u32 v)
{
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) |
           ((v << 24) & 0xFF000000);
}

// String comparison helper
/**
 * @brief Compare two NUL-terminated strings for equality.
 *
 * @details
 * Minimal helper used to match fw_cfg directory entry names.
 *
 * @param a First string.
 * @param b Second string.
 * @return `true` if equal, otherwise `false`.
 */
static bool str_equal(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (*a != *b)
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

/** @copydoc fwcfg::select */
void select(u16 sel)
{
    // fw_cfg selector is written in big-endian format
    volatile u16 *selector = reinterpret_cast<volatile u16 *>(FWCFG_BASE + FWCFG_SELECTOR);
    *selector = be16(sel);
    asm volatile("dsb sy" ::: "memory");
}

/** @copydoc fwcfg::read */
void read(void *buf, u32 size)
{
    volatile u8 *data = reinterpret_cast<volatile u8 *>(FWCFG_BASE + FWCFG_DATA);
    u8 *p = static_cast<u8 *>(buf);
    for (u32 i = 0; i < size; i++)
    {
        p[i] = *data;
    }
}

/** @copydoc fwcfg::write */
void write(const void *buf, u32 size)
{
    volatile u8 *data = reinterpret_cast<volatile u8 *>(FWCFG_BASE + FWCFG_DATA);
    const u8 *p = static_cast<const u8 *>(buf);
    for (u32 i = 0; i < size; i++)
    {
        *data = p[i];
    }
}

// Helper to swap 64-bit value to big-endian
/**
 * @brief Convert a 64-bit value from host order to big-endian.
 *
 * @param v Value in host order.
 * @return Value encoded as big-endian.
 */
static inline u64 cpu_to_be64(u64 v)
{
    u32 hi = be32(v >> 32);
    u32 lo = be32(v & 0xFFFFFFFF);
    return (static_cast<u64>(lo) << 32) | hi;
}

/** @copydoc fwcfg::dma_write */
void dma_write(u16 sel, const void *buf, u32 size)
{
    // DMA access structure - must be in memory accessible by QEMU
    static FWCfgDmaAccess dma __attribute__((aligned(8)));

    // Set up the DMA descriptor
    dma.control =
        be32(FW_CFG_DMA_CTL_SELECT | FW_CFG_DMA_CTL_WRITE | (static_cast<u32>(sel) << 16));
    dma.length = be32(size);
    dma.address = cpu_to_be64(reinterpret_cast<uintptr>(buf));

    // Memory barrier before DMA
    asm volatile("dsb sy" ::: "memory");

    // Write the physical address of the DMA descriptor to the DMA register
    // The DMA register is 64-bit big-endian
    volatile u64 *dma_reg = reinterpret_cast<volatile u64 *>(FWCFG_BASE + FWCFG_DMA);
    *dma_reg = cpu_to_be64(reinterpret_cast<uintptr>(&dma));

    // Memory barrier
    asm volatile("dsb sy" ::: "memory");

    // Wait for DMA to complete (control word becomes 0 or has error bit)
    while (be32(dma.control) & ~FW_CFG_DMA_CTL_ERROR)
    {
        asm volatile("dsb sy" ::: "memory");
    }

    if (be32(dma.control) & FW_CFG_DMA_CTL_ERROR)
    {
        serial::puts("[fwcfg] DMA write error!\n");
    }
}

/** @copydoc fwcfg::init */
void init()
{
    serial::puts("[fwcfg] Checking fw_cfg at ");
    serial::put_hex(FWCFG_BASE);
    serial::puts("\n");

    // Read signature
    select(FW_CFG_SIGNATURE);
    u32 sig = 0;
    read(&sig, 4);

    serial::puts("[fwcfg] Signature: ");
    serial::put_hex(sig);
    serial::puts("\n");

    if (sig == FWCFG_SIGNATURE_VALUE)
    {
        serial::puts("[fwcfg] QEMU fw_cfg detected\n");
    }
    else
    {
        serial::puts("[fwcfg] Warning: fw_cfg not found or signature mismatch\n");
        return;
    }

    // Check ID for file interface support
    select(FW_CFG_ID);
    u32 id = 0;
    read(&id, 4);
    serial::puts("[fwcfg] ID: ");
    serial::put_hex(id);
    serial::puts("\n");

    if (!(id & 1))
    {
        serial::puts("[fwcfg] File interface not supported\n");
    }
}

/** @copydoc fwcfg::find_file */
u32 find_file(const char *name, u16 *selector)
{
    // Check ID first
    select(FW_CFG_ID);
    u32 id = 0;
    read(&id, 4);

    if (!(id & 1))
    {
        // File interface not supported
        return 0;
    }

    // Select file directory
    select(FW_CFG_FILE_DIR);

    // Read file count (big-endian u32)
    u32 count_be = 0;
    read(&count_be, 4);
    u32 count = be32(count_be);

    // Search for the file
    for (u32 i = 0; i < count; i++)
    {
        FWCfgFile file;
        read(&file, sizeof(file));

        if (str_equal(name, file.name))
        {
            *selector = be16(file.select);
            return be32(file.size);
        }
    }

    return 0; // Not found
}

} // namespace fwcfg

#pragma GCC diagnostic pop
