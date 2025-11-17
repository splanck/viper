REM ╔════════════════════════════════════════════════════════╗
REM ║         HAPPY VALLEY FARM SIMULATOR                    ║
REM ╚════════════════════════════════════════════════════════╝

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         HAPPY VALLEY FARM SIMULATOR                    ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

PRINT "Loading farm classes..."
AddFile "farm_classes_module.bas"
PRINT

REM Initialize farmer
DIM player AS Farmer
player = NEW Farmer()
player.Init("Farmer Joe")

REM Initialize crops
DIM field1 AS Crop
DIM field2 AS Crop
DIM field3 AS Crop

field1 = NEW Crop()
field2 = NEW Crop()
field3 = NEW Crop()

field1.Init("Wheat", 3, 50)
field2.Init("Corn", 5, 80)
field3.Init("Tomato", 4, 60)

REM Initialize animals
DIM chicken AS Animal
DIM cow AS Animal

chicken = NEW Animal()
cow = NEW Animal()

chicken.Init("Chicken", "Clucky", 2, 30)
cow.Init("Cow", "Bessie", 3, 50)

PRINT "Welcome to Happy Valley Farm, "; player.name; "!"
PRINT "Starting Money: $"; player.money
PRINT

REM Plant initial crops
PRINT "Planting initial crops..."
field1.Plant()
field2.Plant()
field3.Plant()
PRINT "✓ Fields planted"
PRINT

REM Game loop
DIM totalEarnings AS INTEGER
totalEarnings = 0

WHILE player.day <= 12
    COLOR 14, 0
    PRINT "╔════════════════════════════════════════════════════════╗"
    PRINT "║ Day "; player.day; "                                                  "
    PRINT "╚════════════════════════════════════════════════════════╝"
    COLOR 15, 0

    REM Morning: Show status
    COLOR 11, 0
    PRINT "─── Morning Status ───"
    COLOR 15, 0
    PRINT "Money: $"; player.money; "  Energy: "; player.energy; "/100"
    PRINT

    REM Grow crops
    field1.Grow()
    field2.Grow()
    field3.Grow()

    REM Show field status
    PRINT "Fields:"
    IF field1.isPlanted THEN
        PRINT "  Field 1: "; field1.name; " ("; field1.growthStage; "/"; field1.daysToHarvest; ")";
        IF field1.CanHarvest() THEN
            COLOR 10, 0
            PRINT " ✓ READY";
            COLOR 15, 0
        END IF
        PRINT
    END IF

    IF field2.isPlanted THEN
        PRINT "  Field 2: "; field2.name; " ("; field2.growthStage; "/"; field2.daysToHarvest; ")";
        IF field2.CanHarvest() THEN
            COLOR 10, 0
            PRINT " ✓ READY";
            COLOR 15, 0
        END IF
        PRINT
    END IF

    IF field3.isPlanted THEN
        PRINT "  Field 3: "; field3.name; " ("; field3.growthStage; "/"; field3.daysToHarvest; ")";
        IF field3.CanHarvest() THEN
            COLOR 10, 0
            PRINT " ✓ READY";
            COLOR 15, 0
        END IF
        PRINT
    END IF
    PRINT

    REM Harvest ready crops
    DIM dailyEarnings AS INTEGER
    dailyEarnings = 0

    DIM crop1Value AS INTEGER
    DIM crop2Value AS INTEGER
    DIM crop3Value AS INTEGER

    crop1Value = field1.Harvest()
    crop2Value = field2.Harvest()
    crop3Value = field3.Harvest()

    IF crop1Value > 0 THEN
        COLOR 10, 0
        PRINT "Harvested Field 1! +$"; crop1Value
        COLOR 15, 0
        dailyEarnings = dailyEarnings + crop1Value
        player.Work(15)
        REM Replant
        field1.Plant()
    END IF

    IF crop2Value > 0 THEN
        COLOR 10, 0
        PRINT "Harvested Field 2! +$"; crop2Value
        COLOR 15, 0
        dailyEarnings = dailyEarnings + crop2Value
        player.Work(15)
        REM Replant
        field2.Plant()
    END IF

    IF crop3Value > 0 THEN
        COLOR 10, 0
        PRINT "Harvested Field 3! +$"; crop3Value
        COLOR 15, 0
        dailyEarnings = dailyEarnings + crop3Value
        player.Work(15)
        REM Replant
        field3.Plant()
    END IF

    REM Feed animals (costs energy)
    IF player.day MOD 2 = 0 AND player.CanWork() THEN
        PRINT "Feeding animals..."
        chicken.Feed()
        cow.Feed()
        player.Work(10)
    END IF

    REM Update animals
    chicken.DailyUpdate()
    cow.DailyUpdate()

    REM Show animal status
    PRINT "Animals:"
    PRINT "  "; chicken.name; " - Hunger: "; chicken.hunger; " Production: "; chicken.productionCounter; "/"; chicken.productionRate
    PRINT "  "; cow.name; " - Hunger: "; cow.hunger; " Production: "; cow.productionCounter; "/"; cow.productionRate
    PRINT

    REM Collect animal products
    DIM chickenValue AS INTEGER
    DIM cowValue AS INTEGER

    chickenValue = chicken.Collect()
    cowValue = cow.Collect()

    IF chickenValue > 0 THEN
        COLOR 11, 0
        PRINT "Collected eggs! +$"; chickenValue
        COLOR 15, 0
        dailyEarnings = dailyEarnings + chickenValue
    END IF

    IF cowValue > 0 THEN
        COLOR 11, 0
        PRINT "Collected milk! +$"; cowValue
        COLOR 15, 0
        dailyEarnings = dailyEarnings + cowValue
    END IF

    REM Update money
    IF dailyEarnings > 0 THEN
        player.Earn(dailyEarnings)
        totalEarnings = totalEarnings + dailyEarnings
        COLOR 11, 0
        PRINT "Daily income: $"; dailyEarnings
        COLOR 15, 0
    ELSE
        COLOR 8, 0
        PRINT "No income today."
        COLOR 15, 0
    END IF

    PRINT "End of day - Money: $"; player.money
    PRINT

    REM New day
    player.NewDay()
WEND

REM Final results
PRINT "╔════════════════════════════════════════════════════════╗"
COLOR 10, 0
PRINT "║         FARM SIMULATION COMPLETE!                      ║"
COLOR 15, 0
PRINT "╠════════════════════════════════════════════════════════╣"
PRINT "║  Final Money: $"; player.money
PRINT "║  Total Earnings: $"; totalEarnings
PRINT "║  Days Farmed: "; player.day - 1
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║  FARM GAME STRESS TEST COMPLETE!                       ║"
PRINT "║                                                        ║"
PRINT "║  ✓ AddFile multi-module loading                       ║"
PRINT "║  ✓ Three OOP classes (Farmer, Crop, Animal)           ║"
PRINT "║  ✓ Multiple object instances                          ║"
PRINT "║  ✓ Complex game loop simulation                       ║"
PRINT "║  ✓ Resource management (money, energy)                ║"
PRINT "║  ✓ Production cycles and timers                       ║"
PRINT "║  ✓ ANSI colors and formatting                         ║"
PRINT "╚════════════════════════════════════════════════════════╝"
