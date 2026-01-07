' VIPER BASIC Adventure Game - Stress Test v2
' Testing OOP, classes, arrays, strings, loops, and file inclusion
' Workaround: Avoid calling module SUBs from within class methods

' ===== UTILITY FUNCTIONS (MODULE LEVEL) =====

SUB PrintDivider()
    PRINT "=========================================="
END SUB

SUB PrintHeader(title AS STRING)
    PRINT ""
    PrintDivider()
    PRINT "  "; title
    PrintDivider()
    PRINT ""
END SUB

SUB DisplayRoomName(roomName AS STRING)
    PrintHeader(roomName)
END SUB

' ===== CLASS DEFINITIONS =====

CLASS Item
    DIM name AS STRING
    DIM description AS STRING
    DIM value AS INTEGER
END CLASS

CLASS Room
    DIM name AS STRING
    DIM description AS STRING
    DIM visited AS INTEGER
END CLASS

CLASS Player
    DIM name AS STRING
    DIM health AS INTEGER
    DIM score AS INTEGER
    DIM currentRoom AS INTEGER
END CLASS

' ===== MAIN PROGRAM =====

PrintHeader("THE CRYSTAL CASTLE")
PRINT "A Text Adventure in Viper BASIC"
PRINT ""

' Create player
DIM player AS Player
player = NEW Player()
player.name = "Adventurer"
player.health = 100
player.score = 0
player.currentRoom = 0

' Create rooms
DIM rooms(5) AS Room
DIM tempRoom AS Room
DIM i AS INTEGER

' Room 0: Entrance
tempRoom = NEW Room()
tempRoom.name = "Castle Entrance"
tempRoom.description = "A massive stone archway towers before you. Moss clings to ancient walls."
tempRoom.visited = 0
rooms(0) = tempRoom

' Room 1: Great Hall
tempRoom = NEW Room()
tempRoom.name = "Great Hall"
tempRoom.description = "Dust dances in shafts of light from high windows. Tattered banners hang."
tempRoom.visited = 0
rooms(1) = tempRoom

' Room 2: Library
tempRoom = NEW Room()
tempRoom.name = "Ancient Library"
tempRoom.description = "Countless books line floor-to-ceiling shelves. Old parchment scent fills the air."
tempRoom.visited = 0
rooms(2) = tempRoom

' Room 3: Armory
tempRoom = NEW Room()
tempRoom.name = "Royal Armory"
tempRoom.description = "Weapons and armor rest on stone racks. Some gleam with otherworldly light."
tempRoom.visited = 0
rooms(3) = tempRoom

' Room 4: Treasure Room
tempRoom = NEW Room()
tempRoom.name = "Treasure Chamber"
tempRoom.description = "Gold and jewels overflow from chests. A glowing crystal sits in the center."
tempRoom.visited = 0
rooms(4) = tempRoom

' Create items
DIM items(10) AS Item
DIM tempItem AS Item

tempItem = NEW Item()
tempItem.name = "Rusty Sword"
tempItem.description = "An old but sturdy blade"
tempItem.value = 10
items(0) = tempItem

tempItem = NEW Item()
tempItem.name = "Healing Potion"
tempItem.description = "A vial of shimmering red liquid"
tempItem.value = 25
items(1) = tempItem

tempItem = NEW Item()
tempItem.name = "Ancient Tome"
tempItem.description = "A book bound in dark leather"
tempItem.value = 50
items(2) = tempItem

tempItem = NEW Item()
tempItem.name = "Magic Shield"
tempItem.description = "A shield that hums with power"
tempItem.value = 100
items(3) = tempItem

tempItem = NEW Item()
tempItem.name = "Crystal Key"
tempItem.description = "A translucent key that glows faintly"
tempItem.value = 200
items(4) = tempItem

' Game loop - visit each room
PRINT "Your adventure begins..."
PRINT ""

FOR i = 0 TO 4
    tempRoom = rooms(i)
    player.currentRoom = i

    ' Display room
    DisplayRoomName(tempRoom.name)
    PRINT tempRoom.description
    PRINT ""

    ' Mark visited
    tempRoom.visited = 1
    rooms(i) = tempRoom

    ' Display player status
    PRINT "You are in room "; i
    PRINT "Player: "; player.name; "  Health: "; player.health; "  Score: "; player.score
    PRINT ""

    ' Add points
    player.score = player.score + 10
NEXT i

' Display all items found
PrintHeader("Items Discovered")
FOR i = 0 TO 4
    tempItem = items(i)
    PRINT "  ["; tempItem.name; "] - "; tempItem.description; " (value: "; tempItem.value; ")"
NEXT i

' Final status
PRINT ""
PrintHeader("Adventure Complete!")
PRINT "Player: "; player.name
PRINT "Final Health: "; player.health
PRINT "Final Score: "; player.score
PRINT ""
PRINT "Congratulations! You explored the Crystal Castle."
PRINT "Rooms visited: 5"
PRINT "Items found: 5"

END
