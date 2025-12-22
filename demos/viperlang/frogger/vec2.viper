module Vec2;

// =============================================================================
// Vec2 - 2D Position Value Type
// Uses value semantics for clean, copyable positions
// =============================================================================

value Vec2 {
    Integer x;
    Integer y;

    func init(Integer px, Integer py) {
        x = px;
        y = py;
    }

    func set(Integer px, Integer py) {
        x = px;
        y = py;
    }

    func add(Integer dx, Integer dy) {
        x = x + dx;
        y = y + dy;
    }

    func getX() -> Integer {
        return x;
    }

    func getY() -> Integer {
        return y;
    }

    func equals(Integer px, Integer py) -> Boolean {
        return x == px && y == py;
    }

    func inBounds(Integer minX, Integer maxX, Integer minY, Integer maxY) -> Boolean {
        return x >= minX && x <= maxX && y >= minY && y <= maxY;
    }

    func manhattanDistance(Integer px, Integer py) -> Integer {
        Integer dx = x - px;
        Integer dy = y - py;
        if (dx < 0) { dx = 0 - dx; }
        if (dy < 0) { dy = 0 - dy; }
        return dx + dy;
    }
}

// Factory function
func createVec2(Integer x, Integer y) -> Vec2 {
    var v: Vec2;
    v.init(x, y);
    return v;
}
