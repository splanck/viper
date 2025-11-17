REM ╔════════════════════════════════════════════════════════╗
REM ║     SIMPLE TEXT ADVENTURE GAME                         ║
REM ╚════════════════════════════════════════════════════════╝
REM
REM Testing: Multiple OOP classes, game loop, state management
REM Workarounds: BUG-067 (no arrays), BUG-072 (avoid SELECT CASE)

CLASS Player
    health AS INTEGER
    hasKey AS INTEGER
    hasSword AS INTEGER
    currentRoom AS INTEGER

    SUB Init()
        ME.health = 100
        ME.hasKey = 0
        ME.hasSword = 0
        ME.currentRoom = 1
    END SUB

    SUB TakeDamage(amount AS INTEGER)
        ME.health = ME.health - amount
        IF ME.health < 0 THEN
            ME.health = 0
        END IF
        COLOR 12, 0
        PRINT "You take "; amount; " damage! Health: "; ME.health
        COLOR 15, 0
    END SUB

    FUNCTION IsAlive() AS INTEGER
        IF ME.health > 0 THEN
            RETURN 1
        END IF
        RETURN 0
    END FUNCTION

    SUB ShowStatus()
        COLOR 11, 0
        PRINT "─── Status ───"
        COLOR 15, 0
        PRINT "Health: "; ME.health
        PRINT "Items: ";
        IF ME.hasKey THEN
            PRINT "Key ";
        END IF
        IF ME.hasSword THEN
            PRINT "Sword";
        END IF
        PRINT
    END SUB
END CLASS

CLASS Monster
    name AS STRING
    health AS INTEGER
    damage AS INTEGER
    isAlive AS INTEGER

    SUB Init(monsterName AS STRING, hp AS INTEGER, dmg AS INTEGER)
        ME.name = monsterName
        ME.health = hp
        ME.damage = dmg
        ME.isAlive = 1
    END SUB

    SUB TakeDamage(amount AS INTEGER)
        ME.health = ME.health - amount
        IF ME.health <= 0 THEN
            ME.health = 0
            ME.isAlive = 0
            COLOR 10, 0
            PRINT ME.name; " is defeated!"
            COLOR 15, 0
        ELSE
            COLOR 12, 0
            PRINT ME.name; " takes "; amount; " damage! Health: "; ME.health
            COLOR 15, 0
        END IF
    END SUB

    SUB Attack(target AS Player)
        IF ME.isAlive THEN
            COLOR 12, 0
            PRINT ME.name; " attacks!"
            COLOR 15, 0
            target.TakeDamage(ME.damage)
        END IF
    END SUB
END CLASS

REM ═══════════════════════════════════════════════════════
REM                    MAIN GAME
REM ═══════════════════════════════════════════════════════

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         THE DUNGEON OF DESTINY                         ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

DIM hero AS Player
hero = NEW Player()
hero.Init()

DIM goblin AS Monster
goblin = NEW Monster()
goblin.Init("Goblin", 30, 15)

DIM dragon AS Monster
dragon = NEW Monster()
dragon.Init("Dragon", 50, 25)

REM Game variables
DIM turn AS INTEGER
DIM won AS INTEGER

turn = 1
won = 0

WHILE hero.IsAlive() AND turn <= 15 AND won = 0
    COLOR 14, 0
    PRINT "═══ Turn "; turn; " ═══"
    COLOR 15, 0

    REM Describe current situation
    IF hero.currentRoom = 1 THEN
        PRINT "You are in the Entrance Hall."
        PRINT "A goblin guards the eastern door."
        PRINT

        IF goblin.isAlive THEN
            PRINT "Actions: (a)ttack goblin, (s)earch room"
        ELSE
            PRINT "The goblin lies defeated."
            PRINT "Actions: (e)ast to treasure room, (s)earch room"
        END IF

        REM Simulate player action (auto-play for demo)
        DIM action AS STRING
        IF turn = 1 THEN
            action = "s"
        ELSEIF turn = 2 THEN
            action = "a"
        ELSEIF turn = 3 THEN
            action = "a"
        ELSEIF turn = 4 THEN
            action = "e"
        ELSE
            action = "e"
        END IF

        PRINT "> "; action
        PRINT

        REM Process action using IF/THEN/ELSE
        IF action = "s" THEN
            IF hero.hasSword = 0 THEN
                COLOR 10, 0
                PRINT "You found a rusty sword!"
                COLOR 15, 0
                hero.hasSword = 1
            ELSE
                PRINT "Nothing new here."
            END IF
        ELSEIF action = "a" THEN
            IF goblin.isAlive THEN
                IF hero.hasSword THEN
                    PRINT "You swing your sword!"
                    goblin.TakeDamage(20)
                ELSE
                    PRINT "You punch the goblin!"
                    goblin.TakeDamage(5)
                END IF
                REM Goblin counter-attacks
                goblin.Attack(hero)
            ELSE
                PRINT "The goblin is already defeated."
            END IF
        ELSEIF action = "e" THEN
            IF goblin.isAlive THEN
                PRINT "The goblin blocks your way!"
            ELSE
                PRINT "You move east..."
                hero.currentRoom = 2
            END IF
        END IF

    ELSEIF hero.currentRoom = 2 THEN
        PRINT "You are in the Treasure Room."
        PRINT "A fearsome dragon guards a golden chest!"
        PRINT

        IF dragon.isAlive THEN
            PRINT "Actions: (a)ttack dragon, (f)lee west"
        ELSE
            PRINT "The dragon is defeated!"
            PRINT "Actions: (o)pen chest"
        END IF

        REM Auto-play
        IF turn = 5 OR turn = 6 OR turn = 7 THEN
            action = "a"
        ELSEIF turn = 8 THEN
            action = "o"
        ELSE
            action = "o"
        END IF

        PRINT "> "; action
        PRINT

        IF action = "a" THEN
            IF dragon.isAlive THEN
                IF hero.hasSword THEN
                    PRINT "You strike the dragon with your sword!"
                    dragon.TakeDamage(20)
                ELSE
                    PRINT "You attack bare-handed!"
                    dragon.TakeDamage(10)
                END IF
                REM Dragon counter-attacks
                dragon.Attack(hero)
            ELSE
                PRINT "The dragon is already defeated."
            END IF
        ELSEIF action = "o" THEN
            IF dragon.isAlive THEN
                PRINT "The dragon breathes fire at you!"
                hero.TakeDamage(30)
            ELSE
                COLOR 10, 0
                PRINT "You open the chest and find the Crown of Victory!"
                PRINT "✓ YOU WIN!"
                COLOR 15, 0
                won = 1
            END IF
        ELSEIF action = "f" THEN
            PRINT "You flee back to the entrance..."
            hero.currentRoom = 1
        END IF
    END IF

    PRINT
    hero.ShowStatus()
    PRINT

    turn = turn + 1
WEND

REM Game over
PRINT "╔════════════════════════════════════════════════════════╗"
IF won THEN
    COLOR 10, 0
    PRINT "║         VICTORY! YOU WON THE GAME!                    ║"
ELSEIF hero.IsAlive() = 0 THEN
    COLOR 12, 0
    PRINT "║         GAME OVER - YOU DIED!                         ║"
ELSE
    COLOR 14, 0
    PRINT "║         DEMO ENDED                                    ║"
END IF
COLOR 15, 0
PRINT "╠════════════════════════════════════════════════════════╣"
PRINT "║  Final Health: "; hero.health
PRINT "║  Turns Taken: "; turn - 1
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║  ADVENTURE GAME STRESS TEST COMPLETE!                  ║"
PRINT "║                                                        ║"
PRINT "║  ✓ Multiple OOP classes (Player, Monster)             ║"
PRINT "║  ✓ Game loop with complex state                       ║"
PRINT "║  ✓ String comparisons and conditionals                ║"
PRINT "║  ✓ Inter-object communication                          ║"
PRINT "║  ✓ Color/ANSI graphics                                 ║"
PRINT "╚════════════════════════════════════════════════════════╝"
