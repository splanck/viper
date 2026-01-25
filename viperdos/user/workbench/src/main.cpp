//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file main.cpp
 * @brief Workbench entry point for the ViperDOS desktop environment.
 *
 * This file contains the entry point for the Workbench application,
 * which provides the graphical desktop environment for ViperDOS.
 *
 * ## Application Overview
 *
 * The Workbench is the primary user interface for ViperDOS, inspired
 * by the Amiga Workbench 3.x. It provides:
 * - A graphical desktop with icons for volumes and applications
 * - A pulldown menu bar for system functions
 * - File browser windows for navigating the filesystem
 * - Theme support for customizing the visual appearance
 *
 * ## Startup Sequence
 *
 * 1. Create Desktop object
 * 2. Initialize GUI (connect to displayd)
 * 3. Create full-screen desktop surface
 * 4. Discover mounted volumes and create icons
 * 5. Enter main event loop
 *
 * ## Exit Behavior
 *
 * The Workbench runs indefinitely as the primary GUI shell.
 * It can only be terminated by shutting down the system.
 *
 * @see desktop.hpp for the Desktop class
 * @see filebrowser.hpp for file browser windows
 */
//===----------------------------------------------------------------------===//

#include "../include/desktop.hpp"

/**
 * @brief Application entry point for Workbench.
 *
 * Creates and initializes the Desktop, then runs the main event loop.
 * This function does not normally return.
 *
 * @return 0 on normal exit, 1 if initialization fails.
 */
extern "C" int main() {
    workbench::Desktop desktop;

    if (!desktop.init()) {
        return 1;
    }

    desktop.run();

    return 0;
}
