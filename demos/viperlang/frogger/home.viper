// ============================================================================
// Frogger - Home Entity
// ============================================================================
// Goal slots at the top of the screen that the frog must reach.
// ============================================================================

module Home;

import "config";

entity Home {
    expose Integer col;
    expose Boolean filled;

    expose func init(c: Integer) {
        col = c;
        filled = false;
    }

    expose func getCol() -> Integer {
        return col;
    }

    expose func isFilled() -> Boolean {
        return filled;
    }

    expose func fill() {
        filled = true;
    }

    expose func reset() {
        filled = false;
    }

    expose func checkReached(frogCol: Integer) -> Boolean {
        return frogCol >= col - 1 && frogCol <= col + 1;
    }
}
