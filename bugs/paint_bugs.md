# Viper Paint - Bug Report

## Compilation Issues Discovered

### Bug 1: Incorrect Viper.Graphics.Pixels API Usage

**Status**: Code bug (not a compiler bug)

**Description**:
The canvas.zia file uses static-style method calls for Viper.Graphics.Pixels operations:
```viper
// WRONG
Viper.Graphics.Pixels.Set(pixels, x, y, color);
Viper.Graphics.Pixels.Fill(pixels, color);
Viper.Graphics.Pixels.Get(pixels, x, y);
```

**Correct Pattern** (as shown in plasma.zia and other working demos):
```viper
// CORRECT - call methods on the instance
pixels.Set(x, y, color);
pixels.Fill(color);
var c = pixels.Get(x, y);
```

**Files Affected**:
- canvas.zia
- Potentially app.zia where Blit is called

**Fix**: Refactor all Viper.Graphics.Pixels static calls to instance method calls.

---

### Bug 2: Viper.Graphics.Canvas API Usage in UI Components

**Status**: Code bug (not a compiler bug)

**Description**:
The UI components (button.zia, slider.zia) and app.zia use static-style calls for Canvas operations:
```viper
// WRONG
Viper.Graphics.Canvas.BoxFill(gfx, x, y, w, h, color);
Viper.Graphics.Canvas.Text(gfx, x, y, text, color);
```

**Correct Pattern**:
```viper
// CORRECT - call methods on the canvas instance
gfx.Box(x, y, w, h, color);  // or BoxFill
gfx.Text(x, y, text, color);
```

**Files Affected**:
- ui/button.zia
- ui/slider.zia
- app.zia (handleInput, render, draw methods)
- tools/line.zia, rectangle.zia, ellipse.zia (drawPreview methods)

---

### Bug 3: Module Import Naming Conflict

**Status**: Code bug

**Description**:
In tools/brush.zia and tools/eraser.zia, there is a conflict between:
- The imported `"../brush"` module (which contains BrushSettings entity and SHAPE_ROUND/SHAPE_SQUARE constants)
- Attempting to access `brush.SHAPE_SQUARE` where `brush` is the import path

The brush.zia file has `module brush;` but tools/brush.zia has `module brush_tool;`. When tools/brush.zia does `import "../brush";` it should be able to access `brush.SHAPE_SQUARE`.

**Error Message**:
```
demos/zia/paint/tools/brush.zia:71:29: error[V3000]: Undefined identifier: brush
```

**Affected Lines**:
- tools/brush.zia line 71: `if shape == brush.SHAPE_SQUARE`
- tools/eraser.zia line 68: similar issue

**Fix**: Either:
1. Use fully qualified constant access that works with Viperlang's module system, OR
2. Define shape constants locally in each tool file

---

### Bug 4: LoadBmp API - Width/Height Methods

**Status**: Needs investigation

**Description**:
canvas.zia attempts to get width/height from a loaded Pixels buffer:
```viper
width = Viper.Graphics.Pixels.Width(pixels);
height = Viper.Graphics.Pixels.Height(pixels);
```

Need to verify if Pixels has `.Width` and `.Height` properties or methods, or if width/height must be tracked separately.

**Potential Fix**:
- Check if `pixels.Width` and `pixels.Height` are valid property accesses
- Or store dimensions separately when loading

---

## Summary of Required Fixes

1. **canvas.zia**: Convert all `Viper.Graphics.Pixels.Method(pixels, ...)` to `pixels.Method(...)`
2. **ui/button.zia, ui/slider.zia**: Convert `Viper.Graphics.Canvas.Method(gfx, ...)` to `gfx.Method(...)`
3. **app.zia**: Same Canvas method conversion as above
4. **tools/*.zia**: Same Canvas method conversion for drawPreview functions
5. **tools/brush.zia, tools/eraser.zia**: Fix module constant access or inline the shape constants

---

## Compiler Behavior Notes

When testing incrementally:
- Simple single-file tests work (test_simple.zia compiles and runs)
- Multi-file imports work when the API calls are correct
- The "Undefined identifier: config" errors disappeared after adding explicit config import in test files
- The remaining errors are API usage issues, not compiler bugs

The Viperlang compiler correctly identifies invalid identifier access patterns.

---

## Confirmed Compiler Bug: Module Import Not Propagating

**Status**: CONFIRMED BUG

**Description**:
When module A imports module B which imports module C (config), the constants from C are not accessible in B when B is processed as part of A's import chain.

**Reproduction**:
1. main.zia imports app.zia
2. app.zia imports config.zia AND other modules (canvas, colors, brush)
3. canvas.zia imports config.zia
4. When compiling main.zia, canvas.zia's access to `config.COLOR_WHITE` fails with "Undefined identifier: config"

**Expected Behavior**:
Each module should have access to its own imports regardless of where it appears in the import chain.

**Actual Behavior**:
Modules lose access to their imports when they're imported as part of a larger chain.

**Workaround**:
Inline all constants from config directly into each module that needs them, avoiding cross-module constant access.

---

## Confirmed Compiler Bug: Entity init() auto-call with new Entity()

**Status**: CONFIRMED BUG

**Description**:
When using `new Entity()` syntax, the compiler automatically generates a call to `init()` with only the self parameter. If the entity's init method requires additional parameters, this causes an argument count mismatch error.

**Error Message**:
```
error: main:entry_0: call %t0: call arg count mismatch: @DrawingCanvas.init expects 3 arguments but got 1
```

**Affected Pattern**:
```viper
entity MyEntity {
    expose func init(width: Integer, height: Integer) { ... }
}
// In main:
var e = new MyEntity();   // Generates: create object, then call init(e) with only 1 arg
e.init(100, 100);         // This call is never reached due to error above
```

**Expected Behavior**:
`new Entity()` should either:
1. Not auto-call init(), or
2. Only call init() if a parameterless init exists

**Workaround**:
All entities must have a parameterless `init()` function. Rename parameterized init to something else like `setup()` or `initWithSize()`:
```viper
entity MyEntity {
    expose func init() { /* minimal setup */ }
    expose func setup(width: Integer, height: Integer) { /* full setup */ }
}
```

---

## Confirmed Compiler Bug: Entity type requires module prefix

**Status**: CONFIRMED BUG

**Description**:
When using entities from imported modules, both the type declaration and instantiation require the module name prefix. This is not immediately obvious from the import syntax.

**Correct Pattern**:
```viper
import "./canvas";

// Type declaration needs prefix
expose canvas.DrawingCanvas myCanvas;

// Instantiation needs prefix
myCanvas = new canvas.DrawingCanvas();
```

**Affected Files**:
All files that import and use entities from other modules.

---

## Confirmed Compiler/Runtime Bug: Mouse API Function Naming Mismatch

**Status**: CONFIRMED BUG

**Description**:
The Viperlang semantic analyzer (Sema_Runtime.cpp) defines mouse functions with different names than the runtime (runtime.def):

- Sema_Runtime.cpp: `Viper.Input.Mouse.GetX`, `Viper.Input.Mouse.GetY`, `Viper.Input.Mouse.IsDown`
- runtime.def: `Viper.Input.Mouse.X`, `Viper.Input.Mouse.Y`, `Viper.Input.Mouse.Left`

This causes "unknown callee" errors at runtime when trying to use mouse input.

**Error Message**:
```
error: PaintApp.handleInput:if_end_1: %19 = call: unknown callee @Viper.Input.Mouse.GetX
```

**Impact**:
Mouse input is currently unusable in Viperlang graphics applications.

**Workaround**:
Use keyboard controls only, similar to existing demos (snake, starfield, etc.) which use `canvas.KeyHeld(keycode)`.
