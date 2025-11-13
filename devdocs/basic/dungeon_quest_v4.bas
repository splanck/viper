REM ============================================================================
REM DUNGEON QUEST - A Text Adventure Game
REM Comprehensive VIPER BASIC Feature Testing
REM Properly structured with SUBs and FUNCTIONs
REM Target: 700-800 lines
REM ============================================================================

REM Game Constants
CONST MAX_ROOMS% = 20
CONST MAX_ITEMS% = 30
CONST MAX_INVENTORY% = 10
CONST MAX_HEALTH% = 100
CONST DRAGON_ROOM% = 7
CONST MAX_MONSTERS% = 10

REM Game State (must be global for SUBs to access)
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

PRINT "=== DUNGEON QUEST ==="
PRINT "A Text Adventure Game"
PRINT "Version 3.0 - Structured Edition"
PRINT ""
PRINT "Loading game systems..."
PRINT ""

REM Initialize the game
InitGame()

REM Show instructions
ShowInstructions()

REM Main game loop - now properly structured!
FOR turnLimit% = 1 TO 1000
    IF gameOver% = 1 THEN
        EXIT FOR
    END IF

    REM Show current room status
    ShowRoom()

    REM Process turn
    ProcessTurn()

    REM Check game end conditions
    CheckGameOver()

    turnsPlayed% = turnsPlayed% + 1
NEXT turnLimit%

REM Show final statistics
ShowFinalStats()

PRINT ""
PRINT "Thanks for playing DUNGEON QUEST!"
END

REM ============================================================================
REM SUB: InitGame
REM Initialize all game variables and data structures
REM ============================================================================
SUB InitGame()
    DIM i% AS INTEGER

    REM Initialize player stats
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

    REM Setup dungeon layout
    SetupDungeon()

    REM Place items and monsters
    PlaceItems()
    PlaceMonsters()

    PRINT "Dungeon loaded!"
END SUB

REM ============================================================================
REM SUB: SetupDungeon
REM Configure room connections
REM ============================================================================
SUB SetupDungeon()
    REM Room 1: Entrance
    roomNorth%(1) = 2
    roomEast%(1) = 3

    REM Room 2: North Corridor
    roomSouth%(2) = 1
    roomEast%(2) = 4
    roomDarkness%(2) = 1

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
    roomHasTreasure%(6) = 1

    REM Room 7: Dragon's Lair
    roomSouth%(7) = 6
    roomLocked%(7) = 1
    roomDarkness%(7) = 1
END SUB

REM ============================================================================
REM SUB: PlaceItems
REM Place items throughout the dungeon
REM ============================================================================
SUB PlaceItems()
    itemLocations%(1) = 3  REM key in East Chamber
    itemLocations%(2) = 4  REM sword in Armory
    itemLocations%(3) = 2  REM torch in North Corridor
    itemLocations%(4) = 5  REM scroll in Library
    itemLocations%(5) = 6  REM potion in Treasure Room
    itemLocations%(6) = 2  REM map in North Corridor
    itemLocations%(7) = 0  REM boss key (hidden, revealed by puzzle)
END SUB

REM ============================================================================
REM SUB: PlaceMonsters
REM Place monsters in rooms
REM ============================================================================
SUB PlaceMonsters()
    REM Monster 1: Goblin in corridor
    monsterRoom%(1) = 2
    monsterHealth%(1) = 20
    monsterDamage%(1) = 5
    monsterAlive%(1) = 1

    REM Monster 2: Skeleton in armory
    monsterRoom%(2) = 4
    monsterHealth%(2) = 30
    monsterDamage%(2) = 8
    monsterAlive%(2) = 1

    REM Monster 3: Ghost in library
    monsterRoom%(3) = 5
    monsterHealth%(3) = 25
    monsterDamage%(3) = 6
    monsterAlive%(3) = 1
END SUB

REM ============================================================================
REM SUB: ShowInstructions
REM Display game instructions
REM ============================================================================
SUB ShowInstructions()
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
END SUB

REM ============================================================================
REM SUB: ShowRoom
REM Display current room description and contents
REM ============================================================================
SUB ShowRoom()
    DIM monsterHere%, currentMonster% AS INTEGER
    DIM itemsHere%, itemCheck%, canSeeItems% AS INTEGER
    DIM exitCount% AS INTEGER

    PRINT ""
    PRINT "========================================"
    roomVisited%(currentRoom%) = 1

    REM Check for monsters
    monsterHere% = GetMonsterInRoom()
    currentMonster% = monsterHere%

    REM Show room description
    ShowRoomDescription()

    PRINT ""

    REM Show monster if present
    IF monsterHere% > 0 THEN
        ShowMonster(monsterHere%)
        PRINT ""
    END IF

    REM Show items (if room is lit or we have torch)
    canSeeItems% = CanSeeItems()
    IF canSeeItems% = 1 THEN
        ShowItemsInRoom()
    END IF

    REM Show exits
    ShowExits()

    REM Show status line
    PRINT "Health: "; playerHealth%; "/"; MAX_HEALTH%; " | Score: "; playerScore%; " | Level: "; playerLevel%; " | Turn: "; turnsPlayed%
    PRINT "========================================"
    PRINT ""
END SUB

REM ============================================================================
REM FUNCTION: GetMonsterInRoom
REM Returns monster ID if monster is in current room and alive, 0 otherwise
REM ============================================================================
FUNCTION GetMonsterInRoom%()
    DIM i% AS INTEGER
    FOR i% = 1 TO 3
        IF monsterRoom%(i%) = currentRoom% AND monsterAlive%(i%) = 1 THEN
            GetMonsterInRoom% = i%
            EXIT FUNCTION
        END IF
    NEXT i%
    GetMonsterInRoom% = 0
END FUNCTION

REM ============================================================================
REM FUNCTION: CanSeeItems
REM Returns 1 if player can see items, 0 if too dark
REM ============================================================================
FUNCTION CanSeeItems%()
    IF roomDarkness%(currentRoom%) = 1 AND hasTorch% = 0 THEN
        CanSeeItems% = 0
    ELSE
        CanSeeItems% = 1
    END IF
END FUNCTION

REM ============================================================================
REM SUB: ShowRoomDescription
REM Display description of current room
REM ============================================================================
SUB ShowRoomDescription()
    SELECT CASE currentRoom%
        CASE 1
            PRINT "ENTRANCE HALL"
            PRINT "You stand in the entrance of an ancient dungeon."
            PRINT "Torches flicker on damp stone walls."
        CASE 2
            PRINT "NORTH CORRIDOR"
            PRINT "A long, dark corridor stretches before you."
            IF roomDarkness%(currentRoom%) = 1 AND hasTorch% = 0 THEN
                PRINT "It's too dark to see much!"
            END IF
        CASE 3
            PRINT "EAST CHAMBER"
            PRINT "This chamber contains old furniture covered in dust."
            PRINT "Cobwebs hang from the ceiling."
        CASE 4
            PRINT "ARMORY"
            PRINT "Weapons and armor line the walls."
            PRINT "Most are rusted, but some look serviceable."
        CASE 5
            PRINT "LIBRARY"
            PRINT "Ancient books and scrolls fill tall shelves."
            PRINT "The smell of old parchment fills the air."
            IF puzzleSolved% = 0 THEN
                PRINT "Strange symbols are carved into the desk."
            ELSE
                PRINT "The desk glows - the puzzle is solved!"
            END IF
        CASE 6
            PRINT "TREASURE ROOM"
            IF treasureFound% = 0 THEN
                PRINT "Gold and jewels glitter in the torchlight!"
            ELSE
                PRINT "The treasure chamber, now empty."
            END IF
        CASE 7
            IF dragonDefeated% = 0 THEN
                PRINT "DRAGON'S LAIR"
                PRINT "A MASSIVE RED DRAGON blocks your path!"
                PRINT "Its eyes glow with ancient fury!"
            ELSE
                PRINT "DRAGON'S LAIR"
                PRINT "The defeated dragon lies in a heap."
            END IF
        CASE ELSE
            PRINT "UNKNOWN LOCATION"
    END SELECT
END SUB

REM ============================================================================
REM SUB: ShowMonster
REM Display monster information
REM ============================================================================
SUB ShowMonster(monsterId%)
    PRINT "!!! DANGER !!!"
    SELECT CASE monsterId%
        CASE 1
            PRINT "A snarling GOBLIN bars your way!"
        CASE 2
            PRINT "An armored SKELETON rattles menacingly!"
        CASE 3
            PRINT "A spectral GHOST floats before you!"
    END SELECT
    PRINT "HP: "; monsterHealth%(monsterId%)
END SUB

REM ============================================================================
REM SUB: ShowItemsInRoom
REM Display items present in current room
REM ============================================================================
SUB ShowItemsInRoom()
    DIM itemsHere%, i% AS INTEGER
    itemsHere% = 0

    FOR i% = 1 TO 7
        IF itemLocations%(i%) = currentRoom% THEN
            IF itemsHere% = 0 THEN
                PRINT "You see:"
                itemsHere% = 1
            END IF
            ShowItemName(i%)
        END IF
    NEXT i%

    IF itemsHere% > 0 THEN
        PRINT ""
    END IF
END SUB

REM ============================================================================
REM SUB: ShowItemName
REM Display name of item by ID
REM ============================================================================
SUB ShowItemName(itemId%)
    SELECT CASE itemId%
        CASE 1
            PRINT "  - A rusty iron key"
        CASE 2
            PRINT "  - A sharp steel sword"
        CASE 3
            PRINT "  - A burning torch"
        CASE 4
            PRINT "  - An ancient scroll with runes"
        CASE 5
            PRINT "  - A red healing potion"
        CASE 6
            PRINT "  - A yellowed dungeon map"
        CASE 7
            PRINT "  - A golden boss key!"
    END SELECT
END SUB

REM ============================================================================
REM SUB: ShowExits
REM Display available exits
REM ============================================================================
SUB ShowExits()
    DIM exitCount% AS INTEGER
    exitCount% = 0

    PRINT "Exits: ";
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
END SUB

REM ============================================================================
REM SUB: ProcessTurn
REM Process one game turn (simulate commands for testing)
REM ============================================================================
SUB ProcessTurn()
    REM Simulate player commands for comprehensive testing
    SELECT CASE turnsPlayed%
        CASE 0
            SimulateCommand_Look()
        CASE 1
            SimulateCommand_Stats()
        CASE 2
            SimulateCommand_Move_East()
        CASE 3
            SimulateCommand_Take_Key()
        CASE 4
            SimulateCommand_Move_North()
        CASE 5
            SimulateCommand_Take_Scroll()
        CASE 6
            SimulateCommand_Use_Scroll()
        CASE 7
            SimulateCommand_Take_BossKey()
        CASE 8
            SimulateCommand_Inventory()
        CASE 9
            SimulateCommand_Move_South()
        CASE 10
            SimulateCommand_Move_West()
        CASE 11
            SimulateCommand_Move_North()
        CASE ELSE
            SimulateCommand_Quit()
    END SELECT
END SUB

REM ============================================================================
REM Command Simulation SUBs
REM ============================================================================

SUB SimulateCommand_Look()
    PRINT "> LOOK"
    PRINT "You carefully examine your surroundings."
END SUB

SUB SimulateCommand_Stats()
    PRINT "> STATS"
    PRINT "=== CHARACTER STATS ==="
    PRINT "Level: "; playerLevel%
    PRINT "Experience: "; playerExperience%
    PRINT "Health: "; playerHealth%; "/"; MAX_HEALTH%
    PRINT "Score: "; playerScore%
    PRINT "Items: "; inventoryCount%; "/"; MAX_INVENTORY%
END SUB

SUB SimulateCommand_Move_East()
    DIM nextRoom% AS INTEGER
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
END SUB

SUB SimulateCommand_Take_Key()
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
END SUB

SUB SimulateCommand_Move_North()
    DIM nextRoom% AS INTEGER
    PRINT "> N (North)"
    IF roomNorth%(currentRoom%) > 0 THEN
        nextRoom% = roomNorth%(currentRoom%)
        IF roomLocked%(nextRoom%) = 1 AND hasKey% = 0 THEN
            PRINT "That way is locked!"
        ELSE
            currentRoom% = nextRoom%
            playerScore% = playerScore% + 5
            PRINT "You move north."
        END IF
    ELSE
        PRINT "You can't go that way!"
    END IF
END SUB

SUB SimulateCommand_Take_Scroll()
    PRINT "> TAKE SCROLL"
    IF itemLocations%(4) = currentRoom% THEN
        PRINT "You pick up the ancient scroll."
        itemLocations%(4) = -1
        hasScroll% = 1
        inventoryCount% = inventoryCount% + 1
        inventory%(inventoryCount% - 1) = 4
        playerScore% = playerScore% + 15
    ELSE
        PRINT "There's no scroll here!"
    END IF
END SUB

SUB SimulateCommand_Use_Scroll()
    PRINT "> USE SCROLL"
    IF hasScroll% = 1 AND puzzleSolved% = 0 THEN
        PRINT "You read the ancient scroll..."
        PRINT "The runes reveal a hidden secret!"
        puzzleSolved% = 1
        playerScore% = playerScore% + 50
        itemLocations%(7) = currentRoom%
        PRINT "A golden key appears!"
    ELSEIF hasScroll% = 0 THEN
        PRINT "You don't have a scroll!"
    ELSE
        PRINT "The scroll's magic has been used."
    END IF
END SUB

SUB SimulateCommand_Take_BossKey()
    PRINT "> TAKE BOSS KEY"
    IF itemLocations%(7) = currentRoom% THEN
        PRINT "You take the golden boss key."
        itemLocations%(7) = -1
        bossKeyFound% = 1
        hasKey% = 1
        inventoryCount% = inventoryCount% + 1
        inventory%(inventoryCount% - 1) = 7
        playerScore% = playerScore% + 25
    ELSE
        PRINT "There's no boss key here!"
    END IF
END SUB

SUB SimulateCommand_Inventory()
    DIM i%, itemId% AS INTEGER
    PRINT "> INVENTORY"
    PRINT "You are carrying:"
    IF inventoryCount% = 0 THEN
        PRINT "  Nothing"
    ELSE
        FOR i% = 0 TO inventoryCount% - 1
            itemId% = inventory%(i%)
            ShowItemName(itemId%)
        NEXT i%
    END IF
END SUB

SUB SimulateCommand_Move_South()
    PRINT "> S (South)"
    IF roomSouth%(currentRoom%) > 0 THEN
        currentRoom% = roomSouth%(currentRoom%)
        playerScore% = playerScore% + 5
        PRINT "You move south."
    ELSE
        PRINT "You can't go that way!"
    END IF
END SUB

SUB SimulateCommand_Move_West()
    PRINT "> W (West)"
    IF roomWest%(currentRoom%) > 0 THEN
        currentRoom% = roomWest%(currentRoom%)
        playerScore% = playerScore% + 5
        PRINT "You move west."
    ELSE
        PRINT "You can't go that way!"
    END IF
END SUB

SUB SimulateCommand_Quit()
    PRINT "> QUIT"
    PRINT "Leaving the dungeon..."
    gameOver% = 1
END SUB

REM ============================================================================
REM SUB: CheckGameOver
REM Check for game-ending conditions
REM ============================================================================
SUB CheckGameOver()
    REM Check for dragon combat
    IF currentRoom% = DRAGON_ROOM% AND dragonDefeated% = 0 THEN
        IF hasSword% = 1 THEN
            DragontCombat_WithSword()
        ELSE
            DragonCombat_NoSword()
        END IF
    END IF

    REM Check death
    IF playerHealth% <= 0 THEN
        PRINT ""
        PRINT "*** YOU HAVE DIED ***"
        PRINT "The dungeon claims another victim."
        gameOver% = 1
    END IF

    REM Check victory
    IF roomHasTreasure%(currentRoom%) = 1 AND treasureFound% = 0 AND dragonDefeated% = 1 THEN
        PRINT ""
        PRINT "*** VICTORY ***"
        PRINT "You claim the legendary treasure!"
        treasureFound% = 1
        playerScore% = playerScore% + 500
        gameOver% = 1
    END IF
END SUB

REM ============================================================================
REM SUB: DragonCombat_WithSword
REM Handle dragon combat when player has sword
REM ============================================================================
SUB DragonCombat_WithSword()
    DIM damage% AS INTEGER
    PRINT ""
    PRINT "*** EPIC COMBAT ***"
    PRINT "You fight the dragon with your sword!"
    damage% = 70
    PRINT "Damage dealt: "; damage%
    PRINT "The dragon falls!"
    dragonDefeated% = 1
    playerScore% = playerScore% + 100
    playerHealth% = playerHealth% - 30
    playerExperience% = playerExperience% + 50

    IF playerExperience% >= 50 AND playerLevel% = 1 THEN
        PRINT ""
        PRINT "*** LEVEL UP! ***"
        playerLevel% = playerLevel% + 1
        playerHealth% = MAX_HEALTH%
    END IF
END SUB

REM ============================================================================
REM SUB: DragonCombat_NoSword
REM Handle dragon combat when player has no sword
REM ============================================================================
SUB DragonCombat_NoSword()
    PRINT ""
    PRINT "*** COMBAT ***"
    PRINT "The dragon breathes fire!"
    PRINT "Without a weapon, you flee!"
    playerHealth% = playerHealth% - 50
    IF playerHealth% > 0 THEN
        currentRoom% = roomSouth%(currentRoom%)
    END IF
END SUB

REM ============================================================================
REM SUB: ShowFinalStats
REM Display final game statistics
REM ============================================================================
SUB ShowFinalStats()
    DIM visitedCount%, i% AS INTEGER

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

    visitedCount% = 0
    FOR i% = 1 TO 7
        visitedCount% = visitedCount% + roomVisited%(i%)
    NEXT i%
    PRINT "  Rooms Explored: "; visitedCount%; "/7"

    PRINT ""
    PRINT "Achievements:"
    IF hasKey% = 1 THEN PRINT "  [X] Found the key"
    IF hasSword% = 1 THEN PRINT "  [X] Acquired a weapon"
    IF hasTorch% = 1 THEN PRINT "  [X] Lit your path"
    IF hasScroll% = 1 THEN PRINT "  [X] Deciphered ancient runes"
    IF puzzleSolved% = 1 THEN PRINT "  [X] Solved the puzzle"
    IF bossKeyFound% = 1 THEN PRINT "  [X] Found the golden key"
    IF dragonDefeated% = 1 THEN PRINT "  [X] Defeated the dragon!"
    IF treasureFound% = 1 THEN PRINT "  [X] Claimed the treasure!"

    PRINT ""
    ShowGameRank()
END SUB

REM ============================================================================
REM SUB: ShowGameRank
REM Display player's final rank
REM ============================================================================
SUB ShowGameRank()
    IF treasureFound% = 1 THEN
        PRINT "Rank: LEGENDARY HERO"
        PRINT "You are victorious!"
    ELSEIF dragonDefeated% = 1 THEN
        PRINT "Rank: DRAGON SLAYER"
        PRINT "You defeated the dragon!"
    ELSEIF playerHealth% > 0 THEN
        PRINT "Rank: ADVENTURER"
        PRINT "You survived the dungeon."
    ELSE
        PRINT "Rank: FALLEN HERO"
        PRINT "You fell in battle."
    END IF
END SUB
