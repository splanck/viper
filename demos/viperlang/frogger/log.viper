module Log;

import "./config";
import "./colors";

// =============================================================================
// Log - Floating platform in the river
// The frog can ride on logs to cross safely
// =============================================================================

entity Log {
    expose Integer row;
    expose Integer col;
    expose Integer speed;
    expose Integer direction;  // 1 = right, -1 = left
    expose Integer width;

    expose func init(Integer r, Integer c, Integer spd, Integer dir, Integer w) {
        row = r;
        col = c;
        speed = spd;
        direction = dir;
        width = w;
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

    expose func isOnLog(Integer frogRow, Integer frogCol) -> Boolean {
        if (frogRow != row) {
            return false;
        }

        // Check if frog is on any part of the log
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

    expose func getRideSpeed() -> Integer {
        return speed * direction;
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
        return COLOR_LOG;
    }

    expose func getChar() -> String {
        return "=";
    }
}

// Factory functions
func createLog(Integer row, Integer col, Integer speed, Integer direction, Integer width) -> Log {
    Log l = new Log();
    l.init(row, col, speed, direction, width);
    return l;
}

func createShortLog(Integer row, Integer col, Integer speed, Integer direction) -> Log {
    return createLog(row, col, speed, direction, 5);
}

func createMediumLog(Integer row, Integer col, Integer speed, Integer direction) -> Log {
    return createLog(row, col, speed, direction, 8);
}

func createLongLog(Integer row, Integer col, Integer speed, Integer direction) -> Log {
    return createLog(row, col, speed, direction, 12);
}
