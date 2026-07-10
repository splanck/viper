' test_game_logic.bas — Grid2D, StateMachine, ButtonGroup, ObjectPool, Quadtree
DIM grid AS OBJECT
DIM sm AS OBJECT
DIM bg AS OBJECT
DIM pool AS OBJECT
DIM qt AS OBJECT

grid = Viper.Game.Grid2D.New(4, 4, 0)
PRINT grid.Width
PRINT grid.Height

sm = Viper.Game.StateMachine.New()
PRINT sm.StateCount

bg = Viper.Game.ButtonGroup.New()
PRINT bg.Count

pool = Viper.Game.ObjectPool.New(8)
PRINT pool.Capacity

qt = Viper.Game.Quadtree.New(0.0, 0.0, 100.0, 100.0)
PRINT qt.ItemCount

PRINT "done"
END
