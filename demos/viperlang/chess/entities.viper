module Entities;

// Piece types (use as values 1-6, negative for black)
// EMPTY = 0
// PAWN = 1, KNIGHT = 2, BISHOP = 3, ROOK = 4, QUEEN = 5, KING = 6

// AI Move Result
entity AIResult {
    Integer fromRow;
    Integer fromCol;
    Integer toRow;
    Integer toCol;
    Integer score;
    Integer nodesSearched;

    expose func init() {
        fromRow = 0 - 1;
        fromCol = 0 - 1;
        toRow = 0 - 1;
        toCol = 0 - 1;
        score = 0;
        nodesSearched = 0;
    }

    expose func setMove(fr: Integer, fc: Integer, tr: Integer, tc: Integer, sc: Integer) {
        fromRow = fr;
        fromCol = fc;
        toRow = tr;
        toCol = tc;
        score = sc;
    }

    expose func getFromRow() -> Integer { return fromRow; }
    expose func getFromCol() -> Integer { return fromCol; }
    expose func getToRow() -> Integer { return toRow; }
    expose func getToCol() -> Integer { return toCol; }
    expose func getScore() -> Integer { return score; }
    expose func getNodes() -> Integer { return nodesSearched; }
    expose func incNodes() { nodesSearched = nodesSearched + 1; }
    expose func setNodes(n: Integer) { nodesSearched = n; }
    expose func hasMove() -> Integer {
        if fromRow >= 0 { return 1; }
        return 0;
    }
}

