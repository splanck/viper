#include "gic.hpp"
#include "../../console/serial.hpp"

/**
 * @file gic.cpp
 * @brief GICv2 initialization and IRQ dispatch for QEMU `virt`.
 *
 * @details
 * This implementation targets the QEMU `virt` platform's GICv2 memory map and
 * provides a small handler registration mechanism suitable for early kernel
 * bring-up.
 *
 * The implementation programs:
 * - The Distributor (GICD) for global interrupt configuration.
 * - The CPU Interface (GICC) for per-CPU interrupt delivery and acknowledgement.
 *
 * A simple fixed-size handler array is used for IRQ dispatch.
 */
namespace gic
{

// QEMU virt machine GICv2 addresses
namespace
{
// GIC Distributor (GICD)
constexpr uintptr GICD_BASE = 0x08000000;

// GIC CPU Interface (GICC)
constexpr uintptr GICC_BASE = 0x08010000;

// GICD registers
constexpr u32 GICD_CTLR = 0x000;       // Distributor Control
constexpr u32 GICD_TYPER = 0x004;      // Interrupt Controller Type
constexpr u32 GICD_ISENABLER = 0x100;  // Interrupt Set-Enable (array)
constexpr u32 GICD_ICENABLER = 0x180;  // Interrupt Clear-Enable (array)
constexpr u32 GICD_ISPENDR = 0x200;    // Interrupt Set-Pending (array)
constexpr u32 GICD_ICPENDR = 0x280;    // Interrupt Clear-Pending (array)
constexpr u32 GICD_IPRIORITYR = 0x400; // Interrupt Priority (array)
constexpr u32 GICD_ITARGETSR = 0x800;  // Interrupt Target (array)
constexpr u32 GICD_ICFGR = 0xC00;      // Interrupt Configuration (array)

// GICC registers
constexpr u32 GICC_CTLR = 0x000; // CPU Interface Control
constexpr u32 GICC_PMR = 0x004;  // Priority Mask
constexpr u32 GICC_BPR = 0x008;  // Binary Point
constexpr u32 GICC_IAR = 0x00C;  // Interrupt Acknowledge
constexpr u32 GICC_EOIR = 0x010; // End of Interrupt

// Register access helpers
/**
 * @brief Access a 32-bit Distributor register.
 *
 * @param offset Register offset from the Distributor base.
 * @return Volatile reference to the register.
 */
inline volatile u32 &gicd(u32 offset)
{
    return *reinterpret_cast<volatile u32 *>(GICD_BASE + offset);
}

/**
 * @brief Access a 32-bit CPU Interface register.
 *
 * @param offset Register offset from the CPU Interface base.
 * @return Volatile reference to the register.
 */
inline volatile u32 &gicc(u32 offset)
{
    return *reinterpret_cast<volatile u32 *>(GICC_BASE + offset);
}

// IRQ handlers
IrqHandler handlers[MAX_IRQS] = {nullptr};
} // namespace

/** @copydoc gic::init */
void init()
{
    serial::puts("[gic] Initializing GIC...\n");

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
    { // Skip SGIs and PPIs
        gicd(GICD_ITARGETSR + i * 4) = 0x01010101;
    }

    // Configure all SPIs as level-triggered
    for (u32 i = 2; i < num_irqs / 16; i++)
    { // Skip SGIs and PPIs
        gicd(GICD_ICFGR + i * 4) = 0x00000000;
    }

    // Enable distributor
    gicd(GICD_CTLR) = 1;

    // Configure CPU interface
    gicc(GICC_PMR) = 0xFF; // Accept all priorities
    gicc(GICC_BPR) = 0;    // No priority grouping
    gicc(GICC_CTLR) = 1;   // Enable CPU interface

    serial::puts("[gic] GIC initialized\n");
}

/** @copydoc gic::enable_irq */
void enable_irq(u32 irq)
{
    if (irq >= MAX_IRQS)
        return;

    u32 reg = irq / 32;
    u32 bit = irq % 32;
    gicd(GICD_ISENABLER + reg * 4) = (1 << bit);
}

/** @copydoc gic::disable_irq */
void disable_irq(u32 irq)
{
    if (irq >= MAX_IRQS)
        return;

    u32 reg = irq / 32;
    u32 bit = irq % 32;
    gicd(GICD_ICENABLER + reg * 4) = (1 << bit);
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
    // Read interrupt ID
    u32 iar = gicc(GICC_IAR);
    u32 irq = iar & 0x3FF;

    // Check for spurious interrupt (1023)
    if (irq >= 1020)
    {
        return;
    }

    // End of interrupt BEFORE calling handler
    // This allows context switches inside handlers to work
    // (the timer IRQ will re-trigger if still pending)
    gicc(GICC_EOIR) = iar;

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
    gicc(GICC_EOIR) = irq;
}

} // namespace gic
