module Game;

import "./piece";
import "./board";

entity Game {
    expose Board board;
    expose Piece currentPiece;
    expose Piece nextPiece;
    expose Integer score;
    expose Integer level;
    expose Integer linesCleared;
    expose Boolean gameOver;
    expose Integer dropTimer;
    expose Integer dropSpeed;
    
    // Random number generator state
    Integer randState;
    
    expose func init() {
        board = createBoard();
        score = 0;
        level = 1;
        linesCleared = 0;
        gameOver = false;
        dropTimer = 0;
        dropSpeed = 30;  // Frames between drops
        randState = 12345;  // Seed
        
        // Create first pieces
        nextPiece = self.spawnRandomPiece();
        self.spawnNewPiece();
    }
    
    func nextRandom() -> Integer {
        // Simple LCG random number generator
        randState = (randState * 1103515245 + 12345) % 2147483648;
        Integer result = randState / 65536;
        if (result < 0) {
            result = 0 - result;
        }
        return result;
    }
    
    func spawnRandomPiece() -> Piece {
        Integer pieceType = self.nextRandom() % 7;
        return createPiece(pieceType);
    }
    
    expose func spawnNewPiece() {
        currentPiece = nextPiece;
        nextPiece = self.spawnRandomPiece();
        
        // Check if new piece can be placed
        if (!board.canPlacePiece(currentPiece)) {
            gameOver = true;
        }
    }
    
    expose func moveLeft() {
        if (gameOver) { return; }
        currentPiece.moveLeft();
        if (!board.canPlacePiece(currentPiece)) {
            currentPiece.moveRight();  // Undo
        }
    }
    
    expose func moveRight() {
        if (gameOver) { return; }
        currentPiece.moveRight();
        if (!board.canPlacePiece(currentPiece)) {
            currentPiece.moveLeft();  // Undo
        }
    }
    
    expose func rotate() {
        if (gameOver) { return; }
        currentPiece.rotateRight();
        if (!board.canPlacePiece(currentPiece)) {
            currentPiece.rotateLeft();  // Undo
        }
    }
    
    expose func softDrop() -> Boolean {
        if (gameOver) { return false; }
        currentPiece.moveDown();
        if (!board.canPlacePiece(currentPiece)) {
            currentPiece.moveUp();  // Undo
            return false;  // Can't move down
        }
        return true;
    }
    
    expose func hardDrop() {
        if (gameOver) { return; }
        while (self.softDrop()) {
            score = score + 2;  // Bonus points for hard drop
        }
        self.lockPiece();
    }
    
    expose func lockPiece() {
        board.placePiece(currentPiece);
        
        // Clear lines and update score
        Integer lines = board.clearFullRows();
        if (lines > 0) {
            linesCleared = linesCleared + lines;
            
            // Score based on lines cleared
            if (lines == 1) {
                score = score + 100 * level;
            } else if (lines == 2) {
                score = score + 300 * level;
            } else if (lines == 3) {
                score = score + 500 * level;
            } else if (lines == 4) {
                score = score + 800 * level;  // Tetris!
            }
            
            // Level up every 10 lines
            Integer newLevel = (linesCleared / 10) + 1;
            if (newLevel > level) {
                level = newLevel;
                // Speed up
                dropSpeed = dropSpeed - 2;
                if (dropSpeed < 5) {
                    dropSpeed = 5;
                }
            }
        }
        
        self.spawnNewPiece();
    }

    expose func update() {
        if (gameOver) { return; }

        dropTimer = dropTimer + 1;
        if (dropTimer >= dropSpeed) {
            dropTimer = 0;
            if (!self.softDrop()) {
                self.lockPiece();
            }
        }
    }
}

func createGame() -> Game {
    Game g = new Game();
    g.init();
    return g;
}
