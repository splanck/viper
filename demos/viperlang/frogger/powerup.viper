module PowerUp;

import "./config";
import "./position";

// Power-up entity - collectible bonuses
entity PowerUp {
    expose Position pos;
    expose Integer powerType;
    expose Boolean active;
    expose Integer lifetime;

    expose func init(Integer row, Integer col, Integer pType) {
        pos = createPosition(row, col);
        powerType = pType;
        active = true;
        lifetime = 200;  // Disappears after 200 frames
    }

    expose func update() {
        if (active) {
            lifetime = lifetime - 1;
            if (lifetime <= 0) {
                active = false;
            }
        }
    }

    expose func checkCollect(Integer frogRow, Integer frogCol) -> Boolean {
        if (!active) {
            return false;
        }

        return frogRow == pos.row && frogCol == pos.col;
    }

    expose func collect() {
        active = false;
    }

    expose func spawn(Integer row, Integer col, Integer pType) {
        pos.row = row;
        pos.col = col;
        powerType = pType;
        active = true;
        lifetime = 200;
    }

    expose func getRow() -> Integer {
        return pos.row;
    }

    expose func getCol() -> Integer {
        return pos.col;
    }

    expose func getType() -> Integer {
        return powerType;
    }

    expose func isActive() -> Boolean {
        return active;
    }

    expose func isSpeed() -> Boolean {
        return powerType == POWERUP_SPEED;
    }

    expose func isInvincible() -> Boolean {
        return powerType == POWERUP_INVINCIBLE;
    }

    expose func isExtraLife() -> Boolean {
        return powerType == POWERUP_EXTRA_LIFE;
    }

    expose func getSymbol() -> String {
        if (powerType == POWERUP_SPEED) {
            return "S";
        }
        if (powerType == POWERUP_INVINCIBLE) {
            return "I";
        }
        if (powerType == POWERUP_EXTRA_LIFE) {
            return "+";
        }
        return "?";
    }
}

// Factory function
func createPowerUp(Integer row, Integer col, Integer powerType) -> PowerUp {
    PowerUp p = new PowerUp();
    p.init(row, col, powerType);
    return p;
}

func createInactivePowerUp() -> PowerUp {
    PowerUp p = new PowerUp();
    p.init(0, 0, POWERUP_NONE);
    p.active = false;
    return p;
}
