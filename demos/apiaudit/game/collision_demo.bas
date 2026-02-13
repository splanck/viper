' collision_demo.bas
PRINT "=== Viper.Game.Collision Demo ==="
PRINT Viper.Game.Collision.RectsOverlap(0.0, 0.0, 10.0, 10.0, 5.0, 5.0, 10.0, 10.0)
PRINT Viper.Game.Collision.RectsOverlap(0.0, 0.0, 10.0, 10.0, 20.0, 20.0, 10.0, 10.0)
PRINT Viper.Game.Collision.PointInRect(5.0, 5.0, 0.0, 0.0, 10.0, 10.0)
PRINT Viper.Game.Collision.CirclesOverlap(0.0, 0.0, 5.0, 3.0, 0.0, 5.0)
PRINT Viper.Game.Collision.Distance(0.0, 0.0, 3.0, 4.0)
PRINT "done"
END
