// fill.viper - Fill (flood fill) tool for Viper Paint
module fill;

import "../config";
import "../canvas";
import "../colors";

// FillTool - flood fills an area with the foreground color
entity FillTool {
    expose func init() {
        // No state needed
    }

    expose func getName() -> String {
        return "Fill";
    }

    expose func getIcon() -> String {
        return "F";
    }

    expose func getShortcut() -> Integer {
        return config.KEY_F;
    }

    expose func onMouseDown(x: Integer, y: Integer, canvas: DrawingCanvas, colors: ColorManager) {
        // Flood fill at click position
        canvas.floodFill(x, y, colors.foreground);
    }

    expose func onMouseMove(x: Integer, y: Integer, canvas: DrawingCanvas, colors: ColorManager) {
        // Fill tool doesn't do anything on move
    }

    expose func onMouseUp(x: Integer, y: Integer, canvas: DrawingCanvas, colors: ColorManager) {
        // Fill tool doesn't do anything on mouse up
    }

    expose func isDrawing() -> Integer {
        return 0;  // Fill is instant, not a drag operation
    }
}
