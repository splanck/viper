#include "mmu.hpp"
#include "../../console/serial.hpp"
#include "../../mm/pmm.hpp"

/**
 * @file mmu.cpp
 * @brief AArch64 MMU bring-up and kernel identity mapping tables.
 *
 * @details
 * This file contains the low-level code that builds an initial set of
 * translation tables and enables the MMU. The current strategy is to create a
 * kernel identity mapping over a limited region (first 2GiB) using large block
 * descriptors for simplicity:
 * - Low region is treated as device memory for MMIO.
 * - RAM region is treated as normal cacheable memory.
 *
 * The setup programs:
 * - MAIR_EL1 for memory attribute encodings.
 * - TCR_EL1 for translation control (4KiB granule, 48-bit VAs).
 * - TTBR0_EL1 with the newly created kernel table root.
 * - SCTLR_EL1 to enable the MMU and caches.
 *
 * This is a bring-up implementation and does not yet configure TTBR1 (higher
 * half) or ASIDs beyond a minimal configuration.
 */
namespace mmu
{

// TCR_EL1 bit fields
namespace tcr
{
// T0SZ: Size of TTBR0 region (VA bits = 64 - T0SZ)
// 16 means 48-bit VA (0x0 to 0x0000_FFFF_FFFF_FFFF)
constexpr u64 T0SZ_48BIT = 16ULL << 0;

// T1SZ: Size of TTBR1 region
constexpr u64 T1SZ_48BIT = 16ULL << 16;

// TG0: TTBR0 granule size (4KB)
constexpr u64 TG0_4KB = 0b00ULL << 14;

// TG1: TTBR1 granule size (4KB)
constexpr u64 TG1_4KB = 0b10ULL << 30;

// SH0: TTBR0 shareability (inner shareable)
constexpr u64 SH0_INNER = 0b11ULL << 12;

// SH1: TTBR1 shareability (inner shareable)
constexpr u64 SH1_INNER = 0b11ULL << 28;

// ORGN0/IRGN0: TTBR0 cacheability (write-back, write-allocate)
constexpr u64 ORGN0_WBWA = 0b01ULL << 10;
constexpr u64 IRGN0_WBWA = 0b01ULL << 8;

// ORGN1/IRGN1: TTBR1 cacheability
constexpr u64 ORGN1_WBWA = 0b01ULL << 26;
constexpr u64 IRGN1_WBWA = 0b01ULL << 24;

// EPD0: TTBR0 translation disable = 0 (enable)
constexpr u64 EPD0_ENABLE = 0ULL << 7;

// EPD1: TTBR1 translation disable (disable TTBR1 for now)
constexpr u64 EPD1_DISABLE = 1ULL << 23;

// IPS: Intermediate Physical Address Size (40 bits = 1TB)
constexpr u64 IPS_40BIT = 0b010ULL << 32;

// A1: ASID selection (0 = use TTBR0's ASID)
constexpr u64 A1_TTBR0 = 0ULL << 22;

// AS: ASID size (0 = 8-bit ASIDs for simplicity)
constexpr u64 AS_8BIT = 0ULL << 36;
} // namespace tcr

// MAIR_EL1 attribute indices
namespace mair
{
// Attr0: Device-nGnRnE (strongly ordered)
constexpr u64 ATTR0_DEVICE = 0x00ULL << 0;

// Attr1: Normal, Write-Back, Write-Allocate (inner and outer)
constexpr u64 ATTR1_NORMAL = 0xFFULL << 8;

// Attr2: Normal, Non-cacheable
constexpr u64 ATTR2_NC = 0x44ULL << 16;
} // namespace mair

// Page table entry bits (for kernel identity mapping)
namespace pte
{
constexpr u64 VALID = 1ULL << 0;
constexpr u64 TABLE = 1ULL << 1;       // For L0/L1/L2 table entries
constexpr u64 BLOCK = 0ULL << 1;       // For L1/L2 block entries (1GB/2MB)
constexpr u64 AF = 1ULL << 10;         // Access flag
constexpr u64 SH_INNER = 3ULL << 8;    // Inner shareable
constexpr u64 AP_RW_EL1 = 0ULL << 6;   // EL1 read/write, EL0 no access
constexpr u64 ATTR_NORMAL = 1ULL << 2; // MAIR index 1
constexpr u64 ATTR_DEVICE = 0ULL << 2; // MAIR index 0
constexpr u64 UXN = 1ULL << 54;        // User execute never
constexpr u64 PXN = 0ULL << 53;        // Privileged execute allowed
} // namespace pte

static bool initialized = false;
static u64 kernel_ttbr0 = 0; // Root of kernel page tables

// Create kernel page tables with identity mapping
// Maps 0x00000000-0x80000000 (first 2GB) using 1GB blocks
/**
 * @brief Build the kernel's initial identity-mapped translation tables.
 *
 * @details
 * Allocates and zeros an L0 table and an L1 table. L0[0] points to the L1 table.
 * Two 1GiB block entries are installed in L1:
 * - `0x00000000-0x3FFFFFFF`: device memory (MMIO region).
 * - `0x40000000-0x7FFFFFFF`: normal cacheable memory (RAM region on QEMU virt).
 *
 * The resulting L0 physical address is stored in `kernel_ttbr0` for later use.
 *
 * @return `true` on success, `false` if page-table allocation fails.
 */
static bool create_kernel_page_tables()
{
    serial::puts("[mmu] Creating kernel identity-mapped page tables...\n");

    // Allocate L0 table (one page)
    u64 l0_phys = pmm::alloc_page();
    if (l0_phys == 0)
    {
        serial::puts("[mmu] ERROR: Failed to allocate L0 table\n");
        return false;
    }

    // Zero the L0 table
    u64 *l0 = reinterpret_cast<u64 *>(l0_phys);
    for (int i = 0; i < 512; i++)
    {
        l0[i] = 0;
    }

    // Allocate L1 table for first 512GB (entry 0 of L0)
    u64 l1_phys = pmm::alloc_page();
    if (l1_phys == 0)
    {
        serial::puts("[mmu] ERROR: Failed to allocate L1 table\n");
        pmm::free_page(l0_phys);
        return false;
    }

    // Zero the L1 table
    u64 *l1 = reinterpret_cast<u64 *>(l1_phys);
    for (int i = 0; i < 512; i++)
    {
        l1[i] = 0;
    }

    // Install L1 table in L0[0]
    l0[0] = l1_phys | pte::VALID | pte::TABLE;

    // Map first 2GB using 1GB block entries in L1
    // L1[0]: 0x00000000 - 0x3FFFFFFF (device memory for MMIO)
    // L1[1]: 0x40000000 - 0x7FFFFFFF (normal memory for RAM)

    // Entry 0: Device memory for low MMIO region (UART at 0x09000000, GIC, etc.)
    l1[0] = 0x00000000ULL | pte::VALID | pte::BLOCK | pte::AF | pte::SH_INNER | pte::AP_RW_EL1 |
            pte::ATTR_DEVICE | pte::UXN;

    // Entry 1: Normal memory for RAM (0x40000000 - 0x7FFFFFFF)
    l1[1] = 0x40000000ULL | pte::VALID | pte::BLOCK | pte::AF | pte::SH_INNER | pte::AP_RW_EL1 |
            pte::ATTR_NORMAL | pte::UXN;

    serial::puts("[mmu] L0 table at: ");
    serial::put_hex(l0_phys);
    serial::puts("\n");

    serial::puts("[mmu] L1 table at: ");
    serial::put_hex(l1_phys);
    serial::puts("\n");

    serial::puts("[mmu] L1[0] (device 0x0-0x3FFFFFFF): ");
    serial::put_hex(l1[0]);
    serial::puts("\n");

    serial::puts("[mmu] L1[1] (normal 0x40000000-0x7FFFFFFF): ");
    serial::put_hex(l1[1]);
    serial::puts("\n");

    kernel_ttbr0 = l0_phys;
    return true;
}

/** @copydoc mmu::init */
void init()
{
    serial::puts("[mmu] Configuring MMU for user space support...\n");

    // Read current SCTLR_EL1 to check MMU state
    u64 sctlr;
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    serial::puts("[mmu] Current SCTLR_EL1: ");
    serial::put_hex(sctlr);
    serial::puts(" (M=");
    serial::put_dec(sctlr & 1);
    serial::puts(")\n");

    // Create kernel page tables FIRST (before enabling MMU)
    if (!create_kernel_page_tables())
    {
        serial::puts("[mmu] ERROR: Failed to create kernel page tables!\n");
        return;
    }

    // Configure MAIR_EL1 for memory attributes
    u64 mair_val = mair::ATTR0_DEVICE | mair::ATTR1_NORMAL | mair::ATTR2_NC;
    asm volatile("msr mair_el1, %0" ::"r"(mair_val) : "memory");

    serial::puts("[mmu] MAIR_EL1 configured: ");
    serial::put_hex(mair_val);
    serial::puts("\n");

    // Configure TCR_EL1 for TTBR0 only (disable TTBR1 for now)
    u64 tcr_val = tcr::T0SZ_48BIT | tcr::T1SZ_48BIT | tcr::TG0_4KB | tcr::TG1_4KB | tcr::SH0_INNER |
                  tcr::SH1_INNER | tcr::ORGN0_WBWA | tcr::IRGN0_WBWA | tcr::ORGN1_WBWA |
                  tcr::IRGN1_WBWA | tcr::EPD0_ENABLE |
                  tcr::EPD1_DISABLE | // Disable TTBR1 to avoid issues
                  tcr::IPS_40BIT | tcr::A1_TTBR0 | tcr::AS_8BIT;

    asm volatile("msr tcr_el1, %0" ::"r"(tcr_val) : "memory");
    asm volatile("isb");

    serial::puts("[mmu] TCR_EL1 configured: ");
    serial::put_hex(tcr_val);
    serial::puts("\n");

    // Set TTBR0 to kernel page tables
    asm volatile("msr ttbr0_el1, %0" ::"r"(kernel_ttbr0) : "memory");
    asm volatile("isb");

    serial::puts("[mmu] TTBR0_EL1 set to: ");
    serial::put_hex(kernel_ttbr0);
    serial::puts("\n");

    // Invalidate TLBs
    asm volatile("tlbi vmalle1is");
    asm volatile("dsb sy");
    asm volatile("isb");

    // Enable MMU (M bit = 1), caches (C, I bits)
    // Disable alignment check (A bit = 0) to allow unaligned accesses
    sctlr |= (1ULL << 0);  // M: Enable MMU
    sctlr &= ~(1ULL << 1); // A: Disable alignment check
    sctlr |= (1ULL << 2);  // C: Enable data cache
    sctlr |= (1ULL << 12); // I: Enable instruction cache

    serial::puts("[mmu] Enabling MMU...\n");

    // This is the critical moment - enable MMU with identity-mapped kernel
    asm volatile("msr sctlr_el1, %0  \n"
                 "isb                \n" ::"r"(sctlr)
                 : "memory");

    serial::puts("[mmu] MMU enabled successfully!\n");

    initialized = true;
    serial::puts("[mmu] Kernel running with identity-mapped page tables\n");
}

/** @copydoc mmu::get_kernel_ttbr0 */
u64 get_kernel_ttbr0()
{
    return kernel_ttbr0;
}

/** @copydoc mmu::is_user_space_enabled */
bool is_user_space_enabled()
{
    return initialized;
}

} // namespace mmu
