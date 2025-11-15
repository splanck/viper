// tui/tests/test_term_io.cpp
// @brief Tests for TermIO implementations.
// @invariant StringTermIO captures writes verbatim.
// @ownership StringTermIO owns its buffer.

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
