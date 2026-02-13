' collisionrect_demo.bas
PRINT "=== Viper.Game.CollisionRect Demo ==="
DIM r AS OBJECT
r = NEW Viper.Game.CollisionRect(10.0, 20.0, 50.0, 30.0)
PRINT r.X
PRINT r.Y
PRINT r.Width
PRINT r.Height
PRINT r.Right
PRINT r.Bottom
PRINT r.ContainsPoint(25.0, 30.0)
PRINT r.ContainsPoint(0.0, 0.0)
PRINT "done"
END
