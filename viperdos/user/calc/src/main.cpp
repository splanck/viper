//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file main.cpp
 * @brief Calculator application entry point.
 *
 * This file contains the main function and event loop for the ViperDOS
 * calculator application. The calculator provides a standard desktop-style
 * interface for performing arithmetic calculations.
 *
 * ## Application Structure
 *
 * The calculator is organized into three main components:
 * - **main.cpp** (this file): Event loop and action dispatch
 * - **calc.hpp/cpp**: Calculator logic and state management
 * - **ui.hpp/cpp**: Rendering and input translation
 *
 * ## Event Loop
 *
 * The main loop handles three types of events:
 * 1. **Close events**: Terminate the application
 * 2. **Mouse events**: Map clicks to buttons, trigger actions
 * 3. **Keyboard events**: Map key presses to actions
 *
 * Each action is dispatched through handleAction() which maps the action
 * character to the appropriate calc:: function.
 *
 * ## Action Characters
 *
 * Actions are represented as single characters for simplicity:
 * - '0'-'9': Digit input
 * - '.': Decimal point
 * - '+', '-', '*', '/': Operators
 * - '=': Equals (compute result)
 * - 'C': Clear all
 * - 'E': Clear entry
 * - 'N': Negate (+/-)
 * - '%': Percent
 * - 'I': Inverse (1/x)
 * - 'M': Memory clear
 * - 'R': Memory recall
 * - 'P': Memory plus (M+)
 *
 * @see calc.hpp for calculator state and operations
 * @see ui.hpp for rendering and input handling
 */
//===----------------------------------------------------------------------===//

#include "../include/calc.hpp"
#include "../include/ui.hpp"

//===----------------------------------------------------------------------===//
// Action Dispatch
//===----------------------------------------------------------------------===//

/**
 * @brief Dispatches an action character to the appropriate calculator function.
 *
 * This function maps single-character action codes to their corresponding
 * calc:: functions, providing a centralized dispatch point for all user
 * input whether from mouse clicks or keyboard presses.
 *
 * @param state  Reference to the calculator state to modify.
 * @param action The action character (see file documentation for codes).
 *
 * @note Actions not recognized are silently ignored.
 */
static void handleAction(calc::State &state, char action) {
    switch (action) {
        // Digits
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            calc::inputDigit(state, action);
            break;

        // Decimal
        case '.':
            calc::inputDecimal(state);
            break;

        // Operators
        case '+':
            calc::inputOperator(state, calc::Operation::Add);
            break;
        case '-':
            calc::inputOperator(state, calc::Operation::Subtract);
            break;
        case '*':
            calc::inputOperator(state, calc::Operation::Multiply);
            break;
        case '/':
            calc::inputOperator(state, calc::Operation::Divide);
            break;

        // Equals
        case '=':
            calc::inputEquals(state);
            break;

        // Clear
        case 'C':
            calc::inputClear(state);
            break;
        case 'E':
            calc::inputClearEntry(state);
            break;

        // Functions
        case 'N':
            calc::inputNegate(state);
            break;
        case '%':
            calc::inputPercent(state);
            break;
        case 'I':
            calc::inputInverse(state);
            break;

        // Memory
        case 'M':
            calc::memoryClear(state);
            break;
        case 'R':
            calc::memoryRecall(state);
            break;
        case 'P':
            calc::memoryAdd(state);
            break;
    }
}

//===----------------------------------------------------------------------===//
// Main Entry Point
//===----------------------------------------------------------------------===//

/**
 * @brief Application entry point.
 *
 * Initializes the GUI library, creates the calculator window, and runs
 * the main event loop. The loop continues until the user closes the window.
 *
 * ## Initialization Sequence
 *
 * 1. Initialize GUI library (connect to displayd)
 * 2. Create calculator window with specified dimensions
 * 3. Initialize calculator state to "0"
 * 4. Render initial display
 * 5. Enter event loop
 *
 * ## Event Processing
 *
 * Each iteration of the main loop:
 * 1. Poll for GUI events (non-blocking)
 * 2. Handle any events received (close, mouse, keyboard)
 * 3. Re-render if state changed
 * 4. Yield CPU to prevent busy-waiting
 *
 * @return 0 on successful exit, 1 on initialization failure.
 */
extern "C" int main() {
    // Initialize GUI library
    if (gui_init() != 0) {
        return 1;
    }

    // Create calculator window
    gui_window_t *win = gui_create_window("Calculator", calc::ui::WIN_WIDTH, calc::ui::WIN_HEIGHT);
    if (!win) {
        gui_shutdown();
        return 1;
    }

    // Initialize calculator state and render
    calc::State state;
    calc::init(state);
    calc::ui::render(win, state);

    // Main event loop
    while (true) {
        gui_event_t event;
        if (gui_poll_event(win, &event) == 0) {
            switch (event.type) {
                case GUI_EVENT_CLOSE:
                    goto done;

                case GUI_EVENT_MOUSE:
                    // Handle left mouse button press
                    if (event.mouse.event_type == 1 && event.mouse.button == 0) {
                        char action = calc::ui::getButtonAt(event.mouse.x, event.mouse.y);
                        if (action) {
                            handleAction(state, action);
                            calc::ui::render(win, state);
                        }
                    }
                    break;

                case GUI_EVENT_KEY: {
                    // Handle key press (not release)
                    if (event.key.pressed) {
                        char action = calc::ui::keyToAction(event.key.keycode, event.key.modifiers);
                        if (action) {
                            handleAction(state, action);
                            calc::ui::render(win, state);
                        }
                    }
                    break;
                }

                default:
                    break;
            }
        }

        // Yield CPU to prevent busy-waiting
        __asm__ volatile("mov x8, #0x0E\n\tsvc #0" ::: "x8");
    }

done:
    // Cleanup
    gui_destroy_window(win);
    gui_shutdown();
    return 0;
}
