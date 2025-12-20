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

    expose func init(r: Integer, c: Integer) {
        row = r;
        col = c;
        startRow = r;
        startCol = c;
        lives = 3;
        alive = 1;
        onPlatform = 0;
        platformSpeed = 0;
    }

    expose func moveUp() {
        if row > 1 {
            row = row - 1;
        }
    }

    expose func moveDown() {
        if row < 24 {
            row = row + 1;
        }
    }

    expose func moveLeft() {
        if col > 1 {
            col = col - 1;
        }
    }

    expose func moveRight() {
        if col < 70 {
            col = col + 1;
        }
    }

    expose func updateOnPlatform() {
        if onPlatform == 1 {
            Integer newCol = col + platformSpeed;
            if newCol >= 1 {
                if newCol <= 70 {
                    col = newCol;
                }
            }
        }
    }

    expose func setOnPlatform(speed: Integer) {
        onPlatform = 1;
        platformSpeed = speed;
    }

    expose func clearPlatform() {
        onPlatform = 0;
        platformSpeed = 0;
    }

    expose func die() {
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

    expose func reset() {
        row = startRow;
        col = startCol;
        onPlatform = 0;
        platformSpeed = 0;
    }

    expose func getRow() -> Integer { return row; }
    expose func getCol() -> Integer { return col; }
    expose func getLives() -> Integer { return lives; }
    expose func isAlive() -> Integer { return alive; }
}

// Vehicle - Cars and trucks on the road
entity Vehicle {
    Integer row;
    Integer col;
    Integer speed;
    Integer direction;
    Integer width;
    Integer isTruck;

    expose func init(r: Integer, c: Integer, spd: Integer, dir: Integer, w: Integer, truck: Integer) {
        row = r;
        col = c;
        speed = spd;
        direction = dir;
        width = w;
        isTruck = truck;
    }

    expose func move() {
        Integer newCol = col + speed * direction;
        if newCol > 75 {
            newCol = 1 - width;
        }
        if newCol < 0 - width {
            newCol = 75;
        }
        col = newCol;
    }

    expose func checkCollision(frogRow: Integer, frogCol: Integer) -> Integer {
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

    expose func getRow() -> Integer { return row; }
    expose func getCol() -> Integer { return col; }
    expose func getWidth() -> Integer { return width; }
    expose func getSpeed() -> Integer { return speed; }
    expose func getDirection() -> Integer { return direction; }
    expose func getIsTruck() -> Integer { return isTruck; }
}

// Platform - Logs and turtles in the river
entity Platform {
    Integer row;
    Integer col;
    Integer speed;
    Integer direction;
    Integer width;
    Integer isTurtle;

    expose func init(r: Integer, c: Integer, spd: Integer, dir: Integer, w: Integer, turtle: Integer) {
        row = r;
        col = c;
        speed = spd;
        direction = dir;
        width = w;
        isTurtle = turtle;
    }

    expose func move() {
        Integer newCol = col + speed * direction;
        if newCol > 75 {
            newCol = 1 - width;
        }
        if newCol < 0 - width {
            newCol = 75;
        }
        col = newCol;
    }

    expose func checkOnPlatform(frogRow: Integer, frogCol: Integer) -> Integer {
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

    expose func getRow() -> Integer { return row; }
    expose func getCol() -> Integer { return col; }
    expose func getWidth() -> Integer { return width; }
    expose func getSpeed() -> Integer { return speed; }
    expose func getDirection() -> Integer { return direction; }
    expose func getIsTurtle() -> Integer { return isTurtle; }
}

// Home - Goal slots at the top
entity Home {
    Integer col;
    Integer filled;

    expose func init(c: Integer) {
        col = c;
        filled = 0;
    }

    expose func fill() {
        filled = 1;
    }

    expose func reset() {
        filled = 0;
    }

    expose func getCol() -> Integer { return col; }
    expose func isFilled() -> Integer { return filled; }
}
