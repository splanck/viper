module Board;

import "./piece";

// Game board - 10 columns x 20 rows
// Cells contain 0 (empty) or color code (1-7)

entity Board {
    // Board is stored as a flat array: index = row * 10 + col
    // We use 200 cells for 20 rows x 10 columns
    List[Integer] cells;
    expose Integer width;
    expose Integer height;
    
    expose func init() {
        width = 10;
        height = 20;
        cells = [];
        // Initialize all cells to 0 (empty)
        var i = 0;
        while (i < 200) {
            cells.add(0);
            i = i + 1;
        }
    }
    
    expose func getCell(Integer row, Integer col) -> Integer {
        if (row < 0 || row >= height || col < 0 || col >= width) {
            return 0 - 1;  // Return -1 for out of bounds
        }
        Integer index = row * 10 + col;
        return cells.get(index);
    }
    
    expose func setCell(Integer row, Integer col, Integer val) {
        if (row >= 0 && row < height && col >= 0 && col < width) {
            Integer index = row * 10 + col;
            cells.set(index, val);
        }
    }
    
    expose func canPlacePiece(Piece p) -> Boolean {
        var i = 0;
        while (i < 4) {
            Integer py = p.y;
            Integer px = p.x;
            Integer br = p.getBlockRow(i);
            Integer bc = p.getBlockCol(i);
            Integer blockRow = py + br;
            Integer blockCol = px + bc;
            
            // Check bounds
            if (blockCol < 0 || blockCol >= width) {
                return false;
            }
            if (blockRow >= height) {
                return false;
            }
            
            // Check collision with placed pieces (only if row is on board)
            if (blockRow >= 0) {
                Integer cell = self.getCell(blockRow, blockCol);
                if (cell > 0) {
                    return false;
                }
            }
            
            i = i + 1;
        }
        return true;
    }
    
    expose func placePiece(Piece p) {
        Integer color = p.getColor();
        var i = 0;
        while (i < 4) {
            Integer py = p.y;
            Integer px = p.x;
            Integer br = p.getBlockRow(i);
            Integer bc = p.getBlockCol(i);
            Integer blockRow = py + br;
            Integer blockCol = px + bc;
            if (blockRow >= 0) {
                self.setCell(blockRow, blockCol, color);
            }
            i = i + 1;
        }
    }
    
    expose func isRowFull(Integer row) -> Boolean {
        var col = 0;
        while (col < width) {
            if (self.getCell(row, col) == 0) {
                return false;
            }
            col = col + 1;
        }
        return true;
    }
    
    expose func clearRow(Integer row) {
        // Move all rows above down by one
        var r = row;
        while (r > 0) {
            var col = 0;
            while (col < width) {
                Integer above = self.getCell(r - 1, col);
                self.setCell(r, col, above);
                col = col + 1;
            }
            r = r - 1;
        }
        // Clear top row
        var col = 0;
        while (col < width) {
            self.setCell(0, col, 0);
            col = col + 1;
        }
    }
    
    expose func clearFullRows() -> Integer {
        Integer linesCleared = 0;
        var row = height - 1;
        while (row >= 0) {
            if (self.isRowFull(row)) {
                self.clearRow(row);
                linesCleared = linesCleared + 1;
                // Don't decrement row - check same row again since rows shifted
            } else {
                row = row - 1;
            }
        }
        return linesCleared;
    }
}

func createBoard() -> Board {
    Board b = new Board();
    b.init();
    return b;
}
