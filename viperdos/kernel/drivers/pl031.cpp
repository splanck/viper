//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

/**
 * @file pl031.cpp
 * @brief PL031 Real-Time Clock (RTC) driver for QEMU virt.
 *
 * @details
 * Implements a minimal PL031 RTC driver that reads wall-clock time from the
 * hardware RTC data register. The PL031 is a simple MMIO device that provides
 * seconds since the Unix epoch.
 *
 * QEMU virt machine maps the PL031 at 0x09010000 with IRQ 34 (SPI 2).
 * We only need the data register for read access; alarm/interrupt features
 * are not used.
 *
 * PL031 Register Map (offsets from base):
 *   0x000 RTCDR  - Data Register (read: current time in seconds)
 *   0x004 RTCMR  - Match Register (alarm)
 *   0x008 RTCLR  - Load Register (write: set time)
 *   0x00C RTCCR  - Control Register (bit 0: enable)
 *   0xFE0 PeriphID0-3 (identification)
 */
#include "pl031.hpp"
#include "../console/serial.hpp"

namespace pl031 {

namespace {

/// QEMU virt PL031 MMIO base address
constexpr u64 PL031_BASE = 0x09010000;

/// PL031 register offsets
constexpr u64 RTCDR = 0x000; ///< Data Register (seconds since epoch)
constexpr u64 RTCCR = 0x00C; ///< Control Register

/// PL031 PrimeCell identification register offsets
constexpr u64 PERIPHID0 = 0xFE0;
constexpr u64 PERIPHID1 = 0xFE4;

/// Expected PL031 identification values
constexpr u8 PL031_PERIPHID0 = 0x31; ///< Part number low
constexpr u8 PL031_PERIPHID1 = 0x10; ///< Part number high + designer

/// Driver state
bool initialized = false;

/// Read a 32-bit MMIO register
inline u32 read_reg(u64 offset) {
    return *reinterpret_cast<volatile u32 *>(PL031_BASE + offset);
}

/// Write a 32-bit MMIO register
inline void write_reg(u64 offset, u32 value) {
    *reinterpret_cast<volatile u32 *>(PL031_BASE + offset) = value;
}

} // namespace

bool init() {
    serial::puts("[pl031] Initializing RTC at ");
    serial::put_hex(PL031_BASE);
    serial::puts("\n");

    // Verify PL031 identification registers
    u8 id0 = static_cast<u8>(read_reg(PERIPHID0));
    u8 id1 = static_cast<u8>(read_reg(PERIPHID1));

    if (id0 != PL031_PERIPHID0 || id1 != PL031_PERIPHID1) {
        serial::puts("[pl031] Device identification mismatch: id0=");
        serial::put_hex(id0);
        serial::puts(" id1=");
        serial::put_hex(id1);
        serial::puts(" (expected 0x31, 0x10)\n");
        // Continue anyway - QEMU might have slightly different ID values
        // but the register layout is still PL031
    }

    // Ensure RTC is enabled (bit 0 of RTCCR)
    u32 cr = read_reg(RTCCR);
    if (!(cr & 1)) {
        write_reg(RTCCR, cr | 1);
        serial::puts("[pl031] RTC enabled\n");
    }

    // Read and display current time
    u32 current = read_reg(RTCDR);
    serial::puts("[pl031] Current RTC time: ");
    serial::put_dec(current);
    serial::puts(" seconds since epoch\n");

    initialized = true;
    serial::puts("[pl031] RTC initialized (wall-clock available)\n");

    return true;
}

bool is_available() {
    return initialized;
}

u64 read_time() {
    if (!initialized) {
        return 0;
    }
    return static_cast<u64>(read_reg(RTCDR));
}

} // namespace pl031
