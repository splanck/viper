#pragma once

#include "../../include/types.hpp"

/**
 * @file gic.hpp
 * @brief AArch64 Generic Interrupt Controller (GIC) interface.
 *
 * @details
 * The Generic Interrupt Controller routes hardware interrupts to the CPU and
 * provides prioritization and masking controls. This implementation supports
 * both GICv2 and GICv3:
 *
 * ## GICv2 (Legacy)
 * - Distributor (GICD) for global interrupt configuration
 * - CPU Interface (GICC) via memory-mapped registers
 * - Target-list based interrupt routing
 *
 * ## GICv3 (Modern)
 * - Distributor (GICD) with affinity routing extensions
 * - Redistributor (GICR) per-CPU for SGI/PPI configuration
 * - CPU Interface via ICC_* system registers
 * - Affinity-based interrupt routing
 *
 * Version detection is automatic: the init function probes for GICv3 GICR
 * and falls back to GICv2 if not found.
 *
 * On QEMU virt, GICv3 is available with `-M virt,gic-version=3`.
 */
namespace gic
{

/// GIC version detected during initialization
enum class Version
{
    UNKNOWN = 0,
    V2 = 2,
    V3 = 3
};

/**
 * @brief Function pointer type for IRQ handlers registered with the GIC layer.
 *
 * @details
 * The handler is executed in interrupt context (from the IRQ exception
 * handler). Implementations should avoid blocking operations and should be
 * careful about what locks or subsystems they touch.
 */
using IrqHandler = void (*)();

/** @brief Maximum IRQ number supported by the simple handler table. */
constexpr u32 MAX_IRQS = 256;

/**
 * @brief Initialize the GIC for the current CPU.
 *
 * @details
 * Programs the GIC Distributor and CPU Interface into a known state:
 * - Disables and clears pending interrupts.
 * - Sets default priorities.
 * - Routes shared peripheral interrupts (SPIs) to CPU0.
 * - Enables the distributor and CPU interface.
 *
 * This should be called during early boot before enabling interrupts globally.
 */
void init();

/**
 * @brief Enable delivery of an IRQ.
 *
 * @param irq Interrupt ID to enable.
 */
void enable_irq(u32 irq);

/**
 * @brief Disable delivery of an IRQ.
 *
 * @param irq Interrupt ID to disable.
 */
void disable_irq(u32 irq);

/**
 * @brief Set the priority of an IRQ.
 *
 * @details
 * Lower numeric values represent higher priority on GICv2.
 *
 * @param irq Interrupt ID.
 * @param priority Priority value (0 = highest, 255 = lowest).
 */
void set_priority(u32 irq, u8 priority);

/**
 * @brief Register a callback for an IRQ.
 *
 * @details
 * Stores the handler in a simple in-memory table. If no handler is registered
 * for an IRQ, the default behavior is to print a diagnostic message.
 *
 * @param irq Interrupt ID to associate with the handler.
 * @param handler Callback function to invoke when the IRQ is signaled.
 */
void register_handler(u32 irq, IrqHandler handler);

/**
 * @brief Top-level IRQ dispatch routine called from the IRQ exception handler.
 *
 * @details
 * Acknowledges the pending interrupt via the CPU interface, filters out
 * spurious interrupts, signals end-of-interrupt, and invokes the registered
 * handler (if any).
 *
 * The end-of-interrupt is issued before calling the handler to allow the handler
 * to perform actions (including scheduling) without keeping the interrupt
 * "in service" for the duration of the handler.
 */
void handle_irq();

/**
 * @brief Send an End-Of-Interrupt (EOI) signal for an IRQ.
 *
 * @details
 * Most users should rely on @ref handle_irq which handles acknowledgement and
 * EOI. This helper exists for cases where the kernel wants to manage EOI
 * explicitly.
 *
 * @param irq Interrupt ID / raw IAR value depending on usage.
 */
void eoi(u32 irq);

/**
 * @brief Get the detected GIC version.
 *
 * @return GIC version (V2, V3, or UNKNOWN if not initialized).
 */
Version get_version();

/**
 * @brief Initialize the current CPU's GIC interface.
 *
 * @details
 * For secondary CPUs, call this instead of init() to set up the per-CPU
 * interface (GICC for v2, GICR+ICC for v3) without reinitializing the
 * global distributor.
 */
void init_cpu();

} // namespace gic
