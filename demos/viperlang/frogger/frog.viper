module Frog;

import "./config";
import "./position";

// Frog states
Integer STATE_NORMAL = 0;
Integer STATE_ON_PLATFORM = 1;
Integer STATE_DYING = 2;
Integer STATE_INVINCIBLE = 3;

// Player frog entity
entity Frog {
    expose Position pos;
    expose Position startPos;
    expose Integer lives;
    expose Integer state;
    expose Integer platformSpeed;
    expose Integer invincibleTimer;
    expose Integer speedBoost;
    expose Integer highestRow;

    expose func init(Integer row, Integer col) {
        pos = createPosition(row, col);
        startPos = createPosition(row, col);
        lives = MAX_LIVES;
        state = STATE_NORMAL;
        platformSpeed = 0;
        invincibleTimer = 0;
        speedBoost = 0;
        highestRow = row;
    }

    // Movement with bounds checking
    expose func moveUp() {
        Integer newRow = pos.row - 1;
        if (newRow >= 1) {
            pos.row = newRow;
            // Track progress for scoring
            if (newRow < highestRow) {
                highestRow = newRow;
            }
        }
    }

    expose func moveDown() {
        Integer newRow = pos.row + 1;
        if (newRow <= START_ROW) {
            pos.row = newRow;
        }
    }

    expose func moveLeft() {
        Integer moveAmount = 1;
        if (speedBoost > 0) {
            moveAmount = 2;
        }
        Integer newCol = pos.col - moveAmount;
        if (newCol >= 1) {
            pos.col = newCol;
        }
    }

    expose func moveRight() {
        Integer moveAmount = 1;
        if (speedBoost > 0) {
            moveAmount = 2;
        }
        Integer newCol = pos.col + moveAmount;
        if (newCol <= GAME_WIDTH) {
            pos.col = newCol;
        }
    }

    // Platform riding
    expose func setOnPlatform(Integer speed) {
        state = STATE_ON_PLATFORM;
        platformSpeed = speed;
    }

    expose func clearPlatform() {
        if (state == STATE_ON_PLATFORM) {
            state = STATE_NORMAL;
        }
        platformSpeed = 0;
    }

    expose func updatePlatformRide() {
        if (state == STATE_ON_PLATFORM) {
            Integer newCol = pos.col + platformSpeed;
            if (newCol >= 1 && newCol <= GAME_WIDTH) {
                pos.col = newCol;
            }
        }
    }

    // Power-up effects
    expose func activateSpeedBoost(Integer duration) {
        speedBoost = duration;
    }

    expose func activateInvincibility(Integer frames) {
        state = STATE_INVINCIBLE;
        invincibleTimer = frames;
    }

    expose func addLife() {
        lives = lives + 1;
    }

    // Update timers each frame
    expose func updateTimers() {
        if (speedBoost > 0) {
            speedBoost = speedBoost - 1;
        }

        if (invincibleTimer > 0) {
            invincibleTimer = invincibleTimer - 1;
            if (invincibleTimer <= 0) {
                state = STATE_NORMAL;
            }
        }
    }

    // Death handling
    expose func die() {
        if (state == STATE_INVINCIBLE) {
            return;
        }

        lives = lives - 1;
        if (lives <= 0) {
            state = STATE_DYING;
        } else {
            self.respawn();
        }
    }

    expose func respawn() {
        pos.row = startPos.row;
        pos.col = startPos.col;
        state = STATE_NORMAL;
        platformSpeed = 0;
        speedBoost = 0;
        invincibleTimer = 0;
    }

    expose func reset() {
        self.respawn();
        highestRow = startPos.row;
    }

    // State queries
    expose func isAlive() -> Boolean {
        return lives > 0;
    }

    expose func isInvincible() -> Boolean {
        return state == STATE_INVINCIBLE;
    }

    expose func isOnPlatform() -> Boolean {
        return state == STATE_ON_PLATFORM;
    }

    expose func hasSpeedBoost() -> Boolean {
        return speedBoost > 0;
    }

    expose func getLives() -> Integer {
        return lives;
    }

    expose func getRow() -> Integer {
        return pos.row;
    }

    expose func getCol() -> Integer {
        return pos.col;
    }

    expose func getProgressScore() -> Integer {
        // Score based on how far the frog has traveled
        Integer progress = startPos.row - highestRow;
        return progress * SCORE_FORWARD;
    }
}

func createFrog() -> Frog {
    Frog f = new Frog();
    f.init(START_ROW, 35);
    return f;
}
