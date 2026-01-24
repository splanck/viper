//===----------------------------------------------------------------------===//
// Calculator - Entry point
//===----------------------------------------------------------------------===//

#include "../include/calc.hpp"
#include "../include/ui.hpp"

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

extern "C" int main() {
    if (gui_init() != 0) {
        return 1;
    }

    gui_window_t *win =
        gui_create_window("Calculator", calc::ui::WIN_WIDTH, calc::ui::WIN_HEIGHT);
    if (!win) {
        gui_shutdown();
        return 1;
    }

    calc::State state;
    calc::init(state);
    calc::ui::render(win, state);

    while (true) {
        gui_event_t event;
        if (gui_poll_event(win, &event) == 0) {
            switch (event.type) {
            case GUI_EVENT_CLOSE:
                goto done;

            case GUI_EVENT_MOUSE:
                if (event.mouse.event_type == 1 && event.mouse.button == 0) {
                    char action = calc::ui::getButtonAt(event.mouse.x, event.mouse.y);
                    if (action) {
                        handleAction(state, action);
                        calc::ui::render(win, state);
                    }
                }
                break;

            case GUI_EVENT_KEY: {
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

        // Yield CPU
        __asm__ volatile("mov x8, #0x0E\n\tsvc #0" ::: "x8");
    }

done:
    gui_destroy_window(win);
    gui_shutdown();
    return 0;
}
