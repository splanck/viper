// tui/src/version.cpp
// @brief Implements the viper_tui_version function.
// @invariant Returns a valid, null-terminated string.
// @ownership String has static storage duration.

#include "tui/version.hpp"

namespace viper::tui
{
const char *viper_tui_version() noexcept
{
    return "0.1.0";
}
} // namespace viper::tui
