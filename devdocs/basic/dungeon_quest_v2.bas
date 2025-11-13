REM ============================================================================
REM DUNGEON QUEST - A Text Adventure Game  
REM Comprehensive VIPER BASIC Feature Testing
REM Target: 500-800 lines
REM ============================================================================

REM Game Constants (using type suffixes to avoid BUG-020)
CONST MAX_ROOMS% = 20
CONST MAX_ITEMS% = 30
CONST MAX_INVENTORY% = 10
CONST MAX_HEALTH% = 100
CONST DRAGON_ROOM% = 7

REM Game State
DIM currentRoom% AS INTEGER
DIM playerHealth% AS INTEGER
DIM playerScore% AS INTEGER
DIM inventoryCount% AS INTEGER
DIM turnsPlayed% AS INTEGER
DIM gameOver% AS INTEGER

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

REM Game flags
DIM hasKey%, hasSword%, hasTorch%, hasScroll%, hasPotion% AS INTEGER
DIM dragonDefeated%, puzzleSolved%, treasureFound% AS INTEGER

REM Command parsing
DIM command$ AS STRING
DIM param$ AS STRING

PRINT "=== DUNGEON QUEST ==="
PRINT "A Text Adventure Game"
PRINT "Version 1.0"
PRINT ""
PRINT "Loading..."

REM Initialize everything
currentRoom% = 1
playerHealth% = MAX_HEALTH%
playerScore% = 0
inventoryCount% = 0
turnsPlayed% = 0
gameOver% = 0

REM Initialize flags
hasKey% = 0
hasSword% = 0
hasTorch% = 0
hasScroll% = 0
hasPotion% = 0
dragonDefeated% = 0
puzzleSolved% = 0
treasureFound% = 0

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
    roomNorth%(i%) = 0
    roomSouth%(i%) = 0
    roomEast%(i%) = 0
    roomWest%(i%) = 0
NEXT i%

REM Setup dungeon layout
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

REM Place items
REM Item codes: 1=key, 2=sword, 3=torch, 4=scroll, 5=potion
itemLocations%(1) = 3
itemLocations%(2) = 4
itemLocations%(3) = 2
itemLocations%(4) = 5
itemLocations%(5) = 6

REM Mark treasure room
roomHasTreasure%(6) = 1

PRINT "Dungeon loaded!"
PRINT ""
PRINT "=== INSTRUCTIONS ==="
PRINT "Commands: N, S, E, W (move)"
PRINT "          LOOK, INVENTORY, TAKE, USE"
PRINT "          HELP, SCORE, QUIT"
PRINT ""
PRINT "You are a brave adventurer seeking the dragon's treasure."
PRINT "Explore the dungeon, gather items, and defeat the dragon!"
PRINT ""
PRINT "Press ENTER to begin..."
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
    
    REM Display room description
    IF currentRoom% = 1 THEN
        PRINT "ENTRANCE HALL"
        PRINT "You stand in the entrance of an ancient dungeon."
        PRINT "Torches flicker on damp stone walls."
    ELSEIF currentRoom% = 2 THEN
        PRINT "NORTH CORRIDOR"
        PRINT "A long, dark corridor stretches before you."
        PRINT "You hear water dripping in the distance."
    ELSEIF currentRoom% = 3 THEN
        PRINT "EAST CHAMBER"
        PRINT "This chamber contains old furniture covered in dust."
        PRINT "Cobwebs hang from the ceiling."
    ELSEIF currentRoom% = 4 THEN
        PRINT "ARMORY"
        PRINT "Weapons and armor line the walls."
        PRINT "Most are rusted, but some look serviceable."
    ELSEIF currentRoom% = 5 THEN
        PRINT "LIBRARY"
        PRINT "Ancient books and scrolls fill tall shelves."
        PRINT "The smell of old parchment fills the air."
    ELSEIF currentRoom% = 6 THEN
        PRINT "TREASURE ROOM"
        IF treasureFound% = 0 THEN
            PRINT "Gold coins and jewels glitter in the torchlight!"
            PRINT "This must be the legendary treasure!"
        ELSE
            PRINT "The treasure chamber, now empty of its riches."
        END IF
    ELSEIF currentRoom% = DRAGON_ROOM% THEN
        IF dragonDefeated% = 0 THEN
            PRINT "DRAGON'S LAIR"
            PRINT "A MASSIVE RED DRAGON blocks your path!"
            PRINT "Its eyes glow with ancient fury!"
        ELSE
            PRINT "DRAGON'S LAIR"
            PRINT "The defeated dragon lies in a heap."
            PRINT "Victory is yours!"
        END IF
    ELSE
        PRINT "UNKNOWN LOCATION"
    END IF
    
    PRINT ""
    
    REM Show items in current room
    itemsHere% = 0
    FOR itemCheck% = 1 TO 5
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
                PRINT "  - An ancient scroll"
            ELSEIF itemCheck% = 5 THEN
                PRINT "  - A red healing potion"
            END IF
        END IF
    NEXT itemCheck%
    
    IF itemsHere% > 0 THEN
        PRINT ""
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
    
    PRINT "Health: "; playerHealth%; "/"; MAX_HEALTH%; " | Score: "; playerScore%; " | Turns: "; turnsPlayed%
    PRINT "========================================"
    PRINT ""
    
    REM Get and process command
    REM For automated testing, simulate commands
    turnsPlayed% = turnsPlayed% + 1
    
    REM Simulate player actions for testing
    IF turnsPlayed% = 1 THEN
        command$ = "LOOK"
        PRINT "> LOOK"
        PRINT "You carefully examine your surroundings."
        PRINT "Everything is as described above."
    ELSEIF turnsPlayed% = 2 THEN
        command$ = "E"
        PRINT "> E (East)"
        IF roomEast%(currentRoom%) > 0 THEN
            IF roomLocked%(roomEast%(currentRoom%)) = 1 AND hasKey% = 0 THEN
                PRINT "That way is locked! You need a key."
            ELSE
                currentRoom% = roomEast%(currentRoom%)
                playerScore% = playerScore% + 5
                PRINT "You move east."
            END IF
        ELSE
            PRINT "You can't go that way!"
        END IF
    ELSEIF turnsPlayed% = 3 THEN
        command$ = "TAKE KEY"
        PRINT "> TAKE KEY"
        IF itemLocations%(1) = currentRoom% THEN
            PRINT "You pick up the rusty key."
            itemLocations%(1) = -1
            hasKey% = 1
            inventoryCount% = inventoryCount% + 1
            inventory%(inventoryCount% - 1) = 1
            playerScore% = playerScore% + 10
        ELSE
            PRINT "There's no key here!"
        END IF
    ELSEIF turnsPlayed% = 4 THEN
        command$ = "N"
        PRINT "> N (North)"
        IF roomNorth%(currentRoom%) > 0 THEN
            IF roomLocked%(roomNorth%(currentRoom%)) = 1 AND hasKey% = 0 THEN
                PRINT "That way is locked! You need a key."
            ELSE
                currentRoom% = roomNorth%(currentRoom%)
                playerScore% = playerScore% + 5
                PRINT "You move north."
            END IF
        ELSE
            PRINT "You can't go that way!"
        END IF
    ELSEIF turnsPlayed% = 5 THEN
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
                    PRINT "  - Scroll"
                ELSEIF itemId% = 5 THEN
                    PRINT "  - Potion"
                END IF
            NEXT invCheck%
        END IF
    ELSEIF turnsPlayed% = 6 THEN
        command$ = "W"
        PRINT "> W (West)"
        IF roomWest%(currentRoom%) > 0 THEN
            currentRoom% = roomWest%(currentRoom%)
            playerScore% = playerScore% + 5
            PRINT "You move west."
        ELSE
            PRINT "You can't go that way!"
        END IF
    ELSEIF turnsPlayed% = 7 THEN
        command$ = "N"
        PRINT "> N (North)"
        IF roomNorth%(currentRoom%) > 0 THEN
            currentRoom% = roomNorth%(currentRoom%)
            playerScore% = playerScore% + 5
            PRINT "You move north."
        ELSE
            PRINT "You can't go that way!"
        END IF
    ELSEIF turnsPlayed% = 8 THEN
        command$ = "TAKE TORCH"
        PRINT "> TAKE TORCH"
        IF itemLocations%(3) = currentRoom% THEN
            PRINT "You pick up the burning torch."
            PRINT "It casts dancing shadows on the walls."
            itemLocations%(3) = -1
            hasTorch% = 1
            inventoryCount% = inventoryCount% + 1
            inventory%(inventoryCount% - 1) = 3
            playerScore% = playerScore% + 10
        ELSE
            PRINT "There's no torch here!"
        END IF
    ELSEIF turnsPlayed% >= 9 THEN
        command$ = "QUIT"
        PRINT "> QUIT"
        PRINT "Leaving the dungeon..."
        gameOver% = 1
    END IF
    
    REM Check win/lose conditions
    IF currentRoom% = DRAGON_ROOM% AND dragonDefeated% = 0 THEN
        IF hasSword% = 1 THEN
            PRINT ""
            PRINT "*** COMBAT ***"
            PRINT "You bravely fight the dragon with your sword!"
            PRINT "After an epic battle, the dragon falls!"
            dragonDefeated% = 1
            playerScore% = playerScore% + 100
            playerHealth% = playerHealth% - 30
        ELSE
            PRINT ""
            PRINT "*** COMBAT ***"
            PRINT "The dragon breathes fire at you!"
            PRINT "Without a weapon, you cannot fight back!"
            playerHealth% = playerHealth% - 50
        END IF
    END IF
    
    IF playerHealth% <= 0 THEN
        PRINT ""
        PRINT "You have died! Game Over."
        gameOver% = 1
    END IF
    
    IF roomHasTreasure%(currentRoom%) = 1 AND treasureFound% = 0 AND dragonDefeated% = 1 THEN
        PRINT ""
        PRINT "*** VICTORY ***"
        PRINT "You claim the legendary treasure!"
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
IF dragonDefeated% = 1 THEN PRINT "  [X] Defeated the dragon!"
IF treasureFound% = 1 THEN PRINT "  [X] Claimed the treasure!"

IF treasureFound% = 1 THEN
    PRINT ""
    PRINT "You are victorious! The treasure is yours!"
    PRINT "Rank: LEGENDARY HERO"
ELSEIF dragonDefeated% = 1 THEN
    PRINT ""
    PRINT "You defeated the dragon but did not claim the treasure."
    PRINT "Rank: DRAGON SLAYER"
ELSEIF playerHealth% > 0 THEN
    PRINT ""
    PRINT "You survived but did not complete your quest."
    PRINT "Rank: ADVENTURER"
ELSE
    PRINT ""
    PRINT "You fell in the dungeon."
    PRINT "Rank: FALLEN HERO"
END IF

PRINT ""
PRINT "Thanks for playing DUNGEON QUEST!"
END
