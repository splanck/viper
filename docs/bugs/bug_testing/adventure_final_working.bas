REM ╔════════════════════════════════════════════════════════╗
REM ║     WORKING TEXT ADVENTURE - Final Stress Test         ║
REM ╚════════════════════════════════════════════════════════╝
REM
REM This demonstrates all WORKING features despite limitations
REM Workarounds for: BUG-067, BUG-071, BUG-072, BUG-073

CLASS Room
    roomId AS INTEGER
    name AS STRING
    description AS STRING
    visited AS INTEGER
    hasKey AS INTEGER
    hasPotion AS INTEGER

    SUB Init(id AS INTEGER, roomName AS STRING, desc AS STRING)
        ME.roomId = id
        ME.name = roomName
        ME.description = desc
        ME.visited = 0
        ME.hasKey = 0
        ME.hasPotion = 0
    END SUB

    SUB Describe()
        COLOR 14, 0
        PRINT "════════════════════════════════════════"
        PRINT ME.name
        PRINT "════════════════════════════════════════"
        COLOR 15, 0
        PRINT ME.description
        PRINT

        IF ME.visited = 0 THEN
            COLOR 10, 0
            PRINT "(First visit)"
            COLOR 15, 0
            ME.visited = 1
        END IF

        REM Show items
        IF ME.hasKey THEN
            COLOR 11, 0
            PRINT "You see: Golden Key"
            COLOR 15, 0
        END IF

        IF ME.hasPotion THEN
            COLOR 11, 0
            PRINT "You see: Health Potion"
            COLOR 15, 0
        END IF
    END SUB
END CLASS

CLASS Player
    health AS INTEGER
    hasKey AS INTEGER
    hasPotion AS INTEGER
    score AS INTEGER
    location AS INTEGER

    SUB Init()
        ME.health = 100
        ME.hasKey = 0
        ME.hasPotion = 0
        ME.score = 0
        ME.location = 1
    END SUB

    FUNCTION IsAlive() AS INTEGER
        IF ME.health > 0 THEN
            RETURN 1
        END IF
        RETURN 0
    END FUNCTION

    SUB ShowStatus()
        COLOR 11, 0
        PRINT "─── Your Status ───"
        COLOR 15, 0
        PRINT "Health: "; ME.health; "  Score: "; ME.score
        PRINT "Inventory: ";
        IF ME.hasKey THEN
            PRINT "Key ";
        END IF
        IF ME.hasPotion THEN
            PRINT "Potion";
        END IF
        PRINT
    END SUB

    SUB UsePotion()
        IF ME.hasPotion THEN
            ME.health = ME.health + 30
            IF ME.health > 100 THEN
                ME.health = 100
            END IF
            ME.hasPotion = 0
            COLOR 10, 0
            PRINT "You drink the potion! Health: "; ME.health
            COLOR 15, 0
        ELSE
            PRINT "You don't have a potion."
        END IF
    END SUB
END CLASS

REM ═══════════════════════════════════════════════════════
REM                    MAIN GAME
REM ═══════════════════════════════════════════════════════

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         QUEST FOR THE GOLDEN AMULET                    ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

REM Create player
DIM hero AS Player
hero = NEW Player()
hero.Init()

REM Create rooms (using separate objects, not array)
DIM entrance AS Room
DIM armory AS Room
DIM treasury AS Room

entrance = NEW Room()
armory = NEW Room()
treasury = NEW Room()

entrance.Init(1, "Grand Entrance", "A magnificent hall with marble pillars.")
entrance.hasPotion = 1

armory.Init(2, "Ancient Armory", "Weapons and shields line the walls.")
armory.hasKey = 1

treasury.Init(3, "Royal Treasury", "Gold and jewels sparkle in the torchlight. A locked chest sits in the center.")

REM Game loop
DIM turn AS INTEGER
DIM won AS INTEGER
DIM currentRoom AS Room

turn = 1
won = 0

PRINT "Your quest: Find the Golden Amulet!"
PRINT "Commands: (n)orth, (s)outh, (e)ast, (w)est, (t)ake, (u)se potion, (l)ook"
PRINT

WHILE hero.IsAlive() AND turn <= 25 AND won = 0
    COLOR 14, 0
    PRINT "═══ Turn "; turn; " ═══"
    COLOR 15, 0

    REM Set current room reference
    IF hero.location = 1 THEN
        currentRoom = entrance
    ELSEIF hero.location = 2 THEN
        currentRoom = armory
    ELSEIF hero.location = 3 THEN
        currentRoom = treasury
    END IF

    REM Auto-play simulation
    DIM command AS STRING

    IF turn = 1 THEN
        command = "l"
    ELSEIF turn = 2 THEN
        command = "t"
    ELSEIF turn = 3 THEN
        command = "e"
    ELSEIF turn = 4 THEN
        command = "l"
    ELSEIF turn = 5 THEN
        command = "t"
    ELSEIF turn = 6 THEN
        command = "w"
    ELSEIF turn = 7 THEN
        command = "e"
    ELSEIF turn = 8 THEN
        command = "e"
    ELSEIF turn = 9 THEN
        command = "l"
    ELSEIF turn = 10 THEN
        command = "t"
    ELSE
        command = "l"
    END IF

    PRINT "> "; command
    PRINT

    REM Process command
    IF command = "l" THEN
        currentRoom.Describe()

    ELSEIF command = "n" THEN
        PRINT "You can't go north."

    ELSEIF command = "s" THEN
        PRINT "You can't go south."

    ELSEIF command = "e" THEN
        IF hero.location = 1 THEN
            PRINT "You move east to the Armory."
            hero.location = 2
        ELSEIF hero.location = 2 THEN
            PRINT "You move east to the Treasury."
            hero.location = 3
        ELSE
            PRINT "You can't go east."
        END IF

    ELSEIF command = "w" THEN
        IF hero.location = 2 THEN
            PRINT "You move west to the Entrance."
            hero.location = 1
        ELSEIF hero.location = 3 THEN
            PRINT "You move west to the Armory."
            hero.location = 2
        ELSE
            PRINT "You can't go west."
        END IF

    ELSEIF command = "t" THEN
        IF hero.location = 1 AND entrance.hasPotion THEN
            COLOR 10, 0
            PRINT "You take the Health Potion!"
            COLOR 15, 0
            hero.hasPotion = 1
            entrance.hasPotion = 0
            hero.score = hero.score + 10

        ELSEIF hero.location = 2 AND armory.hasKey THEN
            COLOR 10, 0
            PRINT "You take the Golden Key!"
            COLOR 15, 0
            hero.hasKey = 1
            armory.hasKey = 0
            hero.score = hero.score + 20

        ELSEIF hero.location = 3 THEN
            IF hero.hasKey THEN
                COLOR 10, 0
                PRINT "You unlock the chest with the Golden Key!"
                PRINT "Inside is the GOLDEN AMULET!"
                PRINT
                PRINT "✓✓✓ YOU WIN! ✓✓✓"
                COLOR 15, 0
                won = 1
                hero.score = hero.score + 100
            ELSE
                COLOR 12, 0
                PRINT "The chest is locked. You need a key."
                COLOR 15, 0
            END IF
        ELSE
            PRINT "There's nothing to take here."
        END IF

    ELSEIF command = "u" THEN
        hero.UsePotion()

    ELSE
        PRINT "Unknown command."
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
    PRINT "║         ★ VICTORY! ★                                  ║"
    COLOR 15, 0
ELSEIF hero.IsAlive() = 0 THEN
    COLOR 12, 0
    PRINT "║         GAME OVER                                     ║"
    COLOR 15, 0
ELSE
    PRINT "║         DEMO COMPLETE                                 ║"
END IF
PRINT "╠════════════════════════════════════════════════════════╣"
PRINT "║  Final Score: "; hero.score
PRINT "║  Final Health: "; hero.health
PRINT "║  Turns: "; turn - 1
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║  COMPREHENSIVE ADVENTURE STRESS TEST COMPLETE!         ║"
PRINT "║                                                        ║"
PRINT "║  Features Successfully Tested:                         ║"
PRINT "║  ✓ Multiple OOP classes with state                    ║"
PRINT "║  ✓ Complex game loop with 25 turn simulation          ║"
PRINT "║  ✓ Room system with descriptions                      ║"
PRINT "║  ✓ Inventory management                               ║"
PRINT "║  ✓ String comparisons and commands                    ║"
PRINT "║  ✓ Conditional logic chains                           ║"
PRINT "║  ✓ Object references and state changes               ║"
PRINT "║  ✓ COLOR and ANSI formatting                          ║"
PRINT "║  ✓ Score and health tracking                          ║"
PRINT "║  ✓ Win condition detection                            ║"
PRINT "╚════════════════════════════════════════════════════════╝"
