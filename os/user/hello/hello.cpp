/**
 * @file hello.cpp
 * @brief Simple test program for verifying process spawn functionality.
 *
 * @details
 * This minimal user-space program prints a message and exits. It is used
 * to test the ELF loader and process spawning via the "Run" shell command.
 */

#include "../syscall.hpp"

/**
 * @brief Print a string to the console.
 */
static void puts(const char *s)
{
    while (*s)
    {
        sys::putchar(*s++);
    }
}

/**
 * @brief Print an integer in decimal.
 */
static void put_num(i64 n)
{
    if (n < 0)
    {
        sys::putchar('-');
        n = -n;
    }
    if (n >= 10)
    {
        put_num(n / 10);
    }
    sys::putchar('0' + (n % 10));
}

/**
 * @brief Program entry point.
 *
 * Prints a message identifying this as a spawned process and exits cleanly.
 */
extern "C" void _start()
{
    puts("[hello] Hello from spawned process!\n");
    puts("[hello] Process spawn test successful.\n");
    puts("[hello] Exiting with code 0.\n");
    sys::exit(0);
}
