' quadtree_demo.bas
PRINT "=== Viper.Game.Quadtree Demo ==="
DIM qt AS OBJECT
qt = NEW Viper.Game.Quadtree(0, 0, 100, 100)
PRINT qt.ItemCount
PRINT qt.Insert(1, 10, 10, 5, 5)
PRINT qt.Insert(2, 50, 50, 5, 5)
PRINT qt.ItemCount
PRINT qt.QueryRect(0, 0, 100, 100)
PRINT qt.GetResult(0)
PRINT qt.Remove(1)
PRINT qt.ItemCount
qt.Clear()
PRINT qt.ItemCount
PRINT "done"
END
