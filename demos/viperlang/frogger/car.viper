module Car;

import "./config";
import "./colors";

// =============================================================================
// Car - Fast, narrow vehicle on the road
// =============================================================================

entity Car {
    expose Integer row;
    expose Integer col;
    expose Integer speed;
    expose Integer direction;  // 1 = right, -1 = left
    expose Integer width;
    expose Integer colorCode;

    expose func init(Integer r, Integer c, Integer spd, Integer dir) {
        row = r;
        col = c;
        speed = spd;
        direction = dir;
        width = 3;
        // Alternate colors for variety
        Integer colorPick = c / 10;
        Integer colorMod = colorPick - ((colorPick / 2) * 2);
        if (colorMod == 0) {
            colorCode = COLOR_CAR_RED;
        } else {
            colorCode = COLOR_CAR_YELLOW;
        }
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

        // Check if frog overlaps with car width
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
        return colorCode;
    }

    expose func getChar() -> String {
        if (direction > 0) {
            return ">";
        }
        return "<";
    }
}

// Factory functions
func createCar(Integer row, Integer col, Integer speed, Integer direction) -> Car {
    Car c = new Car();
    c.init(row, col, speed, direction);
    return c;
}

func createFastCar(Integer row, Integer col, Integer direction) -> Car {
    Car c = new Car();
    c.init(row, col, FAST_SPEED, direction);
    return c;
}
