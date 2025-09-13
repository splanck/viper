// tui/tests/test_demo_headless.cpp
// @brief Run tui_demo in headless mode; expect clean exit.
// @invariant VIPERTUI_NO_TTY=1 triggers one-frame render and exit.
// @ownership None.

#include <cassert>
#include <cstdlib>
#include <string>

static void set_no_tty_env()
{
#if defined(_WIN32)
    _putenv_s("VIPERTUI_NO_TTY", "1");
#else
    setenv("VIPERTUI_NO_TTY", "1", 1);
#endif
}

int main()
{
    set_no_tty_env();
#if defined(_WIN32)
    const std::string cmd = "..\\apps\\tui_demo.exe";
#else
    const std::string cmd = "../apps/tui_demo";
#endif
    int rc = std::system(cmd.c_str());
    assert(rc == 0);
    return 0;
}
