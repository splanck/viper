module Chess;

import "./entities";

// Piece constants
Integer EMPTY = 0;
Integer PAWN = 1;
Integer KNIGHT = 2;
Integer BISHOP = 3;
Integer ROOK = 4;
Integer QUEEN = 5;
Integer KING = 6;

// Board state - 8 rows, each row stores 8 pieces (4 bits each = 32 bits)
// Encoding: 0=empty, 1-6=white, 9-14=black (add 8 to piece type)
Integer boardRow0 = 0;
Integer boardRow1 = 0;
Integer boardRow2 = 0;
Integer boardRow3 = 0;
Integer boardRow4 = 0;
Integer boardRow5 = 0;
Integer boardRow6 = 0;
Integer boardRow7 = 0;

// Game state
Integer whiteKingMoved = 0;
Integer blackKingMoved = 0;
Integer whiteRookAMoved = 0;
Integer whiteRookHMoved = 0;
Integer blackRookAMoved = 0;
Integer blackRookHMoved = 0;
Integer enPassantFile = 0 - 1;
Integer whiteTurn = 1;
Integer gameOver = 0;
Integer aiDepth = 2;
Integer playerIsWhite = 1;
Integer vsComputer = 1;

func getRow(r: Integer) -> Integer {
    if r == 0 { return boardRow0; }
    if r == 1 { return boardRow1; }
    if r == 2 { return boardRow2; }
    if r == 3 { return boardRow3; }
    if r == 4 { return boardRow4; }
    if r == 5 { return boardRow5; }
    if r == 6 { return boardRow6; }
    if r == 7 { return boardRow7; }
    return 0;
}

func setRow(r: Integer, v: Integer) {
    if r == 0 { boardRow0 = v; }
    if r == 1 { boardRow1 = v; }
    if r == 2 { boardRow2 = v; }
    if r == 3 { boardRow3 = v; }
    if r == 4 { boardRow4 = v; }
    if r == 5 { boardRow5 = v; }
    if r == 6 { boardRow6 = v; }
    if r == 7 { boardRow7 = v; }
}

func pow16(n: Integer) -> Integer {
    Integer result = 1;
    Integer i = 0;
    while i < n {
        result = result * 16;
        i = i + 1;
    }
    return result;
}

func getPiece(row: Integer, col: Integer) -> Integer {
    if row < 0 { return 0; }
    if row > 7 { return 0; }
    if col < 0 { return 0; }
    if col > 7 { return 0; }
    Integer rowVal = getRow(row);
    Integer shift = pow16(col);
    Integer extracted = rowVal / shift;
    Integer piece = extracted - (extracted / 16) * 16;
    return piece;
}

func setPiece(row: Integer, col: Integer, piece: Integer) {
    if row < 0 { return; }
    if row > 7 { return; }
    if col < 0 { return; }
    if col > 7 { return; }
    Integer rowVal = getRow(row);
    Integer shift = pow16(col);
    Integer old = rowVal / shift;
    Integer oldPiece = old - (old / 16) * 16;
    rowVal = rowVal - oldPiece * shift + piece * shift;
    setRow(row, rowVal);
}

func isWhitePiece(piece: Integer) -> Integer {
    if piece >= 1 {
        if piece <= 6 { return 1; }
    }
    return 0;
}

func isBlackPiece(piece: Integer) -> Integer {
    if piece >= 9 {
        if piece <= 14 { return 1; }
    }
    return 0;
}

func pieceType(piece: Integer) -> Integer {
    if piece >= 9 { return piece - 8; }
    return piece;
}

func pieceValue(piece: Integer) -> Integer {
    Integer pt = pieceType(piece);
    if pt == PAWN { return 100; }
    if pt == KNIGHT { return 320; }
    if pt == BISHOP { return 330; }
    if pt == ROOK { return 500; }
    if pt == QUEEN { return 900; }
    if pt == KING { return 20000; }
    return 0;
}

func pieceSymbol(piece: Integer) -> String {
    if piece == 0 { return " "; }
    if piece == 1 { return "P"; }
    if piece == 2 { return "N"; }
    if piece == 3 { return "B"; }
    if piece == 4 { return "R"; }
    if piece == 5 { return "Q"; }
    if piece == 6 { return "K"; }
    if piece == 9 { return "p"; }
    if piece == 10 { return "n"; }
    if piece == 11 { return "b"; }
    if piece == 12 { return "r"; }
    if piece == 13 { return "q"; }
    if piece == 14 { return "k"; }
    return " ";
}

func resetBoard() {
    // Clear all
    Integer i = 0;
    while i < 8 {
        setRow(i, 0);
        i = i + 1;
    }
    // White pieces (row 0-1)
    setPiece(0, 0, ROOK);
    setPiece(0, 1, KNIGHT);
    setPiece(0, 2, BISHOP);
    setPiece(0, 3, QUEEN);
    setPiece(0, 4, KING);
    setPiece(0, 5, BISHOP);
    setPiece(0, 6, KNIGHT);
    setPiece(0, 7, ROOK);
    Integer c = 0;
    while c < 8 {
        setPiece(1, c, PAWN);
        c = c + 1;
    }
    // Black pieces (row 6-7) - add 8 to piece type
    c = 0;
    while c < 8 {
        setPiece(6, c, PAWN + 8);
        c = c + 1;
    }
    setPiece(7, 0, ROOK + 8);
    setPiece(7, 1, KNIGHT + 8);
    setPiece(7, 2, BISHOP + 8);
    setPiece(7, 3, QUEEN + 8);
    setPiece(7, 4, KING + 8);
    setPiece(7, 5, BISHOP + 8);
    setPiece(7, 6, KNIGHT + 8);
    setPiece(7, 7, ROOK + 8);
    // Reset flags
    whiteKingMoved = 0;
    blackKingMoved = 0;
    whiteRookAMoved = 0;
    whiteRookHMoved = 0;
    blackRookAMoved = 0;
    blackRookHMoved = 0;
    enPassantFile = 0 - 1;
}

func findKingRow(isWhite: Integer) -> Integer {
    Integer target = KING;
    if isWhite == 0 { target = KING + 8; }
    Integer r = 0;
    while r < 8 {
        Integer c = 0;
        while c < 8 {
            if getPiece(r, c) == target { return r; }
            c = c + 1;
        }
        r = r + 1;
    }
    return 0 - 1;
}

func findKingCol(isWhite: Integer) -> Integer {
    Integer target = KING;
    if isWhite == 0 { target = KING + 8; }
    Integer r = 0;
    while r < 8 {
        Integer c = 0;
        while c < 8 {
            if getPiece(r, c) == target { return c; }
            c = c + 1;
        }
        r = r + 1;
    }
    return 0 - 1;
}

func absVal(v: Integer) -> Integer {
    if v < 0 { return 0 - v; }
    return v;
}

func isPathClear(fromRow: Integer, fromCol: Integer, toRow: Integer, toCol: Integer) -> Integer {
    Integer dr = 0;
    Integer dc = 0;
    if toRow > fromRow { dr = 1; }
    if toRow < fromRow { dr = 0 - 1; }
    if toCol > fromCol { dc = 1; }
    if toCol < fromCol { dc = 0 - 1; }
    Integer r = fromRow + dr;
    Integer c = fromCol + dc;
    while r != toRow {
        if getPiece(r, c) != EMPTY { return 0; }
        r = r + dr;
        c = c + dc;
    }
    while c != toCol {
        if getPiece(r, c) != EMPTY { return 0; }
        c = c + dc;
    }
    return 1;
}

func isSquareAttacked(row: Integer, col: Integer, byWhite: Integer) -> Integer {
    Integer r = 0;
    while r < 8 {
        Integer c = 0;
        while c < 8 {
            Integer piece = getPiece(r, c);
            if piece != EMPTY {
                Integer isPieceWhite = isWhitePiece(piece);
                if byWhite == isPieceWhite {
                    Integer pt = pieceType(piece);
                    // Pawn attacks
                    if pt == PAWN {
                        if isPieceWhite == 1 {
                            if r + 1 == row {
                                if c - 1 == col { return 1; }
                                if c + 1 == col { return 1; }
                            }
                        } else {
                            if r - 1 == row {
                                if c - 1 == col { return 1; }
                                if c + 1 == col { return 1; }
                            }
                        }
                    }
                    // Knight attacks
                    if pt == KNIGHT {
                        Integer dr = absVal(row - r);
                        Integer dc = absVal(col - c);
                        if dr == 2 {
                            if dc == 1 { return 1; }
                        }
                        if dr == 1 {
                            if dc == 2 { return 1; }
                        }
                    }
                    // King attacks
                    if pt == KING {
                        Integer dr = absVal(row - r);
                        Integer dc = absVal(col - c);
                        if dr <= 1 {
                            if dc <= 1 {
                                if dr + dc > 0 { return 1; }
                            }
                        }
                    }
                    // Rook/Queen attacks
                    if pt == ROOK {
                        if r == row {
                            if isPathClear(r, c, row, col) == 1 { return 1; }
                        }
                        if c == col {
                            if isPathClear(r, c, row, col) == 1 { return 1; }
                        }
                    }
                    if pt == QUEEN {
                        if r == row {
                            if isPathClear(r, c, row, col) == 1 { return 1; }
                        }
                        if c == col {
                            if isPathClear(r, c, row, col) == 1 { return 1; }
                        }
                    }
                    // Bishop/Queen attacks
                    if pt == BISHOP {
                        Integer dr = absVal(row - r);
                        Integer dc = absVal(col - c);
                        if dr == dc {
                            if dr > 0 {
                                if isPathClear(r, c, row, col) == 1 { return 1; }
                            }
                        }
                    }
                    if pt == QUEEN {
                        Integer dr = absVal(row - r);
                        Integer dc = absVal(col - c);
                        if dr == dc {
                            if dr > 0 {
                                if isPathClear(r, c, row, col) == 1 { return 1; }
                            }
                        }
                    }
                }
            }
            c = c + 1;
        }
        r = r + 1;
    }
    return 0;
}

func isInCheck(isWhite: Integer) -> Integer {
    Integer kingRow = findKingRow(isWhite);
    Integer kingCol = findKingCol(isWhite);
    if kingRow < 0 { return 0; }
    Integer attackByWhite = 0;
    if isWhite == 1 { attackByWhite = 0; } else { attackByWhite = 1; }
    return isSquareAttacked(kingRow, kingCol, attackByWhite);
}

func isValidMove(fromRow: Integer, fromCol: Integer, toRow: Integer, toCol: Integer, isWhiteTurn: Integer) -> Integer {
    if fromRow < 0 { return 0; }
    if fromRow > 7 { return 0; }
    if fromCol < 0 { return 0; }
    if fromCol > 7 { return 0; }
    if toRow < 0 { return 0; }
    if toRow > 7 { return 0; }
    if toCol < 0 { return 0; }
    if toCol > 7 { return 0; }
    if fromRow == toRow {
        if fromCol == toCol { return 0; }
    }
    Integer piece = getPiece(fromRow, fromCol);
    Integer target = getPiece(toRow, toCol);
    if piece == EMPTY { return 0; }
    // Check correct color
    if isWhiteTurn == 1 {
        if isWhitePiece(piece) == 0 { return 0; }
    } else {
        if isBlackPiece(piece) == 0 { return 0; }
    }
    // Can't capture own piece
    if target != EMPTY {
        if isWhitePiece(piece) == isWhitePiece(target) { return 0; }
        if isBlackPiece(piece) == isBlackPiece(target) { return 0; }
    }
    Integer pt = pieceType(piece);
    Integer dr = toRow - fromRow;
    Integer dc = toCol - fromCol;
    Integer absDr = absVal(dr);
    Integer absDc = absVal(dc);
    // Pawn
    if pt == PAWN {
        if isWhitePiece(piece) == 1 {
            if dc == 0 {
                if target == EMPTY {
                    if dr == 1 { return 1; }
                    if dr == 2 {
                        if fromRow == 1 {
                            if getPiece(2, fromCol) == EMPTY { return 1; }
                        }
                    }
                }
            }
            if dr == 1 {
                if absDc == 1 {
                    if isBlackPiece(target) == 1 { return 1; }
                    if toCol == enPassantFile {
                        if fromRow == 4 { return 1; }
                    }
                }
            }
        } else {
            if dc == 0 {
                if target == EMPTY {
                    if dr == 0 - 1 { return 1; }
                    if dr == 0 - 2 {
                        if fromRow == 6 {
                            if getPiece(5, fromCol) == EMPTY { return 1; }
                        }
                    }
                }
            }
            if dr == 0 - 1 {
                if absDc == 1 {
                    if isWhitePiece(target) == 1 { return 1; }
                    if toCol == enPassantFile {
                        if fromRow == 3 { return 1; }
                    }
                }
            }
        }
    }
    // Knight
    if pt == KNIGHT {
        if absDr == 2 {
            if absDc == 1 { return 1; }
        }
        if absDr == 1 {
            if absDc == 2 { return 1; }
        }
    }
    // Bishop
    if pt == BISHOP {
        if absDr == absDc {
            if absDr > 0 {
                if isPathClear(fromRow, fromCol, toRow, toCol) == 1 { return 1; }
            }
        }
    }
    // Rook
    if pt == ROOK {
        if fromRow == toRow {
            if isPathClear(fromRow, fromCol, toRow, toCol) == 1 { return 1; }
        }
        if fromCol == toCol {
            if isPathClear(fromRow, fromCol, toRow, toCol) == 1 { return 1; }
        }
    }
    // Queen
    if pt == QUEEN {
        if fromRow == toRow {
            if isPathClear(fromRow, fromCol, toRow, toCol) == 1 { return 1; }
        }
        if fromCol == toCol {
            if isPathClear(fromRow, fromCol, toRow, toCol) == 1 { return 1; }
        }
        if absDr == absDc {
            if isPathClear(fromRow, fromCol, toRow, toCol) == 1 { return 1; }
        }
    }
    // King
    if pt == KING {
        if absDr <= 1 {
            if absDc <= 1 { return 1; }
        }
        // Castling
        if fromCol == 4 {
            if absDr == 0 {
                if absDc == 2 {
                    if isWhitePiece(piece) == 1 {
                        if whiteKingMoved == 0 {
                            if toCol == 6 {
                                if whiteRookHMoved == 0 {
                                    if getPiece(0, 5) == EMPTY {
                                        if getPiece(0, 6) == EMPTY {
                                            if isInCheck(1) == 0 {
                                                if isSquareAttacked(0, 5, 0) == 0 { return 1; }
                                            }
                                        }
                                    }
                                }
                            }
                            if toCol == 2 {
                                if whiteRookAMoved == 0 {
                                    if getPiece(0, 1) == EMPTY {
                                        if getPiece(0, 2) == EMPTY {
                                            if getPiece(0, 3) == EMPTY {
                                                if isInCheck(1) == 0 {
                                                    if isSquareAttacked(0, 3, 0) == 0 { return 1; }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        if blackKingMoved == 0 {
                            if toCol == 6 {
                                if blackRookHMoved == 0 {
                                    if getPiece(7, 5) == EMPTY {
                                        if getPiece(7, 6) == EMPTY {
                                            if isInCheck(0) == 0 {
                                                if isSquareAttacked(7, 5, 1) == 0 { return 1; }
                                            }
                                        }
                                    }
                                }
                            }
                            if toCol == 2 {
                                if blackRookAMoved == 0 {
                                    if getPiece(7, 1) == EMPTY {
                                        if getPiece(7, 2) == EMPTY {
                                            if getPiece(7, 3) == EMPTY {
                                                if isInCheck(0) == 0 {
                                                    if isSquareAttacked(7, 3, 1) == 0 { return 1; }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}

func leavesKingInCheck(fromRow: Integer, fromCol: Integer, toRow: Integer, toCol: Integer, isWhiteTurn: Integer) -> Integer {
    Integer savedFrom = getPiece(fromRow, fromCol);
    Integer savedTo = getPiece(toRow, toCol);
    setPiece(toRow, toCol, savedFrom);
    setPiece(fromRow, fromCol, EMPTY);
    Integer result = isInCheck(isWhiteTurn);
    setPiece(fromRow, fromCol, savedFrom);
    setPiece(toRow, toCol, savedTo);
    return result;
}

func isLegalMove(fromRow: Integer, fromCol: Integer, toRow: Integer, toCol: Integer, isWhiteTurn: Integer) -> Integer {
    if isValidMove(fromRow, fromCol, toRow, toCol, isWhiteTurn) == 0 { return 0; }
    if leavesKingInCheck(fromRow, fromCol, toRow, toCol, isWhiteTurn) == 1 { return 0; }
    return 1;
}

func hasLegalMoves(isWhiteTurn: Integer) -> Integer {
    Integer fromRow = 0;
    while fromRow < 8 {
        Integer fromCol = 0;
        while fromCol < 8 {
            Integer piece = getPiece(fromRow, fromCol);
            Integer skip = 0;
            if piece == EMPTY { skip = 1; }
            if skip == 0 {
                if isWhiteTurn == 1 {
                    if isWhitePiece(piece) == 0 { skip = 1; }
                } else {
                    if isBlackPiece(piece) == 0 { skip = 1; }
                }
            }
            if skip == 0 {
                Integer toRow = 0;
                while toRow < 8 {
                    Integer toCol = 0;
                    while toCol < 8 {
                        if isLegalMove(fromRow, fromCol, toRow, toCol, isWhiteTurn) == 1 { return 1; }
                        toCol = toCol + 1;
                    }
                    toRow = toRow + 1;
                }
            }
            fromCol = fromCol + 1;
        }
        fromRow = fromRow + 1;
    }
    return 0;
}

func makeMove(fromRow: Integer, fromCol: Integer, toRow: Integer, toCol: Integer) {
    Integer piece = getPiece(fromRow, fromCol);
    Integer pt = pieceType(piece);
    // En passant capture
    if pt == PAWN {
        if toCol == enPassantFile {
            if isWhitePiece(piece) == 1 {
                if fromRow == 4 {
                    if toRow == 5 { setPiece(4, toCol, EMPTY); }
                }
            } else {
                if fromRow == 3 {
                    if toRow == 2 { setPiece(3, toCol, EMPTY); }
                }
            }
        }
    }
    // Reset en passant
    enPassantFile = 0 - 1;
    // Set en passant for pawn double move
    if pt == PAWN {
        if fromRow == 1 {
            if toRow == 3 { enPassantFile = fromCol; }
        }
        if fromRow == 6 {
            if toRow == 4 { enPassantFile = fromCol; }
        }
    }
    // Castling
    if pt == KING {
        if isWhitePiece(piece) == 1 { whiteKingMoved = 1; }
        if isBlackPiece(piece) == 1 { blackKingMoved = 1; }
        // Kingside
        if fromCol == 4 {
            if toCol == 6 {
                setPiece(fromRow, 5, getPiece(fromRow, 7));
                setPiece(fromRow, 7, EMPTY);
            }
        }
        // Queenside
        if fromCol == 4 {
            if toCol == 2 {
                setPiece(fromRow, 3, getPiece(fromRow, 0));
                setPiece(fromRow, 0, EMPTY);
            }
        }
    }
    // Track rook moves
    if pt == ROOK {
        if fromRow == 0 {
            if fromCol == 0 { whiteRookAMoved = 1; }
            if fromCol == 7 { whiteRookHMoved = 1; }
        }
        if fromRow == 7 {
            if fromCol == 0 { blackRookAMoved = 1; }
            if fromCol == 7 { blackRookHMoved = 1; }
        }
    }
    // Move piece
    setPiece(toRow, toCol, piece);
    setPiece(fromRow, fromCol, EMPTY);
    // Pawn promotion
    if pt == PAWN {
        if isWhitePiece(piece) == 1 {
            if toRow == 7 { setPiece(toRow, toCol, QUEEN); }
        } else {
            if toRow == 0 { setPiece(toRow, toCol, QUEEN + 8); }
        }
    }
}

func evaluate() -> Integer {
    Integer score = 0;
    Integer r = 0;
    while r < 8 {
        Integer c = 0;
        while c < 8 {
            Integer piece = getPiece(r, c);
            if piece != EMPTY {
                Integer val = pieceValue(piece);
                if isWhitePiece(piece) == 1 {
                    score = score + val;
                    // Pawn advancement
                    if pieceType(piece) == PAWN { score = score + r * 10; }
                    // Center control
                    if r >= 3 {
                        if r <= 4 {
                            if c >= 3 {
                                if c <= 4 { score = score + 10; }
                            }
                        }
                    }
                } else {
                    score = score - val;
                    if pieceType(piece) == PAWN { score = score - (7 - r) * 10; }
                    if r >= 3 {
                        if r <= 4 {
                            if c >= 3 {
                                if c <= 4 { score = score - 10; }
                            }
                        }
                    }
                }
            }
            c = c + 1;
        }
        r = r + 1;
    }
    return score;
}

func minimax(depth: Integer, alpha: Integer, beta: Integer, isMax: Integer, result: AIResult) -> Integer {
    result.incNodes();
    if depth == 0 { return evaluate(); }
    // Check for end game
    Integer turn = 1;
    if isMax == 0 { turn = 0; }
    if hasLegalMoves(turn) == 0 {
        if isInCheck(turn) == 1 {
            if isMax == 1 { return 0 - 19000 - depth; }
            return 19000 + depth;
        }
        return 0;
    }
    Integer cutoff = 0;
    if isMax == 1 {
        Integer bestScore = 0 - 30000;
        Integer fromRow = 0;
        while fromRow < 8 {
            Integer fromCol = 0;
            while fromCol < 8 {
                Integer piece = getPiece(fromRow, fromCol);
                Integer skip = 0;
                if piece == EMPTY { skip = 1; }
                if skip == 0 {
                    if isWhitePiece(piece) == 0 { skip = 1; }
                }
                if skip == 0 {
                    if cutoff == 0 {
                        Integer toRow = 0;
                        while toRow < 8 {
                            Integer toCol = 0;
                            while toCol < 8 {
                                if cutoff == 0 {
                                    if isLegalMove(fromRow, fromCol, toRow, toCol, 1) == 1 {
                                        Integer savedFrom = getPiece(fromRow, fromCol);
                                        Integer savedTo = getPiece(toRow, toCol);
                                        Integer savedEP = enPassantFile;
                                        setPiece(toRow, toCol, savedFrom);
                                        setPiece(fromRow, fromCol, EMPTY);
                                        enPassantFile = 0 - 1;
                                        Integer score = minimax(depth - 1, alpha, beta, 0, result);
                                        setPiece(fromRow, fromCol, savedFrom);
                                        setPiece(toRow, toCol, savedTo);
                                        enPassantFile = savedEP;
                                        if score > bestScore { bestScore = score; }
                                        if score > alpha { alpha = score; }
                                        if beta <= alpha { cutoff = 1; }
                                    }
                                }
                                toCol = toCol + 1;
                            }
                            toRow = toRow + 1;
                        }
                    }
                }
                fromCol = fromCol + 1;
            }
            fromRow = fromRow + 1;
        }
        return bestScore;
    } else {
        Integer bestScore = 30000;
        Integer fromRow = 0;
        while fromRow < 8 {
            Integer fromCol = 0;
            while fromCol < 8 {
                Integer piece = getPiece(fromRow, fromCol);
                Integer skip = 0;
                if piece == EMPTY { skip = 1; }
                if skip == 0 {
                    if isBlackPiece(piece) == 0 { skip = 1; }
                }
                if skip == 0 {
                    if cutoff == 0 {
                        Integer toRow = 0;
                        while toRow < 8 {
                            Integer toCol = 0;
                            while toCol < 8 {
                                if cutoff == 0 {
                                    if isLegalMove(fromRow, fromCol, toRow, toCol, 0) == 1 {
                                        Integer savedFrom = getPiece(fromRow, fromCol);
                                        Integer savedTo = getPiece(toRow, toCol);
                                        Integer savedEP = enPassantFile;
                                        setPiece(toRow, toCol, savedFrom);
                                        setPiece(fromRow, fromCol, EMPTY);
                                        enPassantFile = 0 - 1;
                                        Integer score = minimax(depth - 1, alpha, beta, 1, result);
                                        setPiece(fromRow, fromCol, savedFrom);
                                        setPiece(toRow, toCol, savedTo);
                                        enPassantFile = savedEP;
                                        if score < bestScore { bestScore = score; }
                                        if score < beta { beta = score; }
                                        if beta <= alpha { cutoff = 1; }
                                    }
                                }
                                toCol = toCol + 1;
                            }
                            toRow = toRow + 1;
                        }
                    }
                }
                fromCol = fromCol + 1;
            }
            fromRow = fromRow + 1;
        }
        return bestScore;
    }
}

func findBestMove(isWhiteTurn: Integer, searchDepth: Integer, result: AIResult) {
    result.init();
    Integer alpha = 0 - 30000;
    Integer beta = 30000;
    Integer bestScore = 0 - 30000;
    if isWhiteTurn == 0 { bestScore = 30000; }
    Integer fromRow = 0;
    while fromRow < 8 {
        Integer fromCol = 0;
        while fromCol < 8 {
            Integer piece = getPiece(fromRow, fromCol);
            Integer skip = 0;
            if piece == EMPTY { skip = 1; }
            if skip == 0 {
                if isWhiteTurn == 1 {
                    if isWhitePiece(piece) == 0 { skip = 1; }
                } else {
                    if isBlackPiece(piece) == 0 { skip = 1; }
                }
            }
            if skip == 0 {
                Integer toRow = 0;
                while toRow < 8 {
                    Integer toCol = 0;
                    while toCol < 8 {
                        if isLegalMove(fromRow, fromCol, toRow, toCol, isWhiteTurn) == 1 {
                            Integer savedFrom = getPiece(fromRow, fromCol);
                            Integer savedTo = getPiece(toRow, toCol);
                            Integer savedEP = enPassantFile;
                            setPiece(toRow, toCol, savedFrom);
                            setPiece(fromRow, fromCol, EMPTY);
                            enPassantFile = 0 - 1;
                            Integer isMax = 0;
                            if isWhiteTurn == 0 { isMax = 1; }
                            Integer score = minimax(searchDepth - 1, alpha, beta, isMax, result);
                            setPiece(fromRow, fromCol, savedFrom);
                            setPiece(toRow, toCol, savedTo);
                            enPassantFile = savedEP;
                            if isWhiteTurn == 1 {
                                if score > bestScore {
                                    bestScore = score;
                                    result.setMove(fromRow, fromCol, toRow, toCol, score);
                                }
                                if score > alpha { alpha = score; }
                            } else {
                                if score < bestScore {
                                    bestScore = score;
                                    result.setMove(fromRow, fromCol, toRow, toCol, score);
                                }
                                if score < beta { beta = score; }
                            }
                        }
                        toCol = toCol + 1;
                    }
                    toRow = toRow + 1;
                }
            }
            fromCol = fromCol + 1;
        }
        fromRow = fromRow + 1;
    }
}

func drawBoard() {
    Viper.Terminal.Clear();
    // Title
    Viper.Terminal.SetColor(14, 0);
    Viper.Terminal.SetPosition(1, 20);
    Viper.Terminal.Print("=== VIPER CHESS ===");
    // Column labels
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.SetPosition(3, 6);
    Viper.Terminal.Print("  a   b   c   d   e   f   g   h");
    // Draw rows (8 to 1)
    Integer r = 7;
    while r >= 0 {
        Integer screenRow = 4 + (7 - r) * 2;
        // Row label
        Viper.Terminal.SetPosition(screenRow, 3);
        Viper.Terminal.SetColor(15, 0);
        Viper.Terminal.PrintInt(r + 1);
        Viper.Terminal.Print(" ");
        // Draw squares
        Integer c = 0;
        while c < 8 {
            Integer bgColor = 3;
            if (r + c) - ((r + c) / 2) * 2 == 0 { bgColor = 4; }
            Integer piece = getPiece(r, c);
            Integer fgColor = bgColor;
            if isWhitePiece(piece) == 1 { fgColor = 15; }
            if isBlackPiece(piece) == 1 { fgColor = 0; }
            Viper.Terminal.SetColor(fgColor, bgColor);
            Viper.Terminal.Print(" ");
            Viper.Terminal.Print(pieceSymbol(piece));
            Viper.Terminal.Print(" ");
            Viper.Terminal.SetColor(7, 0);
            Viper.Terminal.Print(" ");
            c = c + 1;
        }
        // Row label on right
        Viper.Terminal.SetColor(15, 0);
        Viper.Terminal.Print(" ");
        Viper.Terminal.PrintInt(r + 1);
        r = r - 1;
    }
    // Column labels
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.SetPosition(21, 6);
    Viper.Terminal.Print("  a   b   c   d   e   f   g   h");
    // Status
    Viper.Terminal.SetPosition(5, 45);
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.Print("Turn: ");
    if whiteTurn == 1 {
        Viper.Terminal.Print("WHITE");
    } else {
        Viper.Terminal.Print("BLACK");
    }
    if isInCheck(whiteTurn) == 1 {
        Viper.Terminal.SetPosition(7, 45);
        Viper.Terminal.SetColor(12, 0);
        Viper.Terminal.Print("*** CHECK! ***");
    }
    // Help
    Viper.Terminal.SetPosition(10, 45);
    Viper.Terminal.SetColor(8, 0);
    Viper.Terminal.Print("Enter move: e2e4");
    Viper.Terminal.SetPosition(11, 45);
    Viper.Terminal.Print("q = quit");
    Viper.Terminal.SetColor(7, 0);
}

func showMainMenu() {
    Viper.Terminal.Clear();
    // Title
    Viper.Terminal.SetColor(14, 0);
    Viper.Terminal.SetPosition(3, 20);
    Viper.Terminal.Print("+=================================+");
    Viper.Terminal.SetPosition(4, 20);
    Viper.Terminal.Print("|                                 |");
    Viper.Terminal.SetPosition(5, 20);
    Viper.Terminal.Print("|");
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.Print("         VIPER CHESS           ");
    Viper.Terminal.SetColor(14, 0);
    Viper.Terminal.Print("|");
    Viper.Terminal.SetPosition(6, 20);
    Viper.Terminal.Print("|                                 |");
    Viper.Terminal.SetPosition(7, 20);
    Viper.Terminal.Print("|   ");
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.Print("K Q R B N P");
    Viper.Terminal.SetColor(8, 0);
    Viper.Terminal.Print("  k q r b n p");
    Viper.Terminal.SetColor(14, 0);
    Viper.Terminal.Print("   |");
    Viper.Terminal.SetPosition(8, 20);
    Viper.Terminal.Print("|                                 |");
    Viper.Terminal.SetPosition(9, 20);
    Viper.Terminal.Print("+=================================+");
    // Options
    Viper.Terminal.SetPosition(12, 25);
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.Print("[1] ");
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.Print("Play as White");
    Viper.Terminal.SetPosition(14, 25);
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.Print("[2] ");
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.Print("Play as Black");
    Viper.Terminal.SetPosition(16, 25);
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.Print("[3] ");
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.Print("Two Players");
    Viper.Terminal.SetPosition(18, 25);
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.Print("[Q] ");
    Viper.Terminal.SetColor(12, 0);
    Viper.Terminal.Print("Quit");
    // Difficulty
    Viper.Terminal.SetPosition(21, 22);
    Viper.Terminal.SetColor(8, 0);
    Viper.Terminal.Print("AI Depth: ");
    Viper.Terminal.PrintInt(aiDepth);
    Viper.Terminal.Print(" (press 4-6 to change)");
    Viper.Terminal.SetColor(7, 0);
}

func colFromChar(ch: String) -> Integer {
    if ch == "a" { return 0; }
    if ch == "b" { return 1; }
    if ch == "c" { return 2; }
    if ch == "d" { return 3; }
    if ch == "e" { return 4; }
    if ch == "f" { return 5; }
    if ch == "g" { return 6; }
    if ch == "h" { return 7; }
    if ch == "A" { return 0; }
    if ch == "B" { return 1; }
    if ch == "C" { return 2; }
    if ch == "D" { return 3; }
    if ch == "E" { return 4; }
    if ch == "F" { return 5; }
    if ch == "G" { return 6; }
    if ch == "H" { return 7; }
    return 0 - 1;
}

func rowFromChar(ch: String) -> Integer {
    if ch == "1" { return 0; }
    if ch == "2" { return 1; }
    if ch == "3" { return 2; }
    if ch == "4" { return 3; }
    if ch == "5" { return 4; }
    if ch == "6" { return 5; }
    if ch == "7" { return 6; }
    if ch == "8" { return 7; }
    return 0 - 1;
}

func playGame() {
    resetBoard();
    whiteTurn = 1;
    gameOver = 0;
    AIResult aiResult = new AIResult();
    while gameOver == 0 {
        drawBoard();
        // Check for checkmate/stalemate
        if hasLegalMoves(whiteTurn) == 0 {
            if isInCheck(whiteTurn) == 1 {
                Viper.Terminal.SetPosition(23, 5);
                Viper.Terminal.SetColor(12, 0);
                Viper.Terminal.Print("CHECKMATE! ");
                if whiteTurn == 1 {
                    Viper.Terminal.Print("Black wins!");
                } else {
                    Viper.Terminal.Print("White wins!");
                }
            } else {
                Viper.Terminal.SetPosition(23, 5);
                Viper.Terminal.SetColor(14, 0);
                Viper.Terminal.Print("STALEMATE! Draw.");
            }
            Viper.Terminal.SetPosition(24, 5);
            Viper.Terminal.SetColor(8, 0);
            Viper.Terminal.Print("Press any key...");
            Viper.Terminal.GetKey();
            gameOver = 1;
        }
        if gameOver == 0 {
            // Check if computer's turn
            Integer isComputerTurn = 0;
            if vsComputer == 1 {
                if whiteTurn == 1 {
                    if playerIsWhite == 0 { isComputerTurn = 1; }
                } else {
                    if playerIsWhite == 1 { isComputerTurn = 1; }
                }
            }
            if isComputerTurn == 1 {
                Viper.Terminal.SetPosition(23, 5);
                Viper.Terminal.SetColor(14, 0);
                Viper.Terminal.Print("Computer thinking...");
                findBestMove(whiteTurn, aiDepth, aiResult);
                if aiResult.hasMove() == 1 {
                    makeMove(aiResult.getFromRow(), aiResult.getFromCol(), aiResult.getToRow(), aiResult.getToCol());
                    whiteTurn = 1 - whiteTurn;
                } else {
                    gameOver = 1;
                }
            } else {
                // Human's turn
                Viper.Terminal.SetPosition(23, 5);
                Viper.Terminal.SetColor(15, 0);
                if whiteTurn == 1 {
                    Viper.Terminal.Print("White's move: ");
                } else {
                    Viper.Terminal.Print("Black's move: ");
                }
                // Get 4 chars
                String c1 = Viper.Terminal.GetKey();
                if c1 == "q" { gameOver = 1; }
                if c1 == "Q" { gameOver = 1; }
                if gameOver == 0 {
                    Viper.Terminal.Print(c1);
                    String c2 = Viper.Terminal.GetKey();
                    Viper.Terminal.Print(c2);
                    String c3 = Viper.Terminal.GetKey();
                    Viper.Terminal.Print(c3);
                    String c4 = Viper.Terminal.GetKey();
                    Viper.Terminal.Print(c4);
                    Integer fromCol = colFromChar(c1);
                    Integer fromRow = rowFromChar(c2);
                    Integer toCol = colFromChar(c3);
                    Integer toRow = rowFromChar(c4);
                    if isLegalMove(fromRow, fromCol, toRow, toCol, whiteTurn) == 1 {
                        makeMove(fromRow, fromCol, toRow, toCol);
                        whiteTurn = 1 - whiteTurn;
                    } else {
                        Viper.Terminal.SetPosition(24, 5);
                        Viper.Terminal.SetColor(12, 0);
                        Viper.Terminal.Print("Invalid move! Press any key...");
                        Viper.Terminal.GetKey();
                    }
                }
            }
        }
    }
}

func start() {
    Integer running = 1;
    while running == 1 {
        showMainMenu();
        String choice = Viper.Terminal.GetKey();
        if choice == "1" {
            playerIsWhite = 1;
            vsComputer = 1;
            playGame();
        }
        if choice == "2" {
            playerIsWhite = 0;
            vsComputer = 1;
            playGame();
        }
        if choice == "3" {
            vsComputer = 0;
            playGame();
        }
        if choice == "4" { aiDepth = 1; }
        if choice == "5" { aiDepth = 2; }
        if choice == "6" { aiDepth = 3; }
        if choice == "q" { running = 0; }
        if choice == "Q" { running = 0; }
    }
    Viper.Terminal.Clear();
    Viper.Terminal.SetPosition(1, 1);
    Viper.Terminal.Print("Thanks for playing VIPER CHESS!");
}

