module Piece;

// Tetromino piece types: I, O, T, S, Z, J, L
// Each piece has 4 rotation states

entity Piece {
    expose Integer pieceType;  // 0-6 for I, O, T, S, Z, J, L
    expose Integer rotation;   // 0-3 for rotation states
    expose Integer x;          // Position on board
    expose Integer y;
    
    // Get block positions for current rotation
    // Returns offsets as packed integers: offset = row * 10 + col
    // Each piece has 4 blocks
    expose func getBlock(Integer blockIndex) -> Integer {
        // I piece
        if (pieceType == 0) {
            if (rotation == 0 || rotation == 2) {
                // Horizontal: ####
                if (blockIndex == 0) { return 0; }
                if (blockIndex == 1) { return 1; }
                if (blockIndex == 2) { return 2; }
                return 3;
            } else {
                // Vertical
                if (blockIndex == 0) { return 0; }
                if (blockIndex == 1) { return 10; }
                if (blockIndex == 2) { return 20; }
                return 30;
            }
        }
        
        // O piece (square - no rotation effect)
        if (pieceType == 1) {
            if (blockIndex == 0) { return 0; }
            if (blockIndex == 1) { return 1; }
            if (blockIndex == 2) { return 10; }
            return 11;
        }
        
        // T piece
        if (pieceType == 2) {
            if (rotation == 0) {
                // .#.
                // ###
                if (blockIndex == 0) { return 1; }
                if (blockIndex == 1) { return 10; }
                if (blockIndex == 2) { return 11; }
                return 12;
            }
            if (rotation == 1) {
                // #.
                // ##
                // #.
                if (blockIndex == 0) { return 0; }
                if (blockIndex == 1) { return 10; }
                if (blockIndex == 2) { return 11; }
                return 20;
            }
            if (rotation == 2) {
                // ###
                // .#.
                if (blockIndex == 0) { return 0; }
                if (blockIndex == 1) { return 1; }
                if (blockIndex == 2) { return 2; }
                return 11;
            }
            // rotation == 3
            // .#
            // ##
            // .#
            if (blockIndex == 0) { return 1; }
            if (blockIndex == 1) { return 10; }
            if (blockIndex == 2) { return 11; }
            return 21;
        }
        
        // S piece
        if (pieceType == 3) {
            if (rotation == 0 || rotation == 2) {
                // .##
                // ##.
                if (blockIndex == 0) { return 1; }
                if (blockIndex == 1) { return 2; }
                if (blockIndex == 2) { return 10; }
                return 11;
            } else {
                // #.
                // ##
                // .#
                if (blockIndex == 0) { return 0; }
                if (blockIndex == 1) { return 10; }
                if (blockIndex == 2) { return 11; }
                return 21;
            }
        }
        
        // Z piece
        if (pieceType == 4) {
            if (rotation == 0 || rotation == 2) {
                // ##.
                // .##
                if (blockIndex == 0) { return 0; }
                if (blockIndex == 1) { return 1; }
                if (blockIndex == 2) { return 11; }
                return 12;
            } else {
                // .#
                // ##
                // #.
                if (blockIndex == 0) { return 1; }
                if (blockIndex == 1) { return 10; }
                if (blockIndex == 2) { return 11; }
                return 20;
            }
        }
        
        // J piece
        if (pieceType == 5) {
            if (rotation == 0) {
                // #..
                // ###
                if (blockIndex == 0) { return 0; }
                if (blockIndex == 1) { return 10; }
                if (blockIndex == 2) { return 11; }
                return 12;
            }
            if (rotation == 1) {
                // ##
                // #.
                // #.
                if (blockIndex == 0) { return 0; }
                if (blockIndex == 1) { return 1; }
                if (blockIndex == 2) { return 10; }
                return 20;
            }
            if (rotation == 2) {
                // ###
                // ..#
                if (blockIndex == 0) { return 0; }
                if (blockIndex == 1) { return 1; }
                if (blockIndex == 2) { return 2; }
                return 12;
            }
            // rotation == 3
            // .#
            // .#
            // ##
            if (blockIndex == 0) { return 1; }
            if (blockIndex == 1) { return 11; }
            if (blockIndex == 2) { return 20; }
            return 21;
        }
        
        // L piece (pieceType == 6)
        if (rotation == 0) {
            // ..#
            // ###
            if (blockIndex == 0) { return 2; }
            if (blockIndex == 1) { return 10; }
            if (blockIndex == 2) { return 11; }
            return 12;
        }
        if (rotation == 1) {
            // #.
            // #.
            // ##
            if (blockIndex == 0) { return 0; }
            if (blockIndex == 1) { return 10; }
            if (blockIndex == 2) { return 20; }
            return 21;
        }
        if (rotation == 2) {
            // ###
            // #..
            if (blockIndex == 0) { return 0; }
            if (blockIndex == 1) { return 1; }
            if (blockIndex == 2) { return 2; }
            return 10;
        }
        // rotation == 3
        // ##
        // .#
        // .#
        if (blockIndex == 0) { return 0; }
        if (blockIndex == 1) { return 1; }
        if (blockIndex == 2) { return 11; }
        return 21;
    }
    
    expose func getBlockRow(Integer blockIndex) -> Integer {
        Integer offset = self.getBlock(blockIndex);
        return offset / 10;
    }
    
    expose func getBlockCol(Integer blockIndex) -> Integer {
        Integer offset = self.getBlock(blockIndex);
        return offset % 10;
    }
    
    expose func rotateRight() {
        rotation = (rotation + 1) % 4;
    }
    
    expose func rotateLeft() {
        rotation = (rotation + 3) % 4;  // Same as -1 mod 4
    }
    
    expose func moveLeft() {
        x = x - 1;
    }
    
    expose func moveRight() {
        x = x + 1;
    }
    
    expose func moveDown() {
        y = y + 1;
    }
    
    expose func moveUp() {
        y = y - 1;
    }
    
    expose func getColor() -> Integer {
        // Return color code based on piece type
        // 1=Cyan(I), 2=Yellow(O), 3=Purple(T), 4=Green(S), 5=Red(Z), 6=Blue(J), 7=Orange(L)
        return pieceType + 1;
    }
}

// Factory function to create a new piece
func createPiece(Integer pieceType) -> Piece {
    Piece p = new Piece();
    p.pieceType = pieceType;
    p.rotation = 0;
    p.x = 3;  // Start near center of 10-wide board
    p.y = 0;
    return p;
}
