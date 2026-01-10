# Viper Paint - Full-Featured Paint Application

## Overview

A professional-quality paint application written in Viperlang, showcasing the full capabilities of Viper.GUI.* and Viper.Graphics.* APIs with proper OOP architecture.

**Target:** `demos/viperlang/paint/`

### Design Decisions
- **UI Approach:** Hybrid - Use Viper.GUI.* widgets (Button, Slider, Label) for standard controls; custom-draw specialized elements (color palette grid, canvas preview)
- **File Structure:** Multi-file with imports (`import "./tools/brush";`)
- **Default Canvas:** 1024x768 pixels
- **Import Syntax:** Relative paths like the ladders demo (`import "./config";`)

---

## Application Features

### Drawing Tools
1. **Pencil** - Freehand drawing, 1px line
2. **Brush** - Variable size soft brush with opacity
3. **Eraser** - Erase to background/transparent
4. **Line** - Straight lines with preview
5. **Rectangle** - Filled or outline rectangles
6. **Ellipse** - Filled or outline circles/ellipses
7. **Fill Bucket** - Flood fill with tolerance
8. **Eyedropper** - Pick color from canvas
9. **Text** - Add text annotations

### Color System
- Foreground/Background color (click to swap)
- 16-color quick palette
- RGB sliders (0-255)
- HSL sliders (Hue 0-360, Sat/Light 0-100)
- Recent colors history (last 8 used)
- Hex color input

### Brush Settings
- Size: 1-64 pixels (slider)
- Opacity: 0-100% (slider)
- Shape: Round/Square toggle

### Canvas Operations
- New canvas (with size dialog)
- Clear canvas
- Resize canvas
- Crop to selection

### File Operations
- New (Ctrl+N)
- Open BMP (Ctrl+O)
- Save BMP (Ctrl+S)
- Save As BMP (Ctrl+Shift+S)

### Edit Operations
- Undo (Ctrl+Z) - 20 levels
- Redo (Ctrl+Y)
- Cut/Copy/Paste selection
- Select All (Ctrl+A)
- Deselect (Escape)

### Selection Tools
- Rectangle Select - drag to select region
- Move Selection - drag selected region
- Delete Selection (Delete key)

### View Features
- Zoom: 25%, 50%, 100%, 200%, 400%
- Fit to Window
- Show/Hide Grid (for pixel art)
- Canvas size in status bar
- Mouse coordinates in status bar
- Current tool name in status bar

---

## UI Layout

```
+------------------------------------------------------------------+
|  [New] [Open] [Save]  |  [Undo] [Redo]  |  Zoom: [100% v]        |
+------------------------------------------------------------------+
|      |                                              |            |
| TOOLS|              MAIN CANVAS                     |   COLOR    |
|      |                                              |   PANEL    |
| [P]  |    +--------------------------------+        |            |
| [B]  |    |                                |        | [FG] [BG]  |
| [E]  |    |                                |        |  (swap)    |
| [L]  |    |                                |        |            |
| [R]  |    |        Drawing Area            |        | [palette]  |
| [O]  |    |                                |        | 16 colors  |
| [F]  |    |                                |        |            |
| [I]  |    |                                |        | R: [===]   |
| [T]  |    |                                |        | G: [===]   |
|      |    +--------------------------------+        | B: [===]   |
|------|                                              |            |
| Size |                                              | Recent:    |
| [==] |                                              | [8 colors] |
| 16px |                                              |            |
|------|                                              |------------|
|Opac. |                                              | Brush Size |
| [==] |                                              | [=====] 8  |
| 100% |                                              |            |
+------+----------------------------------------------+------------+
| Ready | Tool: Brush | Size: 8 | Pos: 234, 156 | Canvas: 800x600  |
+------------------------------------------------------------------+
```

### Panel Breakdown

**Toolbar (Top - 40px height)**
- File buttons: New, Open, Save
- Edit buttons: Undo, Redo
- Zoom dropdown

**Tool Panel (Left - 48px width)**
- 9 tool buttons in vertical column
- Visual indicator for selected tool
- Keyboard shortcuts (P, B, E, L, R, O, F, I, T)

**Color Panel (Right - 120px width)**
- FG/BG color swatches (click to swap)
- 4x4 color palette grid
- RGB sliders with value labels
- Recent colors row

**Brush Settings (Below tools)**
- Size slider (1-64)
- Opacity slider (0-100%)

**Canvas Area (Center)**
- Scrollable if zoomed > 100%
- Checkerboard pattern for transparency
- Grid overlay option

**Status Bar (Bottom - 24px height)**
- Status message
- Current tool name
- Brush size
- Mouse coordinates
- Canvas dimensions

---

## OOP Architecture

### Class Hierarchy

```
ITool (interface)
  +-- PencilTool
  +-- BrushTool
  +-- EraserTool
  +-- LineTool
  +-- RectangleTool
  +-- EllipseTool
  +-- FillTool
  +-- EyedropperTool
  +-- TextTool
  +-- SelectTool

DrawingCanvas
  - pixels: Pixels buffer
  - width, height
  - selection: Selection
  - draw(), clear(), resize()
  - getPixel(), setPixel()

Selection
  - x, y, width, height
  - active: Boolean
  - pixels: Pixels (copied content)

HistoryManager
  - undoStack: List[CanvasState]
  - redoStack: List[CanvasState]
  - pushState(), undo(), redo()

ColorManager
  - foreground: Integer (ARGB)
  - background: Integer (ARGB)
  - palette: List[Integer]
  - recentColors: List[Integer]
  - swap(), addRecent()

BrushSettings
  - size: Integer (1-64)
  - opacity: Integer (0-100)
  - shape: Integer (0=round, 1=square)

UI Components:
  ToolPanel
  ColorPanel
  BrushPanel
  StatusBar
  Toolbar

PaintApp (main controller)
  - canvas: DrawingCanvas
  - tools: List[ITool]
  - activeTool: ITool
  - colors: ColorManager
  - brush: BrushSettings
  - history: HistoryManager
  - ui components...
  - run(), handleInput(), render()
```

---

## File Structure

```
demos/viperlang/paint/
  main.viper           - Entry point, main loop
  app.viper            - PaintApp controller class
  canvas.viper         - DrawingCanvas, Selection classes
  tools/
    tool.viper         - ITool interface
    pencil.viper       - PencilTool
    brush.viper        - BrushTool
    eraser.viper       - EraserTool
    line.viper         - LineTool
    rectangle.viper    - RectangleTool
    ellipse.viper      - EllipseTool
    fill.viper         - FillTool
    eyedropper.viper   - EyedropperTool
    text.viper         - TextTool
    select.viper       - SelectTool
  ui/
    toolbar.viper      - Top toolbar
    toolpanel.viper    - Left tool buttons
    colorpanel.viper   - Right color controls
    brushpanel.viper   - Brush settings
    statusbar.viper    - Bottom status
  history.viper        - HistoryManager, CanvasState
  colors.viper         - ColorManager
  brush.viper          - BrushSettings
  README.md            - Documentation
```

---

## Detailed Class Designs

### ITool Interface

```viper
interface ITool {
    func getName() -> String;
    func getIcon() -> String;        // Single char for button
    func getShortcut() -> Integer;   // Key code

    func onMouseDown(x: Integer, y: Integer, canvas: DrawingCanvas);
    func onMouseMove(x: Integer, y: Integer, canvas: DrawingCanvas);
    func onMouseUp(x: Integer, y: Integer, canvas: DrawingCanvas);
    func onKeyPress(key: Integer, canvas: DrawingCanvas);

    func drawPreview(gfx: Canvas);   // Draw tool preview overlay
    func isDrawing() -> Boolean;     // Currently in a stroke?
}
```

### BrushTool Example

```viper
entity BrushTool implements ITool {
    hide Boolean drawing;
    hide Integer lastX;
    hide Integer lastY;
    hide BrushSettings brush;
    hide ColorManager colors;

    expose func init(b: BrushSettings, c: ColorManager) {
        brush = b;
        colors = c;
        drawing = false;
    }

    expose func getName() -> String { return "Brush"; }
    expose func getIcon() -> String { return "B"; }
    expose func getShortcut() -> Integer { return 66; } // 'B'

    expose func onMouseDown(x: Integer, y: Integer, canvas: DrawingCanvas) {
        drawing = true;
        lastX = x;
        lastY = y;
        drawBrushStamp(x, y, canvas);
    }

    expose func onMouseMove(x: Integer, y: Integer, canvas: DrawingCanvas) {
        if drawing {
            // Interpolate between last and current for smooth strokes
            drawBrushLine(lastX, lastY, x, y, canvas);
            lastX = x;
            lastY = y;
        }
    }

    expose func onMouseUp(x: Integer, y: Integer, canvas: DrawingCanvas) {
        drawing = false;
    }

    hide func drawBrushStamp(x: Integer, y: Integer, canvas: DrawingCanvas) {
        var size = brush.size;
        var half = size / 2;
        var color = colors.foreground;

        // Draw filled circle for round brush
        var dy = 0 - half;
        while dy <= half {
            var dx = 0 - half;
            while dx <= half {
                if dx * dx + dy * dy <= half * half {
                    canvas.setPixelBlend(x + dx, y + dy, color, brush.opacity);
                }
                dx = dx + 1;
            }
            dy = dy + 1;
        }
    }

    hide func drawBrushLine(x1: Integer, y1: Integer, x2: Integer, y2: Integer, canvas: DrawingCanvas) {
        // Bresenham line with brush stamps
        var dx = x2 - x1;
        var dy = y2 - y1;
        if dx < 0 { dx = 0 - dx; }
        if dy < 0 { dy = 0 - dy; }

        var steps = dx;
        if dy > dx { steps = dy; }
        if steps == 0 { steps = 1; }

        var i = 0;
        while i <= steps {
            var t = i * 100 / steps;
            var px = x1 + (x2 - x1) * t / 100;
            var py = y1 + (y2 - y1) * t / 100;
            drawBrushStamp(px, py, canvas);
            i = i + 1;
        }
    }
}
```

### DrawingCanvas

```viper
entity DrawingCanvas {
    hide Pixels pixels;
    expose Integer width;
    expose Integer height;
    expose Selection selection;
    expose Integer backgroundColor;

    expose func init(w: Integer, h: Integer) {
        width = w;
        height = h;
        backgroundColor = 0xFFFFFFFF; // White
        pixels = Viper.Graphics.Pixels.New(w, h);
        Viper.Graphics.Pixels.Fill(pixels, backgroundColor);
        selection = new Selection();
    }

    // Default constructor with standard size
    expose func initDefault() {
        init(1024, 768);
    }

    expose func getPixel(x: Integer, y: Integer) -> Integer {
        if x >= 0 and x < width and y >= 0 and y < height {
            return Viper.Graphics.Pixels.Get(pixels, x, y);
        }
        return 0;
    }

    expose func setPixel(x: Integer, y: Integer, color: Integer) {
        if x >= 0 and x < width and y >= 0 and y < height {
            Viper.Graphics.Pixels.Set(pixels, x, y, color);
        }
    }

    expose func setPixelBlend(x: Integer, y: Integer, color: Integer, opacity: Integer) {
        if x >= 0 and x < width and y >= 0 and y < height {
            var existing = Viper.Graphics.Pixels.Get(pixels, x, y);
            var blended = Viper.Graphics.Color.Lerp(existing, color, opacity);
            Viper.Graphics.Pixels.Set(pixels, x, y, blended);
        }
    }

    expose func clear() {
        Viper.Graphics.Pixels.Fill(pixels, backgroundColor);
    }

    expose func floodFill(x: Integer, y: Integer, newColor: Integer) {
        var targetColor = getPixel(x, y);
        if targetColor == newColor { return; }
        floodFillRecursive(x, y, targetColor, newColor);
    }

    expose func line(x1: Integer, y1: Integer, x2: Integer, y2: Integer, color: Integer) {
        // Bresenham's line algorithm
        // ... implementation
    }

    expose func rect(x: Integer, y: Integer, w: Integer, h: Integer, color: Integer, filled: Boolean) {
        // Rectangle drawing
        // ... implementation
    }

    expose func ellipse(cx: Integer, cy: Integer, rx: Integer, ry: Integer, color: Integer, filled: Boolean) {
        // Midpoint ellipse algorithm
        // ... implementation
    }

    expose func save(filename: String) -> Boolean {
        return Viper.Graphics.Pixels.SaveBmp(pixels, filename);
    }

    expose func load(filename: String) -> Boolean {
        var loaded = Viper.Graphics.Pixels.LoadBmp(filename);
        if loaded != null {
            pixels = loaded;
            width = Viper.Graphics.Pixels.Width(loaded);
            height = Viper.Graphics.Pixels.Height(loaded);
            return true;
        }
        return false;
    }

    expose func getPixels() -> Pixels {
        return pixels;
    }

    expose func copyState() -> Pixels {
        return Viper.Graphics.Pixels.Clone(pixels);
    }

    expose func restoreState(state: Pixels) {
        pixels = Viper.Graphics.Pixels.Clone(state);
    }
}
```

### HistoryManager

```viper
entity CanvasState {
    expose Pixels pixels;
    expose String description;

    expose func init(p: Pixels, desc: String) {
        pixels = Viper.Graphics.Pixels.Clone(p);
        description = desc;
    }
}

entity HistoryManager {
    hide List[CanvasState] undoStack;
    hide List[CanvasState] redoStack;
    hide Integer maxHistory;

    expose func init() {
        undoStack = new List[CanvasState]();
        redoStack = new List[CanvasState]();
        maxHistory = 20;
    }

    expose func pushState(canvas: DrawingCanvas, description: String) {
        var state = new CanvasState(canvas.getPixels(), description);
        undoStack.add(state);

        // Clear redo stack on new action
        redoStack.clear();

        // Limit history size
        while undoStack.count() > maxHistory {
            undoStack.removeAt(0);
        }
    }

    expose func undo(canvas: DrawingCanvas) -> Boolean {
        if undoStack.count() == 0 { return false; }

        // Save current state to redo
        var current = new CanvasState(canvas.getPixels(), "redo");
        redoStack.add(current);

        // Restore previous state
        var prev = undoStack.removeLast();
        canvas.restoreState(prev.pixels);
        return true;
    }

    expose func redo(canvas: DrawingCanvas) -> Boolean {
        if redoStack.count() == 0 { return false; }

        // Save current to undo
        var current = new CanvasState(canvas.getPixels(), "undo");
        undoStack.add(current);

        // Restore redo state
        var next = redoStack.removeLast();
        canvas.restoreState(next.pixels);
        return true;
    }

    expose func canUndo() -> Boolean {
        return undoStack.count() > 0;
    }

    expose func canRedo() -> Boolean {
        return redoStack.count() > 0;
    }
}
```

### ColorManager

```viper
entity ColorManager {
    expose Integer foreground;
    expose Integer background;
    expose List[Integer] palette;
    expose List[Integer] recentColors;
    hide Integer maxRecent;

    expose func init() {
        foreground = 0xFF000000; // Black
        background = 0xFFFFFFFF; // White
        maxRecent = 8;
        recentColors = new List[Integer]();

        // Initialize 16-color palette
        palette = new List[Integer]();
        palette.add(0xFF000000); // Black
        palette.add(0xFFFFFFFF); // White
        palette.add(0xFFFF0000); // Red
        palette.add(0xFF00FF00); // Green
        palette.add(0xFF0000FF); // Blue
        palette.add(0xFFFFFF00); // Yellow
        palette.add(0xFFFF00FF); // Magenta
        palette.add(0xFF00FFFF); // Cyan
        palette.add(0xFF808080); // Gray
        palette.add(0xFFC0C0C0); // Light Gray
        palette.add(0xFF800000); // Dark Red
        palette.add(0xFF008000); // Dark Green
        palette.add(0xFF000080); // Dark Blue
        palette.add(0xFF808000); // Olive
        palette.add(0xFF800080); // Purple
        palette.add(0xFF008080); // Teal
    }

    expose func swap() {
        var temp = foreground;
        foreground = background;
        background = temp;
    }

    expose func setForeground(color: Integer) {
        foreground = color;
        addRecent(color);
    }

    expose func setBackground(color: Integer) {
        background = color;
    }

    expose func addRecent(color: Integer) {
        // Don't add duplicates
        var i = 0;
        while i < recentColors.count() {
            if recentColors.get(i) == color {
                recentColors.removeAt(i);
                break;
            }
            i = i + 1;
        }

        // Add to front
        recentColors.insert(0, color);

        // Limit size
        while recentColors.count() > maxRecent {
            recentColors.removeLast();
        }
    }

    expose func getRed() -> Integer {
        return Viper.Graphics.Color.GetR(foreground);
    }

    expose func getGreen() -> Integer {
        return Viper.Graphics.Color.GetG(foreground);
    }

    expose func getBlue() -> Integer {
        return Viper.Graphics.Color.GetB(foreground);
    }

    expose func setRGB(r: Integer, g: Integer, b: Integer) {
        foreground = Viper.Graphics.Color.RGB(r, g, b);
        addRecent(foreground);
    }
}
```

### PaintApp (Main Controller)

```viper
entity PaintApp {
    // Core components
    hide DrawingCanvas canvas;
    hide HistoryManager history;
    hide ColorManager colors;
    hide BrushSettings brush;

    // Tools
    hide List[ITool] tools;
    hide ITool activeTool;
    hide Integer activeToolIndex;

    // UI state
    hide Boolean running;
    hide Integer mouseX;
    hide Integer mouseY;
    hide Boolean mouseDown;
    hide String statusMessage;
    hide Integer zoomLevel; // 25, 50, 100, 200, 400
    hide String currentFilename;
    hide Boolean modified;

    // Window/Graphics
    hide Viper.GUI.App app;
    hide Viper.Graphics.Canvas gfx;

    // UI dimensions
    hide Integer windowWidth;
    hide Integer windowHeight;
    hide Integer toolPanelWidth;
    hide Integer colorPanelWidth;
    hide Integer toolbarHeight;
    hide Integer statusBarHeight;

    expose func init() {
        windowWidth = 1024;
        windowHeight = 768;
        toolPanelWidth = 48;
        colorPanelWidth = 140;
        toolbarHeight = 40;
        statusBarHeight = 24;

        // Create main window with graphics canvas for direct drawing
        gfx = Viper.Graphics.Canvas.New("Viper Paint", windowWidth, windowHeight);

        // Initialize subsystems
        canvas = new DrawingCanvas(800, 600);
        history = new HistoryManager();
        colors = new ColorManager();
        brush = new BrushSettings();

        // Initialize tools
        initTools();
        activeToolIndex = 1; // Brush
        activeTool = tools.get(activeToolIndex);

        running = true;
        modified = false;
        currentFilename = "";
        statusMessage = "Ready";
        zoomLevel = 100;

        // Save initial state for undo
        history.pushState(canvas, "New canvas");
    }

    hide func initTools() {
        tools = new List[ITool]();
        tools.add(new PencilTool(brush, colors));
        tools.add(new BrushTool(brush, colors));
        tools.add(new EraserTool(brush, colors));
        tools.add(new LineTool(brush, colors));
        tools.add(new RectangleTool(brush, colors));
        tools.add(new EllipseTool(brush, colors));
        tools.add(new FillTool(colors));
        tools.add(new EyedropperTool(colors));
        tools.add(new SelectTool());
    }

    expose func run() {
        while running {
            handleInput();
            update();
            render();
            Viper.Time.SleepMs(16); // ~60 FPS
        }
    }

    hide func handleInput() {
        gfx.Poll();

        if gfx.ShouldClose != 0 {
            running = false;
            return;
        }

        // Get mouse state
        var mx = gfx.MouseX;
        var my = gfx.MouseY;
        var wasDown = mouseDown;
        mouseDown = gfx.MouseButton(0) != 0;

        // Convert to canvas coordinates
        var canvasX = screenToCanvasX(mx);
        var canvasY = screenToCanvasY(my);

        // Mouse events to active tool
        if mouseDown and not wasDown {
            // Mouse down
            if isInCanvasArea(mx, my) {
                history.pushState(canvas, activeTool.getName());
                activeTool.onMouseDown(canvasX, canvasY, canvas);
                modified = true;
            } else {
                handleUIClick(mx, my);
            }
        } else if mouseDown and wasDown {
            // Mouse drag
            if activeTool.isDrawing() {
                activeTool.onMouseMove(canvasX, canvasY, canvas);
            }
        } else if not mouseDown and wasDown {
            // Mouse up
            activeTool.onMouseUp(canvasX, canvasY, canvas);
        }

        mouseX = mx;
        mouseY = my;

        // Keyboard shortcuts
        handleKeyboard();
    }

    hide func handleKeyboard() {
        // Tool shortcuts
        if gfx.KeyHeld(80) { selectTool(0); } // P - Pencil
        if gfx.KeyHeld(66) { selectTool(1); } // B - Brush
        if gfx.KeyHeld(69) { selectTool(2); } // E - Eraser
        if gfx.KeyHeld(76) { selectTool(3); } // L - Line
        if gfx.KeyHeld(82) { selectTool(4); } // R - Rectangle
        if gfx.KeyHeld(79) { selectTool(5); } // O - Ellipse
        if gfx.KeyHeld(70) { selectTool(6); } // F - Fill
        if gfx.KeyHeld(73) { selectTool(7); } // I - Eyedropper

        // Ctrl shortcuts
        // Note: would need modifier key detection
        // Ctrl+Z = Undo, Ctrl+Y = Redo, Ctrl+S = Save, etc.

        // X to swap colors
        if gfx.KeyHeld(88) { colors.swap(); }

        // [ and ] for brush size
        if gfx.KeyHeld(91) { brush.decreaseSize(); }
        if gfx.KeyHeld(93) { brush.increaseSize(); }
    }

    hide func selectTool(index: Integer) {
        if index >= 0 and index < tools.count() {
            activeToolIndex = index;
            activeTool = tools.get(index);
            statusMessage = "Tool: " + activeTool.getName();
        }
    }

    hide func render() {
        // Clear screen
        gfx.Clear(0xFF2D2D2D); // Dark gray background

        // Draw UI panels
        drawToolbar();
        drawToolPanel();
        drawColorPanel();
        drawStatusBar();

        // Draw canvas area
        drawCanvasArea();

        // Draw canvas content
        drawCanvas();

        // Draw tool preview overlay
        activeTool.drawPreview(gfx);

        // Flip buffers
        gfx.Flip();
    }

    hide func drawCanvas() {
        var offsetX = toolPanelWidth + 10;
        var offsetY = toolbarHeight + 10;

        // Draw checkerboard for transparency
        drawCheckerboard(offsetX, offsetY, canvas.width, canvas.height);

        // Blit canvas pixels
        gfx.BlitAlpha(offsetX, offsetY, canvas.getPixels());

        // Draw selection if active
        if canvas.selection.active {
            drawSelection(offsetX, offsetY);
        }
    }

    // ... more rendering methods
}
```

---

## Implementation Phases

### Phase 1: Core Infrastructure
1. Create project structure
2. Implement DrawingCanvas with basic pixel operations
3. Implement ColorManager with palette
4. Implement BrushSettings
5. Create main application shell with render loop

### Phase 2: Basic Tools
1. Implement ITool interface
2. Create PencilTool (1px drawing)
3. Create BrushTool (variable size)
4. Create EraserTool
5. Wire up mouse input to tools

### Phase 3: Shape Tools
1. Create LineTool with preview
2. Create RectangleTool (filled/outline)
3. Create EllipseTool (filled/outline)
4. Create FillTool (flood fill)

### Phase 4: UI Panels
1. Create ToolPanel with tool buttons
2. Create ColorPanel with palette and sliders
3. Create Toolbar with file/edit buttons
4. Create StatusBar

### Phase 5: History & Files
1. Implement HistoryManager
2. Add Undo/Redo functionality
3. Implement Save/Load BMP
4. Add keyboard shortcuts

### Phase 6: Advanced Features
1. Implement SelectTool with rectangle selection
2. Add selection operations (move, delete, copy)
3. Add EyedropperTool
4. Add zoom functionality

### Phase 7: Polish
1. Add grid overlay option
2. Improve tool previews
3. Add confirmation dialogs
4. Add error handling
5. Performance optimization

---

## Technical Considerations

### Performance
- Use dirty rectangles to minimize redraws
- Only update changed portions of canvas
- Limit undo history to prevent memory bloat
- Use integer math where possible

### User Experience
- Visual feedback for all actions
- Cursor changes for different tools
- Smooth brush strokes with interpolation
- Responsive even on large canvases

### Memory Management
- Clone pixels buffers for undo states
- Limit undo stack size (20 levels)
- Clear redo stack on new actions
- Efficient flood fill (iterative, not recursive)

---

## Verification

### Manual Testing
1. Launch application - window opens correctly
2. Select each tool - visual feedback works
3. Draw with pencil - single pixel lines
4. Draw with brush - variable size strokes
5. Draw shapes - preview shows while dragging
6. Use fill tool - flood fills correctly
7. Use eyedropper - picks correct color
8. Change colors via palette and sliders
9. Undo/Redo - history works correctly
10. Save/Load BMP - files round-trip correctly
11. Zoom in/out - canvas scales properly

### Edge Cases
- Drawing at canvas edges
- Very large brush sizes
- Filling complex regions
- Loading invalid BMP files
- Maximum undo history reached

---

## Files to Create

| File | Purpose | Lines (est.) |
|------|---------|--------------|
| main.viper | Entry point | 30 |
| app.viper | PaintApp class | 400 |
| canvas.viper | DrawingCanvas, Selection | 250 |
| tools/tool.viper | ITool interface | 30 |
| tools/pencil.viper | PencilTool | 80 |
| tools/brush.viper | BrushTool | 120 |
| tools/eraser.viper | EraserTool | 80 |
| tools/line.viper | LineTool | 100 |
| tools/rectangle.viper | RectangleTool | 100 |
| tools/ellipse.viper | EllipseTool | 120 |
| tools/fill.viper | FillTool | 100 |
| tools/eyedropper.viper | EyedropperTool | 60 |
| tools/select.viper | SelectTool | 150 |
| ui/toolbar.viper | Toolbar panel | 100 |
| ui/toolpanel.viper | Tool buttons | 100 |
| ui/colorpanel.viper | Color controls | 150 |
| ui/statusbar.viper | Status display | 60 |
| history.viper | HistoryManager | 100 |
| colors.viper | ColorManager | 120 |
| brush.viper | BrushSettings | 50 |
| **Total** | | **~2100** |

---

## Resolved Design Questions

1. **Module system**: ✅ Viperlang supports multi-file modules with relative imports (`import "./tools/brush";`)

2. **UI approach**: ✅ Hybrid - Use GUI widgets for controls, custom-draw specialized elements like the color palette

3. **Default size**: ✅ 1024x768 pixels

4. **Mouse input**: Use `Viper.Graphics.Canvas` polling for the drawing area:
   - `canvas.Poll()` - Process events
   - `canvas.MouseX`, `canvas.MouseY` - Current position
   - `canvas.MouseButton(0)` - Left button state

## Implementation Notes

### Hybrid UI Strategy

Since we're using a hybrid approach, the architecture will be:

1. **Main Window**: Use `Viper.Graphics.Canvas.New()` for the main window (enables direct pixel drawing)
2. **Drawing Area**: Direct pixel manipulation via `Viper.Graphics.Pixels` buffer, blit to canvas
3. **UI Panels**: Draw UI elements manually with `canvas.Box()`, `canvas.Text()`, etc.
4. **Buttons/Controls**: Implement a simple button system using hit-testing and visual states

This approach gives us full control over rendering while avoiding the complexity of mixing two windowing systems.

### Color Palette Implementation

The color palette will be custom-drawn as a grid:
```viper
// Draw 4x4 palette grid
var i = 0;
while i < 16 {
    var row = i / 4;
    var col = i % 4;
    var x = paletteX + col * 24;
    var y = paletteY + row * 24;
    canvas.BoxFill(x, y, 22, 22, colors.palette.get(i));
    canvas.Frame(x, y, 22, 22, 0xFF000000);
    i = i + 1;
}
```

### Slider Implementation

Simple slider using hit-testing:
```viper
entity Slider {
    expose Integer x;
    expose Integer y;
    expose Integer width;
    expose Integer value;    // 0-100
    expose Integer minVal;
    expose Integer maxVal;
    hide Boolean dragging;

    expose func draw(canvas: Viper.Graphics.Canvas) {
        // Track background
        canvas.BoxFill(x, y + 4, width, 8, 0xFF404040);
        // Thumb position
        var thumbX = x + (value - minVal) * width / (maxVal - minVal);
        canvas.BoxFill(thumbX - 4, y, 8, 16, 0xFFCCCCCC);
    }

    expose func handleMouse(mx: Integer, my: Integer, pressed: Boolean) -> Boolean {
        if pressed and my >= y and my <= y + 16 and mx >= x and mx <= x + width {
            dragging = true;
        }
        if dragging {
            if pressed {
                var newVal = minVal + (mx - x) * (maxVal - minVal) / width;
                if newVal < minVal { newVal = minVal; }
                if newVal > maxVal { newVal = maxVal; }
                value = newVal;
            } else {
                dragging = false;
            }
            return true;
        }
        return false;
    }
}
```
