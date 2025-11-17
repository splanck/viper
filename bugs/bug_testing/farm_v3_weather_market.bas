REM ╔════════════════════════════════════════════════════════╗
REM ║     FARM SIMULATOR - Weather & Market System          ║
REM ╚════════════════════════════════════════════════════════╝

CLASS Weather
    condition AS INTEGER
    temperature AS INTEGER
    rainChance AS INTEGER

    SUB Init()
        ME.condition = 0
        ME.temperature = 70
        ME.rainChance = 20
    END SUB

    SUB Generate(day AS INTEGER)
        REM Simple weather generation based on day
        ME.condition = day MOD 4
        ME.temperature = 60 + (day MOD 20)
        ME.rainChance = (day * 7) MOD 100
    END SUB

    SUB ShowForecast()
        COLOR 11, 0
        PRINT "─── Weather Forecast ───"
        COLOR 15, 0

        PRINT "Condition: ";
        IF ME.condition = 0 THEN
            COLOR 11, 0
            PRINT "☀ Sunny"
        ELSEIF ME.condition = 1 THEN
            COLOR 14, 0
            PRINT "⛅ Partly Cloudy"
        ELSEIF ME.condition = 2 THEN
            COLOR 8, 0
            PRINT "☁ Cloudy"
        ELSE
            COLOR 12, 0
            PRINT "🌧 Rainy"
        END IF
        COLOR 15, 0

        PRINT "Temperature: "; ME.temperature; "°F"
        PRINT "Rain Chance: "; ME.rainChance; "%"
    END SUB

    FUNCTION IsSunny() AS INTEGER
        IF ME.condition = 0 THEN
            RETURN 1
        END IF
        RETURN 0
    END FUNCTION

    FUNCTION IsRainy() AS INTEGER
        IF ME.condition = 3 THEN
            RETURN 1
        END IF
        RETURN 0
    END FUNCTION

    FUNCTION GrowthBonus() AS INTEGER
        REM Rainy days give growth bonus
        IF ME.IsRainy() THEN
            RETURN 1
        END IF
        REM Sunny days are normal
        IF ME.IsSunny() THEN
            RETURN 0
        END IF
        REM Cloudy days slow growth
        RETURN -1
    END FUNCTION
END CLASS

CLASS Market
    wheatPrice AS INTEGER
    cornPrice AS INTEGER
    tomatoPrice AS INTEGER
    priceIndex AS INTEGER

    SUB Init()
        ME.wheatPrice = 50
        ME.cornPrice = 80
        ME.tomatoPrice = 60
        ME.priceIndex = 100
    END SUB

    SUB UpdatePrices(day AS INTEGER)
        REM Fluctuate prices based on day
        DIM variance AS INTEGER
        variance = (day * 13) MOD 30 - 15

        ME.priceIndex = 100 + variance

        REM Calculate new prices
        ME.wheatPrice = 50 + variance / 3
        ME.cornPrice = 80 + variance / 2
        ME.tomatoPrice = 60 + variance / 2

        REM Ensure minimum prices
        IF ME.wheatPrice < 30 THEN
            ME.wheatPrice = 30
        END IF
        IF ME.cornPrice < 50 THEN
            ME.cornPrice = 50
        END IF
        IF ME.tomatoPrice < 40 THEN
            ME.tomatoPrice = 40
        END IF
    END SUB

    SUB ShowPrices()
        COLOR 11, 0
        PRINT "─── Market Prices ───"
        COLOR 15, 0

        PRINT "Market Index: "; ME.priceIndex; "%"

        IF ME.priceIndex > 110 THEN
            COLOR 10, 0
            PRINT "▲ BULL MARKET - High prices!"
        ELSEIF ME.priceIndex < 90 THEN
            COLOR 12, 0
            PRINT "▼ BEAR MARKET - Low prices!"
        ELSE
            COLOR 14, 0
            PRINT "= STABLE MARKET"
        END IF
        COLOR 15, 0

        PRINT "Wheat: $"; ME.wheatPrice
        PRINT "Corn: $"; ME.cornPrice
        PRINT "Tomato: $"; ME.tomatoPrice
    END SUB

    FUNCTION GetWheatPrice() AS INTEGER
        RETURN ME.wheatPrice
    END FUNCTION

    FUNCTION GetCornPrice() AS INTEGER
        RETURN ME.cornPrice
    END FUNCTION

    FUNCTION GetTomatoPrice() AS INTEGER
        RETURN ME.tomatoPrice
    END FUNCTION
END CLASS

REM ═══ TEST WEATHER AND MARKET SYSTEM ═══

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         WEATHER & MARKET SYSTEM TEST                   ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

DIM weather AS Weather
DIM market AS Market

weather = NEW Weather()
market = NEW Market()

weather.Init()
market.Init()

PRINT "Simulating 10 days of weather and market..."
PRINT

DIM day AS INTEGER
FOR day = 1 TO 10
    COLOR 14, 0
    PRINT "╔════════════════════════════════════════════════════════╗"
    PRINT "║ Day "; day; "                                                  "
    PRINT "╚════════════════════════════════════════════════════════╝"
    COLOR 15, 0

    REM Update weather and market
    weather.Generate(day)
    market.UpdatePrices(day)

    REM Show forecasts
    weather.ShowForecast()
    PRINT

    market.ShowPrices()
    PRINT

    REM Calculate growth effects
    DIM bonus AS INTEGER
    bonus = weather.GrowthBonus()

    IF bonus > 0 THEN
        COLOR 10, 0
        PRINT "Crops will grow faster today! (+"; bonus; ")"
        COLOR 15, 0
    ELSEIF bonus < 0 THEN
        COLOR 12, 0
        PRINT "Crops will grow slower today! ("; bonus; ")"
        COLOR 15, 0
    ELSE
        COLOR 15, 0
        PRINT "Normal crop growth today."
    END IF

    REM Simulate selling crops
    IF day MOD 3 = 0 THEN
        COLOR 11, 0
        PRINT "Selling today's harvest..."
        COLOR 15, 0

        DIM wheatEarnings AS INTEGER
        DIM cornEarnings AS INTEGER

        wheatEarnings = market.GetWheatPrice() * 2
        cornEarnings = market.GetCornPrice() * 1

        PRINT "Sold 2 wheat for $"; wheatEarnings
        PRINT "Sold 1 corn for $"; cornEarnings

        DIM total AS INTEGER
        total = wheatEarnings + cornEarnings

        COLOR 10, 0
        PRINT "Total earnings: $"; total
        COLOR 15, 0
    END IF

    PRINT
NEXT day

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║  WEATHER & MARKET TEST COMPLETE!                       ║"
PRINT "║                                                        ║"
PRINT "║  ✓ Weather system with multiple conditions            ║"
PRINT "║  ✓ Market price fluctuations                          ║"
PRINT "║  ✓ Dynamic gameplay modifiers                         ║"
PRINT "║  ✓ Complex condition chains                           ║"
PRINT "╚════════════════════════════════════════════════════════╝"
