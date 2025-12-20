module Entities;

// Piece - Tetromino piece
// Shape is represented as 16 cells (4x4 grid, row-major order)
entity Piece {
    Integer pieceType;   // 0=I, 1=O, 2=T, 3=S, 4=Z, 5=J, 6=L
    Integer pieceColor;
    Integer posX;
    Integer posY;
    Integer rotation;
    // 4x4 shape stored as 16 integers (s00, s01, s02, s03, s10, s11, etc.)
    Integer s00; Integer s01; Integer s02; Integer s03;
    Integer s10; Integer s11; Integer s12; Integer s13;
    Integer s20; Integer s21; Integer s22; Integer s23;
    Integer s30; Integer s31; Integer s32; Integer s33;

    expose func init(pType: Integer) {
        pieceType = pType;
        rotation = 0;
        posX = 3;
        posY = 0;

        // Clear shape
        s00 = 0; s01 = 0; s02 = 0; s03 = 0;
        s10 = 0; s11 = 0; s12 = 0; s13 = 0;
        s20 = 0; s21 = 0; s22 = 0; s23 = 0;
        s30 = 0; s31 = 0; s32 = 0; s33 = 0;

        // Set color based on type
        if pType == 0 { pieceColor = 6; }  // I = Cyan
        if pType == 1 { pieceColor = 3; }  // O = Yellow
        if pType == 2 { pieceColor = 5; }  // T = Magenta
        if pType == 3 { pieceColor = 2; }  // S = Green
        if pType == 4 { pieceColor = 1; }  // Z = Red
        if pType == 5 { pieceColor = 4; }  // J = Blue
        if pType == 6 { pieceColor = 7; }  // L = White

        // Initialize shape based on type
        // I-piece
        if pieceType == 0 {
            s01 = 1; s11 = 1; s21 = 1; s31 = 1;
        }
        // O-piece
        if pieceType == 1 {
            s01 = 1; s02 = 1; s11 = 1; s12 = 1;
        }
        // T-piece
        if pieceType == 2 {
            s01 = 1; s10 = 1; s11 = 1; s12 = 1;
        }
        // S-piece
        if pieceType == 3 {
            s01 = 1; s02 = 1; s10 = 1; s11 = 1;
        }
        // Z-piece
        if pieceType == 4 {
            s00 = 1; s01 = 1; s11 = 1; s12 = 1;
        }
        // J-piece
        if pieceType == 5 {
            s01 = 1; s11 = 1; s20 = 1; s21 = 1;
        }
        // L-piece
        if pieceType == 6 {
            s00 = 1; s10 = 1; s20 = 1; s21 = 1;
        }
    }

    expose func getCell(row: Integer, col: Integer) -> Integer {
        if row == 0 {
            if col == 0 { return s00; }
            if col == 1 { return s01; }
            if col == 2 { return s02; }
            if col == 3 { return s03; }
        }
        if row == 1 {
            if col == 0 { return s10; }
            if col == 1 { return s11; }
            if col == 2 { return s12; }
            if col == 3 { return s13; }
        }
        if row == 2 {
            if col == 0 { return s20; }
            if col == 1 { return s21; }
            if col == 2 { return s22; }
            if col == 3 { return s23; }
        }
        if row == 3 {
            if col == 0 { return s30; }
            if col == 1 { return s31; }
            if col == 2 { return s32; }
            if col == 3 { return s33; }
        }
        return 0;
    }

    expose func setCell(row: Integer, col: Integer, val: Integer) {
        if row == 0 {
            if col == 0 { s00 = val; }
            if col == 1 { s01 = val; }
            if col == 2 { s02 = val; }
            if col == 3 { s03 = val; }
        }
        if row == 1 {
            if col == 0 { s10 = val; }
            if col == 1 { s11 = val; }
            if col == 2 { s12 = val; }
            if col == 3 { s13 = val; }
        }
        if row == 2 {
            if col == 0 { s20 = val; }
            if col == 1 { s21 = val; }
            if col == 2 { s22 = val; }
            if col == 3 { s23 = val; }
        }
        if row == 3 {
            if col == 0 { s30 = val; }
            if col == 1 { s31 = val; }
            if col == 2 { s32 = val; }
            if col == 3 { s33 = val; }
        }
    }

    expose func rotateClockwise() {
        // Store old values
        Integer t00 = s00; Integer t01 = s01; Integer t02 = s02; Integer t03 = s03;
        Integer t10 = s10; Integer t11 = s11; Integer t12 = s12; Integer t13 = s13;
        Integer t20 = s20; Integer t21 = s21; Integer t22 = s22; Integer t23 = s23;
        Integer t30 = s30; Integer t31 = s31; Integer t32 = s32; Integer t33 = s33;

        // Rotate: new[i][j] = old[3-j][i]
        s00 = t30; s01 = t20; s02 = t10; s03 = t00;
        s10 = t31; s11 = t21; s12 = t11; s13 = t01;
        s20 = t32; s21 = t22; s22 = t12; s23 = t02;
        s30 = t33; s31 = t23; s32 = t13; s33 = t03;

        rotation = rotation + 1;
        if rotation > 3 {
            rotation = 0;
        }
    }

    expose func moveLeft() { posX = posX - 1; }
    expose func moveRight() { posX = posX + 1; }
    expose func moveDown() { posY = posY + 1; }

    expose func getPosX() -> Integer { return posX; }
    expose func getPosY() -> Integer { return posY; }
    expose func setPosX(x: Integer) { posX = x; }
    expose func setPosY(y: Integer) { posY = y; }
    expose func getColor() -> Integer { return pieceColor; }
    expose func getType() -> Integer { return pieceType; }
}
