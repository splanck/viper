REM ============================================================================
REM DUNGEON QUEST - A Text Adventure Game
REM Testing comprehensive VIPER BASIC features
REM Target: 500-800 lines of complex BASIC code
REM ============================================================================

REM Game Constants
CONST MAX_ROOMS% = 20
CONST MAX_ITEMS% = 30
CONST MAX_INVENTORY% = 10
REM CONST GAME_VERSION$ = "1.0"

REM Game State Variables
DIM currentRoom% AS INTEGER
DIM playerHealth% AS INTEGER
DIM playerScore% AS INTEGER
DIM inventoryCount% AS INTEGER
DIM gameRunning% AS INTEGER
DIM turnsPlayed% AS INTEGER

REM Item tracking arrays
DIM inventory%(MAX_INVENTORY%)
DIM itemLocations%(MAX_ITEMS%)

REM Room connection arrays (N, S, E, W)
DIM roomNorth%(MAX_ROOMS%)
DIM roomSouth%(MAX_ROOMS%)
DIM roomEast%(MAX_ROOMS%)
DIM roomWest%(MAX_ROOMS%)

REM Room properties
DIM roomVisited%(MAX_ROOMS%)
DIM roomLocked%(MAX_ROOMS%)

REM Game flags
DIM hasKey% AS INTEGER
DIM dragonDefeated% AS INTEGER
DIM puzzleSolved% AS INTEGER

PRINT "=== DUNGEON QUEST ==="
PRINT "A Text Adventure Game"
PRINT "Version 1.0"
PRINT ""

REM Initialize game
GOSUB InitializeGame
GOSUB ShowInstructions

REM Main game loop
gameRunning% = 1
DO WHILE gameRunning% = 1
    GOSUB ShowRoom
    GOSUB GetCommand
    GOSUB ProcessCommand
    turnsPlayed% = turnsPlayed% + 1
LOOP

PRINT ""
PRINT "Thanks for playing!"
PRINT "Final Score: "; playerScore%
PRINT "Turns Played: "; turnsPlayed%
END

REM ============================================================================
REM Subroutine: InitializeGame
REM ============================================================================
InitializeGame:
    PRINT "Initializing game..."
    
    REM Initialize player
    currentRoom% = 1
    playerHealth% = 100
    playerScore% = 0
    inventoryCount% = 0
    turnsPlayed% = 0
    
    REM Initialize flags
    hasKey% = 0
    dragonDefeated% = 0
    puzzleSolved% = 0
    
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
        roomNorth%(i%) = 0
        roomSouth%(i%) = 0
        roomEast%(i%) = 0
        roomWest%(i%) = 0
    NEXT i%
    
    REM Setup room connections (room 1 = entrance)
    roomNorth%(1) = 2
    roomEast%(1) = 3
    
    roomSouth%(2) = 1
    roomEast%(2) = 4
    
    roomWest%(3) = 1
    roomNorth%(3) = 5
    
    roomWest%(4) = 2
    roomNorth%(4) = 6
    
    roomSouth%(5) = 3
    roomEast%(5) = 6
    
    roomSouth%(6) = 4
    roomWest%(6) = 5
    roomNorth%(6) = 7
    
    roomSouth%(7) = 6
    
    REM Lock some rooms
    roomLocked%(7) = 1
    
    REM Place items (item 1 = key, 2 = sword, 3 = torch, 4 = scroll, 5 = potion)
    itemLocations%(1) = 3  REM Key in room 3
    itemLocations%(2) = 4  REM Sword in room 4
    itemLocations%(3) = 2  REM Torch in room 2
    itemLocations%(4) = 5  REM Scroll in room 5
    itemLocations%(5) = 6  REM Potion in room 6
    
    PRINT "Game initialized!"
    PRINT ""
RETURN

REM ============================================================================
REM Subroutine: ShowInstructions
REM ============================================================================
ShowInstructions:
    PRINT "=== INSTRUCTIONS ==="
    PRINT "Commands:"
    PRINT "  N, S, E, W - Move North, South, East, West"
    PRINT "  LOOK - Look around"
    PRINT "  INVENTORY - Check inventory"
    PRINT "  TAKE <item> - Take an item"
    PRINT "  USE <item> - Use an item"
    PRINT "  HELP - Show this help"
    PRINT "  QUIT - Exit game"
    PRINT ""
RETURN

REM ============================================================================
REM Subroutine: ShowRoom
REM ============================================================================
ShowRoom:
    PRINT ""
    PRINT "----------------------------------------"
    
    REM Mark room as visited
    roomVisited%(currentRoom%) = 1
    
    REM Show room description based on room number
    SELECT CASE currentRoom%
        CASE 1
            PRINT "ENTRANCE HALL"
            PRINT "You are in the entrance hall of an ancient dungeon."
            PRINT "Torches flicker on the stone walls."
        CASE 2
            PRINT "NORTH CORRIDOR"
            PRINT "A long, dark corridor stretches before you."
            PRINT "You hear water dripping somewhere."
        CASE 3
            PRINT "EAST CHAMBER"
            PRINT "This chamber has old furniture covered in dust."
        CASE 4
            PRINT "ARMORY"
            PRINT "Weapons and armor line the walls, most rusted."
        CASE 5
            PRINT "LIBRARY"
            PRINT "Shelves of ancient books surround you."
        CASE 6
            PRINT "TREASURE ROOM"
            PRINT "Gold coins glitter in the torchlight!"
        CASE 7
            PRINT "DRAGON'S LAIR"
            PRINT "A massive dragon sleeps on a pile of treasure!"
        CASE ELSE
            PRINT "UNKNOWN LOCATION"
    END SELECT
    
    PRINT ""
    
    REM Show items in room
    itemsHere% = 0
    FOR i% = 1 TO 5
        IF itemLocations%(i%) = currentRoom% THEN
            IF itemsHere% = 0 THEN
                PRINT "You see:"
                itemsHere% = 1
            END IF
            GOSUB ShowItemName
        END IF
    NEXT i%
    
    IF itemsHere% = 1 THEN
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
    
    PRINT "Health: "; playerHealth%; " Score: "; playerScore%
    PRINT "----------------------------------------"
RETURN

REM ============================================================================
REM Subroutine: ShowItemName
REM Helper subroutine to display item names
REM Expects: i% = item number
REM ============================================================================
ShowItemName:
    SELECT CASE i%
        CASE 1
            PRINT "  - A rusty key"
        CASE 2
            PRINT "  - A sharp sword"
        CASE 3
            PRINT "  - A burning torch"
        CASE 4
            PRINT "  - An ancient scroll"
        CASE 5
            PRINT "  - A health potion"
    END SELECT
RETURN

REM ============================================================================
REM Subroutine: GetCommand
REM ============================================================================
GetCommand:
    PRINT ""
    PRINT "What do you do? ";
    REM In real game would use INPUT, for testing we'll simulate
    REM This is where we'd get player input
RETURN

REM ============================================================================
REM Subroutine: ProcessCommand
REM ============================================================================
ProcessCommand:
    REM For testing, simulate a few moves
    IF turnsPlayed% < 3 THEN
        PRINT "Moving north..."
        IF roomNorth%(currentRoom%) > 0 THEN
            currentRoom% = roomNorth%(currentRoom%)
            playerScore% = playerScore% + 5
        ELSE
            PRINT "You can't go that way!"
        END IF
    ELSEIF turnsPlayed% = 3 THEN
        PRINT "Moving south..."
        IF roomSouth%(currentRoom%) > 0 THEN
            currentRoom% = roomSouth%(currentRoom%)
        END IF
    ELSEIF turnsPlayed% = 4 THEN
        PRINT "Moving east..."
        IF roomEast%(currentRoom%) > 0 THEN
            currentRoom% = roomEast%(currentRoom%)
            playerScore% = playerScore% + 5
        END IF
    ELSE
        PRINT "Quitting game..."
        gameRunning% = 0
    END IF
RETURN
