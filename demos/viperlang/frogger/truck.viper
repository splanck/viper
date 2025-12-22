module Truck;

import "./config";
import "./colors";

// =============================================================================
// Truck - Slow, wide vehicle on the road
// =============================================================================

entity Truck {
    expose Integer row;
    expose Integer col;
    expose Integer speed;
    expose Integer direction;  // 1 = right, -1 = left
    expose Integer width;

    expose func init(Integer r, Integer c, Integer spd, Integer dir) {
        row = r;
        col = c;
        speed = spd;
        direction = dir;
        width = 6;
    }

    expose func move() {
        Integer delta = speed * direction;
        col = col + delta;

        // Wrap around screen edges
        if (direction > 0 && col > GAME_WIDTH + width) {
            col = 1 - width;
        } else if (direction < 0 && col < 1 - width) {
            col = GAME_WIDTH + width;
        }
    }

    expose func checkCollision(Integer frogRow, Integer frogCol) -> Boolean {
        if (frogRow != row) {
            return false;
        }

        // Check if frog overlaps with truck width
        var i = 0;
        while (i < width) {
            Integer checkCol = col + i;
            if (frogCol == checkCol) {
                return true;
            }
            i = i + 1;
        }
        return false;
    }

    expose func getRow() -> Integer {
        return row;
    }

    expose func getCol() -> Integer {
        return col;
    }

    expose func getWidth() -> Integer {
        return width;
    }

    expose func getColor() -> Integer {
        return COLOR_TRUCK;
    }

    expose func getChar() -> String {
        return "=";
    }
}

// Factory function
func createTruck(Integer row, Integer col, Integer speed, Integer direction) -> Truck {
    Truck t = new Truck();
    t.init(row, col, speed, direction);
    return t;
}
