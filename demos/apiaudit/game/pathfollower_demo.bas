' pathfollower_demo.bas
PRINT "=== Viper.Game.PathFollower Demo ==="
DIM pf AS OBJECT
pf = NEW Viper.Game.PathFollower()
PRINT pf.PointCount
PRINT pf.AddPoint(0, 0)
PRINT pf.AddPoint(100, 0)
PRINT pf.AddPoint(100, 100)
PRINT pf.PointCount
PRINT pf.Speed
PRINT pf.IsActive
pf.Clear()
PRINT pf.PointCount
PRINT "done"
END
