# Monopoly Game - Bug Tracking

This document tracks bugs discovered while developing the Monopoly game for Viper BASIC.

## Bug Format
- **ID**: BUG-XXX
- **Status**: Open/Fixed/Won't Fix
- **Severity**: Critical/High/Medium/Low
- **Component**: VM/Codegen/Runtime/Frontend
- **Description**: What happened
- **Expected**: What should happen
- **Reproduction**: Steps to reproduce
- **Notes**: Additional information

---

## Active Bugs

### BUG-001: Symbol resolution conflict between global variables and method parameters
- **Status**: Open (Workaround Available)
- **Severity**: Critical
- **Component**: Frontend (Symbol Resolution)
- **Description**: When a global variable has the same name as a method parameter in a different class, the compiler confuses the two symbols during code generation, causing type mismatch errors like "store: operand type mismatch: operand 1 must be i64".
- **Expected**: Method parameters should shadow global variables within their scope; each symbol should be resolved correctly by type and scope.
- **Reproduction**:
```basic
CLASS Space
    DIM mPrice AS INTEGER

    SUB SetPrice(p AS INTEGER)   ' <-- Parameter named "p"
        mPrice = p
    END SUB
END CLASS

CLASS Player
    DIM mName AS STRING
END CLASS

DIM p AS Player   ' <-- Global variable named "p" (same as parameter above)
p = NEW Player()

DIM s AS Space
s = NEW Space()
s.SetPrice(100)   ' ERROR: Compiler confuses "p" parameter with Player "p"
```
- **Root Cause**: The compiler's symbol lookup is not properly scoped. When compiling `SetPrice`, it finds the global variable `p` (of type Player) instead of the local parameter `p` (of type INTEGER), causing the type mismatch in the generated IL.
- **Workaround**: Avoid using variable names that match parameter names in other classes. For example, use `player` instead of `p`, or rename parameters to be more specific (e.g., `price` instead of `p`).
- **Test Case**: `/tmp/test_p_var.bas` fails, `/tmp/test_varname.bas` passes (only difference is `DIM p AS Player` vs `DIM player AS Player`)

---

## Fixed Bugs

### Applied Workaround for BUG-001 (2024-12-13)
- **Files Modified**:
  - `board.bas`: Renamed parameters `h` → `numHouses`, `m` → `isMortgaged`
  - `players.bas`: Renamed local variables `sp`, `np`, `pp` → `oldPos`, `newPos` and parameter `spaces` → `numSpaces`
- **Result**: Monopoly game now compiles and runs successfully in VM

---

## Testing Log

### Test Session 1 - Bug Discovery and Workaround
- Date: 2024-12-13
- Version: v0.1.3-snapshot
- Tests:
  - VM run: PASS (after workaround applied)
  - ARM64 native compilation: PASS (42K lines of assembly, links successfully)
  - Native execution: PASS (menu displays and quits cleanly)
- Notes: Discovered BUG-001 (symbol resolution conflict). Applied workaround by renaming short parameter names to descriptive names.

---
