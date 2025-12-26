#include "console.hpp"
#include "serial.hpp"

/**
 * @file console.cpp
 * @brief Console façade implementation.
 *
 * @details
 * Currently the console façade forwards directly to the serial backend. This
 * indirection exists so that higher-level kernel code can depend on a stable
 * `console::` API while the concrete output routing can evolve (e.g. add a
 * framebuffer console, a ring-buffered log, or per-CPU log sinks).
 */
namespace console
{

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
