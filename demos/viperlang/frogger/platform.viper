module Platform;

import "./config";
import "./position";

// Platform types
Integer TYPE_LOG = 1;
Integer TYPE_TURTLE = 2;
Integer TYPE_SINKING_TURTLE = 3;

// Platform entity - logs and turtles in the river
entity Platform {
    expose Position pos;
    expose Integer speed;
    expose Integer direction;
    expose Integer width;
    expose Integer platformType;
    expose Integer sinkTimer;      // For sinking turtles
    expose Boolean submerged;

    expose func init(Integer row, Integer col, Integer spd, Integer dir, Integer w, Integer pType) {
        pos = createPosition(row, col);
        speed = spd;
        direction = dir;
        width = w;
        platformType = pType;
        sinkTimer = 0;
        submerged = false;
    }

    expose func move() {
        Integer delta = speed * direction;
        Integer newCol = pos.col + delta;

        // Wrap around screen edges
        if (direction > 0) {
            if (newCol > GAME_WIDTH + width) {
                newCol = 0 - width;
            }
        } else {
            if (newCol < (0 - width)) {
                newCol = GAME_WIDTH + width;
            }
        }

        pos.col = newCol;

        // Handle sinking turtles
        if (platformType == TYPE_SINKING_TURTLE) {
            sinkTimer = sinkTimer + 1;
            // Submerge every 40 frames for 15 frames
            Integer cycle = sinkTimer / 55;
            Integer phase = sinkTimer - (cycle * 55);
            submerged = phase >= 40;
        }
    }

    expose func checkOnPlatform(Integer frogRow, Integer frogCol) -> Boolean {
        if (frogRow != pos.row) {
            return false;
        }

        // Sinking turtles don't count when submerged
        if (submerged) {
            return false;
        }

        var i = 0;
        while (i < width) {
            Integer checkCol = pos.col + i;
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
        return pos.row;
    }

    expose func getCol() -> Integer {
        return pos.col;
    }

    expose func getWidth() -> Integer {
        return width;
    }

    expose func isLog() -> Boolean {
        return platformType == TYPE_LOG;
    }

    expose func isTurtle() -> Boolean {
        return platformType == TYPE_TURTLE || platformType == TYPE_SINKING_TURTLE;
    }

    expose func isSinking() -> Boolean {
        return platformType == TYPE_SINKING_TURTLE;
    }

    expose func isSubmerged() -> Boolean {
        return submerged;
    }
}

// Factory functions
func createLog(Integer row, Integer col, Integer speed, Integer direction, Integer width) -> Platform {
    Platform p = new Platform();
    p.init(row, col, speed, direction, width, TYPE_LOG);
    return p;
}

func createTurtle(Integer row, Integer col, Integer speed, Integer direction, Integer width) -> Platform {
    Platform p = new Platform();
    p.init(row, col, speed, direction, width, TYPE_TURTLE);
    return p;
}

func createSinkingTurtle(Integer row, Integer col, Integer speed, Integer direction, Integer width) -> Platform {
    Platform p = new Platform();
    p.init(row, col, speed, direction, width, TYPE_SINKING_TURTLE);
    return p;
}
