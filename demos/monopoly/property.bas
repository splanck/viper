' property.bas - Property class for Monopoly
' Represents a property on the board

CLASS GameProperty
    DIM propName AS STRING
    DIM propGroup AS INTEGER      ' GRP_BROWN, GRP_LIGHTBLUE, etc.
    DIM propCost AS INTEGER
    DIM propMortgage AS INTEGER
    DIM propHouseCost AS INTEGER
    DIM propOwnerIdx AS INTEGER   ' -1 = unowned, 0-3 = player index
    DIM propHouses AS INTEGER     ' 0-4 houses, 5 = hotel
    DIM propMortgaged AS INTEGER  ' 0 or 1
    DIM propPosition AS INTEGER   ' Board position 0-39
    DIM propType AS INTEGER       ' SPACE_PROPERTY, SPACE_RAILROAD, SPACE_UTILITY

    ' Rent levels: base, 1house, 2house, 3house, 4house, hotel
    DIM rent0 AS INTEGER
    DIM rent1 AS INTEGER
    DIM rent2 AS INTEGER
    DIM rent3 AS INTEGER
    DIM rent4 AS INTEGER
    DIM rentHotel AS INTEGER

    SUB Init(nm AS STRING, grp AS INTEGER, cst AS INTEGER, pos AS INTEGER, typ AS INTEGER)
        propName = nm
        propGroup = grp
        propCost = cst
        propMortgage = cst / 2
        propOwnerIdx = -1
        propHouses = 0
        propMortgaged = 0
        propPosition = pos
        propType = typ

        ' Default house cost based on group
        IF grp = GRP_BROWN THEN propHouseCost = 50
        IF grp = GRP_LIGHTBLUE THEN propHouseCost = 50
        IF grp = GRP_PINK THEN propHouseCost = 100
        IF grp = GRP_ORANGE THEN propHouseCost = 100
        IF grp = GRP_RED THEN propHouseCost = 150
        IF grp = GRP_YELLOW THEN propHouseCost = 150
        IF grp = GRP_GREEN THEN propHouseCost = 200
        IF grp = GRP_DARKBLUE THEN propHouseCost = 200
        IF typ = SPACE_RAILROAD THEN propHouseCost = 0
        IF typ = SPACE_UTILITY THEN propHouseCost = 0

        ' Default rent values (will be set specifically for each property)
        rent0 = cst / 10
        rent1 = cst / 2
        rent2 = cst
        rent3 = cst * 3
        rent4 = cst * 4
        rentHotel = cst * 5
    END SUB

    SUB SetRents(r0 AS INTEGER, r1 AS INTEGER, r2 AS INTEGER, r3 AS INTEGER, r4 AS INTEGER, rH AS INTEGER)
        rent0 = r0
        rent1 = r1
        rent2 = r2
        rent3 = r3
        rent4 = r4
        rentHotel = rH
    END SUB

    FUNCTION GetName() AS STRING
        GetName = propName
    END FUNCTION

    FUNCTION GetGroup() AS INTEGER
        GetGroup = propGroup
    END FUNCTION

    FUNCTION GetCost() AS INTEGER
        GetCost = propCost
    END FUNCTION

    FUNCTION GetMortgageValue() AS INTEGER
        GetMortgageValue = propMortgage
    END FUNCTION

    FUNCTION GetHouseCost() AS INTEGER
        GetHouseCost = propHouseCost
    END FUNCTION

    FUNCTION GetOwner() AS INTEGER
        GetOwner = propOwnerIdx
    END FUNCTION

    SUB SetOwner(idx AS INTEGER)
        propOwnerIdx = idx
    END SUB

    FUNCTION GetHouses() AS INTEGER
        GetHouses = propHouses
    END FUNCTION

    SUB SetHouses(h AS INTEGER)
        propHouses = h
    END SUB

    FUNCTION IsMortgaged() AS INTEGER
        IsMortgaged = propMortgaged
    END FUNCTION

    SUB SetMortgaged(m AS INTEGER)
        propMortgaged = m
    END SUB

    FUNCTION GetPosition() AS INTEGER
        GetPosition = propPosition
    END FUNCTION

    FUNCTION GetType() AS INTEGER
        GetType = propType
    END FUNCTION

    FUNCTION GetRent(houses AS INTEGER) AS INTEGER
        IF houses = 0 THEN GetRent = rent0
        IF houses = 1 THEN GetRent = rent1
        IF houses = 2 THEN GetRent = rent2
        IF houses = 3 THEN GetRent = rent3
        IF houses = 4 THEN GetRent = rent4
        IF houses = 5 THEN GetRent = rentHotel
    END FUNCTION

    FUNCTION GetBaseRent() AS INTEGER
        GetBaseRent = rent0
    END FUNCTION

    ' Get abbreviated name for board display (max 8 chars)
    FUNCTION GetShortName() AS STRING
        DIM nm AS STRING
        nm = propName
        IF LEN(nm) > 8 THEN
            nm = LEFT$(nm, 7)
        END IF
        GetShortName = nm
    END FUNCTION

    ' Get group color for terminal display
    FUNCTION GetGroupColor() AS INTEGER
        IF propGroup = GRP_BROWN THEN GetGroupColor = CLR_YELLOW
        IF propGroup = GRP_LIGHTBLUE THEN GetGroupColor = CLR_BRIGHT_CYAN
        IF propGroup = GRP_PINK THEN GetGroupColor = CLR_BRIGHT_MAGENTA
        IF propGroup = GRP_ORANGE THEN GetGroupColor = TOKEN_ORANGE
        IF propGroup = GRP_RED THEN GetGroupColor = CLR_BRIGHT_RED
        IF propGroup = GRP_YELLOW THEN GetGroupColor = CLR_BRIGHT_YELLOW
        IF propGroup = GRP_GREEN THEN GetGroupColor = CLR_BRIGHT_GREEN
        IF propGroup = GRP_DARKBLUE THEN GetGroupColor = CLR_BRIGHT_BLUE
        IF propGroup = GRP_RAILROAD THEN GetGroupColor = CLR_WHITE
        IF propGroup = GRP_UTILITY THEN GetGroupColor = CLR_GRAY
    END FUNCTION

    FUNCTION IsOwned() AS INTEGER
        IsOwned = 0
        IF propOwnerIdx >= 0 THEN IsOwned = 1
    END FUNCTION

    FUNCTION CanBuild() AS INTEGER
        CanBuild = 0
        IF propType = SPACE_PROPERTY THEN
            IF propMortgaged = 0 THEN
                IF propHouses < 5 THEN
                    CanBuild = 1
                END IF
            END IF
        END IF
    END FUNCTION
END CLASS

' Alias for use in other files - renamed from Property to avoid keyword conflict

