' VIPER BASIC Adventure Game - Stress Test
' Testing OOP, classes, arrays, strings, loops, and file inclusion

ADDFILE "game_utils.bas"

' ===== CLASS DEFINITIONS =====

CLASS Item
    DIM name AS STRING
    DIM description AS STRING
    DIM value AS INTEGER

    SUB Init(n AS STRING, d AS STRING, v AS INTEGER)
        name = n
        description = d
        value = v
    END SUB

    SUB Display()
        PRINT "  ["; name; "] - "; description; " (value: "; value; ")"
    END SUB
END CLASS

CLASS Room
    DIM name AS STRING
    DIM description AS STRING
    DIM visited AS INTEGER
    DIM itemCount AS INTEGER

    SUB Init(n AS STRING, d AS STRING)
        name = n
        description = d
        visited = 0
        itemCount = 0
    END SUB

    SUB Describe()
        PRINT ""
        PrintHeader(name)
        PRINT description
        PRINT ""
    END SUB

    SUB MarkVisited()
        visited = 1
    END SUB
END CLASS

CLASS Player
    DIM name AS STRING
    DIM health AS INTEGER
    DIM score AS INTEGER
    DIM currentRoom AS INTEGER

    SUB Init(playerName AS STRING)
        name = playerName
        health = 100
        score = 0
        currentRoom = 0
    END SUB

    SUB DisplayStatus()
        PRINT "Player: "; name; "  Health: "; health; "  Score: "; score
    END SUB
END CLASS

' ===== MAIN PROGRAM =====

PrintHeader("THE CRYSTAL CASTLE")
PRINT "A Text Adventure in Viper BASIC"
PRINT ""

' Create player
DIM player AS Player
player = NEW Player()
player.Init("Adventurer")

' Create rooms
DIM rooms(5) AS Room
DIM tempRoom AS Room
DIM i AS INTEGER

' Room 0: Entrance
tempRoom = NEW Room()
tempRoom.Init("Castle Entrance", "A massive stone archway towers before you. Moss clings to ancient walls.")
rooms(0) = tempRoom

' Room 1: Great Hall
tempRoom = NEW Room()
tempRoom.Init("Great Hall", "Dust dances in shafts of light from high windows. Tattered banners hang from the ceiling.")
rooms(1) = tempRoom

' Room 2: Library
tempRoom = NEW Room()
tempRoom.Init("Ancient Library", "Countless books line floor-to-ceiling shelves. The smell of old parchment fills the air.")
rooms(2) = tempRoom

' Room 3: Armory
tempRoom = NEW Room()
tempRoom.Init("Royal Armory", "Weapons and armor rest on stone racks. Some items gleam with an otherworldly light.")
rooms(3) = tempRoom

' Room 4: Treasure Room
tempRoom = NEW Room()
tempRoom.Init("Treasure Chamber", "Gold and jewels overflow from ornate chests. In the center sits a glowing crystal.")
rooms(4) = tempRoom

' Create items
DIM items(10) AS Item
DIM tempItem AS Item

tempItem = NEW Item()
tempItem.Init("Rusty Sword", "An old but sturdy blade", 10)
items(0) = tempItem

tempItem = NEW Item()
tempItem.Init("Healing Potion", "A vial of shimmering red liquid", 25)
items(1) = tempItem

tempItem = NEW Item()
tempItem.Init("Ancient Tome", "A book bound in dark leather", 50)
items(2) = tempItem

tempItem = NEW Item()
tempItem.Init("Magic Shield", "A shield that hums with power", 100)
items(3) = tempItem

tempItem = NEW Item()
tempItem.Init("Crystal Key", "A translucent key that glows faintly", 200)
items(4) = tempItem

' Game loop simulation (simplified due to lack of IF in methods)
PRINT "Your adventure begins..."
PRINT ""

' Visit each room
FOR i = 0 TO 4
    tempRoom = rooms(i)
    player.currentRoom = i
    tempRoom.Describe()
    tempRoom.MarkVisited()

    PRINT "You are in room "; i
    player.DisplayStatus()

    ' Add points for visiting
    player.score = player.score + 10

    PRINT ""
NEXT i

' Display all items found
PrintHeader("Items Discovered")
FOR i = 0 TO 4
    tempItem = items(i)
    tempItem.Display()
NEXT i

' Final status
PRINT ""
PrintHeader("Adventure Complete!")
player.DisplayStatus()
PRINT ""
PRINT "Congratulations! You explored the Crystal Castle."
PRINT "Rooms visited: 5"
PRINT "Items found: 5"

END
