// tui/include/tui/version.hpp
#pragma once

/// @brief Returns the ViperTUI version string.
/// @invariant The returned pointer is non-null and points to a null-terminated string.
/// @ownership The returned string has static storage duration and must not be freed.
/// @notes Part of the public ViperTUI API.
namespace viper::tui
{
const char *viper_tui_version() noexcept;
} // namespace viper::tui
