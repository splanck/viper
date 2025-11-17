REM ╔════════════════════════════════════════════════════════╗
REM ║      FROGGER - AddFile Multi-Module Test              ║
REM ╚════════════════════════════════════════════════════════╝

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         FROGGER AddFile Demo                          ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

PRINT "Loading game classes from external module..."
AddFile "frogger_classes_module.bas"
PRINT

PRINT "Creating game objects..."
DIM player AS Frog
player = NEW Frog()
player.Init()
PRINT "✓ Player frog created at ("; player.x; ", "; player.y; ")"

DIM obstacle1 AS Obstacle
obstacle1 = NEW Obstacle()
obstacle1.Init(5, 2, 5)
PRINT "✓ Obstacle 1 created"

DIM obstacle2 AS Obstacle
obstacle2 = NEW Obstacle()
obstacle2.Init(7, 1, 15)
PRINT "✓ Obstacle 2 created"
PRINT

PRINT "═══════════════════════════════════════════════════════"
PRINT "               GAME SIMULATION"
PRINT "═══════════════════════════════════════════════════════"
PRINT

DIM tick AS INTEGER
FOR tick = 1 TO 10
    PRINT "Turn "; tick; ":"
    
    REM Move obstacles
    obstacle1.Move()
    obstacle2.Move()
    
    REM Display positions
    PRINT "  Frog: Y="; player.y; ", X="; player.x; " | Score: "; player.score
    PRINT "  Obstacle1: Y="; obstacle1.y; ", X="; obstacle1.x
    PRINT "  Obstacle2: Y="; obstacle2.y; ", X="; obstacle2.x
    
    REM Check collisions
    IF obstacle1.HitsFrog(player.x, player.y) OR obstacle2.HitsFrog(player.x, player.y) THEN
        COLOR 12, 0
        PRINT "  💥 COLLISION!"
        COLOR 15, 0
    END IF
    
    REM Move player
    IF tick MOD 2 = 0 THEN
        player.MoveUp()
        IF player.AtGoal() THEN
            COLOR 10, 0
            PRINT "  🎯 GOAL REACHED!"
            COLOR 15, 0
            player.score = player.score + 50
        END IF
    END IF
    
    PRINT
NEXT tick

PRINT "═══════════════════════════════════════════════════════"
PRINT "FINAL SCORE: "; player.score
PRINT "═══════════════════════════════════════════════════════"
PRINT

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║        AddFile TEST COMPLETE!                          ║"
PRINT "║                                                        ║"
PRINT "║  ✓ External module loaded successfully                 ║"
PRINT "║  ✓ Classes shared across files                         ║"
PRINT "║  ✓ Object instantiation from external classes          ║"
PRINT "║  ✓ Method calls on multi-file objects                  ║"
PRINT "╚════════════════════════════════════════════════════════╝"
