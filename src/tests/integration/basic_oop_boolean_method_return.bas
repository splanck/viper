CLASS Player
    DIM health AS INTEGER
    FUNCTION IsAlive() AS BOOLEAN
        RETURN health > 0
    END FUNCTION
END CLASS

DIM p AS Player
p = NEW Player()
p.health = 3
IF p.IsAlive() THEN PRINT 1 ELSE PRINT 0
p.health = 0
IF p.IsAlive() THEN PRINT 1 ELSE PRINT 0

