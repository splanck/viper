// rectangle.viper - Rectangle tool for Viper Paint
module rectangle;

import "../config";
import "../canvas";
import "../colors";

// RectangleTool - draws rectangles (filled or outline)
entity RectangleTool {
    hide Integer drawing;       // 1 if currently drawing
    expose Integer startX;      // Start corner X
    expose Integer startY;      // Start corner Y
    expose Integer endX;        // End corner X (current mouse)
    expose Integer endY;        // End corner Y
    expose Integer filled;      // 1 = filled, 0 = outline

    expose func init() {
        drawing = 0;
        startX = 0;
        startY = 0;
        endX = 0;
        endY = 0;
        filled = 1;  // Default to filled
    }

    expose func getName() -> String {
        return "Rectangle";
    }

    expose func getIcon() -> String {
        return "R";
    }

    expose func getShortcut() -> Integer {
        return config.KEY_R;
    }

    expose func toggleFilled() {
        if filled == 1 {
            filled = 0;
        } else {
            filled = 1;
        }
    }

    expose func onMouseDown(x: Integer, y: Integer, canvas: DrawingCanvas, colors: ColorManager) {
        drawing = 1;
        startX = x;
        startY = y;
        endX = x;
        endY = y;
    }

    expose func onMouseMove(x: Integer, y: Integer, canvas: DrawingCanvas, colors: ColorManager) {
        if drawing == 1 {
            endX = x;
            endY = y;
        }
    }

    expose func onMouseUp(x: Integer, y: Integer, canvas: DrawingCanvas, colors: ColorManager) {
        if drawing == 1 {
            // Calculate normalized rectangle
            var x1 = startX;
            var y1 = startY;
            var x2 = x;
            var y2 = y;
            if x2 < x1 {
                var tmp = x1;
                x1 = x2;
                x2 = tmp;
            }
            if y2 < y1 {
                var tmp = y1;
                y1 = y2;
                y2 = tmp;
            }
            var w = x2 - x1 + 1;
            var h = y2 - y1 + 1;

            if filled == 1 {
                canvas.drawRectFill(x1, y1, w, h, colors.foreground);
            } else {
                canvas.drawRect(x1, y1, w, h, colors.foreground);
            }
            drawing = 0;
        }
    }

    expose func isDrawing() -> Integer {
        return drawing;
    }

    // Draw preview on screen canvas
    expose func drawPreview(gfx: Viper.Graphics.Canvas, offsetX: Integer, offsetY: Integer, color: Integer) {
        if drawing == 1 {
            var x1 = startX;
            var y1 = startY;
            var x2 = endX;
            var y2 = endY;
            if x2 < x1 {
                var tmp = x1;
                x1 = x2;
                x2 = tmp;
            }
            if y2 < y1 {
                var tmp = y1;
                y1 = y2;
                y2 = tmp;
            }
            var w = x2 - x1 + 1;
            var h = y2 - y1 + 1;

            // Draw rectangle outline as preview
            gfx.Frame(offsetX + x1, offsetY + y1, w, h, color);
        }
    }
}
