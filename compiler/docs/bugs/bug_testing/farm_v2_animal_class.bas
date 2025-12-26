REM ╔════════════════════════════════════════════════════════╗
REM ║     FARM SIMULATOR - Animal Class Test                ║
REM ╚════════════════════════════════════════════════════════╝

CLASS Animal
    name AS STRING
    animalType AS STRING
    hunger AS INTEGER
    happiness AS INTEGER
    productionCounter AS INTEGER
    productionRate AS INTEGER
    productValue AS INTEGER

    SUB Init(aType AS STRING, aName AS STRING, prodRate AS INTEGER, prodValue AS INTEGER)
        ME.animalType = aType
        ME.name = aName
        ME.hunger = 0
        ME.happiness = 100
        ME.productionCounter = 0
        ME.productionRate = prodRate
        ME.productValue = prodValue
    END SUB

    SUB Feed()
        ME.hunger = 0
        ME.happiness = ME.happiness + 10
        IF ME.happiness > 100 THEN
            ME.happiness = 100
        END IF
        COLOR 10, 0
        PRINT ME.name; " fed! Happiness: "; ME.happiness
        COLOR 15, 0
    END SUB

    SUB DailyUpdate()
        REM Animals get hungry and less happy each day
        ME.hunger = ME.hunger + 1
        IF ME.hunger > 3 THEN
            ME.happiness = ME.happiness - 20
            IF ME.happiness < 0 THEN
                ME.happiness = 0
            END IF
        END IF

        REM Production counter
        IF ME.hunger <= 2 THEN
            ME.productionCounter = ME.productionCounter + 1
        END IF
    END SUB

    FUNCTION CanProduce() AS INTEGER
        IF ME.productionCounter >= ME.productionRate THEN
            RETURN 1
        END IF
        RETURN 0
    END FUNCTION

    FUNCTION Collect() AS INTEGER
        IF ME.CanProduce() THEN
            ME.productionCounter = 0
            COLOR 11, 0

            IF ME.animalType = "Chicken" THEN
                PRINT "Collected eggs from "; ME.name; "! Worth $"; ME.productValue
            ELSEIF ME.animalType = "Cow" THEN
                PRINT "Milked "; ME.name; "! Worth $"; ME.productValue
            ELSEIF ME.animalType = "Sheep" THEN
                PRINT "Sheared "; ME.name; "! Worth $"; ME.productValue
            END IF

            COLOR 15, 0
            RETURN ME.productValue
        END IF
        RETURN 0
    END FUNCTION

    SUB ShowStatus()
        COLOR 14, 0
        PRINT ME.name; " ("; ME.animalType; ")"
        COLOR 15, 0

        REM Happiness bar
        PRINT "  Happiness: ";
        DIM i AS INTEGER
        FOR i = 1 TO 10
            IF i <= ME.happiness / 10 THEN
                COLOR 10, 0
                PRINT "♥";
            ELSE
                COLOR 8, 0
                PRINT "♡";
            END IF
        NEXT i
        COLOR 15, 0
        PRINT " ("; ME.happiness; "%)"

        REM Hunger indicator
        PRINT "  Hunger: ";
        IF ME.hunger = 0 THEN
            COLOR 10, 0
            PRINT "Fed"
        ELSEIF ME.hunger <= 2 THEN
            COLOR 11, 0
            PRINT "Hungry ("; ME.hunger; ")"
        ELSE
            COLOR 12, 0
            PRINT "STARVING! ("; ME.hunger; ")"
        END IF
        COLOR 15, 0

        REM Production status
        PRINT "  Production: ";
        FOR i = 1 TO ME.productionRate
            IF i <= ME.productionCounter THEN
                COLOR 11, 0
                PRINT "●";
            ELSE
                COLOR 8, 0
                PRINT "○";
            END IF
        NEXT i
        COLOR 15, 0

        IF ME.CanProduce() THEN
            COLOR 11, 0
            PRINT " ✓ READY"
            COLOR 15, 0
        ELSE
            PRINT " ("; ME.productionCounter; "/"; ME.productionRate; ")"
        END IF
    END SUB
END CLASS

REM ═══ TEST ANIMAL CLASS ═══

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         ANIMAL CLASS STRESS TEST                       ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

DIM chicken1 AS Animal
DIM cow1 AS Animal
DIM sheep1 AS Animal

chicken1 = NEW Animal()
cow1 = NEW Animal()
sheep1 = NEW Animal()

chicken1.Init("Chicken", "Henrietta", 2, 30)
cow1.Init("Cow", "Bessie", 3, 50)
sheep1.Init("Sheep", "Woolly", 4, 40)

PRINT "Animals acquired!"
PRINT

REM Simulate farm days
DIM day AS INTEGER
FOR day = 1 TO 8
    COLOR 14, 0
    PRINT "╔════════════════════════════════════════════════════════╗"
    PRINT "║ Day "; day; "                                                    "
    PRINT "╚════════════════════════════════════════════════════════╝"
    COLOR 15, 0

    REM Feed animals on certain days
    IF day = 1 OR day = 3 OR day = 5 OR day = 7 THEN
        PRINT "Morning: Feeding animals..."
        chicken1.Feed()
        cow1.Feed()
        sheep1.Feed()
        PRINT
    END IF

    REM Daily update
    chicken1.DailyUpdate()
    cow1.DailyUpdate()
    sheep1.DailyUpdate()

    PRINT "Barn Status:"
    chicken1.ShowStatus()
    PRINT
    cow1.ShowStatus()
    PRINT
    sheep1.ShowStatus()
    PRINT

    REM Collect products
    PRINT "Collecting products..."
    DIM earnings AS INTEGER
    earnings = 0
    earnings = earnings + chicken1.Collect()
    earnings = earnings + cow1.Collect()
    earnings = earnings + sheep1.Collect()

    IF earnings > 0 THEN
        COLOR 11, 0
        PRINT "Daily income: $"; earnings
        COLOR 15, 0
    ELSE
        COLOR 8, 0
        PRINT "No products ready today."
        COLOR 15, 0
    END IF
    PRINT
NEXT day

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║  ANIMAL CLASS TEST COMPLETE!                           ║"
PRINT "╚════════════════════════════════════════════════════════╝"
