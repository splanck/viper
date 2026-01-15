# Zia Bugs Found During Ladders Demo Development

This document tracks bugs and spec-vs-implementation differences discovered while building the Ladders game demo.

---

## Spec vs Implementation Differences

### Difference #1: Bible Appendix A Uses Different Syntax Than Actual Implementation

**Document:** `docs/bible/appendices/a-zia-reference.md`

The Bible's Appendix A describes a significantly different syntax than what is actually implemented:

| Feature | Bible (Appendix A) | Actual Implementation |
|---------|-------------------|----------------------|
| Immutable vars | `let x = 42;` | `final x = 42;` |
| Compile-time constants | `const PI = 3.14;` | `final PI = 3.14;` |
| Integer types | `i8`, `i16`, `i32`, `i64` | `Integer` (64-bit only) |
| Unsigned types | `u8`, `u16`, `u32`, `u64` | Not implemented |
| Float types | `f32`, `f64` | `Number` (64-bit only) |
| Boolean | `bool` | `Boolean` |
| Character type | `char` | Not implemented (use String) |
| Class definition | `class Foo { constructor() { } }` | `entity Foo { func init() { } }` |
| Visibility export | `export` keyword | `expose` keyword for fields |
| Compound assignment | `+=`, `-=`, `*=`, `/=` | Not implemented |
| Array literals | `[1, 2, 3]` | `[]` then `.add()` calls |
| Array methods | `.length`, `.push()`, `.pop()` | `.size()`, `.add()`, `.get()` |

**Impact:** The Bible appendix appears to be a future-looking spec that hasn't been implemented. Users reading the Bible will encounter errors when trying those patterns.

**Recommendation:** Either update the Bible to match reality, or clearly mark Appendix A as "planned features" vs "implemented features".

---

### Difference #2: Entry Point Function Name

**Documents:** `zia-reference.md` says `start()`, but demos use various names.

The spec says:
```viper
func start() {
    // Program execution begins here
}
```

However, examining existing demos:
- `gfx_centipede/main.zia` uses `func main()` (line 7)
- Some tests may use different patterns

**Status:** Need to verify which is actually correct by testing.

---

### Difference #3: Random Number API

**Document:** `namespaces.md` shows `Viper.Random.Next()` returning Number.

But demos use `Viper.Random.NextInt(max)` returning Integer.

From namespaces.md:
```
Viper.Random.Next() -> F64 — Return next random number in [0, 1)
```

From gfx_centipede/game.zia:
```viper
var chance = Viper.Random.NextInt(100);
```

**Status:** Both may exist - `Next()` for float [0,1), `NextInt(max)` for integer [0,max).

---

### Difference #4: List Method Names

**Document:** `zia-reference.md` says `list.size()`.

From the reference:
```viper
list.size();                        // Get count
```

But snake.zia uses:
```viper
segments.count()  // Line 130
```

**Status:** Both `.size()` and `.count()` may work as aliases. Need to verify.

---

## Bugs Found During Development

### Bug #55: Snake Demo Uses Wrong Key Codes

**File:** `demos/zia/graphics_show/snake.zia`
**Severity:** Medium
**Status:** BUG CONFIRMED

**Description:**
The snake.zia demo uses GLFW key codes instead of VGFX key codes for arrow keys.

**Wrong codes used in snake.zia (lines 322-333):**
```viper
if canvas.KeyHeld(265) != 0 {  // Should be 260
    game.snake.setDirection(DIR_UP);
}
if canvas.KeyHeld(264) != 0 {  // Should be 261
    game.snake.setDirection(DIR_DOWN);
}
if canvas.KeyHeld(263) != 0 {  // Should be 258
    game.snake.setDirection(DIR_LEFT);
}
if canvas.KeyHeld(262) != 0 {  // Should be 259
    game.snake.setDirection(DIR_RIGHT);
}
```

**Correct VGFX key codes (from vgfx.h):**
| Key | VGFX Code | GLFW Code (wrong) |
|-----|-----------|-------------------|
| ESCAPE | 256 | 256 |
| ENTER | 257 | 257 |
| LEFT | 258 | 263 |
| RIGHT | 259 | 262 |
| UP | 260 | 265 |
| DOWN | 261 | 264 |

**Impact:** Arrow keys will not work correctly in snake.zia on VGFX backend.

**Fix:** Replace GLFW codes with VGFX codes in snake.zia.

---

## Notes on Working Patterns

Based on examining existing demos, these patterns work reliably:

### Working Entry Point
```viper
module main;

func main() {
    // Code here
}
```

### Working Entity Pattern
```viper
entity Player {
    expose Integer x;
    expose Integer y;

    expose func init(startX: Integer, startY: Integer) {
        x = startX;
        y = startY;
    }

    expose func move(dx: Integer, dy: Integer) {
        x = x + dx;
        y = y + dy;
    }
}
```

### Working List Pattern
```viper
var items: List[Integer] = [];
items.add(1);
items.add(2);
var first = items.get(0);
var count = items.count();  // or .size()
```

### Working Graphics Pattern
```viper
var canvas = new Viper.Graphics.Canvas("Title", 800, 600);

while canvas.ShouldClose == 0 {
    canvas.Poll();

    if canvas.KeyHeld(262) != 0 {  // Right arrow
        // handle input
    }

    canvas.Clear(Viper.Graphics.Color.RGB(0, 0, 0));
    canvas.Box(x, y, width, height, color);
    canvas.Flip();

    Viper.Time.SleepMs(16);
}
```

### Key Codes (from vgfx.h - CORRECT codes)
```
VGFX_KEY_SPACE  = 32 (' ')
VGFX_KEY_0-9    = 48-57 ('0'-'9')
VGFX_KEY_A-Z    = 65-90 ('A'-'Z')
VGFX_KEY_ESCAPE = 256
VGFX_KEY_ENTER  = 257
VGFX_KEY_LEFT   = 258
VGFX_KEY_RIGHT  = 259
VGFX_KEY_UP     = 260
VGFX_KEY_DOWN   = 261
```

---
