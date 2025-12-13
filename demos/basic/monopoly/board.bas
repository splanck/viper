' ====================================================================
' Monopoly Board Data and Space Definitions
' ====================================================================

' Space types
DIM SPACE_GO AS INTEGER
DIM SPACE_PROPERTY AS INTEGER
DIM SPACE_RAILROAD AS INTEGER
DIM SPACE_UTILITY AS INTEGER
DIM SPACE_TAX AS INTEGER
DIM SPACE_CHANCE AS INTEGER
DIM SPACE_CHEST AS INTEGER
DIM SPACE_JAIL AS INTEGER
DIM SPACE_FREE_PARKING AS INTEGER
DIM SPACE_GO_TO_JAIL AS INTEGER

SPACE_GO = 0
SPACE_PROPERTY = 1
SPACE_RAILROAD = 2
SPACE_UTILITY = 3
SPACE_TAX = 4
SPACE_CHANCE = 5
SPACE_CHEST = 6
SPACE_JAIL = 7
SPACE_FREE_PARKING = 8
SPACE_GO_TO_JAIL = 9

' Property color groups
DIM GROUP_BROWN AS INTEGER
DIM GROUP_LIGHT_BLUE AS INTEGER
DIM GROUP_PINK AS INTEGER
DIM GROUP_ORANGE AS INTEGER
DIM GROUP_RED AS INTEGER
DIM GROUP_YELLOW AS INTEGER
DIM GROUP_GREEN AS INTEGER
DIM GROUP_DARK_BLUE AS INTEGER
DIM GROUP_RAILROAD AS INTEGER
DIM GROUP_UTILITY AS INTEGER
DIM GROUP_NONE AS INTEGER

GROUP_BROWN = 1
GROUP_LIGHT_BLUE = 2
GROUP_PINK = 3
GROUP_ORANGE = 4
GROUP_RED = 5
GROUP_YELLOW = 6
GROUP_GREEN = 7
GROUP_DARK_BLUE = 8
GROUP_RAILROAD = 9
GROUP_UTILITY = 10
GROUP_NONE = 0

' ====================================================================
' Space Class - represents a single board space
' ====================================================================
CLASS Space
    DIM mIndex AS INTEGER
    DIM mName AS STRING
    DIM mType AS INTEGER
    DIM mGroup AS INTEGER
    DIM mPrice AS INTEGER
    DIM mRent AS INTEGER
    DIM mRent1 AS INTEGER
    DIM mRent2 AS INTEGER
    DIM mRent3 AS INTEGER
    DIM mRent4 AS INTEGER
    DIM mRentHotel AS INTEGER
    DIM mHouseCost AS INTEGER
    DIM mMortgage AS INTEGER
    DIM mOwner AS INTEGER
    DIM mHouses AS INTEGER
    DIM mMortgaged AS INTEGER

    SUB Init(idx AS INTEGER, nm AS STRING, tp AS INTEGER, grp AS INTEGER)
        mIndex = idx
        mName = nm
        mType = tp
        mGroup = grp
        mPrice = 0
        mRent = 0
        mOwner = -1
        mHouses = 0
        mMortgaged = 0
    END SUB

    SUB SetPricing(price AS INTEGER, rent AS INTEGER, r1 AS INTEGER, r2 AS INTEGER, r3 AS INTEGER, r4 AS INTEGER, rH AS INTEGER, hCost AS INTEGER)
        mPrice = price
        mRent = rent
        mRent1 = r1
        mRent2 = r2
        mRent3 = r3
        mRent4 = r4
        mRentHotel = rH
        mHouseCost = hCost
        mMortgage = price / 2
    END SUB

    FUNCTION GetIndex() AS INTEGER
        GetIndex = mIndex
    END FUNCTION

    FUNCTION GetName() AS STRING
        GetName = mName
    END FUNCTION

    FUNCTION GetShortName() AS STRING
        DIM nameLen AS INTEGER
        nameLen = LEN(mName)
        IF nameLen > 8 THEN
            GetShortName = mName.Left(7) + "."
        ELSE
            GetShortName = mName
        END IF
    END FUNCTION

    FUNCTION GetType() AS INTEGER
        GetType = mType
    END FUNCTION

    FUNCTION GetGroup() AS INTEGER
        GetGroup = mGroup
    END FUNCTION

    FUNCTION GetPrice() AS INTEGER
        GetPrice = mPrice
    END FUNCTION

    FUNCTION GetMortgageValue() AS INTEGER
        GetMortgageValue = mMortgage
    END FUNCTION

    FUNCTION GetHouseCost() AS INTEGER
        GetHouseCost = mHouseCost
    END FUNCTION

    FUNCTION GetOwner() AS INTEGER
        GetOwner = mOwner
    END FUNCTION

    SUB SetOwner(playerIdx AS INTEGER)
        mOwner = playerIdx
    END SUB

    FUNCTION GetHouses() AS INTEGER
        GetHouses = mHouses
    END FUNCTION

    SUB SetHouses(numHouses AS INTEGER)
        mHouses = numHouses
    END SUB

    FUNCTION IsMortgaged() AS INTEGER
        IsMortgaged = mMortgaged
    END FUNCTION

    SUB SetMortgaged(isMortgaged AS INTEGER)
        mMortgaged = isMortgaged
    END SUB

    FUNCTION IsOwnable() AS INTEGER
        IF mType = SPACE_PROPERTY OR mType = SPACE_RAILROAD OR mType = SPACE_UTILITY THEN
            IsOwnable = 1
        ELSE
            IsOwnable = 0
        END IF
    END FUNCTION

    FUNCTION CalculateRent(diceTotal AS INTEGER, sameGroupOwned AS INTEGER, totalInGroup AS INTEGER) AS INTEGER
        DIM rent AS INTEGER
        rent = 0

        IF mMortgaged = 1 THEN
            CalculateRent = 0
            RETURN
        END IF

        IF mType = SPACE_PROPERTY THEN
            IF mHouses = 0 THEN
                rent = mRent
                IF sameGroupOwned = totalInGroup THEN
                    rent = rent * 2
                END IF
            ELSE IF mHouses = 1 THEN
                rent = mRent1
            ELSE IF mHouses = 2 THEN
                rent = mRent2
            ELSE IF mHouses = 3 THEN
                rent = mRent3
            ELSE IF mHouses = 4 THEN
                rent = mRent4
            ELSE IF mHouses = 5 THEN
                rent = mRentHotel
            END IF
        ELSE IF mType = SPACE_RAILROAD THEN
            IF sameGroupOwned = 1 THEN
                rent = 25
            ELSE IF sameGroupOwned = 2 THEN
                rent = 50
            ELSE IF sameGroupOwned = 3 THEN
                rent = 100
            ELSE IF sameGroupOwned = 4 THEN
                rent = 200
            END IF
        ELSE IF mType = SPACE_UTILITY THEN
            IF sameGroupOwned = 1 THEN
                rent = diceTotal * 4
            ELSE
                rent = diceTotal * 10
            END IF
        END IF

        CalculateRent = rent
    END FUNCTION

    FUNCTION GetColorCode() AS STRING
        DIM result AS STRING
        result = "[48;5;255m"

        IF mGroup = GROUP_BROWN THEN
            result = "[48;5;94m"
        ELSE IF mGroup = GROUP_LIGHT_BLUE THEN
            result = "[48;5;117m"
        ELSE IF mGroup = GROUP_PINK THEN
            result = "[48;5;205m"
        ELSE IF mGroup = GROUP_ORANGE THEN
            result = "[48;5;208m"
        ELSE IF mGroup = GROUP_RED THEN
            result = "[48;5;196m"
        ELSE IF mGroup = GROUP_YELLOW THEN
            result = "[48;5;226m"
        ELSE IF mGroup = GROUP_GREEN THEN
            result = "[48;5;34m"
        ELSE IF mGroup = GROUP_DARK_BLUE THEN
            result = "[48;5;21m"
        ELSE IF mGroup = GROUP_RAILROAD THEN
            result = "[48;5;240m"
        ELSE IF mGroup = GROUP_UTILITY THEN
            result = "[48;5;250m"
        END IF

        GetColorCode = result
    END FUNCTION
END CLASS

' ====================================================================
' Board array - all 40 spaces
' ====================================================================
DIM gBoard(40) AS Space

' Count of properties in each group
DIM gGroupCounts(11) AS INTEGER

' ====================================================================
' Initialize the board with all 40 spaces
' ====================================================================
SUB InitBoard()
    DIM i AS INTEGER

    ' Initialize group counts
    gGroupCounts(GROUP_BROWN) = 2
    gGroupCounts(GROUP_LIGHT_BLUE) = 3
    gGroupCounts(GROUP_PINK) = 3
    gGroupCounts(GROUP_ORANGE) = 3
    gGroupCounts(GROUP_RED) = 3
    gGroupCounts(GROUP_YELLOW) = 3
    gGroupCounts(GROUP_GREEN) = 3
    gGroupCounts(GROUP_DARK_BLUE) = 2
    gGroupCounts(GROUP_RAILROAD) = 4
    gGroupCounts(GROUP_UTILITY) = 2

    ' Create all 40 spaces
    FOR i = 0 TO 39
        gBoard(i) = NEW Space()
    NEXT i

    ' === BOTTOM ROW (0-10) ===
    gBoard(0).Init(0, "GO", SPACE_GO, GROUP_NONE)

    gBoard(1).Init(1, "Mediterr.", SPACE_PROPERTY, GROUP_BROWN)
    gBoard(1).SetPricing(60, 2, 10, 30, 90, 160, 250, 50)

    gBoard(2).Init(2, "Community", SPACE_CHEST, GROUP_NONE)

    gBoard(3).Init(3, "Baltic", SPACE_PROPERTY, GROUP_BROWN)
    gBoard(3).SetPricing(60, 4, 20, 60, 180, 320, 450, 50)

    gBoard(4).Init(4, "Income Tax", SPACE_TAX, GROUP_NONE)

    gBoard(5).Init(5, "Reading", SPACE_RAILROAD, GROUP_RAILROAD)
    gBoard(5).SetPricing(200, 25, 0, 0, 0, 0, 0, 0)

    gBoard(6).Init(6, "Oriental", SPACE_PROPERTY, GROUP_LIGHT_BLUE)
    gBoard(6).SetPricing(100, 6, 30, 90, 270, 400, 550, 50)

    gBoard(7).Init(7, "Chance", SPACE_CHANCE, GROUP_NONE)

    gBoard(8).Init(8, "Vermont", SPACE_PROPERTY, GROUP_LIGHT_BLUE)
    gBoard(8).SetPricing(100, 6, 30, 90, 270, 400, 550, 50)

    gBoard(9).Init(9, "Connectic.", SPACE_PROPERTY, GROUP_LIGHT_BLUE)
    gBoard(9).SetPricing(120, 8, 40, 100, 300, 450, 600, 50)

    gBoard(10).Init(10, "Jail", SPACE_JAIL, GROUP_NONE)

    ' === LEFT SIDE (11-19) ===
    gBoard(11).Init(11, "St.Charles", SPACE_PROPERTY, GROUP_PINK)
    gBoard(11).SetPricing(140, 10, 50, 150, 450, 625, 750, 100)

    gBoard(12).Init(12, "Electric", SPACE_UTILITY, GROUP_UTILITY)
    gBoard(12).SetPricing(150, 0, 0, 0, 0, 0, 0, 0)

    gBoard(13).Init(13, "States", SPACE_PROPERTY, GROUP_PINK)
    gBoard(13).SetPricing(140, 10, 50, 150, 450, 625, 750, 100)

    gBoard(14).Init(14, "Virginia", SPACE_PROPERTY, GROUP_PINK)
    gBoard(14).SetPricing(160, 12, 60, 180, 500, 700, 900, 100)

    gBoard(15).Init(15, "Pennsylv.", SPACE_RAILROAD, GROUP_RAILROAD)
    gBoard(15).SetPricing(200, 25, 0, 0, 0, 0, 0, 0)

    gBoard(16).Init(16, "St.James", SPACE_PROPERTY, GROUP_ORANGE)
    gBoard(16).SetPricing(180, 14, 70, 200, 550, 750, 950, 100)

    gBoard(17).Init(17, "Community", SPACE_CHEST, GROUP_NONE)

    gBoard(18).Init(18, "Tennessee", SPACE_PROPERTY, GROUP_ORANGE)
    gBoard(18).SetPricing(180, 14, 70, 200, 550, 750, 950, 100)

    gBoard(19).Init(19, "New York", SPACE_PROPERTY, GROUP_ORANGE)
    gBoard(19).SetPricing(200, 16, 80, 220, 600, 800, 1000, 100)

    gBoard(20).Init(20, "Free Park", SPACE_FREE_PARKING, GROUP_NONE)

    ' === TOP ROW (21-30) ===
    gBoard(21).Init(21, "Kentucky", SPACE_PROPERTY, GROUP_RED)
    gBoard(21).SetPricing(220, 18, 90, 250, 700, 875, 1050, 150)

    gBoard(22).Init(22, "Chance", SPACE_CHANCE, GROUP_NONE)

    gBoard(23).Init(23, "Indiana", SPACE_PROPERTY, GROUP_RED)
    gBoard(23).SetPricing(220, 18, 90, 250, 700, 875, 1050, 150)

    gBoard(24).Init(24, "Illinois", SPACE_PROPERTY, GROUP_RED)
    gBoard(24).SetPricing(240, 20, 100, 300, 750, 925, 1100, 150)

    gBoard(25).Init(25, "B&O RR", SPACE_RAILROAD, GROUP_RAILROAD)
    gBoard(25).SetPricing(200, 25, 0, 0, 0, 0, 0, 0)

    gBoard(26).Init(26, "Atlantic", SPACE_PROPERTY, GROUP_YELLOW)
    gBoard(26).SetPricing(260, 22, 110, 330, 800, 975, 1150, 150)

    gBoard(27).Init(27, "Ventnor", SPACE_PROPERTY, GROUP_YELLOW)
    gBoard(27).SetPricing(260, 22, 110, 330, 800, 975, 1150, 150)

    gBoard(28).Init(28, "Water Wks", SPACE_UTILITY, GROUP_UTILITY)
    gBoard(28).SetPricing(150, 0, 0, 0, 0, 0, 0, 0)

    gBoard(29).Init(29, "Marvin", SPACE_PROPERTY, GROUP_YELLOW)
    gBoard(29).SetPricing(280, 24, 120, 360, 850, 1025, 1200, 150)

    gBoard(30).Init(30, "Go To Jail", SPACE_GO_TO_JAIL, GROUP_NONE)

    ' === RIGHT SIDE (31-39) ===
    gBoard(31).Init(31, "Pacific", SPACE_PROPERTY, GROUP_GREEN)
    gBoard(31).SetPricing(300, 26, 130, 390, 900, 1100, 1275, 200)

    gBoard(32).Init(32, "N.Carolina", SPACE_PROPERTY, GROUP_GREEN)
    gBoard(32).SetPricing(300, 26, 130, 390, 900, 1100, 1275, 200)

    gBoard(33).Init(33, "Community", SPACE_CHEST, GROUP_NONE)

    gBoard(34).Init(34, "Pennsylv.", SPACE_PROPERTY, GROUP_GREEN)
    gBoard(34).SetPricing(320, 28, 150, 450, 1000, 1200, 1400, 200)

    gBoard(35).Init(35, "Short Line", SPACE_RAILROAD, GROUP_RAILROAD)
    gBoard(35).SetPricing(200, 25, 0, 0, 0, 0, 0, 0)

    gBoard(36).Init(36, "Chance", SPACE_CHANCE, GROUP_NONE)

    gBoard(37).Init(37, "Park Place", SPACE_PROPERTY, GROUP_DARK_BLUE)
    gBoard(37).SetPricing(350, 35, 175, 500, 1100, 1300, 1500, 200)

    gBoard(38).Init(38, "Luxury Tax", SPACE_TAX, GROUP_NONE)

    gBoard(39).Init(39, "Boardwalk", SPACE_PROPERTY, GROUP_DARK_BLUE)
    gBoard(39).SetPricing(400, 50, 200, 600, 1400, 1700, 2000, 200)
END SUB

' ====================================================================
' Helper functions
' ====================================================================

FUNCTION GetGroupCount(grp AS INTEGER) AS INTEGER
    IF grp >= 0 AND grp <= 10 THEN
        GetGroupCount = gGroupCounts(grp)
    ELSE
        GetGroupCount = 0
    END IF
END FUNCTION

FUNCTION CountOwnedInGroup(playerIdx AS INTEGER, grp AS INTEGER) AS INTEGER
    DIM count AS INTEGER
    DIM i AS INTEGER
    count = 0

    FOR i = 0 TO 39
        IF gBoard(i).GetGroup() = grp AND gBoard(i).GetOwner() = playerIdx THEN
            count = count + 1
        END IF
    NEXT i

    CountOwnedInGroup = count
END FUNCTION

FUNCTION HasMonopoly(playerIdx AS INTEGER, grp AS INTEGER) AS INTEGER
    IF grp = GROUP_NONE OR grp = GROUP_RAILROAD OR grp = GROUP_UTILITY THEN
        HasMonopoly = 0
    ELSE
        IF CountOwnedInGroup(playerIdx, grp) = GetGroupCount(grp) THEN
            HasMonopoly = 1
        ELSE
            HasMonopoly = 0
        END IF
    END IF
END FUNCTION

FUNCTION GetSpace(idx AS INTEGER) AS Space
    IF idx >= 0 AND idx <= 39 THEN
        GetSpace = gBoard(idx)
    ELSE
        GetSpace = gBoard(0)
    END IF
END FUNCTION
