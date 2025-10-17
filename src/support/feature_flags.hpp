// src/support/feature_flags.hpp
#pragma once

/// @brief Provides default values for optional compile-time feature toggles.
/// @notes Include from translation units that depend on optionally enabled
/// features so builds succeed even when the compiler does not define the
/// corresponding macros.
#ifndef VIPER_ENABLE_OOP
#define VIPER_ENABLE_OOP 0
#endif
