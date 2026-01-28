//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/include/constants.hpp
// Purpose: Centralized kernel-wide constants (memory, hardware, limits).
// Key invariants: Constants grouped by namespace; values immutable at runtime.
// Ownership/Lifetime: Header-only; constexpr definitions only.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

/**
 * @file constants.hpp
 * @brief Centralized kernel-wide constants for ViperDOS.
 *
 * @details
 * This header consolidates magic numbers and configuration constants that are
 * used across multiple kernel subsystems. Constants are organized into nested
 * namespaces by category for clarity and to avoid naming collisions.
 *
 * Usage:
 * @code
 *   #include "kernel/include/constants.hpp"
 *   namespace kc = kernel::constants;
 *   u64 base = kc::mem::RAM_BASE;
 * @endcode
 *
 * Constants that are specific to a single subsystem and unlikely to be needed
 * elsewhere should remain in their respective headers (e.g., GIC register
 * offsets in gic.cpp, filesystem format magic in format.hpp).
 */

#include "types.hpp"

namespace kernel::constants {

// =============================================================================
// SECTION 1: MEMORY LAYOUT (QEMU virt machine for AArch64)
// =============================================================================
namespace mem {

/// QEMU virt machine RAM start address
constexpr u64 RAM_BASE = 0x40000000;

/// Total system RAM size (128MB for QEMU virt default)
constexpr u64 RAM_SIZE = 128 * 1024 * 1024;

/// Framebuffer base address (reserved region in RAM)
constexpr u64 FB_BASE = 0x41000000;

/// Maximum framebuffer size (9MB for 1920x1080)
constexpr u64 FB_SIZE = 9 * 1024 * 1024;

/// Kernel stack pool base address
constexpr u64 STACK_POOL_BASE = 0x44000000;

/// Start of kernel virtual address space (AArch64 upper half)
constexpr u64 KERNEL_VIRT_BASE = 0xFFFF000000000000ULL;

} // namespace mem

// =============================================================================
// SECTION 2: USER SPACE LAYOUT
// =============================================================================
namespace user {

/// User code segment base (2GB, outside kernel's 1GB block region)
constexpr u64 CODE_BASE = 0x0000'0000'8000'0000ULL;

/// User data segment base (3GB)
constexpr u64 DATA_BASE = 0x0000'0000'C000'0000ULL;

/// User heap starts at 4GB
constexpr u64 HEAP_BASE = 0x0000'0001'0000'0000ULL;

/// User stack top (grows down, ~128TB)
constexpr u64 STACK_TOP = 0x0000'7FFF'FFFF'0000ULL;

/// Default user stack size (1MB)
constexpr u64 STACK_SIZE = 1 * 1024 * 1024;

/// Maximum valid user address (bit 47 must be 0 for user space in AArch64)
constexpr u64 USER_ADDR_MAX = 0x0000'7FFF'FFFF'FFFFULL;

/**
 * @brief Check if an address is in user space (valid for user access).
 *
 * @details
 * In AArch64 with 48-bit VAs, user space addresses have bit 47 = 0.
 * This means valid user addresses are from 0x0 to 0x0000'7FFF'FFFF'FFFF.
 *
 * @param addr Address to check.
 * @return true if address is in user space, false if in kernel space.
 */
constexpr bool is_user_addr(u64 addr) {
    return addr <= USER_ADDR_MAX;
}

/**
 * @brief Check if an address is in kernel space.
 *
 * @param addr Address to check.
 * @return true if address is in kernel space.
 */
constexpr bool is_kernel_addr(u64 addr) {
    return addr > USER_ADDR_MAX;
}

/**
 * @brief Validate a user buffer (address + length doesn't overflow into kernel).
 *
 * @param addr Start address of buffer.
 * @param len Length of buffer in bytes.
 * @return true if entire buffer is in user space.
 */
constexpr bool is_valid_user_buffer(u64 addr, u64 len) {
    // Check for overflow
    if (addr + len < addr)
        return false;
    // Check entire range is in user space
    return is_user_addr(addr) && is_user_addr(addr + len - 1);
}

} // namespace user

// =============================================================================
// SECTION 3: HARDWARE DEVICE ADDRESSES (QEMU virt machine)
// =============================================================================
namespace hw {

// UART (PL011)
constexpr u64 UART_BASE = 0x09000000;
constexpr u32 UART_IRQ = 33;

// GIC (Generic Interrupt Controller)
constexpr u64 GICD_BASE = 0x08000000;   // Distributor
constexpr u64 GICC_BASE = 0x08010000;   // CPU Interface (GICv2)
constexpr u64 GICR_BASE = 0x080A0000;   // Redistributor (GICv3)
constexpr u64 GICR_STRIDE = 0x20000;    // 128KB per CPU
constexpr u64 GICD_SGIR_OFFSET = 0xF00; // Software Generated Interrupt Register

// Firmware Config (QEMU fw_cfg)
constexpr u64 FWCFG_BASE = 0x09020000;

// VirtIO MMIO region
constexpr u64 VIRTIO_MMIO_BASE = 0x0a000000;
constexpr u64 VIRTIO_DEVICE_STRIDE = 0x200; // Spacing between devices
constexpr u32 VIRTIO_IRQ_BASE = 48;         // IRQs 48-79 for devices
constexpr u32 VIRTIO_MAX_DEVICES = 32;

// RTC (PL031)
constexpr u64 RTC_BASE = 0x09010000;
constexpr u32 RTC_IRQ = 34;

// GPIO
constexpr u64 GPIO_BASE = 0x09030000;
constexpr u32 GPIO_IRQ = 35;

// Timer IRQ (architected timer)
constexpr u32 TIMER_IRQ = 30;

} // namespace hw

// =============================================================================
// SECTION 4: PAGE AND BLOCK SIZES
// =============================================================================
namespace page {

/// Page size in bytes (4KB for AArch64 with 4KB granule)
constexpr u64 SIZE = 4096;

/// Log2 of page size (for address calculations)
constexpr u64 SHIFT = 12;

/// Page offset mask (SIZE - 1)
constexpr u64 MASK = SIZE - 1;

/// 2MB block size (large page)
constexpr u64 BLOCK_2MB = 2 * 1024 * 1024;

/// 1GB block size (huge page)
constexpr u64 BLOCK_1GB = 1024 * 1024 * 1024;

/// Entries per page table (512 for 4KB pages with 8-byte descriptors)
constexpr u64 TABLE_ENTRIES = 512;

/// Page table index mask (9 bits)
constexpr u64 TABLE_INDEX_MASK = 0x1FF;

} // namespace page

namespace block {

/// Disk sector size (typically 512 bytes)
constexpr u64 SECTOR_SIZE = 512;

/// Filesystem block size (matches page size)
constexpr u64 FS_BLOCK_SIZE = 4096;

} // namespace block

// =============================================================================
// SECTION 5: KERNEL LIMITS AND CAPACITIES
// =============================================================================
namespace limits {

// ----- Stack Sizes -----

/// Kernel stack size per task (16KB)
constexpr u64 KERNEL_STACK_SIZE = 16 * 1024;

/// Guard page size for stack overflow detection
constexpr u64 GUARD_PAGE_SIZE = 4096;

// ----- Memory Limits -----

/// Default per-process memory limit (64MB)
constexpr u64 DEFAULT_MEMORY_LIMIT = 64 * 1024 * 1024;

/// Maximum single allocation size (16MB)
constexpr u64 MAX_ALLOCATION_SIZE = 16 * 1024 * 1024;

// ----- Path and String Limits -----

/// Maximum pathname length
constexpr u32 MAX_PATH = 256;

/// Maximum assign name length
constexpr u32 MAX_ASSIGN_NAME = 31;

// ----- Task and Process Limits -----

/// Maximum number of tasks
constexpr u32 MAX_TASKS = 64;

/// Maximum number of CPUs supported
constexpr u32 MAX_CPUS = 8;

/// Maximum number of Viper processes
constexpr u32 MAX_VIPERS = 64;

// ----- IPC Limits -----

/// Maximum number of channels
constexpr u32 MAX_CHANNELS = 64;

/// Maximum message size in bytes
constexpr u32 MAX_MSG_SIZE = 8192;

/// Maximum handles transferred per message
constexpr u32 MAX_HANDLES_PER_MSG = 4;

/// Default pending message queue depth
constexpr u32 DEFAULT_PENDING_MSGS = 16;

/// Maximum pending messages per channel
constexpr u32 MAX_PENDING_MSGS = 64;

/// Maximum events per poll call
constexpr u32 MAX_POLL_EVENTS = 16;

// ----- Filesystem Limits -----

/// Maximum direct block pointers in inode
constexpr u32 MAX_DIRECT_BLOCKS = 12;

/// Inode cache size
constexpr u32 INODE_CACHE_SIZE = 256;

/// Block cache size (in blocks, 256KB with 4KB blocks)
constexpr u32 BLOCK_CACHE_SIZE = 64;

/// Maximum assigns in assign table
constexpr u32 MAX_ASSIGNS = 64;

// ----- Capability Limits -----

/// Default capability table capacity
constexpr u32 DEFAULT_CAP_CAPACITY = 256;

/// Default handle limit per process
constexpr u32 DEFAULT_HANDLE_LIMIT = 1024;

// ----- IRQ Limits -----

/// Maximum number of IRQs (GIC limit)
constexpr u32 MAX_IRQS = 256;

/// Start of SPI interrupts (after SGIs and PPIs)
constexpr u32 SPI_START = 32;

} // namespace limits

// =============================================================================
// SECTION 6: SPECIAL HANDLES AND SENTINELS
// =============================================================================
namespace handle {

/// Invalid handle sentinel
constexpr u32 INVALID = 0xFFFFFFFF;

/// No parent sentinel (for capability tree root)
constexpr u32 NO_PARENT = 0xFFFFFFFF;

/// Pseudo-handle for console input
constexpr u32 CONSOLE_INPUT = 0xFFFF0001;

/// Pseudo-handle for network receive
constexpr u32 NETWORK_RX = 0xFFFF0002;

/// Handle index mask (24 bits)
constexpr u32 INDEX_MASK = 0x00FFFFFF;

/// Generation mask (8 bits)
constexpr u32 GEN_MASK = 0xFF;

/// Generation shift
constexpr u32 GEN_SHIFT = 24;

} // namespace handle

// =============================================================================
// SECTION 7: DISPLAY AND GRAPHICS
// =============================================================================
namespace display {

/// Default framebuffer width
constexpr u32 DEFAULT_WIDTH = 1024;

/// Default framebuffer height
constexpr u32 DEFAULT_HEIGHT = 768;

/// Default bits per pixel
constexpr u32 DEFAULT_BPP = 32;

/// Border width in pixels (for console frame)
constexpr u32 BORDER_WIDTH = 20;

/// Padding between border and text
constexpr u32 BORDER_PADDING = 8;

/// Total inset from edge to text
constexpr u32 TEXT_INSET = BORDER_WIDTH + BORDER_PADDING;

/// Base font width (unscaled)
constexpr u32 FONT_BASE_WIDTH = 8;

/// Base font height (unscaled)
constexpr u32 FONT_BASE_HEIGHT = 16;

/// Font scale numerator (3/2 = 1.5x)
constexpr u32 FONT_SCALE_NUM = 3;

/// Font scale denominator
constexpr u32 FONT_SCALE_DEN = 2;

/// Cursor blink interval in milliseconds
constexpr u32 CURSOR_BLINK_MS = 500;

/// Scrollback buffer size in lines
constexpr u32 SCROLLBACK_LINES = 512;

/// Maximum columns per line in scrollback buffer
constexpr u32 SCROLLBACK_COLS = 128;

} // namespace display

// =============================================================================
// SECTION 8: COLORS (ARGB format: 0xAARRGGBB)
// =============================================================================
namespace color {

// Standard ANSI colors
constexpr u32 BLACK = 0xFF000000;
constexpr u32 RED = 0xFFCC3333;
constexpr u32 GREEN = 0xFF00AA44;
constexpr u32 YELLOW = 0xFFCCAA00;
constexpr u32 BLUE = 0xFF3366CC;
constexpr u32 MAGENTA = 0xFFCC33CC;
constexpr u32 CYAN = 0xFF33CCCC;
constexpr u32 WHITE = 0xFFEEEEEE;
constexpr u32 GRAY = 0xFF666666;

// Bright variants
constexpr u32 BRIGHT_RED = 0xFFFF6666;
constexpr u32 BRIGHT_GREEN = 0xFF66FF66;
constexpr u32 BRIGHT_YELLOW = 0xFFFFFF66;
constexpr u32 BRIGHT_BLUE = 0xFF6699FF;
constexpr u32 BRIGHT_MAGENTA = 0xFFFF66FF;
constexpr u32 BRIGHT_CYAN = 0xFF66FFFF;
constexpr u32 BRIGHT_WHITE = 0xFFFFFFFF;

// Viper theme colors
constexpr u32 VIPER_GREEN = 0xFF00AA44;
constexpr u32 VIPER_DARK_BROWN = 0xFF1A1208;
constexpr u32 VIPER_YELLOW = 0xFFFFDD00;
constexpr u32 VIPER_RED = 0xFFCC3333;
constexpr u32 VIPER_WHITE = 0xFFEEEEEE;
constexpr u32 VIPER_BLUE = 0xFF0055AA; // Workbench blue (matches desktop)

} // namespace color

// =============================================================================
// SECTION 9: TIMING
// =============================================================================
namespace timing {

/// Default network timeout in milliseconds
constexpr u32 DEFAULT_NETWORK_TIMEOUT_MS = 5000;

/// ICMP ping timeout in milliseconds
constexpr u32 PING_TIMEOUT_MS = 3000;

/// Interrupt polling wait iterations
constexpr u32 INTERRUPT_WAIT_ITERS = 100000;

/// Timer wheel level 0 slots
constexpr u32 TIMER_WHEEL_SLOTS = 256;

/// Default scheduler time slice in ticks (10ms at 1000Hz)
constexpr u32 DEFAULT_TIME_SLICE = 10;

/// Real-time default time slice in ticks (100ms)
constexpr u32 RT_TIME_SLICE = 100;

} // namespace timing

// =============================================================================
// SECTION 10: DEBUG MAGIC NUMBERS
// =============================================================================
namespace magic {

/// Heap block allocated marker
constexpr u32 HEAP_ALLOCATED = 0xCAFEBABE;

/// Heap block freed marker
constexpr u32 HEAP_FREED = 0xDEADBEEF;

/// Heap block poisoned marker (double-free detection)
constexpr u32 HEAP_POISONED = 0xFEEDFACE;

/// ViperFS superblock magic ("VPFS")
constexpr u32 VIPERFS_MAGIC = 0x53465056;

/// Journal magic ("JRNL")
constexpr u32 JOURNAL_MAGIC = 0x4A524E4C;

/// Flattened Device Tree magic
constexpr u32 FDT_MAGIC = 0xD00DFEED;

/// QEMU fw_cfg signature ("QEMU")
constexpr u32 FWCFG_QEMU = 0x554D4551;

/// VBoot magic ("VIPER\0")
constexpr u64 VBOOT_MAGIC = 0x564950455200ULL;

/// DRM format XRGB8888 ("XR24")
constexpr u32 DRM_FORMAT_XRGB8888 = 0x34325258;

} // namespace magic

// =============================================================================
// SECTION 11: SCHEDULER CONSTANTS
// =============================================================================
namespace sched {

/// Number of priority queues
constexpr u8 NUM_PRIORITY_QUEUES = 8;

/// Priorities per queue (256 / 8)
constexpr u8 PRIORITIES_PER_QUEUE = 32;

/// Highest priority (most urgent)
constexpr u8 PRIORITY_HIGHEST = 0;

/// Default priority for normal tasks
constexpr u8 PRIORITY_DEFAULT = 128;

/// Lowest priority (idle task)
constexpr u8 PRIORITY_LOWEST = 255;

/// Minimum tasks before work stealing
constexpr u32 STEAL_THRESHOLD = 2;

/// Queue to start stealing from (skip high-priority queues)
constexpr u8 STEAL_START_QUEUE = 4;

} // namespace sched

// =============================================================================
// SECTION 12: FILE OPEN FLAGS (POSIX-compatible)
// =============================================================================
namespace file {

constexpr u32 O_RDONLY = 0x0000;
constexpr u32 O_WRONLY = 0x0001;
constexpr u32 O_RDWR = 0x0002;
constexpr u32 O_CREAT = 0x0040;
constexpr u32 O_TRUNC = 0x0200;
constexpr u32 O_APPEND = 0x0400;

// File type mask and values
constexpr u32 TYPE_MASK = 0xF000;
constexpr u32 TYPE_FILE = 0x8000;
constexpr u32 TYPE_DIR = 0x4000;
constexpr u32 TYPE_LINK = 0xA000;

// Permission bits
constexpr u32 PERM_READ = 0x0004;
constexpr u32 PERM_WRITE = 0x0002;
constexpr u32 PERM_EXEC = 0x0001;

} // namespace file

// =============================================================================
// SECTION 13: KEYBOARD MODIFIERS
// =============================================================================
namespace keyboard {

constexpr u8 MOD_SHIFT = 0x01;
constexpr u8 MOD_CTRL = 0x02;
constexpr u8 MOD_ALT = 0x04;
constexpr u8 MOD_META = 0x08;
constexpr u8 MOD_CAPS_LOCK = 0x10;

} // namespace keyboard

// =============================================================================
// SECTION 14: SIGNAL NUMBERS (POSIX subset)
// =============================================================================
namespace signal {

constexpr i32 SIGKILL = 9;  // Kill (cannot be caught)
constexpr i32 SIGTERM = 15; // Termination
constexpr i32 SIGCONT = 18; // Continue if stopped
constexpr i32 SIGSTOP = 19; // Stop (cannot be caught)

} // namespace signal

// =============================================================================
// SECTION 15: ANSI ESCAPE SEQUENCES
// =============================================================================
namespace ansi {

/// ESC character (0x1B)
constexpr char ESC = '\033';

/// Control Sequence Introducer (ESC[)
constexpr const char *CSI = "\033[";

// ----- Cursor Movement -----
constexpr const char *CURSOR_UP = "\033[A";
constexpr const char *CURSOR_DOWN = "\033[B";
constexpr const char *CURSOR_RIGHT = "\033[C";
constexpr const char *CURSOR_LEFT = "\033[D";
constexpr const char *CURSOR_HOME = "\033[H";
constexpr const char *CURSOR_END = "\033[F";

// ----- Cursor Visibility (DECTCEM) -----
constexpr const char *CURSOR_SHOW = "\033[?25h";
constexpr const char *CURSOR_HIDE = "\033[?25l";

// ----- Screen/Line Clearing -----
constexpr const char *CLEAR_SCREEN = "\033[2J";
constexpr const char *CLEAR_LINE = "\033[K";
constexpr const char *CLEAR_LINE_FULL = "\033[2K";

// ----- Text Attributes -----
constexpr const char *RESET = "\033[0m";
constexpr const char *BOLD = "\033[1m";
constexpr const char *DIM = "\033[2m";
constexpr const char *ITALIC = "\033[3m";
constexpr const char *UNDERLINE = "\033[4m";
constexpr const char *REVERSE = "\033[7m";

// ----- Foreground Colors (ANSI 16-color) -----
constexpr const char *FG_BLACK = "\033[30m";
constexpr const char *FG_RED = "\033[31m";
constexpr const char *FG_GREEN = "\033[32m";
constexpr const char *FG_YELLOW = "\033[33m";
constexpr const char *FG_BLUE = "\033[34m";
constexpr const char *FG_MAGENTA = "\033[35m";
constexpr const char *FG_CYAN = "\033[36m";
constexpr const char *FG_WHITE = "\033[37m";
constexpr const char *FG_DEFAULT = "\033[39m";

// ----- Bright Foreground Colors -----
constexpr const char *FG_BRIGHT_BLACK = "\033[90m";
constexpr const char *FG_BRIGHT_RED = "\033[91m";
constexpr const char *FG_BRIGHT_GREEN = "\033[92m";
constexpr const char *FG_BRIGHT_YELLOW = "\033[93m";
constexpr const char *FG_BRIGHT_BLUE = "\033[94m";
constexpr const char *FG_BRIGHT_MAGENTA = "\033[95m";
constexpr const char *FG_BRIGHT_CYAN = "\033[96m";
constexpr const char *FG_BRIGHT_WHITE = "\033[97m";

// ----- Background Colors -----
constexpr const char *BG_BLACK = "\033[40m";
constexpr const char *BG_RED = "\033[41m";
constexpr const char *BG_GREEN = "\033[42m";
constexpr const char *BG_YELLOW = "\033[43m";
constexpr const char *BG_BLUE = "\033[44m";
constexpr const char *BG_MAGENTA = "\033[45m";
constexpr const char *BG_CYAN = "\033[46m";
constexpr const char *BG_WHITE = "\033[47m";
constexpr const char *BG_DEFAULT = "\033[49m";

// ----- Special Keys (as escape sequences) -----
constexpr const char *KEY_DELETE = "\033[3~";
constexpr const char *KEY_PAGE_UP = "\033[5~";
constexpr const char *KEY_PAGE_DOWN = "\033[6~";

} // namespace ansi

// =============================================================================
// SECTION 16: SYSTEM MOUNT POINTS
// =============================================================================
namespace mount {

/// System disk mount point prefix
constexpr const char *SYS_PREFIX = "/sys/";

/// Length of system prefix (including trailing slash)
constexpr usize SYS_PREFIX_LEN = 5;

/// Path to vinit on system disk
constexpr const char *VINIT_PATH = "/sys/vinit.sys";

/// Path to blkd server on system disk
constexpr const char *BLKD_PATH = "/sys/blkd.sys";

/// Path to fsd server on system disk
constexpr const char *FSD_PATH = "/sys/fsd.sys";

/// Path to netd server on system disk
constexpr const char *NETD_PATH = "/sys/netd.sys";

/// Path to consoled server on system disk
constexpr const char *CONSOLED_PATH = "/sys/consoled.sys";

/// Path to inputd server on system disk
constexpr const char *INPUTD_PATH = "/sys/inputd.sys";

/// Path to displayd server on system disk
constexpr const char *DISPLAYD_PATH = "/sys/displayd.sys";

} // namespace mount

// =============================================================================
// SECTION 17: NETWORK STACK PARAMETERS
// =============================================================================
namespace net {

// ----- Port Ranges -----

/// Starting ephemeral port for outbound connections
constexpr u16 EPHEMERAL_PORT_START = 49152;

/// Maximum ephemeral port value
constexpr u16 EPHEMERAL_PORT_MAX = 65534;

/// DNS server port
constexpr u16 DNS_PORT = 53;

// ----- Buffer Sizes -----

/// Network receive buffer for polling
constexpr u32 RX_BUFFER_SIZE = 2048;

/// Maximum Ethernet frame size (1500 + headers)
constexpr u32 FRAME_MAX_SIZE = 1518;

/// Maximum IP packet payload
constexpr u32 IP_PACKET_MAX = 1500;

/// Maximum TCP segment data size
constexpr u32 TCP_SEGMENT_MAX = 1460;

/// Maximum TCP data chunk per send
constexpr u32 TCP_MAX_CHUNK = 1400;

/// Maximum UDP datagram size
constexpr u32 UDP_DATAGRAM_MAX = 1472;

/// DNS query buffer size
constexpr u32 DNS_QUERY_BUFFER_SIZE = 256;

/// ARP frame buffer size
constexpr u32 ARP_FRAME_SIZE = 64;

/// ICMP buffer size
constexpr u32 ICMP_BUFFER_SIZE = 64;

/// ICMP data payload size
constexpr u32 ICMP_DATA_SIZE = 56;

// ----- Connection Limits -----

/// Maximum TCP connections
constexpr u32 MAX_TCP_CONNS = 32;

/// Maximum UDP sockets
constexpr u32 MAX_UDP_SOCKETS = 16;

/// TCP receive buffer size per connection
constexpr u32 TCP_RX_BUFFER_SIZE = 32768;

/// TCP transmit buffer size per connection
constexpr u32 TCP_TX_BUFFER_SIZE = 8192;

/// TCP backlog queue size (pending connections)
constexpr u32 TCP_BACKLOG_SIZE = 8;

/// UDP receive buffer size per socket
constexpr u32 UDP_RX_BUFFER_SIZE = 4096;

/// ARP cache size (entries)
constexpr u32 ARP_CACHE_SIZE = 16;

// ----- Timeouts and Retries -----

/// TCP connect poll iterations (legacy, kept for reference)
constexpr u32 TCP_CONNECT_POLL_ITERATIONS = 2000;

/// TCP connect timeout in milliseconds
constexpr u32 TCP_CONNECT_TIMEOUT_MS = 5000;

/// DNS resolution poll iterations
constexpr u32 DNS_POLL_ITERATIONS = 5000;

/// TCP close grace period poll count
constexpr u32 TCP_CLOSE_POLL_ITERATIONS = 20;

/// ARP request retry poll count
constexpr u32 ARP_REQUEST_POLL_ITERATIONS = 200;

/// ICMP reply poll count
constexpr u32 ICMP_POLL_ITERATIONS = 100;

/// Busy-wait delay iteration count (CPU yield)
constexpr u32 BUSY_WAIT_ITERATIONS = 50000;

/// TCP/DNS retry attempt count
constexpr u32 CONNECT_RETRY_COUNT = 5;

// ----- Protocol Defaults -----

/// Default IP TTL
constexpr u8 IP_TTL_DEFAULT = 64;

} // namespace net

// =============================================================================
// SECTION 18: VIRTIO DRIVER PARAMETERS
// =============================================================================
namespace virtio {

// ----- Network Driver -----

/// Number of RX buffers for virtio-net
constexpr u32 NET_RX_BUFFER_COUNT = 32;

/// Size of each RX buffer
constexpr u32 NET_RX_BUFFER_SIZE = 2048;

/// RX queue size (descriptors)
constexpr u32 NET_RX_QUEUE_SIZE = 16;

/// RX/TX virtqueue size
constexpr u32 NET_VIRTQUEUE_SIZE = 128;

/// Network init poll iterations
constexpr u32 NET_INIT_POLL_ITERATIONS = 1000;

// ----- Block Driver -----

/// System disk capacity (sectors)
constexpr u32 SYSTEM_DISK_SECTORS = 4096;

/// User disk capacity (sectors)
constexpr u32 USER_DISK_SECTORS = 16384;

/// Block I/O polling timeout iterations
constexpr u32 BLK_POLLING_TIMEOUT = 10000000;

/// Block device virtqueue size
constexpr u32 BLK_VIRTQUEUE_SIZE = 128;

// ----- GPU Driver -----

/// Max GPU scanouts (displays)
constexpr u32 GPU_MAX_SCANOUTS = 16;

/// GPU command buffer size
constexpr u32 GPU_CMD_BUF_SIZE = 4096;

/// GPU response buffer size
constexpr u32 GPU_RESP_BUF_SIZE = 4096;

/// GPU control virtqueue size
constexpr u32 GPU_CONTROLQ_SIZE = 64;

/// GPU cursor virtqueue size
constexpr u32 GPU_CURSORQ_SIZE = 16;

// ----- Input Driver -----

/// Input event buffers count
constexpr u32 INPUT_EVENT_BUFFERS = 64;

/// Input config string/bitmap size
constexpr u32 INPUT_CONFIG_SIZE = 128;

// ----- RNG Driver -----

/// RNG buffer size
constexpr u32 RNG_BUFFER_SIZE = 256;

/// RNG polling timeout iterations
constexpr u32 RNG_POLLING_TIMEOUT = 100000;

// ----- Common -----

/// VirtQueue ring alignment
constexpr u32 RING_ALIGNMENT = 4096;

/// Guest page size (legacy virtio)
constexpr u32 GUEST_PAGE_SIZE = 4096;

} // namespace virtio

// =============================================================================
// SECTION 19: CFS SCHEDULER PARAMETERS
// =============================================================================
namespace cfs {

/// Minimum scheduling granularity (microseconds)
constexpr u32 MIN_GRANULARITY_US = 750;

/// Target latency for all runnable tasks (microseconds)
constexpr u32 TARGET_LATENCY_US = 6000;

/// Default weight for nice 0 tasks
constexpr u32 WEIGHT_DEFAULT = 1024;

/// Vruntime calculation shift factor
constexpr u32 VRUNTIME_SHIFT = 22;

} // namespace cfs

// =============================================================================
// SECTION 20: DEADLINE SCHEDULER PARAMETERS
// =============================================================================
namespace deadline {

/// Bandwidth fraction denominator (0.1% = 1/1000)
constexpr u32 BANDWIDTH_FRACTION = 1000;

/// Maximum total bandwidth (95% of capacity)
constexpr u32 MAX_TOTAL_BANDWIDTH = 950;

} // namespace deadline

// =============================================================================
// SECTION 21: MEMORY MANAGEMENT PARAMETERS
// =============================================================================
namespace vmm {

/// Maximum VMAs per address space
constexpr u32 MAX_VMAS = 64;

/// Maximum stack size per process (8MB)
constexpr u64 MAX_STACK_SIZE = 8 * 1024 * 1024;

/// Stack guard page size
constexpr u64 STACK_GUARD_PAGE_SIZE = 4096;

/// Maximum kernel heap size
constexpr u64 KHEAP_MAX_SIZE = 64 * 1024 * 1024;

/// Bitmap word bit count
constexpr u32 BITMAP_WORD_BITS = 64;

/// Maximum ASID count
constexpr u32 MAX_ASID = 256;

} // namespace vmm

// =============================================================================
// SECTION 22: SYSCALL PARAMETERS
// =============================================================================
namespace sys {

/// Maximum shared memory mappings per process
constexpr u32 MAX_SHM_MAPPINGS = 256;

/// Maximum DMA allocations
constexpr u32 MAX_DMA_ALLOCATIONS = 64;

/// Maximum heap allocation size (64MB)
constexpr u64 MAX_HEAP_ALLOC = 64 * 1024 * 1024;

/// Maximum framebuffer width
constexpr u32 MAX_FB_WIDTH = 8192;

/// Maximum framebuffer height
constexpr u32 MAX_FB_HEIGHT = 8192;

/// Maximum message log size for validation
constexpr u32 MAX_MSG_LOG_SIZE = 4096;

} // namespace sys

// =============================================================================
// SECTION 23: CONSOLE AND TTY PARAMETERS
// =============================================================================
namespace console {

/// Console input buffer size
constexpr u32 INPUT_BUFFER_SIZE = 1024;

/// Console line buffer size
constexpr u32 LINE_BUFFER_SIZE = 256;

/// TTY input buffer size
constexpr u32 TTY_BUFFER_SIZE = 256;

/// Input character translation buffer size
constexpr u32 CHAR_BUFFER_SIZE = 256;

/// Input event queue size
constexpr u32 EVENT_QUEUE_SIZE = 64;

} // namespace console

// =============================================================================
// SECTION 24: FILESYSTEM PARAMETERS
// =============================================================================
namespace fs {

/// Maximum path component stack depth for traversal
constexpr u32 MAX_PATH_STACK_DEPTH = 64;

/// Filename buffer size
constexpr u32 FILENAME_BUFFER_SIZE = 256;

/// ViperFS inode size
constexpr u32 VIPERFS_INODE_SIZE = 256;

/// Maximum journal block records
constexpr u32 MAX_JOURNAL_BLOCKS = 32;

} // namespace fs

// =============================================================================
// SECTION 25: BOOT PARAMETERS
// =============================================================================
namespace boot {

/// Maximum memory regions in boot info
constexpr u32 MAX_MEMORY_REGIONS = 64;

} // namespace boot

// =============================================================================
// SECTION 26: CPU PARAMETERS
// =============================================================================
namespace cpu {

/// CPU per-core stack size
constexpr u64 STACK_SIZE = 16384;

/// Cache line size for flush operations
constexpr u32 CACHE_LINE_SIZE = 64;

} // namespace cpu

} // namespace kernel::constants

// Convenience namespace alias for shorter access
namespace kc = kernel::constants;
