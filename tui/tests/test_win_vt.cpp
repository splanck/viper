// tui/tests/test_win_vt.cpp
// @brief Compile-time check that TerminalSession enables VT sequences on Windows.
// @invariant No runtime behavior is verified.
// @ownership No ownership transfer.

#include "tui/term/session.hpp"

int main()
{
#ifdef _WIN32
    viper::tui::TerminalSession session;
#endif
    return 0;
}
