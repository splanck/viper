// eyedropper.viper - Eyedropper (color picker) tool for Viper Paint
module eyedropper;

import "../config";
import "../canvas";
import "../colors";

// EyedropperTool - picks a color from the canvas
entity EyedropperTool {
    expose Integer lastPickedColor;  // Last color that was picked

    expose func init() {
        lastPickedColor = 0;
    }

    expose func getName() -> String {
        return "Eyedropper";
    }

    expose func getIcon() -> String {
        return "I";
    }

    expose func getShortcut() -> Integer {
        return config.KEY_I;
    }

    expose func onMouseDown(x: Integer, y: Integer, canvas: DrawingCanvas, colors: ColorManager) {
        // Pick color at position
        var color = canvas.getPixel(x, y);
        if color != 0 {
            colors.setForeground(color);
            lastPickedColor = color;
        }
    }

    expose func onMouseMove(x: Integer, y: Integer, canvas: DrawingCanvas, colors: ColorManager) {
        // Could show preview of color under cursor
    }

    expose func onMouseUp(x: Integer, y: Integer, canvas: DrawingCanvas, colors: ColorManager) {
        // Eyedropper doesn't do anything on mouse up
    }

    expose func isDrawing() -> Integer {
        return 0;  // Eyedropper is instant
    }
}
