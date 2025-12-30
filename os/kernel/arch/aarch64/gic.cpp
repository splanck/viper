#include "gic.hpp"
#include "../../console/serial.hpp"

// Suppress unused variable warnings for register offset definitions
// These document the full register set even if not all are currently used
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wunused-function"

/**
 * @file gic.cpp
 * @brief GICv2/GICv3 initialization and IRQ dispatch for QEMU `virt`.
 *
 * @details
 * This implementation targets the QEMU `virt` platform and supports both
 * GICv2 and GICv3 memory maps. Version detection is performed during init()
 * by checking the GICD_PIDR2 register for the architecture version.
 *
 * ## GICv2
 * - Distributor (GICD) at 0x08000000
 * - CPU Interface (GICC) at 0x08010000
 * - Memory-mapped interrupt acknowledge/EOI
 *
 * ## GICv3
 * - Distributor (GICD) at 0x08000000
 * - Redistributor (GICR) at 0x080A0000 (per-CPU, 128KB each)
 * - CPU Interface via ICC_* system registers
 * - Affinity-based routing
 */
namespace gic
{

// QEMU virt machine GIC addresses
namespace
{
// GIC Distributor (GICD) - same for v2 and v3
constexpr uintptr GICD_BASE = 0x08000000;

// GIC CPU Interface (GICC) - v2 only
constexpr uintptr GICC_BASE = 0x08010000;

// GIC Redistributor (GICR) - v3 only
// Each CPU has 128KB: 64KB RD_base + 64KB SGI_base
constexpr uintptr GICR_BASE = 0x080A0000;
constexpr u64 GICR_STRIDE = 0x20000; // 128KB per CPU

// GICD registers (common)
constexpr u32 GICD_CTLR = 0x000;       // Distributor Control
constexpr u32 GICD_TYPER = 0x004;      // Interrupt Controller Type
constexpr u32 GICD_IIDR = 0x008;       // Implementer ID
constexpr u32 GICD_ISENABLER = 0x100;  // Interrupt Set-Enable (array)
constexpr u32 GICD_ICENABLER = 0x180;  // Interrupt Clear-Enable (array)
constexpr u32 GICD_ISPENDR = 0x200;    // Interrupt Set-Pending (array)
constexpr u32 GICD_ICPENDR = 0x280;    // Interrupt Clear-Pending (array)
constexpr u32 GICD_IPRIORITYR = 0x400; // Interrupt Priority (array)
constexpr u32 GICD_ITARGETSR = 0x800;  // Interrupt Target (v2 only)
constexpr u32 GICD_ICFGR = 0xC00;      // Interrupt Configuration (array)
constexpr u32 GICD_PIDR2 = 0xFFE8;     // Peripheral ID 2 (arch version)

// GICv3 GICD registers
constexpr u32 GICD_IROUTER = 0x6100; // Interrupt Routing (v3 only)

// GICC registers (v2 only)
constexpr u32 GICC_CTLR = 0x000; // CPU Interface Control
constexpr u32 GICC_PMR = 0x004;  // Priority Mask
constexpr u32 GICC_BPR = 0x008;  // Binary Point
constexpr u32 GICC_IAR = 0x00C;  // Interrupt Acknowledge
constexpr u32 GICC_EOIR = 0x010; // End of Interrupt

// GICR registers (v3 only)
// RD_base (first 64KB)
constexpr u32 GICR_CTLR = 0x0000;
constexpr u32 GICR_IIDR = 0x0004;
constexpr u32 GICR_TYPER = 0x0008; // 64-bit
constexpr u32 GICR_WAKER = 0x0014;
constexpr u32 GICR_PIDR2 = 0xFFE8;
// SGI_base (second 64KB, offset 0x10000)
constexpr u32 GICR_IGROUPR0 = 0x10080;
constexpr u32 GICR_ISENABLER0 = 0x10100;
constexpr u32 GICR_ICENABLER0 = 0x10180;
constexpr u32 GICR_IPRIORITYR = 0x10400;

// GICD_CTLR bits
constexpr u32 GICD_CTLR_EnableGrp0 = 1 << 0;
constexpr u32 GICD_CTLR_EnableGrp1NS = 1 << 1;
constexpr u32 GICD_CTLR_EnableGrp1A = 1 << 1; // v3 alias
constexpr u32 GICD_CTLR_ARE_S = 1 << 4;       // v3: Affinity routing enable (secure)
constexpr u32 GICD_CTLR_ARE_NS = 1 << 5;      // v3: Affinity routing enable (non-secure)

// GICR_WAKER bits
constexpr u32 GICR_WAKER_ProcessorSleep = 1 << 1;
constexpr u32 GICR_WAKER_ChildrenAsleep = 1 << 2;

// Detected version
Version detected_version = Version::UNKNOWN;

// Register access helpers
inline volatile u32 &gicd(u32 offset)
{
    return *reinterpret_cast<volatile u32 *>(GICD_BASE + offset);
}

inline volatile u64 &gicd64(u32 offset)
{
    return *reinterpret_cast<volatile u64 *>(GICD_BASE + offset);
}

inline volatile u32 &gicc(u32 offset)
{
    return *reinterpret_cast<volatile u32 *>(GICC_BASE + offset);
}

inline volatile u32 &gicr(u32 cpu, u32 offset)
{
    return *reinterpret_cast<volatile u32 *>(GICR_BASE + cpu * GICR_STRIDE + offset);
}

inline volatile u64 &gicr64(u32 cpu, u32 offset)
{
    return *reinterpret_cast<volatile u64 *>(GICR_BASE + cpu * GICR_STRIDE + offset);
}

// ICC system register access (GICv3)
inline u64 read_icc_sre_el1()
{
    u64 val;
    asm volatile("mrs %0, S3_0_C12_C12_5" : "=r"(val)); // ICC_SRE_EL1
    return val;
}

inline void write_icc_sre_el1(u64 val)
{
    asm volatile("msr S3_0_C12_C12_5, %0" ::"r"(val)); // ICC_SRE_EL1
    asm volatile("isb");
}

inline void write_icc_pmr_el1(u64 val)
{
    asm volatile("msr S3_0_C4_C6_0, %0" ::"r"(val)); // ICC_PMR_EL1
}

inline void write_icc_bpr1_el1(u64 val)
{
    asm volatile("msr S3_0_C12_C12_3, %0" ::"r"(val)); // ICC_BPR1_EL1
}

inline void write_icc_ctlr_el1(u64 val)
{
    asm volatile("msr S3_0_C12_C12_4, %0" ::"r"(val)); // ICC_CTLR_EL1
}

inline void write_icc_igrpen1_el1(u64 val)
{
    asm volatile("msr S3_0_C12_C12_7, %0" ::"r"(val)); // ICC_IGRPEN1_EL1
}

inline u64 read_icc_iar1_el1()
{
    u64 val;
    asm volatile("mrs %0, S3_0_C12_C12_0" : "=r"(val)); // ICC_IAR1_EL1
    return val;
}

inline void write_icc_eoir1_el1(u64 val)
{
    asm volatile("msr S3_0_C12_C12_1, %0" ::"r"(val)); // ICC_EOIR1_EL1
}

// Get current CPU ID from MPIDR
inline u32 get_cpu_id()
{
    u64 mpidr;
    asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return mpidr & 0xFF; // Aff0 for QEMU virt
}

// IRQ handlers
IrqHandler handlers[MAX_IRQS] = {nullptr};

} // namespace

// Forward declarations for version-specific init (in gic namespace)
static void init_v2();
static void init_v3();
static void init_cpu_v2();
static void init_cpu_v3();

/**
 * @brief Detect GIC version.
 *
 * @details
 * For now, we default to GICv2 which is what QEMU virt uses by default.
 * Reading GICD_PIDR2 to auto-detect requires device memory to be mapped,
 * which may not be the case when gic::init() is called early in boot.
 *
 * To use GICv3, define VIPER_GIC_V3 at compile time or call
 * gic::init() after MMU setup maps the device region.
 *
 * QEMU virt GICv3 can be enabled with: -M virt,gic-version=3
 */
static Version detect_version()
{
#ifdef VIPER_GIC_V3
    serial::puts("[gic] GICv3 selected via compile flag\n");
    return Version::V3;
#else
    // Default to GICv2 for QEMU virt compatibility
    // GICv3 detection via PIDR2 requires device memory mapping
    serial::puts("[gic] Using GICv2 (default for QEMU virt)\n");
    return Version::V2;
#endif
}

/**
 * @brief Initialize GICv2.
 */
static void init_v2()
{
    serial::puts("[gic] Initializing GICv2...\n");

    // Read GIC type
    u32 typer = gicd(GICD_TYPER);
    u32 num_irqs = ((typer & 0x1F) + 1) * 32;
    serial::puts("[gic] Max IRQs: ");
    serial::put_dec(num_irqs);
    serial::puts("\n");

    // Disable distributor while configuring
    gicd(GICD_CTLR) = 0;

    // Disable all interrupts
    for (u32 i = 0; i < num_irqs / 32; i++)
    {
        gicd(GICD_ICENABLER + i * 4) = 0xFFFFFFFF;
    }

    // Clear all pending interrupts
    for (u32 i = 0; i < num_irqs / 32; i++)
    {
        gicd(GICD_ICPENDR + i * 4) = 0xFFFFFFFF;
    }

    // Set all interrupts to lowest priority
    for (u32 i = 0; i < num_irqs / 4; i++)
    {
        gicd(GICD_IPRIORITYR + i * 4) = 0xA0A0A0A0;
    }

    // Set all SPIs to target CPU 0
    for (u32 i = 8; i < num_irqs / 4; i++)
    {
        gicd(GICD_ITARGETSR + i * 4) = 0x01010101;
    }

    // Configure all SPIs as level-triggered
    for (u32 i = 2; i < num_irqs / 16; i++)
    {
        gicd(GICD_ICFGR + i * 4) = 0x00000000;
    }

    // Enable distributor
    gicd(GICD_CTLR) = 1;

    // Configure CPU interface
    init_cpu_v2();

    serial::puts("[gic] GICv2 initialized\n");
}

/**
 * @brief Initialize GICv2 CPU interface.
 */
static void init_cpu_v2()
{
    gicc(GICC_PMR) = 0xFF; // Accept all priorities
    gicc(GICC_BPR) = 0;    // No priority grouping
    gicc(GICC_CTLR) = 1;   // Enable CPU interface
}

/**
 * @brief Wake up a GICv3 redistributor.
 */
static bool wake_redistributor(u32 cpu)
{
    // Clear ProcessorSleep bit
    u32 waker = gicr(cpu, GICR_WAKER);
    waker &= ~GICR_WAKER_ProcessorSleep;
    gicr(cpu, GICR_WAKER) = waker;

    // Wait for ChildrenAsleep to clear (with timeout)
    for (int i = 0; i < 1000000; i++)
    {
        waker = gicr(cpu, GICR_WAKER);
        if (!(waker & GICR_WAKER_ChildrenAsleep))
        {
            return true;
        }
    }

    serial::puts("[gic] WARNING: Redistributor wake timeout for CPU ");
    serial::put_dec(cpu);
    serial::puts("\n");
    return false;
}

/**
 * @brief Initialize GICv3.
 */
static void init_v3()
{
    serial::puts("[gic] Initializing GICv3...\n");

    // Read GIC type
    u32 typer = gicd(GICD_TYPER);
    u32 num_irqs = ((typer & 0x1F) + 1) * 32;
    serial::puts("[gic] Max IRQs: ");
    serial::put_dec(num_irqs);
    serial::puts("\n");

    // Disable distributor while configuring
    gicd(GICD_CTLR) = 0;

    // Wait for RWP (Register Write Pending) to clear
    while (gicd(GICD_CTLR) & (1 << 31))
        ;

    // Disable all SPIs (IRQs 32+)
    for (u32 i = 1; i < num_irqs / 32; i++)
    {
        gicd(GICD_ICENABLER + i * 4) = 0xFFFFFFFF;
    }

    // Clear all pending SPIs
    for (u32 i = 1; i < num_irqs / 32; i++)
    {
        gicd(GICD_ICPENDR + i * 4) = 0xFFFFFFFF;
    }

    // Set SPI priorities
    for (u32 i = 8; i < num_irqs / 4; i++)
    {
        gicd(GICD_IPRIORITYR + i * 4) = 0xA0A0A0A0;
    }

    // Configure SPI routing - route all to CPU 0 (affinity 0.0.0.0)
    // IROUTER format: Aff3.Aff2.Aff1.Aff0 in bits [39:32].[23:16].[15:8].[7:0]
    for (u32 i = 32; i < num_irqs; i++)
    {
        gicd64(GICD_IROUTER + (i - 32) * 8) = 0; // Route to Aff0=0
    }

    // Configure all SPIs as level-triggered
    for (u32 i = 2; i < num_irqs / 16; i++)
    {
        gicd(GICD_ICFGR + i * 4) = 0x00000000;
    }

    // Enable distributor with affinity routing
    gicd(GICD_CTLR) = GICD_CTLR_EnableGrp1NS | GICD_CTLR_ARE_NS;

    // Wait for RWP
    while (gicd(GICD_CTLR) & (1 << 31))
        ;

    serial::puts("[gic] Distributor configured with affinity routing\n");

    // Initialize CPU interface
    init_cpu_v3();

    serial::puts("[gic] GICv3 initialized\n");
}

/**
 * @brief Initialize GICv3 CPU interface (redistributor + ICC registers).
 */
static void init_cpu_v3()
{
    u32 cpu = get_cpu_id();

    serial::puts("[gic] Initializing GICv3 CPU interface for CPU ");
    serial::put_dec(cpu);
    serial::puts("\n");

    // Wake up the redistributor
    if (!wake_redistributor(cpu))
    {
        serial::puts("[gic] ERROR: Failed to wake redistributor\n");
        return;
    }

    // Configure redistributor for SGIs/PPIs
    // Disable all SGIs and PPIs first
    gicr(cpu, GICR_ICENABLER0) = 0xFFFFFFFF;

    // Set SGI/PPI priorities
    for (u32 i = 0; i < 8; i++)
    {
        gicr(cpu, GICR_IPRIORITYR + i * 4) = 0xA0A0A0A0;
    }

    // Put all interrupts in group 1 (non-secure)
    gicr(cpu, GICR_IGROUPR0) = 0xFFFFFFFF;

    // Enable system register access
    u64 sre = read_icc_sre_el1();
    sre |= 0x7; // SRE, DFB, DIB
    write_icc_sre_el1(sre);

    // Configure ICC registers
    write_icc_pmr_el1(0xFF);  // Accept all priorities
    write_icc_bpr1_el1(0);    // No priority grouping
    write_icc_ctlr_el1(0);    // EOImode = 0 (drop priority and deactivate)
    write_icc_igrpen1_el1(1); // Enable group 1 interrupts

    asm volatile("isb");

    serial::puts("[gic] CPU ");
    serial::put_dec(cpu);
    serial::puts(" interface configured\n");
}

/** @copydoc gic::init */
void init()
{
    serial::puts("[gic] Initializing GIC...\n");

    // Detect GIC version
    detected_version = detect_version();

    switch (detected_version)
    {
        case Version::V2:
            init_v2();
            break;
        case Version::V3:
            init_v3();
            break;
        default:
            serial::puts("[gic] ERROR: Unknown GIC version, falling back to v2\n");
            detected_version = Version::V2;
            init_v2();
            break;
    }
}

/** @copydoc gic::enable_irq */
void enable_irq(u32 irq)
{
    if (irq >= MAX_IRQS)
        return;

    u32 reg = irq / 32;
    u32 bit = irq % 32;

    if (irq < 32 && detected_version == Version::V3)
    {
        // SGIs and PPIs (0-31) use redistributor in GICv3
        u32 cpu = get_cpu_id();
        gicr(cpu, GICR_ISENABLER0) = (1 << bit);
    }
    else
    {
        // SPIs (32+) and all v2 IRQs use distributor
        gicd(GICD_ISENABLER + reg * 4) = (1 << bit);
    }
}

/** @copydoc gic::disable_irq */
void disable_irq(u32 irq)
{
    if (irq >= MAX_IRQS)
        return;

    u32 reg = irq / 32;
    u32 bit = irq % 32;

    if (irq < 32 && detected_version == Version::V3)
    {
        // SGIs and PPIs (0-31) use redistributor in GICv3
        u32 cpu = get_cpu_id();
        gicr(cpu, GICR_ICENABLER0) = (1 << bit);
    }
    else
    {
        // SPIs (32+) and all v2 IRQs use distributor
        gicd(GICD_ICENABLER + reg * 4) = (1 << bit);
    }
}

/** @copydoc gic::set_priority */
void set_priority(u32 irq, u8 priority)
{
    if (irq >= MAX_IRQS)
        return;

    u32 reg = irq / 4;
    u32 offset = (irq % 4) * 8;

    u32 val = gicd(GICD_IPRIORITYR + reg * 4);
    val &= ~(0xFF << offset);
    val |= (priority << offset);
    gicd(GICD_IPRIORITYR + reg * 4) = val;
}

/** @copydoc gic::register_handler */
void register_handler(u32 irq, IrqHandler handler)
{
    if (irq < MAX_IRQS)
    {
        handlers[irq] = handler;
    }
}

/** @copydoc gic::handle_irq */
void handle_irq()
{
    u32 irq;

    if (detected_version == Version::V3)
    {
        // GICv3: Read interrupt ID from ICC_IAR1_EL1
        u64 iar = read_icc_iar1_el1();
        irq = iar & 0xFFFFFF; // 24-bit interrupt ID

        // Check for spurious interrupt (1023 = no pending interrupt)
        if (irq >= 1020)
        {
            return;
        }

        // End of interrupt BEFORE calling handler
        write_icc_eoir1_el1(irq);
    }
    else
    {
        // GICv2: Read interrupt ID from GICC_IAR
        u32 iar = gicc(GICC_IAR);
        irq = iar & 0x3FF;

        // Check for spurious interrupt (1023)
        if (irq >= 1020)
        {
            return;
        }

        // End of interrupt BEFORE calling handler
        gicc(GICC_EOIR) = iar;
    }

    // Call handler if registered
    if (irq < MAX_IRQS && handlers[irq])
    {
        handlers[irq]();
    }
    else
    {
        serial::puts("[gic] Unhandled IRQ: ");
        serial::put_dec(irq);
        serial::puts("\n");
    }
}

/** @copydoc gic::eoi */
void eoi(u32 irq)
{
    if (detected_version == Version::V3)
    {
        write_icc_eoir1_el1(irq);
    }
    else
    {
        gicc(GICC_EOIR) = irq;
    }
}

/** @copydoc gic::get_version */
Version get_version()
{
    return detected_version;
}

/** @copydoc gic::init_cpu */
void init_cpu()
{
    if (detected_version == Version::V3)
    {
        init_cpu_v3();
    }
    else
    {
        init_cpu_v2();
    }
}

} // namespace gic

#pragma GCC diagnostic pop
