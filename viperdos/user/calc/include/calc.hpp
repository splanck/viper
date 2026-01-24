#pragma once
//===----------------------------------------------------------------------===//
// Calculator state and operations
//===----------------------------------------------------------------------===//

#include <stdint.h>

namespace calc {

// Calculator operations
enum class Operation { None, Add, Subtract, Multiply, Divide };

// Calculator state
struct State {
    char display[32];
    double accumulator;
    double memory;
    Operation pendingOp;
    bool newNumber;
    bool hasDecimal;
    bool hasMemory;
    bool error;
};

// Initialize calculator state
void init(State &state);

// Input handling
void inputDigit(State &state, char digit);
void inputDecimal(State &state);
void inputOperator(State &state, Operation op);
void inputEquals(State &state);
void inputClear(State &state);
void inputClearEntry(State &state);
void inputNegate(State &state);
void inputPercent(State &state);
void inputInverse(State &state);

// Memory operations
void memoryClear(State &state);
void memoryRecall(State &state);
void memoryAdd(State &state);
void memorySubtract(State &state);

// Utility
void formatDisplay(State &state, double value);

} // namespace calc
