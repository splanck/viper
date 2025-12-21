module Position;

// Position value object for 2D coordinates
entity Position {
    expose Integer row;
    expose Integer col;

    expose func init(Integer r, Integer c) {
        row = r;
        col = c;
    }

    expose func set(Integer r, Integer c) {
        row = r;
        col = c;
    }

    expose func getRow() -> Integer {
        return row;
    }

    expose func getCol() -> Integer {
        return col;
    }

    expose func moveUp() {
        row = row - 1;
    }

    expose func moveDown() {
        row = row + 1;
    }

    expose func moveLeft() {
        col = col - 1;
    }

    expose func moveRight() {
        col = col + 1;
    }

    expose func isInBounds(Integer minRow, Integer maxRow, Integer minCol, Integer maxCol) -> Boolean {
        return row >= minRow && row <= maxRow && col >= minCol && col <= maxCol;
    }

    expose func distanceTo(Position other) -> Integer {
        Integer rowDiff = row - other.row;
        Integer colDiff = col - other.col;

        // Absolute value
        if (rowDiff < 0) {
            rowDiff = 0 - rowDiff;
        }
        if (colDiff < 0) {
            colDiff = 0 - colDiff;
        }

        return rowDiff + colDiff;
    }

    expose func clone() -> Position {
        Position p = new Position();
        p.init(row, col);
        return p;
    }
}

func createPosition(Integer row, Integer col) -> Position {
    Position p = new Position();
    p.init(row, col);
    return p;
}
