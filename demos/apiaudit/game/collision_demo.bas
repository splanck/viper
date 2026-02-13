' collision_demo.bas - Comprehensive API audit for Viper.Game.CollisionRect and Collision
' Tests CollisionRect: New, X, Y, Width, Height, Right, Bottom, CenterX, CenterY,
'   SetPosition, SetSize, Set, SetCenter, Move, ContainsPoint, Overlaps,
'   OverlapsRect, OverlapX, OverlapY, Expand, ContainsRect
' Tests Collision (static): RectsOverlap, PointInRect, CirclesOverlap,
'   PointInCircle, CircleRect, Distance, DistanceSquared

PRINT "=== CollisionRect API Audit ==="

' --- New (x, y, w, h) ---
PRINT "--- New ---"
DIM r1 AS OBJECT
r1 = Viper.Game.CollisionRect.New(10.0, 20.0, 100.0, 50.0)
PRINT r1.X         ' 10.0
PRINT r1.Y         ' 20.0
PRINT r1.Width     ' 100.0
PRINT r1.Height    ' 50.0

' --- Right / Bottom ---
PRINT "--- Right / Bottom ---"
PRINT r1.Right     ' 110.0
PRINT r1.Bottom    ' 70.0

' --- CenterX / CenterY ---
PRINT "--- CenterX / CenterY ---"
PRINT r1.CenterX   ' 60.0
PRINT r1.CenterY   ' 45.0

' --- SetPosition ---
PRINT "--- SetPosition ---"
r1.SetPosition(0.0, 0.0)
PRINT r1.X         ' 0.0
PRINT r1.Y         ' 0.0

' --- SetSize ---
PRINT "--- SetSize ---"
r1.SetSize(200.0, 100.0)
PRINT r1.Width     ' 200.0
PRINT r1.Height    ' 100.0

' --- Set (x, y, w, h) ---
PRINT "--- Set ---"
r1.Set(5.0, 10.0, 50.0, 25.0)
PRINT r1.X         ' 5.0
PRINT r1.Y         ' 10.0
PRINT r1.Width     ' 50.0
PRINT r1.Height    ' 25.0

' --- SetCenter ---
PRINT "--- SetCenter ---"
r1.SetCenter(100.0, 100.0)
PRINT r1.CenterX   ' 100.0
PRINT r1.CenterY   ' 100.0

' --- Move ---
PRINT "--- Move ---"
r1.Move(10.0, -5.0)

' --- ContainsPoint ---
PRINT "--- ContainsPoint ---"
DIM r2 AS OBJECT
r2 = Viper.Game.CollisionRect.New(0.0, 0.0, 100.0, 100.0)
PRINT r2.ContainsPoint(50.0, 50.0)   ' 1 (inside)
PRINT r2.ContainsPoint(150.0, 50.0)  ' 0 (outside)
PRINT r2.ContainsPoint(0.0, 0.0)     ' 1 (corner)

' --- Overlaps ---
PRINT "--- Overlaps ---"
DIM r3 AS OBJECT
r3 = Viper.Game.CollisionRect.New(50.0, 50.0, 100.0, 100.0)
PRINT r2.Overlaps(r3)  ' 1 (overlapping)
DIM r4 AS OBJECT
r4 = Viper.Game.CollisionRect.New(200.0, 200.0, 10.0, 10.0)
PRINT r2.Overlaps(r4)  ' 0

' --- OverlapsRect ---
PRINT "--- OverlapsRect ---"
PRINT r2.OverlapsRect(50.0, 50.0, 100.0, 100.0)   ' 1
PRINT r2.OverlapsRect(200.0, 200.0, 10.0, 10.0)    ' 0

' --- OverlapX / OverlapY ---
PRINT "--- OverlapX / OverlapY ---"
PRINT r2.OverlapX(r3)  ' overlap amount on X
PRINT r2.OverlapY(r3)  ' overlap amount on Y

' --- Expand ---
PRINT "--- Expand ---"
DIM r5 AS OBJECT
r5 = Viper.Game.CollisionRect.New(10.0, 10.0, 20.0, 20.0)
r5.Expand(5.0)
PRINT r5.Width    ' 30.0
PRINT r5.Height   ' 30.0

' --- ContainsRect ---
PRINT "--- ContainsRect ---"
DIM outer AS OBJECT
outer = Viper.Game.CollisionRect.New(0.0, 0.0, 200.0, 200.0)
DIM inner AS OBJECT
inner = Viper.Game.CollisionRect.New(10.0, 10.0, 50.0, 50.0)
PRINT outer.ContainsRect(inner)  ' 1
PRINT inner.ContainsRect(outer)  ' 0

' =============================================
PRINT "=== Collision Static API Audit ==="

' --- RectsOverlap ---
PRINT "--- RectsOverlap ---"
PRINT Viper.Game.Collision.RectsOverlap(0.0, 0.0, 100.0, 100.0, 50.0, 50.0, 100.0, 100.0)  ' 1
PRINT Viper.Game.Collision.RectsOverlap(0.0, 0.0, 10.0, 10.0, 200.0, 200.0, 10.0, 10.0)     ' 0

' --- PointInRect ---
PRINT "--- PointInRect ---"
PRINT Viper.Game.Collision.PointInRect(50.0, 50.0, 0.0, 0.0, 100.0, 100.0)   ' 1
PRINT Viper.Game.Collision.PointInRect(150.0, 50.0, 0.0, 0.0, 100.0, 100.0)  ' 0

' --- CirclesOverlap ---
PRINT "--- CirclesOverlap ---"
PRINT Viper.Game.Collision.CirclesOverlap(0.0, 0.0, 50.0, 30.0, 0.0, 50.0)    ' 1
PRINT Viper.Game.Collision.CirclesOverlap(0.0, 0.0, 10.0, 200.0, 200.0, 10.0)  ' 0

' --- PointInCircle ---
PRINT "--- PointInCircle ---"
PRINT Viper.Game.Collision.PointInCircle(5.0, 5.0, 0.0, 0.0, 50.0)      ' 1
PRINT Viper.Game.Collision.PointInCircle(100.0, 100.0, 0.0, 0.0, 10.0)  ' 0

' --- CircleRect ---
PRINT "--- CircleRect ---"
PRINT Viper.Game.Collision.CircleRect(50.0, 50.0, 30.0, 0.0, 0.0, 100.0, 100.0)   ' 1
PRINT Viper.Game.Collision.CircleRect(200.0, 200.0, 5.0, 0.0, 0.0, 100.0, 100.0)  ' 0

' --- Distance ---
PRINT "--- Distance ---"
PRINT Viper.Game.Collision.Distance(0.0, 0.0, 3.0, 4.0)  ' 5.0

' --- DistanceSquared ---
PRINT "--- DistanceSquared ---"
PRINT Viper.Game.Collision.DistanceSquared(0.0, 0.0, 3.0, 4.0)  ' 25.0

PRINT "=== Collision audit complete ==="
END
