# Frogger - Text-Based Game in Viper BASIC

A complete implementation of the classic Frogger game using Viper BASIC's OOP features, ANSI graphics, and modular file inclusion.

## Features Demonstrated

- **OOP Design**: Multiple classes (Frog, Car, Position) with inheritance patterns
- **Object Arrays**: Arrays of Car objects with independent behavior
- **AddFile**: Modular code organization across multiple files
- **INKEY$**: Non-blocking keyboard input for responsive gameplay
- **ANSI Graphics**: Color output and cursor positioning
- **Collision Detection**: Object-oriented collision checking between frog and cars

## Files

- `frogger.bas` - Main game file
- `frogger_classes.bas` - Game object class definitions (Frog, Car, Position)
- `frogger_ansi.bas` - ANSI terminal utilities module

## How to Play

```bash
ilc front basic -run frogger.bas
```

**Controls:**
- W - Move up
- A - Move left
- S - Move down
- D - Move right
- Q - Quit

**Objective:** Guide the frog (@) to the goal line at the top while avoiding cars (<, >).
Reach the goal to score 100 points. You have 3 lives.

## Technical Notes

This game was created as a stress test for Viper BASIC during development to validate:

1. **Nested objects**: Frog contains a Position object
2. **Object lifetime management**: Proper reference counting with nested method calls
3. **Object arrays**: Array of 8 independent Car objects
4. **Module system**: Three separate .bas files included via AddFile
5. **Real-time input**: INKEY$() for non-blocking game loop
6. **String concatenation**: Building ANSI escape sequences
7. **Method chaining**: Object methods calling other object methods

## Known Workarounds

**BUG-106**: Avoid naming conflicts between class fields and methods. For example, a class cannot have both a field named `isAlive` and a method named `IsAlive()`. Use distinct names like `alive` (field) and `IsAlive()` (method).

## Development

Created: 2025-11-18
Purpose: Comprehensive OOP and features stress test for Viper BASIC
Lines of Code: ~450 across 3 modules
Classes: 3 (Position, Frog, Car)
Objects: 9 (1 frog + 8 cars)
