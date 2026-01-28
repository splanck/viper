#pragma once
//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file calc.hpp
 * @brief Calculator application state and operations.
 *
 * This file defines the core calculator logic for a standard desktop-style
 * calculator. The calculator supports:
 * - Basic arithmetic operations (add, subtract, multiply, divide)
 * - Memory functions (M+, MR, MC)
 * - Special functions (percent, negate, inverse)
 * - Decimal number input
 *
 * ## Design
 *
 * The calculator uses an accumulator-based architecture:
 * - User enters a number, then presses an operator
 * - The number is stored and operation is remembered
 * - User enters a second number and presses equals
 * - The operation is applied to both numbers
 *
 * ## State Machine
 *
 * ```
 *    ┌─────────┐
 *    │  Init   │ (display "0", newNumber=true)
 *    └────┬────┘
 *         │ digit
 *         ▼
 *    ┌─────────┐
 *    │ Entering│ (building number in display)
 *    │ Number  │◄──────────────────────────┐
 *    └────┬────┘         digit             │
 *         │ operator                       │
 *         ▼                                │
 *    ┌─────────┐                           │
 *    │Operator │ (save accumulator,        │
 *    │ Pending │  remember op)             │
 *    └────┬────┘                           │
 *         │ digit                          │
 *         ▼                                │
 *    ┌─────────┐                           │
 *    │ Second  │                           │
 *    │ Number  │───────────────────────────┘
 *    └────┬────┘         equals
 *         │
 *         ▼
 *    ┌─────────┐
 *    │ Result  │ (display result, ready for next op)
 *    └─────────┘
 * ```
 *
 * ## Usage
 *
 * @code
 * calc::State state;
 * calc::init(state);
 *
 * // User presses 5 + 3 =
 * calc::inputDigit(state, '5');
 * calc::inputOperator(state, calc::Operation::Add);
 * calc::inputDigit(state, '3');
 * calc::inputEquals(state);
 * // state.display now shows "8"
 * @endcode
 *
 * @see ui.hpp for the UI rendering and input handling
 */
//===----------------------------------------------------------------------===//

#include <stdint.h>

namespace calc {

//===----------------------------------------------------------------------===//
// Types
//===----------------------------------------------------------------------===//

/**
 * @brief Arithmetic operations supported by the calculator.
 *
 * These represent the four basic arithmetic operations that can be
 * performed between the accumulator and the current display value.
 */
enum class Operation {
    None,     /**< No pending operation. */
    Add,      /**< Addition: accumulator + display */
    Subtract, /**< Subtraction: accumulator - display */
    Multiply, /**< Multiplication: accumulator * display */
    Divide    /**< Division: accumulator / display (may cause error) */
};

/**
 * @brief Calculator state containing all runtime data.
 *
 * This structure holds the complete state of the calculator, including
 * the display string, numeric values, and flags controlling input behavior.
 *
 * ## Display String
 *
 * The display buffer contains the human-readable number as a null-terminated
 * string. It can hold numbers, a decimal point, a negative sign, and the
 * special "Error" message when division by zero occurs.
 *
 * ## Accumulator vs Display
 *
 * - **display**: The currently visible number (as a string for rendering)
 * - **accumulator**: The first operand, stored when an operator is pressed
 *
 * When the user presses "5 + 3 =":
 * 1. "5" is entered, display shows "5"
 * 2. "+" is pressed, accumulator=5, pendingOp=Add, newNumber=true
 * 3. "3" is entered, display shows "3"
 * 4. "=" is pressed, result = accumulator + display = 8, display shows "8"
 */
struct State {
    char display[32];    /**< Display string (null-terminated number or "Error"). */
    double accumulator;  /**< First operand, stored when operator is pressed. */
    double memory;       /**< Memory register for M+/MR operations. */
    Operation pendingOp; /**< Operation to apply when = or next op is pressed. */
    bool newNumber;      /**< True if next digit starts a new number. */
    bool hasDecimal;     /**< True if current number already has a decimal point. */
    bool hasMemory;      /**< True if memory register contains a stored value. */
    bool error;          /**< True if an error occurred (e.g., division by zero). */
};

//===----------------------------------------------------------------------===//
// Initialization
//===----------------------------------------------------------------------===//

/**
 * @brief Initializes the calculator to its default state.
 *
 * This function resets all calculator state to initial values:
 * - Display shows "0"
 * - Accumulator and memory are cleared
 * - No pending operation
 * - Ready for new number input
 *
 * @param state Reference to the State structure to initialize.
 *
 * @note Call this once at application startup.
 */
void init(State &state);

//===----------------------------------------------------------------------===//
// Input Handling
//===----------------------------------------------------------------------===//

/**
 * @brief Handles a digit key press (0-9).
 *
 * Appends the digit to the current number being entered. If newNumber is true,
 * the display is cleared first and the digit becomes the start of a new number.
 *
 * ## Leading Zero Handling
 *
 * If the display is "0" and the digit is not "0", the leading zero is replaced
 * rather than appending (e.g., pressing "5" changes "0" to "5", not "05").
 *
 * @param state Reference to the calculator state.
 * @param digit The digit character ('0' through '9').
 *
 * @note The display buffer is limited; very long numbers may be truncated.
 */
void inputDigit(State &state, char digit);

/**
 * @brief Handles the decimal point key press.
 *
 * Adds a decimal point to the current number if one isn't already present.
 * If newNumber is true, starts a new number with "0." prefix.
 *
 * @param state Reference to the calculator state.
 *
 * @note Has no effect if the current number already contains a decimal point.
 */
void inputDecimal(State &state);

/**
 * @brief Handles an operator key press (+, -, *, /).
 *
 * When an operator is pressed:
 * 1. If there's a pending operation, it is executed first (chaining)
 * 2. The current display value is stored in the accumulator
 * 3. The new operation is remembered as pending
 * 4. newNumber is set true so the next digit starts fresh
 *
 * ## Chained Operations
 *
 * Pressing "5 + 3 * 2 =" evaluates left-to-right:
 * - After "+ 3": accumulator = 8
 * - After "* 2": result = 16
 *
 * @param state Reference to the calculator state.
 * @param op    The operation to perform (Add, Subtract, Multiply, Divide).
 */
void inputOperator(State &state, Operation op);

/**
 * @brief Handles the equals key press.
 *
 * Executes the pending operation using the accumulator and display value,
 * then shows the result. If division by zero would occur, sets error state.
 *
 * @param state Reference to the calculator state.
 *
 * @note After equals, the result is in the display and newNumber is true.
 * @note Pressing equals with no pending operation has no effect.
 */
void inputEquals(State &state);

/**
 * @brief Handles the Clear (C) key press.
 *
 * Performs a full reset of the calculator, clearing:
 * - Display (returns to "0")
 * - Accumulator
 * - Pending operation
 * - Error state
 *
 * @param state Reference to the calculator state.
 *
 * @note Memory is NOT cleared by this function (use memoryClear for that).
 *
 * @see inputClearEntry() To clear only the current entry.
 */
void inputClear(State &state);

/**
 * @brief Handles the Clear Entry (CE) key press.
 *
 * Clears only the current display entry, leaving the accumulator and
 * pending operation intact. Useful for correcting a typo without
 * losing the calculation in progress.
 *
 * @param state Reference to the calculator state.
 *
 * @see inputClear() To clear everything.
 */
void inputClearEntry(State &state);

/**
 * @brief Handles the negate (+/-) key press.
 *
 * Toggles the sign of the current display value. Positive numbers become
 * negative and vice versa. Zero is unaffected.
 *
 * @param state Reference to the calculator state.
 */
void inputNegate(State &state);

/**
 * @brief Handles the percent (%) key press.
 *
 * Divides the current display value by 100, converting it to a percentage.
 * For example, pressing "%" after "50" gives "0.5".
 *
 * @param state Reference to the calculator state.
 */
void inputPercent(State &state);

/**
 * @brief Handles the inverse (1/x) key press.
 *
 * Computes the reciprocal of the current display value. Sets error state
 * if the display value is zero (division by zero).
 *
 * @param state Reference to the calculator state.
 */
void inputInverse(State &state);

//===----------------------------------------------------------------------===//
// Memory Operations
//===----------------------------------------------------------------------===//

/**
 * @brief Clears the memory register (MC).
 *
 * Sets the memory value to zero and marks memory as empty. The "M" indicator
 * in the UI will be hidden after this operation.
 *
 * @param state Reference to the calculator state.
 */
void memoryClear(State &state);

/**
 * @brief Recalls the memory value to the display (MR).
 *
 * Replaces the current display with the value stored in memory. If memory
 * is empty (hasMemory is false), displays zero.
 *
 * @param state Reference to the calculator state.
 */
void memoryRecall(State &state);

/**
 * @brief Adds the display value to memory (M+).
 *
 * Adds the current display value to the memory register. If memory was
 * empty, this is equivalent to storing the value. Sets hasMemory to true.
 *
 * @param state Reference to the calculator state.
 */
void memoryAdd(State &state);

/**
 * @brief Subtracts the display value from memory (M-).
 *
 * Subtracts the current display value from the memory register. If memory
 * was empty, this stores the negated value. Sets hasMemory to true.
 *
 * @param state Reference to the calculator state.
 */
void memorySubtract(State &state);

//===----------------------------------------------------------------------===//
// Utility
//===----------------------------------------------------------------------===//

/**
 * @brief Formats a numeric value for display.
 *
 * Converts a double-precision value to a display string with appropriate
 * formatting:
 * - Removes unnecessary trailing zeros
 * - Handles very large and very small numbers
 * - Shows "Error" for NaN/Inf values
 *
 * @param state Reference to the calculator state (display is updated).
 * @param value The numeric value to format.
 */
void formatDisplay(State &state, double value);

} // namespace calc
