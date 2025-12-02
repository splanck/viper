' player.bas - Player class for Monopoly
' Represents a player (human or AI)

CLASS Player
    DIM playerName AS STRING
    DIM playerToken AS STRING
    DIM playerColor AS INTEGER
    DIM playerPosition AS INTEGER
    DIM playerMoney AS INTEGER
    DIM playerJailTurns AS INTEGER
    DIM playerHasJailCard AS INTEGER
    DIM playerIsAI AS INTEGER
    DIM playerStrategy AS INTEGER
    DIM playerIsBankrupt AS INTEGER
    DIM playerIndex AS INTEGER

    ' Property indices owned (stored as comma-separated string for simplicity)
    ' We'll use helper functions to manage this
    DIM ownedProps AS STRING

    SUB Init(nm AS STRING, tok AS STRING, clr AS INTEGER, idx AS INTEGER, isAI AS INTEGER, strat AS INTEGER)
        playerName = nm
        playerToken = tok
        playerColor = clr
        playerIndex = idx
        playerPosition = 0
        playerMoney = STARTING_MONEY
        playerJailTurns = 0
        playerHasJailCard = 0
        playerIsAI = isAI
        playerStrategy = strat
        playerIsBankrupt = 0
        ownedProps = ""
    END SUB

    FUNCTION GetName() AS STRING
        GetName = playerName
    END FUNCTION

    FUNCTION GetToken() AS STRING
        GetToken = playerToken
    END FUNCTION

    FUNCTION GetColor() AS INTEGER
        GetColor = playerColor
    END FUNCTION

    FUNCTION GetPosition() AS INTEGER
        GetPosition = playerPosition
    END FUNCTION

    SUB SetPosition(pos AS INTEGER)
        playerPosition = pos
    END SUB

    FUNCTION GetMoney() AS INTEGER
        GetMoney = playerMoney
    END FUNCTION

    SUB SetMoney(amt AS INTEGER)
        playerMoney = amt
    END SUB

    SUB AddMoney(amt AS INTEGER)
        playerMoney = playerMoney + amt
    END SUB

    SUB SubtractMoney(amt AS INTEGER)
        playerMoney = playerMoney - amt
    END SUB

    FUNCTION GetJailTurns() AS INTEGER
        GetJailTurns = playerJailTurns
    END FUNCTION

    SUB SetJailTurns(t AS INTEGER)
        playerJailTurns = t
    END SUB

    FUNCTION HasJailCard() AS INTEGER
        HasJailCard = playerHasJailCard
    END FUNCTION

    SUB SetJailCard(has AS INTEGER)
        playerHasJailCard = has
    END SUB

    FUNCTION IsAI() AS INTEGER
        IsAI = playerIsAI
    END FUNCTION

    FUNCTION GetStrategy() AS INTEGER
        GetStrategy = playerStrategy
    END FUNCTION

    FUNCTION IsBankrupt() AS INTEGER
        IsBankrupt = playerIsBankrupt
    END FUNCTION

    SUB SetBankrupt(b AS INTEGER)
        playerIsBankrupt = b
    END SUB

    FUNCTION GetIndex() AS INTEGER
        GetIndex = playerIndex
    END FUNCTION

    FUNCTION IsInJail() AS INTEGER
        IsInJail = 0
        IF playerJailTurns > 0 THEN IsInJail = 1
    END FUNCTION

    SUB SendToJail()
        playerPosition = JAIL_POSITION
        playerJailTurns = 1
    END SUB

    SUB ReleaseFromJail()
        playerJailTurns = 0
    END SUB

    ' Move player by dice roll, handle passing GO
    FUNCTION MoveBy(spaces AS INTEGER) AS INTEGER
        DIM newPos AS INTEGER
        DIM passedGo AS INTEGER
        passedGo = 0

        newPos = playerPosition + spaces
        IF newPos >= 40 THEN
            newPos = newPos - 40
            passedGo = 1
        END IF
        playerPosition = newPos
        MoveBy = passedGo
    END FUNCTION

    ' Move player to specific position, optionally collecting GO
    FUNCTION MoveTo(pos AS INTEGER, collectGo AS INTEGER) AS INTEGER
        DIM passedGo AS INTEGER
        passedGo = 0

        IF collectGo = 1 THEN
            IF pos < playerPosition THEN
                passedGo = 1
            END IF
        END IF
        playerPosition = pos
        MoveTo = passedGo
    END FUNCTION

    ' Property tracking using string (workaround for List storing objects only)
    SUB AddProperty(propIdx AS INTEGER)
        IF LEN(ownedProps) = 0 THEN
            ownedProps = STR$(propIdx)
        ELSE
            ownedProps = ownedProps + "," + STR$(propIdx)
        END IF
    END SUB

    SUB RemoveProperty(propIdx AS INTEGER)
        DIM newProps AS STRING
        DIM i AS INTEGER
        DIM part AS STRING
        DIM found AS INTEGER
        DIM commaPos AS INTEGER
        DIM remaining AS STRING

        newProps = ""
        remaining = ownedProps

        DO WHILE LEN(remaining) > 0
            commaPos = 0
            FOR i = 1 TO LEN(remaining)
                IF MID$(remaining, i, 1) = "," THEN
                    commaPos = i
                    EXIT FOR
                END IF
            NEXT i

            IF commaPos > 0 THEN
                part = LEFT$(remaining, commaPos - 1)
                remaining = MID$(remaining, commaPos + 1)
            ELSE
                part = remaining
                remaining = ""
            END IF

            part = LTRIM$(part)
            IF VAL(part) <> propIdx THEN
                IF LEN(newProps) = 0 THEN
                    newProps = part
                ELSE
                    newProps = newProps + "," + part
                END IF
            END IF
        LOOP

        ownedProps = newProps
    END SUB

    FUNCTION GetOwnedPropsString() AS STRING
        GetOwnedPropsString = ownedProps
    END FUNCTION

    FUNCTION GetPropertyCount() AS INTEGER
        DIM cnt AS INTEGER
        DIM i AS INTEGER

        IF LEN(ownedProps) = 0 THEN
            GetPropertyCount = 0
            EXIT FUNCTION
        END IF

        cnt = 1
        FOR i = 1 TO LEN(ownedProps)
            IF MID$(ownedProps, i, 1) = "," THEN
                cnt = cnt + 1
            END IF
        NEXT i
        GetPropertyCount = cnt
    END FUNCTION

    ' Check if player owns a specific property
    FUNCTION OwnsProperty(propIdx AS INTEGER) AS INTEGER
        DIM i AS INTEGER
        DIM part AS STRING
        DIM commaPos AS INTEGER
        DIM remaining AS STRING

        OwnsProperty = 0
        remaining = ownedProps

        DO WHILE LEN(remaining) > 0
            commaPos = 0
            FOR i = 1 TO LEN(remaining)
                IF MID$(remaining, i, 1) = "," THEN
                    commaPos = i
                    EXIT FOR
                END IF
            NEXT i

            IF commaPos > 0 THEN
                part = LEFT$(remaining, commaPos - 1)
                remaining = MID$(remaining, commaPos + 1)
            ELSE
                part = remaining
                remaining = ""
            END IF

            part = LTRIM$(part)
            IF VAL(part) = propIdx THEN
                OwnsProperty = 1
                EXIT FUNCTION
            END IF
        LOOP
    END FUNCTION

    ' Get player status string for display
    FUNCTION GetStatusString() AS STRING
        DIM st AS STRING
        st = playerName + " $" + STR$(playerMoney)
        IF playerJailTurns > 0 THEN
            st = st + " [JAIL]"
        END IF
        IF playerIsBankrupt = 1 THEN
            st = st + " [BANKRUPT]"
        END IF
        GetStatusString = st
    END FUNCTION
END CLASS

