' board.bas - Board setup and space definitions for Monopoly

CLASS BoardSpace
    DIM spaceName AS STRING
    DIM spaceType AS INTEGER
    DIM spacePosition AS INTEGER
    DIM propertyIndex AS INTEGER   ' -1 if not a property

    SUB Init(nm AS STRING, typ AS INTEGER, pos AS INTEGER, propIdx AS INTEGER)
        spaceName = nm
        spaceType = typ
        spacePosition = pos
        propertyIndex = propIdx
    END SUB

    FUNCTION GetName() AS STRING
        GetName = spaceName
    END FUNCTION

    FUNCTION GetType() AS INTEGER
        GetType = spaceType
    END FUNCTION

    FUNCTION GetPosition() AS INTEGER
        GetPosition = spacePosition
    END FUNCTION

    FUNCTION GetPropertyIndex() AS INTEGER
        GetPropertyIndex = propertyIndex
    END FUNCTION

    FUNCTION IsProperty() AS INTEGER
        IsProperty = 0
        IF spaceType = SPACE_PROPERTY THEN IsProperty = 1
        IF spaceType = SPACE_RAILROAD THEN IsProperty = 1
        IF spaceType = SPACE_UTILITY THEN IsProperty = 1
    END FUNCTION
END CLASS

' Board manager class
CLASS GameBoard
    ' All 40 spaces
    DIM spaces(39) AS BoardSpace

    ' All 28 properties (22 regular + 4 railroads + 2 utilities)
    DIM properties(27) AS GameProperty

    ' Count properties
    DIM propertyCount AS INTEGER

    SUB Init()
        propertyCount = 0
        Me.SetupSpaces()
        Me.SetupProperties()
    END SUB

    SUB SetupSpaces()
        DIM i AS INTEGER

        ' Create all 40 spaces
        FOR i = 0 TO 39
            spaces(i) = NEW BoardSpace()
        NEXT i

        ' Initialize each space
        ' Row 0 (bottom): GO to Jail
        spaces(0).Init("GO", SPACE_GO, 0, -1)
        spaces(1).Init("Mediterran", SPACE_PROPERTY, 1, 0)
        spaces(2).Init("Comm Chest", SPACE_CHEST, 2, -1)
        spaces(3).Init("Baltic Ave", SPACE_PROPERTY, 3, 1)
        spaces(4).Init("Income Tax", SPACE_TAX, 4, -1)
        spaces(5).Init("Reading RR", SPACE_RAILROAD, 5, 2)
        spaces(6).Init("Oriental", SPACE_PROPERTY, 6, 3)
        spaces(7).Init("Chance", SPACE_CHANCE, 7, -1)
        spaces(8).Init("Vermont", SPACE_PROPERTY, 8, 4)
        spaces(9).Init("Connectcut", SPACE_PROPERTY, 9, 5)

        ' Row 1 (left): Jail to Free Parking
        spaces(10).Init("Jail", SPACE_JAIL, 10, -1)
        spaces(11).Init("St Charles", SPACE_PROPERTY, 11, 6)
        spaces(12).Init("Electric", SPACE_UTILITY, 12, 7)
        spaces(13).Init("States Ave", SPACE_PROPERTY, 13, 8)
        spaces(14).Init("Virginia", SPACE_PROPERTY, 14, 9)
        spaces(15).Init("Penn RR", SPACE_RAILROAD, 15, 10)
        spaces(16).Init("St James", SPACE_PROPERTY, 16, 11)
        spaces(17).Init("Comm Chest", SPACE_CHEST, 17, -1)
        spaces(18).Init("Tennessee", SPACE_PROPERTY, 18, 12)
        spaces(19).Init("New York", SPACE_PROPERTY, 19, 13)

        ' Row 2 (top): Free Parking to Go To Jail
        spaces(20).Init("Free Park", SPACE_FREEPARKING, 20, -1)
        spaces(21).Init("Kentucky", SPACE_PROPERTY, 21, 14)
        spaces(22).Init("Chance", SPACE_CHANCE, 22, -1)
        spaces(23).Init("Indiana", SPACE_PROPERTY, 23, 15)
        spaces(24).Init("Illinois", SPACE_PROPERTY, 24, 16)
        spaces(25).Init("B&O RR", SPACE_RAILROAD, 25, 17)
        spaces(26).Init("Atlantic", SPACE_PROPERTY, 26, 18)
        spaces(27).Init("Ventnor", SPACE_PROPERTY, 27, 19)
        spaces(28).Init("Water Work", SPACE_UTILITY, 28, 20)
        spaces(29).Init("Marvin Gdn", SPACE_PROPERTY, 29, 21)

        ' Row 3 (right): Go To Jail to Boardwalk
        spaces(30).Init("Go To Jail", SPACE_GOTOJAIL, 30, -1)
        spaces(31).Init("Pacific", SPACE_PROPERTY, 31, 22)
        spaces(32).Init("N Carolina", SPACE_PROPERTY, 32, 23)
        spaces(33).Init("Comm Chest", SPACE_CHEST, 33, -1)
        spaces(34).Init("Pennsylvan", SPACE_PROPERTY, 34, 24)
        spaces(35).Init("Short Line", SPACE_RAILROAD, 35, 25)
        spaces(36).Init("Chance", SPACE_CHANCE, 36, -1)
        spaces(37).Init("Park Place", SPACE_PROPERTY, 37, 26)
        spaces(38).Init("Luxury Tax", SPACE_TAX, 38, -1)
        spaces(39).Init("Boardwalk", SPACE_PROPERTY, 39, 27)
    END SUB

    SUB SetupProperties()
        DIM i AS INTEGER

        FOR i = 0 TO 27
            properties(i) = NEW GameProperty()
        NEXT i

        ' Brown properties
        properties(0).Init("Mediterranean Ave", GRP_BROWN, 60, 1, SPACE_PROPERTY)
        properties(0).SetRents(2, 10, 30, 90, 160, 250)

        properties(1).Init("Baltic Avenue", GRP_BROWN, 60, 3, SPACE_PROPERTY)
        properties(1).SetRents(4, 20, 60, 180, 320, 450)

        ' Railroads
        properties(2).Init("Reading Railroad", GRP_RAILROAD, 200, 5, SPACE_RAILROAD)
        properties(2).SetRents(25, 50, 100, 200, 200, 200)

        ' Light Blue properties
        properties(3).Init("Oriental Avenue", GRP_LIGHTBLUE, 100, 6, SPACE_PROPERTY)
        properties(3).SetRents(6, 30, 90, 270, 400, 550)

        properties(4).Init("Vermont Avenue", GRP_LIGHTBLUE, 100, 8, SPACE_PROPERTY)
        properties(4).SetRents(6, 30, 90, 270, 400, 550)

        properties(5).Init("Connecticut Ave", GRP_LIGHTBLUE, 120, 9, SPACE_PROPERTY)
        properties(5).SetRents(8, 40, 100, 300, 450, 600)

        ' Pink properties
        properties(6).Init("St. Charles Place", GRP_PINK, 140, 11, SPACE_PROPERTY)
        properties(6).SetRents(10, 50, 150, 450, 625, 750)

        ' Electric Company
        properties(7).Init("Electric Company", GRP_UTILITY, 150, 12, SPACE_UTILITY)
        properties(7).SetRents(4, 10, 10, 10, 10, 10)

        properties(8).Init("States Avenue", GRP_PINK, 140, 13, SPACE_PROPERTY)
        properties(8).SetRents(10, 50, 150, 450, 625, 750)

        properties(9).Init("Virginia Avenue", GRP_PINK, 160, 14, SPACE_PROPERTY)
        properties(9).SetRents(12, 60, 180, 500, 700, 900)

        ' Pennsylvania Railroad
        properties(10).Init("Pennsylvania RR", GRP_RAILROAD, 200, 15, SPACE_RAILROAD)
        properties(10).SetRents(25, 50, 100, 200, 200, 200)

        ' Orange properties
        properties(11).Init("St. James Place", GRP_ORANGE, 180, 16, SPACE_PROPERTY)
        properties(11).SetRents(14, 70, 200, 550, 750, 950)

        properties(12).Init("Tennessee Avenue", GRP_ORANGE, 180, 18, SPACE_PROPERTY)
        properties(12).SetRents(14, 70, 200, 550, 750, 950)

        properties(13).Init("New York Avenue", GRP_ORANGE, 200, 19, SPACE_PROPERTY)
        properties(13).SetRents(16, 80, 220, 600, 800, 1000)

        ' Red properties
        properties(14).Init("Kentucky Avenue", GRP_RED, 220, 21, SPACE_PROPERTY)
        properties(14).SetRents(18, 90, 250, 700, 875, 1050)

        properties(15).Init("Indiana Avenue", GRP_RED, 220, 23, SPACE_PROPERTY)
        properties(15).SetRents(18, 90, 250, 700, 875, 1050)

        properties(16).Init("Illinois Avenue", GRP_RED, 240, 24, SPACE_PROPERTY)
        properties(16).SetRents(20, 100, 300, 750, 925, 1100)

        ' B&O Railroad
        properties(17).Init("B&O Railroad", GRP_RAILROAD, 200, 25, SPACE_RAILROAD)
        properties(17).SetRents(25, 50, 100, 200, 200, 200)

        ' Yellow properties
        properties(18).Init("Atlantic Avenue", GRP_YELLOW, 260, 26, SPACE_PROPERTY)
        properties(18).SetRents(22, 110, 330, 800, 975, 1150)

        properties(19).Init("Ventnor Avenue", GRP_YELLOW, 260, 27, SPACE_PROPERTY)
        properties(19).SetRents(22, 110, 330, 800, 975, 1150)

        ' Water Works
        properties(20).Init("Water Works", GRP_UTILITY, 150, 28, SPACE_UTILITY)
        properties(20).SetRents(4, 10, 10, 10, 10, 10)

        properties(21).Init("Marvin Gardens", GRP_YELLOW, 280, 29, SPACE_PROPERTY)
        properties(21).SetRents(24, 120, 360, 850, 1025, 1200)

        ' Green properties
        properties(22).Init("Pacific Avenue", GRP_GREEN, 300, 31, SPACE_PROPERTY)
        properties(22).SetRents(26, 130, 390, 900, 1100, 1275)

        properties(23).Init("N. Carolina Ave", GRP_GREEN, 300, 32, SPACE_PROPERTY)
        properties(23).SetRents(26, 130, 390, 900, 1100, 1275)

        properties(24).Init("Pennsylvania Ave", GRP_GREEN, 320, 34, SPACE_PROPERTY)
        properties(24).SetRents(28, 150, 450, 1000, 1200, 1400)

        ' Short Line Railroad
        properties(25).Init("Short Line RR", GRP_RAILROAD, 200, 35, SPACE_RAILROAD)
        properties(25).SetRents(25, 50, 100, 200, 200, 200)

        ' Dark Blue properties
        properties(26).Init("Park Place", GRP_DARKBLUE, 350, 37, SPACE_PROPERTY)
        properties(26).SetRents(35, 175, 500, 1100, 1300, 1500)

        properties(27).Init("Boardwalk", GRP_DARKBLUE, 400, 39, SPACE_PROPERTY)
        properties(27).SetRents(50, 200, 600, 1400, 1700, 2000)

        propertyCount = 28
    END SUB

    FUNCTION GetSpace(pos AS INTEGER) AS BoardSpace
        GetSpace = spaces(pos)
    END FUNCTION

    FUNCTION GetProperty(idx AS INTEGER) AS GameProperty
        GetProperty = properties(idx)
    END FUNCTION

    FUNCTION GetPropertyByPosition(pos AS INTEGER) AS GameProperty
        DIM i AS INTEGER
        FOR i = 0 TO 27
            IF properties(i).GetPosition() = pos THEN
                GetPropertyByPosition = properties(i)
                EXIT FUNCTION
            END IF
        NEXT i
        GetPropertyByPosition = properties(0)
    END FUNCTION

    FUNCTION GetPropertyIndexByPosition(pos AS INTEGER) AS INTEGER
        DIM i AS INTEGER
        FOR i = 0 TO 27
            IF properties(i).GetPosition() = pos THEN
                GetPropertyIndexByPosition = i
                EXIT FUNCTION
            END IF
        NEXT i
        GetPropertyIndexByPosition = -1
    END FUNCTION

    FUNCTION GetPropertyCount() AS INTEGER
        GetPropertyCount = propertyCount
    END FUNCTION

    ' Get properties in a group
    FUNCTION CountPropertiesInGroup(grp AS INTEGER) AS INTEGER
        DIM cnt AS INTEGER
        DIM i AS INTEGER
        cnt = 0
        FOR i = 0 TO 27
            IF properties(i).GetGroup() = grp THEN
                cnt = cnt + 1
            END IF
        NEXT i
        CountPropertiesInGroup = cnt
    END FUNCTION

    ' Get number of properties a player owns in a group
    FUNCTION CountPlayerPropertiesInGroup(playerIdx AS INTEGER, grp AS INTEGER) AS INTEGER
        DIM cnt AS INTEGER
        DIM i AS INTEGER
        cnt = 0
        FOR i = 0 TO 27
            IF properties(i).GetGroup() = grp THEN
                IF properties(i).GetOwner() = playerIdx THEN
                    cnt = cnt + 1
                END IF
            END IF
        NEXT i
        CountPlayerPropertiesInGroup = cnt
    END FUNCTION

    ' Check if player has monopoly in group
    FUNCTION PlayerHasMonopoly(playerIdx AS INTEGER, grp AS INTEGER) AS INTEGER
        DIM total AS INTEGER
        DIM owned AS INTEGER
        total = Me.CountPropertiesInGroup(grp)
        owned = Me.CountPlayerPropertiesInGroup(playerIdx, grp)
        PlayerHasMonopoly = 0
        IF owned = total THEN
            IF total > 0 THEN
                PlayerHasMonopoly = 1
            END IF
        END IF
    END FUNCTION

    ' Count railroads owned by player
    FUNCTION CountRailroads(playerIdx AS INTEGER) AS INTEGER
        CountRailroads = Me.CountPlayerPropertiesInGroup(playerIdx, GRP_RAILROAD)
    END FUNCTION

    ' Count utilities owned by player
    FUNCTION CountUtilities(playerIdx AS INTEGER) AS INTEGER
        CountUtilities = Me.CountPlayerPropertiesInGroup(playerIdx, GRP_UTILITY)
    END FUNCTION

    ' Find nearest railroad from position
    FUNCTION FindNearestRailroad(pos AS INTEGER) AS INTEGER
        IF pos < 5 THEN FindNearestRailroad = 5
        IF pos >= 5 THEN
            IF pos < 15 THEN FindNearestRailroad = 15
        END IF
        IF pos >= 15 THEN
            IF pos < 25 THEN FindNearestRailroad = 25
        END IF
        IF pos >= 25 THEN
            IF pos < 35 THEN FindNearestRailroad = 35
        END IF
        IF pos >= 35 THEN FindNearestRailroad = 5
    END FUNCTION

    ' Find nearest utility from position
    FUNCTION FindNearestUtility(pos AS INTEGER) AS INTEGER
        IF pos < 12 THEN FindNearestUtility = 12
        IF pos >= 12 THEN
            IF pos < 28 THEN FindNearestUtility = 28
        END IF
        IF pos >= 28 THEN FindNearestUtility = 12
    END FUNCTION
END CLASS

