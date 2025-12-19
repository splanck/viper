module Entities;

// Frog - The player character
entity Frog {
    Integer row;
    Integer col;
    Integer startRow;
    Integer startCol;
    Integer lives;
    Integer alive;
    Integer onPlatform;
    Integer platformSpeed;

    func init(r: Integer, c: Integer) {
        row = r;
        col = c;
        startRow = r;
        startCol = c;
        lives = 3;
        alive = 1;
        onPlatform = 0;
        platformSpeed = 0;
    }

    func moveUp() {
        if row > 1 {
            row = row - 1;
        }
    }

    func moveDown() {
        if row < 24 {
            row = row + 1;
        }
    }

    func moveLeft() {
        if col > 1 {
            col = col - 1;
        }
    }

    func moveRight() {
        if col < 70 {
            col = col + 1;
        }
    }

    func updateOnPlatform() {
        if onPlatform == 1 {
            Integer newCol = col + platformSpeed;
            if newCol >= 1 {
                if newCol <= 70 {
                    col = newCol;
                }
            }
        }
    }

    func setOnPlatform(speed: Integer) {
        onPlatform = 1;
        platformSpeed = speed;
    }

    func clearPlatform() {
        onPlatform = 0;
        platformSpeed = 0;
    }

    func die() {
        lives = lives - 1;
        if lives <= 0 {
            alive = 0;
        } else {
            row = startRow;
            col = startCol;
        }
        onPlatform = 0;
        platformSpeed = 0;
    }

    func reset() {
        row = startRow;
        col = startCol;
        onPlatform = 0;
        platformSpeed = 0;
    }

    func getRow() -> Integer { return row; }
    func getCol() -> Integer { return col; }
    func getLives() -> Integer { return lives; }
    func isAlive() -> Integer { return alive; }
}

// Vehicle - Cars and trucks on the road
entity Vehicle {
    Integer row;
    Integer col;
    Integer speed;
    Integer direction;
    Integer width;

    func init(r: Integer, c: Integer, spd: Integer, dir: Integer, w: Integer) {
        row = r;
        col = c;
        speed = spd;
        direction = dir;
        width = w;
    }

    func move() {
        Integer newCol = col + speed * direction;
        if newCol > 75 {
            newCol = 1 - width;
        }
        if newCol < 0 - width {
            newCol = 75;
        }
        col = newCol;
    }

    func checkCollision(frogRow: Integer, frogCol: Integer) -> Integer {
        if frogRow == row {
            Integer i = 0;
            while i < width {
                if frogCol == col + i {
                    return 1;
                }
                i = i + 1;
            }
        }
        return 0;
    }

    func getRow() -> Integer { return row; }
    func getCol() -> Integer { return col; }
    func getWidth() -> Integer { return width; }
    func getSpeed() -> Integer { return speed; }
    func getDirection() -> Integer { return direction; }
}

// Platform - Logs and turtles in the river
entity Platform {
    Integer row;
    Integer col;
    Integer speed;
    Integer direction;
    Integer width;
    Integer isTurtle;

    func init(r: Integer, c: Integer, spd: Integer, dir: Integer, w: Integer, turtle: Integer) {
        row = r;
        col = c;
        speed = spd;
        direction = dir;
        width = w;
        isTurtle = turtle;
    }

    func move() {
        Integer newCol = col + speed * direction;
        if newCol > 75 {
            newCol = 1 - width;
        }
        if newCol < 0 - width {
            newCol = 75;
        }
        col = newCol;
    }

    func checkOnPlatform(frogRow: Integer, frogCol: Integer) -> Integer {
        if frogRow == row {
            Integer i = 0;
            while i < width {
                if frogCol == col + i {
                    return 1;
                }
                i = i + 1;
            }
        }
        return 0;
    }

    func getRow() -> Integer { return row; }
    func getCol() -> Integer { return col; }
    func getWidth() -> Integer { return width; }
    func getSpeed() -> Integer { return speed; }
    func getDirection() -> Integer { return direction; }
    func getIsTurtle() -> Integer { return isTurtle; }
}

// Home - Goal slots at the top
entity Home {
    Integer col;
    Integer filled;

    func init(c: Integer) {
        col = c;
        filled = 0;
    }

    func fill() {
        filled = 1;
    }

    func reset() {
        filled = 0;
    }

    func getCol() -> Integer { return col; }
    func isFilled() -> Integer { return filled; }
}
