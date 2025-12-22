module Frog;

import "./config";
import "./vec2";

// =============================================================================
// Frog - The player character
// Features: movement, lives, platform riding, invincibility, scoring
// =============================================================================

// Frog state constants
final STATE_NORMAL = 0;
final STATE_ON_PLATFORM = 1;
final STATE_INVINCIBLE = 2;
final STATE_DEAD = 3;

entity Frog {
    // Position
    expose Integer row;
    expose Integer col;
    expose Integer startRow;
    expose Integer startCol;

    // State
    expose Integer lives;
    expose Integer state;
    expose Integer invincibleTimer;
    expose Integer highestRow;

    // Platform riding
    expose Integer platformSpeed;
    expose Integer ridingPlatform;  // 1=riding, 0=not (Boolean alignment workaround)

    // Animation
    expose Integer animFrame;
    expose Integer deathTimer;

    expose func init(Integer r, Integer c) {
        row = r;
        col = c;
        startRow = r;
        startCol = c;
        lives = MAX_LIVES;
        state = STATE_NORMAL;
        invincibleTimer = 0;
        highestRow = r;
        platformSpeed = 0;
        ridingPlatform = 0;
        animFrame = 0;
        deathTimer = 0;
    }

    // Movement methods
    expose func moveUp() {
        if (state == STATE_DEAD) { return; }

        Integer newRow = row - 1;
        if (newRow >= HOME_ROW) {
            row = newRow;
            // Track progress for scoring
            if (newRow < highestRow) {
                highestRow = newRow;
            }
        }
    }

    expose func moveDown() {
        if (state == STATE_DEAD) { return; }

        Integer newRow = row + 1;
        if (newRow <= START_ROW) {
            row = newRow;
        }
    }

    expose func moveLeft() {
        if (state == STATE_DEAD) { return; }

        Integer newCol = col - FROG_SPEED;
        if (newCol >= 2) {
            col = newCol;
        }
    }

    expose func moveRight() {
        if (state == STATE_DEAD) { return; }

        Integer newCol = col + FROG_SPEED;
        if (newCol <= GAME_WIDTH) {
            col = newCol;
        }
    }

    // Platform interaction
    expose func setOnPlatform(Integer speed) {
        ridingPlatform = 1;
        platformSpeed = speed;
        if (state == STATE_NORMAL) {
            state = STATE_ON_PLATFORM;
        }
    }

    expose func clearPlatform() {
        ridingPlatform = 0;
        platformSpeed = 0;
        if (state == STATE_ON_PLATFORM) {
            state = STATE_NORMAL;
        }
    }

    expose func updatePlatformRide() {
        if (ridingPlatform == 1) {
            Integer newCol = col + platformSpeed;
            if (newCol >= 2 && newCol <= GAME_WIDTH) {
                col = newCol;
            } else {
                // Fell off the edge
                self.die();
            }
        }
    }

    // Invincibility
    expose func activateInvincibility() {
        state = STATE_INVINCIBLE;
        invincibleTimer = INVINCIBLE_DURATION;
    }

    // Timer updates
    expose func update() {
        animFrame = animFrame + 1;

        // Update invincibility
        if (invincibleTimer > 0) {
            invincibleTimer = invincibleTimer - 1;
            if (invincibleTimer <= 0 && state == STATE_INVINCIBLE) {
                state = STATE_NORMAL;
            }
        }

        // Update death timer
        if (state == STATE_DEAD && deathTimer > 0) {
            deathTimer = deathTimer - 1;
        }
    }

    // Death and respawn
    expose func die() {
        if (state == STATE_INVINCIBLE) {
            return;
        }

        lives = lives - 1;
        state = STATE_DEAD;
        deathTimer = 15;
    }

    expose func respawn() {
        row = startRow;
        col = startCol;
        state = STATE_NORMAL;
        platformSpeed = 0;
        ridingPlatform = 0;
        invincibleTimer = 0;
        animFrame = 0;
        deathTimer = 0;
    }

    expose func resetForNewLevel() {
        self.respawn();
        highestRow = startRow;
    }

    // State queries
    expose func isAlive() -> Boolean {
        return lives > 0;
    }

    expose func isDead() -> Boolean {
        return state == STATE_DEAD;
    }

    expose func canRespawn() -> Boolean {
        return state == STATE_DEAD && deathTimer <= 0 && lives > 0;
    }

    expose func isInvincible() -> Boolean {
        return state == STATE_INVINCIBLE;
    }

    expose func isVisible() -> Boolean {
        // Flicker when invincible
        if (state == STATE_INVINCIBLE) {
            Integer flicker = animFrame / 3;
            Integer odd = flicker - ((flicker / 2) * 2);
            return odd == 0;
        }
        return state != STATE_DEAD || deathTimer > 0;
    }

    // Getters
    expose func getRow() -> Integer {
        return row;
    }

    expose func getCol() -> Integer {
        return col;
    }

    expose func getLives() -> Integer {
        return lives;
    }

    expose func getProgressScore() -> Integer {
        Integer progress = startRow - highestRow;
        if (progress < 0) { progress = 0; }
        return progress * SCORE_FORWARD;
    }

    // Visual representation
    expose func getChar() -> String {
        if (state == STATE_DEAD) {
            return "X";
        }
        if (state == STATE_INVINCIBLE) {
            return "*";
        }
        return "@";
    }
}

// Factory function
func createFrog() -> Frog {
    Frog f = new Frog();
    f.init(START_ROW, 40);
    return f;
}
