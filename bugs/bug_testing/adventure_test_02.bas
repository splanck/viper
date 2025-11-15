REM Test 02: Player class with OOP and field arrays
ADDFILE "adventure_engine.bas"
ADDFILE "adventure_player.bas"

DIM p AS Player
p = NEW Player()

p.Init("Adventurer")
p.ShowStatus()
PRINT ""

PRINT "Taking 15 damage..."
p.TakeDamage(15)
p.ShowStatus()
PRINT ""

PRINT "Healing 10 HP..."
p.Heal(10)
p.ShowStatus()
PRINT ""

PRINT "Adding items..."
DIM success AS BOOLEAN
success = p.AddItem("Sword")
success = p.AddItem("Shield")
success = p.AddItem("Potion")
p.ShowInventory()
PRINT ""

PRINT "Is alive? " + STR$(p.IsAlive())
END
