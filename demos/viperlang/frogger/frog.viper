// ============================================================================
// Frogger - Frog Entity
// ============================================================================
// The player-controlled frog character.
// ============================================================================

module Frog;

import "config";

entity Frog {
    expose Integer row;
    expose Integer col;
    expose Integer startRow;
    expose Integer startCol;
    expose Integer lives;
    expose Boolean alive;
    expose Boolean onPlatform;
    expose Integer platformSpeed;
    expose Integer furthestRow;

    expose func init(r: Integer, c: Integer) {
        row = r;
        col = c;
        startRow = r;
        startCol = c;
        lives = STARTING_LIVES;
        alive = true;
        onPlatform = false;
        platformSpeed = 0;
        furthestRow = r;
    }

    expose func getRow() -> Integer {
        return row;
    }

    expose func getCol() -> Integer {
        return col;
    }

    expose func getLives() -> Integer {
        return lives;
    }

    expose func isAlive() -> Boolean {
        return alive;
    }

    expose func isOnPlatform() -> Boolean {
        return onPlatform;
    }

    expose func moveUp() -> Integer {
        var bonus = 0;
        if row > 1 {
            row = row - 1;
            if row < furthestRow {
                furthestRow = row;
                bonus = SCORE_FORWARD;
            }
        }
        return bonus;
    }

    expose func moveDown() {
        if row < GAME_HEIGHT {
            row = row + 1;
        }
    }

    expose func moveLeft() {
        if col > 1 {
            col = col - 1;
        }
    }

    expose func moveRight() {
        if col < GAME_WIDTH {
            col = col + 1;
        }
    }

    expose func setOnPlatform(speed: Integer) {
        onPlatform = true;
        platformSpeed = speed;
    }

    expose func clearPlatform() {
        onPlatform = false;
        platformSpeed = 0;
    }

    expose func updateOnPlatform() {
        if onPlatform {
            var newCol = col + platformSpeed;
            if newCol >= 1 && newCol <= GAME_WIDTH {
                col = newCol;
            }
        }
    }

    expose func die() {
        lives = lives - 1;
        if lives <= 0 {
            alive = false;
        } else {
            row = startRow;
            col = startCol;
            furthestRow = startRow;
        }
        onPlatform = false;
        platformSpeed = 0;
    }

    expose func reset() {
        row = startRow;
        col = startCol;
        furthestRow = startRow;
        onPlatform = false;
        platformSpeed = 0;
    }

    expose func getSymbol() -> String {
        return "@";
    }
}
