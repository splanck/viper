module Turtle;

import "./config";
import "./colors";

// =============================================================================
// Turtle - Swimming platform that can sink
// Adds danger to river crossing - sinking turtles submerge periodically
// =============================================================================

entity Turtle {
    expose Integer row;
    expose Integer col;
    expose Integer speed;
    expose Integer direction;  // 1 = right, -1 = left
    expose Integer width;
    expose Integer canSink;     // 1=can sink, 0=normal (Boolean workaround)
    expose Integer sinkTimer;
    expose Integer submerged;   // 1=submerged, 0=visible

    expose func init(Integer r, Integer c, Integer spd, Integer dir, Integer w, Integer sinking) {
        row = r;
        col = c;
        speed = spd;
        direction = dir;
        width = w;
        canSink = sinking;
        sinkTimer = 0;
        submerged = 0;
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

        // Handle sinking animation
        if (canSink == 1) {
            sinkTimer = sinkTimer + 1;
            // Cycle: 40 frames visible, 20 frames submerged
            Integer cycleLength = 60;
            Integer cycle = sinkTimer / cycleLength;
            Integer phase = sinkTimer - (cycle * cycleLength);
            if (phase >= 40) {
                submerged = 1;
            } else {
                submerged = 0;
            }
        }
    }

    expose func isOnTurtle(Integer frogRow, Integer frogCol) -> Boolean {
        if (frogRow != row) {
            return false;
        }

        // Can't stand on submerged turtles
        if (submerged == 1) {
            return false;
        }

        // Check if frog is on any part of the turtle group
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

    expose func isSubmerged() -> Boolean {
        return submerged == 1;
    }

    expose func isSinking() -> Boolean {
        return canSink == 1;
    }

    expose func getColor() -> Integer {
        return COLOR_TURTLE;
    }

    expose func getChar() -> String {
        if (submerged == 1) {
            return "~";
        }
        if (canSink == 1) {
            // Sinking turtles look different
            return "o";
        }
        return "O";
    }
}

// Factory functions
func createTurtle(Integer row, Integer col, Integer speed, Integer direction, Integer width) -> Turtle {
    Turtle t = new Turtle();
    t.init(row, col, speed, direction, width, 0);
    return t;
}

func createSinkingTurtle(Integer row, Integer col, Integer speed, Integer direction, Integer width) -> Turtle {
    Turtle t = new Turtle();
    t.init(row, col, speed, direction, width, 1);
    return t;
}
