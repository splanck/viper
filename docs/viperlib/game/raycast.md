# Viper.Game.Raycast & Collision Line Tests

2D line intersection and tilemap raycasting for shooting, AI sight, and collision.

## API

### Raycast.HasLineOfSight(tilemap, x1, y1, x2, y2) -> Boolean
Returns true if no solid tile blocks the path between two points.
Tilemap rays use grid DDA traversal, so long rays are not capped by a fixed pixel-step limit.

### Collision.LineRect(x1, y1, x2, y2, rx, ry, rw, rh) -> Boolean
Line segment vs axis-aligned rectangle intersection (Liang-Barsky).

### Collision.LineCircle(x1, y1, x2, y2, cx, cy, r) -> Boolean
Line segment vs circle intersection.
Zero-length segments are treated as point-in-circle tests. Negative, non-finite, or otherwise
invalid radii return false.

## Example
```zia
if Raycast.HasLineOfSight(tilemap, enemyX, enemyY, playerX, playerY) {
    // Enemy can see player — chase!
}

if Collision.LineRect(bulletX, bulletY, targetX, targetY, wallX, wallY, wallW, wallH) {
    // Bullet path blocked by wall
}
```
