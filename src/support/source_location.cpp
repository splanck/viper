// File: src/support/source_location.cpp
// Purpose: Implements helpers for source location metadata.
// Key invariants: File identifier 0 indicates an unknown location.
// Ownership/Lifetime: Provides value-type utilities with no dynamic ownership.
// Links: docs/codemap.md

#include "support/source_location.hpp"

namespace il::support
{
/// @brief Determine whether the source location refers to a registered file.
/// @return True when file_id is non-zero, indicating a valid location.
bool SourceLoc::isValid() const
{
    return file_id != 0;
}
} // namespace il::support

