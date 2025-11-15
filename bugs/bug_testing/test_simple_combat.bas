CLASS Monster
    DIM health AS INTEGER
    DIM attack AS INTEGER

    SUB Init(hp AS INTEGER, atk AS INTEGER)
        health = hp
        attack = atk
    END SUB
END CLASS

DIM m AS Monster
m = NEW Monster()
m.Init(30, 8)

PRINT "Monster health: "; m.health
PRINT "Monster attack: "; m.attack

DIM damage AS INTEGER
damage = m.attack
PRINT "Damage: "; damage

DIM newHealth AS INTEGER
newHealth = m.health - damage
m.health = newHealth
PRINT "After damage: "; m.health

END
