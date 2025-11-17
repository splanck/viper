REM ═══════════════════════════════════════════════════════
REM  FARM CLASSES MODULE - For AddFile testing
REM ═══════════════════════════════════════════════════════

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
    END SUB

    SUB Grow()
        IF ME.isPlanted THEN
            ME.growthStage = ME.growthStage + 1
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
            ME.isPlanted = 0
            ME.growthStage = 0
            RETURN ME.value
        END IF
        RETURN 0
    END FUNCTION
END CLASS

CLASS Animal
    name AS STRING
    animalType AS STRING
    hunger AS INTEGER
    productionCounter AS INTEGER
    productionRate AS INTEGER
    productValue AS INTEGER

    SUB Init(aType AS STRING, aName AS STRING, prodRate AS INTEGER, prodValue AS INTEGER)
        ME.animalType = aType
        ME.name = aName
        ME.hunger = 0
        ME.productionCounter = 0
        ME.productionRate = prodRate
        ME.productValue = prodValue
    END SUB

    SUB Feed()
        ME.hunger = 0
    END SUB

    SUB DailyUpdate()
        ME.hunger = ME.hunger + 1
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
            RETURN ME.productValue
        END IF
        RETURN 0
    END FUNCTION
END CLASS

CLASS Farmer
    name AS STRING
    money AS INTEGER
    energy AS INTEGER
    day AS INTEGER

    SUB Init(farmerName AS STRING)
        ME.name = farmerName
        ME.money = 100
        ME.energy = 100
        ME.day = 1
    END SUB

    SUB NewDay()
        ME.day = ME.day + 1
        ME.energy = 100
    END SUB

    SUB Work(amount AS INTEGER)
        ME.energy = ME.energy - amount
        IF ME.energy < 0 THEN
            ME.energy = 0
        END IF
    END SUB

    SUB Earn(amount AS INTEGER)
        ME.money = ME.money + amount
    END SUB

    SUB Spend(amount AS INTEGER)
        ME.money = ME.money - amount
    END SUB

    FUNCTION CanWork() AS INTEGER
        IF ME.energy > 0 THEN
            RETURN 1
        END IF
        RETURN 0
    END FUNCTION
END CLASS

PRINT "✓ Farm classes loaded via AddFile"
