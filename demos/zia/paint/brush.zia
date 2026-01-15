// brush.viper - Brush settings for Viper Paint
module brush;

import "./config";

// Brush shapes
final SHAPE_ROUND = 0;
final SHAPE_SQUARE = 1;

// BrushSettings entity - manages brush size, opacity, and shape
entity BrushSettings {
    expose Integer size;        // Brush size in pixels (1-64)
    expose Integer opacity;     // Opacity percentage (0-100)
    expose Integer shape;       // Brush shape (SHAPE_ROUND or SHAPE_SQUARE)

    // Initialize with defaults
    expose func init() {
        size = config.DEFAULT_BRUSH_SIZE;
        opacity = config.DEFAULT_OPACITY;
        shape = SHAPE_ROUND;
    }

    // Set brush size with clamping
    expose func setSize(newSize: Integer) {
        if newSize < config.MIN_BRUSH_SIZE {
            size = config.MIN_BRUSH_SIZE;
        } else if newSize > config.MAX_BRUSH_SIZE {
            size = config.MAX_BRUSH_SIZE;
        } else {
            size = newSize;
        }
    }

    // Increase brush size
    expose func increaseSize() {
        if size < config.MAX_BRUSH_SIZE {
            if size < 4 {
                size = size + 1;
            } else if size < 16 {
                size = size + 2;
            } else {
                size = size + 4;
            }
            if size > config.MAX_BRUSH_SIZE {
                size = config.MAX_BRUSH_SIZE;
            }
        }
    }

    // Decrease brush size
    expose func decreaseSize() {
        if size > config.MIN_BRUSH_SIZE {
            if size <= 4 {
                size = size - 1;
            } else if size <= 16 {
                size = size - 2;
            } else {
                size = size - 4;
            }
            if size < config.MIN_BRUSH_SIZE {
                size = config.MIN_BRUSH_SIZE;
            }
        }
    }

    // Set opacity with clamping
    expose func setOpacity(newOpacity: Integer) {
        if newOpacity < config.MIN_OPACITY {
            opacity = config.MIN_OPACITY;
        } else if newOpacity > config.MAX_OPACITY {
            opacity = config.MAX_OPACITY;
        } else {
            opacity = newOpacity;
        }
    }

    // Toggle brush shape
    expose func toggleShape() {
        if shape == SHAPE_ROUND {
            shape = SHAPE_SQUARE;
        } else {
            shape = SHAPE_ROUND;
        }
    }

    // Check if point is within brush at center (cx, cy)
    expose func isInBrush(cx: Integer, cy: Integer, px: Integer, py: Integer) -> Integer {
        var dx = px - cx;
        var dy = py - cy;
        var half = size / 2;

        if shape == SHAPE_SQUARE {
            // Square brush
            if dx < 0 { dx = 0 - dx; }
            if dy < 0 { dy = 0 - dy; }
            if dx <= half and dy <= half {
                return 1;
            }
        } else {
            // Round brush - check distance
            if dx * dx + dy * dy <= half * half {
                return 1;
            }
        }
        return 0;
    }

    // Get the radius (half of size)
    expose func getRadius() -> Integer {
        return size / 2;
    }
}
