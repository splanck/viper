// File: tests/runtime/RTChrInvalidTests.cpp
// Purpose: Ensure rt_chr traps on out-of-range input.
// Key invariants: Codes outside 0-255 trigger runtime trap.
// Ownership: Uses runtime library.
// Links: docs/runtime-abi.md
#include "rt.hpp"

int main()
{
    rt_chr(-1);
    return 0;
}
