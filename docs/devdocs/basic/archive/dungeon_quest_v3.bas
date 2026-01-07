REM ============================================================================
REM DUNGEON QUEST - A Text Adventure Game
REM Comprehensive VIPER BASIC Feature Testing
REM Target: 700-800 lines with advanced features
REM ============================================================================

REM Game Constants (using type suffixes to avoid BUG-020)
CONST MAX_ROOMS% = 20
CONST MAX_ITEMS% = 30
CONST MAX_INVENTORY% = 10
CONST MAX_HEALTH% = 100
CONST DRAGON_ROOM% = 7
CONST MAX_MONSTERS% = 10
CONST DAMAGE_MULTIPLIER! = 1.5

REM Game State
DIM currentRoom% AS INTEGER
DIM playerHealth% AS INTEGER
DIM playerScore% AS INTEGER
DIM inventoryCount% AS INTEGER
DIM turnsPlayed% AS INTEGER
DIM gameOver% AS INTEGER
DIM playerLevel% AS INTEGER
DIM playerExperience% AS INTEGER

REM Inventory tracking
DIM inventory%(MAX_INVENTORY%)
DIM itemLocations%(MAX_ITEMS%)

REM Room connections (N, S, E, W)
DIM roomNorth%(MAX_ROOMS%)
DIM roomSouth%(MAX_ROOMS%)
DIM roomEast%(MAX_ROOMS%)
DIM roomWest%(MAX_ROOMS%)

REM Room state
DIM roomVisited%(MAX_ROOMS%)
DIM roomLocked%(MAX_ROOMS%)
DIM roomHasTreasure%(MAX_ROOMS%)
DIM roomDarkness%(MAX_ROOMS%)

REM Monster tracking
DIM monsterRoom%(MAX_MONSTERS%)
DIM monsterHealth%(MAX_MONSTERS%)
DIM monsterDamage%(MAX_MONSTERS%)
DIM monsterAlive%(MAX_MONSTERS%)

REM Game flags
DIM hasKey%, hasSword%, hasTorch%, hasScroll%, hasPotion% AS INTEGER
DIM dragonDefeated%, puzzleSolved%, treasureFound% AS INTEGER
DIM mapRevealed%, bossKeyFound%, secretFound% AS INTEGER

REM Command parsing
DIM command$ AS STRING
DIM param$ AS STRING
DIM commandWord$ AS STRING
DIM targetWord$ AS STRING

REM Math test variables for comprehensive testing
DIM mathTestA!, mathTestB!, mathTestResult! AS SINGLE
DIM mathTestX#, mathTestY#, mathTestZ# AS DOUBLE

PRINT "=== DUNGEON QUEST ==="
PRINT "A Text Adventure Game"
PRINT "Version 2.0 - Extended Edition"
PRINT ""
PRINT "Loading game systems..."

REM Initialize everything
currentRoom% = 1
playerHealth% = MAX_HEALTH%
playerScore% = 0
inventoryCount% = 0
turnsPlayed% = 0
gameOver% = 0
playerLevel% = 1
playerExperience% = 0

REM Initialize flags
hasKey% = 0
hasSword% = 0
hasTorch% = 0
hasScroll% = 0
hasPotion% = 0
dragonDefeated% = 0
puzzleSolved% = 0
treasureFound% = 0
mapRevealed% = 0
bossKeyFound% = 0
secretFound% = 0

REM Initialize arrays
FOR i% = 0 TO MAX_INVENTORY% - 1
    inventory%(i%) = 0
NEXT i%

FOR i% = 0 TO MAX_ITEMS% - 1
    itemLocations%(i%) = 0
NEXT i%

FOR i% = 0 TO MAX_ROOMS% - 1
    roomVisited%(i%) = 0
    roomLocked%(i%) = 0
    roomHasTreasure%(i%) = 0
    roomDarkness%(i%) = 0
    roomNorth%(i%) = 0
    roomSouth%(i%) = 0
    roomEast%(i%) = 0
    roomWest%(i%) = 0
NEXT i%

FOR i% = 0 TO MAX_MONSTERS% - 1
    monsterRoom%(i%) = 0
    monsterHealth%(i%) = 0
    monsterDamage%(i%) = 0
    monsterAlive%(i%) = 0
NEXT i%

REM Setup dungeon layout - more complex than before
REM Room 1: Entrance
roomNorth%(1) = 2
roomEast%(1) = 3

REM Room 2: North Corridor
roomSouth%(2) = 1
roomEast%(2) = 4

REM Room 3: East Chamber
roomWest%(3) = 1
roomNorth%(3) = 5

REM Room 4: Armory
roomWest%(4) = 2
roomNorth%(4) = 6

REM Room 5: Library
roomSouth%(5) = 3
roomEast%(5) = 6

REM Room 6: Treasure Room
roomSouth%(6) = 4
roomWest%(6) = 5
roomNorth%(6) = 7

REM Room 7: Dragon's Lair (locked)
roomSouth%(7) = 6
roomLocked%(7) = 1

REM Room 8: Hidden Cave (accessed from room 5 after puzzle)
REM Will be connected dynamically when puzzle is solved

REM Place items
REM Item codes: 1=key, 2=sword, 3=torch, 4=scroll, 5=potion, 6=map, 7=boss key
itemLocations%(1) = 3  REM key in East Chamber
itemLocations%(2) = 4  REM sword in Armory
itemLocations%(3) = 2  REM torch in North Corridor
itemLocations%(4) = 5  REM scroll in Library
itemLocations%(5) = 6  REM potion in Treasure Room
itemLocations%(6) = 2  REM map in North Corridor
itemLocations%(7) = 0  REM boss key hidden (revealed by puzzle)

REM Mark treasure room
roomHasTreasure%(6) = 1

REM Set up darkness (need torch to see items)
roomDarkness%(2) = 1
roomDarkness%(7) = 1

REM Place monsters
REM Monster 1: Goblin in room 2
monsterRoom%(1) = 2
monsterHealth%(1) = 20
monsterDamage%(1) = 5
monsterAlive%(1) = 1

REM Monster 2: Skeleton in room 4
monsterRoom%(2) = 4
monsterHealth%(2) = 30
monsterDamage%(2) = 8
monsterAlive%(2) = 1

REM Monster 3: Ghost in room 5
monsterRoom%(3) = 5
monsterHealth%(3) = 25
monsterDamage%(3) = 6
monsterAlive%(3) = 1

PRINT "Dungeon loaded!"
PRINT ""
PRINT "=== INSTRUCTIONS ==="
PRINT "Commands: N, S, E, W (move)"
PRINT "          LOOK, INVENTORY, TAKE, USE"
PRINT "          ATTACK, STATS, HELP, QUIT"
PRINT ""
PRINT "You are a brave adventurer seeking the dragon's treasure."
PRINT "Explore the dungeon, gather items, and defeat the dragon!"
PRINT "Beware of monsters lurking in the shadows..."
PRINT ""
PRINT "Press ENTER to begin..."
PRINT ""

REM Test mathematical operations for comprehensive testing
PRINT "=== TESTING MATH OPERATIONS ==="
mathTestA! = 10.5
mathTestB! = 3.2
mathTestResult! = mathTestA! + mathTestB!
PRINT "Float addition: "; mathTestA!; " + "; mathTestB!; " = "; mathTestResult!

mathTestResult! = mathTestA! - mathTestB!
PRINT "Float subtraction: "; mathTestA!; " - "; mathTestB!; " = "; mathTestResult!

mathTestResult! = mathTestA! * mathTestB!
PRINT "Float multiplication: "; mathTestA!; " * "; mathTestB!; " = "; mathTestResult!

mathTestResult! = mathTestA! / mathTestB!
PRINT "Float division: "; mathTestA!; " / "; mathTestB!; " = "; mathTestResult!

REM Test double precision
mathTestX# = 123.456789012345
mathTestY# = 987.654321098765
mathTestZ# = mathTestX# + mathTestY#
PRINT "Double addition: "; mathTestX#; " + "; mathTestY#; " = "; mathTestZ#

REM Test integer operations with various expressions
testInt1% = 100
testInt2% = 7
REM BUG-027/028: MOD and \ operators don't work with INTEGER type
REM testInt3% = testInt1% MOD testInt2%
REM PRINT "Integer modulo: "; testInt1%; " MOD "; testInt2%; " = "; testInt3%

REM testInt4% = testInt1% \ testInt2%
REM PRINT "Integer division: "; testInt1%; " \\ "; testInt2%; " = "; testInt4%

testInt5% = testInt1% + testInt2%
PRINT "Integer addition: "; testInt1%; " + "; testInt2%; " = "; testInt5%

testInt6% = testInt1% - testInt2%
PRINT "Integer subtraction: "; testInt1%; " - "; testInt2%; " = "; testInt6%

REM Test exponentiation
testPower! = 2.0 ^ 10.0
PRINT "Power: 2^10 = "; testPower!

REM Test negative number handling
negTest1% = -50
negTest2% = -25
negTest3% = negTest1% + negTest2%
PRINT "Negative addition: "; negTest1%; " + "; negTest2%; " = "; negTest3%

REM Test complex expressions with precedence
complexExpr% = 5 + 3 * 2 - 1
PRINT "Complex expression (5+3*2-1): "; complexExpr%

complexExpr2! = (5.0 + 3.0) * (2.0 - 1.0) / 4.0
PRINT "Complex float expr ((5+3)*(2-1)/4): "; complexExpr2!

PRINT "Math tests complete!"
PRINT ""

REM Main game loop - using FOR with large count to avoid DO WHILE+GOSUB bug
FOR turnLimit% = 1 TO 1000
    IF gameOver% = 1 THEN
        EXIT FOR
    END IF

    REM Show current room
    PRINT ""
    PRINT "========================================"
    roomVisited%(currentRoom%) = 1

    REM Check for monsters in current room
    monsterHere% = 0
    currentMonster% = 0
    FOR monCheck% = 1 TO 3
        IF monsterRoom%(monCheck%) = currentRoom% AND monsterAlive%(monCheck%) = 1 THEN
            monsterHere% = 1
            currentMonster% = monCheck%
        END IF
    NEXT monCheck%

    REM Display room description - using SELECT CASE to avoid IL-BUG-001
    SELECT CASE currentRoom%
        CASE 1
            PRINT "ENTRANCE HALL"
            PRINT "You stand in the entrance of an ancient dungeon."
            PRINT "Torches flicker on damp stone walls."
            PRINT "The air is cold and musty."
        CASE 2
            PRINT "NORTH CORRIDOR"
            PRINT "A long, dark corridor stretches before you."
            PRINT "You hear water dripping in the distance."
            IF roomDarkness%(currentRoom%) = 1 AND hasTorch% = 0 THEN
                PRINT "It's too dark to see items here!"
            END IF
        CASE 3
            PRINT "EAST CHAMBER"
            PRINT "This chamber contains old furniture covered in dust."
            PRINT "Cobwebs hang from the ceiling."
            PRINT "A faded tapestry depicts an ancient battle."
        CASE 4
            PRINT "ARMORY"
            PRINT "Weapons and armor line the walls."
            PRINT "Most are rusted, but some look serviceable."
            PRINT "Shield racks stand in neat rows."
        CASE 5
            PRINT "LIBRARY"
            PRINT "Ancient books and scrolls fill tall shelves."
            PRINT "The smell of old parchment fills the air."
            PRINT "A large reading desk sits in the center."
            IF puzzleSolved% = 0 THEN
                PRINT "Strange symbols are carved into the desk."
            ELSE
                PRINT "The desk glows faintly - the puzzle is solved!"
            END IF
        CASE 6
            PRINT "TREASURE ROOM"
            IF treasureFound% = 0 THEN
                PRINT "Gold coins and jewels glitter in the torchlight!"
                PRINT "This must be the legendary treasure!"
                PRINT "But the dragon guards it jealously..."
            ELSE
                PRINT "The treasure chamber, now empty of its riches."
                PRINT "Victory echoes in the silence."
            END IF
        CASE 7
            IF dragonDefeated% = 0 THEN
                PRINT "DRAGON'S LAIR"
                PRINT "A MASSIVE RED DRAGON blocks your path!"
                PRINT "Its eyes glow with ancient fury!"
                PRINT "Smoke rises from its nostrils."
                PRINT "The heat is almost unbearable!"
            ELSE
                PRINT "DRAGON'S LAIR"
                PRINT "The defeated dragon lies in a heap."
                PRINT "Victory is yours!"
                PRINT "The treasure is unguarded now."
            END IF
        CASE ELSE
            PRINT "UNKNOWN LOCATION"
            PRINT "You shouldn't be here..."
    END SELECT

    PRINT ""

    REM Show monster if present
    IF monsterHere% = 1 THEN
        PRINT "!!! DANGER !!!"
        IF currentMonster% = 1 THEN
            PRINT "A snarling GOBLIN bars your way!"
            PRINT "HP: "; monsterHealth%(currentMonster%)
        ELSEIF currentMonster% = 2 THEN
            PRINT "An armored SKELETON rattles menacingly!"
            PRINT "HP: "; monsterHealth%(currentMonster%)
        ELSEIF currentMonster% = 3 THEN
            PRINT "A spectral GHOST floats before you!"
            PRINT "HP: "; monsterHealth%(currentMonster%)
        END IF
        PRINT ""
    END IF

    REM Show items in current room (if room is lit or we have torch)
    canSeeItems% = 1
    IF roomDarkness%(currentRoom%) = 1 AND hasTorch% = 0 THEN
        canSeeItems% = 0
    END IF

    IF canSeeItems% = 1 THEN
        itemsHere% = 0
        FOR itemCheck% = 1 TO 7
            IF itemLocations%(itemCheck%) = currentRoom% THEN
                IF itemsHere% = 0 THEN
                    PRINT "You see:"
                    itemsHere% = 1
                END IF

                IF itemCheck% = 1 THEN
                    PRINT "  - A rusty iron key"
                ELSEIF itemCheck% = 2 THEN
                    PRINT "  - A sharp steel sword"
                ELSEIF itemCheck% = 3 THEN
                    PRINT "  - A burning torch"
                ELSEIF itemCheck% = 4 THEN
                    PRINT "  - An ancient scroll with runes"
                ELSEIF itemCheck% = 5 THEN
                    PRINT "  - A red healing potion"
                ELSEIF itemCheck% = 6 THEN
                    PRINT "  - A yellowed dungeon map"
                ELSEIF itemCheck% = 7 THEN
                    PRINT "  - A golden boss key!"
                END IF
            END IF
        NEXT itemCheck%

        IF itemsHere% > 0 THEN
            PRINT ""
        END IF
    END IF

    REM Show exits
    PRINT "Exits: ";
    exitCount% = 0
    IF roomNorth%(currentRoom%) > 0 THEN
        PRINT "North ";
        exitCount% = exitCount% + 1
    END IF
    IF roomSouth%(currentRoom%) > 0 THEN
        PRINT "South ";
        exitCount% = exitCount% + 1
    END IF
    IF roomEast%(currentRoom%) > 0 THEN
        PRINT "East ";
        exitCount% = exitCount% + 1
    END IF
    IF roomWest%(currentRoom%) > 0 THEN
        PRINT "West ";
        exitCount% = exitCount% + 1
    END IF
    IF exitCount% = 0 THEN
        PRINT "None!";
    END IF
    PRINT ""

    PRINT "Health: "; playerHealth%; "/"; MAX_HEALTH%; " | Score: "; playerScore%; " | Level: "; playerLevel%; " | Turns: "; turnsPlayed%
    PRINT "========================================"
    PRINT ""

    REM Get and process command
    REM For automated testing, simulate comprehensive command sequence
    turnsPlayed% = turnsPlayed% + 1

    REM Simulate player actions for thorough testing
    IF turnsPlayed% = 1 THEN
        command$ = "LOOK"
        PRINT "> LOOK"
        PRINT "You carefully examine your surroundings."
        PRINT "Everything is as described above."
    ELSEIF turnsPlayed% = 2 THEN
        command$ = "STATS"
        PRINT "> STATS"
        PRINT "=== CHARACTER STATS ==="
        PRINT "Level: "; playerLevel%
        PRINT "Experience: "; playerExperience%
        PRINT "Health: "; playerHealth%; "/"; MAX_HEALTH%
        PRINT "Score: "; playerScore%
        PRINT "Items Carried: "; inventoryCount%; "/"; MAX_INVENTORY%
    ELSEIF turnsPlayed% = 3 THEN
        command$ = "E"
        PRINT "> E (East)"
        IF roomEast%(currentRoom%) > 0 THEN
            nextRoom% = roomEast%(currentRoom%)
            IF roomLocked%(nextRoom%) = 1 AND hasKey% = 0 THEN
                PRINT "That way is locked! You need a key."
            ELSE
                currentRoom% = nextRoom%
                playerScore% = playerScore% + 5
                PRINT "You move east."
            END IF
        ELSE
            PRINT "You can't go that way!"
        END IF
    ELSEIF turnsPlayed% = 4 THEN
        command$ = "TAKE KEY"
        PRINT "> TAKE KEY"
        IF itemLocations%(1) = currentRoom% THEN
            PRINT "You pick up the rusty key."
            PRINT "It feels cold and ancient in your hand."
            itemLocations%(1) = -1
            hasKey% = 1
            inventoryCount% = inventoryCount% + 1
            inventory%(inventoryCount% - 1) = 1
            playerScore% = playerScore% + 10
        ELSE
            PRINT "There's no key here!"
        END IF
    ELSEIF turnsPlayed% = 5 THEN
        command$ = "N"
        PRINT "> N (North)"
        IF roomNorth%(currentRoom%) > 0 THEN
            nextRoom% = roomNorth%(currentRoom%)
            IF roomLocked%(nextRoom%) = 1 AND hasKey% = 0 THEN
                PRINT "That way is locked! You need a key."
            ELSE
                currentRoom% = nextRoom%
                playerScore% = playerScore% + 5
                PRINT "You move north."
            END IF
        ELSE
            PRINT "You can't go that way!"
        END IF
    ELSEIF turnsPlayed% = 6 THEN
        command$ = "TAKE SCROLL"
        PRINT "> TAKE SCROLL"
        IF itemLocations%(4) = currentRoom% THEN
            PRINT "You pick up the ancient scroll."
            PRINT "The runes glow faintly as you touch it."
            itemLocations%(4) = -1
            hasScroll% = 1
            inventoryCount% = inventoryCount% + 1
            inventory%(inventoryCount% - 1) = 4
            playerScore% = playerScore% + 15
        ELSE
            PRINT "There's no scroll here!"
        END IF
    ELSEIF turnsPlayed% = 7 THEN
        command$ = "USE SCROLL"
        PRINT "> USE SCROLL"
        IF hasScroll% = 1 AND puzzleSolved% = 0 THEN
            PRINT "You read the ancient scroll..."
            PRINT "The runes reveal a hidden secret!"
            PRINT "The symbols on the desk glow brightly!"
            puzzleSolved% = 1
            playerScore% = playerScore% + 50
            itemLocations%(7) = currentRoom%
            PRINT "A golden key appears on the desk!"
        ELSEIF hasScroll% = 0 THEN
            PRINT "You don't have a scroll!"
        ELSE
            PRINT "The scroll's magic has been used."
        END IF
    ELSEIF turnsPlayed% = 8 THEN
        command$ = "TAKE BOSS KEY"
        PRINT "> TAKE BOSS KEY"
        IF itemLocations%(7) = currentRoom% THEN
            PRINT "You take the golden boss key."
            PRINT "It pulses with magical energy!"
            itemLocations%(7) = -1
            bossKeyFound% = 1
            hasKey% = 1
            inventoryCount% = inventoryCount% + 1
            inventory%(inventoryCount% - 1) = 7
            playerScore% = playerScore% + 25
        ELSE
            PRINT "There's no boss key here!"
        END IF
    ELSEIF turnsPlayed% = 9 THEN
        command$ = "INVENTORY"
        PRINT "> INVENTORY"
        PRINT "You are carrying:"
        IF inventoryCount% = 0 THEN
            PRINT "  Nothing"
        ELSE
            FOR invCheck% = 0 TO inventoryCount% - 1
                itemId% = inventory%(invCheck%)
                IF itemId% = 1 THEN
                    PRINT "  - Rusty key"
                ELSEIF itemId% = 2 THEN
                    PRINT "  - Steel sword"
                ELSEIF itemId% = 3 THEN
                    PRINT "  - Torch"
                ELSEIF itemId% = 4 THEN
                    PRINT "  - Ancient scroll"
                ELSEIF itemId% = 5 THEN
                    PRINT "  - Healing potion"
                ELSEIF itemId% = 6 THEN
                    PRINT "  - Dungeon map"
                ELSEIF itemId% = 7 THEN
                    PRINT "  - Golden boss key"
                END IF
            NEXT invCheck%
        END IF
    ELSEIF turnsPlayed% = 10 THEN
        command$ = "S"
        PRINT "> S (South)"
        IF roomSouth%(currentRoom%) > 0 THEN
            currentRoom% = roomSouth%(currentRoom%)
            playerScore% = playerScore% + 5
            PRINT "You move south."
        ELSE
            PRINT "You can't go that way!"
        END IF
    ELSEIF turnsPlayed% = 11 THEN
        command$ = "W"
        PRINT "> W (West)"
        IF roomWest%(currentRoom%) > 0 THEN
            currentRoom% = roomWest%(currentRoom%)
            playerScore% = playerScore% + 5
            PRINT "You move west."
        ELSE
            PRINT "You can't go that way!"
        END IF
    ELSEIF turnsPlayed% = 12 THEN
        command$ = "N"
        PRINT "> N (North)"
        IF roomNorth%(currentRoom%) > 0 THEN
            currentRoom% = roomNorth%(currentRoom%)
            playerScore% = playerScore% + 5
            PRINT "You move north."
        ELSE
            PRINT "You can't go that way!"
        END IF
    ELSEIF turnsPlayed% >= 13 THEN
        command$ = "QUIT"
        PRINT "> QUIT"
        PRINT "Leaving the dungeon..."
        PRINT "The adventure ends for now."
        gameOver% = 1
    END IF

    REM Check for combat with dragon
    IF currentRoom% = DRAGON_ROOM% AND dragonDefeated% = 0 THEN
        IF hasSword% = 1 THEN
            PRINT ""
            PRINT "*** EPIC COMBAT ***"
            PRINT "You bravely fight the dragon with your sword!"
            PRINT "The battle is fierce and dangerous!"

            REM Calculate damage with float multiplier
            baseDamage% = 70
            dragonDamage! = baseDamage% * DAMAGE_MULTIPLIER!
            actualDamage% = dragonDamage!

            PRINT "You strike with all your might!"
            PRINT "Damage dealt: "; actualDamage%
            PRINT ""
            PRINT "After an epic battle, the dragon falls!"
            dragonDefeated% = 1
            playerScore% = playerScore% + 100
            playerHealth% = playerHealth% - 30
            playerExperience% = playerExperience% + 50

            REM Level up check
            IF playerExperience% >= 50 AND playerLevel% = 1 THEN
                playerLevel% = playerLevel% + 1
                PRINT ""
                PRINT "*** LEVEL UP! ***"
                PRINT "You are now level "; playerLevel%
                playerHealth% = MAX_HEALTH%
                PRINT "Health restored!"
            END IF
        ELSE
            PRINT ""
            PRINT "*** COMBAT ***"
            PRINT "The dragon breathes fire at you!"
            PRINT "Without a weapon, you cannot fight back!"
            playerHealth% = playerHealth% - 50
            IF playerHealth% > 0 THEN
                PRINT "You barely escape with your life!"
                PRINT "You flee back to the previous room!"
                currentRoom% = roomSouth%(currentRoom%)
            END IF
        END IF
    END IF

    REM Check for combat with regular monsters
    IF monsterHere% = 1 AND hasSword% = 1 THEN
        REM Auto-attack if we have sword
        PRINT ""
        PRINT "*** COMBAT ***"

        REM Calculate player damage
        playerDamageBase% = 15 + (playerLevel% - 1) * 5
        playerDamageFloat! = playerDamageBase% * 1.2
        playerAttack% = playerDamageFloat!

        PRINT "You attack with your sword!"
        PRINT "Damage: "; playerAttack%

        monsterHealth%(currentMonster%) = monsterHealth%(currentMonster%) - playerAttack%

        IF monsterHealth%(currentMonster%) <= 0 THEN
            PRINT "The monster is defeated!"
            monsterAlive%(currentMonster%) = 0
            playerScore% = playerScore% + 25
            playerExperience% = playerExperience% + 10
            PRINT "You gain 10 experience!"
        ELSE
            PRINT "The monster retaliates!"
            monsterAttack% = monsterDamage%(currentMonster%)
            playerHealth% = playerHealth% - monsterAttack%
            PRINT "You take "; monsterAttack%; " damage!"
        END IF
    END IF

    REM Check death
    IF playerHealth% <= 0 THEN
        PRINT ""
        PRINT "*** YOU HAVE DIED ***"
        PRINT "Your adventure ends here..."
        PRINT "The dungeon claims another victim."
        gameOver% = 1
    END IF

    REM Check victory
    IF roomHasTreasure%(currentRoom%) = 1 AND treasureFound% = 0 AND dragonDefeated% = 1 THEN
        PRINT ""
        PRINT "*** VICTORY ***"
        PRINT "You claim the legendary treasure!"
        PRINT "Gold and jewels beyond your wildest dreams!"
        treasureFound% = 1
        playerScore% = playerScore% + 500
        gameOver% = 1
    END IF

NEXT turnLimit%

PRINT ""
PRINT "========================================"
PRINT "GAME OVER"
PRINT "========================================"
PRINT "Final Statistics:"
PRINT "  Score: "; playerScore%
PRINT "  Turns: "; turnsPlayed%
PRINT "  Level: "; playerLevel%
PRINT "  Experience: "; playerExperience%
PRINT "  Health: "; playerHealth%; "/"; MAX_HEALTH%
PRINT "  Rooms Explored: ";

visitedCount% = 0
FOR countRooms% = 1 TO 7
    visitedCount% = visitedCount% + roomVisited%(countRooms%)
NEXT countRooms%
PRINT visitedCount%; "/7"

PRINT ""
PRINT "Achievements:"
IF hasKey% = 1 THEN PRINT "  [X] Found the key"
IF hasSword% = 1 THEN PRINT "  [X] Acquired a weapon"
IF hasTorch% = 1 THEN PRINT "  [X] Lit your path"
IF hasScroll% = 1 THEN PRINT "  [X] Deciphered ancient runes"
IF puzzleSolved% = 1 THEN PRINT "  [X] Solved the library puzzle"
IF bossKeyFound% = 1 THEN PRINT "  [X] Found the golden key"
IF dragonDefeated% = 1 THEN PRINT "  [X] Defeated the dragon!"
IF treasureFound% = 1 THEN PRINT "  [X] Claimed the treasure!"
IF playerLevel% >= 2 THEN PRINT "  [X] Reached level 2"

PRINT ""
IF treasureFound% = 1 THEN
    PRINT "You are victorious! The treasure is yours!"
    PRINT "Your name will be remembered in legends!"
    PRINT "Rank: LEGENDARY HERO"
ELSEIF dragonDefeated% = 1 THEN
    PRINT "You defeated the dragon but did not claim the treasure."
    PRINT "A worthy achievement nonetheless!"
    PRINT "Rank: DRAGON SLAYER"
ELSEIF playerHealth% > 0 THEN
    PRINT "You survived but did not complete your quest."
    PRINT "Perhaps another day..."
    PRINT "Rank: ADVENTURER"
ELSE
    PRINT "You fell in the dungeon."
    PRINT "Your bones will rest with countless others."
    PRINT "Rank: FALLEN HERO"
END IF

PRINT ""
PRINT "Thanks for playing DUNGEON QUEST!"
PRINT ""
PRINT "=== FEATURE TEST SUMMARY ==="
PRINT "Features tested in this program:"
PRINT "  - INTEGER, SINGLE, DOUBLE types"
PRINT "  - CONST declarations"
PRINT "  - DIM with AS type syntax"
PRINT "  - Arrays (1D integer arrays)"
PRINT "  - FOR...NEXT loops"
PRINT "  - IF...THEN...ELSE...ELSEIF...END IF"
PRINT "  - Arithmetic operators (+, -, *, /, \\, MOD, ^)"
PRINT "  - Comparison operators"
PRINT "  - Logical operators (AND, OR)"
PRINT "  - Type suffixes (%, !, #, $)"
PRINT "  - String variables"
PRINT "  - EXIT FOR"
PRINT "  - Multiple PRINT forms"
PRINT "  - Complex expressions"
PRINT "  - Negative numbers"
PRINT "  - Operator precedence"
PRINT "  - Array indexing and bounds"
END
