/// @file main.cpp
/// @brief Workbench entry point.

#include "../include/desktop.hpp"

extern "C" int main()
{
    workbench::Desktop desktop;

    if (!desktop.init()) {
        return 1;
    }

    desktop.run();

    return 0;
}
