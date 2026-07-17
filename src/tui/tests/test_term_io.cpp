//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_term_io.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/internals/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/term/term_io.hpp"

#include "tests/TestHarness.hpp"

TEST(TUI, TermIO) {
    zanna::tui::term::StringTermIO tio;
    tio.write("hello");
    tio.flush();
    ASSERT_EQ(tio.buffer(), "hello");
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
