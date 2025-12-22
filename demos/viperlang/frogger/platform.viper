// ============================================================================
// Frogger - Platform Entity
// ============================================================================
// Logs and turtles in the river that the frog can ride.
// ============================================================================

module Platform;

import "config";

entity Platform {
    expose Integer row;
    expose Integer col;
    expose Integer speed;
    expose Integer direction;
    expose String symbol;
    expose Integer width;
    expose Boolean isTurtle;

    expose func init(r: Integer, c: Integer, spd: Integer, dir: Integer, sym: String, w: Integer) {
        row = r;
        col = c;
        speed = spd;
        direction = dir;
        symbol = sym;
        width = w;
        isTurtle = (sym == "O");
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

    expose func getSymbol() -> String {
        return symbol;
    }

    expose func getSpeed() -> Integer {
        return speed;
    }

    expose func getDirection() -> Integer {
        return direction;
    }

    expose func getMovementSpeed() -> Integer {
        return speed * direction;
    }

    expose func getIsTurtle() -> Boolean {
        return isTurtle;
    }

    expose func move() {
        var newCol = col + (speed * direction);

        if newCol > GAME_WIDTH + 5 {
            newCol = 1 - width;
        }
        if newCol < (0 - width) {
            newCol = GAME_WIDTH + 5;
        }

        col = newCol;
    }

    expose func checkOnPlatform(frogRow: Integer, frogCol: Integer) -> Boolean {
        if frogRow != row {
            return false;
        }

        for i in 0..width {
            if frogCol == col + i {
                return true;
            }
        }

        return false;
    }
}
