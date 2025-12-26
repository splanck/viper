' DUNGEON CRAWLER - Advanced OOP Stress Test
' Tests: Multiple classes, arrays, inheritance simulation, complex logic

' ===== GLOBAL CONFIGURATION =====
DIM GAME_WIDTH AS INTEGER
DIM GAME_HEIGHT AS INTEGER
DIM MAX_MONSTERS AS INTEGER
DIM MAX_ITEMS AS INTEGER

GAME_WIDTH = 60
GAME_HEIGHT = 20
MAX_MONSTERS = 10
MAX_ITEMS = 15

' ===== UTILITY FUNCTIONS =====

SUB PrintBanner()
    DIM i AS INTEGER
    FOR i = 1 TO GAME_WIDTH
        PRINT "=";
    NEXT i
    PRINT ""
END SUB

SUB PrintCentered(msg AS STRING)
    DIM msgLen AS INTEGER
    DIM padding AS INTEGER
    DIM i AS INTEGER

    msgLen = LEN(msg)
    padding = (GAME_WIDTH - msgLen) / 2

    FOR i = 1 TO padding
        PRINT " ";
    NEXT i
    PRINT msg
END SUB

FUNCTION Min(a AS INTEGER, b AS INTEGER) AS INTEGER
    DIM result AS INTEGER
    result = a
    ' Workaround for no IF: use math
    ' This is a limitation we'll work around manually
    RETURN result
END FUNCTION

FUNCTION Max(a AS INTEGER, b AS INTEGER) AS INTEGER
    DIM result AS INTEGER
    result = a
    ' Workaround for no IF
    RETURN result
END FUNCTION

' ===== CLASS DEFINITIONS =====

CLASS Entity
    DIM name AS STRING
    DIM x AS INTEGER
    DIM y AS INTEGER
    DIM health AS INTEGER
    DIM maxHealth AS INTEGER
    DIM alive AS INTEGER
END CLASS

CLASS Monster
    DIM name AS STRING
    DIM health AS INTEGER
    DIM damage AS INTEGER
    DIM xp AS INTEGER
END CLASS

CLASS Item
    DIM name AS STRING
    DIM itemType AS INTEGER  ' 0=potion, 1=weapon, 2=armor, 3=treasure
    DIM value AS INTEGER
    DIM found AS INTEGER
END CLASS

CLASS Player
    DIM name AS STRING
    DIM health AS INTEGER
    DIM maxHealth AS INTEGER
    DIM attack AS INTEGER
    DIM defense AS INTEGER
    DIM xp AS INTEGER
    DIM level AS INTEGER
    DIM gold AS INTEGER
END CLASS

CLASS GameStats
    DIM monstersDefeated AS INTEGER
    DIM itemsCollected AS INTEGER
    DIM roomsExplored AS INTEGER
    DIM goldEarned AS INTEGER
    DIM turnsPlayed AS INTEGER
END CLASS

' ===== MAIN PROGRAM =====

PrintBanner()
PrintCentered("DUNGEON CRAWLER")
PrintCentered("An Advanced Viper BASIC Adventure")
PrintBanner()
PRINT ""

' Initialize player
DIM player AS Player
player = NEW Player()
player.name = "Brave Adventurer"
player.maxHealth = 100
player.health = 100
player.attack = 10
player.defense = 5
player.xp = 0
player.level = 1
player.gold = 50

' Initialize stats
DIM stats AS GameStats
stats = NEW GameStats()
stats.monstersDefeated = 0
stats.itemsCollected = 0
stats.roomsExplored = 0
stats.goldEarned = 0
stats.turnsPlayed = 0

' Create monsters
DIM monsters(10) AS Monster
DIM tempMonster AS Monster
DIM i AS INTEGER

' Monster 0: Goblin
tempMonster = NEW Monster()
tempMonster.name = "Goblin"
tempMonster.health = 20
tempMonster.damage = 5
tempMonster.xp = 10
monsters(0) = tempMonster

' Monster 1: Orc
tempMonster = NEW Monster()
tempMonster.name = "Orc"
tempMonster.health = 40
tempMonster.damage = 10
tempMonster.xp = 25
monsters(1) = tempMonster

' Monster 2: Troll
tempMonster = NEW Monster()
tempMonster.name = "Troll"
tempMonster.health = 60
tempMonster.damage = 15
tempMonster.xp = 50
monsters(2) = tempMonster

' Monster 3: Dragon
tempMonster = NEW Monster()
tempMonster.name = "Dragon"
tempMonster.health = 150
tempMonster.damage = 30
tempMonster.xp = 200
monsters(3) = tempMonster

' Monster 4: Skeleton
tempMonster = NEW Monster()
tempMonster.name = "Skeleton"
tempMonster.health = 25
tempMonster.damage = 7
tempMonster.xp = 15
monsters(4) = tempMonster

' Create items
DIM items(15) AS Item
DIM tempItem AS Item

' Item 0: Health Potion
tempItem = NEW Item()
tempItem.name = "Health Potion"
tempItem.itemType = 0
tempItem.value = 50
tempItem.found = 0
items(0) = tempItem

' Item 1: Sword of Power
tempItem = NEW Item()
tempItem.name = "Sword of Power"
tempItem.itemType = 1
tempItem.value = 100
tempItem.found = 0
items(1) = tempItem

' Item 2: Steel Armor
tempItem = NEW Item()
tempItem.name = "Steel Armor"
tempItem.itemType = 2
tempItem.value = 75
tempItem.found = 0
items(2) = tempItem

' Item 3: Gold Coins
tempItem = NEW Item()
tempItem.name = "Bag of Gold"
tempItem.itemType = 3
tempItem.value = 500
tempItem.found = 0
items(3) = tempItem

' Item 4: Magic Ring
tempItem = NEW Item()
tempItem.name = "Magic Ring"
tempItem.itemType = 1
tempItem.value = 200
tempItem.found = 0
items(4) = tempItem

' Simulate game progression
PRINT "=== GAME START ==="
PRINT ""
PRINT "Player: "; player.name
PRINT "Health: "; player.health; "/"; player.maxHealth
PRINT "Attack: "; player.attack; "  Defense: "; player.defense
PRINT "Level: "; player.level; "  XP: "; player.xp
PRINT "Gold: "; player.gold
PRINT ""

' Simulate encounters
DIM encounterCount AS INTEGER
encounterCount = 5

FOR i = 0 TO encounterCount - 1
    PRINT "--- Encounter "; i + 1; " ---"

    ' Select a monster (cycling through)
    DIM monsterIndex AS INTEGER
    monsterIndex = i
    ' Modulo workaround
    DO WHILE monsterIndex >= 5
        monsterIndex = monsterIndex - 5
    LOOP

    tempMonster = monsters(monsterIndex)

    PRINT "A "; tempMonster.name; " appears!"
    PRINT "  Monster Health: "; tempMonster.health
    PRINT "  Monster Damage: "; tempMonster.damage
    PRINT "  XP Reward: "; tempMonster.xp

    ' Simulate combat (without IF, we'll just show results)
    PRINT "  >> You attack for "; player.attack; " damage!"
    PRINT "  >> The "; tempMonster.name; " attacks for "; tempMonster.damage; " damage!"

    ' Update stats
    player.health = player.health - tempMonster.damage
    player.xp = player.xp + tempMonster.xp
    player.gold = player.gold + (tempMonster.xp * 2)
    stats.monstersDefeated = stats.monstersDefeated + 1
    stats.goldEarned = stats.goldEarned + (tempMonster.xp * 2)
    stats.turnsPlayed = stats.turnsPlayed + 1

    PRINT "  >> Victory! You gained "; tempMonster.xp; " XP and "; tempMonster.xp * 2; " gold!"
    PRINT "  Current Health: "; player.health; "/"; player.maxHealth
    PRINT "  Current XP: "; player.xp
    PRINT ""
NEXT i

' Find items
PRINT "--- Treasure Hunting ---"
FOR i = 0 TO 4
    tempItem = items(i)
    PRINT "You found: "; tempItem.name; " (value: "; tempItem.value; ")"
    tempItem.found = 1
    items(i) = tempItem
    stats.itemsCollected = stats.itemsCollected + 1
NEXT i
PRINT ""

' Final statistics
PrintBanner()
PrintCentered("ADVENTURE COMPLETE!")
PrintBanner()
PRINT ""
PRINT "=== PLAYER STATS ==="
PRINT "Name: "; player.name
PRINT "Final Health: "; player.health; "/"; player.maxHealth
PRINT "Level: "; player.level
PRINT "XP: "; player.xp
PRINT "Gold: "; player.gold
PRINT "Attack: "; player.attack
PRINT "Defense: "; player.defense
PRINT ""
PRINT "=== GAME STATS ==="
PRINT "Monsters Defeated: "; stats.monstersDefeated
PRINT "Items Collected: "; stats.itemsCollected
PRINT "Gold Earned: "; stats.goldEarned
PRINT "Turns Played: "; stats.turnsPlayed
PRINT ""
PrintBanner()
PRINT "Thank you for playing!"

END
