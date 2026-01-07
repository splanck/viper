# Pacman BASIC Bug Tracker

This document tracks bugs found while developing the Pacman game as a stress test for Viper BASIC and Viper.* runtime maturity.

## Summary

**Total Lines of Code:** 2,087 (including tests)
**Main Game Files:** 1,354 lines
- pacman.bas: 498 lines (main game logic)
- ghost.bas: 341 lines (ghost AI)
- maze.bas: 291 lines (maze rendering)
- player.bas: 224 lines (Pacman player)

**Overall Status:** ✅ Viper BASIC is mature enough for game development

## Bug List

### BUG-001: PRINT generates newline after each call (EXPECTED BEHAVIOR)
- **Severity:** Info/Expected
- **Description:** Each PRINT statement generates a newline unless terminated with semicolon
- **Impact:** Need to use `PRINT "text";` (with semicolon) for same-line output
- **Status:** Not a bug - expected behavior
- **Resolution:** Use semicolon syntax documented in other demos

### BUG-002: Viper.Terminal.InKey() doesn't read from piped stdin
- **Severity:** Minor
- **Description:** When running with piped input (`echo "q" | vbasic ...`), InKey() didn't read the piped character
- **Impact:** Couldn't automate testing of interactive programs via pipe
- **Root Cause:** `readkey_nonblocking()` used `tcgetattr()` which fails on pipes
- **Fix:** Added `isatty()` check to use `select()` directly for pipes without termios
- **Status:** ✅ FIXED (rt_term.c)

---

## Feature Testing Results

### ✅ Core Language Features
| Feature | VM | Native | Notes |
|---------|----|---------| ------|
| Integer variables | ✅ | ✅ | |
| Double variables | ✅ | ✅ | |
| String variables | ✅ | ✅ | |
| IF/ELSEIF/ELSE/END IF | ✅ | ✅ | |
| FOR/NEXT loops | ✅ | ✅ | |
| DO WHILE/LOOP | ✅ | ✅ | |
| SUB/END SUB | ✅ | ✅ | |
| FUNCTION/END FUNCTION | ✅ | ✅ | |
| Exit Function | ✅ | ✅ | |

### ✅ OOP Features
| Feature | VM | Native | Notes |
|---------|----|---------| ------|
| CLASS/END CLASS | ✅ | ✅ | |
| DIM fields in class | ✅ | ✅ | |
| SUB methods | ✅ | ✅ | |
| FUNCTION methods | ✅ | ✅ | |
| NEW operator | ✅ | ✅ | |
| Me keyword | ✅ | ✅ | |
| Object field access | ✅ | ✅ | |
| Object method calls | ✅ | ✅ | |
| Arrays inside classes | ✅ | ✅ | |
| Arrays of objects | ✅ | ✅ | |
| Object as parameter | ✅ | ✅ | |

### ✅ Viper.* Runtime APIs
| API | VM | Native | Notes |
|-----|----|---------| ------|
| Viper.Terminal.Clear() | ✅ | ✅ | |
| Viper.Terminal.SetColor(fg, bg) | ✅ | ✅ | |
| Viper.Terminal.SetPosition(row, col) | ✅ | ✅ | |
| Viper.Terminal.InKey() | ✅ | ✅ | Works in TTY, not pipes |
| Viper.Terminal.GetKey() | ✅ | ✅ | Blocking version |
| Viper.Time.SleepMs(ms) | ✅ | ✅ | |

### ✅ String Functions
| Function | VM | Native | Notes |
|----------|----|---------| ------|
| String concatenation (+) | ✅ | ✅ | |
| LEN() | ✅ | ✅ | |
| CHR$() | ✅ | ✅ | |
| ASC() | ✅ | ✅ | |
| STR$() | ✅ | ✅ | |
| MID$() | ✅ | ✅ | |

### ✅ Math Functions
| Function | VM | Native | Notes |
|----------|----|---------| ------|
| INT() | ✅ | ✅ | |
| RND() | ✅ | ✅ | |
| RANDOMIZE | ✅ | ✅ | |
| ABS() | ✅ | ✅ | |

### ✅ Multi-File Support
| Feature | VM | Native | Notes |
|---------|----|---------| ------|
| AddFile directive | ✅ | ✅ | |
| Cross-file class access | ✅ | ✅ | |
| Cross-file function calls | ✅ | ✅ | |
| Shared global variables | ✅ | ✅ | |

---

## Testing Log

### Iteration 1: Minimal Game Loop Skeleton
- **Features tested:** Viper.Terminal.*, basic loop, variables
- **VM Result:** PASS
- **Native Result:** PASS
- **Output Match:** YES

### Iteration 2: OOP Class Testing
- **Features tested:** CLASS, NEW, methods, field access
- **VM Result:** PASS (6/6 tests)
- **Native Result:** PASS (6/6 tests)
- **Output Match:** YES

### Iteration 3: Arrays and Complex OOP
- **Features tested:** Arrays in classes, arrays of objects
- **VM Result:** PASS (6/6 tests)
- **Native Result:** PASS (6/6 tests)
- **Output Match:** YES

### Iteration 4: Strings, Random, Maze Drawing
- **Features tested:** String functions, RND, terminal positioning
- **VM Result:** PASS (8/8 tests)
- **Native Result:** PASS (8/8 tests)
- **Output Match:** YES

### Iteration 5: Full Maze Integration
- **Features tested:** Classic Pacman maze layout, multi-file AddFile
- **VM Result:** PASS
- **Native Result:** PASS
- **Output Match:** YES

### Iteration 6: Ghost Module
- **Features tested:** Ghost AI, frightened mode, movement
- **VM Result:** PASS
- **Native Result:** PASS
- **Output Match:** YES

### Full Integration Test
- **Features tested:** Complete game with Pacman, ghosts, collision, scoring
- **VM Result:** PASS
- **Native Result:** PASS
- **Output Match:** YES

---

## Conclusion

**Viper BASIC is mature and ready for:**
- Game development with OOP
- Terminal-based interactive applications
- Multi-file projects
- Complex data structures (arrays, objects)

**Recommended improvements:**
1. Fix InKey() to work with piped stdin (minor)
2. Consider adding PRINT USING for formatted output
3. Document semicolon syntax for suppressing newlines

**Runtime namespace expansion is recommended.** All tested Viper.* APIs work correctly in both VM and native compilation.
