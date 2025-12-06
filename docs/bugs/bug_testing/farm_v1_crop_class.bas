REM ╔════════════════════════════════════════════════════════╗
REM ║     FARM SIMULATOR - Crop Class Test                  ║
REM ╚════════════════════════════════════════════════════════╝

CLASS Crop
    name AS STRING
    growthStage AS INTEGER
    daysToHarvest AS INTEGER
    value AS INTEGER
    isPlanted AS INTEGER

    SUB Init(cropName AS STRING, growthDays AS INTEGER, cropValue AS INTEGER)
        ME.name = cropName
        ME.growthStage = 0
        ME.daysToHarvest = growthDays
        ME.value = cropValue
        ME.isPlanted = 0
    END SUB

    SUB Plant()
        ME.isPlanted = 1
        ME.growthStage = 0
        COLOR 10, 0
        PRINT "Planted "; ME.name; "!"
        COLOR 15, 0
    END SUB

    SUB Grow()
        IF ME.isPlanted THEN
            ME.growthStage = ME.growthStage + 1
            IF ME.growthStage >= ME.daysToHarvest THEN
                COLOR 11, 0
                PRINT ME.name; " is ready to harvest!"
                COLOR 15, 0
            ELSE
                COLOR 8, 0
                PRINT ME.name; " growing... ("; ME.growthStage; "/"; ME.daysToHarvest; ")"
                COLOR 15, 0
            END IF
        END IF
    END SUB

    FUNCTION CanHarvest() AS INTEGER
        IF ME.isPlanted AND ME.growthStage >= ME.daysToHarvest THEN
            RETURN 1
        END IF
        RETURN 0
    END FUNCTION

    FUNCTION Harvest() AS INTEGER
        IF ME.CanHarvest() THEN
            COLOR 10, 0
            PRINT "Harvested "; ME.name; "! Worth $"; ME.value
            COLOR 15, 0
            ME.isPlanted = 0
            ME.growthStage = 0
            RETURN ME.value
        END IF
        PRINT "Crop not ready to harvest."
        RETURN 0
    END FUNCTION

    SUB ShowStatus()
        IF ME.isPlanted THEN
            COLOR 14, 0
            PRINT ME.name; ": ";
            COLOR 15, 0

            REM Show growth progress with visual indicator
            DIM i AS INTEGER
            FOR i = 1 TO ME.daysToHarvest
                IF i <= ME.growthStage THEN
                    COLOR 10, 0
                    PRINT "█";
                ELSE
                    COLOR 8, 0
                    PRINT "░";
                END IF
            NEXT i
            COLOR 15, 0

            IF ME.CanHarvest() THEN
                COLOR 11, 0
                PRINT " ✓ READY"
                COLOR 15, 0
            ELSE
                PRINT " (Day "; ME.growthStage; "/"; ME.daysToHarvest; ")"
            END IF
        ELSE
            COLOR 8, 0
            PRINT ME.name; ": [Empty plot]"
            COLOR 15, 0
        END IF
    END SUB
END CLASS

REM ═══ TEST CROP CLASS ═══

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         CROP CLASS STRESS TEST                         ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

DIM wheat AS Crop
DIM corn AS Crop
DIM tomato AS Crop

wheat = NEW Crop()
corn = NEW Crop()
tomato = NEW Crop()

wheat.Init("Wheat", 3, 50)
corn.Init("Corn", 5, 80)
tomato.Init("Tomato", 4, 60)

PRINT "Planting crops..."
wheat.Plant()
corn.Plant()
tomato.Plant()
PRINT

REM Simulate growth cycle
DIM day AS INTEGER
FOR day = 1 TO 6
    COLOR 14, 0
    PRINT "═══ Day "; day; " ═══"
    COLOR 15, 0

    wheat.Grow()
    corn.Grow()
    tomato.Grow()
    PRINT

    PRINT "Field Status:"
    wheat.ShowStatus()
    corn.ShowStatus()
    tomato.ShowStatus()
    PRINT

    REM Harvest when ready
    DIM earnings AS INTEGER
    earnings = 0

    earnings = wheat.Harvest()
    earnings = earnings + corn.Harvest()
    earnings = earnings + tomato.Harvest()

    IF earnings > 0 THEN
        COLOR 11, 0
        PRINT "Daily earnings: $"; earnings
        COLOR 15, 0
    END IF
    PRINT
NEXT day

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║  CROP CLASS TEST COMPLETE!                             ║"
PRINT "╚════════════════════════════════════════════════════════╝"
