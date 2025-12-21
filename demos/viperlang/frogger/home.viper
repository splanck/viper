module Home;

import "./config";
import "./position";

// Home slot states
Integer HOME_EMPTY = 0;
Integer HOME_FILLED = 1;
Integer HOME_HAS_FLY = 2;

// Home entity - goal slots at the top of the screen
entity Home {
    expose Position pos;
    expose Integer state;
    expose Integer flyTimer;

    expose func init(Integer col) {
        pos = createPosition(HOME_ROW, col);
        state = HOME_EMPTY;
        flyTimer = 0;
    }

    expose func checkLanding(Integer frogCol) -> Boolean {
        // Home has width of 3 centered on col
        Integer leftEdge = pos.col - 1;
        Integer rightEdge = pos.col + 1;
        return frogCol >= leftEdge && frogCol <= rightEdge;
    }

    expose func fill() {
        state = HOME_FILLED;
    }

    expose func reset() {
        state = HOME_EMPTY;
        flyTimer = 0;
    }

    expose func spawnFly() {
        if (state == HOME_EMPTY) {
            state = HOME_HAS_FLY;
            flyTimer = 100;  // Fly visible for 100 frames
        }
    }

    expose func update() {
        if (state == HOME_HAS_FLY) {
            flyTimer = flyTimer - 1;
            if (flyTimer <= 0) {
                state = HOME_EMPTY;
            }
        }
    }

    expose func getCol() -> Integer {
        return pos.col;
    }

    expose func isFilled() -> Boolean {
        return state == HOME_FILLED;
    }

    expose func isEmpty() -> Boolean {
        return state == HOME_EMPTY;
    }

    expose func hasFly() -> Boolean {
        return state == HOME_HAS_FLY;
    }

    expose func getBonusPoints() -> Integer {
        if (state == HOME_HAS_FLY) {
            return SCORE_HOME + 200;  // Bonus for catching fly
        }
        return SCORE_HOME;
    }
}

// Factory function
func createHome(Integer col) -> Home {
    Home h = new Home();
    h.init(col);
    return h;
}
