REM ====================================================================
REM MONOPOLY - Board Data and Card Definitions
REM All 40 board spaces and 32 cards (16 Chance, 16 Community Chest)
REM ====================================================================

REM Property group constants
DIM GROUP_BROWN AS INTEGER
DIM GROUP_LTBLUE AS INTEGER
DIM GROUP_PINK AS INTEGER
DIM GROUP_ORANGE AS INTEGER
DIM GROUP_RED AS INTEGER
DIM GROUP_YELLOW AS INTEGER
DIM GROUP_GREEN AS INTEGER
DIM GROUP_DKBLUE AS INTEGER
DIM GROUP_RAILROAD AS INTEGER
DIM GROUP_UTILITY AS INTEGER
DIM GROUP_SPECIAL AS INTEGER

GROUP_BROWN = 0
GROUP_LTBLUE = 1
GROUP_PINK = 2
GROUP_ORANGE = 3
GROUP_RED = 4
GROUP_YELLOW = 5
GROUP_GREEN = 6
GROUP_DKBLUE = 7
GROUP_RAILROAD = 8
GROUP_UTILITY = 9
GROUP_SPECIAL = 10

REM Space type constants
DIM SPACE_PROPERTY AS INTEGER
DIM SPACE_RAILROAD AS INTEGER
DIM SPACE_UTILITY AS INTEGER
DIM SPACE_CHANCE AS INTEGER
DIM SPACE_CHEST AS INTEGER
DIM SPACE_TAX AS INTEGER
DIM SPACE_GO AS INTEGER
DIM SPACE_JAIL AS INTEGER
DIM SPACE_PARKING AS INTEGER
DIM SPACE_GOTOJAIL AS INTEGER

SPACE_PROPERTY = 0
SPACE_RAILROAD = 1
SPACE_UTILITY = 2
SPACE_CHANCE = 3
SPACE_CHEST = 4
SPACE_TAX = 5
SPACE_GO = 6
SPACE_JAIL = 7
SPACE_PARKING = 8
SPACE_GOTOJAIL = 9

REM Card action constants
DIM CARD_MONEY AS INTEGER
DIM CARD_MOVE AS INTEGER
DIM CARD_MOVETO AS INTEGER
DIM CARD_JAIL AS INTEGER
DIM CARD_JAILFREE AS INTEGER
DIM CARD_REPAIRS AS INTEGER
DIM CARD_PAYEACH AS INTEGER
DIM CARD_COLLECTEACH AS INTEGER
DIM CARD_NEARRR AS INTEGER
DIM CARD_NEARUTIL AS INTEGER

CARD_MONEY = 0
CARD_MOVE = 1
CARD_MOVETO = 2
CARD_JAIL = 3
CARD_JAILFREE = 4
CARD_REPAIRS = 5
CARD_PAYEACH = 6
CARD_COLLECTEACH = 7
CARD_NEARRR = 8
CARD_NEARUTIL = 9

REM Global game data lists
DIM properties AS Viper.Collections.List
DIM boardSpaces AS Viper.Collections.List
DIM chanceCards AS Viper.Collections.List
DIM chestCards AS Viper.Collections.List
DIM players AS Viper.Collections.List

REM Property index lookup by position (for quick access)
REM Using individual vars due to array access issues
DIM propIdxPos0 AS INTEGER
DIM propIdxPos1 AS INTEGER
DIM propIdxPos3 AS INTEGER
DIM propIdxPos5 AS INTEGER
DIM propIdxPos6 AS INTEGER
DIM propIdxPos8 AS INTEGER
DIM propIdxPos9 AS INTEGER
DIM propIdxPos11 AS INTEGER
DIM propIdxPos12 AS INTEGER
DIM propIdxPos13 AS INTEGER
DIM propIdxPos14 AS INTEGER
DIM propIdxPos15 AS INTEGER
DIM propIdxPos16 AS INTEGER
DIM propIdxPos18 AS INTEGER
DIM propIdxPos19 AS INTEGER
DIM propIdxPos21 AS INTEGER
DIM propIdxPos23 AS INTEGER
DIM propIdxPos24 AS INTEGER
DIM propIdxPos25 AS INTEGER
DIM propIdxPos26 AS INTEGER
DIM propIdxPos27 AS INTEGER
DIM propIdxPos28 AS INTEGER
DIM propIdxPos29 AS INTEGER
DIM propIdxPos31 AS INTEGER
DIM propIdxPos32 AS INTEGER
DIM propIdxPos34 AS INTEGER
DIM propIdxPos35 AS INTEGER
DIM propIdxPos37 AS INTEGER
DIM propIdxPos39 AS INTEGER

REM ====================================================================
REM Initialize all properties (28 total: 22 streets, 4 railroads, 2 utilities)
REM ====================================================================
SUB InitProperties()
    DIM p AS PropData
    DIM idx AS INTEGER

    properties = NEW Viper.Collections.List()
    idx = 0

    REM Mediterranean Avenue (Brown)
    p = NEW PropData()
    p.Init(1, "Mediterranean Ave", GROUP_BROWN, 60)
    p.SetRents(2, 10, 30, 90, 160, 250)
    p.SetHouseCost(50)
    properties.Add(p)
    propIdxPos1 = idx
    idx = idx + 1

    REM Baltic Avenue (Brown)
    p = NEW PropData()
    p.Init(3, "Baltic Ave", GROUP_BROWN, 60)
    p.SetRents(4, 20, 60, 180, 320, 450)
    p.SetHouseCost(50)
    properties.Add(p)
    propIdxPos3 = idx
    idx = idx + 1

    REM Reading Railroad
    p = NEW PropData()
    p.Init(5, "Reading Railroad", GROUP_RAILROAD, 200)
    p.SetRents(25, 50, 100, 200, 0, 0)
    properties.Add(p)
    propIdxPos5 = idx
    idx = idx + 1

    REM Oriental Avenue (Light Blue)
    p = NEW PropData()
    p.Init(6, "Oriental Ave", GROUP_LTBLUE, 100)
    p.SetRents(6, 30, 90, 270, 400, 550)
    p.SetHouseCost(50)
    properties.Add(p)
    propIdxPos6 = idx
    idx = idx + 1

    REM Vermont Avenue (Light Blue)
    p = NEW PropData()
    p.Init(8, "Vermont Ave", GROUP_LTBLUE, 100)
    p.SetRents(6, 30, 90, 270, 400, 550)
    p.SetHouseCost(50)
    properties.Add(p)
    propIdxPos8 = idx
    idx = idx + 1

    REM Connecticut Avenue (Light Blue)
    p = NEW PropData()
    p.Init(9, "Connecticut Ave", GROUP_LTBLUE, 120)
    p.SetRents(8, 40, 100, 300, 450, 600)
    p.SetHouseCost(50)
    properties.Add(p)
    propIdxPos9 = idx
    idx = idx + 1

    REM St. Charles Place (Pink)
    p = NEW PropData()
    p.Init(11, "St. Charles Place", GROUP_PINK, 140)
    p.SetRents(10, 50, 150, 450, 625, 750)
    p.SetHouseCost(100)
    properties.Add(p)
    propIdxPos11 = idx
    idx = idx + 1

    REM Electric Company (Utility)
    p = NEW PropData()
    p.Init(12, "Electric Company", GROUP_UTILITY, 150)
    p.SetRents(4, 10, 0, 0, 0, 0)
    properties.Add(p)
    propIdxPos12 = idx
    idx = idx + 1

    REM States Avenue (Pink)
    p = NEW PropData()
    p.Init(13, "States Ave", GROUP_PINK, 140)
    p.SetRents(10, 50, 150, 450, 625, 750)
    p.SetHouseCost(100)
    properties.Add(p)
    propIdxPos13 = idx
    idx = idx + 1

    REM Virginia Avenue (Pink)
    p = NEW PropData()
    p.Init(14, "Virginia Ave", GROUP_PINK, 160)
    p.SetRents(12, 60, 180, 500, 700, 900)
    p.SetHouseCost(100)
    properties.Add(p)
    propIdxPos14 = idx
    idx = idx + 1

    REM Pennsylvania Railroad
    p = NEW PropData()
    p.Init(15, "Pennsylvania RR", GROUP_RAILROAD, 200)
    p.SetRents(25, 50, 100, 200, 0, 0)
    properties.Add(p)
    propIdxPos15 = idx
    idx = idx + 1

    REM St. James Place (Orange)
    p = NEW PropData()
    p.Init(16, "St. James Place", GROUP_ORANGE, 180)
    p.SetRents(14, 70, 200, 550, 750, 950)
    p.SetHouseCost(100)
    properties.Add(p)
    propIdxPos16 = idx
    idx = idx + 1

    REM Tennessee Avenue (Orange)
    p = NEW PropData()
    p.Init(18, "Tennessee Ave", GROUP_ORANGE, 180)
    p.SetRents(14, 70, 200, 550, 750, 950)
    p.SetHouseCost(100)
    properties.Add(p)
    propIdxPos18 = idx
    idx = idx + 1

    REM New York Avenue (Orange)
    p = NEW PropData()
    p.Init(19, "New York Ave", GROUP_ORANGE, 200)
    p.SetRents(16, 80, 220, 600, 800, 1000)
    p.SetHouseCost(100)
    properties.Add(p)
    propIdxPos19 = idx
    idx = idx + 1

    REM Kentucky Avenue (Red)
    p = NEW PropData()
    p.Init(21, "Kentucky Ave", GROUP_RED, 220)
    p.SetRents(18, 90, 250, 700, 875, 1050)
    p.SetHouseCost(150)
    properties.Add(p)
    propIdxPos21 = idx
    idx = idx + 1

    REM Indiana Avenue (Red)
    p = NEW PropData()
    p.Init(23, "Indiana Ave", GROUP_RED, 220)
    p.SetRents(18, 90, 250, 700, 875, 1050)
    p.SetHouseCost(150)
    properties.Add(p)
    propIdxPos23 = idx
    idx = idx + 1

    REM Illinois Avenue (Red)
    p = NEW PropData()
    p.Init(24, "Illinois Ave", GROUP_RED, 240)
    p.SetRents(20, 100, 300, 750, 925, 1100)
    p.SetHouseCost(150)
    properties.Add(p)
    propIdxPos24 = idx
    idx = idx + 1

    REM B&O Railroad
    p = NEW PropData()
    p.Init(25, "B&O Railroad", GROUP_RAILROAD, 200)
    p.SetRents(25, 50, 100, 200, 0, 0)
    properties.Add(p)
    propIdxPos25 = idx
    idx = idx + 1

    REM Atlantic Avenue (Yellow)
    p = NEW PropData()
    p.Init(26, "Atlantic Ave", GROUP_YELLOW, 260)
    p.SetRents(22, 110, 330, 800, 975, 1150)
    p.SetHouseCost(150)
    properties.Add(p)
    propIdxPos26 = idx
    idx = idx + 1

    REM Ventnor Avenue (Yellow)
    p = NEW PropData()
    p.Init(27, "Ventnor Ave", GROUP_YELLOW, 260)
    p.SetRents(22, 110, 330, 800, 975, 1150)
    p.SetHouseCost(150)
    properties.Add(p)
    propIdxPos27 = idx
    idx = idx + 1

    REM Water Works (Utility)
    p = NEW PropData()
    p.Init(28, "Water Works", GROUP_UTILITY, 150)
    p.SetRents(4, 10, 0, 0, 0, 0)
    properties.Add(p)
    propIdxPos28 = idx
    idx = idx + 1

    REM Marvin Gardens (Yellow)
    p = NEW PropData()
    p.Init(29, "Marvin Gardens", GROUP_YELLOW, 280)
    p.SetRents(24, 120, 360, 850, 1025, 1200)
    p.SetHouseCost(150)
    properties.Add(p)
    propIdxPos29 = idx
    idx = idx + 1

    REM Pacific Avenue (Green)
    p = NEW PropData()
    p.Init(31, "Pacific Ave", GROUP_GREEN, 300)
    p.SetRents(26, 130, 390, 900, 1100, 1275)
    p.SetHouseCost(200)
    properties.Add(p)
    propIdxPos31 = idx
    idx = idx + 1

    REM North Carolina Avenue (Green)
    p = NEW PropData()
    p.Init(32, "N. Carolina Ave", GROUP_GREEN, 300)
    p.SetRents(26, 130, 390, 900, 1100, 1275)
    p.SetHouseCost(200)
    properties.Add(p)
    propIdxPos32 = idx
    idx = idx + 1

    REM Pennsylvania Avenue (Green)
    p = NEW PropData()
    p.Init(34, "Pennsylvania Ave", GROUP_GREEN, 320)
    p.SetRents(28, 150, 450, 1000, 1200, 1400)
    p.SetHouseCost(200)
    properties.Add(p)
    propIdxPos34 = idx
    idx = idx + 1

    REM Short Line Railroad
    p = NEW PropData()
    p.Init(35, "Short Line RR", GROUP_RAILROAD, 200)
    p.SetRents(25, 50, 100, 200, 0, 0)
    properties.Add(p)
    propIdxPos35 = idx
    idx = idx + 1

    REM Park Place (Dark Blue)
    p = NEW PropData()
    p.Init(37, "Park Place", GROUP_DKBLUE, 350)
    p.SetRents(35, 175, 500, 1100, 1300, 1500)
    p.SetHouseCost(200)
    properties.Add(p)
    propIdxPos37 = idx
    idx = idx + 1

    REM Boardwalk (Dark Blue)
    p = NEW PropData()
    p.Init(39, "Boardwalk", GROUP_DKBLUE, 400)
    p.SetRents(50, 200, 600, 1400, 1700, 2000)
    p.SetHouseCost(200)
    properties.Add(p)
    propIdxPos39 = idx
END SUB

REM ====================================================================
REM Get property index for a board position
REM ====================================================================
FUNCTION GetPropertyIndexAt(pos AS INTEGER) AS INTEGER
    DIM result AS INTEGER
    result = -1
    IF pos = 1 THEN
        result = propIdxPos1
    ELSEIF pos = 3 THEN
        result = propIdxPos3
    ELSEIF pos = 5 THEN
        result = propIdxPos5
    ELSEIF pos = 6 THEN
        result = propIdxPos6
    ELSEIF pos = 8 THEN
        result = propIdxPos8
    ELSEIF pos = 9 THEN
        result = propIdxPos9
    ELSEIF pos = 11 THEN
        result = propIdxPos11
    ELSEIF pos = 12 THEN
        result = propIdxPos12
    ELSEIF pos = 13 THEN
        result = propIdxPos13
    ELSEIF pos = 14 THEN
        result = propIdxPos14
    ELSEIF pos = 15 THEN
        result = propIdxPos15
    ELSEIF pos = 16 THEN
        result = propIdxPos16
    ELSEIF pos = 18 THEN
        result = propIdxPos18
    ELSEIF pos = 19 THEN
        result = propIdxPos19
    ELSEIF pos = 21 THEN
        result = propIdxPos21
    ELSEIF pos = 23 THEN
        result = propIdxPos23
    ELSEIF pos = 24 THEN
        result = propIdxPos24
    ELSEIF pos = 25 THEN
        result = propIdxPos25
    ELSEIF pos = 26 THEN
        result = propIdxPos26
    ELSEIF pos = 27 THEN
        result = propIdxPos27
    ELSEIF pos = 28 THEN
        result = propIdxPos28
    ELSEIF pos = 29 THEN
        result = propIdxPos29
    ELSEIF pos = 31 THEN
        result = propIdxPos31
    ELSEIF pos = 32 THEN
        result = propIdxPos32
    ELSEIF pos = 34 THEN
        result = propIdxPos34
    ELSEIF pos = 35 THEN
        result = propIdxPos35
    ELSEIF pos = 37 THEN
        result = propIdxPos37
    ELSEIF pos = 39 THEN
        result = propIdxPos39
    END IF
    GetPropertyIndexAt = result
END FUNCTION

REM ====================================================================
REM Initialize all board spaces (40 total)
REM ====================================================================
SUB InitBoardSpaces()
    DIM s AS BoardSpace
    DIM propIdx AS INTEGER

    boardSpaces = NEW Viper.Collections.List()

    REM Position 0: GO
    s = NEW BoardSpace()
    s.Init(0, "GO", SPACE_GO, -1)
    boardSpaces.Add(s)

    REM Position 1: Mediterranean Avenue
    s = NEW BoardSpace()
    s.Init(1, "Mediterranean Ave", SPACE_PROPERTY, propIdxPos1)
    boardSpaces.Add(s)

    REM Position 2: Community Chest
    s = NEW BoardSpace()
    s.Init(2, "Community Chest", SPACE_CHEST, -1)
    boardSpaces.Add(s)

    REM Position 3: Baltic Avenue
    s = NEW BoardSpace()
    s.Init(3, "Baltic Ave", SPACE_PROPERTY, propIdxPos3)
    boardSpaces.Add(s)

    REM Position 4: Income Tax
    s = NEW BoardSpace()
    s.Init(4, "Income Tax", SPACE_TAX, -1)
    boardSpaces.Add(s)

    REM Position 5: Reading Railroad
    s = NEW BoardSpace()
    s.Init(5, "Reading RR", SPACE_RAILROAD, propIdxPos5)
    boardSpaces.Add(s)

    REM Position 6: Oriental Avenue
    s = NEW BoardSpace()
    s.Init(6, "Oriental Ave", SPACE_PROPERTY, propIdxPos6)
    boardSpaces.Add(s)

    REM Position 7: Chance
    s = NEW BoardSpace()
    s.Init(7, "Chance", SPACE_CHANCE, -1)
    boardSpaces.Add(s)

    REM Position 8: Vermont Avenue
    s = NEW BoardSpace()
    s.Init(8, "Vermont Ave", SPACE_PROPERTY, propIdxPos8)
    boardSpaces.Add(s)

    REM Position 9: Connecticut Avenue
    s = NEW BoardSpace()
    s.Init(9, "Connecticut Ave", SPACE_PROPERTY, propIdxPos9)
    boardSpaces.Add(s)

    REM Position 10: Jail/Just Visiting
    s = NEW BoardSpace()
    s.Init(10, "Jail", SPACE_JAIL, -1)
    boardSpaces.Add(s)

    REM Position 11: St. Charles Place
    s = NEW BoardSpace()
    s.Init(11, "St. Charles Pl", SPACE_PROPERTY, propIdxPos11)
    boardSpaces.Add(s)

    REM Position 12: Electric Company
    s = NEW BoardSpace()
    s.Init(12, "Electric Co", SPACE_UTILITY, propIdxPos12)
    boardSpaces.Add(s)

    REM Position 13: States Avenue
    s = NEW BoardSpace()
    s.Init(13, "States Ave", SPACE_PROPERTY, propIdxPos13)
    boardSpaces.Add(s)

    REM Position 14: Virginia Avenue
    s = NEW BoardSpace()
    s.Init(14, "Virginia Ave", SPACE_PROPERTY, propIdxPos14)
    boardSpaces.Add(s)

    REM Position 15: Pennsylvania Railroad
    s = NEW BoardSpace()
    s.Init(15, "Pennsylvania RR", SPACE_RAILROAD, propIdxPos15)
    boardSpaces.Add(s)

    REM Position 16: St. James Place
    s = NEW BoardSpace()
    s.Init(16, "St. James Pl", SPACE_PROPERTY, propIdxPos16)
    boardSpaces.Add(s)

    REM Position 17: Community Chest
    s = NEW BoardSpace()
    s.Init(17, "Community Chest", SPACE_CHEST, -1)
    boardSpaces.Add(s)

    REM Position 18: Tennessee Avenue
    s = NEW BoardSpace()
    s.Init(18, "Tennessee Ave", SPACE_PROPERTY, propIdxPos18)
    boardSpaces.Add(s)

    REM Position 19: New York Avenue
    s = NEW BoardSpace()
    s.Init(19, "New York Ave", SPACE_PROPERTY, propIdxPos19)
    boardSpaces.Add(s)

    REM Position 20: Free Parking
    s = NEW BoardSpace()
    s.Init(20, "Free Parking", SPACE_PARKING, -1)
    boardSpaces.Add(s)

    REM Position 21: Kentucky Avenue
    s = NEW BoardSpace()
    s.Init(21, "Kentucky Ave", SPACE_PROPERTY, propIdxPos21)
    boardSpaces.Add(s)

    REM Position 22: Chance
    s = NEW BoardSpace()
    s.Init(22, "Chance", SPACE_CHANCE, -1)
    boardSpaces.Add(s)

    REM Position 23: Indiana Avenue
    s = NEW BoardSpace()
    s.Init(23, "Indiana Ave", SPACE_PROPERTY, propIdxPos23)
    boardSpaces.Add(s)

    REM Position 24: Illinois Avenue
    s = NEW BoardSpace()
    s.Init(24, "Illinois Ave", SPACE_PROPERTY, propIdxPos24)
    boardSpaces.Add(s)

    REM Position 25: B&O Railroad
    s = NEW BoardSpace()
    s.Init(25, "B&O RR", SPACE_RAILROAD, propIdxPos25)
    boardSpaces.Add(s)

    REM Position 26: Atlantic Avenue
    s = NEW BoardSpace()
    s.Init(26, "Atlantic Ave", SPACE_PROPERTY, propIdxPos26)
    boardSpaces.Add(s)

    REM Position 27: Ventnor Avenue
    s = NEW BoardSpace()
    s.Init(27, "Ventnor Ave", SPACE_PROPERTY, propIdxPos27)
    boardSpaces.Add(s)

    REM Position 28: Water Works
    s = NEW BoardSpace()
    s.Init(28, "Water Works", SPACE_UTILITY, propIdxPos28)
    boardSpaces.Add(s)

    REM Position 29: Marvin Gardens
    s = NEW BoardSpace()
    s.Init(29, "Marvin Gardens", SPACE_PROPERTY, propIdxPos29)
    boardSpaces.Add(s)

    REM Position 30: Go To Jail
    s = NEW BoardSpace()
    s.Init(30, "Go To Jail", SPACE_GOTOJAIL, -1)
    boardSpaces.Add(s)

    REM Position 31: Pacific Avenue
    s = NEW BoardSpace()
    s.Init(31, "Pacific Ave", SPACE_PROPERTY, propIdxPos31)
    boardSpaces.Add(s)

    REM Position 32: North Carolina Avenue
    s = NEW BoardSpace()
    s.Init(32, "N. Carolina Ave", SPACE_PROPERTY, propIdxPos32)
    boardSpaces.Add(s)

    REM Position 33: Community Chest
    s = NEW BoardSpace()
    s.Init(33, "Community Chest", SPACE_CHEST, -1)
    boardSpaces.Add(s)

    REM Position 34: Pennsylvania Avenue
    s = NEW BoardSpace()
    s.Init(34, "Pennsylvania Ave", SPACE_PROPERTY, propIdxPos34)
    boardSpaces.Add(s)

    REM Position 35: Short Line Railroad
    s = NEW BoardSpace()
    s.Init(35, "Short Line RR", SPACE_RAILROAD, propIdxPos35)
    boardSpaces.Add(s)

    REM Position 36: Chance
    s = NEW BoardSpace()
    s.Init(36, "Chance", SPACE_CHANCE, -1)
    boardSpaces.Add(s)

    REM Position 37: Park Place
    s = NEW BoardSpace()
    s.Init(37, "Park Place", SPACE_PROPERTY, propIdxPos37)
    boardSpaces.Add(s)

    REM Position 38: Luxury Tax
    s = NEW BoardSpace()
    s.Init(38, "Luxury Tax", SPACE_TAX, -1)
    boardSpaces.Add(s)

    REM Position 39: Boardwalk
    s = NEW BoardSpace()
    s.Init(39, "Boardwalk", SPACE_PROPERTY, propIdxPos39)
    boardSpaces.Add(s)
END SUB

REM ====================================================================
REM Initialize Chance cards (16 cards)
REM ====================================================================
SUB InitChanceCards()
    DIM c AS Card

    chanceCards = NEW Viper.Collections.List()

    c = NEW Card()
    c.Init("Advance to Go. Collect $200.", CARD_MOVETO, 0)
    chanceCards.Add(c)

    c = NEW Card()
    c.Init("Advance to Illinois Ave.", CARD_MOVETO, 24)
    chanceCards.Add(c)

    c = NEW Card()
    c.Init("Advance to St. Charles Place.", CARD_MOVETO, 11)
    chanceCards.Add(c)

    c = NEW Card()
    c.Init("Advance to nearest Utility.", CARD_NEARUTIL, 0)
    chanceCards.Add(c)

    c = NEW Card()
    c.Init("Advance to nearest Railroad.", CARD_NEARRR, 0)
    chanceCards.Add(c)

    c = NEW Card()
    c.Init("Bank pays you dividend of $50.", CARD_MONEY, 50)
    chanceCards.Add(c)

    c = NEW Card()
    c.Init("Get Out of Jail Free.", CARD_JAILFREE, 0)
    chanceCards.Add(c)

    c = NEW Card()
    c.Init("Go Back 3 Spaces.", CARD_MOVE, -3)
    chanceCards.Add(c)

    c = NEW Card()
    c.Init("Go to Jail.", CARD_JAIL, 0)
    chanceCards.Add(c)

    c = NEW Card()
    c.Init("Make repairs: $25/house, $100/hotel.", CARD_REPAIRS, 25)
    c.SetRepairCosts(25, 100)
    chanceCards.Add(c)

    c = NEW Card()
    c.Init("Pay poor tax of $15.", CARD_MONEY, -15)
    chanceCards.Add(c)

    c = NEW Card()
    c.Init("Take a trip to Reading Railroad.", CARD_MOVETO, 5)
    chanceCards.Add(c)

    c = NEW Card()
    c.Init("Advance to Boardwalk.", CARD_MOVETO, 39)
    chanceCards.Add(c)

    c = NEW Card()
    c.Init("Pay each player $50.", CARD_PAYEACH, 50)
    chanceCards.Add(c)

    c = NEW Card()
    c.Init("Your building loan matures. Collect $150.", CARD_MONEY, 150)
    chanceCards.Add(c)

    c = NEW Card()
    c.Init("You won a crossword competition. Collect $100.", CARD_MONEY, 100)
    chanceCards.Add(c)
END SUB

REM ====================================================================
REM Initialize Community Chest cards (16 cards)
REM ====================================================================
SUB InitChestCards()
    DIM c AS Card

    chestCards = NEW Viper.Collections.List()

    c = NEW Card()
    c.Init("Advance to Go. Collect $200.", CARD_MOVETO, 0)
    chestCards.Add(c)

    c = NEW Card()
    c.Init("Bank error in your favor. Collect $200.", CARD_MONEY, 200)
    chestCards.Add(c)

    c = NEW Card()
    c.Init("Doctor's fees. Pay $50.", CARD_MONEY, -50)
    chestCards.Add(c)

    c = NEW Card()
    c.Init("From sale of stock you get $50.", CARD_MONEY, 50)
    chestCards.Add(c)

    c = NEW Card()
    c.Init("Get Out of Jail Free.", CARD_JAILFREE, 0)
    chestCards.Add(c)

    c = NEW Card()
    c.Init("Go to Jail.", CARD_JAIL, 0)
    chestCards.Add(c)

    c = NEW Card()
    c.Init("Grand Opera Night. Collect $50 from each player.", CARD_COLLECTEACH, 50)
    chestCards.Add(c)

    c = NEW Card()
    c.Init("Holiday Fund matures. Receive $100.", CARD_MONEY, 100)
    chestCards.Add(c)

    c = NEW Card()
    c.Init("Income tax refund. Collect $20.", CARD_MONEY, 20)
    chestCards.Add(c)

    c = NEW Card()
    c.Init("It is your birthday. Collect $10 from each player.", CARD_COLLECTEACH, 10)
    chestCards.Add(c)

    c = NEW Card()
    c.Init("Life insurance matures. Collect $100.", CARD_MONEY, 100)
    chestCards.Add(c)

    c = NEW Card()
    c.Init("Pay hospital fees of $100.", CARD_MONEY, -100)
    chestCards.Add(c)

    c = NEW Card()
    c.Init("Pay school fees of $50.", CARD_MONEY, -50)
    chestCards.Add(c)

    c = NEW Card()
    c.Init("Receive $25 consultancy fee.", CARD_MONEY, 25)
    chestCards.Add(c)

    c = NEW Card()
    c.Init("Repairs: $40/house, $115/hotel.", CARD_REPAIRS, 40)
    c.SetRepairCosts(40, 115)
    chestCards.Add(c)

    c = NEW Card()
    c.Init("You inherit $100.", CARD_MONEY, 100)
    chestCards.Add(c)
END SUB

REM ====================================================================
REM Shuffle cards (Fisher-Yates shuffle)
REM ====================================================================
SUB ShuffleCards(deck AS Viper.Collections.List)
    DIM i AS INTEGER
    DIM j AS INTEGER
    DIM n AS INTEGER
    DIM temp AS Card
    DIM cardI AS Card
    DIM cardJ AS Card

    n = deck.Count
    i = n - 1
    WHILE i > 0
        j = INT(RND() * (i + 1))
        IF j <> i THEN
            cardI = deck.get_Item(i)
            cardJ = deck.get_Item(j)
            deck.set_Item(i, cardJ)
            deck.set_Item(j, cardI)
        END IF
        i = i - 1
    WEND
END SUB

REM Card deck indices
DIM chanceIndex AS INTEGER
DIM chestIndex AS INTEGER

REM ====================================================================
REM Draw a Chance card
REM ====================================================================
FUNCTION DrawChanceCard() AS Card
    DIM c AS Card
    c = chanceCards.get_Item(chanceIndex)
    chanceIndex = chanceIndex + 1
    IF chanceIndex >= chanceCards.Count THEN
        chanceIndex = 0
        ShuffleCards(chanceCards)
    END IF
    DrawChanceCard = c
END FUNCTION

REM ====================================================================
REM Draw a Community Chest card
REM ====================================================================
FUNCTION DrawChestCard() AS Card
    DIM c AS Card
    c = chestCards.get_Item(chestIndex)
    chestIndex = chestIndex + 1
    IF chestIndex >= chestCards.Count THEN
        chestIndex = 0
        ShuffleCards(chestCards)
    END IF
    DrawChestCard = c
END FUNCTION

REM ====================================================================
REM Initialize all game data
REM ====================================================================
SUB InitGameData()
    chanceIndex = 0
    chestIndex = 0
    InitProperties()
    InitBoardSpaces()
    InitChanceCards()
    InitChestCards()
    ShuffleCards(chanceCards)
    ShuffleCards(chestCards)
END SUB

REM ====================================================================
REM Get group name string
REM ====================================================================
FUNCTION GetGroupName(grp AS INTEGER) AS STRING
    DIM name AS STRING
    IF grp = GROUP_BROWN THEN
        name = "Brown"
    ELSEIF grp = GROUP_LTBLUE THEN
        name = "Light Blue"
    ELSEIF grp = GROUP_PINK THEN
        name = "Pink"
    ELSEIF grp = GROUP_ORANGE THEN
        name = "Orange"
    ELSEIF grp = GROUP_RED THEN
        name = "Red"
    ELSEIF grp = GROUP_YELLOW THEN
        name = "Yellow"
    ELSEIF grp = GROUP_GREEN THEN
        name = "Green"
    ELSEIF grp = GROUP_DKBLUE THEN
        name = "Dark Blue"
    ELSEIF grp = GROUP_RAILROAD THEN
        name = "Railroad"
    ELSEIF grp = GROUP_UTILITY THEN
        name = "Utility"
    ELSE
        name = "Special"
    END IF
    GetGroupName = name
END FUNCTION

REM ====================================================================
REM Count properties in a group owned by a player
REM ====================================================================
FUNCTION CountGroupOwned(playerIdx AS INTEGER, grp AS INTEGER) AS INTEGER
    DIM i AS INTEGER
    DIM count AS INTEGER
    DIM p AS PropData

    count = 0
    i = 0
    WHILE i < properties.Count
        p = properties.get_Item(i)
        IF p.GetGroup() = grp THEN
            IF p.GetOwner() = playerIdx THEN
                count = count + 1
            END IF
        END IF
        i = i + 1
    WEND
    CountGroupOwned = count
END FUNCTION

REM ====================================================================
REM Get total properties in a group
REM ====================================================================
FUNCTION GetGroupSize(grp AS INTEGER) AS INTEGER
    DIM size AS INTEGER
    IF grp = GROUP_BROWN THEN
        size = 2
    ELSEIF grp = GROUP_DKBLUE THEN
        size = 2
    ELSEIF grp = GROUP_RAILROAD THEN
        size = 4
    ELSEIF grp = GROUP_UTILITY THEN
        size = 2
    ELSE
        size = 3
    END IF
    GetGroupSize = size
END FUNCTION

REM ====================================================================
REM Check if player owns complete color group
REM ====================================================================
FUNCTION OwnsCompleteGroup(playerIdx AS INTEGER, grp AS INTEGER) AS INTEGER
    DIM owned AS INTEGER
    DIM needed AS INTEGER
    owned = CountGroupOwned(playerIdx, grp)
    needed = GetGroupSize(grp)
    IF owned = needed THEN
        OwnsCompleteGroup = 1
    ELSE
        OwnsCompleteGroup = 0
    END IF
END FUNCTION

