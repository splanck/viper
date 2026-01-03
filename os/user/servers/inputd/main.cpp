//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/servers/inputd/main.cpp
// Purpose: Input server (inputd) main entry point.
// Key invariants: Uses VirtIO-input; registered as "INPUTD:" service.
// Ownership/Lifetime: Long-running service process.
// Links: user/servers/inputd/input_protocol.hpp
//
//===----------------------------------------------------------------------===//

/**
 * @file main.cpp
 * @brief Input server (inputd) main entry point.
 *
 * @details
 * This server provides keyboard and mouse input services to user-space
 * processes via IPC. It:
 * - Finds and initializes a VirtIO-input device (keyboard)
 * - Translates key events to ASCII characters
 * - Creates a service channel
 * - Registers with the assign system as "INPUTD:"
 * - Handles input queries from clients
 */

#include "../../libvirtio/include/device.hpp"
#include "../../syscall.hpp"
#include "input_protocol.hpp"
#include "keycodes.hpp"

using namespace input_protocol;

// Debug output helpers
static void debug_print(const char *msg)
{
    sys::print(msg);
}

static void debug_print_hex(uint64_t val)
{
    char buf[17];
    const char *hex = "0123456789abcdef";
    for (int i = 15; i >= 0; i--)
    {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[16] = '\0';
    sys::print(buf);
}

static void debug_print_dec(uint64_t val)
{
    if (val == 0)
    {
        sys::print("0");
        return;
    }
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    while (val > 0 && i > 0)
    {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    sys::print(&buf[i]);
}

// VirtIO input device registers
namespace virtio_input
{
constexpr uint32_t MAGIC_VALUE = 0x74726976; // "virt"
constexpr uint32_t DEVICE_ID_INPUT = 18;

// Event types
constexpr uint16_t EV_KEY = 0x01;

// VirtIO input event structure
struct InputEvent
{
    uint16_t type;
    uint16_t code;
    uint32_t value;
};
} // namespace virtio_input

// VirtIO register offsets (only used ones)
namespace reg
{
constexpr uint32_t DEVICE_ID = 0x008;
constexpr uint32_t DRIVER_FEATURES = 0x020;
constexpr uint32_t DRIVER_FEATURES_SEL = 0x024;
constexpr uint32_t QUEUE_SEL = 0x030;
constexpr uint32_t QUEUE_NUM_MAX = 0x034;
constexpr uint32_t QUEUE_NUM = 0x038;
constexpr uint32_t QUEUE_READY = 0x044;
constexpr uint32_t QUEUE_NOTIFY = 0x050;
constexpr uint32_t INTERRUPT_STATUS = 0x060;
constexpr uint32_t INTERRUPT_ACK = 0x064;
constexpr uint32_t STATUS = 0x070;
constexpr uint32_t QUEUE_DESC_LOW = 0x080;
constexpr uint32_t QUEUE_DESC_HIGH = 0x084;
constexpr uint32_t QUEUE_DRIVER_LOW = 0x090;
constexpr uint32_t QUEUE_DRIVER_HIGH = 0x094;
constexpr uint32_t QUEUE_DEVICE_LOW = 0x0A0;
constexpr uint32_t QUEUE_DEVICE_HIGH = 0x0A4;
} // namespace reg

// Status bits
namespace status
{
constexpr uint32_t ACKNOWLEDGE = 1;
constexpr uint32_t DRIVER = 2;
constexpr uint32_t DRIVER_OK = 4;
constexpr uint32_t FEATURES_OK = 8;
} // namespace status

// VirtQueue descriptor flags
namespace vq_flags
{
constexpr uint16_t WRITE = 2;
} // namespace vq_flags

// VirtQueue structures
struct VirtqDesc
{
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct VirtqAvail
{
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[64];
};

struct VirtqUsed
{
    uint16_t flags;
    uint16_t idx;

    struct
    {
        uint32_t id;
        uint32_t len;
    } ring[64];
};

// Global state
static volatile uint32_t *g_mmio = nullptr;
static uint64_t g_mmio_phys = 0;
static uint32_t g_irq = 0;
static int32_t g_service_channel = -1;

// Event queue (queue 0)
static VirtqDesc *g_event_desc = nullptr;
static VirtqAvail *g_event_avail = nullptr;
static VirtqUsed *g_event_used = nullptr;
static virtio_input::InputEvent *g_event_buffers = nullptr;
static uint16_t g_event_last_used = 0;

// Input state
static constexpr size_t EVENT_QUEUE_SIZE = 64;
static constexpr size_t CHAR_BUFFER_SIZE = 256;

static input_protocol::InputEvent g_event_queue[EVENT_QUEUE_SIZE];
static volatile size_t g_event_head = 0;
static volatile size_t g_event_tail = 0;

static char g_char_buffer[CHAR_BUFFER_SIZE];
static volatile size_t g_char_head = 0;
static volatile size_t g_char_tail = 0;

static uint8_t g_modifiers = 0;
static bool g_caps_lock = false;

// QEMU virt machine VirtIO IRQ base
constexpr uint32_t VIRTIO_IRQ_BASE = 48;

static void recv_bootstrap_caps()
{
    constexpr int32_t BOOTSTRAP_RECV = 0;
    uint8_t dummy[1];
    uint32_t handles[4];
    uint32_t handle_count = 4;

    for (uint32_t i = 0; i < 2000; i++)
    {
        handle_count = 4;
        int64_t n = sys::channel_recv(BOOTSTRAP_RECV, dummy, sizeof(dummy), handles, &handle_count);
        if (n >= 0)
        {
            sys::channel_close(BOOTSTRAP_RECV);
            return;
        }
        if (n == VERR_WOULD_BLOCK)
        {
            sys::yield();
            continue;
        }
        return;
    }
}

/**
 * @brief Find VirtIO-input device in the system.
 */
static bool find_input_device(uint64_t *mmio_phys, uint32_t *irq)
{
    constexpr uint64_t VIRTIO_BASE = 0x0a000000;
    constexpr uint64_t VIRTIO_END = 0x0a004000;
    constexpr uint64_t VIRTIO_STRIDE = 0x200;

    for (uint64_t addr = VIRTIO_BASE; addr < VIRTIO_END; addr += VIRTIO_STRIDE)
    {
        uint64_t virt = device::map_device(addr, VIRTIO_STRIDE);
        if (virt == 0)
            continue;

        volatile uint32_t *mmio = reinterpret_cast<volatile uint32_t *>(virt);

        // Check magic
        if (mmio[0] != virtio_input::MAGIC_VALUE)
            continue;

        // Check device type
        uint32_t device_id = mmio[reg::DEVICE_ID / 4];
        if (device_id != virtio_input::DEVICE_ID_INPUT)
            continue;

        // Skip already configured devices
        uint32_t dev_status = mmio[reg::STATUS / 4];
        if (dev_status != 0)
            continue;

        *mmio_phys = addr;
        *irq = VIRTIO_IRQ_BASE + static_cast<uint32_t>((addr - VIRTIO_BASE) / VIRTIO_STRIDE);

        debug_print("[inputd] Found VirtIO-input at 0x");
        debug_print_hex(addr);
        debug_print(" IRQ ");
        debug_print_dec(*irq);
        debug_print("\n");

        g_mmio = mmio;
        g_mmio_phys = addr;
        g_irq = *irq;
        return true;
    }
    return false;
}

/**
 * @brief Initialize the VirtIO input device.
 */
static bool init_device()
{
    if (!g_mmio)
        return false;

    // Reset
    g_mmio[reg::STATUS / 4] = 0;
    asm volatile("dsb sy" ::: "memory");

    // Acknowledge
    g_mmio[reg::STATUS / 4] = status::ACKNOWLEDGE;
    asm volatile("dsb sy" ::: "memory");

    // Driver
    g_mmio[reg::STATUS / 4] = status::ACKNOWLEDGE | status::DRIVER;
    asm volatile("dsb sy" ::: "memory");

    // Negotiate features (none needed for basic input)
    g_mmio[reg::DRIVER_FEATURES_SEL / 4] = 0;
    g_mmio[reg::DRIVER_FEATURES / 4] = 0;
    asm volatile("dsb sy" ::: "memory");

    // Features OK
    g_mmio[reg::STATUS / 4] = status::ACKNOWLEDGE | status::DRIVER | status::FEATURES_OK;
    asm volatile("dsb sy" ::: "memory");

    if (!(g_mmio[reg::STATUS / 4] & status::FEATURES_OK))
    {
        debug_print("[inputd] Features negotiation failed\n");
        return false;
    }

    // Set up event queue (queue 0)
    g_mmio[reg::QUEUE_SEL / 4] = 0;
    asm volatile("dsb sy" ::: "memory");

    uint32_t queue_max = g_mmio[reg::QUEUE_NUM_MAX / 4];
    if (queue_max == 0)
    {
        debug_print("[inputd] No event queue available\n");
        return false;
    }

    uint32_t queue_size = (queue_max < 64) ? queue_max : 64;
    g_mmio[reg::QUEUE_NUM / 4] = queue_size;
    asm volatile("dsb sy" ::: "memory");

    // Allocate queue memory via DMA
    size_t desc_size = queue_size * sizeof(VirtqDesc);
    size_t avail_size = sizeof(VirtqAvail);
    size_t used_size = sizeof(VirtqUsed);
    size_t event_size = queue_size * sizeof(virtio_input::InputEvent);
    size_t total_size =
        desc_size + avail_size + used_size + event_size + 4096; // Extra for alignment

    device::DmaBuffer dma_buf;
    if (device::dma_alloc(total_size, &dma_buf) < 0)
    {
        debug_print("[inputd] DMA allocation failed\n");
        return false;
    }

    // Zero the memory
    uint8_t *p = reinterpret_cast<uint8_t *>(dma_buf.virt_addr);
    for (size_t i = 0; i < total_size; i++)
        p[i] = 0;

    // Assign structures
    g_event_desc = reinterpret_cast<VirtqDesc *>(p);
    p += desc_size;
    g_event_avail = reinterpret_cast<VirtqAvail *>(p);
    p += avail_size;
    // Align used ring to 4 bytes
    p = reinterpret_cast<uint8_t *>((reinterpret_cast<uint64_t>(p) + 3) & ~3ULL);
    g_event_used = reinterpret_cast<VirtqUsed *>(p);
    p += used_size;
    // Align event buffers
    p = reinterpret_cast<uint8_t *>((reinterpret_cast<uint64_t>(p) + 7) & ~7ULL);
    g_event_buffers = reinterpret_cast<virtio_input::InputEvent *>(p);

    // Get physical addresses
    uint64_t desc_phys = device::virt_to_phys(reinterpret_cast<u64>(g_event_desc));
    uint64_t avail_phys = device::virt_to_phys(reinterpret_cast<u64>(g_event_avail));
    uint64_t used_phys = device::virt_to_phys(reinterpret_cast<u64>(g_event_used));
    uint64_t event_phys = device::virt_to_phys(reinterpret_cast<u64>(g_event_buffers));

    // Set up descriptors - each points to an event buffer for device to write
    for (uint32_t i = 0; i < queue_size; i++)
    {
        g_event_desc[i].addr = event_phys + i * sizeof(virtio_input::InputEvent);
        g_event_desc[i].len = sizeof(virtio_input::InputEvent);
        g_event_desc[i].flags = vq_flags::WRITE;
        g_event_desc[i].next = 0;

        // Add to available ring
        g_event_avail->ring[i] = static_cast<uint16_t>(i);
    }
    g_event_avail->idx = queue_size;
    g_event_last_used = 0;

    // Configure queue addresses
    g_mmio[reg::QUEUE_DESC_LOW / 4] = static_cast<uint32_t>(desc_phys);
    g_mmio[reg::QUEUE_DESC_HIGH / 4] = static_cast<uint32_t>(desc_phys >> 32);
    g_mmio[reg::QUEUE_DRIVER_LOW / 4] = static_cast<uint32_t>(avail_phys);
    g_mmio[reg::QUEUE_DRIVER_HIGH / 4] = static_cast<uint32_t>(avail_phys >> 32);
    g_mmio[reg::QUEUE_DEVICE_LOW / 4] = static_cast<uint32_t>(used_phys);
    g_mmio[reg::QUEUE_DEVICE_HIGH / 4] = static_cast<uint32_t>(used_phys >> 32);
    asm volatile("dsb sy" ::: "memory");

    // Enable queue
    g_mmio[reg::QUEUE_READY / 4] = 1;
    asm volatile("dsb sy" ::: "memory");

    // Driver OK
    g_mmio[reg::STATUS / 4] =
        status::ACKNOWLEDGE | status::DRIVER | status::FEATURES_OK | status::DRIVER_OK;
    asm volatile("dsb sy" ::: "memory");

    debug_print("[inputd] VirtIO-input initialized, queue size ");
    debug_print_dec(queue_size);
    debug_print("\n");

    return true;
}

/**
 * @brief Push an event to the event queue.
 */
static void push_event(const input_protocol::InputEvent &ev)
{
    size_t next = (g_event_tail + 1) % EVENT_QUEUE_SIZE;
    if (next != g_event_head)
    {
        g_event_queue[g_event_tail] = ev;
        g_event_tail = next;
    }
}

/**
 * @brief Push a character to the character buffer.
 */
static void push_char(char c)
{
    size_t next = (g_char_tail + 1) % CHAR_BUFFER_SIZE;
    if (next != g_char_head)
    {
        g_char_buffer[g_char_tail] = c;
        g_char_tail = next;
    }
}

/**
 * @brief Push an escape sequence to the character buffer.
 */
static void push_escape_seq(const char *seq)
{
    while (*seq)
    {
        push_char(*seq++);
    }
}

/**
 * @brief Poll the VirtIO device for new events.
 */
static void poll_device()
{
    if (!g_mmio || !g_event_used)
        return;

    // Check for completed events
    asm volatile("dmb sy" ::: "memory");

    while (g_event_last_used != g_event_used->idx)
    {
        uint16_t idx = g_event_last_used % 64;
        uint32_t desc_idx = g_event_used->ring[idx].id;

        virtio_input::InputEvent *ev = &g_event_buffers[desc_idx];

        // Only process key events
        if (ev->type == virtio_input::EV_KEY)
        {
            uint16_t code = ev->code;
            bool pressed = (ev->value != 0);

            // Update modifier state
            if (input::is_modifier(code))
            {
                uint8_t mod_bit = input::modifier_bit(code);
                if (pressed)
                    g_modifiers |= mod_bit;
                else
                    g_modifiers &= ~mod_bit;
            }
            else if (code == input::key::CAPS_LOCK && pressed)
            {
                g_caps_lock = !g_caps_lock;
                if (g_caps_lock)
                    g_modifiers |= modifier::CAPS_LOCK;
                else
                    g_modifiers &= ~modifier::CAPS_LOCK;
            }
            else
            {
                // Create event
                input_protocol::InputEvent iev;
                iev.type = pressed ? EVENT_KEY_PRESS : EVENT_KEY_RELEASE;
                iev.modifiers = g_modifiers;
                iev.code = code;
                iev.value = pressed ? 1 : 0;
                push_event(iev);

                // Translate to ASCII for key presses
                if (pressed)
                {
                    switch (code)
                    {
                        case input::key::UP:
                            if (g_modifiers & modifier::SHIFT)
                                push_escape_seq("\033[1;2A");  // xterm Shift+Up
                            else
                                push_escape_seq("\033[A");
                            break;
                        case input::key::DOWN:
                            if (g_modifiers & modifier::SHIFT)
                                push_escape_seq("\033[1;2B");  // xterm Shift+Down
                            else
                                push_escape_seq("\033[B");
                            break;
                        case input::key::RIGHT:
                            push_escape_seq("\033[C");
                            break;
                        case input::key::LEFT:
                            push_escape_seq("\033[D");
                            break;
                        case input::key::HOME:
                            push_escape_seq("\033[H");
                            break;
                        case input::key::END:
                            push_escape_seq("\033[F");
                            break;
                        case input::key::DELETE:
                            push_escape_seq("\033[3~");
                            break;
                        case input::key::PAGE_UP:
                            push_escape_seq("\033[5~");
                            break;
                        case input::key::PAGE_DOWN:
                            push_escape_seq("\033[6~");
                            break;
                        default:
                            char c = input::key_to_ascii(code, g_modifiers);
                            if (c != 0)
                                push_char(c);
                            break;
                    }
                }
            }
        }

        // Re-add descriptor to available ring
        uint16_t avail_idx = g_event_avail->idx % 64;
        g_event_avail->ring[avail_idx] = static_cast<uint16_t>(desc_idx);
        asm volatile("dmb sy" ::: "memory");
        g_event_avail->idx++;

        g_event_last_used++;
    }

    // Notify device if we added descriptors
    asm volatile("dmb sy" ::: "memory");
    g_mmio[reg::QUEUE_NOTIFY / 4] = 0;
}

/**
 * @brief Acknowledge device interrupt.
 */
static void ack_interrupt()
{
    if (g_mmio)
    {
        uint32_t isr = g_mmio[reg::INTERRUPT_STATUS / 4];
        g_mmio[reg::INTERRUPT_ACK / 4] = isr;
        asm volatile("dsb sy" ::: "memory");
    }
}

/**
 * @brief Handle a client request.
 */
static void handle_request(int32_t client_channel, const uint8_t *data, size_t len)
{
    if (len < 4)
        return;

    uint32_t msg_type = *reinterpret_cast<const uint32_t *>(data);

    switch (msg_type)
    {
        case INP_GET_CHAR:
        {
            if (len < sizeof(GetCharRequest))
                return;
            auto *req = reinterpret_cast<const GetCharRequest *>(data);

            GetCharReply reply;
            reply.type = INP_GET_CHAR_REPLY;
            reply.request_id = req->request_id;

            if (g_char_head != g_char_tail)
            {
                reply.result =
                    static_cast<int32_t>(static_cast<uint8_t>(g_char_buffer[g_char_head]));
                g_char_head = (g_char_head + 1) % CHAR_BUFFER_SIZE;
            }
            else
            {
                reply.result = -1;
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case INP_GET_EVENT:
        {
            if (len < sizeof(GetEventRequest))
                return;
            auto *req = reinterpret_cast<const GetEventRequest *>(data);

            GetEventReply reply;
            reply.type = INP_GET_EVENT_REPLY;
            reply.request_id = req->request_id;

            if (g_event_head != g_event_tail)
            {
                reply.status = 0;
                reply.event = g_event_queue[g_event_head];
                g_event_head = (g_event_head + 1) % EVENT_QUEUE_SIZE;
            }
            else
            {
                reply.status = -1;
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case INP_GET_MODIFIERS:
        {
            if (len < sizeof(GetModifiersRequest))
                return;
            auto *req = reinterpret_cast<const GetModifiersRequest *>(data);

            GetModifiersReply reply;
            reply.type = INP_GET_MODIFIERS_REPLY;
            reply.request_id = req->request_id;
            reply.modifiers = g_modifiers;
            reply._pad[0] = reply._pad[1] = reply._pad[2] = 0;

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case INP_HAS_INPUT:
        {
            if (len < sizeof(HasInputRequest))
                return;
            auto *req = reinterpret_cast<const HasInputRequest *>(data);

            HasInputReply reply;
            reply.type = INP_HAS_INPUT_REPLY;
            reply.request_id = req->request_id;
            reply.has_char = (g_char_head != g_char_tail) ? 1 : 0;
            reply.has_event = (g_event_head != g_event_tail) ? 1 : 0;

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case INP_GET_MOUSE:
        {
            if (len < sizeof(GetMouseRequest))
                return;
            auto *req = reinterpret_cast<const GetMouseRequest *>(data);

            GetMouseReply reply;
            reply.type = INP_GET_MOUSE_REPLY;
            reply.request_id = req->request_id;

            // Query kernel for mouse state
            sys::MouseState state;
            if (sys::get_mouse_state(&state) == 0)
            {
                reply.x = state.x;
                reply.y = state.y;
                reply.dx = state.dx;
                reply.dy = state.dy;
                reply.buttons = state.buttons;
            }
            else
            {
                reply.x = 0;
                reply.y = 0;
                reply.dx = 0;
                reply.dy = 0;
                reply.buttons = 0;
            }
            reply._pad[0] = reply._pad[1] = reply._pad[2] = 0;

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        default:
            debug_print("[inputd] Unknown message type: ");
            debug_print_dec(msg_type);
            debug_print("\n");
            break;
    }
}

/**
 * @brief Main entry point.
 */
extern "C" void _start()
{
    debug_print("[inputd] Starting input server...\n");

    // Receive bootstrap capabilities
    recv_bootstrap_caps();

    // Find VirtIO input device
    uint64_t mmio_phys = 0;
    uint32_t irq = 0;
    if (!find_input_device(&mmio_phys, &irq))
    {
        debug_print("[inputd] No VirtIO-input device found\n");
        sys::exit(1);
    }

    // Initialize device
    if (!init_device())
    {
        debug_print("[inputd] Failed to initialize device\n");
        sys::exit(1);
    }

    // Register for IRQ
    int64_t irq_result = device::irq_register(g_irq);
    if (irq_result < 0)
    {
        debug_print("[inputd] Failed to register IRQ ");
        debug_print_dec(g_irq);
        debug_print("\n");
        // Continue anyway, we can poll
    }

    // Create service channel
    auto ch_result = sys::channel_create();
    if (ch_result.error != 0)
    {
        debug_print("[inputd] Failed to create service channel\n");
        sys::exit(1);
    }
    int32_t send_ch = static_cast<int32_t>(ch_result.val0);
    int32_t recv_ch = static_cast<int32_t>(ch_result.val1);
    g_service_channel = recv_ch;

    // Register with assign system
    if (sys::assign_set("INPUTD:", send_ch) < 0)
    {
        debug_print("[inputd] Failed to register INPUTD: assign\n");
        sys::exit(1);
    }

    debug_print("[inputd] Service registered as INPUTD:\n");
    debug_print("[inputd] Ready.\n");

    // Main event loop
    uint8_t msg_buf[MAX_PAYLOAD];
    uint32_t handles[4];

    while (true)
    {
        // Poll device for input
        poll_device();

        // Check for client messages
        uint32_t handle_count = 4;
        int64_t n =
            sys::channel_recv(g_service_channel, msg_buf, sizeof(msg_buf), handles, &handle_count);

        if (n > 0)
        {
            // Got a message - first handle is client's reply channel
            if (handle_count > 0)
            {
                int32_t client_ch = static_cast<int32_t>(handles[0]);
                handle_request(client_ch, msg_buf, static_cast<size_t>(n));

                // Close unused handles
                for (uint32_t i = 0; i < handle_count; i++)
                {
                    sys::channel_close(static_cast<int32_t>(handles[i]));
                }
            }
        }
        else if (n == VERR_WOULD_BLOCK)
        {
            // No message, wait a bit (or for IRQ)
            if (irq_result >= 0)
            {
                device::irq_wait(g_irq, 10); // 10ms timeout
                ack_interrupt();
            }
            else
            {
                sys::yield();
            }
        }
    }

    // Unreachable - server runs forever
    sys::exit(0);
}
