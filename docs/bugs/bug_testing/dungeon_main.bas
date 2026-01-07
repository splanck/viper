REM ╔════════════════════════════════════════════════════════╗
REM ║          DUNGEON OF VIPER - AddFile Test              ║
REM ║     Testing multi-file BASIC program loading          ║
REM ╚════════════════════════════════════════════════════════╝

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         DUNGEON OF VIPER - Multi-File Demo            ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

PRINT "Loading game classes..."
AddFile "dungeon_classes.bas"
PRINT

REM Now we can use the classes defined in the included file!

PRINT "Creating game world..."
PRINT

REM Create player
DIM hero AS Player
hero = NEW Player()
hero.SetData("The Modular Hero")
PRINT "✓ Player created"

REM Create items
DIM sword AS Item
sword = NEW Item()
sword.SetData("Modular Blade", "A sword from another file!", 200)
PRINT "✓ Item created"

REM Create monster
DIM goblin AS Monster
goblin = NEW Monster()
goblin.SetData("File Goblin", 25, 8)
PRINT "✓ Monster created"
PRINT

PRINT "══════════════════════════════════════"
PRINT "        GAME STATE"
PRINT "══════════════════════════════════════"
PRINT

hero.DisplayStatus()
PRINT

PRINT "Items in inventory:"
sword.Display()
PRINT

PRINT "Nearby enemies:"
PRINT "  👹 "; goblin.name; " (HP: "; goblin.health; ", ATK: "; goblin.attack; ")"
PRINT

REM Quick combat test
PRINT "══════════════════════════════════════"
PRINT "        COMBAT TEST"
PRINT "══════════════════════════════════════"
PRINT

DIM round AS INTEGER
round = 1

WHILE hero.IsAlive() AND goblin.IsAlive() AND round <= 5
    PRINT "Round "; round; ":"

    REM Player attacks
    DIM dmg AS INTEGER
    dmg = hero.weapon + 3
    PRINT "  ⚔ Hero attacks for "; dmg; " damage!"
    goblin.TakeDamage(dmg)

    IF goblin.IsAlive() THEN
        PRINT "  💥 Goblin HP: "; goblin.health
        PRINT "  ⚔ Goblin counter-attacks!"
        hero.TakeDamage(goblin.attack)
        PRINT "  💥 Hero HP: "; hero.health
    ELSE
        PRINT "  ☠️  Goblin defeated!"
        hero.gold = hero.gold + 15
        PRINT "  💰 Gained 15 gold!"
    END IF

    PRINT
    round = round + 1
WEND

hero.DisplayStatus()

PRINT
PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║             AddFile FEATURE TEST COMPLETE!             ║"
PRINT "║                                                        ║"
PRINT "║  ✓ Successfully loaded external BASIC file             ║"
PRINT "║  ✓ Shared class definitions across files               ║"
PRINT "║  ✓ Created instances of externally-defined classes     ║"
PRINT "║  ✓ Called methods on multi-file objects                ║"
PRINT "╚════════════════════════════════════════════════════════╝"
