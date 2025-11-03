// File: include/viper/il/IO.hpp
// Purpose: Stable façade for IL text parsing, serialization, and string utilities.
// Key invariants: Re-exports supported IO interfaces; parser internals stay internal.
// Ownership/Lifetime: Parser/Serializer mirror underlying implementations.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/io/Parser.hpp"
#include "il/io/Serializer.hpp"
#include "il/io/StringEscape.hpp"

/// @file include/viper/il/IO.hpp
/// @brief Aggregated public header for IL text IO utilities.  Provides access
///        to the high-level parser, serializer, and string escape helpers while
///        keeping implementation details hidden under src/il/internal.

