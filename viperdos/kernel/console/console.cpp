#include "console.hpp"
#include "../drivers/virtio/input.hpp"
#include "../input/input.hpp"
#include "gcon.hpp"
#include "serial.hpp"

/**
 * @file console.cpp
 * @brief Console I/O implementation with buffered input.
 *
 * @details
 * The console subsystem provides:
 * - Output routing to serial and graphics console backends.
 * - A unified input buffer that merges keyboard and serial input.
 * - Canonical mode line editing for shell-style input.
 *
 * The input buffer is a ring buffer that can hold up to INPUT_BUFFER_SIZE
 * characters. Characters from both virtio keyboard and serial UART are
 * pushed into this buffer during poll_input().
 */
namespace console
{

// Input ring buffer
static char input_buffer[INPUT_BUFFER_SIZE];
static volatile usize input_head = 0; // Read position
static volatile usize input_tail = 0; // Write position

/**
 * @brief Push a character into the input ring buffer.
 *
 * @param c Character to push.
 * @return true if character was buffered, false if buffer is full.
 */
static bool push_char(char c)
{
    usize next = (input_tail + 1) % INPUT_BUFFER_SIZE;
    if (next == input_head)
    {
        return false; // Buffer full
    }
    input_buffer[input_tail] = c;
    input_tail = next;
    return true;
}

/** @copydoc console::init_input */
void init_input()
{
    input_head = 0;
    input_tail = 0;
    serial::puts("[console] Input buffer initialized (1024 bytes)\n");
}

/** @copydoc console::poll_input */
void poll_input()
{
    // Poll keyboard input (this also polls the virtio device)
    if (virtio::keyboard)
    {
        input::poll();
        // Drain keyboard character buffer into console buffer
        i32 c;
        while ((c = input::getchar()) >= 0)
        {
            push_char(static_cast<char>(c));
        }
    }

    // Poll serial input
    while (serial::has_char())
    {
        char c = serial::getc();
        push_char(c);
    }
}

/** @copydoc console::has_input */
bool has_input()
{
    return input_head != input_tail;
}

/** @copydoc console::getchar */
i32 getchar()
{
    if (input_head == input_tail)
    {
        return -1;
    }
    char c = input_buffer[input_head];
    input_head = (input_head + 1) % INPUT_BUFFER_SIZE;
    return static_cast<i32>(static_cast<u8>(c));
}

/** @copydoc console::input_available */
usize input_available()
{
    if (input_tail >= input_head)
    {
        return input_tail - input_head;
    }
    return INPUT_BUFFER_SIZE - input_head + input_tail;
}

/** @copydoc console::readline */
i32 readline(char *buf, usize maxlen)
{
    if (!buf || maxlen < 2)
        return -1;

    usize pos = 0;
    maxlen--; // Reserve space for NUL terminator

    while (pos < maxlen)
    {
        // Wait for input
        while (!has_input())
        {
            poll_input();
            asm volatile("wfe");
        }

        i32 c = getchar();
        if (c < 0)
            continue;

        char ch = static_cast<char>(c);

        // Handle special characters
        switch (ch)
        {
            case '\n':
            case '\r':
                // End of line
                buf[pos] = '\0';
                serial::putc('\n');
                if (gcon::is_available())
                    gcon::putc('\n');
                return static_cast<i32>(pos);

            case '\b':
            case 0x7F: // DEL
                // Backspace
                if (pos > 0)
                {
                    pos--;
                    // Erase character on terminal
                    serial::puts("\b \b");
                    if (gcon::is_available())
                        gcon::puts("\b \b");
                }
                break;

            case 0x03: // Ctrl+C
                // Cancel line
                buf[0] = '\0';
                serial::puts("^C\n");
                if (gcon::is_available())
                    gcon::puts("^C\n");
                return 0;

            case 0x04: // Ctrl+D
                // EOF
                buf[pos] = '\0';
                return pos > 0 ? static_cast<i32>(pos) : -1;

            case 0x15: // Ctrl+U
                // Clear line
                while (pos > 0)
                {
                    pos--;
                    serial::puts("\b \b");
                    if (gcon::is_available())
                        gcon::puts("\b \b");
                }
                break;

            default:
                // Regular printable character
                if (ch >= 0x20 && ch < 0x7F)
                {
                    buf[pos++] = ch;
                    serial::putc(ch);
                    if (gcon::is_available())
                        gcon::putc(ch);
                }
                break;
        }
    }

    // Buffer full
    buf[pos] = '\0';
    return static_cast<i32>(pos);
}

/** @copydoc console::print */
void print(const char *s)
{
    serial::puts(s);
}

/** @copydoc console::print_dec */
void print_dec(i64 value)
{
    serial::put_dec(value);
}

/** @copydoc console::print_hex */
void print_hex(u64 value)
{
    serial::put_hex(value);
}

} // namespace console
