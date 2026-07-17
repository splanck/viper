' test_game_logic.bas — Grid2D, StateMachine, ButtonGroup, ObjectPool, Quadtree
DIM grid AS OBJECT
DIM sm AS OBJECT
DIM bg AS OBJECT
DIM pool AS OBJECT
DIM qt AS OBJECT

grid = Zanna.Game.Grid2D.New(4, 4, 0)
PRINT grid.Width
PRINT grid.Height

sm = Zanna.Game.StateMachine.New()
PRINT sm.StateCount

bg = Zanna.Game.ButtonGroup.New()
PRINT bg.Count

pool = Zanna.Game.ObjectPool.New(8)
PRINT pool.Capacity

qt = Zanna.Game.Quadtree.New(0.0, 0.0, 100.0, 100.0)
PRINT qt.ItemCount

PRINT "done"
END
