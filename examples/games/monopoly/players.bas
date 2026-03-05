' ====================================================================
' Player Management for Monopoly
' ====================================================================

' Player tokens for display
DIM TOKEN_HUMAN AS STRING
DIM TOKEN_ANDY AS STRING
DIM TOKEN_BETTY AS STRING
DIM TOKEN_CHIP AS STRING
TOKEN_HUMAN = "@"
TOKEN_ANDY = "A"
TOKEN_BETTY = "B"
TOKEN_CHIP = "C"

' ====================================================================
' Player Class - simplified to avoid codegen issues
' ====================================================================
CLASS Player
    DIM mIndex AS INTEGER
    DIM mName AS STRING
    DIM mToken AS STRING
    DIM mMoney AS INTEGER
    DIM mPosition AS INTEGER
    DIM mInJail AS INTEGER
    DIM mJailTurns AS INTEGER
    DIM mBankrupt AS INTEGER
    DIM mIsHuman AS INTEGER
    DIM mGetOutOfJailCards AS INTEGER
    DIM mDoubleCount AS INTEGER
    DIM mColorCode AS STRING
    DIM mPropertyCount AS INTEGER

    SUB Init(idx AS INTEGER, nm AS STRING, tok AS STRING, isHum AS INTEGER, clr AS STRING)
        mIndex = idx
        mName = nm
        mToken = tok
        mMoney = 1500
        mPosition = 0
        mInJail = 0
        mJailTurns = 0
        mBankrupt = 0
        mIsHuman = isHum
        mGetOutOfJailCards = 0
        mDoubleCount = 0
        mColorCode = clr
        mPropertyCount = 0
    END SUB

    FUNCTION GetIndex() AS INTEGER
        GetIndex = mIndex
    END FUNCTION

    FUNCTION GetName() AS STRING
        GetName = mName
    END FUNCTION

    FUNCTION GetToken() AS STRING
        GetToken = mToken
    END FUNCTION

    FUNCTION GetColorCode() AS STRING
        GetColorCode = mColorCode
    END FUNCTION

    FUNCTION GetMoney() AS INTEGER
        GetMoney = mMoney
    END FUNCTION

    SUB AddMoney(amount AS INTEGER)
        mMoney = mMoney + amount
    END SUB

    SUB SubtractMoney(amount AS INTEGER)
        mMoney = mMoney - amount
        IF mMoney < 0 THEN
            mMoney = 0
        END IF
    END SUB

    FUNCTION CanAfford(amount AS INTEGER) AS INTEGER
        IF mMoney >= amount THEN
            CanAfford = 1
        ELSE
            CanAfford = 0
        END IF
    END FUNCTION

    FUNCTION GetPosition() AS INTEGER
        GetPosition = mPosition
    END FUNCTION

    SUB SetPosition(pos AS INTEGER)
        mPosition = pos
        IF mPosition >= 40 THEN
            mPosition = mPosition - 40
        END IF
        IF mPosition < 0 THEN
            mPosition = mPosition + 40
        END IF
    END SUB

    SUB MoveForward(numSpaces AS INTEGER)
        DIM newPos AS INTEGER
        DIM oldPos AS INTEGER
        oldPos = GetPosition()
        newPos = oldPos + numSpaces

        IF newPos >= 40 THEN
            newPos = newPos - 40
            IF mInJail = 0 THEN
                AddMoney(200)
            END IF
        END IF

        SetPosition(newPos)
    END SUB

    FUNCTION IsInJail() AS INTEGER
        IsInJail = mInJail
    END FUNCTION

    SUB GoToJail()
        mInJail = 1
        mJailTurns = 0
        mPosition = 10
        mDoubleCount = 0
    END SUB

    SUB ReleaseFromJail()
        mInJail = 0
        mJailTurns = 0
    END SUB

    FUNCTION GetJailTurns() AS INTEGER
        GetJailTurns = mJailTurns
    END FUNCTION

    SUB IncrementJailTurns()
        mJailTurns = mJailTurns + 1
    END SUB

    FUNCTION IsBankrupt() AS INTEGER
        IsBankrupt = mBankrupt
    END FUNCTION

    SUB SetBankrupt()
        mBankrupt = 1
    END SUB

    FUNCTION IsHuman() AS INTEGER
        IsHuman = mIsHuman
    END FUNCTION

    FUNCTION GetPropertyCount() AS INTEGER
        GetPropertyCount = mPropertyCount
    END FUNCTION

    SUB AddProperty(propIdx AS INTEGER)
        mPropertyCount = mPropertyCount + 1
    END SUB

    SUB RemoveProperty(propIdx AS INTEGER)
        IF mPropertyCount > 0 THEN
            mPropertyCount = mPropertyCount - 1
        END IF
    END SUB

    FUNCTION GetGetOutOfJailCards() AS INTEGER
        GetGetOutOfJailCards = mGetOutOfJailCards
    END FUNCTION

    SUB AddGetOutOfJailCard()
        mGetOutOfJailCards = mGetOutOfJailCards + 1
    END SUB

    SUB UseGetOutOfJailCard()
        IF mGetOutOfJailCards > 0 THEN
            mGetOutOfJailCards = mGetOutOfJailCards - 1
            mInJail = 0
            mJailTurns = 0
        END IF
    END SUB

    FUNCTION GetDoubleCount() AS INTEGER
        GetDoubleCount = mDoubleCount
    END FUNCTION

    SUB IncrementDoubles()
        mDoubleCount = mDoubleCount + 1
    END SUB

    SUB ResetDoubles()
        mDoubleCount = 0
    END SUB

    FUNCTION GetNetWorth() AS INTEGER
        GetNetWorth = mMoney
    END FUNCTION
END CLASS

' ====================================================================
' Global player array
' ====================================================================
DIM gPlayers(4) AS Player
DIM gCurrentPlayer AS INTEGER
DIM gActivePlayers AS INTEGER

' ====================================================================
' Initialize players
' ====================================================================
SUB InitPlayers()
    DIM i AS INTEGER

    FOR i = 0 TO 3
        gPlayers(i) = NEW Player()
    NEXT i

    gPlayers(0).Init(0, "You", TOKEN_HUMAN, 1, "[92m")
    gPlayers(1).Init(1, "Andy", TOKEN_ANDY, 0, "[91m")
    gPlayers(2).Init(2, "Betty", TOKEN_BETTY, 0, "[94m")
    gPlayers(3).Init(3, "Chip", TOKEN_CHIP, 0, "[95m")

    gCurrentPlayer = 0
    gActivePlayers = 4
END SUB

FUNCTION GetPlayer(idx AS INTEGER) AS Player
    IF idx >= 0 AND idx <= 3 THEN
        GetPlayer = gPlayers(idx)
    ELSE
        GetPlayer = gPlayers(0)
    END IF
END FUNCTION

FUNCTION GetCurrentPlayer() AS Player
    GetCurrentPlayer = gPlayers(gCurrentPlayer)
END FUNCTION

SUB NextPlayer()
    DIM startPlayer AS INTEGER
    startPlayer = gCurrentPlayer

    DO
        gCurrentPlayer = gCurrentPlayer + 1
        IF gCurrentPlayer > 3 THEN
            gCurrentPlayer = 0
        END IF

        IF gCurrentPlayer = startPlayer THEN
            EXIT DO
        END IF
    LOOP WHILE gPlayers(gCurrentPlayer).IsBankrupt() = 1
END SUB

FUNCTION CountActivePlayers() AS INTEGER
    DIM count AS INTEGER
    DIM i AS INTEGER
    count = 0

    FOR i = 0 TO 3
        IF gPlayers(i).IsBankrupt() = 0 THEN
            count = count + 1
        END IF
    NEXT i

    CountActivePlayers = count
END FUNCTION

FUNCTION FindWinner() AS INTEGER
    DIM i AS INTEGER
    DIM maxWorth AS INTEGER
    DIM winner AS INTEGER

    maxWorth = -1
    winner = 0

    FOR i = 0 TO 3
        IF gPlayers(i).IsBankrupt() = 0 THEN
            IF gPlayers(i).GetNetWorth() > maxWorth THEN
                maxWorth = gPlayers(i).GetNetWorth()
                winner = i
            END IF
        END IF
    NEXT i

    FindWinner = winner
END FUNCTION
