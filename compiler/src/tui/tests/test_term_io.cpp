//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_term_io.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/term/term_io.hpp"

#include <cassert>

int main()
{
    viper::tui::term::StringTermIO tio;
    tio.write("hello");
    tio.flush();
    assert(tio.buffer() == "hello");
    return 0;
}
