module Home;

import "./config";
import "./colors";

// =============================================================================
// HomeSlot - Goal destination at the top of the screen
// The frog must fill all 5 homes to win the level
// =============================================================================

// Home states
final HOME_EMPTY = 0;
final HOME_FILLED = 1;
final HOME_HAS_FLY = 2;

entity HomeSlot {
    expose Integer col;
    expose Integer state;
    expose Integer flyTimer;

    expose func init(Integer c) {
        col = c;
        state = HOME_EMPTY;
        flyTimer = 0;
    }

    expose func update() {
        // Update fly timer
        if (state == HOME_HAS_FLY) {
            flyTimer = flyTimer - 1;
            if (flyTimer <= 0) {
                state = HOME_EMPTY;
            }
        }
    }

    expose func spawnFly() {
        if (state == HOME_EMPTY) {
            state = HOME_HAS_FLY;
            flyTimer = FLY_DURATION;
        }
    }

    expose func checkLanding(Integer frogCol) -> Boolean {
        // Home slot has width of 5 centered on col
        Integer leftEdge = col - 2;
        Integer rightEdge = col + 2;
        return frogCol >= leftEdge && frogCol <= rightEdge;
    }

    expose func fill() {
        state = HOME_FILLED;
    }

    expose func reset() {
        state = HOME_EMPTY;
        flyTimer = 0;
    }

    // State queries
    expose func isEmpty() -> Boolean {
        return state == HOME_EMPTY;
    }

    expose func isFilled() -> Boolean {
        return state == HOME_FILLED;
    }

    expose func hasFly() -> Boolean {
        return state == HOME_HAS_FLY;
    }

    // Getters
    expose func getCol() -> Integer {
        return col;
    }

    expose func getBonusPoints() -> Integer {
        if (state == HOME_HAS_FLY) {
            return SCORE_HOME + SCORE_FLY_BONUS;
        }
        return SCORE_HOME;
    }

    expose func getColor() -> Integer {
        if (state == HOME_FILLED) {
            return COLOR_HOME_FILLED;
        }
        if (state == HOME_HAS_FLY) {
            return COLOR_FLY;
        }
        return COLOR_HOME_EMPTY;
    }

    expose func getChar() -> String {
        if (state == HOME_FILLED) {
            return "F";
        }
        if (state == HOME_HAS_FLY) {
            return "*";
        }
        return " ";
    }
}

// Factory function
func createHomeSlot(Integer col) -> HomeSlot {
    HomeSlot h = new HomeSlot();
    h.init(col);
    return h;
}
