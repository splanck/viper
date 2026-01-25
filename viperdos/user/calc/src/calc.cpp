//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file calc.cpp
 * @brief Calculator logic implementation for the ViperDOS Calculator app.
 *
 * This file implements the calculator's computation engine, handling:
 * - Digit input and display formatting
 * - Arithmetic operations (add, subtract, multiply, divide)
 * - Special functions (percent, inverse, negate)
 * - Memory operations (MC, MR, M+, M-)
 *
 * ## State Machine
 *
 * The calculator uses a simple state machine:
 * ```
 *  [Input] ─> [Operator] ─> [Input] ─> [Equals]
 *     │            │            │           │
 *     └─────────── Chain ───────┘           │
 *                                           v
 *                                    [Display Result]
 * ```
 *
 * Key state variables:
 * - `newNumber`: True if next digit should start a new number
 * - `pendingOp`: Operation waiting for second operand
 * - `accumulator`: First operand (or running result for chained ops)
 *
 * ## Chained Calculations
 *
 * When entering `2 + 3 + 4 =`:
 * 1. Input "2" -> display "2"
 * 2. Press "+" -> accumulator=2, pendingOp=Add
 * 3. Input "3" -> display "3"
 * 4. Press "+" -> calculate 2+3=5, display "5", accumulator=5, pendingOp=Add
 * 5. Input "4" -> display "4"
 * 6. Press "=" -> calculate 5+4=9, display "9"
 *
 * ## Display Formatting
 *
 * Numbers are displayed with the following rules:
 * - Integers that fit are shown without decimal point
 * - Large numbers use scientific notation (%.10g format)
 * - Maximum display width is 14 characters
 * - "Error" is shown for division by zero
 *
 * @see calc.hpp for State structure and function declarations
 */
//===----------------------------------------------------------------------===//

#include "../include/calc.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace calc {

void init(State &state) {
    strcpy(state.display, "0");
    state.accumulator = 0;
    state.memory = 0;
    state.pendingOp = Operation::None;
    state.newNumber = true;
    state.hasDecimal = false;
    state.hasMemory = false;
    state.error = false;
}

void formatDisplay(State &state, double value) {
    // Check for integer value that fits
    if (value == (long long)value && value < 1e12 && value > -1e12) {
        snprintf(state.display, sizeof(state.display), "%lld", (long long)value);
    } else {
        snprintf(state.display, sizeof(state.display), "%.10g", value);
    }

    // Truncate if too long
    if (strlen(state.display) > 14) {
        state.display[14] = '\0';
    }
}

static double calculate(double left, double right, Operation op) {
    switch (op) {
    case Operation::Add:
        return left + right;
    case Operation::Subtract:
        return left - right;
    case Operation::Multiply:
        return left * right;
    case Operation::Divide:
        return right != 0 ? left / right : 0;
    default:
        return right;
    }
}

void inputDigit(State &state, char digit) {
    if (state.error) {
        init(state);
    }

    if (state.newNumber) {
        state.display[0] = digit;
        state.display[1] = '\0';
        state.newNumber = false;
        state.hasDecimal = false;
    } else if (strlen(state.display) < 14) {
        size_t len = strlen(state.display);
        state.display[len] = digit;
        state.display[len + 1] = '\0';
    }
}

void inputDecimal(State &state) {
    if (state.error) {
        init(state);
    }

    if (state.newNumber) {
        strcpy(state.display, "0.");
        state.newNumber = false;
        state.hasDecimal = true;
    } else if (!state.hasDecimal && strlen(state.display) < 13) {
        strcat(state.display, ".");
        state.hasDecimal = true;
    }
}

void inputOperator(State &state, Operation op) {
    if (state.error) {
        init(state);
        return;
    }

    double currentValue = atof(state.display);

    if (state.pendingOp != Operation::None && !state.newNumber) {
        // Chain calculation
        double result = calculate(state.accumulator, currentValue, state.pendingOp);

        if (state.pendingOp == Operation::Divide && currentValue == 0) {
            strcpy(state.display, "Error");
            state.error = true;
            state.pendingOp = Operation::None;
            return;
        }

        state.accumulator = result;
        formatDisplay(state, result);
    } else {
        state.accumulator = currentValue;
    }

    state.pendingOp = op;
    state.newNumber = true;
    state.hasDecimal = false;
}

void inputEquals(State &state) {
    if (state.error || state.pendingOp == Operation::None) {
        return;
    }

    double currentValue = atof(state.display);

    if (state.pendingOp == Operation::Divide && currentValue == 0) {
        strcpy(state.display, "Error");
        state.error = true;
        state.pendingOp = Operation::None;
        return;
    }

    double result = calculate(state.accumulator, currentValue, state.pendingOp);
    state.accumulator = result;
    formatDisplay(state, result);
    state.pendingOp = Operation::None;
    state.newNumber = true;
    state.hasDecimal = false;
}

void inputClear(State &state) {
    init(state);
}

void inputClearEntry(State &state) {
    strcpy(state.display, "0");
    state.newNumber = true;
    state.hasDecimal = false;
    state.error = false;
}

void inputNegate(State &state) {
    if (state.error) {
        return;
    }

    if (state.display[0] == '-') {
        memmove(state.display, state.display + 1, strlen(state.display));
    } else if (strcmp(state.display, "0") != 0) {
        memmove(state.display + 1, state.display, strlen(state.display) + 1);
        state.display[0] = '-';
    }
}

void inputPercent(State &state) {
    if (state.error) {
        return;
    }

    double value = atof(state.display) / 100.0;
    formatDisplay(state, value);
    state.newNumber = true;
}

void inputInverse(State &state) {
    if (state.error) {
        return;
    }

    double value = atof(state.display);
    if (value == 0) {
        strcpy(state.display, "Error");
        state.error = true;
    } else {
        formatDisplay(state, 1.0 / value);
    }
    state.newNumber = true;
}

void memoryClear(State &state) {
    state.memory = 0;
    state.hasMemory = false;
}

void memoryRecall(State &state) {
    formatDisplay(state, state.memory);
    state.newNumber = true;
}

void memoryAdd(State &state) {
    state.memory += atof(state.display);
    state.hasMemory = true;
    state.newNumber = true;
}

void memorySubtract(State &state) {
    state.memory -= atof(state.display);
    state.hasMemory = true;
    state.newNumber = true;
}

} // namespace calc
