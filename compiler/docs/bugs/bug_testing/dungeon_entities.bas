REM Dungeon of Viper - Entity Classes
REM Testing OOP: Classes, fields, methods, constructors

REM Item class - things the player can find and use
CLASS Item
    name AS STRING
    description AS STRING
    usable AS BOOLEAN
    value AS INTEGER

    SUB Init(itemName AS STRING, desc AS STRING, canUse AS BOOLEAN, val AS INTEGER)
        name = itemName
        description = desc
        usable = canUse
        value = val
    END SUB

    FUNCTION GetName() AS STRING
        GetName = name
    END FUNCTION

    FUNCTION GetDescription() AS STRING
        GetDescription = description
    END FUNCTION
END CLASS

REM Player class - the hero
CLASS Player
    name AS STRING
    health AS INTEGER
    maxHealth AS INTEGER
    gold AS INTEGER
    x AS INTEGER
    y AS INTEGER
    inventory(10) AS Item
    inventoryCount AS INTEGER

    SUB Init(playerName AS STRING)
        name = playerName
        health = 100
        maxHealth = 100
        gold = 0
        x = 0
        y = 0
        inventoryCount = 0
    END SUB

    SUB TakeDamage(amount AS INTEGER)
        health = health - amount
        IF health < 0 THEN
            health = 0
        END IF
    END SUB

    SUB Heal(amount AS INTEGER)
        health = health + amount
        IF health > maxHealth THEN
            health = maxHealth
        END IF
    END SUB

    FUNCTION IsAlive() AS BOOLEAN
        IsAlive = health > 0
    END FUNCTION

    FUNCTION GetHealthPercent() AS INTEGER
        IF maxHealth = 0 THEN
            GetHealthPercent = 0
        ELSE
            GetHealthPercent = (health * 100) / maxHealth
        END IF
    END FUNCTION
END CLASS

REM Test basic class instantiation
PRINT "=== Testing Entity Classes ==="
PRINT

DIM sword AS Item
CALL sword.Init("Iron Sword", "A sturdy blade", 1, 50)
PRINT "Created item: "; sword.GetName()
PRINT "Description: "; sword.GetDescription()
PRINT

DIM hero AS Player
CALL hero.Init("Brave Adventurer")
PRINT "Created player: "; hero.name
PRINT "Health: "; hero.health; "/"; hero.maxHealth
PRINT "Health %: "; hero.GetHealthPercent(); "%"
PRINT

PRINT "Testing damage..."
CALL hero.TakeDamage(30)
PRINT "Health after 30 damage: "; hero.health
PRINT "Health %: "; hero.GetHealthPercent(); "%"
PRINT "Alive? "; hero.IsAlive()
PRINT

PRINT "Testing heal..."
CALL hero.Heal(20)
PRINT "Health after healing 20: "; hero.health
PRINT

PRINT "=== Entity test complete! ==="
