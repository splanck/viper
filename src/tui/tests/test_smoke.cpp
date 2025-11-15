// tui/tests/test_smoke.cpp
// @brief Smoke test for the ViperTUI version API.
// @invariant The version string is non-null.
// @ownership No ownership of returned string is taken.

#include "tui/version.hpp"
#include <iostream>

int main()
{
    std::cout << viper::tui::viper_tui_version() << std::endl;
    return 0;
}
