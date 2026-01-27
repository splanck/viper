//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "input.hpp"
#include "../console/serial.hpp"
#include "../drivers/virtio/input.hpp"
#include "../lib/spinlock.hpp"
#include "keycodes.hpp"

/**
 * @file input.cpp
 * @brief Input subsystem implementation (virtio keyboard/mouse).
 *
 * @details
 * The input subsystem polls available virtio input devices and:
 * - Enqueues structured key/mouse events into an event ring buffer.
 * - Translates key press events into ASCII (and escape sequences for special
 *   keys) and enqueues them into a character ring buffer suitable for console
 *   input.
 *
 * The design is polling-based for simplicity during bring-up and is intended
 * to be invoked periodically from the timer interrupt handler.
 */
namespace input {

// Spinlock to protect char_buffer from concurrent access
// (timer interrupt vs syscall context)
static Spinlock char_lock;

// Event ring buffer - uses atomic operations for lock-free SPSC queue
// Producer (timer interrupt) only writes queue_tail
// Consumer (syscall context) only writes queue_head
static Event event_queue[EVENT_QUEUE_SIZE];
static volatile usize queue_head = 0;
static volatile usize queue_tail = 0;

// Character buffer (for translated keyboard input)
static char char_buffer[256];
static volatile usize char_head = 0;
static volatile usize char_tail = 0;

// Current modifier state
static u8 current_modifiers = 0;

// Caps lock state (toggle)
static bool caps_lock_on = false;

// Mouse state
static MouseState g_mouse = {0, 0, 0, 0, 0, 0, 0, {0, 0, 0}};
static u32 g_screen_width = 1024; // Default, updated by set_mouse_bounds()
static u32 g_screen_height = 768; // Default, updated by set_mouse_bounds()
static Spinlock mouse_lock;

/** @copydoc input::init */
void init() {
    serial::puts("[input] Initializing input subsystem\n");
    queue_head = 0;
    queue_tail = 0;
    char_head = 0;
    char_tail = 0;
    current_modifiers = 0;
    caps_lock_on = false;

    // Initialize mouse state (center of default screen)
    g_mouse.x = static_cast<i32>(g_screen_width / 2);
    g_mouse.y = static_cast<i32>(g_screen_height / 2);
    g_mouse.dx = 0;
    g_mouse.dy = 0;
    g_mouse.buttons = 0;

    serial::puts("[input] Input subsystem initialized\n");
}

/**
 * @brief Push an input event into the event ring buffer.
 *
 * @details
 * Lock-free for SPSC queue. Uses memory barriers for correctness.
 * Drops the event if the ring buffer is full.
 *
 * @param ev Event to enqueue.
 */
static void push_event(const Event &ev) {
    usize tail = __atomic_load_n(&queue_tail, __ATOMIC_RELAXED);
    usize next = (tail + 1) % EVENT_QUEUE_SIZE;
    usize head = __atomic_load_n(&queue_head, __ATOMIC_ACQUIRE);

    if (next != head) {
        event_queue[tail] = ev;
        __atomic_store_n(&queue_tail, next, __ATOMIC_RELEASE);
    }
}

/**
 * @brief Push a character byte into the character ring buffer (unlocked).
 *
 * @details
 * Drops the byte if the buffer is full. Caller must hold char_lock.
 *
 * @param c Character byte to enqueue.
 */
static void push_char_unlocked(char c) {
    usize next = (char_tail + 1) % sizeof(char_buffer);
    if (next != char_head) {
        char_buffer[char_tail] = c;
        char_tail = next;
    }
}

/**
 * @brief Push a character byte into the character ring buffer.
 *
 * @details
 * Thread-safe version that acquires the spinlock.
 *
 * @param c Character byte to enqueue.
 */
static void push_char(char c) {
    SpinlockGuard guard(char_lock);
    push_char_unlocked(c);
}

// Push an escape sequence for special keys (e.g., "\033[A" for up arrow)
/**
 * @brief Enqueue an ANSI escape sequence as a series of character bytes.
 *
 * @details
 * Used to represent special navigation keys as conventional terminal escape
 * sequences so higher-level console code can interpret them.
 * The entire sequence is added atomically to prevent interleaving.
 *
 * @param seq NUL-terminated escape sequence string.
 */
static void push_escape_seq(const char *seq) {
    SpinlockGuard guard(char_lock);
    while (*seq) {
        push_char_unlocked(*seq++);
    }
}

// =============================================================================
// Input Polling Helpers
// =============================================================================

/**
 * @brief Handle a single keyboard key event.
 *
 * @details
 * Processes key press/release, updates modifiers, emits events, and translates
 * to ASCII or escape sequences for special keys.
 *
 * @param code Linux evdev keycode.
 * @param pressed True if key pressed, false if released.
 */
static void handle_key_event(u16 code, bool pressed) {
    // Update modifier state
    if (is_modifier(code)) {
        u8 mod_bit = modifier_bit(code);
        if (pressed)
            current_modifiers |= mod_bit;
        else
            current_modifiers &= ~mod_bit;
        return;
    }

    // Handle caps lock toggle
    if (code == key::CAPS_LOCK && pressed) {
        caps_lock_on = !caps_lock_on;
        if (caps_lock_on)
            current_modifiers |= modifier::CAPS_LOCK;
        else
            current_modifiers &= ~modifier::CAPS_LOCK;
        return;
    }

    // Create and push event
    Event ev;
    ev.type = pressed ? EventType::KeyPress : EventType::KeyRelease;
    ev.modifiers = current_modifiers;
    ev.code = code;
    ev.value = pressed ? 1 : 0;
    push_event(ev);

    // Translate to ASCII/escape sequences for key presses
    if (!pressed)
        return;

    switch (code) {
        case key::UP:
            if (current_modifiers & modifier::SHIFT)
                push_escape_seq("\033[1;2A");
            else
                push_escape_seq("\033[A");
            break;
        case key::DOWN:
            if (current_modifiers & modifier::SHIFT)
                push_escape_seq("\033[1;2B");
            else
                push_escape_seq("\033[B");
            break;
        case key::RIGHT:
            push_escape_seq("\033[C");
            break;
        case key::LEFT:
            push_escape_seq("\033[D");
            break;
        case key::HOME:
            push_escape_seq("\033[H");
            break;
        case key::END:
            push_escape_seq("\033[F");
            break;
        case key::DELETE:
            push_escape_seq("\033[3~");
            break;
        case key::PAGE_UP:
            push_escape_seq("\033[5~");
            break;
        case key::PAGE_DOWN:
            push_escape_seq("\033[6~");
            break;
        default: {
            char c = key_to_ascii(code, current_modifiers);
            if (c != 0)
                push_char(c);
            break;
        }
    }
}

/**
 * @brief Poll the keyboard for pending events.
 */
static void poll_keyboard() {
    if (!virtio::keyboard)
        return;

    virtio::InputEvent vev;
    while (virtio::keyboard->get_event(&vev)) {
        if (vev.type == virtio::ev_type::KEY) {
            handle_key_event(vev.code, vev.value != 0);
        }
    }
}

/**
 * @brief Handle a mouse relative movement event.
 *
 * @param code REL_X or REL_Y axis code.
 * @param value Movement delta.
 */
static void handle_mouse_move(u16 code, i32 value) {
    constexpr u16 REL_X = 0x00;
    constexpr u16 REL_Y = 0x01;
    constexpr u16 REL_WHEEL = 0x08;
    constexpr u16 REL_HWHEEL = 0x06;

    if (code == REL_X) {
        g_mouse.dx += value;
        g_mouse.x += value;
        if (g_mouse.x < 0)
            g_mouse.x = 0;
        if (g_mouse.x >= static_cast<i32>(g_screen_width))
            g_mouse.x = static_cast<i32>(g_screen_width) - 1;

        Event ev;
        ev.type = EventType::MouseMove;
        ev.modifiers = current_modifiers;
        ev.code = 0;
        ev.value = 0;
        push_event(ev);
    } else if (code == REL_Y) {
        g_mouse.dy += value;
        g_mouse.y += value;
        if (g_mouse.y < 0)
            g_mouse.y = 0;
        if (g_mouse.y >= static_cast<i32>(g_screen_height))
            g_mouse.y = static_cast<i32>(g_screen_height) - 1;

        Event ev;
        ev.type = EventType::MouseMove;
        ev.modifiers = current_modifiers;
        ev.code = 0;
        ev.value = 0;
        push_event(ev);
    } else if (code == REL_WHEEL) {
        g_mouse.scroll += value;

        Event ev;
        ev.type = EventType::MouseScroll;
        ev.modifiers = current_modifiers;
        ev.code = REL_WHEEL;
        ev.value = value;
        push_event(ev);
    } else if (code == REL_HWHEEL) {
        g_mouse.hscroll += value;

        Event ev;
        ev.type = EventType::MouseScroll;
        ev.modifiers = current_modifiers;
        ev.code = REL_HWHEEL;
        ev.value = value;
        push_event(ev);
    }
}

/**
 * @brief Handle a mouse button event.
 *
 * @param code Button code (BTN_LEFT, BTN_RIGHT, BTN_MIDDLE).
 * @param pressed True if button pressed, false if released.
 */
static void handle_mouse_button(u16 code, bool pressed) {
    constexpr u16 BTN_LEFT = 0x110;
    constexpr u16 BTN_RIGHT = 0x111;
    constexpr u16 BTN_MIDDLE = 0x112;

    u8 button_bit = 0;
    if (code == BTN_LEFT)
        button_bit = 0x01;
    else if (code == BTN_RIGHT)
        button_bit = 0x02;
    else if (code == BTN_MIDDLE)
        button_bit = 0x04;

    if (button_bit == 0)
        return;

    if (pressed)
        g_mouse.buttons |= button_bit;
    else
        g_mouse.buttons &= ~button_bit;

    Event ev;
    ev.type = EventType::MouseButton;
    ev.modifiers = current_modifiers;
    ev.code = code;
    ev.value = pressed ? 1 : 0;
    push_event(ev);
}

/**
 * @brief Poll the mouse for pending events.
 */
static void poll_mouse() {
    if (!virtio::mouse)
        return;

    virtio::InputEvent vev;
    while (virtio::mouse->get_event(&vev)) {
        SpinlockGuard guard(mouse_lock);

        if (vev.type == virtio::ev_type::REL) {
            handle_mouse_move(vev.code, static_cast<i32>(vev.value));
        } else if (vev.type == virtio::ev_type::KEY) {
            handle_mouse_button(vev.code, vev.value != 0);
        }
    }
}

// =============================================================================
// Input Polling Main Entry Point
// =============================================================================

/** @copydoc input::poll */
void poll() {
    poll_keyboard();
    poll_mouse();
}

/** @copydoc input::has_event */
bool has_event() {
    usize head = __atomic_load_n(&queue_head, __ATOMIC_RELAXED);
    usize tail = __atomic_load_n(&queue_tail, __ATOMIC_ACQUIRE);
    return head != tail;
}

/** @copydoc input::get_event */
bool get_event(Event *event) {
    usize head = __atomic_load_n(&queue_head, __ATOMIC_RELAXED);
    usize tail = __atomic_load_n(&queue_tail, __ATOMIC_ACQUIRE);

    if (head == tail) {
        return false;
    }

    *event = event_queue[head];
    __atomic_store_n(&queue_head, (head + 1) % EVENT_QUEUE_SIZE, __ATOMIC_RELEASE);
    return true;
}

/** @copydoc input::get_modifiers */
u8 get_modifiers() {
    return current_modifiers;
}

/** @copydoc input::has_char */
bool has_char() {
    SpinlockGuard guard(char_lock);
    return char_head != char_tail;
}

/** @copydoc input::getchar */
i32 getchar() {
    SpinlockGuard guard(char_lock);
    if (char_head == char_tail) {
        return -1;
    }
    char c = char_buffer[char_head];
    char_head = (char_head + 1) % sizeof(char_buffer);
    return static_cast<i32>(static_cast<u8>(c));
}

// =============================================================================
// Key-to-ASCII Translation Tables
// =============================================================================

namespace {

// Keycode to lowercase letter lookup (0 = not a letter)
// Index is evdev keycode, value is lowercase ASCII letter
constexpr char keycode_to_letter[128] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0,   0,   0,   0,   // 0-15
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 0, 0, 0,   0,   'a', 's', // 16-31
    'd', 'f', 'g', 'h', 'j', 'k', 'l', 0,   0,   0,   0, 0, 'z', 'x', 'c', 'v', // 32-47
    'b', 'n', 'm', 0,   0,   0,   0,   0,   0,   0,   0, 0, 0,   0,   0,   0,   // 48-63
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0,   0,   0,   0,   // 64-79
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0,   0,   0,   0,   // 80-95
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0,   0,   0,   0,   // 96-111
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0,   0,   0,   0,   // 112-127
};

// Number row: unshifted and shifted characters
// Keys 2-13 map to 1,2,3,4,5,6,7,8,9,0,-,=
constexpr char number_unshifted[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '='};
constexpr char number_shifted[12] = {'!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+'};

// Symbol keys: keycode -> (unshifted, shifted)
struct SymbolEntry {
    u16 code;
    char unshifted;
    char shifted;
};

constexpr SymbolEntry symbol_table[] = {
    {key::LEFT_BRACKET, '[', '{'},
    {key::RIGHT_BRACKET, ']', '}'},
    {key::BACKSLASH, '\\', '|'},
    {key::SEMICOLON, ';', ':'},
    {key::APOSTROPHE, '\'', '"'},
    {key::GRAVE, '`', '~'},
    {key::COMMA, ',', '<'},
    {key::DOT, '.', '>'},
    {key::SLASH, '/', '?'},
};

} // namespace

/**
 * @brief Translate an evdev keycode into an ASCII byte.
 *
 * @param code Linux evdev keycode.
 * @param modifiers Current modifier bitmask.
 * @return ASCII character byte, or 0 if not representable.
 */
char key_to_ascii(u16 code, u8 modifiers) {
    bool shift = (modifiers & modifier::SHIFT) != 0;
    bool caps = (modifiers & modifier::CAPS_LOCK) != 0;
    bool ctrl = (modifiers & modifier::CTRL) != 0;

    // Check letter keys via lookup table
    if (code < 128) {
        char letter = keycode_to_letter[code];
        if (letter != 0) {
            if (ctrl)
                return static_cast<char>(letter - 'a' + 1);
            bool uppercase = shift ^ caps;
            return uppercase ? static_cast<char>(letter - 32) : letter;
        }
    }

    // Number row (keycodes 2-13)
    if (code >= 2 && code <= 13) {
        u16 idx = code - 2;
        return shift ? number_shifted[idx] : number_unshifted[idx];
    }

    // Symbol keys
    for (const auto &sym : symbol_table) {
        if (code == sym.code)
            return shift ? sym.shifted : sym.unshifted;
    }

    // Special keys
    switch (code) {
        case key::SPACE:
            return ' ';
        case key::ENTER:
            return '\n';
        case key::TAB:
            return '\t';
        case key::BACKSPACE:
            return '\b';
        case key::ESCAPE:
            return '\033';
        default:
            return 0;
    }
}

/** @copydoc input::is_modifier */
bool is_modifier(u16 code) {
    return code == key::LEFT_SHIFT || code == key::RIGHT_SHIFT || code == key::LEFT_CTRL ||
           code == key::RIGHT_CTRL || code == key::LEFT_ALT || code == key::RIGHT_ALT ||
           code == key::LEFT_META || code == key::RIGHT_META;
}

/** @copydoc input::modifier_bit */
u8 modifier_bit(u16 code) {
    switch (code) {
        case key::LEFT_SHIFT:
        case key::RIGHT_SHIFT:
            return modifier::SHIFT;
        case key::LEFT_CTRL:
        case key::RIGHT_CTRL:
            return modifier::CTRL;
        case key::LEFT_ALT:
        case key::RIGHT_ALT:
            return modifier::ALT;
        case key::LEFT_META:
        case key::RIGHT_META:
            return modifier::META;
        default:
            return 0;
    }
}

// =============================================================================
// Mouse API Implementation
// =============================================================================

/** @copydoc input::get_mouse_state */
MouseState get_mouse_state() {
    SpinlockGuard guard(mouse_lock);
    MouseState state = g_mouse;
    // Reset deltas after reading
    g_mouse.dx = 0;
    g_mouse.dy = 0;
    g_mouse.scroll = 0;
    g_mouse.hscroll = 0;
    return state;
}

/** @copydoc input::set_mouse_bounds */
void set_mouse_bounds(u32 width, u32 height) {
    SpinlockGuard guard(mouse_lock);
    g_screen_width = width;
    g_screen_height = height;

    // Clamp current position to new bounds
    if (g_mouse.x >= static_cast<i32>(width))
        g_mouse.x = static_cast<i32>(width) - 1;
    if (g_mouse.y >= static_cast<i32>(height))
        g_mouse.y = static_cast<i32>(height) - 1;
    if (g_mouse.x < 0)
        g_mouse.x = 0;
    if (g_mouse.y < 0)
        g_mouse.y = 0;
}

/** @copydoc input::get_mouse_position */
void get_mouse_position(i32 &x, i32 &y) {
    SpinlockGuard guard(mouse_lock);
    x = g_mouse.x;
    y = g_mouse.y;
}

/** @copydoc input::set_mouse_position */
void set_mouse_position(i32 x, i32 y) {
    SpinlockGuard guard(mouse_lock);

    // Clamp to screen bounds
    if (x < 0)
        x = 0;
    if (x >= static_cast<i32>(g_screen_width))
        x = static_cast<i32>(g_screen_width) - 1;
    if (y < 0)
        y = 0;
    if (y >= static_cast<i32>(g_screen_height))
        y = static_cast<i32>(g_screen_height) - 1;

    g_mouse.x = x;
    g_mouse.y = y;
}

} // namespace input
