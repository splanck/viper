#include "input.hpp"
#include "../console/serial.hpp"
#include "../drivers/virtio/input.hpp"
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
namespace input
{

// Event ring buffer
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

/** @copydoc input::init */
void init()
{
    serial::puts("[input] Initializing input subsystem\n");
    queue_head = 0;
    queue_tail = 0;
    char_head = 0;
    char_tail = 0;
    current_modifiers = 0;
    caps_lock_on = false;
    serial::puts("[input] Input subsystem initialized\n");
}

/**
 * @brief Push an input event into the event ring buffer.
 *
 * @details
 * Drops the event if the ring buffer is full.
 *
 * @param ev Event to enqueue.
 */
static void push_event(const Event &ev)
{
    usize next = (queue_tail + 1) % EVENT_QUEUE_SIZE;
    if (next != queue_head)
    {
        event_queue[queue_tail] = ev;
        queue_tail = next;
    }
}

/**
 * @brief Push a character byte into the character ring buffer.
 *
 * @details
 * Drops the byte if the buffer is full.
 *
 * @param c Character byte to enqueue.
 */
static void push_char(char c)
{
    usize next = (char_tail + 1) % sizeof(char_buffer);
    if (next != char_head)
    {
        char_buffer[char_tail] = c;
        char_tail = next;
    }
}

// Push an escape sequence for special keys (e.g., "\033[A" for up arrow)
/**
 * @brief Enqueue an ANSI escape sequence as a series of character bytes.
 *
 * @details
 * Used to represent special navigation keys as conventional terminal escape
 * sequences so higher-level console code can interpret them.
 *
 * @param seq NUL-terminated escape sequence string.
 */
static void push_escape_seq(const char *seq)
{
    while (*seq)
    {
        push_char(*seq++);
    }
}

/** @copydoc input::poll */
void poll()
{
    // Poll keyboard
    if (virtio::keyboard)
    {
        virtio::InputEvent vev;
        while (virtio::keyboard->get_event(&vev))
        {
            // Only process key events
            if (vev.type != virtio::ev_type::KEY)
            {
                continue;
            }

            u16 code = vev.code;
            bool pressed = (vev.value != 0);

            // Update modifier state
            if (is_modifier(code))
            {
                u8 mod_bit = modifier_bit(code);
                if (pressed)
                {
                    current_modifiers |= mod_bit;
                }
                else
                {
                    current_modifiers &= ~mod_bit;
                }
                continue;
            }

            // Handle caps lock toggle
            if (code == key::CAPS_LOCK && pressed)
            {
                caps_lock_on = !caps_lock_on;
                if (caps_lock_on)
                {
                    current_modifiers |= modifier::CAPS_LOCK;
                }
                else
                {
                    current_modifiers &= ~modifier::CAPS_LOCK;
                }
                continue;
            }

            // Create event
            Event ev;
            ev.type = pressed ? EventType::KeyPress : EventType::KeyRelease;
            ev.modifiers = current_modifiers;
            ev.code = code;
            ev.value = pressed ? 1 : 0;
            push_event(ev);

            // Translate to ASCII for key presses
            if (pressed)
            {
                // Handle special keys that generate escape sequences
                switch (code)
                {
                    case key::UP:
                        push_escape_seq("\033[A");
                        break;
                    case key::DOWN:
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
                    default:
                        // Regular ASCII translation
                        char c = key_to_ascii(code, current_modifiers);
                        if (c != 0)
                        {
                            push_char(c);
                        }
                        break;
                }
            }
        }
    }

    // Poll mouse (just consume events for now)
    if (virtio::mouse)
    {
        virtio::InputEvent vev;
        while (virtio::mouse->get_event(&vev))
        {
            // Ignore mouse events for now
        }
    }
}

/** @copydoc input::has_event */
bool has_event()
{
    return queue_head != queue_tail;
}

/** @copydoc input::get_event */
bool get_event(Event *event)
{
    if (queue_head == queue_tail)
    {
        return false;
    }
    *event = event_queue[queue_head];
    queue_head = (queue_head + 1) % EVENT_QUEUE_SIZE;
    return true;
}

/** @copydoc input::get_modifiers */
u8 get_modifiers()
{
    return current_modifiers;
}

/** @copydoc input::has_char */
bool has_char()
{
    return char_head != char_tail;
}

/** @copydoc input::getchar */
i32 getchar()
{
    if (char_head == char_tail)
    {
        return -1;
    }
    char c = char_buffer[char_head];
    char_head = (char_head + 1) % sizeof(char_buffer);
    return static_cast<i32>(static_cast<u8>(c));
}

/**
 * @brief Translate an evdev keycode into an ASCII byte (if representable).
 *
 * @details
 * This helper performs the final step of keyboard translation for the console
 * character buffer:
 * - Determines whether Shift, Caps Lock, and Ctrl are active based on the
 *   provided modifier mask.
 * - Maps a subset of Linux evdev keycodes (see `keycodes.hpp`) to printable
 *   ASCII characters.
 * - Applies simple modifier rules:
 *   - For letters: `Shift` and `Caps Lock` combine via XOR to decide case.
 *   - For `Ctrl+letter`: returns control codes 1–26 (`^A`..`^Z`).
 *   - For number row and punctuation: `Shift` selects the shifted symbol.
 *
 * Keys that do not have a single-byte ASCII representation (e.g., function
 * keys) are not translated here; higher-level code may represent them as ANSI
 * escape sequences instead.
 *
 * @param code Linux evdev keycode (e.g., @ref key::A).
 * @param modifiers Current modifier bitmask (see `modifier::*`).
 * @return ASCII character byte, or 0 if the key is not representable as ASCII.
 */
char key_to_ascii(u16 code, u8 modifiers)
{
    bool shift = (modifiers & modifier::SHIFT) != 0;
    bool caps = (modifiers & modifier::CAPS_LOCK) != 0;
    bool ctrl = (modifiers & modifier::CTRL) != 0;

    // Letters (A-Z are evdev codes 30-38, 44-50, 16-25)
    char letter = 0;
    if (code == key::A)
        letter = 'a';
    else if (code == key::B)
        letter = 'b';
    else if (code == key::C)
        letter = 'c';
    else if (code == key::D)
        letter = 'd';
    else if (code == key::E)
        letter = 'e';
    else if (code == key::F)
        letter = 'f';
    else if (code == key::G)
        letter = 'g';
    else if (code == key::H)
        letter = 'h';
    else if (code == key::I)
        letter = 'i';
    else if (code == key::J)
        letter = 'j';
    else if (code == key::K)
        letter = 'k';
    else if (code == key::L)
        letter = 'l';
    else if (code == key::M)
        letter = 'm';
    else if (code == key::N)
        letter = 'n';
    else if (code == key::O)
        letter = 'o';
    else if (code == key::P)
        letter = 'p';
    else if (code == key::Q)
        letter = 'q';
    else if (code == key::R)
        letter = 'r';
    else if (code == key::S)
        letter = 's';
    else if (code == key::T)
        letter = 't';
    else if (code == key::U)
        letter = 'u';
    else if (code == key::V)
        letter = 'v';
    else if (code == key::W)
        letter = 'w';
    else if (code == key::X)
        letter = 'x';
    else if (code == key::Y)
        letter = 'y';
    else if (code == key::Z)
        letter = 'z';

    if (letter != 0)
    {
        // Handle Ctrl+letter
        if (ctrl)
        {
            return static_cast<char>(letter - 'a' + 1);
        }
        // Handle shift/caps
        bool uppercase = shift ^ caps;
        return uppercase ? (letter - 32) : letter;
    }

    // Numbers and symbols
    switch (code)
    {
        case key::_1:
            return shift ? '!' : '1';
        case key::_2:
            return shift ? '@' : '2';
        case key::_3:
            return shift ? '#' : '3';
        case key::_4:
            return shift ? '$' : '4';
        case key::_5:
            return shift ? '%' : '5';
        case key::_6:
            return shift ? '^' : '6';
        case key::_7:
            return shift ? '&' : '7';
        case key::_8:
            return shift ? '*' : '8';
        case key::_9:
            return shift ? '(' : '9';
        case key::_0:
            return shift ? ')' : '0';

        case key::MINUS:
            return shift ? '_' : '-';
        case key::EQUAL:
            return shift ? '+' : '=';
        case key::LEFT_BRACKET:
            return shift ? '{' : '[';
        case key::RIGHT_BRACKET:
            return shift ? '}' : ']';
        case key::BACKSLASH:
            return shift ? '|' : '\\';
        case key::SEMICOLON:
            return shift ? ':' : ';';
        case key::APOSTROPHE:
            return shift ? '"' : '\'';
        case key::GRAVE:
            return shift ? '~' : '`';
        case key::COMMA:
            return shift ? '<' : ',';
        case key::DOT:
            return shift ? '>' : '.';
        case key::SLASH:
            return shift ? '?' : '/';

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
bool is_modifier(u16 code)
{
    return code == key::LEFT_SHIFT || code == key::RIGHT_SHIFT || code == key::LEFT_CTRL ||
           code == key::RIGHT_CTRL || code == key::LEFT_ALT || code == key::RIGHT_ALT ||
           code == key::LEFT_META || code == key::RIGHT_META;
}

/** @copydoc input::modifier_bit */
u8 modifier_bit(u16 code)
{
    switch (code)
    {
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

} // namespace input
