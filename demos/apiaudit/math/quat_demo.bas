' quat_demo.bas
PRINT "=== Viper.Math.Quat Demo ==="
DIM q AS OBJECT
q = NEW Viper.Math.Quat(0.0, 0.0, 0.0, 1.0)
PRINT q.X
PRINT q.Y
PRINT q.Z
PRINT q.W
PRINT q.Len()
PRINT q.LenSq()
PRINT "done"
END
