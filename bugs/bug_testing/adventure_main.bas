' The Dungeon of Viper - Main Game File
' Testing: AddFile, OOP, game loop, SELECT CASE

' Include color helpers
' AddFile "adventure_colors.bas"

' Game Classes
CLASS Player
    DIM name AS STRING
    DIM health AS INTEGER
    DIM maxHealth AS INTEGER
    DIM attack AS INTEGER
    DIM defense AS INTEGER
    DIM gold AS INTEGER
    DIM inventory(10) AS STRING
    DIM inventoryCount AS INTEGER

    SUB Init(playerName AS STRING)
        name = playerName
        health = 100
        maxHealth = 100
        attack = 10
        defense = 5
        gold = 0
        inventoryCount = 0
    END SUB

    SUB TakeDamage(damage AS INTEGER)
        DIM actualDamage AS INTEGER
        actualDamage = damage - defense
        IF actualDamage < 1 THEN
            actualDamage = 1
        END IF
        health = health - actualDamage
        IF health < 0 THEN
            health = 0
        END IF
    END SUB

    FUNCTION IsAlive() AS INTEGER
        IF health > 0 THEN
            RETURN 1
        ELSE
            RETURN 0
        END IF
    END FUNCTION

    SUB AddItem(item AS STRING)
        IF inventoryCount < 10 THEN
            inventory(inventoryCount) = item
            inventoryCount = inventoryCount + 1
        END IF
    END SUB
END CLASS

' Test creating player
DIM p AS Player
p = NEW Player()
p.Init("Hero")

PRINT "Player created: "; p.name
PRINT "Health: "; p.health; "/"; p.maxHealth
PRINT "Attack: "; p.attack
PRINT "Defense: "; p.defense

' Test combat
p.TakeDamage(15)
PRINT "After taking damage: "; p.health; "/"; p.maxHealth

' Test inventory
p.AddItem("Sword")
p.AddItem("Potion")
PRINT "Inventory count: "; p.inventoryCount
PRINT "Item 0: "; p.inventory(0)
PRINT "Item 1: "; p.inventory(1)

END
