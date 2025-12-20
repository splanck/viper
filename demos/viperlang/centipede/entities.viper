module Entities;

// Segment - Centipede body segment
entity Segment {
    Integer posX;
    Integer posY;
    Integer dirX;     // -1 = left, 1 = right
    Integer active;   // 1 = alive, 0 = dead
    Integer isHead;   // 1 = head segment

    expose func init(x: Integer, y: Integer, dir: Integer, head: Integer) {
        posX = x;
        posY = y;
        dirX = dir;
        active = 1;
        isHead = head;
    }

    expose func getX() -> Integer { return posX; }
    expose func getY() -> Integer { return posY; }
    expose func getDir() -> Integer { return dirX; }
    expose func isActive() -> Integer { return active; }
    expose func isHeadSeg() -> Integer { return isHead; }

    expose func setX(x: Integer) { posX = x; }
    expose func setY(y: Integer) { posY = y; }
    expose func setDir(d: Integer) { dirX = d; }
    expose func setActive(a: Integer) { active = a; }
    expose func setHead(h: Integer) { isHead = h; }

    expose func reverseDir() {
        dirX = 0 - dirX;
    }

    expose func moveDown() {
        posY = posY + 1;
    }
}

// Spider - Erratic enemy
entity Spider {
    Integer posX;
    Integer posY;
    Integer dirX;
    Integer dirY;
    Integer active;
    Integer moveTimer;

    expose func init() {
        posX = 0;
        posY = 0;
        dirX = 1;
        dirY = 1;
        active = 0;
        moveTimer = 0;
    }

    expose func spawn(startX: Integer, startY: Integer, startDirX: Integer, startDirY: Integer) {
        posX = startX;
        posY = startY;
        dirX = startDirX;
        dirY = startDirY;
        active = 1;
        moveTimer = 0;
    }

    expose func getX() -> Integer { return posX; }
    expose func getY() -> Integer { return posY; }
    expose func isActive() -> Integer { return active; }

    expose func setX(x: Integer) { posX = x; }
    expose func setY(y: Integer) { posY = y; }

    expose func kill() {
        active = 0;
    }

    expose func getTimer() -> Integer { return moveTimer; }
    expose func incTimer() { moveTimer = moveTimer + 1; }
    expose func resetTimer() { moveTimer = 0; }

    expose func getDirX() -> Integer { return dirX; }
    expose func getDirY() -> Integer { return dirY; }
    expose func setDirY(d: Integer) { dirY = d; }
}

// Player - Ship and bullet
entity Player {
    Integer posX;
    Integer posY;
    Integer lives;
    Integer score;
    Integer bulletX;
    Integer bulletY;
    Integer bulletActive;

    expose func init(startX: Integer, startY: Integer) {
        posX = startX;
        posY = startY;
        lives = 3;
        score = 0;
        bulletActive = 0;
        bulletX = 0;
        bulletY = 0;
    }

    expose func getX() -> Integer { return posX; }
    expose func getY() -> Integer { return posY; }
    expose func getLives() -> Integer { return lives; }
    expose func getScore() -> Integer { return score; }
    expose func getBulletX() -> Integer { return bulletX; }
    expose func getBulletY() -> Integer { return bulletY; }
    expose func hasBullet() -> Integer { return bulletActive; }

    expose func setX(x: Integer) { posX = x; }
    expose func setY(y: Integer) { posY = y; }

    expose func moveLeft() { posX = posX - 1; }
    expose func moveRight() { posX = posX + 1; }
    expose func moveUp() { posY = posY - 1; }
    expose func moveDown() { posY = posY + 1; }

    expose func addScore(pts: Integer) {
        score = score + pts;
    }

    expose func loseLife() -> Integer {
        lives = lives - 1;
        if lives <= 0 {
            return 1;
        }
        return 0;
    }

    expose func fire() {
        if bulletActive == 0 {
            bulletX = posX;
            bulletY = posY - 1;
            bulletActive = 1;
        }
    }

    expose func updateBullet() {
        if bulletActive == 1 {
            bulletY = bulletY - 1;
            if bulletY < 0 {
                bulletActive = 0;
            }
        }
    }

    expose func deactivateBullet() {
        bulletActive = 0;
    }

    expose func reset(startX: Integer, startY: Integer) {
        posX = startX;
        posY = startY;
        bulletActive = 0;
    }
}

