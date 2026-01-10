// eraser.viper - Eraser tool for Viper Paint
module eraser;

import "../config";
import "../canvas";
import "../colors";
import "../brush";

// Brush shape constants (must match brush.viper)
final SHAPE_ROUND = 0;
final SHAPE_SQUARE = 1;

// EraserTool - erases to background color with variable size
entity EraserTool {
    hide Integer drawing;       // 1 if currently drawing
    hide Integer lastX;         // Last mouse X
    hide Integer lastY;         // Last mouse Y

    expose func init() {
        drawing = 0;
        lastX = 0;
        lastY = 0;
    }

    expose func getName() -> String {
        return "Eraser";
    }

    expose func getIcon() -> String {
        return "E";
    }

    expose func getShortcut() -> Integer {
        return config.KEY_E;
    }

    expose func onMouseDown(x: Integer, y: Integer, canvas: DrawingCanvas, colors: ColorManager, brushSettings: BrushSettings) {
        drawing = 1;
        lastX = x;
        lastY = y;
        drawEraserStamp(x, y, canvas, colors, brushSettings);
    }

    expose func onMouseMove(x: Integer, y: Integer, canvas: DrawingCanvas, colors: ColorManager, brushSettings: BrushSettings) {
        if drawing == 1 {
            drawEraserLine(lastX, lastY, x, y, canvas, colors, brushSettings);
            lastX = x;
            lastY = y;
        }
    }

    expose func onMouseUp(x: Integer, y: Integer, canvas: DrawingCanvas, colors: ColorManager, brushSettings: BrushSettings) {
        drawing = 0;
    }

    expose func isDrawing() -> Integer {
        return drawing;
    }

    // Draw eraser stamp (uses background color)
    hide func drawEraserStamp(cx: Integer, cy: Integer, canvas: DrawingCanvas, colors: ColorManager, brushSettings: BrushSettings) {
        var size = brushSettings.size;
        var half = size / 2;
        var color = canvas.backgroundColor;  // Erase to canvas background
        var shape = brushSettings.shape;

        var dy = 0 - half;
        while dy <= half {
            var dx = 0 - half;
            while dx <= half {
                var inBrush = 0;
                if shape == SHAPE_SQUARE {
                    inBrush = 1;
                } else {
                    if dx * dx + dy * dy <= half * half {
                        inBrush = 1;
                    }
                }
                if inBrush == 1 {
                    canvas.setPixel(cx + dx, cy + dy, color);
                }
                dx = dx + 1;
            }
            dy = dy + 1;
        }
    }

    // Draw eraser stamps along a line
    hide func drawEraserLine(x1: Integer, y1: Integer, x2: Integer, y2: Integer, canvas: DrawingCanvas, colors: ColorManager, brushSettings: BrushSettings) {
        var dx = x2 - x1;
        var dy = y2 - y1;
        if dx < 0 { dx = 0 - dx; }
        if dy < 0 { dy = 0 - dy; }

        var steps = dx;
        if dy > dx {
            steps = dy;
        }
        if steps == 0 {
            steps = 1;
        }

        var spacing = brushSettings.size / 4;
        if spacing < 1 {
            spacing = 1;
        }

        var i = 0;
        while i <= steps {
            var t = i * 100 / steps;
            var px = x1 + (x2 - x1) * t / 100;
            var py = y1 + (y2 - y1) * t / 100;
            drawEraserStamp(px, py, canvas, colors, brushSettings);
            i = i + spacing;
        }
    }
}
