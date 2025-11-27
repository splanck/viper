REM ====================================================================
REM MONOPOLY - OOP Classes
REM All game entity classes for Monopoly
REM ====================================================================

REM ====================================================================
REM PropData Class - Represents a property on the board
REM ====================================================================
CLASS PropData
    DIM propName AS STRING
    DIM propGroup AS INTEGER     REM 0=Brown,1=LtBlue,2=Pink,3=Orange,4=Red,5=Yellow,6=Green,7=DkBlue,8=RR,9=Util,10=Special
    DIM propCost AS INTEGER
    DIM propRent0 AS INTEGER     REM Base rent (no houses)
    DIM propRent1 AS INTEGER     REM Rent with 1 house
    DIM propRent2 AS INTEGER     REM Rent with 2 houses
    DIM propRent3 AS INTEGER     REM Rent with 3 houses
    DIM propRent4 AS INTEGER     REM Rent with 4 houses
    DIM propRentH AS INTEGER     REM Rent with hotel
    DIM propHouseCost AS INTEGER
    DIM propOwner AS INTEGER     REM -1 = bank, 0-5 = player index
    DIM propHouses AS INTEGER    REM 0-4 houses, 5 = hotel
    DIM propMortgaged AS INTEGER REM 1 if mortgaged
    DIM propPosition AS INTEGER  REM Board position 0-39
    DIM propMortgageValue AS INTEGER

    SUB Init(pos AS INTEGER, nm AS STRING, grp AS INTEGER, cost AS INTEGER)
        propPosition = pos
        propName = nm
        propGroup = grp
        propCost = cost
        propRent0 = 0
        propRent1 = 0
        propRent2 = 0
        propRent3 = 0
        propRent4 = 0
        propRentH = 0
        propHouseCost = 0
        propOwner = -1
        propHouses = 0
        propMortgaged = 0
        propMortgageValue = cost / 2
    END SUB

    SUB SetRents(r0 AS INTEGER, r1 AS INTEGER, r2 AS INTEGER, r3 AS INTEGER, r4 AS INTEGER, rH AS INTEGER)
        propRent0 = r0
        propRent1 = r1
        propRent2 = r2
        propRent3 = r3
        propRent4 = r4
        propRentH = rH
    END SUB

    SUB SetHouseCost(hc AS INTEGER)
        propHouseCost = hc
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

    FUNCTION GetOwner() AS INTEGER
        GetOwner = propOwner
    END FUNCTION

    FUNCTION GetHouses() AS INTEGER
        GetHouses = propHouses
    END FUNCTION

    FUNCTION IsMortgaged() AS INTEGER
        IsMortgaged = propMortgaged
    END FUNCTION

    FUNCTION GetPosition() AS INTEGER
        GetPosition = propPosition
    END FUNCTION

    FUNCTION GetHouseCost() AS INTEGER
        GetHouseCost = propHouseCost
    END FUNCTION

    FUNCTION GetMortgageValue() AS INTEGER
        GetMortgageValue = propMortgageValue
    END FUNCTION

    FUNCTION GetRent() AS INTEGER
        DIM rent AS INTEGER
        IF propMortgaged = 1 THEN
            GetRent = 0
            EXIT FUNCTION
        END IF
        IF propHouses = 0 THEN
            rent = propRent0
        ELSEIF propHouses = 1 THEN
            rent = propRent1
        ELSEIF propHouses = 2 THEN
            rent = propRent2
        ELSEIF propHouses = 3 THEN
            rent = propRent3
        ELSEIF propHouses = 4 THEN
            rent = propRent4
        ELSEIF propHouses = 5 THEN
            rent = propRentH
        ELSE
            rent = propRent0
        END IF
        GetRent = rent
    END FUNCTION

    FUNCTION GetBaseRent() AS INTEGER
        GetBaseRent = propRent0
    END FUNCTION

    SUB SetOwner(o AS INTEGER)
        propOwner = o
    END SUB

    SUB AddHouse()
        IF propHouses < 5 THEN
            propHouses = propHouses + 1
        END IF
    END SUB

    SUB RemoveHouse()
        IF propHouses > 0 THEN
            propHouses = propHouses - 1
        END IF
    END SUB

    SUB Mortgage()
        propMortgaged = 1
        propHouses = 0
    END SUB

    SUB Unmortgage()
        propMortgaged = 0
    END SUB

    SUB Reset()
        propOwner = -1
        propHouses = 0
        propMortgaged = 0
    END SUB
END CLASS

REM ====================================================================
REM Card Class - Chance or Community Chest card
REM ====================================================================
CLASS Card
    DIM cardText AS STRING
    DIM cardAction AS INTEGER    REM 0=money,1=move,2=moveTo,3=jail,4=jailFree,5=repairs,6=payEach,7=collectEach
    DIM cardValue AS INTEGER
    DIM cardValue2 AS INTEGER    REM For repairs: hotel cost

    SUB Init(txt AS STRING, act AS INTEGER, val AS INTEGER)
        cardText = txt
        cardAction = act
        cardValue = val
        cardValue2 = 0
    END SUB

    SUB SetRepairCosts(houseCost AS INTEGER, hotelCost AS INTEGER)
        cardValue = houseCost
        cardValue2 = hotelCost
    END SUB

    FUNCTION GetText() AS STRING
        GetText = cardText
    END FUNCTION

    FUNCTION GetAction() AS INTEGER
        GetAction = cardAction
    END FUNCTION

    FUNCTION GetValue() AS INTEGER
        GetValue = cardValue
    END FUNCTION

    FUNCTION GetValue2() AS INTEGER
        GetValue2 = cardValue2
    END FUNCTION
END CLASS

REM ====================================================================
REM Player Class - A player in the game
REM ====================================================================
CLASS Player
    DIM playerName AS STRING
    DIM playerMoney AS INTEGER
    DIM playerPosition AS INTEGER
    DIM playerInJail AS INTEGER
    DIM playerJailTurns AS INTEGER
    DIM playerJailFreeCards AS INTEGER
    DIM playerBankrupt AS INTEGER
    DIM playerIsAI AS INTEGER
    DIM playerAIType AS INTEGER  REM 0=aggressive,1=conservative,2=balanced
    DIM playerToken AS STRING
    DIM playerIndex AS INTEGER
    DIM playerDoubles AS INTEGER REM Count of consecutive doubles

    SUB Init(idx AS INTEGER, nm AS STRING, tkn AS STRING, isAI AS INTEGER, aiType AS INTEGER)
        playerIndex = idx
        playerName = nm
        playerToken = tkn
        playerMoney = 1500
        playerPosition = 0
        playerInJail = 0
        playerJailTurns = 0
        playerJailFreeCards = 0
        playerBankrupt = 0
        playerIsAI = isAI
        playerAIType = aiType
        playerDoubles = 0
    END SUB

    FUNCTION GetName() AS STRING
        GetName = playerName
    END FUNCTION

    FUNCTION GetMoney() AS INTEGER
        GetMoney = playerMoney
    END FUNCTION

    FUNCTION GetPosition() AS INTEGER
        GetPosition = playerPosition
    END FUNCTION

    FUNCTION IsInJail() AS INTEGER
        IsInJail = playerInJail
    END FUNCTION

    FUNCTION GetJailTurns() AS INTEGER
        GetJailTurns = playerJailTurns
    END FUNCTION

    FUNCTION HasJailFreeCard() AS INTEGER
        IF playerJailFreeCards > 0 THEN
            HasJailFreeCard = 1
        ELSE
            HasJailFreeCard = 0
        END IF
    END FUNCTION

    FUNCTION IsBankrupt() AS INTEGER
        IsBankrupt = playerBankrupt
    END FUNCTION

    FUNCTION IsAI() AS INTEGER
        IsAI = playerIsAI
    END FUNCTION

    FUNCTION GetAIType() AS INTEGER
        GetAIType = playerAIType
    END FUNCTION

    FUNCTION GetToken() AS STRING
        GetToken = playerToken
    END FUNCTION

    FUNCTION GetIndex() AS INTEGER
        GetIndex = playerIndex
    END FUNCTION

    FUNCTION GetDoubles() AS INTEGER
        GetDoubles = playerDoubles
    END FUNCTION

    SUB SetPosition(pos AS INTEGER)
        playerPosition = pos
    END SUB

    SUB AddMoney(amount AS INTEGER)
        playerMoney = playerMoney + amount
    END SUB

    SUB SubtractMoney(amount AS INTEGER)
        playerMoney = playerMoney - amount
        IF playerMoney < 0 THEN
            playerMoney = 0
        END IF
    END SUB

    SUB GoToJail()
        playerInJail = 1
        playerJailTurns = 0
        playerPosition = 10
    END SUB

    SUB ReleaseFromJail()
        playerInJail = 0
        playerJailTurns = 0
    END SUB

    SUB IncrementJailTurns()
        playerJailTurns = playerJailTurns + 1
    END SUB

    SUB AddJailFreeCard()
        playerJailFreeCards = playerJailFreeCards + 1
    END SUB

    SUB UseJailFreeCard()
        IF playerJailFreeCards > 0 THEN
            playerJailFreeCards = playerJailFreeCards - 1
        END IF
    END SUB

    SUB GoBankrupt()
        playerBankrupt = 1
        playerMoney = 0
    END SUB

    SUB AddDouble()
        playerDoubles = playerDoubles + 1
    END SUB

    SUB ResetDoubles()
        playerDoubles = 0
    END SUB

    SUB SetMoney(amt AS INTEGER)
        playerMoney = amt
    END SUB
END CLASS

REM ====================================================================
REM TradeOffer Class - A trade proposal between players
REM ====================================================================
CLASS TradeOffer
    DIM offerFrom AS INTEGER
    DIM offerTo AS INTEGER
    DIM offerMoneyFrom AS INTEGER
    DIM offerMoneyTo AS INTEGER
    DIM offerJailCard AS INTEGER
    DIM offerActive AS INTEGER

    SUB Init(fromPlayer AS INTEGER, toPlayer AS INTEGER)
        offerFrom = fromPlayer
        offerTo = toPlayer
        offerMoneyFrom = 0
        offerMoneyTo = 0
        offerJailCard = 0
        offerActive = 1
    END SUB

    FUNCTION GetFrom() AS INTEGER
        GetFrom = offerFrom
    END FUNCTION

    FUNCTION GetTo() AS INTEGER
        GetTo = offerTo
    END FUNCTION

    FUNCTION GetMoneyFrom() AS INTEGER
        GetMoneyFrom = offerMoneyFrom
    END FUNCTION

    FUNCTION GetMoneyTo() AS INTEGER
        GetMoneyTo = offerMoneyTo
    END FUNCTION

    FUNCTION IsActive() AS INTEGER
        IsActive = offerActive
    END FUNCTION

    SUB SetMoneyFrom(amt AS INTEGER)
        offerMoneyFrom = amt
    END SUB

    SUB SetMoneyTo(amt AS INTEGER)
        offerMoneyTo = amt
    END SUB

    SUB SetJailCard(val AS INTEGER)
        offerJailCard = val
    END SUB

    SUB Cancel()
        offerActive = 0
    END SUB
END CLASS

REM ====================================================================
REM BoardSpace Class - A space on the board (wrapper for display)
REM ====================================================================
CLASS BoardSpace
    DIM spaceType AS INTEGER     REM 0=property,1=railroad,2=utility,3=chance,4=chest,5=tax,6=go,7=jail,8=parking,9=goToJail
    DIM spaceName AS STRING
    DIM spacePosition AS INTEGER
    DIM spacePropIndex AS INTEGER REM Index into properties list if applicable

    SUB Init(pos AS INTEGER, nm AS STRING, typ AS INTEGER, propIdx AS INTEGER)
        spacePosition = pos
        spaceName = nm
        spaceType = typ
        spacePropIndex = propIdx
    END SUB

    FUNCTION GetType() AS INTEGER
        GetType = spaceType
    END FUNCTION

    FUNCTION GetName() AS STRING
        GetName = spaceName
    END FUNCTION

    FUNCTION GetPosition() AS INTEGER
        GetPosition = spacePosition
    END FUNCTION

    FUNCTION GetPropertyIndex() AS INTEGER
        GetPropertyIndex = spacePropIndex
    END FUNCTION
END CLASS

REM ====================================================================
REM GameStats Class - Track game statistics
REM ====================================================================
CLASS GameStats
    DIM totalTurns AS INTEGER
    DIM totalRentPaid AS INTEGER
    DIM totalHousesBuilt AS INTEGER
    DIM totalHotelsBuilt AS INTEGER
    DIM totalBankruptcies AS INTEGER
    DIM mostExpensiveRent AS INTEGER
    DIM mostLandedSpace AS INTEGER
    DIM landCounts(39) AS INTEGER

    SUB Init()
        totalTurns = 0
        totalRentPaid = 0
        totalHousesBuilt = 0
        totalHotelsBuilt = 0
        totalBankruptcies = 0
        mostExpensiveRent = 0
        mostLandedSpace = 0
    END SUB

    SUB RecordTurn()
        totalTurns = totalTurns + 1
    END SUB

    SUB RecordRent(amount AS INTEGER)
        totalRentPaid = totalRentPaid + amount
        IF amount > mostExpensiveRent THEN
            mostExpensiveRent = amount
        END IF
    END SUB

    SUB RecordHouse()
        totalHousesBuilt = totalHousesBuilt + 1
    END SUB

    SUB RecordHotel()
        totalHotelsBuilt = totalHotelsBuilt + 1
    END SUB

    SUB RecordBankruptcy()
        totalBankruptcies = totalBankruptcies + 1
    END SUB

    SUB RecordLanding(pos AS INTEGER)
        landCounts(pos) = landCounts(pos) + 1
    END SUB

    FUNCTION GetTotalTurns() AS INTEGER
        GetTotalTurns = totalTurns
    END FUNCTION

    FUNCTION GetTotalRent() AS INTEGER
        GetTotalRent = totalRentPaid
    END FUNCTION
END CLASS

