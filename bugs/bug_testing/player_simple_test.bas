REM Simple Player test without engine ADDFILE
ADDFILE "adventure_player.bas"

DIM p AS Player
p = NEW Player()
p.Init("TestPlayer")
PRINT "Player created"
END
