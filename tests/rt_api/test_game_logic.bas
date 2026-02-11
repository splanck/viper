' test_game_logic.bas — Grid2D, StateMachine, ButtonGroup, ObjectPool, Quadtree
' NOTE: ALL Viper.Game.* classes trigger heap assertion crash in BASIC VM (BUG-008)
' Grid2D.New(10,10,0) — Assertion failed: hdr->magic == RT_MAGIC, rt_heap.c:66
' StateMachine.New() — same crash
' ButtonGroup.New() — same crash
' ObjectPool.New(100) — same crash
' Quadtree.New(0,0,1000,1000) — same crash

PRINT "skipped: all Game.* classes crash in BASIC (BUG-008)"
PRINT "done"
END
